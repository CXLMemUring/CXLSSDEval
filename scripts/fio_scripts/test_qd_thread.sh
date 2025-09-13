#!/bin/bash

# Thread Variation Test Script

# Source common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

# Test name
TEST_NAME="Thread Variation"
RESULTS_DIR="${RESULTS_BASE_DIR}/raw/qd_thread"

# Create results directory
mkdir -p "$RESULTS_DIR"

# Function to run thread tests
run_thread_tests() {
    local test_type=$1  # read or write
    
    log_message "Starting $TEST_NAME tests for $test_type"
    
    # Parse job counts from config
    JOB_VALUES=(1 2 4 8 16 32)
    
    for jobs in "${JOB_VALUES[@]}"; do
        local test_label="jobs${jobs}_${test_type}"
        local output_file="${RESULTS_DIR}/${test_label}.json"
        
        log_message "  Testing Jobs=${jobs}, Type=${test_type}"
        
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
            --bs=4k \
            --iodepth="${DEFAULT_IODEPTH}" \
            --numjobs="${jobs}" \
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
    run_thread_tests "read"
    run_thread_tests "write"
    
    log_message "===== Completed $TEST_NAME ====="
}

# Run if executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi