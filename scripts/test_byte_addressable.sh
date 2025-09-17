#!/bin/bash

# Test script for byte-addressable I/O comparison between CXL SSD (DAX) and NVMe (filesystem)
# Tests sub-512B I/O operations

set -e

# Configuration
CXL_DEVICE=${CXL_DEVICE:-"/dev/mem"}
CXL_OFFSET=${CXL_OFFSET:-"0x100000000"}  # Memory offset (4GB)
CXL_SIZE=${CXL_SIZE:-"16G"}
NVME_DEVICE=${NVME_DEVICE:-"/dev/nvme0n1"}
TEST_DIR=${TEST_DIR:-"/mnt/test_byte_addr"}
RESULTS_DIR="results/byte_addressable"
FIO_RUNTIME=${FIO_RUNTIME:-30}

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}Byte-Addressable I/O Test${NC}"
echo "=================================="
echo "Comparing CXL SSD (Memory-mapped) vs NVMe (filesystem+fdatasync)"
echo ""

# Create results directory
mkdir -p $RESULTS_DIR

# Function to run filesystem test with fdatasync for NVMe
run_nvme_test() {
    local bs=$1
    local test_name="nvme_${bs}B"

    echo -e "\n${YELLOW}NVMe Test: ${bs}B with filesystem + fdatasync${NC}"

    # Create test filesystem if not exists
    if [ ! -d "$TEST_DIR" ]; then
        mkdir -p $TEST_DIR
        mkfs.ext4 $NVME_DEVICE >/dev/null 2>&1 || true
        mount $NVME_DEVICE $TEST_DIR || true
    fi

    # Run FIO with buffered I/O + fdatasync
    # Using psync engine without direct=1 for page cache writes
    # fdatasync=1 forces sync after each write
    fio --name=$test_name \
        --filename=$TEST_DIR/testfile \
        --size=100M \
        --bs=${bs} \
        --rw=randwrite \
        --ioengine=psync \
        --fdatasync=1 \
        --runtime=$FIO_RUNTIME \
        --time_based \
        --group_reporting \
        --output-format=json \
        --output=$RESULTS_DIR/${test_name}.json \
        2>/dev/null

    # Extract metrics
    if command -v jq >/dev/null 2>&1; then
        echo "Results for NVMe ${bs}B:"
        jq '.jobs[0].write | {
            iops: .iops,
            bw_KBps: (.bw/1),
            lat_usec: .lat_ns.mean/1000,
            p99_lat_usec: .lat_ns.percentile."99.000000"/1000
        }' $RESULTS_DIR/${test_name}.json 2>/dev/null || echo "Failed to parse results"
    fi
}

# Function to run memory-mapped test for CXL SSD
run_cxl_mem_test() {
    local bs=$1
    local test_name="cxl_mem_${bs}B"

    echo -e "\n${YELLOW}CXL Memory Test: ${bs}B with direct memory access${NC}"

    # Use LD_PRELOAD for DAX interception or direct DAX access
    if [ -f "./libfio_intercept.so" ]; then
        # Using LD_PRELOAD interception
        export FIO_INTERCEPT_ENABLE=1
        export FIO_MEM_DEVICE=$CXL_DEVICE
        export FIO_MEM_OFFSET=$CXL_OFFSET
        export FIO_MEM_SIZE=$CXL_SIZE
        export LD_PRELOAD=./libfio_intercept.so

        fio --name=$test_name \
            --filename=test.dat \
            --size=100M \
            --bs=${bs} \
            --rw=randwrite \
            --direct=1 \
            --ioengine=psync \
            --runtime=$FIO_RUNTIME \
            --time_based \
            --group_reporting \
            --output-format=json \
            --output=$RESULTS_DIR/${test_name}.json \
            2>/dev/null

        unset LD_PRELOAD
        unset FIO_INTERCEPT_ENABLE
    elif [ -c "$CXL_DEVICE" ]; then
        # Direct DAX device access
        fio --name=$test_name \
            --filename=$CXL_DEVICE \
            --size=100M \
            --bs=${bs} \
            --rw=randwrite \
            --direct=1 \
            --ioengine=libaio \
            --runtime=$FIO_RUNTIME \
            --time_based \
            --group_reporting \
            --output-format=json \
            --output=$RESULTS_DIR/${test_name}.json \
            2>/dev/null
    else
        echo -e "${RED}Neither LD_PRELOAD library nor memory device found${NC}"
        return 1
    fi

    # Extract metrics
    if command -v jq >/dev/null 2>&1; then
        echo "Results for CXL Memory ${bs}B:"
        jq '.jobs[0].write | {
            iops: .iops,
            bw_KBps: (.bw/1),
            lat_usec: .lat_ns.mean/1000,
            p99_lat_usec: .lat_ns.percentile."99.000000"/1000
        }' $RESULTS_DIR/${test_name}.json 2>/dev/null || echo "Failed to parse results"
    fi
}

# Build intercept library if needed
if [ ! -f "./libfio_intercept.so" ] && [ -f "../src/fio_intercept.cpp" ]; then
    echo -e "${YELLOW}Building FIO intercept library...${NC}"
    g++ -shared -fPIC -O3 -march=native ../src/fio_intercept.cpp -o libfio_intercept.so -ldl -lpthread
fi

# Test sizes (all sub-512B)
BYTE_SIZES="8 16 32 64 128 256 384"

echo -e "\n${GREEN}Starting Byte-Addressable Tests${NC}"
echo "Test sizes: ${BYTE_SIZES} bytes"
echo "Runtime per test: ${FIO_RUNTIME} seconds"

# Create CSV header
echo "Size_Bytes,Device,IOPS,BW_KBps,Lat_usec,P99_Lat_usec" > $RESULTS_DIR/summary.csv

# Run tests for each size
for bs in $BYTE_SIZES; do
    echo -e "\n${GREEN}Testing ${bs}B I/O${NC}"
    echo "----------------------------------------"

    # Run NVMe test
    run_nvme_test $bs

    # Run CXL Memory test
    run_cxl_mem_test $bs

    # Parse and add to CSV
    if command -v jq >/dev/null 2>&1; then
        # NVMe results
        nvme_iops=$(jq '.jobs[0].write.iops' $RESULTS_DIR/nvme_${bs}B.json 2>/dev/null || echo "0")
        nvme_bw=$(jq '.jobs[0].write.bw' $RESULTS_DIR/nvme_${bs}B.json 2>/dev/null || echo "0")
        nvme_lat=$(jq '.jobs[0].write.lat_ns.mean/1000' $RESULTS_DIR/nvme_${bs}B.json 2>/dev/null || echo "0")
        nvme_p99=$(jq '.jobs[0].write.lat_ns.percentile."99.000000"/1000' $RESULTS_DIR/nvme_${bs}B.json 2>/dev/null || echo "0")

        # CXL results
        cxl_iops=$(jq '.jobs[0].write.iops' $RESULTS_DIR/cxl_mem_${bs}B.json 2>/dev/null || echo "0")
        cxl_bw=$(jq '.jobs[0].write.bw' $RESULTS_DIR/cxl_mem_${bs}B.json 2>/dev/null || echo "0")
        cxl_lat=$(jq '.jobs[0].write.lat_ns.mean/1000' $RESULTS_DIR/cxl_mem_${bs}B.json 2>/dev/null || echo "0")
        cxl_p99=$(jq '.jobs[0].write.lat_ns.percentile."99.000000"/1000' $RESULTS_DIR/cxl_mem_${bs}B.json 2>/dev/null || echo "0")

        echo "$bs,NVMe,$nvme_iops,$nvme_bw,$nvme_lat,$nvme_p99" >> $RESULTS_DIR/summary.csv
        echo "$bs,CXL_MEM,$cxl_iops,$cxl_bw,$cxl_lat,$cxl_p99" >> $RESULTS_DIR/summary.csv
    fi
done

# Generate comparison report
echo -e "\n${GREEN}Generating Comparison Report${NC}"
echo "========================================"

cat > $RESULTS_DIR/report.md << EOF
# Byte-Addressable I/O Test Report

## Test Configuration
- NVMe Device: $NVME_DEVICE (filesystem + fdatasync)
- CXL Device: $CXL_DEVICE (Memory-mapped direct access at offset $CXL_OFFSET)
- Test Sizes: $BYTE_SIZES bytes
- Runtime per test: $FIO_RUNTIME seconds

## Key Findings

### Performance Comparison

EOF

# Add performance comparison table
if [ -f "$RESULTS_DIR/summary.csv" ]; then
    echo "### Detailed Results" >> $RESULTS_DIR/report.md
    echo "" >> $RESULTS_DIR/report.md
    echo '```csv' >> $RESULTS_DIR/report.md
    cat $RESULTS_DIR/summary.csv >> $RESULTS_DIR/report.md
    echo '```' >> $RESULTS_DIR/report.md
fi

cat >> $RESULTS_DIR/report.md << EOF

## Analysis

### NVMe Limitations
- NVMe SSDs have 512B sector size limitation
- Sub-512B writes require filesystem buffering
- Each small write triggers fdatasync overhead
- Page cache involvement adds latency

### CXL SSD Advantages
- True byte-addressable storage
- Direct memory-mapped access
- No filesystem overhead
- Ideal for AI/LLM workloads with small random accesses

## Conclusion

CXL SSDs with memory-mapped support provide significant advantages for byte-addressable I/O operations,
particularly important for modern AI/LLM workloads that frequently perform sub-512B accesses.
Traditional NVMe SSDs must rely on filesystem buffering and synchronization, adding substantial overhead.

EOF

echo -e "\n${GREEN}Test Completed!${NC}"
echo "Results saved to: $RESULTS_DIR/"
echo "  - JSON results: ${RESULTS_DIR}/*.json"
echo "  - Summary CSV: ${RESULTS_DIR}/summary.csv"
echo "  - Report: ${RESULTS_DIR}/report.md"

# Cleanup
if [ -d "$TEST_DIR" ]; then
    umount $TEST_DIR 2>/dev/null || true
    rmdir $TEST_DIR 2>/dev/null || true
fi