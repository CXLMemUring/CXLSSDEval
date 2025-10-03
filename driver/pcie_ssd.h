/*
 * PCIe SSD Driver Header
 * IOCTL definitions and user-space interface
 */

#ifndef PCIE_SSD_H
#define PCIE_SSD_H

#include <linux/ioctl.h>

/* IOCTL definitions */
#define PCIE_SSD_MAGIC 'P'

#define PCIE_SSD_RESET _IO(PCIE_SSD_MAGIC, 0)
#define PCIE_SSD_GET_STATUS _IOR(PCIE_SSD_MAGIC, 1, unsigned int)
#define PCIE_SSD_GET_INFO _IOR(PCIE_SSD_MAGIC, 2, struct pcie_ssd_info)

/* Device information structure */
struct pcie_ssd_info
{
    unsigned int vendor_id;
    unsigned int device_id;
    unsigned long bar0_size;
    unsigned long bar2_size;
    unsigned long total_transfers;
    unsigned int status;
    unsigned int open_count;
};

/* Status bits */
#define PCIE_SSD_STATUS_READY 0x01
#define PCIE_SSD_STATUS_ERROR 0x02
#define PCIE_SSD_STATUS_DMA_BUSY 0x04

#endif /* PCIE_SSD_H */