#!/bin/bash

# Test Environment Cleanup Script
# This script cleans up the test environment including filesystems and temporary files

set -e  # Exit on error

# Source common functions if available
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -f "${SCRIPT_DIR}/fio_scripts/common.sh" ]; then
    source "${SCRIPT_DIR}/fio_scripts/common.sh"
else
    # Define basic logging if common.sh not available
    log_message() {
        echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1"
    }
fi

# Load configuration
CONFIG_FILE="${CONFIG_FILE:-${SCRIPT_DIR}/config.yaml}"

# Parse basic config values if config exists
if [ -f "$CONFIG_FILE" ]; then
    FILESYSTEM_MOUNT=$(grep "filesystem_mount:" "$CONFIG_FILE" | awk '{print $2}' | tr -d '"')
    RESULTS_DIR=$(grep "results_dir:" "$CONFIG_FILE" | awk '{print $2}' | tr -d '"')
    DB_PATH=$(grep "db_path:" "$CONFIG_FILE" | awk '{print $2}' | tr -d '"')
else
    # Use defaults if config not found
    FILESYSTEM_MOUNT="/nv1"
    RESULTS_DIR="./results"
    DB_PATH="/nv1/rocksdb_test"
fi

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to print colored messages
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to unmount filesystem
unmount_filesystem() {
    log_info "Checking for mounted filesystems..."
    
    if mountpoint -q "$FILESYSTEM_MOUNT" 2>/dev/null; then
        log_warn "Found mounted filesystem at $FILESYSTEM_MOUNT"
        
        # Check for active processes
        if lsof "$FILESYSTEM_MOUNT" &>/dev/null; then
            log_warn "Active processes using filesystem. Attempting to stop them..."
            sudo fuser -km "$FILESYSTEM_MOUNT" 2>/dev/null || true
            sleep 2
        fi
        
        # Unmount filesystem
        log_info "Unmounting filesystem..."
        sudo umount "$FILESYSTEM_MOUNT"
        
        if [ $? -eq 0 ]; then
            log_info "Filesystem unmounted successfully"
        else
            log_error "Failed to unmount filesystem. Trying force unmount..."
            sudo umount -f "$FILESYSTEM_MOUNT" 2>/dev/null || true
        fi
    else
        log_info "No filesystem mounted at $FILESYSTEM_MOUNT"
    fi
    
    # Remove mount point if empty
    if [ -d "$FILESYSTEM_MOUNT" ]; then
        if [ -z "$(ls -A $FILESYSTEM_MOUNT 2>/dev/null)" ]; then
            log_info "Removing empty mount point directory"
            sudo rmdir "$FILESYSTEM_MOUNT" 2>/dev/null || true
        fi
    fi
}

# Function to clean temporary files
clean_temp_files() {
    log_info "Cleaning temporary files..."
    
    # Clean FIO temporary files
    local fio_temps=(
        "/tmp/fio*"
        "/tmp/*.fio"
        "/var/tmp/fio*"
    )
    
    for pattern in "${fio_temps[@]}"; do
        if ls $pattern &>/dev/null; then
            log_info "Removing FIO temporary files: $pattern"
            sudo rm -rf $pattern
        fi
    done
    
    # Clean RocksDB test databases
    if [ -n "$DB_PATH" ] && [ -d "$DB_PATH" ]; then
        log_warn "Found RocksDB test database at $DB_PATH"
        read -p "Remove RocksDB test database? (y/N): " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            log_info "Removing RocksDB test database..."
            sudo rm -rf "$DB_PATH"
        fi
    fi
    
    # Clean other test databases in /tmp
    local rocksdb_temps=(
        "/tmp/rocksdb_test*"
        "/tmp/dbbench*"
    )
    
    for pattern in "${rocksdb_temps[@]}"; do
        if ls $pattern &>/dev/null; then
            log_info "Removing RocksDB temporary files: $pattern"
            sudo rm -rf $pattern
        fi
    done
}

# Function to clean log files
clean_logs() {
    log_info "Cleaning log files..."
    
    local log_dir="${SCRIPT_DIR}/logs"
    
    if [ -d "$log_dir" ]; then
        read -p "Remove all log files in $log_dir? (y/N): " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            log_info "Removing log files..."
            rm -rf "$log_dir"/*
            log_info "Log files removed"
        else
            log_info "Keeping log files"
        fi
    else
        log_info "No log directory found"
    fi
}

# Function to clean test results
clean_results() {
    log_info "Checking test results..."
    
    if [ -d "$RESULTS_DIR" ]; then
        # Count result files
        local file_count=$(find "$RESULTS_DIR" -type f | wc -l)
        
        if [ $file_count -gt 0 ]; then
            log_warn "Found $file_count result files in $RESULTS_DIR"
            
            read -p "Remove all test results? (y/N): " -n 1 -r
            echo
            if [[ $REPLY =~ ^[Yy]$ ]]; then
                log_info "Removing test results..."
                rm -rf "$RESULTS_DIR"/*
                log_info "Test results removed"
            else
                log_info "Keeping test results"
            fi
        else
            log_info "No test results to clean"
        fi
    else
        log_info "No results directory found"
    fi
}

# Function to kill running test processes
kill_test_processes() {
    log_info "Checking for running test processes..."
    
    local processes_killed=0
    
    # Kill FIO processes
    if pgrep -x "fio" > /dev/null; then
        log_warn "Found running FIO processes"
        sudo pkill -TERM fio
        sleep 2
        if pgrep -x "fio" > /dev/null; then
            log_warn "Force killing FIO processes"
            sudo pkill -KILL fio
        fi
        processes_killed=$((processes_killed + 1))
    fi
    
    # Kill db_bench processes
    if pgrep -x "db_bench" > /dev/null; then
        log_warn "Found running db_bench processes"
        sudo pkill -TERM db_bench
        sleep 2
        if pgrep -x "db_bench" > /dev/null; then
            log_warn "Force killing db_bench processes"
            sudo pkill -KILL db_bench
        fi
        processes_killed=$((processes_killed + 1))
    fi
    
    if [ $processes_killed -eq 0 ]; then
        log_info "No test processes running"
    else
        log_info "Killed $processes_killed test process types"
    fi
}

# Function to reset device (optional, requires user confirmation)
reset_device() {
    log_info "Device reset option..."
    
    # Try to get device from config
    local device=$(grep "raw_device:" "$CONFIG_FILE" 2>/dev/null | awk '{print $2}' | tr -d '"')
    
    if [ -z "$device" ]; then
        log_warn "No device configured in config.yaml"
        return
    fi
    
    if [ ! -b "$device" ]; then
        log_warn "Device $device not found"
        return
    fi
    
    log_warn "Device reset will clear all data on $device"
    read -p "Are you ABSOLUTELY sure you want to reset $device? Type 'yes' to confirm: " -r
    
    if [ "$REPLY" == "yes" ]; then
        log_info "Resetting device $device..."
        
        # Secure erase if supported (NVMe)
        if [[ "$device" == *"nvme"* ]]; then
            log_info "Attempting NVMe secure erase..."
            sudo nvme format "$device" -s 1 2>/dev/null || {
                log_warn "NVMe secure erase not supported, using standard method"
                sudo dd if=/dev/zero of="$device" bs=1M count=100 status=progress 2>/dev/null || true
            }
        else
            # Standard method for other devices
            log_info "Clearing first 100MB of device..."
            sudo dd if=/dev/zero of="$device" bs=1M count=100 status=progress 2>/dev/null || true
        fi
        
        log_info "Device reset completed"
    else
        log_info "Device reset cancelled"
    fi
}

# Function to show cleanup summary
show_summary() {
    log_info "Cleanup Summary:"
    echo "  - Filesystems unmounted: ✓"
    echo "  - Test processes stopped: ✓"
    echo "  - Temporary files cleaned: ✓"
    
    # Check what remains
    local remaining_items=0
    
    if [ -d "$RESULTS_DIR" ] && [ -n "$(ls -A $RESULTS_DIR 2>/dev/null)" ]; then
        echo "  - Test results: Kept"
        remaining_items=$((remaining_items + 1))
    else
        echo "  - Test results: Cleaned"
    fi
    
    if [ -d "${SCRIPT_DIR}/logs" ] && [ -n "$(ls -A ${SCRIPT_DIR}/logs 2>/dev/null)" ]; then
        echo "  - Log files: Kept"
        remaining_items=$((remaining_items + 1))
    else
        echo "  - Log files: Cleaned"
    fi
    
    if [ $remaining_items -gt 0 ]; then
        log_info "Some items were kept. Run with --all to remove everything."
    else
        log_info "Environment fully cleaned"
    fi
}

# Main cleanup function
main() {
    echo "========================================="
    echo "Test Environment Cleanup Script"
    echo "========================================="
    
    # Parse command line arguments
    local clean_all=false
    local skip_confirm=false
    
    for arg in "$@"; do
        case $arg in
            --all)
                clean_all=true
                ;;
            --force|-f)
                skip_confirm=true
                ;;
            --help|-h)
                echo "Usage: $0 [OPTIONS]"
                echo "Options:"
                echo "  --all        Clean everything including results and logs"
                echo "  --force, -f  Skip confirmation prompts"
                echo "  --help, -h   Show this help message"
                exit 0
                ;;
        esac
    done
    
    if [ "$skip_confirm" = false ]; then
        log_warn "This will clean up the test environment"
        read -p "Continue? (y/N): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            log_info "Cleanup cancelled"
            exit 0
        fi
    fi
    
    # Perform cleanup steps
    log_info "Starting cleanup..."
    
    # Step 1: Kill running processes
    kill_test_processes
    
    # Step 2: Unmount filesystems
    unmount_filesystem
    
    # Step 3: Clean temporary files
    clean_temp_files
    
    # Step 4: Clean logs (optional)
    if [ "$clean_all" = true ] || [ "$skip_confirm" = true ]; then
        clean_logs
    else
        log_info "Skipping log cleanup (use --all to include)"
    fi
    
    # Step 5: Clean results (optional)
    if [ "$clean_all" = true ]; then
        clean_results
    else
        log_info "Skipping results cleanup (use --all to include)"
    fi
    
    # Step 6: Device reset (only with explicit confirmation)
    if [ "$clean_all" = true ] && [ "$skip_confirm" = false ]; then
        reset_device
    fi
    
    echo ""
    show_summary
    
    log_info "Cleanup completed successfully!"
}

# Run main function
main "$@"