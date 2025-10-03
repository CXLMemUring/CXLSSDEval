# PCIe SSD Driver

A Linux kernel driver for PCIe SSD device at 00:15.0 with the specified memory layout.

## Memory Layout

- **BAR0/1**: 16TB VMEM space
- **BAR2/3**: 8GB combined space
  - `0x0_0000_0000 - 0x0_0000_ffff`: Config registers (64KB)
  - `0x0_0001_0000 - 0x0_0001_ffff`: M2B registers (64KB)
  - `0x1_0000_0000 - 0x1_ffff_ffff`: SSD init mem DMA (4GB)

## Features

- PCIe device driver with proper resource management
- Memory-mapped I/O for configuration and control registers
- DMA support for high-performance data transfers
- Interrupt handling (MSI preferred, legacy fallback)
- Character device interface for userspace applications
- Comprehensive error handling and cleanup

## Files

- `pcie_ssd_driver.c` - Main driver source code
- `pcie_ssd.h` - Header file with IOCTL definitions
- `test_pcie_ssd.c` - Test application
- `Makefile` - Build configuration
- `install.sh` - Installation and test script
- `README.md` - This file

## Building and Installation

### Prerequisites

- Linux kernel headers for your running kernel
- GCC compiler
- Make utility
- Root privileges for driver loading

### Quick Start

```bash
# Full build, install, and test (as root)
sudo ./install.sh full

# Or step by step:
sudo ./install.sh build    # Build driver
sudo ./install.sh install  # Load driver
sudo ./install.sh test     # Run test application
```

### Manual Build

```bash
# Build driver
make

# Load driver (as root)
sudo insmod pcie_ssd_driver.ko

# Build test application
gcc -o test_pcie_ssd test_pcie_ssd.c

# Run test
sudo ./test_pcie_ssd
```

## Configuration

Before using the driver, you may need to update the vendor and device IDs in `pcie_ssd_driver.c`:

```c
#define VENDOR_ID 0x1234  /* Replace with actual vendor ID */
#define DEVICE_ID 0x5678  /* Replace with actual device ID */
```

To find your device IDs:
```bash
lspci -nn | grep "00:15.0"
```

## Usage

### Character Device Interface

The driver creates a character device at `/dev/pcie_ssd0` that supports:

- **Read/Write**: DMA-based data transfers
- **IOCTL**: Device control operations

### IOCTL Commands

- `PCIE_SSD_RESET`: Reset the device
- `PCIE_SSD_GET_STATUS`: Get device status
- `PCIE_SSD_GET_INFO`: Get device information

### Example Usage

```c
#include "pcie_ssd.h"

int fd = open("/dev/pcie_ssd0", O_RDWR);

// Get device status
unsigned int status;
ioctl(fd, PCIE_SSD_GET_STATUS, &status);

// Read data
char buffer[1024];
ssize_t bytes_read = read(fd, buffer, sizeof(buffer));

// Write data
ssize_t bytes_written = write(fd, buffer, sizeof(buffer));

close(fd);
```

## Driver Architecture

### Key Components

1. **PCI Device Management**
   - Device detection and initialization
   - BAR mapping and resource allocation
   - Power management

2. **Memory Management**
   - Memory-mapped I/O regions
   - DMA coherent buffer allocation
   - 64-bit DMA addressing support

3. **Interrupt Handling**
   - MSI interrupt support with legacy fallback
   - DMA completion notification
   - Error condition handling

4. **Character Device Interface**
   - File operations (open, close, read, write, ioctl)
   - Device node creation (/dev/pcie_ssd0)
   - User-space API

### Register Layout

#### Config Registers (BAR2/3 + 0x0000_0000)
- `0x0000`: Control Register
- `0x0004`: Status Register  
- `0x0008`: Interrupt Enable Register
- `0x000C`: Interrupt Status Register

#### M2B Registers (BAR2/3 + 0x0001_0000)
- `0x0000`: Control Register
- `0x0004`: Status Register
- `0x0008`: DMA Address Low
- `0x000C`: DMA Address High
- `0x0010`: DMA Size
- `0x0014`: DMA Control

## Troubleshooting

### Check if device is detected
```bash
lspci | grep "00:15.0"
```

### Check driver status
```bash
sudo ./install.sh info
```

### View kernel messages
```bash
dmesg | grep pcie_ssd
```

### Common Issues

1. **Device not found**: Update vendor/device IDs in driver source
2. **Permission denied**: Ensure running as root for driver operations
3. **Module build fails**: Install kernel headers for your kernel version
4. **Device node not created**: Check if udev rules are needed

## Uninstalling

```bash
# Unload driver and clean up
sudo ./install.sh unload

# Clean build files
sudo ./install.sh clean
```

## Development Notes

### Customization Points

1. **Device IDs**: Update `VENDOR_ID` and `DEVICE_ID` macros
2. **Register Layout**: Modify register offset definitions
3. **DMA Buffer Size**: Adjust `dma_size` allocation
4. **IOCTL Interface**: Add custom IOCTL commands as needed

### Safety Considerations

- Always test on non-production systems first
- Ensure proper error handling for all hardware interactions
- Validate DMA addresses and sizes
- Handle device removal gracefully

## License

GPL v2 - See source files for full license text.