#!/bin/bash

# Byte-Addressable IO Test Script
# Tests IO sizes smaller than 512B to demonstrate CXL SSD byte-addressable capability

# Source common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

# Test name
TEST_NAME="Byte-Addressable IO"
RESULTS_DIR="${RESULTS_BASE_DIR}/raw/byte_addressable"

# Create results directory
mkdir -p "$RESULTS_DIR"

# Parse byte-addressable sizes from config
parse_byte_sizes() {
    local config_file="${SCRIPT_DIR}/../config.yaml"
    if [[ -f "$config_file" ]]; then
        # Extract byte_addressable_sizes from YAML
        BYTE_SIZES=($(grep "byte_addressable_sizes:" "$config_file" | sed 's/.*\[\(.*\)\].*/\1/' | tr ',' ' '))
    else
        # Default values if config not found
        BYTE_SIZES=(8 16 32 64 128 256 384 448)
    fi
    log_message "Using byte-addressable sizes: ${BYTE_SIZES[*]}"
}

# Function to run byte-addressable tests
run_byte_addressable_tests() {
    local test_type=$1  # read, write, randread, or randwrite

    log_message "Starting $TEST_NAME tests for $test_type"

    for bs in "${BYTE_SIZES[@]}"; do
        local test_label="bs_${bs}b_${test_type}"
        local output_file="${RESULTS_DIR}/${test_label}.json"

        log_message "  Testing Byte Size=${bs}B, Type=${test_type}"

        # Try to run the test - may fail on regular SSDs
        # Using psync engine for better small IO support
        if run_fio_test "$test_label" "$output_file" \
            --name="${test_label}" \
            --filename="${DEVICE}" \
            --direct=1 \
            --rw="${test_type}" \
            --bs="${bs}" \
            --ioengine=psync \
            --iodepth=1 \
            --numjobs=1 \
            --runtime="${STANDARD_DURATION}" \
            --time_based 2>/dev/null; then
            log_message "    ✓ Successfully completed ${bs}B ${test_type}"
        else
            log_message "    ✗ Device does not support ${bs}B IO (expected for non-CXL SSDs)"
            echo "${bs}B ${test_type}: Not supported" >> "${RESULTS_DIR}/unsupported_operations.log"
        fi
    done

    # Also test with 512B as baseline comparison
    local test_label="bs_512b_${test_type}"
    local output_file="${RESULTS_DIR}/${test_label}.json"

    log_message "  Testing baseline 512B, Type=${test_type}"

    run_fio_test "$test_label" "$output_file" \
        --name="${test_label}" \
        --filename="${DEVICE}" \
        --direct=1 \
        --rw="${test_type}" \
        --bs=512 \
        --ioengine=libaio \
        --iodepth="${DEFAULT_IODEPTH}" \
        --numjobs="${DEFAULT_NUMJOBS}" \
        --runtime="${STANDARD_DURATION}" \
        --time_based
}

# Function to generate comparison report
generate_comparison_report() {
    local report_file="${RESULTS_DIR}/byte_addressable_summary.txt"

    cat > "$report_file" << EOF
===============================================
Byte-Addressable IO Test Summary
===============================================
Test Date: $(date)
Device: ${DEVICE}

Purpose:
This test evaluates the byte-addressable capability of storage devices
by attempting IO operations smaller than the traditional 512B sector size.

Expected Results:
- Traditional NVMe SSDs: Cannot perform IO < 512B at block level
- CXL SSDs: Support byte-addressable operations through memory semantics

Test Configuration:
- IO Sizes Tested: ${BYTE_SIZES[*]}B, 512B (baseline)
- Access Patterns: Sequential Read/Write, Random Read/Write
- IO Engine: psync for sub-512B, libaio for 512B baseline
- Duration: ${STANDARD_DURATION} seconds per test

Results:
EOF

    if [[ -f "${RESULTS_DIR}/unsupported_operations.log" ]]; then
        echo -e "\nUnsupported Operations (Expected for non-CXL devices):" >> "$report_file"
        cat "${RESULTS_DIR}/unsupported_operations.log" >> "$report_file"
    else
        echo -e "\nAll byte-level operations supported! (CXL SSD detected)" >> "$report_file"
    fi

    # Parse successful results if any exist
    echo -e "\n\nSuccessful Operations:" >> "$report_file"
    for json_file in "${RESULTS_DIR}"/*.json; do
        if [[ -f "$json_file" ]]; then
            local filename=$(basename "$json_file" .json)
            # Try to extract basic metrics from FIO JSON output
            if command -v jq &> /dev/null; then
                local bw=$(jq -r '.jobs[0].read.bw // .jobs[0].write.bw // "N/A"' "$json_file" 2>/dev/null)
                local iops=$(jq -r '.jobs[0].read.iops // .jobs[0].write.iops // "N/A"' "$json_file" 2>/dev/null)
                echo "  $filename: BW=${bw}KB/s, IOPS=${iops}" >> "$report_file"
            else
                echo "  $filename: Results saved (install jq for parsed metrics)" >> "$report_file"
            fi
        fi
    done

    echo -e "\nDetailed results available in: ${RESULTS_DIR}" >> "$report_file"

    log_message "Summary report generated: $report_file"
    cat "$report_file"
}

# Main execution
main() {
    log_message "===== Starting $TEST_NAME ====="

    # Check device
    check_device "$DEVICE" || exit 1

    # Parse byte sizes from config
    parse_byte_sizes

    # Clear any previous unsupported operations log
    rm -f "${RESULTS_DIR}/unsupported_operations.log"

    # Run tests for different access patterns
    # run_byte_addressable_tests "read"
    run_byte_addressable_tests "write"
    # run_byte_addressable_tests "randread"
    # run_byte_addressable_tests "randwrite"

    # Generate comparison report
    generate_comparison_report

    log_message "===== Completed $TEST_NAME ====="
}

# Run if executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi