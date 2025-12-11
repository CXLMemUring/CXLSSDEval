#!/bin/bash

# Byte-addressable test script
# Supports two modes based on config:
#   1. "raw" (default): FIO mmap engine directly on device - for CXL SSD with 8B atomic access
#   2. "filesystem": Mount ext4 normal, FIO buffer I/O + fdatasync - for traditional NVMe

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

# Test parameters
BLOCK_SIZES=(8 16 32 64 128 256 512 1024 2048 4096)
TEST_SIZE="1G"
RUNTIME="${STANDARD_DURATION:-30}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Mount point for filesystem mode
BYTE_MOUNT="${FILESYSTEM_MOUNT}/byte_test"
TEST_FILE="${BYTE_MOUNT}/testfile.dat"

# Function to setup normal ext4 filesystem (for traditional NVMe)
setup_normal_filesystem() {
    echo -e "${YELLOW}Setting up normal ext4 filesystem...${NC}"

    # Unmount if already mounted
    if mountpoint -q "$BYTE_MOUNT"; then
        sudo umount "$BYTE_MOUNT" 2>/dev/null || true
    fi

    # Create mount point
    if [[ ! -d "$BYTE_MOUNT" ]]; then
        sudo mkdir -p "$BYTE_MOUNT"
    fi

    # Format with ext4
    echo "Formatting $DEVICE with ext4..."
    sudo mkfs.ext4 -F "$DEVICE" > /dev/null 2>&1

    # Mount normally (no DAX)
    echo "Mounting filesystem..."
    sudo mount -t ext4 "$DEVICE" "$BYTE_MOUNT"

    sudo chmod 777 "$BYTE_MOUNT"
}

# Function to cleanup filesystem
cleanup_filesystem() {
    rm -f "$TEST_FILE" 2>/dev/null || true

    if mountpoint -q "$BYTE_MOUNT"; then
        sudo umount "$BYTE_MOUNT" 2>/dev/null || true
    fi
}

# Function to run FIO test in RAW mode (mmap directly on device, no fsync)
run_fio_raw_test() {
    local bs=$1
    local output_file=$2

    echo -e "${GREEN}Running RAW mmap test for block size: ${bs}B (direct mmap, no fsync)${NC}"

    # FIO with mmap engine directly on device
    fio --name=byte_raw_${bs} \
        --filename="$DEVICE" \
        --size=$TEST_SIZE \
        --bs=$bs \
        --rw=randwrite \
        --ioengine=mmap \
        --runtime=$RUNTIME \
        --time_based \
        --group_reporting \
        --output-format=json \
        --output="$output_file" \
        --iodepth=1 \
        --numjobs=1
}

# Function to run FIO test in filesystem mode (buffer I/O + fdatasync)
run_fio_filesystem_test() {
    local bs=$1
    local output_file=$2

    echo -e "${YELLOW}Running filesystem test for block size: ${bs}B (buffer I/O + fdatasync)${NC}"

    # Remove existing test file
    rm -f "$TEST_FILE"

    # FIO with buffer I/O and fdatasync
    fio --name=byte_fs_${bs} \
        --filename="$TEST_FILE" \
        --size=$TEST_SIZE \
        --bs=$bs \
        --rw=randwrite \
        --ioengine=sync \
        --direct=0 \
        --fdatasync=1 \
        --runtime=$RUNTIME \
        --time_based \
        --group_reporting \
        --output-format=json \
        --output="$output_file" \
        --iodepth=1 \
        --numjobs=1
}

# Function to parse FIO JSON results
parse_fio_results() {
    local json_file=$1

    if [[ ! -f "$json_file" ]]; then
        echo "0,0,0,0,0"
        return
    fi

    # Try to get write stats, fall back to sync stats
    local iops=$(jq -r '.jobs[0].write.iops // .jobs[0].sync.iops // 0' "$json_file" 2>/dev/null || echo "0")
    local bw=$(jq -r '.jobs[0].write.bw // .jobs[0].sync.bw // 0' "$json_file" 2>/dev/null || echo "0")
    local lat_mean=$(jq -r '.jobs[0].write.clat_ns.mean // .jobs[0].sync.clat_ns.mean // 0' "$json_file" 2>/dev/null || echo "0")
    local lat_95=$(jq -r '.jobs[0].write.clat_ns.percentile["95.000000"] // .jobs[0].sync.clat_ns.percentile["95.000000"] // 0' "$json_file" 2>/dev/null || echo "0")
    local lat_99=$(jq -r '.jobs[0].write.clat_ns.percentile["99.000000"] // .jobs[0].sync.clat_ns.percentile["99.000000"] // 0' "$json_file" 2>/dev/null || echo "0")

    # Convert ns to us
    local lat_mean_us=$(echo "scale=2; $lat_mean / 1000" | bc 2>/dev/null || echo "0")
    local lat_95_us=$(echo "scale=2; $lat_95 / 1000" | bc 2>/dev/null || echo "0")
    local lat_99_us=$(echo "scale=2; $lat_99 / 1000" | bc 2>/dev/null || echo "0")

    echo "$iops,$bw,$lat_mean_us,$lat_95_us,$lat_99_us"
}

# Main execution
main() {
    echo "=== Byte-Addressable I/O Test ==="
    echo "Device: $DEVICE"
    echo -e "Test Mode: ${GREEN}${BYTE_ADDRESSABLE_MODE}${NC}"

    if [[ "$BYTE_ADDRESSABLE_MODE" == "raw" ]]; then
        echo "  -> FIO mmap engine directly on device"
        echo "  -> No fsync (direct memory mapped write)"
        echo "  -> For CXL SSD with 8B atomic access"
    else
        echo "  -> Mount ext4 normal"
        echo "  -> FIO buffer I/O + fdatasync"
        echo "  -> For traditional NVMe"
    fi

    echo "Test size: $TEST_SIZE"
    echo "Runtime per test: ${RUNTIME}s"
    echo ""

    # Create results directory based on mode
    if [[ "$BYTE_ADDRESSABLE_MODE" == "raw" ]]; then
        RESULTS_DIR="${RESULTS_BASE_DIR}/raw/byte_addressable"
    else
        RESULTS_DIR="${RESULTS_BASE_DIR}/filesystem/byte_addressable"
    fi
    mkdir -p "$RESULTS_DIR"

    # Setup filesystem if in filesystem mode
    if [[ "$BYTE_ADDRESSABLE_MODE" == "filesystem" ]]; then
        setup_normal_filesystem
    fi

    # Create summary CSV
    SUMMARY_CSV="$RESULTS_DIR/byte_addressable_summary.csv"
    echo "block_size,mode,write_iops,write_bw_kbps,write_lat_us,write_lat_95_us,write_lat_99_us" > "$SUMMARY_CSV"

    # Run tests for each block size
    for bs in "${BLOCK_SIZES[@]}"; do
        echo ""
        echo "----------------------------------------"
        echo "Testing block size: ${bs}B"
        echo "----------------------------------------"

        OUTPUT_FILE="$RESULTS_DIR/bs_${bs}_write.json"

        # Clear page cache before test
        sync
        echo 3 | sudo tee /proc/sys/vm/drop_caches > /dev/null

        if [[ "$BYTE_ADDRESSABLE_MODE" == "raw" ]]; then
            run_fio_raw_test "$bs" "$OUTPUT_FILE"
        else
            run_fio_filesystem_test "$bs" "$OUTPUT_FILE"
        fi

        # Parse results and add to CSV
        if [[ -f "$OUTPUT_FILE" ]]; then
            local results=$(parse_fio_results "$OUTPUT_FILE")
            IFS=',' read -r iops bw lat_avg lat_95 lat_99 <<< "$results"

            # Format and output results
            printf "%-10s: IOPS=%-10.0f BW=%-10.0f KB/s Lat=%-10.3f us (95%%=%-10.3f us, 99%%=%-10.3f us)\n" \
                "${bs}B" "$iops" "$bw" "$lat_avg" "$lat_95" "$lat_99"

            # Add to CSV
            echo "${bs},${BYTE_ADDRESSABLE_MODE},${iops},${bw},${lat_avg},${lat_95},${lat_99}" >> "$SUMMARY_CSV"
        fi

        # Small delay between tests
        sleep 2
    done

    echo ""
    echo "=== Test Complete ==="
    echo "Results saved to: $RESULTS_DIR"
    echo "Summary CSV: $SUMMARY_CSV"

    # Generate report
    generate_report

    # Cleanup if in filesystem mode
    if [[ "$BYTE_ADDRESSABLE_MODE" == "filesystem" ]]; then
        cleanup_filesystem
    fi
}

generate_report() {
    local mode_desc=""
    local mode_detail=""

    if [[ "$BYTE_ADDRESSABLE_MODE" == "raw" ]]; then
        mode_desc="FIO mmap engine on raw device"
        mode_detail="- I/O Engine: mmap (memory mapped I/O)
- Target: Raw block device ($DEVICE)
- Sync: None (no fsync/fdatasync)
- Note: For CXL SSDs with true byte-addressable (8B atomic) access
- Writes via mmap go directly to device without filesystem overhead"
    else
        mode_desc="ext4 Normal + Buffer I/O + fdatasync"
        mode_detail="- Filesystem: ext4 mounted normally
- I/O Engine: sync (buffer I/O)
- Sync: fdatasync after each write
- Note: For traditional NVMe SSDs that don't support sub-512B atomic writes
- Each write: page cache -> fdatasync -> flush to disk"
    fi

    cat > "$RESULTS_DIR/byte_addressable_report.txt" << EOF
Byte-Addressable I/O Test Report
================================

Test Configuration:
- Device: $DEVICE
- Test Mode: $BYTE_ADDRESSABLE_MODE ($mode_desc)
$mode_detail
- Test Size: $TEST_SIZE
- Runtime per test: ${RUNTIME}s
- Block Sizes Tested: ${BLOCK_SIZES[*]} bytes

Results Summary:
EOF

    echo "" >> "$RESULTS_DIR/byte_addressable_report.txt"
    echo "Block Size | Mode       | Write IOPS   | Write BW (KB/s) | Mean Lat (us) | 95% Lat (us) | 99% Lat (us)" >> "$RESULTS_DIR/byte_addressable_report.txt"
    echo "-----------|------------|--------------|-----------------|---------------|--------------|-------------" >> "$RESULTS_DIR/byte_addressable_report.txt"

    tail -n +2 "$SUMMARY_CSV" | while IFS=',' read -r bs mode iops bw lat lat95 lat99; do
        printf "%-10s | %-10s | %-12.0f | %-15.0f | %-13.3f | %-12.3f | %-12.3f\n" \
            "${bs}B" "$mode" "$iops" "$bw" "$lat" "$lat95" "$lat99" >> "$RESULTS_DIR/byte_addressable_report.txt"
    done

    if [[ "$BYTE_ADDRESSABLE_MODE" == "raw" ]]; then
        cat >> "$RESULTS_DIR/byte_addressable_report.txt" << EOF

Analysis (RAW Mode - CXL SSD):
- FIO mmap engine maps the device directly into memory
- Writes are performed via memcpy to the mapped region
- CXL SSDs support 8B atomic writes natively
- No filesystem overhead or sync operations
- Ideal for AI/LLM workloads with small random writes
- Latency is measured in nanoseconds due to direct memory access
EOF
    else
        cat >> "$RESULTS_DIR/byte_addressable_report.txt" << EOF

Analysis (Filesystem Mode - Traditional NVMe):
- Traditional NVMe SSDs have 512B or 4KB sector size limitation
- Sub-512B writes require filesystem buffering
- fdatasync after each write ensures data durability
- Performance is limited by fdatasync latency overhead
- This is the only way to achieve durable sub-512B writes on NVMe
EOF
    fi

    echo "Report generated: $RESULTS_DIR/byte_addressable_report.txt"
}

# Run main function
main "$@"
