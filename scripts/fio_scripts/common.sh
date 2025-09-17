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
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG_FILE="${CONFIG_FILE:-$(dirname "$SCRIPT_DIR")/config.yaml}"
if [[ ! -f "$CONFIG_FILE" ]]; then
    echo "Error: Configuration file not found: $CONFIG_FILE"
    exit 1
fi

# Parse configuration
eval $(parse_yaml $CONFIG_FILE "CONFIG_")

# Export common variables
export DEVICE="${CONFIG_device:-/dev/nvme0n1}"
export FILESYSTEM="${CONFIG_filesystem:-ext4}"
export STANDARD_DURATION="${CONFIG_test_durations_standard:-30}"
export ENDURANCE_DURATION="${CONFIG_test_durations_endurance:-1200}"
export WARMUP_TIME="${CONFIG_test_durations_warmup:-60}"
export RESULTS_BASE_DIR="${CONFIG_output_base_dir:-results}"
export LOG_LEVEL="${CONFIG_logging_level:-INFO}"

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

# Function to check TRIM support
check_trim_support() {
    local device=$1
    local device_name=$(basename "$device")
    
    # Check if device supports discard (TRIM)
    if [[ -f "/sys/block/${device_name}/queue/discard_max_bytes" ]]; then
        local discard_max=$(cat "/sys/block/${device_name}/queue/discard_max_bytes")
        if [[ "$discard_max" != "0" ]]; then
            log_message "TRIM support detected for $device (max_bytes: $discard_max)"
            return 0
        fi
    fi
    
    log_message "Warning: TRIM not supported or disabled for $device"
    return 1
}

# Function to clear device using blkdiscard
clear_device() {
    local device=$1
    
    # Try blkdiscard if TRIM is supported
    if check_trim_support "$device"; then
        log_message "Clearing device $device using blkdiscard..."
        if blkdiscard "$device" 2>/dev/null; then
            log_message "Device cleared successfully with blkdiscard"
            return 0
        else
            log_message "Warning: blkdiscard failed, device may not be fully cleared"
        fi
    else
        log_message "TRIM not supported, skipping blkdiscard for $device"
    fi
    
    return 1
}

# Function to run fio test with standard parameters
run_fio_test() {
    local test_name=$1
    local output_file=$2
    shift 2
    
    # Clear device before each test
    clear_device "$DEVICE"
    
    # Wait 3 seconds for device to stabilize after discard
    log_message "Waiting 3 seconds for device to stabilize..."
    sleep 3
    
    log_message "Running test: $test_name"
    
    fio "$@" \
        --allow_mounted_write=1 \
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