#!/bin/bash

# Quick Test Script for Validation
# This script runs a quick blocksize test to verify the test framework
# Test duration is reduced to 5 seconds for quick validation

set -e  # Exit on error

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=========================================${NC}"
echo -e "${BLUE}Quick Test Script - Blocksize Test Only${NC}"
echo -e "${BLUE}=========================================${NC}"

# Create temporary config file with short duration
TEMP_CONFIG="/tmp/quick_test_config.yaml"

cat > "$TEMP_CONFIG" << EOF
# CXL SSD Quick Test Configuration

# Device Configuration
device:
  raw_device: "/dev/nvme1n2"      # Raw device path for CXL SSD
  filesystem_mount: "/nv1"         # Mount point for filesystem tests
  filesystem_type: "ext4"          # Filesystem type

# Test Duration Configuration (Short for quick test)
test_duration:
  standard: 5                      # Quick test - 5 seconds
  endurance: 10                    # Quick endurance - 10 seconds
  warmup: 1                        # Quick warmup - 1 second

# Test Parameters
test_params:
  queue_depths: [1, 4, 16]         # Reduced queue depths for quick test
  num_jobs: [1, 2]                 # Reduced job counts for quick test
  default_iodepth: 1               # Lower default for quick test
  default_numjobs: 1               # Default number of jobs
  block_sizes: ["4k", "64k", "1m"] # Only test 3 block sizes
  rw_patterns: ["read", "write"]
  rw_mix_ratios: [100, 50, 0]
  distributions: ["random"]

# Output Configuration
output:
  results_dir: "./quick_test_results"
  log_level: "normal"
  formats: ["json", "normal"]

# RocksDB Configuration
rocksdb:
  db_path: "/nv1/quick_rocksdb_test"
  num_keys: 1000
  value_size: 100
  benchmarks: ["fillseq", "readrandom"]
EOF

echo "Created temporary config file: $TEMP_CONFIG"

# Export config file location
export CONFIG_FILE="$TEMP_CONFIG"

# Source common functions with new config
source "${SCRIPT_DIR}/fio_scripts/common.sh"

# Create quick test results directory
QUICK_RESULTS_DIR="./quick_test_results"
mkdir -p "$QUICK_RESULTS_DIR/raw/blocksize"

echo -e "\n${YELLOW}Test Configuration:${NC}"
echo "  Device: $DEVICE"
echo "  Test Duration: 5 seconds"
echo "  Block Sizes: 4k, 64k, 1m"
echo "  IO Depth: $DEFAULT_IODEPTH"
echo "  Num Jobs: $DEFAULT_NUMJOBS"
echo "  Results: $QUICK_RESULTS_DIR"

# Check if device exists
if [[ ! -b "$DEVICE" ]]; then
    echo -e "${RED}Error: Device $DEVICE not found${NC}"
    echo "Please update the device path in the script or config"
    rm -f "$TEMP_CONFIG"
    exit 1
fi

echo -e "\n${BLUE}Starting Quick Blocksize Test...${NC}"

# Run only blocksize test with limited parameters
run_quick_blocksize_test() {
    local BLOCK_SIZES=("4k" "64k" "1m")
    local TEST_TYPES=("read" "write")
    
    for bs in "${BLOCK_SIZES[@]}"; do
        for test_type in "${TEST_TYPES[@]}"; do
            local test_label="quick_bs_${bs}_${test_type}"
            local output_file="${QUICK_RESULTS_DIR}/raw/blocksize/${test_label}.json"
            
            echo -e "\n${GREEN}Testing: Block Size=${bs}, Type=${test_type}${NC}"
            
            fio --name="${test_label}" \
                --filename="${DEVICE}" \
                --direct=1 \
                --rw="${test_type}" \
                --bs="${bs}" \
                --iodepth="${DEFAULT_IODEPTH}" \
                --numjobs="${DEFAULT_NUMJOBS}" \
                --runtime=5 \
                --time_based \
                --lat_percentiles=1 \
                --percentile_list=90:95:99:99.9:99.99 \
                --output-format=json \
                --output="$output_file" \
                --group_reporting
            
            if [[ $? -eq 0 ]]; then
                echo -e "${GREEN}✓ Test completed: ${test_label}${NC}"
                
                # Extract and display key metrics
                if command -v python3 &>/dev/null; then
                    python3 -c "
import json
import sys

try:
    with open('$output_file', 'r') as f:
        data = json.load(f)
    
    if 'jobs' in data and len(data['jobs']) > 0:
        job = data['jobs'][0]
        
        if '${test_type}' in job:
            io_data = job['${test_type}']
            iops = io_data.get('iops', 0)
            bw_mb = io_data.get('bw', 0) / 1024
            lat_us = io_data.get('lat_ns', {}).get('mean', 0) / 1000
            
            print(f'  IOPS: {iops:,.0f}')
            print(f'  Bandwidth: {bw_mb:,.1f} MB/s')
            print(f'  Latency: {lat_us:,.1f} μs')
except Exception as e:
    print(f'  Could not parse results: {e}')
"
                fi
            else
                echo -e "${RED}✗ Test failed: ${test_label}${NC}"
            fi
        done
    done
}

# Run the quick test
run_quick_blocksize_test

# Summary
echo -e "\n${BLUE}=========================================${NC}"
echo -e "${BLUE}Quick Test Summary${NC}"
echo -e "${BLUE}=========================================${NC}"

# Count result files
if [[ -d "$QUICK_RESULTS_DIR" ]]; then
    JSON_COUNT=$(find "$QUICK_RESULTS_DIR" -name "*.json" 2>/dev/null | wc -l)
    echo -e "${GREEN}Result files generated: $JSON_COUNT${NC}"
    
    # List all result files
    echo -e "\n${YELLOW}Result files:${NC}"
    find "$QUICK_RESULTS_DIR" -name "*.json" -type f | while read -r file; do
        size=$(stat -c%s "$file" 2>/dev/null || stat -f%z "$file" 2>/dev/null || echo "0")
        if [[ $size -gt 0 ]]; then
            echo -e "  ${GREEN}✓${NC} $file ($(( size / 1024 ))KB)"
        else
            echo -e "  ${RED}✗${NC} $file (empty)"
        fi
    done
else
    echo -e "${RED}No results directory found${NC}"
fi

# Validate results if validation script exists
if [[ -f "${SCRIPT_DIR}/validate_results.py" ]] && command -v python3 &>/dev/null; then
    echo -e "\n${YELLOW}Running result validation...${NC}"
    python3 "${SCRIPT_DIR}/validate_results.py" --results-dir "$QUICK_RESULTS_DIR" 2>/dev/null || true
fi

# Clean up temporary config
rm -f "$TEMP_CONFIG"

echo -e "\n${GREEN}Quick test completed!${NC}"
echo -e "${YELLOW}Full test results are in: $QUICK_RESULTS_DIR${NC}"
echo -e "\nTo run full tests, use: ./run_all_tests.sh"