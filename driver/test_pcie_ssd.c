/*
 * Test application for PCIe SSD Driver
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include "pcie_ssd.h"

#define DEVICE_PATH "/dev/pcie_ssd0"
#define TEST_DATA_SIZE 1024

int main(int argc, char *argv[])
{
    int fd;
    int ret;
    unsigned int status;
    struct pcie_ssd_info info;
    char write_buffer[TEST_DATA_SIZE];
    char read_buffer[TEST_DATA_SIZE];
    int i;

    printf("PCIe SSD Driver Test Application\n");
    printf("================================\n");

    /* Open device */
    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0)
    {
        perror("Failed to open device");
        printf("Make sure the driver is loaded and device node exists\n");
        return 1;
    }

    printf("Device opened successfully\n");

    /* Get device status */
    ret = ioctl(fd, PCIE_SSD_GET_STATUS, &status);
    if (ret < 0)
    {
        perror("Failed to get device status");
        goto cleanup;
    }

    printf("Device status: 0x%08x\n", status);
    if (status & PCIE_SSD_STATUS_READY)
        printf("  - Device is ready\n");
    if (status & PCIE_SSD_STATUS_ERROR)
        printf("  - Device has errors\n");
    if (status & PCIE_SSD_STATUS_DMA_BUSY)
        printf("  - DMA is busy\n");

    /* Get device information */
    ret = ioctl(fd, PCIE_SSD_GET_INFO, &info);
    if (ret < 0)
    {
        perror("Failed to get device info");
        goto cleanup;
    }

    printf("\nDevice Information:\n");
    printf("  Vendor ID: 0x%04x\n", info.vendor_id);
    printf("  Device ID: 0x%04x\n", info.device_id);
    printf("  BAR0 size: %lu bytes\n", info.bar0_size);
    printf("  BAR2 size: %lu bytes\n", info.bar2_size);
    printf("  Total transfers: %lu\n", info.total_transfers);
    printf("  Open count: %u\n", info.open_count);

    /* Prepare test data */
    printf("\nPreparing test data...\n");
    for (i = 0; i < TEST_DATA_SIZE; i++)
    {
        write_buffer[i] = (char)(i % 256);
    }

    /* Test write operation */
    printf("Testing write operation...\n");
    ret = write(fd, write_buffer, TEST_DATA_SIZE);
    if (ret < 0)
    {
        perror("Write failed");
        goto cleanup;
    }
    printf("Write completed: %d bytes written\n", ret);

    /* Test read operation */
    printf("Testing read operation...\n");
    memset(read_buffer, 0, TEST_DATA_SIZE);
    ret = read(fd, read_buffer, TEST_DATA_SIZE);
    if (ret < 0)
    {
        perror("Read failed");
        goto cleanup;
    }
    printf("Read completed: %d bytes read\n", ret);

    /* Verify data */
    printf("Verifying data...\n");
    if (memcmp(write_buffer, read_buffer, TEST_DATA_SIZE) == 0)
    {
        printf("Data verification PASSED\n");
    }
    else
    {
        printf("Data verification FAILED\n");
        printf("First 16 bytes written: ");
        for (i = 0; i < 16; i++)
        {
            printf("%02x ", (unsigned char)write_buffer[i]);
        }
        printf("\n");
        printf("First 16 bytes read:    ");
        for (i = 0; i < 16; i++)
        {
            printf("%02x ", (unsigned char)read_buffer[i]);
        }
        printf("\n");
    }

    /* Test reset */
    printf("\nTesting device reset...\n");
    ret = ioctl(fd, PCIE_SSD_RESET);
    if (ret < 0)
    {
        perror("Reset failed");
    }
    else
    {
        printf("Device reset completed\n");
    }

    printf("\nTest completed successfully\n");

cleanup:
    close(fd);
    return 0;
}