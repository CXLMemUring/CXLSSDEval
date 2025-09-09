#!/bin/bash

# Access Pattern Test Script

# Source common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

# Test name
TEST_NAME="Access Pattern"
RESULTS_DIR="${RESULTS_BASE_DIR}/raw/access_pattern"

# Create results directory
mkdir -p "$RESULTS_DIR"

# Function to run access pattern tests
run_access_pattern_tests() {
    log_message "Starting $TEST_NAME tests"
    
    # Access patterns
    PATTERNS=("read" "write" "randread" "randwrite")
    
    for pattern in "${PATTERNS[@]}"; do
        local test_label="pattern_${pattern}"
        local output_file="${RESULTS_DIR}/${test_label}.json"
        
        log_message "  Testing Pattern=${pattern}"
        
        run_fio_test "$test_label" "$output_file" \
            --name="${test_label}" \
            --filename="${DEVICE}" \
            --direct=1 \
            --rw="${pattern}" \
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
    run_access_pattern_tests
    
    log_message "===== Completed $TEST_NAME ====="
}

# Run if executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi