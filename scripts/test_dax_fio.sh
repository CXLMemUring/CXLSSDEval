#!/bin/bash

# Test script for DAX device with FIO using LD_PRELOAD interception

set -e

# Configuration
DAX_DEVICE=${DAX_DEVICE:-"/dev/dax0.0"}  # Or /dev/pmem0
DAX_SIZE=${DAX_SIZE:-"16G"}
FIO_FILE_SIZE=${FIO_FILE_SIZE:-"1G"}
INTERCEPT_LIB="./libfio_intercept.so"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}DAX Device FIO Test with LD_PRELOAD${NC}"
echo "========================================"

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${YELLOW}Warning: May need root for DAX device access${NC}"
fi

# Build the intercept library
echo -e "\n${YELLOW}Building intercept library...${NC}"
g++ -shared -fPIC -O3 -march=native \
    ../src/fio_intercept.cpp \
    -o libfio_intercept.so \
    -ldl -lpthread

# Build DAX test program
echo -e "\n${YELLOW}Building DAX test program...${NC}"
g++ -O3 -march=native \
    ../src/cxl_mwait_dax.cpp \
    -o test_dax \
    -lpthread

# Function to run FIO test with interception
run_fio_test() {
    local test_name=$1
    local fio_params=$2

    echo -e "\n${GREEN}Running test: $test_name${NC}"

    # Set environment for interception
    export FIO_INTERCEPT_ENABLE=1
    export FIO_DAX_DEVICE=$DAX_DEVICE
    export FIO_DAX_SIZE=$DAX_SIZE
    export FIO_FILE_SIZE=$FIO_FILE_SIZE
    export FIO_DEBUG=${FIO_DEBUG:-0}
    export LD_PRELOAD=$INTERCEPT_LIB

    # Run FIO test
    fio --name=$test_name \
        --direct=1 \
        --ioengine=psync \
        --runtime=10 \
        --time_based \
        --group_reporting \
        --output-format=json \
        $fio_params \
        > results_${test_name}.json 2>&1

    unset LD_PRELOAD

    # Extract and display key metrics
    if command -v jq >/dev/null 2>&1; then
        echo "Results for $test_name:"
        jq '.jobs[0].read | {iops: .iops, bw_mbps: (.bw/1024), lat_usec: .lat_ns.mean/1000}' \
            results_${test_name}.json 2>/dev/null || true
        jq '.jobs[0].write | {iops: .iops, bw_mbps: (.bw/1024), lat_usec: .lat_ns.mean/1000}' \
            results_${test_name}.json 2>/dev/null || true
    fi
}

# Test 1: Sequential Read
run_fio_test "seq_read" \
    "--rw=read --bs=4k --size=$FIO_FILE_SIZE --filename=test.dat"

# Test 2: Sequential Write
run_fio_test "seq_write" \
    "--rw=write --bs=4k --size=$FIO_FILE_SIZE --filename=test.dat"

# Test 3: Random Read
run_fio_test "rand_read" \
    "--rw=randread --bs=4k --size=$FIO_FILE_SIZE --filename=test.dat"

# Test 4: Random Write
run_fio_test "rand_write" \
    "--rw=randwrite --bs=4k --size=$FIO_FILE_SIZE --filename=test.dat"

# Test 5: Mixed workload
run_fio_test "mixed_rw" \
    "--rw=randrw --rwmixread=70 --bs=4k --size=$FIO_FILE_SIZE --filename=test.dat"

# Test 6: Byte-addressable operations (sub-512B)
echo -e "\n${GREEN}Testing byte-addressable operations${NC}"
for bs in 64 128 256 384; do
    run_fio_test "byte_addr_${bs}B" \
        "--rw=randwrite --bs=${bs} --size=100M --filename=test.dat"
done

# Test 7: Direct DAX device test (without interception)
echo -e "\n${GREEN}Direct DAX device test${NC}"
if [ -e "$DAX_DEVICE" ]; then
    echo "Testing direct DAX device access..."
    ./test_dax $DAX_DEVICE
else
    echo -e "${YELLOW}DAX device not found at $DAX_DEVICE${NC}"
fi

# Compare with regular file I/O
echo -e "\n${GREEN}Comparison with regular file I/O (no interception)${NC}"
unset LD_PRELOAD
unset FIO_INTERCEPT_ENABLE

fio --name=regular_file \
    --rw=randwrite \
    --bs=4k \
    --size=1G \
    --direct=1 \
    --ioengine=psync \
    --runtime=10 \
    --time_based \
    --filename=/tmp/test_regular.dat \
    --output-format=normal

echo -e "\n${GREEN}Test completed!${NC}"

# Cleanup
rm -f libfio_intercept.so test_dax results_*.json /tmp/test_regular.dat