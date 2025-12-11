#!/bin/bash

# Master Raw Device Testing Script

# Source common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

# Function to run all raw device tests
run_all_raw_tests() {
    log_message "Starting all raw device tests on ${DEVICE}"
    
    # Array of test scripts to run
    TEST_SCRIPTS=(
        "test_raw_byte_addressable.sh"
        "test_qd_thread.sh"
        "test_blocksize.sh"
        "test_access_pattern.sh"
        "test_rwmix.sh"
        "test_distribution.sh"
        "test_endurance.sh"
    )
    
    # Run each test script
    for script in "${TEST_SCRIPTS[@]}"; do
        local script_path="${SCRIPT_DIR}/${script}"
        
        if [[ -f "$script_path" ]]; then
            log_message "Executing: $script"
            bash "$script_path"
            
            if [[ $? -ne 0 ]]; then
                log_message "Error: Failed to execute $script"
                return 1
            fi
        else
            log_message "Warning: Script not found: $script_path"
        fi
    done
    
    log_message "All raw device tests completed"
}

# Main execution
main() {
    log_message "========================================="
    log_message "Starting Raw Device Test Suite"
    log_message "Device: ${DEVICE}"
    log_message "Results Directory: ${RESULTS_BASE_DIR}/raw"
    log_message "========================================="
    
    # Check device
    check_device "$DEVICE" || exit 1
    
    # Run all tests
    run_all_raw_tests
    
    log_message "========================================="
    log_message "Raw Device Test Suite Completed"
    log_message "========================================="
}

# Run if executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi
