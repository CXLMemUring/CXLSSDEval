#!/bin/bash

# Test script for CXL MWAIT functionality

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}CXL SSD MWAIT Test Script${NC}"
echo "================================"

# Check if running as root
if [ "$EUID" -ne 0 ]; then 
    echo -e "${RED}Error: This script must be run as root${NC}"
    exit 1
fi

# Check for CXL devices
echo -e "\n${YELLOW}Checking for CXL devices...${NC}"
if ! ls /sys/bus/cxl/devices/ > /dev/null 2>&1; then
    echo -e "${RED}No CXL devices found${NC}"
    exit 1
fi

echo "Found CXL devices:"
ls /sys/bus/cxl/devices/

# Check CPU support for MONITOR/MWAIT
echo -e "\n${YELLOW}Checking CPU support for MONITOR/MWAIT...${NC}"
if grep -q "monitor" /proc/cpuinfo; then
    echo -e "${GREEN}MONITOR/MWAIT supported${NC}"
else
    echo -e "${RED}MONITOR/MWAIT not supported by CPU${NC}"
    exit 1
fi

# Check if test binary exists
TEST_BIN="../build/test_mwait"
if [ ! -f "$TEST_BIN" ]; then
    echo -e "${YELLOW}Test binary not found. Building...${NC}"
    cd ..
    mkdir -p build
    cd build
    cmake ..
    make
    cd ../scripts
fi

# Run tests with different configurations
echo -e "\n${YELLOW}Running MWAIT tests...${NC}"

# Test 1: Basic MWAIT functionality
echo -e "\n${GREEN}Test 1: Basic MWAIT functionality${NC}"
$TEST_BIN --test basic

# Test 2: PMR access latency
echo -e "\n${GREEN}Test 2: PMR access latency${NC}"
$TEST_BIN --test pmr_latency

# Test 3: MWAIT with different C-states
echo -e "\n${GREEN}Test 3: MWAIT with different C-states${NC}"
for cstate in C0 C1 C2 C3 C6; do
    echo "  Testing $cstate..."
    $TEST_BIN --test cstate --cstate $cstate
done

# Test 4: Batch monitoring
echo -e "\n${GREEN}Test 4: Batch monitoring${NC}"
$TEST_BIN --test batch --addresses 4

# Test 5: Performance benchmark
echo -e "\n${GREEN}Test 5: Performance benchmark${NC}"
$TEST_BIN --test benchmark --iterations 10000

echo -e "\n${GREEN}All tests completed successfully!${NC}"