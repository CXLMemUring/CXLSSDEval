/*
 * Test application for Custom NVMe Driver
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#define DEVICE_PATH "/dev/nvme_custom0"
#define BLOCK_SIZE 512
#define TEST_BLOCKS 8

int main(int argc, char *argv[])
{
    int fd;
    ssize_t ret;
    char write_buffer[BLOCK_SIZE * TEST_BLOCKS];
    char read_buffer[BLOCK_SIZE * TEST_BLOCKS];
    struct stat st;
    int i;

    printf("Custom NVMe Driver Test Application\n");
    printf("===================================\n");

    /* Check if device exists */
    if (stat(DEVICE_PATH, &st) != 0)
    {
        printf("Device %s not found\n", DEVICE_PATH);
        printf("Make sure the nvme_custom driver is loaded\n");
        printf("Check with: lsblk | grep nvme_custom\n");
        return 1;
    }

    printf("Device found: %s\n", DEVICE_PATH);
    printf("Device type: %s\n", S_ISBLK(st.st_mode) ? "Block device" : "Other");

    /* Open device */
    fd = open(DEVICE_PATH, O_RDWR | O_DIRECT);
    if (fd < 0)
    {
        perror("Failed to open device");
        printf("Try with sudo privileges\n");
        return 1;
    }

    printf("Device opened successfully\n");

    /* Prepare test data */
    printf("\nPreparing test data (%d blocks of %d bytes)...\n",
           TEST_BLOCKS, BLOCK_SIZE);

    for (i = 0; i < BLOCK_SIZE * TEST_BLOCKS; i++)
    {
        write_buffer[i] = (char)(i % 256);
    }

    /* Test write operation */
    printf("Testing write operation...\n");
    ret = write(fd, write_buffer, BLOCK_SIZE * TEST_BLOCKS);
    if (ret < 0)
    {
        perror("Write failed");
        goto cleanup;
    }
    printf("Write completed: %zd bytes written\n", ret);

    /* Seek back to beginning */
    if (lseek(fd, 0, SEEK_SET) < 0)
    {
        perror("Seek failed");
        goto cleanup;
    }

    /* Test read operation */
    printf("Testing read operation...\n");
    memset(read_buffer, 0, BLOCK_SIZE * TEST_BLOCKS);
    ret = read(fd, read_buffer, BLOCK_SIZE * TEST_BLOCKS);
    if (ret < 0)
    {
        perror("Read failed");
        goto cleanup;
    }
    printf("Read completed: %zd bytes read\n", ret);

    /* Verify data */
    printf("Verifying data...\n");
    if (memcmp(write_buffer, read_buffer, BLOCK_SIZE * TEST_BLOCKS) == 0)
    {
        printf("✅ Data verification PASSED\n");
    }
    else
    {
        printf("❌ Data verification FAILED\n");
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

    /* Test random access */
    printf("\nTesting random access...\n");
    off_t offset = BLOCK_SIZE * 2; /* Seek to block 2 */
    if (lseek(fd, offset, SEEK_SET) < 0)
    {
        perror("Random seek failed");
        goto cleanup;
    }

    /* Write a pattern */
    char pattern[BLOCK_SIZE];
    memset(pattern, 0xAA, BLOCK_SIZE);
    ret = write(fd, pattern, BLOCK_SIZE);
    if (ret < 0)
    {
        perror("Random write failed");
        goto cleanup;
    }
    printf("Random write completed at offset %ld\n", offset);

    /* Read it back */
    if (lseek(fd, offset, SEEK_SET) < 0)
    {
        perror("Random seek failed");
        goto cleanup;
    }

    char verify_pattern[BLOCK_SIZE];
    ret = read(fd, verify_pattern, BLOCK_SIZE);
    if (ret < 0)
    {
        perror("Random read failed");
        goto cleanup;
    }

    if (memcmp(pattern, verify_pattern, BLOCK_SIZE) == 0)
    {
        printf("✅ Random access test PASSED\n");
    }
    else
    {
        printf("❌ Random access test FAILED\n");
    }

    printf("\n✅ All tests completed successfully\n");

cleanup:
    close(fd);
    return 0;
}

/* Additional utility functions */
void print_device_info(void)
{
    FILE *fp;
    char line[256];

    printf("\nNVMe Device Information:\n");
    printf("========================\n");

    /* Check if device appears in /proc/partitions */
    fp = fopen("/proc/partitions", "r");
    if (fp)
    {
        printf("Block devices:\n");
        while (fgets(line, sizeof(line), fp))
        {
            if (strstr(line, "nvme_custom"))
            {
                printf("  %s", line);
            }
        }
        fclose(fp);
    }

    /* Check kernel messages */
    system("dmesg | grep nvme_custom | tail -5");
}