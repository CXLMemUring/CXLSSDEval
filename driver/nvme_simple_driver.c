/*
 * Simple NVMe-compatible PCIe Driver for device 15:00.0
 * Integrates with existing NVMe subsystem
 *
 * Memory Layout:
 * BAR0/1: 16TB VMEM space
 * BAR2/3: 8GB
 *   0x0_0000_0000 - 0x0_0000_ffff: cfg reg (64KB)
 *   0x0_0001_0000 - 0x0_0001_ffff: m2b reg (64KB)
 *   0x1_0000_0000 - 0x1_ffff_ffff: ssd init mem DMA (4GB)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>

#define DRIVER_NAME "nvme_custom"
#define DRIVER_VERSION "1.0"

/* Device identification */
#define VENDOR_ID 0x1172 /* Altera Corporation */
#define DEVICE_ID 0x0000 /* Altera Device */

/* BAR definitions */
#define BAR0_BAR1_SIZE (16ULL * 1024 * 1024 * 1024 * 1024) /* 16TB */
#define BAR2_BAR3_SIZE (8ULL * 1024 * 1024 * 1024)         /* 8GB */

/* Register offsets in BAR2/3 */
#define CFG_REG_BASE 0x00000000
#define CFG_REG_SIZE 0x00010000 /* 64KB */
#define M2B_REG_BASE 0x00010000
#define M2B_REG_SIZE 0x00010000 /* 64KB */
#define DMA_MEM_BASE 0x100000000ULL
#define DMA_MEM_SIZE 0x100000000ULL /* 4GB */

/* Control registers */
#define CFG_CONTROL_REG 0x0000
#define CFG_STATUS_REG 0x0004
#define CFG_INT_ENABLE_REG 0x0008
#define CFG_INT_STATUS_REG 0x000C

#define M2B_CONTROL_REG 0x0000
#define M2B_STATUS_REG 0x0004
#define M2B_DMA_ADDR_LOW 0x0008
#define M2B_DMA_ADDR_HIGH 0x000C
#define M2B_DMA_SIZE 0x0010
#define M2B_DMA_CONTROL 0x0014

/* Status and control bits */
#define STATUS_READY BIT(0)
#define STATUS_ERROR BIT(1)
#define STATUS_DMA_DONE BIT(2)

#define CTRL_ENABLE BIT(0)
#define CTRL_RESET BIT(1)
#define CTRL_DMA_START BIT(2)

/* Character device for diagnostics */
#define DEVICE_COUNT 1
#define MINOR_BASE 0

/* Main device structure */
struct nvme_custom_dev
{
    struct pci_dev *pdev;

    /* Memory mapped regions */
    void __iomem *bar0_bar1_mem; /* 16TB VMEM space */
    void __iomem *bar2_bar3_mem; /* 8GB combined space */
    void __iomem *cfg_regs;      /* Config registers */
    void __iomem *m2b_regs;      /* M2B registers */
    void __iomem *dma_mem;       /* DMA memory region */

    /* Block device */
    struct gendisk *disk;
    struct request_queue *queue;
    struct blk_mq_tag_set tag_set;

    /* Character device for diagnostics */
    dev_t devt;
    struct cdev cdev;
    struct device *device;
    struct class *class;

    /* DMA coherent buffer */
    dma_addr_t dma_handle;
    void *dma_vaddr;
    size_t dma_size;

    /* Synchronization */
    struct mutex lock;
    wait_queue_head_t wait_queue;

    /* Device state */
    bool device_ready;
    bool dma_in_progress;

    /* Statistics */
    atomic_t ios_completed;
    unsigned long total_transfers;
};

static struct nvme_custom_dev *global_dev = NULL;

/* PCI device table */
static const struct pci_device_id nvme_custom_id_table[] = {
    {PCI_DEVICE(VENDOR_ID, DEVICE_ID)},
    {
        0,
    }};
MODULE_DEVICE_TABLE(pci, nvme_custom_id_table);

/* Register access helpers */
static inline u32 cfg_read32(struct nvme_custom_dev *dev, u32 offset)
{
    return ioread32(dev->cfg_regs + offset);
}

static inline void cfg_write32(struct nvme_custom_dev *dev, u32 offset, u32 value)
{
    iowrite32(value, dev->cfg_regs + offset);
}

static inline u32 m2b_read32(struct nvme_custom_dev *dev, u32 offset)
{
    return ioread32(dev->m2b_regs + offset);
}

static inline void m2b_write32(struct nvme_custom_dev *dev, u32 offset, u32 value)
{
    iowrite32(value, dev->m2b_regs + offset);
}

/* Device initialization */
static int nvme_custom_hw_init(struct nvme_custom_dev *dev)
{
    u32 status;
    int timeout = 1000;

    pr_info("%s: Initializing hardware\n", DRIVER_NAME);

    /* Reset device */
    cfg_write32(dev, CFG_CONTROL_REG, CTRL_RESET);
    msleep(100);

    /* Enable device */
    cfg_write32(dev, CFG_CONTROL_REG, CTRL_ENABLE);

    /* Wait for device to be ready */
    while (timeout--)
    {
        status = cfg_read32(dev, CFG_STATUS_REG);
        if (status & STATUS_READY)
        {
            dev->device_ready = true;
            pr_info("%s: Device ready\n", DRIVER_NAME);
            return 0;
        }
        msleep(10);
    }

    pr_err("%s: Device failed to initialize\n", DRIVER_NAME);
    return -ETIMEDOUT;
}

/* DMA operations */
static int nvme_custom_start_dma(struct nvme_custom_dev *dev,
                                 dma_addr_t dma_addr, size_t size)
{
    if (dev->dma_in_progress)
        return -EBUSY;

    dev->dma_in_progress = true;

    /* Configure DMA */
    m2b_write32(dev, M2B_DMA_ADDR_LOW, lower_32_bits(dma_addr));
    m2b_write32(dev, M2B_DMA_ADDR_HIGH, upper_32_bits(dma_addr));
    m2b_write32(dev, M2B_DMA_SIZE, size);

    /* Start DMA */
    m2b_write32(dev, M2B_DMA_CONTROL, CTRL_DMA_START);

    return 0;
}

static int nvme_custom_wait_dma(struct nvme_custom_dev *dev)
{
    int ret;

    ret = wait_event_interruptible_timeout(dev->wait_queue,
                                           !dev->dma_in_progress,
                                           msecs_to_jiffies(5000));
    if (ret == 0)
        return -ETIMEDOUT;
    if (ret < 0)
        return ret;

    return 0;
}

/* Interrupt handler */
static irqreturn_t nvme_custom_interrupt(int irq, void *dev_id)
{
    struct nvme_custom_dev *dev = dev_id;
    u32 int_status;

    int_status = cfg_read32(dev, CFG_INT_STATUS_REG);
    if (!int_status)
        return IRQ_NONE;

    /* Clear interrupt */
    cfg_write32(dev, CFG_INT_STATUS_REG, int_status);

    if (int_status & STATUS_DMA_DONE)
    {
        dev->dma_in_progress = false;
        wake_up_interruptible(&dev->wait_queue);
    }

    if (int_status & STATUS_ERROR)
    {
        pr_err("%s: Hardware error detected\n", DRIVER_NAME);
    }

    return IRQ_HANDLED;
}

/* Block device operations */
static blk_status_t nvme_custom_queue_rq(struct blk_mq_hw_ctx *hctx,
                                         const struct blk_mq_queue_data *bd)
{
    struct nvme_custom_dev *dev = hctx->queue->queuedata;
    struct request *req = bd->rq;
    sector_t sector = blk_rq_pos(req);
    unsigned int nr_sectors = blk_rq_sectors(req);

    blk_mq_start_request(req);

    pr_debug("%s: Processing request - sector %llu, sectors %u, %s\n",
             DRIVER_NAME, (unsigned long long)sector, nr_sectors,
             rq_data_dir(req) ? "WRITE" : "READ");

    /* Simulate successful I/O for now */
    atomic_inc(&dev->ios_completed);
    blk_mq_end_request(req, BLK_STS_OK);

    return BLK_STS_OK;
}

static const struct blk_mq_ops nvme_custom_mq_ops = {
    .queue_rq = nvme_custom_queue_rq,
};

/* Character device operations for diagnostics */
static int nvme_custom_open(struct inode *inode, struct file *file)
{
    struct nvme_custom_dev *dev = global_dev;

    if (!dev || !dev->device_ready)
        return -ENODEV;

    file->private_data = dev;

    pr_debug("%s: Diagnostic device opened\n", DRIVER_NAME);
    return 0;
}

static int nvme_custom_release(struct inode *inode, struct file *file)
{
    pr_debug("%s: Diagnostic device released\n", DRIVER_NAME);
    return 0;
}

static long nvme_custom_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct nvme_custom_dev *dev = file->private_data;

    switch (cmd)
    {
    case 0x1000: /* Reset device */
        if (mutex_lock_interruptible(&dev->lock))
            return -ERESTARTSYS;
        nvme_custom_hw_init(dev);
        mutex_unlock(&dev->lock);
        return 0;

    case 0x1001: /* Get status */
        return cfg_read32(dev, CFG_STATUS_REG);

    case 0x1002: /* Get I/O count */
        return atomic_read(&dev->ios_completed);

    default:
        return -ENOTTY;
    }
}

static const struct file_operations nvme_custom_fops = {
    .owner = THIS_MODULE,
    .open = nvme_custom_open,
    .release = nvme_custom_release,
    .unlocked_ioctl = nvme_custom_ioctl,
    .llseek = no_llseek,
};

/* Block device setup */
static int nvme_custom_setup_block_device(struct nvme_custom_dev *dev)
{
    int ret;

    /* Initialize tag set */
    dev->tag_set.ops = &nvme_custom_mq_ops;
    dev->tag_set.nr_hw_queues = 1;
    dev->tag_set.queue_depth = 32;
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

    /* Allocate disk */
    dev->disk = blk_mq_alloc_disk(&dev->tag_set, dev);
    if (IS_ERR(dev->disk))
    {
        pr_err("%s: Failed to allocate disk\n", DRIVER_NAME);
        ret = PTR_ERR(dev->disk);
        goto free_tagset;
    }

    dev->queue = dev->disk->queue;
    dev->queue->queuedata = dev;

    /* Configure disk */
    strcpy(dev->disk->disk_name, "nvme_custom0");

    /* Set capacity - 1GB for testing */
    set_capacity(dev->disk, 2 * 1024 * 1024); /* 1GB in 512-byte sectors */

    /* Set logical block size */
    blk_queue_logical_block_size(dev->queue, 512);

    /* Add disk */
    ret = add_disk(dev->disk);
    if (ret)
    {
        pr_err("%s: Failed to add disk\n", DRIVER_NAME);
        goto cleanup_disk;
    }

    pr_info("%s: Block device created: %s\n", DRIVER_NAME, dev->disk->disk_name);
    return 0;

cleanup_disk:
    blk_cleanup_disk(dev->disk);
free_tagset:
    blk_mq_free_tag_set(&dev->tag_set);
    return ret;
}

/* PCI probe function */
static int nvme_custom_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct nvme_custom_dev *dev;
    int ret;

    pr_info("%s: Probing device %s\n", DRIVER_NAME, pci_name(pdev));

    /* Allocate device structure */
    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->pdev = pdev;
    mutex_init(&dev->lock);
    init_waitqueue_head(&dev->wait_queue);
    atomic_set(&dev->ios_completed, 0);

    /* Enable PCI device */
    ret = pci_enable_device(pdev);
    if (ret)
    {
        pr_err("%s: Failed to enable PCI device\n", DRIVER_NAME);
        goto err_free_dev;
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

    pci_set_master(pdev);

    /* Request memory regions */
    ret = pci_request_regions(pdev, DRIVER_NAME);
    if (ret)
    {
        pr_err("%s: Failed to request PCI regions\n", DRIVER_NAME);
        goto err_disable_device;
    }

    /* Map BAR0/1 (16TB VMEM space) */
    dev->bar0_bar1_mem = pci_ioremap_bar(pdev, 0);
    if (!dev->bar0_bar1_mem)
    {
        pr_err("%s: Failed to map BAR0/1\n", DRIVER_NAME);
        ret = -ENOMEM;
        goto err_release_regions;
    }

    /* Map BAR2/3 (8GB combined space) */
    dev->bar2_bar3_mem = pci_ioremap_bar(pdev, 2);
    if (!dev->bar2_bar3_mem)
    {
        pr_err("%s: Failed to map BAR2/3\n", DRIVER_NAME);
        ret = -ENOMEM;
        goto err_unmap_bar0;
    }

    /* Set up register regions */
    dev->cfg_regs = dev->bar2_bar3_mem + CFG_REG_BASE;
    dev->m2b_regs = dev->bar2_bar3_mem + M2B_REG_BASE;
    dev->dma_mem = dev->bar2_bar3_mem + DMA_MEM_BASE;

    /* Allocate DMA coherent buffer */
    dev->dma_size = 1024 * 1024; /* 1MB buffer */
    dev->dma_vaddr = dma_alloc_coherent(&pdev->dev, dev->dma_size,
                                        &dev->dma_handle, GFP_KERNEL);
    if (!dev->dma_vaddr)
    {
        pr_err("%s: Failed to allocate DMA buffer\n", DRIVER_NAME);
        ret = -ENOMEM;
        goto err_unmap_bar2;
    }

    /* Setup MSI interrupt */
    ret = pci_enable_msi(pdev);
    if (ret)
    {
        pr_warn("%s: MSI not available, using legacy interrupt\n", DRIVER_NAME);
    }

    ret = request_irq(pdev->irq, nvme_custom_interrupt, IRQF_SHARED,
                      DRIVER_NAME, dev);
    if (ret)
    {
        pr_err("%s: Failed to request interrupt\n", DRIVER_NAME);
        goto err_free_dma;
    }

    /* Initialize hardware */
    ret = nvme_custom_hw_init(dev);
    if (ret)
        goto err_free_irq;

    /* Enable interrupts */
    cfg_write32(dev, CFG_INT_ENABLE_REG, STATUS_DMA_DONE | STATUS_ERROR);

    /* Setup block device */
    ret = nvme_custom_setup_block_device(dev);
    if (ret)
        goto err_free_irq;

    /* Create character device for diagnostics */
    ret = alloc_chrdev_region(&dev->devt, MINOR_BASE, DEVICE_COUNT, DRIVER_NAME);
    if (ret)
    {
        pr_err("%s: Failed to allocate character device region\n", DRIVER_NAME);
        goto err_cleanup_block;
    }

    cdev_init(&dev->cdev, &nvme_custom_fops);
    dev->cdev.owner = THIS_MODULE;

    ret = cdev_add(&dev->cdev, dev->devt, DEVICE_COUNT);
    if (ret)
    {
        pr_err("%s: Failed to add character device\n", DRIVER_NAME);
        goto err_unregister_chrdev;
    }

    /* Create device class */
    dev->class = class_create(THIS_MODULE, DRIVER_NAME);
    if (IS_ERR(dev->class))
    {
        ret = PTR_ERR(dev->class);
        pr_err("%s: Failed to create device class\n", DRIVER_NAME);
        goto err_cdev_del;
    }

    /* Create device node */
    dev->device = device_create(dev->class, &pdev->dev, dev->devt, dev,
                                "%s_diag", DRIVER_NAME);
    if (IS_ERR(dev->device))
    {
        ret = PTR_ERR(dev->device);
        pr_err("%s: Failed to create device\n", DRIVER_NAME);
        goto err_class_destroy;
    }

    /* Set PCI driver data */
    pci_set_drvdata(pdev, dev);
    global_dev = dev;

    pr_info("%s: Device %s successfully initialized\n", DRIVER_NAME, pci_name(pdev));
    pr_info("%s: Block device: /dev/%s\n", DRIVER_NAME, dev->disk->disk_name);
    pr_info("%s: Diagnostic device: /dev/%s_diag\n", DRIVER_NAME, DRIVER_NAME);

    return 0;

err_class_destroy:
    class_destroy(dev->class);
err_cdev_del:
    cdev_del(&dev->cdev);
err_unregister_chrdev:
    unregister_chrdev_region(dev->devt, DEVICE_COUNT);
err_cleanup_block:
    del_gendisk(dev->disk);
    blk_cleanup_disk(dev->disk);
    blk_mq_free_tag_set(&dev->tag_set);
err_free_irq:
    free_irq(pdev->irq, dev);
    pci_disable_msi(pdev);
err_free_dma:
    dma_free_coherent(&pdev->dev, dev->dma_size, dev->dma_vaddr, dev->dma_handle);
err_unmap_bar2:
    iounmap(dev->bar2_bar3_mem);
err_unmap_bar0:
    iounmap(dev->bar0_bar1_mem);
err_release_regions:
    pci_release_regions(pdev);
err_disable_device:
    pci_disable_device(pdev);
err_free_dev:
    kfree(dev);
    return ret;
}

/* PCI remove function */
static void nvme_custom_remove(struct pci_dev *pdev)
{
    struct nvme_custom_dev *dev = pci_get_drvdata(pdev);

    pr_info("%s: Removing device %s\n", DRIVER_NAME, pci_name(pdev));

    /* Disable interrupts */
    cfg_write32(dev, CFG_INT_ENABLE_REG, 0);

    /* Clean up character device */
    device_destroy(dev->class, dev->devt);
    class_destroy(dev->class);
    cdev_del(&dev->cdev);
    unregister_chrdev_region(dev->devt, DEVICE_COUNT);

    /* Clean up block device */
    del_gendisk(dev->disk);
    blk_cleanup_disk(dev->disk);
    blk_mq_free_tag_set(&dev->tag_set);

    /* Free interrupt */
    free_irq(pdev->irq, dev);
    pci_disable_msi(pdev);

    /* Free DMA buffer */
    dma_free_coherent(&pdev->dev, dev->dma_size, dev->dma_vaddr, dev->dma_handle);

    /* Unmap memory regions */
    iounmap(dev->bar2_bar3_mem);
    iounmap(dev->bar0_bar1_mem);

    /* Release PCI resources */
    pci_release_regions(pdev);
    pci_disable_device(pdev);

    global_dev = NULL;
    kfree(dev);

    pr_info("%s: Device removed successfully\n", DRIVER_NAME);
}

/* PCI driver structure */
static struct pci_driver nvme_custom_driver = {
    .name = DRIVER_NAME,
    .id_table = nvme_custom_id_table,
    .probe = nvme_custom_probe,
    .remove = nvme_custom_remove,
};

/* Module initialization */
static int __init nvme_custom_init(void)
{
    int ret;

    pr_info("%s: Loading NVMe driver version %s\n", DRIVER_NAME, DRIVER_VERSION);

    ret = pci_register_driver(&nvme_custom_driver);
    if (ret)
    {
        pr_err("%s: Failed to register PCI driver\n", DRIVER_NAME);
        return ret;
    }

    pr_info("%s: NVMe driver loaded successfully\n", DRIVER_NAME);
    return 0;
}

/* Module cleanup */
static void __exit nvme_custom_exit(void)
{
    pr_info("%s: Unloading NVMe driver\n", DRIVER_NAME);
    pci_unregister_driver(&nvme_custom_driver);
    pr_info("%s: NVMe driver unloaded successfully\n", DRIVER_NAME);
}

module_init(nvme_custom_init);
module_exit(nvme_custom_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Custom NVMe PCIe Driver for device 15:00.0");
MODULE_VERSION(DRIVER_VERSION);