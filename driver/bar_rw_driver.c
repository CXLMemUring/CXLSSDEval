/*
 * Simple BAR Read/Write Driver for device 15:00.0
 *
 * Hardware automatically translates 64-byte writes to BAR into memory operations
 *
 * Memory Layout:
 * BAR0/1: 16TB VMEM space
 * BAR2/3: 8GB
 *   0x0_0000_0000 - 0x0_0000_ffff: cfg reg (64KB)
 *   0x0_0001_0000 - 0x0_0001_ffff: m2b reg (64KB) - 64-byte command interface
 *   0x1_0000_0000 - 0x1_ffff_ffff: ssd init mem DMA (4GB)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/bio.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>

#define DRIVER_NAME "bar_rw"
#define DRIVER_VERSION "1.0"

/* Device identification */
#define VENDOR_ID 0x1172 /* Altera Corporation */
#define DEVICE_ID 0x0000 /* Altera Device */

/* BAR definitions */
#define BAR0_SIZE (4ULL * 1024 * 1024 * 1024) /* 4GB for device 16:00.0 */
                                              /* 16TB for device 15:00.0 */

/* Register offsets in BAR0 */
#define CFG_REG_BASE 0x00000000
#define M2B_REG_BASE 0x00010000 /* 64-byte command interface */
#define DMA_MEM_BASE 0x00100000 /* DMA area */

/* Command structure - 64 bytes */
struct bar_command
{
    u8 opcode;      /* 0x00: READ or WRITE */
    u8 flags;       /* 0x01: flags */
    u16 reserved1;  /* 0x02-0x03 */
    u32 length;     /* 0x04-0x07: transfer length */
    u64 lba;        /* 0x08-0x0F: logical block address */
    u64 dma_addr;   /* 0x10-0x17: DMA address */
    u8 padding[40]; /* 0x18-0x3F: padding to 64 bytes */
} __packed;

/* Opcodes */
#define BAR_CMD_READ 0x01
#define BAR_CMD_WRITE 0x02

/* Global variables */
static int bar_rw_major = 0;

/* Module parameters */
static char *backend_dev = "/dev/nvme1n1";
module_param(backend_dev, charp, 0644);
MODULE_PARM_DESC(backend_dev, "Backend block device path (default: /dev/nvme1n1)");

/* Device structure */
struct bar_rw_dev
{
    struct pci_dev *pdev;

    /* Memory mapped regions */
    void __iomem *bar0_mem; /* Main BAR - contains all regions */
    void __iomem *cmd_reg;  /* M2B command register (64-byte interface) */

    /* Block device */
    struct gendisk *disk;
    struct request_queue *queue;
    struct blk_mq_tag_set tag_set;

    /* Backend block device (e.g., nvme1n1) */
    struct block_device *backend_bdev;
    struct bdev_handle *backend_handle;  /* Handle for kernel 6.8+ */
    char backend_path[256];

    /* DMA buffer */
    void *dma_buffer;
    dma_addr_t dma_handle;
    size_t dma_size;

    /* Synchronization */
    struct mutex cmd_lock;

    /* Statistics */
    atomic64_t total_reads;
    atomic64_t total_writes;
};

/* PCI device table */
static const struct pci_device_id bar_rw_id_table[] = {
    {PCI_DEVICE(VENDOR_ID, DEVICE_ID)},
    {
        0,
    }};
MODULE_DEVICE_TABLE(pci, bar_rw_id_table);

/* Enable device BARs by setting command register */
static int enable_device_bars(struct pci_dev *pdev)
{
    u16 cmd;
    int ret;

    /* Read current command register */
    ret = pci_read_config_word(pdev, PCI_COMMAND, &cmd);
    if (ret)
        return ret;

    pr_info("%s: Current PCI command: 0x%04x\n", DRIVER_NAME, cmd);

    /* Enable Memory Space and Bus Master */
    cmd |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;

    /* Write back */
    ret = pci_write_config_word(pdev, PCI_COMMAND, cmd);
    if (ret)
        return ret;

    /* Verify */
    pci_read_config_word(pdev, PCI_COMMAND, &cmd);
    pr_info("%s: Updated PCI command: 0x%04x\n", DRIVER_NAME, cmd);

    return 0;
}

/* Open backend block device */
static int open_backend_device(struct bar_rw_dev *dev, const char *path)
{
    struct bdev_handle *handle;
    sector_t capacity;
    
    if (!path || !dev)
        return -EINVAL;
    
    /* Open the backend device - kernel 6.8 uses bdev_open_by_path with bdev_handle */
    handle = bdev_open_by_path(path, BLK_OPEN_READ | BLK_OPEN_WRITE, dev, NULL);
    if (IS_ERR(handle)) {
        pr_err("%s: Failed to open backend device %s: %ld\n", 
               DRIVER_NAME, path, PTR_ERR(handle));
        return PTR_ERR(handle);
    }
    
    dev->backend_bdev = handle->bdev;
    dev->backend_handle = handle;  /* Store handle for later release */
    strncpy(dev->backend_path, path, sizeof(dev->backend_path) - 1);
    
    capacity = bdev_nr_sectors(dev->backend_bdev);
    pr_info("%s: Opened backend device %s (capacity: %llu sectors = %llu GB)\n",
            DRIVER_NAME, path, capacity, capacity >> 21);
    
    return 0;
}

/* Close backend block device */
static void close_backend_device(struct bar_rw_dev *dev)
{
    if (dev->backend_handle) {
        bdev_release(dev->backend_handle);  /* Release handle in kernel 6.8+ */
        dev->backend_handle = NULL;
        dev->backend_bdev = NULL;
        pr_info("%s: Closed backend device %s\n", DRIVER_NAME, dev->backend_path);
    }
}

/* Send 64-byte command to BAR */
static int bar_send_command(struct bar_rw_dev *dev, struct bar_command *cmd)
{
    if (!dev || !dev->cmd_reg || !cmd)
        return -EINVAL;

    /* Write 64-byte command to M2B register area */
    memcpy_toio(dev->cmd_reg, cmd, sizeof(*cmd));

    /* Hardware processes the command automatically */
    wmb(); /* Ensure write completes */

    return 0;
}

/* Perform read operation */
static int bar_do_read(struct bar_rw_dev *dev, sector_t lba,
                       unsigned int sectors, void *buffer)
{
    struct bar_command cmd = {0};
    struct bio *bio;
    struct page *page;
    int ret;

    if (!dev || !buffer)
        return -EINVAL;

    /* If we have a backend device, read from it using bio */
    if (dev->backend_bdev) {
        page = virt_to_page(buffer);
        
        bio = bio_alloc(dev->backend_bdev, 1, REQ_OP_READ | REQ_SYNC, GFP_KERNEL);
        if (!bio)
            return -ENOMEM;
            
        bio->bi_iter.bi_sector = lba;
        bio_add_page(bio, page, sectors * 512, offset_in_page(buffer));
        
        ret = submit_bio_wait(bio);
        bio_put(bio);
        
        if (ret)
            return ret;
            
        atomic64_inc(&dev->total_reads);
        return 0;
    }

    /* Fallback to BAR/DMA method */
    if (!dev->dma_buffer)
        return -EINVAL;

    cmd.opcode = BAR_CMD_READ;
    cmd.lba = lba;
    cmd.length = sectors * 512;
    cmd.dma_addr = dev->dma_handle;

    mutex_lock(&dev->cmd_lock);

    /* Send command to hardware via BAR write */
    ret = bar_send_command(dev, &cmd);
    if (ret)
    {
        mutex_unlock(&dev->cmd_lock);
        return ret;
    }

    /* Small delay for hardware to complete */
    udelay(100);

    /* Copy data from DMA buffer to destination */
    memcpy(buffer, dev->dma_buffer, sectors * 512);

    mutex_unlock(&dev->cmd_lock);

    atomic64_inc(&dev->total_reads);
    return 0;
}

/* Perform write operation */
static int bar_do_write(struct bar_rw_dev *dev, sector_t lba,
                        unsigned int sectors, const void *buffer)
{
    struct bar_command cmd = {0};
    struct bio *bio;
    struct page *page;
    int ret;

    if (!dev || !buffer)
        return -EINVAL;

    /* If we have a backend device, write to it using bio */
    if (dev->backend_bdev) {
        page = virt_to_page(buffer);
        
        bio = bio_alloc(dev->backend_bdev, 1, REQ_OP_WRITE | REQ_SYNC, GFP_KERNEL);
        if (!bio)
            return -ENOMEM;
            
        bio->bi_iter.bi_sector = lba;
        bio_add_page(bio, page, sectors * 512, offset_in_page(buffer));
        
        ret = submit_bio_wait(bio);
        bio_put(bio);
        
        if (ret)
            return ret;
            
        atomic64_inc(&dev->total_writes);
        return 0;
    }

    /* Fallback to BAR/DMA method */
    if (!dev->dma_buffer)
        return -EINVAL;

    mutex_lock(&dev->cmd_lock);

    /* Copy data to DMA buffer */
    memcpy(dev->dma_buffer, buffer, sectors * 512);

    cmd.opcode = BAR_CMD_WRITE;
    cmd.lba = lba;
    cmd.length = sectors * 512;
    cmd.dma_addr = dev->dma_handle;

    /* Send command to hardware via BAR write */
    ret = bar_send_command(dev, &cmd);
    if (ret)
    {
        mutex_unlock(&dev->cmd_lock);
        return ret;
    }

    /* Small delay for hardware to complete */
    udelay(100);

    mutex_unlock(&dev->cmd_lock);

    atomic64_inc(&dev->total_writes);
    return 0;
}

/* Block layer request handler */
static blk_status_t bar_rw_queue_rq(struct blk_mq_hw_ctx *hctx,
                                    const struct blk_mq_queue_data *bd)
{
    struct bar_rw_dev *dev = hctx->queue->queuedata;
    struct request *req = bd->rq;
    struct bio_vec bvec;
    struct req_iterator iter;
    sector_t sector = blk_rq_pos(req);
    void *buffer;
    int ret;

    /* Safety check */
    if (!dev || !dev->dma_buffer || !dev->cmd_reg)
    {
        blk_mq_end_request(req, BLK_STS_IOERR);
        return BLK_STS_IOERR;
    }

    blk_mq_start_request(req);

    /* Process each bio segment */
    rq_for_each_segment(bvec, req, iter)
    {
        buffer = kmap_atomic(bvec.bv_page);

        if (rq_data_dir(req) == WRITE)
        {
            ret = bar_do_write(dev, sector, bvec.bv_len / 512,
                               buffer + bvec.bv_offset);
        }
        else
        {
            ret = bar_do_read(dev, sector, bvec.bv_len / 512,
                              buffer + bvec.bv_offset);
        }

        kunmap_atomic(buffer);

        if (ret)
        {
            blk_mq_end_request(req, BLK_STS_IOERR);
            return BLK_STS_IOERR;
        }

        sector += bvec.bv_len / 512;
    }

    blk_mq_end_request(req, BLK_STS_OK);
    return BLK_STS_OK;
}

static const struct blk_mq_ops bar_rw_mq_ops = {
    .queue_rq = bar_rw_queue_rq,
};

/* Block device operations */
static const struct block_device_operations bar_rw_fops = {
    .owner = THIS_MODULE,
};

/* Setup block device */
static int bar_rw_setup_block(struct bar_rw_dev *dev)
{
    int ret;

    /* Initialize tag set */
    dev->tag_set.ops = &bar_rw_mq_ops;
    dev->tag_set.nr_hw_queues = 1;
    dev->tag_set.nr_maps = 1;  /* Required for kernel 6.8+ */
    dev->tag_set.queue_depth = 128;
    dev->tag_set.numa_node = NUMA_NO_NODE;
    dev->tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
    dev->tag_set.cmd_size = 0;
    dev->tag_set.driver_data = dev;

    ret = blk_mq_alloc_tag_set(&dev->tag_set);
    if (ret)
    {
        pr_err("%s: Failed to allocate tag set\n", DRIVER_NAME);
        return ret;
    }

    /* Allocate disk - kernel 6.8 uses blk_mq_alloc_disk with 2 args */
    dev->disk = blk_mq_alloc_disk(&dev->tag_set, dev);
    if (IS_ERR(dev->disk))
    {
        pr_err("%s: Failed to allocate disk\n", DRIVER_NAME);
        ret = PTR_ERR(dev->disk);
        goto free_tagset;
    }

    /* Validate that disk was properly allocated */
    if (!dev->disk)
    {
        pr_err("%s: blk_mq_alloc_disk returned NULL\n", DRIVER_NAME);
        ret = -ENOMEM;
        goto free_tagset;
    }

    /* Get queue from disk and validate */
    dev->queue = dev->disk->queue;
    if (!dev->queue)
    {
        pr_err("%s: Disk queue is NULL\n", DRIVER_NAME);
        ret = -ENOMEM;
        goto cleanup_disk;
    }
    
    /* Set queue data */
    dev->queue->queuedata = dev;
    
    /* Ensure queue private data is properly set */
    if (!dev->disk->private_data)
        dev->disk->private_data = dev;

    /* Configure disk */
    dev->disk->fops = &bar_rw_fops;
    snprintf(dev->disk->disk_name, sizeof(dev->disk->disk_name),
             "bar_rw%d", 0);
    
    /* Set the major number and first minor - allocate major if not done */
    if (bar_rw_major == 0) {
        bar_rw_major = register_blkdev(0, DRIVER_NAME);
        if (bar_rw_major < 0) {
            pr_err("%s: Failed to register block device\n", DRIVER_NAME);
            ret = bar_rw_major;
            goto cleanup_disk;
        }
        pr_info("%s: Registered block device with major %d\n", DRIVER_NAME, bar_rw_major);
    }
    
    dev->disk->major = bar_rw_major;
    dev->disk->first_minor = 0;
    dev->disk->minors = 1;

    /* Set capacity - use backend device capacity if available, otherwise default to 1GB */
    if (dev->backend_bdev) {
        sector_t capacity = bdev_nr_sectors(dev->backend_bdev);
        set_capacity(dev->disk, capacity);
        pr_info("%s: Using backend device capacity: %llu sectors (%llu GB)\n",
                DRIVER_NAME, capacity, capacity >> 21);
    } else {
        set_capacity(dev->disk, 2ULL * 1024 * 1024); /* 1GB default */
        pr_info("%s: Using default capacity: 1GB\n", DRIVER_NAME);
    }

    /* Set block sizes */
    blk_queue_logical_block_size(dev->queue, 512);
    blk_queue_physical_block_size(dev->queue, 512);

    /* Add disk */
    ret = device_add_disk(NULL, dev->disk, NULL);
    if (ret)
    {
        pr_err("%s: Failed to add disk\n", DRIVER_NAME);
        goto cleanup_disk;
    }

    pr_info("%s: Block device created: /dev/%s\n",
            DRIVER_NAME, dev->disk->disk_name);
    return 0;

cleanup_disk:
    put_disk(dev->disk);
free_tagset:
    blk_mq_free_tag_set(&dev->tag_set);
    return ret;
}

/* PCI probe */
static int bar_rw_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct bar_rw_dev *dev;
    int ret;

    pr_info("%s: Probing device %s\n", DRIVER_NAME, pci_name(pdev));

    /* Allocate device structure */
    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->pdev = pdev;
    mutex_init(&dev->cmd_lock);
    atomic64_set(&dev->total_reads, 0);
    atomic64_set(&dev->total_writes, 0);

    /* Enable PCI device */
    ret = pci_enable_device(pdev);
    if (ret)
    {
        pr_err("%s: Failed to enable PCI device\n", DRIVER_NAME);
        goto err_free_dev;
    }

    /* Enable memory access and bus master */
    pci_set_master(pdev);

    /* Explicitly enable BARs via command register */
    ret = enable_device_bars(pdev);
    if (ret)
    {
        pr_warn("%s: Failed to enable BARs via config space\n", DRIVER_NAME);
    }

    /* Set DMA mask */
    ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
    if (ret)
    {
        ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
        if (ret)
        {
            pr_err("%s: Failed to set DMA mask\n", DRIVER_NAME);
            goto err_disable_device;
        }
    }

    /* Request memory regions */
    ret = pci_request_regions(pdev, DRIVER_NAME);
    if (ret)
    {
        pr_err("%s: Failed to request PCI regions\n", DRIVER_NAME);
        goto err_disable_device;
    }

    /* Check if BARs are actually assigned by reading config space */
    {
        u32 bar0_lo, bar0_hi;
        u64 bar0_addr;
        pci_read_config_dword(pdev, PCI_BASE_ADDRESS_0, &bar0_lo);
        pci_read_config_dword(pdev, PCI_BASE_ADDRESS_1, &bar0_hi);
        bar0_addr = ((u64)bar0_hi << 32) | (bar0_lo & ~0xF);
        pr_info("%s: BAR0 address from config: 0x%llx\n", DRIVER_NAME, bar0_addr);
    }

    /* Map BAR0 - this is the only BAR available */
    dev->bar0_mem = pci_ioremap_bar(pdev, 0);
    if (!dev->bar0_mem)
    {
        pr_err("%s: Failed to map BAR0\n", DRIVER_NAME);
        ret = -ENOMEM;
        goto err_release;
    }

    /* Set command register pointer (M2B area at offset in BAR0) */
    dev->cmd_reg = dev->bar0_mem + M2B_REG_BASE;

    /* Allocate DMA buffer (1MB) */
    dev->dma_size = 1024 * 1024;
    dev->dma_buffer = dma_alloc_coherent(&pdev->dev, dev->dma_size,
                                         &dev->dma_handle, GFP_KERNEL);
    if (!dev->dma_buffer)
    {
        pr_err("%s: Failed to allocate DMA buffer\n", DRIVER_NAME);
        ret = -ENOMEM;
        goto err_unmap_bar0;
    }

    /* Open backend block device if specified */
    if (backend_dev && strlen(backend_dev) > 0) {
        ret = open_backend_device(dev, backend_dev);
        if (ret) {
            pr_warn("%s: Backend device not available, will use BAR/DMA method\n", DRIVER_NAME);
            dev->backend_bdev = NULL;  /* Ensure it's NULL on failure */
        }
    }

    /* Setup block device */
    ret = bar_rw_setup_block(dev);
    if (ret)
        goto err_free_dma;

    pci_set_drvdata(pdev, dev);

    pr_info("%s: Device initialized successfully\n", DRIVER_NAME);
    pr_info("%s: BAR0 at %p, CMD reg at %p (offset 0x%x)\n",
            DRIVER_NAME, dev->bar0_mem, dev->cmd_reg, M2B_REG_BASE);
    pr_info("%s: DMA buffer at 0x%llx\n", DRIVER_NAME,
            (unsigned long long)dev->dma_handle);

    return 0;

err_free_dma:
    dma_free_coherent(&pdev->dev, dev->dma_size,
                      dev->dma_buffer, dev->dma_handle);
err_unmap_bar0:
    if (dev->bar0_mem)
        iounmap(dev->bar0_mem);
err_release:
    pci_release_regions(pdev);
err_disable_device:
    pci_disable_device(pdev);
err_free_dev:
    kfree(dev);
    return ret;
}

/* PCI remove */
static void bar_rw_remove(struct pci_dev *pdev)
{
    struct bar_rw_dev *dev = pci_get_drvdata(pdev);

    if (!dev)
    {
        pr_warn("%s: Remove called with NULL device\n", DRIVER_NAME);
        return;
    }

    pr_info("%s: Removing device\n", DRIVER_NAME);
    pr_info("%s: Stats - Reads: %lld, Writes: %lld\n", DRIVER_NAME,
            atomic64_read(&dev->total_reads),
            atomic64_read(&dev->total_writes));

    /* Remove block device */
    if (dev->disk)
    {
        del_gendisk(dev->disk);
        put_disk(dev->disk);
    }
    
    blk_mq_free_tag_set(&dev->tag_set);

    /* Close backend device */
    close_backend_device(dev);

    /* Free DMA buffer */
    if (dev->dma_buffer)
    {
        dma_free_coherent(&pdev->dev, dev->dma_size,
                          dev->dma_buffer, dev->dma_handle);
    }

    /* Unmap BARs */
    if (dev->bar0_mem)
        iounmap(dev->bar0_mem);

    /* Release resources */
    pci_release_regions(pdev);
    pci_disable_device(pdev);

    kfree(dev);
    pr_info("%s: Device removed\n", DRIVER_NAME);
}

/* PCI driver */
static struct pci_driver bar_rw_driver = {
    .name = DRIVER_NAME,
    .id_table = bar_rw_id_table,
    .probe = bar_rw_probe,
    .remove = bar_rw_remove,
};

static int __init bar_rw_init(void)
{
    pr_info("%s: Loading driver version %s\n", DRIVER_NAME, DRIVER_VERSION);
    return pci_register_driver(&bar_rw_driver);
}

static void __exit bar_rw_exit(void)
{
    pr_info("%s: Unloading driver\n", DRIVER_NAME);
    pci_unregister_driver(&bar_rw_driver);
    
    /* Unregister block device major number */
    if (bar_rw_major > 0) {
        unregister_blkdev(bar_rw_major, DRIVER_NAME);
        pr_info("%s: Unregistered block device major %d\n", DRIVER_NAME, bar_rw_major);
        bar_rw_major = 0;
    }
}

module_init(bar_rw_init);
module_exit(bar_rw_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Simple BAR Read/Write Driver - 64-byte command interface");
MODULE_VERSION(DRIVER_VERSION);