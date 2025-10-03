# BAR Read/Write Driver for PCIe Device 15:00.0

A simple Linux block driver that uses 64-byte BAR writes to communicate with hardware.

## Overview

This driver provides a simple interface to a PCIe device that automatically translates 64-byte writes to its BAR area into memory read/write operations. **No NVMe subsystem is used** - the hardware handles everything.

## Hardware Interface

- **BAR0**: 16TB VMEM space
- **BAR2**: 8GB space
  - `0x00010000`: M2B register area (64-byte command interface)
  - Commands are sent by writing 64 bytes to this area

### 64-Byte Command Format

```c
struct bar_command {
    u8 opcode;          // 0x00: READ (0x01) or WRITE (0x02)
    u8 flags;           // 0x01: flags
    u16 reserved1;      // 0x02-0x03
    u32 length;         // 0x04-0x07: transfer length in bytes
    u64 lba;            // 0x08-0x0F: logical block address
    u64 dma_addr;       // 0x10-0x17: DMA address
    u8 padding[40];     // 0x18-0x3F: padding to 64 bytes
} __packed;
```

## Files

- `bar_rw_driver.c` - Main driver source
- `Makefile` - Build configuration
- `test_bar_rw.sh` - Test script
- `install.sh` - Installation helper
- `diagnose.sh` - Diagnostic tool

## Building

```bash
make clean
make
```

## Usage

### Load the Driver

```bash
sudo insmod bar_rw_driver.ko
```

### Check Device

```bash
lsblk | grep bar_rw
ls -l /dev/bar_rw0
```

### Test Read/Write

```bash
# Write test data
sudo dd if=/dev/urandom of=/dev/bar_rw0 bs=4096 count=1000

# Read it back
sudo dd if=/dev/bar_rw0 of=/tmp/readback bs=4096 count=1000

# Or use the test script
sudo ./test_bar_rw.sh
```

### Unload Driver

```bash
sudo rmmod bar_rw_driver
```

## How It Works

1. **Write Operation**:
   - User writes data to `/dev/bar_rw0`
   - Driver copies data to DMA buffer
   - Driver writes 64-byte command to BAR2+0x10000
   - Hardware processes command and reads from DMA buffer

2. **Read Operation**:
   - User reads from `/dev/bar_rw0`
   - Driver writes 64-byte READ command to BAR2+0x10000
   - Hardware writes data to DMA buffer
   - Driver copies data from DMA buffer to user

## Device Information

- **Vendor ID**: 0x1172 (Altera Corporation)
- **Device ID**: 0x0000
- **Location**: 15:00.0

## Debugging

```bash
# View driver messages
dmesg | grep bar_rw

# Check stats
dmesg | grep "Stats -"

# Run diagnostics
sudo ./diagnose.sh
```

## Performance

The driver uses:
- 1MB DMA buffer for transfers
- Block Multi-Queue (blk-mq) for parallelism
- 128 queue depth
- 512-byte logical blocks

## Notes

- The hardware automatically handles command translation
- No complex NVMe protocol implementation needed
- Simple 64-byte write interface
- Direct memory access via DMA
- Synchronous operation with minimal latency

## License

GPL v2