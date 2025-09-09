#!/bin/bash

# Access Distribution Test Script

# Source common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

# Test name
TEST_NAME="Access Distribution"
RESULTS_DIR="${RESULTS_BASE_DIR}/raw/distribution"

# Create results directory
mkdir -p "$RESULTS_DIR"

# Function to run distribution tests
run_distribution_tests() {
    local test_type=$1  # read or write
    
    log_message "Starting $TEST_NAME tests for $test_type"
    
    if [[ "$test_type" == "read" ]]; then
        rw_pattern="randread"
    else
        rw_pattern="randwrite"
    fi
    
    # Test uniform distribution
    local test_label="dist_uniform_${test_type}"
    local output_file="${RESULTS_DIR}/${test_label}.json"
    
    log_message "  Testing Distribution=uniform, Type=${test_type}"
    
    run_fio_test "$test_label" "$output_file" \
        --name="${test_label}" \
        --filename="${DEVICE}" \
        --direct=1 \
        --rw="${rw_pattern}" \
        --bs=4k \
        --iodepth="${DEFAULT_IODEPTH}" \
        --numjobs="${DEFAULT_NUMJOBS}" \
        --runtime="${STANDARD_DURATION}" \
        --time_based
    
    # Test Zipfian distribution
    test_label="dist_zipf_${test_type}"
    output_file="${RESULTS_DIR}/${test_label}.json"
    
    log_message "  Testing Distribution=zipf:1.2, Type=${test_type}"
    
    run_fio_test "$test_label" "$output_file" \
        --name="${test_label}" \
        --filename="${DEVICE}" \
        --direct=1 \
        --rw="${rw_pattern}" \
        --bs=4k \
        --iodepth="${DEFAULT_IODEPTH}" \
        --numjobs="${DEFAULT_NUMJOBS}" \
        --runtime="${STANDARD_DURATION}" \
        --time_based \
        --random_distribution=zipf:1.2
    
    # Test Normal distribution
    test_label="dist_normal_${test_type}"
    output_file="${RESULTS_DIR}/${test_label}.json"
    
    log_message "  Testing Distribution=normal:50, Type=${test_type}"
    
    run_fio_test "$test_label" "$output_file" \
        --name="${test_label}" \
        --filename="${DEVICE}" \
        --direct=1 \
        --rw="${rw_pattern}" \
        --bs=4k \
        --iodepth="${DEFAULT_IODEPTH}" \
        --numjobs="${DEFAULT_NUMJOBS}" \
        --runtime="${STANDARD_DURATION}" \
        --time_based \
        --random_distribution=normal:50
    
    # Test Pareto distribution
    test_label="dist_pareto_${test_type}"
    output_file="${RESULTS_DIR}/${test_label}.json"
    
    log_message "  Testing Distribution=pareto:0.9, Type=${test_type}"
    
    run_fio_test "$test_label" "$output_file" \
        --name="${test_label}" \
        --filename="${DEVICE}" \
        --direct=1 \
        --rw="${rw_pattern}" \
        --bs=4k \
        --iodepth="${DEFAULT_IODEPTH}" \
        --numjobs="${DEFAULT_NUMJOBS}" \
        --runtime="${STANDARD_DURATION}" \
        --time_based \
        --random_distribution=pareto:0.9
}

# Main execution
main() {
    log_message "===== Starting $TEST_NAME ====="
    
    # Check device
    check_device "$DEVICE" || exit 1
    
    # Run tests for both read and write
    run_distribution_tests "read"
    run_distribution_tests "write"
    
    log_message "===== Completed $TEST_NAME ====="
}

# Run if executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi