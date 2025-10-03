/*
 * Custom NVMe PCIe Driver for device 15:00.0
 * Based on Linux NVMe subsystem
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
#include <linux/nvme.h>
#include <linux/bio.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/completion.h>
#include <linux/types.h>
#include <linux/io.h>

#define DRIVER_NAME "nvme_custom"
#define DRIVER_VERSION "1.0"

/* Device identification - Actual device IDs */
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

/* NVMe Controller Registers (standard NVMe layout) */
#define NVME_REG_CAP 0x0000   /* Controller Capabilities */
#define NVME_REG_VS 0x0008    /* Version */
#define NVME_REG_INTMS 0x000c /* Interrupt Mask Set */
#define NVME_REG_INTMC 0x0010 /* Interrupt Mask Clear */
#define NVME_REG_CC 0x0014    /* Controller Configuration */
#define NVME_REG_CSTS 0x001c  /* Controller Status */
#define NVME_REG_AQA 0x0024   /* Admin Queue Attributes */
#define NVME_REG_ASQ 0x0028   /* Admin Submission Queue Base */
#define NVME_REG_ACQ 0x0030   /* Admin Completion Queue Base */

/* Custom registers */
#define CUSTOM_CTRL_REG 0x1000
#define CUSTOM_STATUS_REG 0x1004
#define CUSTOM_INT_REG 0x1008
#define CUSTOM_DMA_REG 0x100C

/* Queue definitions */
#define NVME_AQ_DEPTH 32
#define NVME_Q_DEPTH 1024
#define NVME_MAX_QUEUES 16

/* Command and completion structures */
struct nvme_command
{
    __u8 opcode;
    __u8 flags;
    __u16 command_id;
    __le32 nsid;
    __u64 rsvd2;
    __le64 metadata;
    __le64 prp1;
    __le64 prp2;
    __u32 cdw10[6];
};

struct nvme_completion
{
    __le32 result;
    __u32 rsvd;
    __le16 sq_head;
    __le16 sq_id;
    __u16 command_id;
    __le16 status;
};

/* Queue structure */
struct nvme_queue
{
    struct nvme_dev *dev;
    spinlock_t q_lock;
    struct nvme_command *sq_cmds;
    struct nvme_completion *cqes;
    dma_addr_t sq_dma_addr;
    dma_addr_t cq_dma_addr;
    u32 __iomem *q_db;
    u16 q_depth;
    s16 cq_vector;
    u16 sq_head;
    u16 sq_tail;
    u16 cq_head;
    u16 cq_phase;
    u16 qid;
    u8 cq_full;
    struct completion *completion;
};

/* Main device structure */
struct nvme_dev
{
    struct pci_dev *pdev;

    /* Memory mapped regions */
    void __iomem *bar0_bar1_mem; /* 16TB VMEM space */
    void __iomem *bar2_bar3_mem; /* 8GB combined space */
    void __iomem *ctrl_regs;     /* NVMe controller registers */
    void __iomem *cfg_regs;      /* Custom config registers */
    void __iomem *m2b_regs;      /* M2B registers */
    void __iomem *dma_mem;       /* DMA memory region */

    /* NVMe subsystem integration */
    struct nvme_ctrl ctrl;
    struct gendisk *disk;
    struct request_queue *queue;
    struct blk_mq_tag_set tag_set;

    /* Queues */
    struct nvme_queue *queues;
    unsigned int queue_count;
    unsigned int max_qid;

    /* Device capabilities */
    u64 cap;
    u32 vs;
    u32 page_size;

    /* DMA coherent buffer */
    dma_addr_t dma_handle;
    void *dma_vaddr;
    size_t dma_size;

    /* Synchronization */
    struct mutex lock;
    struct work_struct reset_work;

    /* Device state */
    bool device_ready;
    bool ctrl_enabled;

    /* Statistics */
    atomic_t ios_completed;
    unsigned long reset_count;
};

/* Function prototypes */
static int nvme_custom_probe(struct pci_dev *pdev, const struct pci_device_id *id);
static void nvme_custom_remove(struct pci_dev *pdev);
static irqreturn_t nvme_custom_irq(int irq, void *data);

/* PCI device table */
static const struct pci_device_id nvme_custom_id_table[] = {
    {PCI_DEVICE(VENDOR_ID, DEVICE_ID)},
    {
        0,
    }};
MODULE_DEVICE_TABLE(pci, nvme_custom_id_table);

/* Register access helpers */
static inline u32 nvme_readl(struct nvme_dev *dev, unsigned offset)
{
    return readl(dev->ctrl_regs + offset);
}

static inline void nvme_writel(struct nvme_dev *dev, u32 val, unsigned offset)
{
    writel(val, dev->ctrl_regs + offset);
}

static inline u64 nvme_readq(struct nvme_dev *dev, unsigned offset)
{
    return readq(dev->ctrl_regs + offset);
}

static inline void nvme_writeq(struct nvme_dev *dev, u64 val, unsigned offset)
{
    writeq(val, dev->ctrl_regs + offset);
}

/* Custom register access */
static inline u32 custom_readl(struct nvme_dev *dev, unsigned offset)
{
    return readl(dev->cfg_regs + offset);
}

static inline void custom_writel(struct nvme_dev *dev, u32 val, unsigned offset)
{
    writel(val, dev->cfg_regs + offset);
}

/* Queue management */
static int nvme_alloc_queue(struct nvme_dev *dev, int qid, int depth)
{
    struct nvme_queue *nvmeq;

    nvmeq = kzalloc(sizeof(*nvmeq), GFP_KERNEL);
    if (!nvmeq)
        return -ENOMEM;

    nvmeq->dev = dev;
    nvmeq->qid = qid;
    nvmeq->q_depth = depth;
    nvmeq->cq_phase = 1;

    spin_lock_init(&nvmeq->q_lock);

    /* Allocate submission queue */
    nvmeq->sq_cmds = dma_alloc_coherent(&dev->pdev->dev,
                                        depth * sizeof(struct nvme_command),
                                        &nvmeq->sq_dma_addr, GFP_KERNEL);
    if (!nvmeq->sq_cmds)
        goto free_nvmeq;

    /* Allocate completion queue */
    nvmeq->cqes = dma_alloc_coherent(&dev->pdev->dev,
                                     depth * sizeof(struct nvme_completion),
                                     &nvmeq->cq_dma_addr, GFP_KERNEL);
    if (!nvmeq->cqes)
        goto free_sq;

    dev->queues[qid] = nvmeq;

    pr_info("%s: Allocated queue %d, depth %d\n", DRIVER_NAME, qid, depth);
    return 0;

free_sq:
    dma_free_coherent(&dev->pdev->dev, depth * sizeof(struct nvme_command),
                      nvmeq->sq_cmds, nvmeq->sq_dma_addr);
free_nvmeq:
    kfree(nvmeq);
    return -ENOMEM;
}

static void nvme_free_queue(struct nvme_dev *dev, int qid)
{
    struct nvme_queue *nvmeq = dev->queues[qid];
    int depth = nvmeq->q_depth;

    if (!nvmeq)
        return;

    dma_free_coherent(&dev->pdev->dev, depth * sizeof(struct nvme_completion),
                      nvmeq->cqes, nvmeq->cq_dma_addr);
    dma_free_coherent(&dev->pdev->dev, depth * sizeof(struct nvme_command),
                      nvmeq->sq_cmds, nvmeq->sq_dma_addr);

    kfree(nvmeq);
    dev->queues[qid] = NULL;
}

/* Controller initialization */
static int nvme_enable_ctrl(struct nvme_dev *dev)
{
    u64 cap = nvme_readq(dev, NVME_REG_CAP);
    u32 dev_page_min = NVME_CAP_MPSMIN(cap) + 12;
    u32 page_shift = 12;
    u32 cc;

    if (page_shift < dev_page_min)
    {
        pr_err("%s: Minimum page size not supported\n", DRIVER_NAME);
        return -ENODEV;
    }

    dev->page_size = 1 << page_shift;
    dev->cap = cap;

    /* Disable controller first */
    cc = nvme_readl(dev, NVME_REG_CC);
    cc &= ~NVME_CC_ENABLE;
    nvme_writel(dev, cc, NVME_REG_CC);

    /* Wait for controller to be ready */
    while (nvme_readl(dev, NVME_REG_CSTS) & NVME_CSTS_RDY)
    {
        msleep(1);
    }

    /* Configure admin queues */
    nvme_writel(dev, (NVME_AQ_DEPTH - 1) | ((NVME_AQ_DEPTH - 1) << 16),
                NVME_REG_AQA);
    nvme_writeq(dev, dev->queues[0]->sq_dma_addr, NVME_REG_ASQ);
    nvme_writeq(dev, dev->queues[0]->cq_dma_addr, NVME_REG_ACQ);

    /* Enable controller */
    cc = NVME_CC_ENABLE | NVME_CC_CSS_NVM | NVME_CC_MPS(page_shift - 12) |
         NVME_CC_ARB_RR | NVME_CC_SHN_NONE |
         NVME_CC_IOSQES | NVME_CC_IOCQES;
    nvme_writel(dev, cc, NVME_REG_CC);

    /* Wait for controller ready */
    while (!(nvme_readl(dev, NVME_REG_CSTS) & NVME_CSTS_RDY))
    {
        msleep(1);
    }

    dev->ctrl_enabled = true;
    pr_info("%s: Controller enabled successfully\n", DRIVER_NAME);

    return 0;
}

