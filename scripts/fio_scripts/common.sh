#!/bin/bash

# Common functions and configuration loader for FIO scripts

# Function to parse YAML configuration
parse_yaml() {
    local prefix=$2
    local s='[[:space:]]*' w='[a-zA-Z0-9_]*' fs=$(echo @|tr @ '\034')
    # First remove comments, then parse
    sed 's/#.*$//' $1 | sed -ne "s|^\($s\):|\1|" \
        -e "s|^\($s\)\($w\)$s:$s[\"']\(.*\)[\"']$s\$|\1$fs\2$fs\3|p" \
        -e "s|^\($s\)\($w\)$s:$s\(.*\)$s\$|\1$fs\2$fs\3|p" |
    awk -F$fs '{
        indent = length($1)/2;
        vname[indent] = $2;
        for (i in vname) {if (i > indent) {delete vname[i]}}
        if (length($3) > 0) {
            # Clean the value - remove quotes and trim spaces
            value = $3;
            gsub(/^[[:space:]]*"|"[[:space:]]*$/, "", value);
            gsub(/^[[:space:]]*'"'"'|'"'"'[[:space:]]*$/, "", value);
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", value);
            vn=""; for (i=0; i<indent; i++) {vn=(vn)(vname[i])("_")}
            printf("%s%s%s=\"%s\"\n", "'$prefix'",vn, $2, value);
        }
    }'
}

# Load configuration
CONFIG_FILE="${CONFIG_FILE:-/home/huyp/CXLSSDEval/scripts/config.yaml}"
if [[ ! -f "$CONFIG_FILE" ]]; then
    echo "Error: Configuration file not found: $CONFIG_FILE"
    exit 1
fi

# Parse configuration
eval $(parse_yaml $CONFIG_FILE "CONFIG_")

# Export common variables
export DEVICE="${CONFIG_device_raw_device}"
export FILESYSTEM_MOUNT="${CONFIG_device_filesystem_mount}"
export FILESYSTEM_TYPE="${CONFIG_device_filesystem_type}"
export STANDARD_DURATION="${CONFIG_test_duration_standard}"
export ENDURANCE_DURATION="${CONFIG_test_duration_endurance}"
export WARMUP_TIME="${CONFIG_test_duration_warmup}"
export RESULTS_BASE_DIR="${CONFIG_output_results_dir}"
export LOG_LEVEL="${CONFIG_output_log_level}"

# Export test parameters
export DEFAULT_IODEPTH="${CONFIG_test_params_default_iodepth:-32}"
export DEFAULT_NUMJOBS="${CONFIG_test_params_default_numjobs:-1}"

# Create results directories if they don't exist
mkdir -p "${RESULTS_BASE_DIR}/raw"
mkdir -p "${RESULTS_BASE_DIR}/filesystem"
mkdir -p "${RESULTS_BASE_DIR}/rocksdb"
mkdir -p "${RESULTS_BASE_DIR}/summary"

# Function to log messages
log_message() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1"
}

# Function to check if device exists
check_device() {
    if [[ ! -b "$1" ]]; then
        log_message "Error: Device $1 does not exist or is not a block device"
        return 1
    fi
    return 0
}

# Function to run fio test with standard parameters
run_fio_test() {
    local test_name=$1
    local output_file=$2
    shift 2
    
    log_message "Running test: $test_name"
    
    fio "$@" \
        --lat_percentiles=1 \
        --percentile_list=90:95:99:99.9:99.99 \
        --output-format=json,normal \
        --output="$output_file" \
        --group_reporting
    
    if [[ $? -eq 0 ]]; then
        log_message "Test completed: $test_name"
    else
        log_message "Test failed: $test_name"
        return 1
    fi
}