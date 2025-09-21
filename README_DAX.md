# DAX Device Memory-Mapped I/O with FIO LD_PRELOAD Interception

This implementation provides memory-mapped DAX device access with MWAIT support and an LD_PRELOAD library to intercept FIO's read/write/fsync syscalls, redirecting them to DAX device operations.

## Components

### 1. Memory Device Implementation (`src/cxl_mwait_dax.cpp`)
- Direct memory-mapped access to memory devices (e.g., `/dev/mem` with offset)
- Support for byte-addressable operations (sub-512B)
- MWAIT/MONITOR support for efficient polling
- Atomic load/store operations
- Cache line flushing for persistence

### 2. FIO Interception Library (`src/fio_intercept.cpp`)
- LD_PRELOAD library that intercepts syscalls
- Redirects file I/O to memory-mapped DAX device
- Supports read, write, pread, pwrite, fsync, lseek
- Transparent to FIO - no modifications needed

### 3. Test Programs
- `test_mwait_dax`: Comprehensive DAX device testing
- `test_dax_fio.sh`: FIO benchmark with interception

## Building

```bash
# Using make
make -f Makefile.dax all

# Or using CMake
mkdir build && cd build
cmake ..
make fio_intercept test_mwait_dax
```

## Usage

### Direct DAX Device Testing

```bash
# Test memory device directly
./test_mwait_dax /dev/mem 0x100000000 16G all

# Specific tests
./test_mwait_dax /dev/mem 0x100000000 16G byte      # Byte-addressable ops
./test_mwait_dax /dev/mem 0x100000000 16G mwait    # MWAIT performance
./test_mwait_dax /dev/mem 0x100000000 16G latency  # Latency measurements
```

### FIO with LD_PRELOAD Interception

```bash
# Set environment variables
export FIO_INTERCEPT_ENABLE=1
export FIO_MEM_DEVICE=/dev/mem
export FIO_MEM_OFFSET=0x100000000
export FIO_MEM_SIZE=16G
export FIO_FILE_SIZE=1G
export LD_PRELOAD=./libfio_intercept.so

# Run FIO normally - I/O will be redirected to DAX
fio --name=test --rw=randwrite --bs=4k --size=1G --direct=1 --ioengine=psync --runtime=10

# Or use the test script
./scripts/test_dax_fio.sh
```

### Byte-Addressable Operations (Sub-512B)

The implementation supports I/O sizes smaller than 512B, which traditional SSDs cannot handle:

```bash
# Test with various sub-512B sizes
for bs in 64 128 256 384; do
    FIO_INTERCEPT_ENABLE=1 \
    FIO_MEM_DEVICE=/dev/mem \
    FIO_MEM_OFFSET=0x100000000 \
    FIO_MEM_SIZE=16G \
    LD_PRELOAD=./libfio_intercept.so \
    fio --name=byte_test --rw=randwrite --bs=${bs} --size=100M --direct=1
done
```

## Environment Variables

- `FIO_INTERCEPT_ENABLE`: Enable interception (0/1)
- `FIO_MEM_DEVICE`: Memory device path (e.g., /dev/mem)
- `FIO_MEM_OFFSET`: Memory offset (e.g., 0x100000000)
- `FIO_MEM_SIZE`: Total memory region size
- `FIO_FILE_SIZE`: Size per FIO test file
- `FIO_INTERCEPT_PATTERN`: Additional file patterns to intercept
- `FIO_DEBUG`: Enable debug output (0/1)

## Key Features

### 1. Memory-Mapped DAX Access
- Direct load/store to persistent memory
- No kernel involvement in data path
- Sub-microsecond latencies

### 2. MWAIT Support
- CPU waits on memory address changes
- Power-efficient polling
- Automatic fallback to regular polling if not supported

### 3. Byte Addressability
- Supports I/O sizes from 1 byte to 64MB
- No 512B sector alignment requirement
- Efficient for small random accesses

### 4. Persistence Guarantees
- Uses clflushopt + sfence for durability
- MAP_SYNC for DAX devices
- Proper cache line flushing

## Performance Benefits

1. **Ultra-low latency**: Direct memory access bypasses kernel
2. **Byte addressability**: Efficient for small I/O (< 512B)
3. **MWAIT efficiency**: Reduces CPU usage during polling
4. **No syscall overhead**: LD_PRELOAD redirects to memory ops

## Troubleshooting

### DAX Device Not Found
```bash
# Check for DAX devices
ls -la /dev/dax* /dev/pmem*

# Check kernel support
grep -i dax /boot/config-$(uname -r)
```

### Permission Denied
```bash
# Run with sudo or adjust permissions
sudo chmod 666 /dev/mem
# Note: /dev/mem access requires root privileges
```

### MWAIT Not Supported
- The code automatically falls back to regular polling
- Check CPU support: `grep monitor /proc/cpuinfo`

### Debug Output
```bash
export FIO_DEBUG=1  # Enable debug messages
```

## Example Results

```
=== Byte-Addressable Test ===
Size 64 bytes: PASSED
Size 128 bytes: PASSED
Size 256 bytes: PASSED

=== Throughput Test (Block size: 256 bytes) ===
Results:
  Total operations: 5234567
  Operations/sec: 1046913.4
  Bandwidth: 257.54 MB/s

=== Latency Test ===
Latency statistics (4KB writes):
  Average: 0.85 µs
  P50: 0.72 µs
  P90: 1.13 µs
  P99: 2.41 µs
```