/* Custom hardware initialization */
static int nvme_custom_hw_init(struct nvme_dev *dev)
{
    u32 status;
    int timeout = 1000;

    pr_info("%s: Initializing custom hardware\n", DRIVER_NAME);

    /* Reset custom hardware */
    custom_writel(dev, 0x01, CUSTOM_CTRL_REG);
    msleep(100);

    /* Enable custom hardware */
    custom_writel(dev, 0x02, CUSTOM_CTRL_REG);

    /* Wait for hardware ready */
    while (timeout--)
    {
        status = custom_readl(dev, CUSTOM_STATUS_REG);
        if (status & 0x01)
        {
            dev->device_ready = true;
            pr_info("%s: Custom hardware ready\n", DRIVER_NAME);
            return 0;
        }
        msleep(10);
    }

    pr_err("%s: Custom hardware initialization failed\n", DRIVER_NAME);
    return -ETIMEDOUT;
}

/* Block device operations */
static blk_status_t nvme_custom_queue_rq(struct blk_mq_hw_ctx *hctx,
                                         const struct blk_mq_queue_data *bd)
{
    struct nvme_dev *dev = hctx->queue->queuedata;
    struct request *req = bd->rq;
    struct nvme_queue *nvmeq = hctx->driver_data;
    struct nvme_command cmd = {};

    blk_mq_start_request(req);

    /* Build NVMe command */
    cmd.common.opcode = (rq_data_dir(req) ? nvme_cmd_write : nvme_cmd_read);
    cmd.rw.nsid = cpu_to_le32(1);                          /* Namespace 1 */
    cmd.rw.slba = cpu_to_le64(blk_rq_pos(req) >> (9 - 9)); /* Convert to LBA */
    cmd.rw.length = cpu_to_le16((blk_rq_bytes(req) >> 9) - 1);

    /* Set data pointer - simplified for demonstration */
    if (blk_rq_bytes(req))
    {
        cmd.rw.prp1 = cpu_to_le64(dev->dma_handle);
    }

    /* Submit command to hardware queue */
    spin_lock(&nvmeq->q_lock);

    /* Copy command to submission queue */
    memcpy(&nvmeq->sq_cmds[nvmeq->sq_tail], &cmd, sizeof(cmd));

    /* Update tail pointer */
    if (++nvmeq->sq_tail == nvmeq->q_depth)
        nvmeq->sq_tail = 0;

    /* Ring doorbell */
    writel(nvmeq->sq_tail, nvmeq->q_db);

    spin_unlock(&nvmeq->q_lock);

    atomic_inc(&dev->ios_completed);

    return BLK_STS_OK;
}

static const struct blk_mq_ops nvme_custom_mq_ops = {
    .queue_rq = nvme_custom_queue_rq,
};

/* Interrupt handler */
static irqreturn_t nvme_custom_irq(int irq, void *data)
{
    struct nvme_dev *dev = data;
    struct nvme_queue *nvmeq;
    int i;
    bool handled = false;

    /* Process completion queues */
    for (i = 0; i < dev->queue_count; i++)
    {
        nvmeq = dev->queues[i];
        if (!nvmeq)
            continue;

        /* Check for completions - simplified */
        if (nvmeq->cqes[nvmeq->cq_head].status != 0)
        {
            /* Process completion */
            nvmeq->cq_head++;
            if (nvmeq->cq_head == nvmeq->q_depth)
            {
                nvmeq->cq_head = 0;
                nvmeq->cq_phase = !nvmeq->cq_phase;
            }
            handled = true;
        }
    }

    return handled ? IRQ_HANDLED : IRQ_NONE;
}

