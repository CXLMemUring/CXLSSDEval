#!/bin/bash

# Custom NVMe Driver Installation and Test Script

set -e

DRIVER_NAME="nvme_custom_driver"
DEVICE_PATH="/dev/nvme_custom0"

echo "Custom NVMe Driver Installation Script"
echo "======================================"

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "This script must be run as root"
    exit 1
fi

# Function to check if device exists
check_device() {
    echo "Checking for PCIe device 15:00.0..."
    if lspci | grep -q "15:00.0"; then
        echo "Device found:"
        lspci | grep "15:00.0"
        echo ""
        echo "Detailed device information:"
        lspci -vv -s 15:00.0 | head -20
    else
        echo "Warning: Device 15:00.0 not found. You may need to update the vendor/device IDs in the driver."
        echo "Available devices:"
        lspci | grep -E "(Unassigned|Storage|NVMe)" | head -5
    fi
}

# Function to build the driver
build_driver() {
    echo "Building the driver..."
    make clean
    make
    
    if [ ! -f "${DRIVER_NAME}.ko" ]; then
        echo "Error: Driver build failed"
        exit 1
    fi
    
    echo "Driver built successfully"
}

# Function to load the driver
load_driver() {
    echo "Loading the driver..."
    
    # Remove if already loaded
    if lsmod | grep -q "$DRIVER_NAME"; then
        echo "Driver already loaded, removing..."
        rmmod "$DRIVER_NAME"
    fi
    
    # Load the driver
    insmod "${DRIVER_NAME}.ko"
    
    # Check if loaded successfully
    if lsmod | grep -q "$DRIVER_NAME"; then
        echo "Driver loaded successfully"
    else
        echo "Error: Failed to load driver"
        exit 1
    fi
    
    # Wait a moment for device creation
    sleep 1
    
    # Check if device node was created
    if [ -b "$DEVICE_PATH" ]; then
        echo "Block device created: $DEVICE_PATH"
        ls -l "$DEVICE_PATH"
        echo ""
        echo "Device appears in block device list:"
        lsblk | grep nvme_custom || echo "Device not yet visible in lsblk"
    else
        echo "Warning: Block device not found at $DEVICE_PATH"
        echo "Check dmesg for driver messages:"
        dmesg | grep nvme_custom | tail -5
    fi
}

# Function to build test application
build_test() {
    echo "Building test application..."
    gcc -o nvme_test_app nvme_test_app.c
    
    if [ ! -f "nvme_test_app" ]; then
        echo "Error: Test application build failed"
        exit 1
    fi
    
    echo "Test application built successfully"
}

# Function to run test
run_test() {
    echo "Running test application..."
    echo "=========================="
    ./nvme_test_app
}

# Function to show driver info
show_info() {
    echo "Driver Information:"
    echo "=================="
    modinfo "${DRIVER_NAME}.ko"
    
    echo ""
    echo "Driver Status:"
    echo "=============="
    if lsmod | grep -q "$DRIVER_NAME"; then
        echo "Driver is loaded"
        lsmod | grep "$DRIVER_NAME"
    else
        echo "Driver is not loaded"
    fi
    
    echo ""
    echo "Recent kernel messages:"
    echo "======================="
    dmesg | tail -10 | grep -i "nvme_custom\|$DRIVER_NAME" || echo "No recent messages found"
}

# Function to unload driver
unload_driver() {
    echo "Unloading driver..."
    if lsmod | grep -q "$DRIVER_NAME"; then
        rmmod "$DRIVER_NAME"
        echo "Driver unloaded"
    else
        echo "Driver is not loaded"
    fi
    
    # Remove device node if it exists
    if [ -b "$DEVICE_PATH" ]; then
        echo "Block device will be removed automatically"
    fi
}

# Main menu
case "${1:-help}" in
    "build")
        check_device
        build_driver
        ;;
    "install")
        check_device
        build_driver
        load_driver
        ;;
    "test")
        build_test
        run_test
        ;;
    "full")
        check_device
        build_driver
        load_driver
        build_test
        run_test
        ;;
    "info")
        show_info
        ;;
    "unload")
        unload_driver
        ;;
    "clean")
        make clean
        rm -f nvme_test_app
        echo "Clean completed"
        ;;
    "help"|*)
        echo "Usage: $0 {build|install|test|full|info|unload|clean|help}"
        echo ""
        echo "Commands:"
        echo "  build    - Build the driver only"
        echo "  install  - Build and load the driver"
        echo "  test     - Build and run test application"
        echo "  full     - Build driver, load it, build test, and run test"
        echo "  info     - Show driver information and status"
        echo "  unload   - Unload the driver and remove device node"
        echo "  clean    - Clean build files"
        echo "  help     - Show this help message"
        echo ""
        echo "Note: This script must be run as root for driver operations"
        ;;
esac