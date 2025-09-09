#!/bin/bash

# Block Size Variation Test Script

# Source common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

# Test name
TEST_NAME="Block Size Variation"
RESULTS_DIR="${RESULTS_BASE_DIR}/raw/blocksize"

# Create results directory
mkdir -p "$RESULTS_DIR"

# Function to run block size tests
run_blocksize_tests() {
    local test_type=$1  # read or write
    
    log_message "Starting $TEST_NAME tests for $test_type"
    
    # Block sizes from config
    BLOCK_SIZES=(512 1k 4k 16k 64k 256k 1m 4m 16m 64m)
    
    for bs in "${BLOCK_SIZES[@]}"; do
        local test_label="bs_${bs}_${test_type}"
        local output_file="${RESULTS_DIR}/${test_label}.json"
        
        log_message "  Testing Block Size=${bs}, Type=${test_type}"
        
        if [[ "$test_type" == "read" ]]; then
            rw_pattern="read"
        else
            rw_pattern="write"
        fi
        
        run_fio_test "$test_label" "$output_file" \
            --name="${test_label}" \
            --filename="${DEVICE}" \
            --direct=1 \
            --rw="${rw_pattern}" \
            --bs="${bs}" \
            --iodepth="${DEFAULT_IODEPTH}" \
            --numjobs="${DEFAULT_NUMJOBS}" \
            --runtime="${STANDARD_DURATION}" \
            --time_based
    done
}

# Main execution
main() {
    log_message "===== Starting $TEST_NAME ====="
    
    # Check device
    check_device "$DEVICE" || exit 1
    
    # Run tests for both read and write
    run_blocksize_tests "read"
    run_blocksize_tests "write"
    
    log_message "===== Completed $TEST_NAME ====="
}

# Run if executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi