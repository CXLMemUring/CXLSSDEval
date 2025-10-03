#!/bin/bash

# PCIe SSD Driver Diagnostic Script

echo "PCIe SSD Driver Diagnostic Tool"
echo "==============================="

# Function to check system requirements
check_requirements() {
    echo "Checking system requirements..."
    
    # Check if running as root
    if [ "$EUID" -ne 0 ]; then
        echo "❌ Not running as root"
        echo "   Run with: sudo $0"
        return 1
    else
        echo "✅ Running as root"
    fi
    
    # Check kernel headers
    KERNEL_VERSION=$(uname -r)
    if [ -d "/lib/modules/$KERNEL_VERSION/build" ]; then
        echo "✅ Kernel headers found for $KERNEL_VERSION"
    else
        echo "❌ Kernel headers not found for $KERNEL_VERSION"
        echo "   Install with: apt-get install linux-headers-$KERNEL_VERSION"
        return 1
    fi
    
    # Check required tools
    for tool in gcc make lspci dmesg; do
        if command -v "$tool" &> /dev/null; then
            echo "✅ $tool found"
        else
            echo "❌ $tool not found"
            return 1
        fi
    done
    
    return 0
}

# Function to check PCIe device
check_pcie_device() {
    echo ""
    echo "Checking PCIe devices..."
    
    # List all PCIe devices
    echo "All PCIe devices:"
    lspci | head -10
    
    echo ""
    
    # Check for device at 15:00.0
    if lspci | grep -q "15:00.0"; then
        echo "✅ Device found at 15:00.0:"
        lspci -v -s 15:00.0
        
        echo ""
        echo "Device details:"
        lspci -vv -s 15:00.0 | grep -E "(Vendor|Device|Class|Memory|I/O|IRQ)"
        
    else
        echo "❌ No device found at 15:00.0"
        echo ""
        echo "Available devices on bus 15:"
        lspci | grep "^15:" || echo "No devices on bus 15"
        
        echo ""
        echo "Available storage/unassigned devices:"
        lspci | grep -E "(Storage|Unassigned|NVMe)"
        
        echo ""
        echo "To find your device:"
        echo "1. Run 'lspci' to list all devices"
        echo "2. Update VENDOR_ID and DEVICE_ID in nvme_custom_driver.c"
        echo "3. Update the PCI device ID in the script"
    fi
}

# Function to check driver status
check_driver_status() {
    echo ""
    echo "Checking driver status..."
    
    DRIVER_NAME="nvme_custom_driver"
    
    # Check if driver is loaded
    if lsmod | grep -q "$DRIVER_NAME"; then
        echo "✅ Driver is loaded:"
        lsmod | grep "$DRIVER_NAME"
        
        # Check block device
        if [ -b "/dev/nvme_custom0" ]; then
            echo "✅ Block device exists:"
            ls -l "/dev/nvme_custom0"
            echo "Block device info:"
            lsblk | grep nvme_custom || echo "Device not visible in lsblk yet"
        else
            echo "❌ Block device /dev/nvme_custom0 not found"
        fi
        
    else
        echo "❌ Driver is not loaded"
    fi
    
    # Check recent kernel messages
    echo ""
    echo "Recent kernel messages (last 20 lines):"
    dmesg | tail -20 | grep -i "nvme_custom\|$DRIVER_NAME" || echo "No driver messages found"
}

# Function to check memory and resources
check_resources() {
    echo ""
    echo "Checking system resources..."
    
    # Check memory
    echo "Memory information:"
    free -h | head -2
    
    echo ""
    echo "DMA zones:"
    cat /proc/buddyinfo | head -3
    
    echo ""
    echo "IOMMU status:"
    if [ -d "/sys/kernel/iommu_groups" ]; then
        echo "✅ IOMMU available"
        ls /sys/kernel/iommu_groups/ | wc -l | xargs echo "IOMMU groups:"
    else
        echo "❌ IOMMU not available or not enabled"
    fi
    
    # Check for any PCIe errors
    echo ""
    echo "PCIe error status:"
    dmesg | grep -i "pcie.*error\|pci.*error" | tail -5 || echo "No PCIe errors found"
}

# Function to test build
test_build() {
    echo ""
    echo "Testing driver build..."
    
    if [ -f "Makefile" ]; then
        echo "✅ Makefile found"
        
        # Clean previous build
        make clean &> /dev/null
        
        # Try to build
        echo "Building driver..."
        if make &> build.log; then
            echo "✅ Driver build successful"
            if [ -f "pcie_ssd_driver.ko" ]; then
                echo "✅ Kernel module created"
                modinfo pcie_ssd_driver.ko | head -10
            fi
        else
            echo "❌ Driver build failed"
            echo "Build errors:"
            tail -10 build.log
        fi
    else
        echo "❌ Makefile not found"
    fi
}

# Function to show recommendations
show_recommendations() {
    echo ""
    echo "Recommendations:"
    echo "==============="
    
    # Check if device is at 00:15.0
    if ! lspci | grep -q "00:15.0"; then
        echo "1. Update device location in driver:"
        echo "   - Find your device with 'lspci'"
        echo "   - Update vendor/device IDs in pcie_ssd_driver.c"
    fi
    
    # Check if driver builds
    if [ ! -f "pcie_ssd_driver.ko" ]; then
        echo "2. Fix build issues:"
        echo "   - Install kernel headers: apt-get install linux-headers-$(uname -r)"
        echo "   - Check build log for specific errors"
    fi
    
    # General recommendations
    echo "3. Development workflow:"
    echo "   - Test on a non-production system first"
    echo "   - Use './install.sh build' to build driver"
    echo "   - Use './install.sh install' to load driver"
    echo "   - Use './install.sh test' to run tests"
    echo "   - Check dmesg for driver messages"
    
    echo ""
    echo "4. If device is not detected:"
    echo "   - Verify PCIe slot connection"
    echo "   - Check BIOS/UEFI PCIe settings"
    echo "   - Ensure device is powered correctly"
    echo "   - Check for PCIe lane configuration"
}

# Function to collect system info
collect_system_info() {
    echo ""
    echo "System Information:"
    echo "=================="
    
    echo "Kernel: $(uname -r)"
    echo "Architecture: $(uname -m)"
    echo "Distribution: $(lsb_release -d 2>/dev/null | cut -f2 || echo "Unknown")"
    echo "GCC Version: $(gcc --version 2>/dev/null | head -1 || echo "Not found")"
    
    echo ""
    echo "PCIe Information:"
    echo "================"
    echo "PCIe devices count: $(lspci | wc -l)"
    echo "PCIe tree:"
    lspci -t 2>/dev/null || echo "lspci -t not available"
}

# Main execution
main() {
    collect_system_info
    
    if check_requirements; then
        check_pcie_device
        check_driver_status
        check_resources
        test_build
    fi
    
    show_recommendations
    
    echo ""
    echo "Diagnostic complete. Check the output above for issues."
    echo "For more help, see README.md or run './install.sh help'"
}

# Run main function
main