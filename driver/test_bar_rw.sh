#!/bin/bash
# Test script for BAR Read/Write Driver

set -e

echo "BAR Read/Write Driver Test"
echo "==========================="

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root"
    exit 1
fi

# Load the driver
echo "Loading driver..."
insmod bar_rw_driver.ko

# Wait for device
sleep 1

# Check if device was created
if [ -b "/dev/bar_rw0" ]; then
    echo "✅ Block device created: /dev/bar_rw0"
    ls -l /dev/bar_rw0
else
    echo "❌ Block device not found"
    dmesg | tail -10
    exit 1
fi

# Show device info
echo ""
echo "Device information:"
lsblk | grep bar_rw || true
cat /proc/partitions | grep bar_rw || true

# Test write
echo ""
echo "Testing write operation..."
dd if=/dev/urandom of=/tmp/test_data bs=4096 count=256
dd if=/tmp/test_data of=/dev/bar_rw0 bs=4096 count=256

# Test read
echo ""
echo "Testing read operation..."
dd if=/dev/bar_rw0 of=/tmp/read_data bs=4096 count=256

# Verify
echo ""
echo "Verifying data..."
if cmp /tmp/test_data /tmp/read_data; then
    echo "✅ Data verification PASSED"
else
    echo "❌ Data verification FAILED"
fi

# Cleanup
rm -f /tmp/test_data /tmp/read_data

# Show stats
echo ""
echo "Driver messages:"
dmesg | grep bar_rw | tail -10

echo ""
echo "Test completed!"