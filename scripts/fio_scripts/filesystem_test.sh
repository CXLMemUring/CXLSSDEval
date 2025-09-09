#!/bin/bash

# Filesystem Testing Script

# Source common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

# Filesystem specific variables
TEST_FILE_SIZE="100G"
TEST_FILE="${FILESYSTEM_MOUNT}/fio_test_file"
RESULTS_DIR="${RESULTS_BASE_DIR}/filesystem"

# Create results directory
mkdir -p "$RESULTS_DIR"

# Function to create and mount filesystem
setup_filesystem() {
    log_message "Setting up filesystem"
    
    # Check if mount point exists
    if [[ ! -d "$FILESYSTEM_MOUNT" ]]; then
        log_message "Creating mount point: $FILESYSTEM_MOUNT"
        sudo mkdir -p "$FILESYSTEM_MOUNT"
    fi
    
    # Check if already mounted
    if mountpoint -q "$FILESYSTEM_MOUNT"; then
        log_message "Filesystem already mounted at $FILESYSTEM_MOUNT"
        return 0
    fi
    
    # Create filesystem if not exists
    log_message "Creating ${FILESYSTEM_TYPE} filesystem on ${DEVICE}"
    sudo mkfs.${FILESYSTEM_TYPE} -F "$DEVICE" 2>/dev/null
    
    # Mount filesystem
    log_message "Mounting filesystem"
    sudo mount -t ${FILESYSTEM_TYPE} "$DEVICE" "$FILESYSTEM_MOUNT"
    
    if [[ $? -eq 0 ]]; then
        log_message "Filesystem mounted successfully"
        sudo chmod 777 "$FILESYSTEM_MOUNT"
    else
        log_message "Error: Failed to mount filesystem"
        return 1
    fi
}

# Function to unmount filesystem
cleanup_filesystem() {
    log_message "Cleaning up filesystem"
    
    # Remove test file if exists
    if [[ -f "$TEST_FILE" ]]; then
        log_message "Removing test file: $TEST_FILE"
        sudo rm -f "$TEST_FILE"
    fi
    
    # Unmount if mounted
    if mountpoint -q "$FILESYSTEM_MOUNT"; then
        log_message "Unmounting filesystem"
        sudo umount "$FILESYSTEM_MOUNT"
    fi
}

# Function to run filesystem test with standard parameters
run_fs_test() {
    local test_name=$1
    local output_file=$2
    shift 2
    
    log_message "Running filesystem test: $test_name"
    
    fio "$@" \
        --filename="$TEST_FILE" \
        --size="$TEST_FILE_SIZE" \
        --lat_percentiles=1 \
        --percentile_list=90:95:99:99.9:99.99 \
        --output-format=json,normal \
        --output="$output_file" \
        --group_reporting
    
    if [[ $? -eq 0 ]]; then
        log_message "Test completed: $test_name"
    else
        log_message "Test failed: $test_name"
        return 1
    fi
}

# Test 1: Block Size Variation
test_fs_blocksize() {
    log_message "Testing Block Size variations on filesystem"
    
    BLOCK_SIZES=(512 1k 4k 16k 64k 256k 1m 4m)
    
    for bs in "${BLOCK_SIZES[@]}"; do
        for test_type in "read" "write"; do
            local test_label="fs_bs_${bs}_${test_type}"
            local output_file="${RESULTS_DIR}/${test_label}.json"
            
            log_message "  Block Size=${bs}, Type=${test_type}"
            
            run_fs_test "$test_label" "$output_file" \
                --name="${test_label}" \
                --direct=1 \
                --rw="${test_type}" \
                --bs="${bs}" \
                --iodepth=32 \
                --numjobs=1 \
                --runtime="${STANDARD_DURATION}" \
                --time_based
        done
    done
}

# Test 2: Queue Depth and Thread Variation
test_fs_qd_thread() {
    log_message "Testing Queue Depth and Thread variations on filesystem"
    
    QD_VALUES=(1 4 16 64 128)
    JOB_VALUES=(1 2 4 8)
    
    for qd in "${QD_VALUES[@]}"; do
        for jobs in "${JOB_VALUES[@]}"; do
            for test_type in "randread" "randwrite"; do
                local test_label="fs_qd${qd}_jobs${jobs}_${test_type}"
                local output_file="${RESULTS_DIR}/${test_label}.json"
                
                log_message "  QD=${qd}, Jobs=${jobs}, Type=${test_type}"
                
                run_fs_test "$test_label" "$output_file" \
                    --name="${test_label}" \
                    --direct=1 \
                    --rw="${test_type}" \
                    --bs=4k \
                    --iodepth="${qd}" \
                    --numjobs="${jobs}" \
                    --runtime="${STANDARD_DURATION}" \
                    --time_based
            done
        done
    done
}

# Test 3: Access Pattern
test_fs_access_pattern() {
    log_message "Testing Access Patterns on filesystem"
    
    PATTERNS=("read" "write" "randread" "randwrite")
    
    for pattern in "${PATTERNS[@]}"; do
        local test_label="fs_pattern_${pattern}"
        local output_file="${RESULTS_DIR}/${test_label}.json"
        
        log_message "  Pattern=${pattern}"
        
        run_fs_test "$test_label" "$output_file" \
            --name="${test_label}" \
            --direct=1 \
            --rw="${pattern}" \
            --bs=4k \
            --iodepth=32 \
            --numjobs=4 \
            --runtime="${STANDARD_DURATION}" \
            --time_based
    done
}

# Test 4: Read/Write Mix
test_fs_rwmix() {
    log_message "Testing Read/Write Mix on filesystem"
    
    RW_MIX_RATIOS=(100 75 50 25 0)
    
    for read_pct in "${RW_MIX_RATIOS[@]}"; do
        local write_pct=$((100 - read_pct))
        local test_label="fs_rwmix_r${read_pct}_w${write_pct}"
        local output_file="${RESULTS_DIR}/${test_label}.json"
        
        log_message "  Read:Write = ${read_pct}:${write_pct}"
        
        run_fs_test "$test_label" "$output_file" \
            --name="${test_label}" \
            --direct=1 \
            --rw=randrw \
            --rwmixread="${read_pct}" \
            --bs=4k \
            --iodepth=32 \
            --numjobs=4 \
            --runtime="${STANDARD_DURATION}" \
            --time_based
    done
}

# Test 5: File Operations Test
test_fs_file_operations() {
    log_message "Testing File Operations on filesystem"
    
    # Create multiple small files test
    local test_label="fs_file_create"
    local output_file="${RESULTS_DIR}/${test_label}.json"
    
    log_message "  Testing file creation"
    
    fio --name="${test_label}" \
        --directory="${FILESYSTEM_MOUNT}" \
        --direct=1 \
        --rw=write \
        --bs=4k \
        --filesize=1m \
        --nrfiles=1000 \
        --openfiles=10 \
        --fallocate=none \
        --iodepth=1 \
        --numjobs=1 \
        --runtime="${STANDARD_DURATION}" \
        --time_based \
        --lat_percentiles=1 \
        --percentile_list=90:95:99:99.9:99.99 \
        --output-format=json,normal \
        --output="$output_file" \
        --group_reporting
    
    # Clean up created files
    rm -rf "${FILESYSTEM_MOUNT}/fs_file_create.*"
}

# Main execution
main() {
    log_message "========================================="
    log_message "Starting Filesystem Test Suite"
    log_message "Device: ${DEVICE}"
    log_message "Mount Point: ${FILESYSTEM_MOUNT}"
    log_message "Filesystem Type: ${FILESYSTEM_TYPE}"
    log_message "Results Directory: ${RESULTS_DIR}"
    log_message "========================================="
    
    # Check device
    check_device "$DEVICE" || exit 1
    
    # Setup filesystem
    setup_filesystem || exit 1
    
    # Run all filesystem tests
    test_fs_blocksize
    test_fs_qd_thread
    test_fs_access_pattern
    test_fs_rwmix
    test_fs_file_operations
    
    # Cleanup
    cleanup_filesystem
    
    log_message "========================================="
    log_message "Filesystem Test Suite Completed"
    log_message "========================================="
}

# Run if executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi