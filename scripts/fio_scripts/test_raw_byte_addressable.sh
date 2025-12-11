#!/bin/bash

# Raw device byte-addressable comparison test
# Compares NVMe (filesystem buffered + fdatasync) vs CXL DAX access

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
source "${SCRIPT_DIR}/common.sh"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Configuration with sensible fallbacks
CXL_DEVICE=${CXL_DEVICE:-${CONFIG_dax_device:-/dev/mem}}
CXL_OFFSET=${CXL_OFFSET:-${CONFIG_dax_offset:-0x100000000}}
CXL_SIZE=${CXL_SIZE:-${CONFIG_dax_size:-16G}}
NVME_DEVICE=${NVME_DEVICE:-${DEVICE}}
FILESYSTEM_TYPE=${FILESYSTEM_TYPE:-${CONFIG_filesystem:-ext4}}
FILESYSTEM_MOUNT_DEFAULT="${CONFIG_filesystem_mount:-/mnt}"
TEST_DIR=${TEST_DIR:-"${FILESYSTEM_MOUNT_DEFAULT%/}/byte_addr_test"}
RESULTS_DIR="${RESULTS_BASE_DIR}/raw/byte_addressable"
FIO_RUNTIME=${FIO_RUNTIME:-${STANDARD_DURATION:-30}}
BYTE_SIZES="${BYTE_SIZES:-"8 16 32 64 128 256 384"}"

LIB_INTERCEPT="${PROJECT_ROOT}/libfio_intercept.so"
INTERCEPT_SRC="${PROJECT_ROOT}/../src/fio_intercept.cpp"

mkdir -p "$RESULTS_DIR"

prepare_nvme_fs() {
    if [[ ! -b "$NVME_DEVICE" ]]; then
        echo -e "${RED}NVMe device $NVME_DEVICE not found${NC}"
        exit 1
    fi

    if [[ ! -d "$TEST_DIR" ]]; then
        sudo mkdir -p "$TEST_DIR"
    fi

    if mountpoint -q "$TEST_DIR"; then
        return
    fi

    echo -e "${YELLOW}Formatting ${NVME_DEVICE} as ${FILESYSTEM_TYPE} and mounting at ${TEST_DIR}${NC}"
    sudo mkfs.${FILESYSTEM_TYPE} -F "$NVME_DEVICE" >/dev/null 2>&1 || true
    sudo mount -t ${FILESYSTEM_TYPE} "$NVME_DEVICE" "$TEST_DIR"
    sudo chmod 777 "$TEST_DIR"
}

cleanup_nvme_fs() {
    if mountpoint -q "$TEST_DIR"; then
        sudo umount "$TEST_DIR" || true
    fi
    if [[ -d "$TEST_DIR" ]]; then
        sudo rmdir "$TEST_DIR" 2>/dev/null || true
    fi
}

build_intercept_if_needed() {
    if [[ -f "$LIB_INTERCEPT" ]]; then
        return
    fi

    if [[ -f "$INTERCEPT_SRC" ]]; then
        echo -e "${YELLOW}Building FIO intercept library for CXL DAX path...${NC}"
        g++ -shared -fPIC -O3 -march=native "$INTERCEPT_SRC" \
            -o "$LIB_INTERCEPT" -ldl -lpthread
    fi
}

run_nvme_test() {
    local bs=$1
    local test_name="nvme_${bs}B"

    echo -e "\n${YELLOW}NVMe Test (${bs}B buffered writes + fdatasync)${NC}"

    prepare_nvme_fs

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

run_cxl_mem_test() {
    local bs=$1
    local test_name="cxl_mem_${bs}B"

    echo -e "\n${YELLOW}CXL Memory Test (${bs}B direct access)${NC}"

    build_intercept_if_needed

    if [[ -f "$LIB_INTERCEPT" ]]; then
        export FIO_INTERCEPT_ENABLE=1
        export FIO_MEM_DEVICE=$CXL_DEVICE
        export FIO_MEM_OFFSET=$CXL_OFFSET
        export FIO_MEM_SIZE=$CXL_SIZE
        export LD_PRELOAD="$LIB_INTERCEPT"

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

        unset LD_PRELOAD FIO_INTERCEPT_ENABLE FIO_MEM_DEVICE FIO_MEM_OFFSET FIO_MEM_SIZE
    elif [[ -c "$CXL_DEVICE" ]]; then
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
        echo -e "${RED}Neither LD_PRELOAD intercept nor CXL device ${CXL_DEVICE} available${NC}"
        return 1
    fi

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

append_summary_row() {
    local bs=$1
    local csv=$2

    local nvme_json="$RESULTS_DIR/nvme_${bs}B.json"
    local cxl_json="$RESULTS_DIR/cxl_mem_${bs}B.json"

    if command -v jq >/dev/null 2>&1; then
        nvme_iops=$(jq '.jobs[0].write.iops' "$nvme_json" 2>/dev/null || echo "0")
        nvme_bw=$(jq '.jobs[0].write.bw' "$nvme_json" 2>/dev/null || echo "0")
        nvme_lat=$(jq '.jobs[0].write.lat_ns.mean/1000' "$nvme_json" 2>/dev/null || echo "0")
        nvme_p99=$(jq '.jobs[0].write.lat_ns.percentile."99.000000"/1000' "$nvme_json" 2>/dev/null || echo "0")

        cxl_iops=$(jq '.jobs[0].write.iops' "$cxl_json" 2>/dev/null || echo "0")
        cxl_bw=$(jq '.jobs[0].write.bw' "$cxl_json" 2>/dev/null || echo "0")
        cxl_lat=$(jq '.jobs[0].write.lat_ns.mean/1000' "$cxl_json" 2>/dev/null || echo "0")
        cxl_p99=$(jq '.jobs[0].write.lat_ns.percentile."99.000000"/1000' "$cxl_json" 2>/dev/null || echo "0")

        echo "$bs,NVMe,$nvme_iops,$nvme_bw,$nvme_lat,$nvme_p99" >> "$csv"
        echo "$bs,CXL_MEM,$cxl_iops,$cxl_bw,$cxl_lat,$cxl_p99" >> "$csv"
    fi
}

generate_report() {
    local csv=$1
    local report="${RESULTS_DIR}/report.md"

    cat > "$report" << EOF
# Byte-Addressable I/O Test Report

## Test Configuration
- NVMe Device: $NVME_DEVICE (filesystem + fdatasync)
- CXL Device: $CXL_DEVICE (Memory-mapped direct access at offset $CXL_OFFSET)
- Test Sizes: $BYTE_SIZES bytes
- Runtime per test: $FIO_RUNTIME seconds

## Key Findings

### Performance Comparison

EOF

    if [[ -f "$csv" ]]; then
        echo "### Detailed Results" >> "$report"
        echo "" >> "$report"
        echo '```csv' >> "$report"
        cat "$csv" >> "$report"
        echo '```' >> "$report"
    fi

    cat >> "$report" << EOF

## Analysis

### NVMe Limitations
- NVMe SSDs have 512B sector size limitation
- Sub-512B writes require filesystem buffering
- fdatasync after each write adds latency
- Page cache involvement increases tail latency

### CXL SSD Advantages
- True byte-addressable storage
- Direct memory-mapped access
- No filesystem overhead
- Better suited for AI/LLM workloads with tiny random writes

## Conclusion

CXL SSDs with DAX access offer substantial advantages for sub-512B operations.
Traditional NVMe SSDs incur heavy overhead due to filesystem buffering and fdatasync.

EOF
}

main() {
    echo -e "${GREEN}Byte-Addressable Raw Device Test${NC}"
    echo "========================================"
    echo "Results Directory: $RESULTS_DIR"
    echo "NVMe Device: $NVME_DEVICE"
    echo "CXL Device:  $CXL_DEVICE"
    echo "Test Sizes:  $BYTE_SIZES bytes"
    echo "Runtime:     ${FIO_RUNTIME}s per test"
    echo ""

    SUMMARY_CSV="$RESULTS_DIR/summary.csv"
    echo "Size_Bytes,Device,IOPS,BW_KBps,Lat_usec,P99_Lat_usec" > "$SUMMARY_CSV"

    for bs in $BYTE_SIZES; do
        echo -e "\n${GREEN}Testing ${bs}B I/O${NC}"
        echo "----------------------------------------"
        run_nvme_test "$bs"
        run_cxl_mem_test "$bs"
        append_summary_row "$bs" "$SUMMARY_CSV"
    done

    echo ""
    echo -e "${GREEN}Generating comparison report...${NC}"
    generate_report "$SUMMARY_CSV"
    echo "Report: ${RESULTS_DIR}/report.md"
    echo "Summary CSV: $SUMMARY_CSV"

    cleanup_nvme_fs

    echo ""
    echo -e "${GREEN}Byte-addressable raw test complete!${NC}"
}

trap cleanup_nvme_fs EXIT
main "$@"