/* Block device setup */
static int nvme_setup_block_device(struct nvme_dev *dev)
{
    int ret;

    /* Initialize tag set */
    dev->tag_set.ops = &nvme_custom_mq_ops;
    dev->tag_set.nr_hw_queues = dev->queue_count - 1; /* Exclude admin queue */
    dev->tag_set.queue_depth = NVME_Q_DEPTH;
    dev->tag_set.numa_node = NUMA_NO_NODE;
    dev->tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
    dev->tag_set.cmd_size = sizeof(struct nvme_command);
    dev->tag_set.driver_data = dev;

    ret = blk_mq_alloc_tag_set(&dev->tag_set);
    if (ret)
    {
        pr_err("%s: Failed to allocate tag set\n", DRIVER_NAME);
        return ret;
    }

    /* Allocate disk */
    dev->disk = blk_alloc_disk(NUMA_NO_NODE);
    if (!dev->disk)
    {
        pr_err("%s: Failed to allocate disk\n", DRIVER_NAME);
        ret = -ENOMEM;
        goto free_tagset;
    }

    /* Create request queue */
    dev->queue = blk_mq_init_queue(&dev->tag_set);
    if (IS_ERR(dev->queue))
    {
        pr_err("%s: Failed to create request queue\n", DRIVER_NAME);
        ret = PTR_ERR(dev->queue);
        goto free_disk;
    }

    /* Configure disk */
    dev->disk->major = 0; /* Dynamic allocation */
    dev->disk->first_minor = 0;
    dev->disk->queue = dev->queue;
    dev->queue->queuedata = dev;
    snprintf(dev->disk->disk_name, sizeof(dev->disk->disk_name), "nvme_custom0");

    /* Set capacity - 1TB for example */
    set_capacity(dev->disk, 1024 * 1024 * 1024 * 2); /* 1TB in 512-byte sectors */

    /* Add disk */
    ret = add_disk(dev->disk);
    if (ret)
    {
        pr_err("%s: Failed to add disk\n", DRIVER_NAME);
        goto cleanup_queue;
    }

    pr_info("%s: Block device created: %s\n", DRIVER_NAME, dev->disk->disk_name);
    return 0;

cleanup_queue:
    blk_cleanup_queue(dev->queue);
free_disk:
    put_disk(dev->disk);
free_tagset:
    blk_mq_free_tag_set(&dev->tag_set);
    return ret;
}

/* PCI probe function */
static int nvme_custom_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct nvme_dev *dev;
    int ret, i;

    pr_info("%s: Probing device %s\n", DRIVER_NAME, pci_name(pdev));

    /* Allocate device structure */
    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->pdev = pdev;
    mutex_init(&dev->lock);

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
    dev->ctrl_regs = dev->bar2_bar3_mem; /* NVMe controller registers at base */
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

    ret = request_irq(pdev->irq, nvme_custom_irq, IRQF_SHARED,
                      DRIVER_NAME, dev);
    if (ret)
    {
        pr_err("%s: Failed to request interrupt\n", DRIVER_NAME);
        goto err_free_dma;
    }

    /* Initialize custom hardware */
    ret = nvme_custom_hw_init(dev);
    if (ret)
        goto err_free_irq;

    /* Allocate queues */
    dev->queue_count = min(num_online_cpus() + 1, NVME_MAX_QUEUES);
    dev->queues = kcalloc(dev->queue_count, sizeof(struct nvme_queue *), GFP_KERNEL);
    if (!dev->queues)
    {
        ret = -ENOMEM;
        goto err_free_irq;
    }

    /* Allocate admin queue */
    ret = nvme_alloc_queue(dev, 0, NVME_AQ_DEPTH);
    if (ret)
        goto err_free_queues;

    /* Allocate I/O queues */
    for (i = 1; i < dev->queue_count; i++)
    {
        ret = nvme_alloc_queue(dev, i, NVME_Q_DEPTH);
        if (ret)
            goto err_free_all_queues;
    }

    /* Enable NVMe controller */
    ret = nvme_enable_ctrl(dev);
    if (ret)
        goto err_free_all_queues;

    /* Setup block device */
    ret = nvme_setup_block_device(dev);
    if (ret)
        goto err_free_all_queues;

    /* Set PCI driver data */
    pci_set_drvdata(pdev, dev);

    pr_info("%s: Device %s successfully initialized\n", DRIVER_NAME, pci_name(pdev));
    pr_info("%s: NVMe device created: /dev/%s\n", DRIVER_NAME, dev->disk->disk_name);

    return 0;

err_free_all_queues:
    for (i = 0; i < dev->queue_count; i++)
        nvme_free_queue(dev, i);
err_free_queues:
    kfree(dev->queues);
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
    struct nvme_dev *dev = pci_get_drvdata(pdev);
    int i;

    pr_info("%s: Removing device %s\n", DRIVER_NAME, pci_name(pdev));

    /* Remove block device */
    if (dev->disk)
    {
        del_gendisk(dev->disk);
        put_disk(dev->disk);
    }

    if (dev->queue)
        blk_cleanup_queue(dev->queue);

    blk_mq_free_tag_set(&dev->tag_set);

    /* Disable controller */
    if (dev->ctrl_enabled)
    {
        u32 cc = nvme_readl(dev, NVME_REG_CC);
        cc &= ~NVME_CC_ENABLE;
        nvme_writel(dev, cc, NVME_REG_CC);
    }

    /* Free queues */
    for (i = 0; i < dev->queue_count; i++)
        nvme_free_queue(dev, i);
    kfree(dev->queues);

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