/*
 * PCIe SSD Driver for device 00:15.0
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
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include "pcie_ssd.h"

#define DRIVER_NAME "pcie_ssd"
#define DRIVER_VERSION "1.0"

/* Device identification */
#define VENDOR_ID 0x1234 /* Replace with actual vendor ID */
#define DEVICE_ID 0x5678 /* Replace with actual device ID */

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

/* Character device */
#define DEVICE_COUNT 1
#define MINOR_BASE 0

struct pcie_ssd_device
{
    struct pci_dev *pdev;

    /* Memory mapped regions */
    void __iomem *bar0_bar1_mem; /* 16TB VMEM space */
    void __iomem *bar2_bar3_mem; /* 8GB combined space */
    void __iomem *cfg_regs;      /* Config registers */
    void __iomem *m2b_regs;      /* M2B registers */
    void __iomem *dma_mem;       /* DMA memory region */

    /* DMA coherent buffer for data transfers */
    dma_addr_t dma_handle;
    void *dma_vaddr;
    size_t dma_size;

    /* Character device */
    dev_t devt;
    struct cdev cdev;
    struct device *device;
    struct class *class;

    /* Synchronization */
    struct mutex lock;
    wait_queue_head_t wait_queue;

    /* Device state */
    bool device_ready;
    bool dma_in_progress;

    /* Statistics */
    atomic_t open_count;
    unsigned long total_transfers;
};

static struct pcie_ssd_device *global_dev = NULL;

/* PCI device table */
static const struct pci_device_id pcie_ssd_id_table[] = {
    {PCI_DEVICE(VENDOR_ID, DEVICE_ID)},
    {
        0,
    }};
MODULE_DEVICE_TABLE(pci, pcie_ssd_id_table);

/* Register access helpers */
static inline u32 cfg_read32(struct pcie_ssd_device *dev, u32 offset)
{
    return ioread32(dev->cfg_regs + offset);
}

static inline void cfg_write32(struct pcie_ssd_device *dev, u32 offset, u32 value)
{
    iowrite32(value, dev->cfg_regs + offset);
}

static inline u32 m2b_read32(struct pcie_ssd_device *dev, u32 offset)
{
    return ioread32(dev->m2b_regs + offset);
}

static inline void m2b_write32(struct pcie_ssd_device *dev, u32 offset, u32 value)
{
    iowrite32(value, dev->m2b_regs + offset);
}

/* Device initialization */
static int pcie_ssd_hw_init(struct pcie_ssd_device *dev)
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

/* Interrupt handler */
static irqreturn_t pcie_ssd_interrupt(int irq, void *dev_id)
{
    struct pcie_ssd_device *dev = dev_id;
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

/* DMA operations */
static int pcie_ssd_start_dma(struct pcie_ssd_device *dev,
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

static int pcie_ssd_wait_dma(struct pcie_ssd_device *dev)
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

/* Character device operations */
static int pcie_ssd_open(struct inode *inode, struct file *file)
{
    struct pcie_ssd_device *dev = global_dev;

    if (!dev || !dev->device_ready)
        return -ENODEV;

    atomic_inc(&dev->open_count);
    file->private_data = dev;

    pr_debug("%s: Device opened\n", DRIVER_NAME);
    return 0;
}

static int pcie_ssd_release(struct inode *inode, struct file *file)
{
    struct pcie_ssd_device *dev = file->private_data;

    atomic_dec(&dev->open_count);

    pr_debug("%s: Device released\n", DRIVER_NAME);
    return 0;
}

static ssize_t pcie_ssd_read(struct file *file, char __user *buf,
                             size_t count, loff_t *ppos)
{
    struct pcie_ssd_device *dev = file->private_data;
    size_t transfer_size;
    int ret;

    if (!dev->device_ready)
        return -ENODEV;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    transfer_size = min(count, dev->dma_size);

    /* Start DMA read */
    ret = pcie_ssd_start_dma(dev, dev->dma_handle, transfer_size);
    if (ret)
        goto out_unlock;

    /* Wait for completion */
    ret = pcie_ssd_wait_dma(dev);
    if (ret)
        goto out_unlock;

    /* Copy to user space */
    if (copy_to_user(buf, dev->dma_vaddr, transfer_size))
    {
        ret = -EFAULT;
        goto out_unlock;
    }

    *ppos += transfer_size;
    ret = transfer_size;
    dev->total_transfers++;

out_unlock:
    mutex_unlock(&dev->lock);
    return ret;
}

static ssize_t pcie_ssd_write(struct file *file, const char __user *buf,
                              size_t count, loff_t *ppos)
{
    struct pcie_ssd_device *dev = file->private_data;
    size_t transfer_size;
    int ret;

    if (!dev->device_ready)
        return -ENODEV;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    transfer_size = min(count, dev->dma_size);

    /* Copy from user space */
    if (copy_from_user(dev->dma_vaddr, buf, transfer_size))
    {
        ret = -EFAULT;
        goto out_unlock;
    }

    /* Start DMA write */
    ret = pcie_ssd_start_dma(dev, dev->dma_handle, transfer_size);
    if (ret)
        goto out_unlock;

    /* Wait for completion */
    ret = pcie_ssd_wait_dma(dev);
    if (ret)
        goto out_unlock;

    *ppos += transfer_size;
    ret = transfer_size;
    dev->total_transfers++;

out_unlock:
    mutex_unlock(&dev->lock);
    return ret;
}

static long pcie_ssd_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct pcie_ssd_device *dev = file->private_data;
    struct pcie_ssd_info info;
    unsigned int status;
    int ret;

    switch (cmd)
    {
    case PCIE_SSD_RESET: /* Reset device */
        if (mutex_lock_interruptible(&dev->lock))
            return -ERESTARTSYS;
        ret = pcie_ssd_hw_init(dev);
        mutex_unlock(&dev->lock);
        return ret;

    case PCIE_SSD_GET_STATUS: /* Get status */
        status = cfg_read32(dev, CFG_STATUS_REG);
        if (copy_to_user((void __user *)arg, &status, sizeof(status)))
            return -EFAULT;
        return 0;

    case PCIE_SSD_GET_INFO: /* Get device info */
        memset(&info, 0, sizeof(info));
        info.vendor_id = dev->pdev->vendor;
        info.device_id = dev->pdev->device;
        info.bar0_size = BAR0_BAR1_SIZE;
        info.bar2_size = BAR2_BAR3_SIZE;
        info.total_transfers = dev->total_transfers;
        info.status = cfg_read32(dev, CFG_STATUS_REG);
        info.open_count = atomic_read(&dev->open_count);

        if (copy_to_user((void __user *)arg, &info, sizeof(info)))
            return -EFAULT;
        return 0;

    default:
        return -ENOTTY;
    }
}

static const struct file_operations pcie_ssd_fops = {
    .owner = THIS_MODULE,
    .open = pcie_ssd_open,
    .release = pcie_ssd_release,
    .read = pcie_ssd_read,
    .write = pcie_ssd_write,
    .unlocked_ioctl = pcie_ssd_ioctl,
    .llseek = no_llseek,
};

/* PCI probe function */
static int pcie_ssd_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct pcie_ssd_device *dev;
    int ret;

    pr_info("%s: Probing device %s\n", DRIVER_NAME, pci_name(pdev));

    /* Allocate device structure */
    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->pdev = pdev;
    mutex_init(&dev->lock);
    init_waitqueue_head(&dev->wait_queue);
    atomic_set(&dev->open_count, 0);

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

    /* Set up MSI interrupt */
    ret = pci_enable_msi(pdev);
    if (ret)
    {
        pr_warn("%s: MSI not available, using legacy interrupt\n", DRIVER_NAME);
    }

    ret = request_irq(pdev->irq, pcie_ssd_interrupt, IRQF_SHARED,
                      DRIVER_NAME, dev);
    if (ret)
    {
        pr_err("%s: Failed to request interrupt\n", DRIVER_NAME);
        goto err_free_dma;
    }

    /* Initialize hardware */
    ret = pcie_ssd_hw_init(dev);
    if (ret)
        goto err_free_irq;

    /* Enable interrupts */
    cfg_write32(dev, CFG_INT_ENABLE_REG, STATUS_DMA_DONE | STATUS_ERROR);

    /* Create character device */
    ret = alloc_chrdev_region(&dev->devt, MINOR_BASE, DEVICE_COUNT, DRIVER_NAME);
    if (ret)
    {
        pr_err("%s: Failed to allocate character device region\n", DRIVER_NAME);
        goto err_free_irq;
    }

    cdev_init(&dev->cdev, &pcie_ssd_fops);
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
                                "%s0", DRIVER_NAME);
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
    pr_info("%s: BAR0/1 mapped at %p, BAR2/3 mapped at %p\n",
            DRIVER_NAME, dev->bar0_bar1_mem, dev->bar2_bar3_mem);
    pr_info("%s: Character device created: /dev/%s0\n", DRIVER_NAME, DRIVER_NAME);

    return 0;

err_class_destroy:
    class_destroy(dev->class);
err_cdev_del:
    cdev_del(&dev->cdev);
err_unregister_chrdev:
    unregister_chrdev_region(dev->devt, DEVICE_COUNT);
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
static void pcie_ssd_remove(struct pci_dev *pdev)
{
    struct pcie_ssd_device *dev = pci_get_drvdata(pdev);

    pr_info("%s: Removing device %s\n", DRIVER_NAME, pci_name(pdev));

    /* Disable interrupts */
    cfg_write32(dev, CFG_INT_ENABLE_REG, 0);

    /* Clean up character device */
    device_destroy(dev->class, dev->devt);
    class_destroy(dev->class);
    cdev_del(&dev->cdev);
    unregister_chrdev_region(dev->devt, DEVICE_COUNT);

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
static struct pci_driver pcie_ssd_driver = {
    .name = DRIVER_NAME,
    .id_table = pcie_ssd_id_table,
    .probe = pcie_ssd_probe,
    .remove = pcie_ssd_remove,
};

/* Module initialization */
static int __init pcie_ssd_init(void)
{
    int ret;

    pr_info("%s: Loading driver version %s\n", DRIVER_NAME, DRIVER_VERSION);

    ret = pci_register_driver(&pcie_ssd_driver);
    if (ret)
    {
        pr_err("%s: Failed to register PCI driver\n", DRIVER_NAME);
        return ret;
    }

    pr_info("%s: Driver loaded successfully\n", DRIVER_NAME);
    return 0;
}

/* Module cleanup */
static void __exit pcie_ssd_exit(void)
{
    pr_info("%s: Unloading driver\n", DRIVER_NAME);
    pci_unregister_driver(&pcie_ssd_driver);
    pr_info("%s: Driver unloaded successfully\n", DRIVER_NAME);
}

module_init(pcie_ssd_init);
module_exit(pcie_ssd_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("PCIe SSD Driver for device 00:15.0");
MODULE_VERSION(DRIVER_VERSION);