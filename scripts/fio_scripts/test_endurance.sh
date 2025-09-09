#!/bin/bash

# Endurance Test Script

# Source common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

# Test name
TEST_NAME="Endurance Test"
RESULTS_DIR="${RESULTS_BASE_DIR}/raw/endurance"

# Create results directory
mkdir -p "$RESULTS_DIR"

# Function to run endurance test
run_endurance_test() {
    log_message "Starting $TEST_NAME (Duration: ${ENDURANCE_DURATION}s)"
    
    local test_label="endurance_randrw"
    local output_file="${RESULTS_DIR}/${test_label}.json"
    
    log_message "  Running endurance test with 70:30 read:write ratio"
    
    fio --name="${test_label}" \
        --filename="${DEVICE}" \
        --direct=1 \
        --rw=randrw \
        --rwmixread=70 \
        --bs=4k \
        --iodepth="${DEFAULT_IODEPTH}" \
        --numjobs="${DEFAULT_NUMJOBS}" \
        --runtime="${ENDURANCE_DURATION}" \
        --time_based \
        --lat_percentiles=1 \
        --percentile_list=90:95:99:99.9:99.99 \
        --output-format=json,normal \
        --output="$output_file" \
        --group_reporting \
        --write_bw_log="${RESULTS_DIR}/endurance_bw" \
        --write_iops_log="${RESULTS_DIR}/endurance_iops" \
        --write_lat_log="${RESULTS_DIR}/endurance_lat" \
        --log_avg_msec=1000
    
    if [[ $? -eq 0 ]]; then
        log_message "Endurance test completed successfully"
    else
        log_message "Endurance test failed"
        return 1
    fi
}

# Main execution
main() {
    log_message "===== Starting $TEST_NAME ====="
    
    # Check device
    check_device "$DEVICE" || exit 1
    
    # Run endurance test
    run_endurance_test
    
    log_message "===== Completed $TEST_NAME ====="
}

# Run if executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi