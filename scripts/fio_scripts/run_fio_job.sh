#!/bin/bash

# FIO Job Runner Script
# This script runs FIO job files with configuration from config.yaml

# Source common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

# Function to run FIO job file
run_fio_job() {
    local job_file=$1
    local output_dir=$2
    
    if [[ ! -f "$job_file" ]]; then
        log_message "Error: Job file not found: $job_file"
        return 1
    fi
    
    local job_name=$(basename "$job_file" .fio)
    local output_file="${output_dir}/${job_name}.json"
    
    log_message "Running FIO job: $job_name"
    
    # Export environment variables for FIO job files
    export DEVICE="$DEVICE"
    export DURATION="$STANDARD_DURATION"
    export ENDURANCE_DURATION="$ENDURANCE_DURATION"
    export WARMUP="$WARMUP_TIME"
    
    # Run FIO with job file
    fio "$job_file" \
        --allow_mounted_write=1 \
        --output-format=json,normal \
        --output="$output_file"
    
    if [[ $? -eq 0 ]]; then
        log_message "Job completed: $job_name"
    else
        log_message "Job failed: $job_name"
        return 1
    fi
}

# Main execution
main() {
    local job_file=$1
    local output_dir=${2:-"${RESULTS_BASE_DIR}/fio_jobs"}
    
    if [[ -z "$job_file" ]]; then
        echo "Usage: $0 <job_file> [output_dir]"
        echo "Example: $0 ../fio_jobs/qd_thread_test.fio"
        exit 1
    fi
    
    # Create output directory
    mkdir -p "$output_dir"
    
    # Check device
    check_device "$DEVICE" || exit 1
    
    # Run the job
    run_fio_job "$job_file" "$output_dir"
}

# Run if executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi