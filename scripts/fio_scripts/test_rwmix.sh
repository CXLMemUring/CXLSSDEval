#!/bin/bash

# Read/Write Mix Test Script

# Source common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

# Test name
TEST_NAME="Read/Write Mix"
RESULTS_DIR="${RESULTS_BASE_DIR}/raw/rwmix"

# Create results directory
mkdir -p "$RESULTS_DIR"

# Function to run read/write mix tests
run_rwmix_tests() {
    log_message "Starting $TEST_NAME tests"
    
    # Read percentages from config
    RW_MIX_RATIOS=(100 75 50 25 0)
    
    for read_pct in "${RW_MIX_RATIOS[@]}"; do
        local write_pct=$((100 - read_pct))
        local test_label="rwmix_r${read_pct}_w${write_pct}"
        local output_file="${RESULTS_DIR}/${test_label}.json"
        
        log_message "  Testing Read:Write = ${read_pct}:${write_pct}"
        
        run_fio_test "$test_label" "$output_file" \
            --name="${test_label}" \
            --filename="${DEVICE}" \
            --direct=1 \
            --rw=rw \
            --rwmixread="${read_pct}" \
            --bs=4k \
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
    
    # Run tests
    run_rwmix_tests
    
    log_message "===== Completed $TEST_NAME ====="
}

# Run if executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi