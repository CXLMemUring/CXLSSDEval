#!/bin/bash

# Queue Depth and Thread Variation Test Script

# Source common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

# Test name
TEST_NAME="Queue Depth and Thread Variation"
RESULTS_DIR="${RESULTS_BASE_DIR}/raw/qd_thread"

# Create results directory
mkdir -p "$RESULTS_DIR"

# Function to run queue depth and thread tests
run_qd_thread_tests() {
    local test_type=$1  # read or write
    
    log_message "Starting $TEST_NAME tests for $test_type"
    
    # Parse queue depths from config
    QD_VALUES=(1 2 4 8 16 32 64 128)
    JOB_VALUES=(1 2 4 8 16 32)
    
    for qd in "${QD_VALUES[@]}"; do
        for jobs in "${JOB_VALUES[@]}"; do
            local test_label="qd${qd}_jobs${jobs}_${test_type}"
            local output_file="${RESULTS_DIR}/${test_label}.json"
            
            log_message "  Testing QD=${qd}, Jobs=${jobs}, Type=${test_type}"
            
            if [[ "$test_type" == "read" ]]; then
                rw_pattern="randread"
            else
                rw_pattern="randwrite"
            fi
            
            run_fio_test "$test_label" "$output_file" \
                --name="${test_label}" \
                --filename="${DEVICE}" \
                --direct=1 \
                --rw="${rw_pattern}" \
                --bs=4k \
                --iodepth="${qd}" \
                --numjobs="${jobs}" \
                --runtime="${STANDARD_DURATION}" \
                --time_based
        done
    done
}

# Main execution
main() {
    log_message "===== Starting $TEST_NAME ====="
    
    # Check device
    check_device "$DEVICE" || exit 1
    
    # Run tests for both read and write
    run_qd_thread_tests "read"
    run_qd_thread_tests "write"
    
    log_message "===== Completed $TEST_NAME ====="
}

# Run if executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi