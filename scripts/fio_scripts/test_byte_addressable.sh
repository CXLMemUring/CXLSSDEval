#!/bin/bash

# Byte-addressable test script using FIO with buffer I/O and fdatasync
# Tests sub-512B I/O performance on filesystem

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

# Test parameters
BLOCK_SIZES=(8 16 32 64 128 256 512 1k 2k 4k)
FILE_SIZE="1G"
# Use filesystem mount point from config
TEST_FILE="${FILESYSTEM_MOUNT}/byte_test.dat"
RUNTIME="${STANDARD_DURATION:-30}"

# Function to run FIO byte-addressable test
run_fio_byte_test() {
    local bs=$1
    local output_file=$2

    echo "Running byte-addressable test for block size: $bs"

    # Create FIO job for buffer I/O with fdatasync
    fio --name=byte_addressable_${bs} \
        --filename=$TEST_FILE \
        --size=$FILE_SIZE \
        --bs=$bs \
        --rw=write \
        --ioengine=sync \
        --direct=0 \
        --fdatasync=1 \
        --runtime=$RUNTIME \
        --time_based \
        --group_reporting \
        --output-format=json \
        --output=$output_file \
        --iodepth=1 \
        --numjobs=1
}

# Main execution
main() {
    echo "=== Byte-Addressable I/O Test (FIO) ==="
    echo "Testing sub-512B writes with filesystem buffer I/O + fdatasync"
    echo "Device: $DEVICE"
    echo "Filesystem: ${FILESYSTEM_TYPE}"
    echo "Mount point: ${FILESYSTEM_MOUNT}"
    echo "File size: $FILE_SIZE"
    echo "Runtime per test: ${RUNTIME}s"
    echo ""

    # Create results directory
    RESULTS_DIR="${RESULTS_BASE_DIR}/filesystem/byte_addressable"
    mkdir -p "$RESULTS_DIR"

    # Setup filesystem if not already mounted
    if [[ ! -d "$FILESYSTEM_MOUNT" ]]; then
        echo "Creating mount point: $FILESYSTEM_MOUNT"
        sudo mkdir -p "$FILESYSTEM_MOUNT"
    fi

    if ! mountpoint -q "$FILESYSTEM_MOUNT"; then
        echo "Creating ${FILESYSTEM_TYPE} filesystem on $DEVICE..."
        sudo mkfs.${FILESYSTEM_TYPE} -F "$DEVICE" > /dev/null 2>&1
        echo "Mounting filesystem..."
        sudo mount -t ${FILESYSTEM_TYPE} "$DEVICE" "$FILESYSTEM_MOUNT"
        sudo chmod 777 "$FILESYSTEM_MOUNT"
    fi

    # Create test file directory
    mkdir -p "$(dirname $TEST_FILE)"

    # Create summary CSV
    SUMMARY_CSV="$RESULTS_DIR/byte_addressable_summary.csv"
    echo "block_size,write_iops,write_bw_kbps,write_lat_us,write_lat_95_us,write_lat_99_us" > "$SUMMARY_CSV"

    # Run tests for each block size
    for bs in "${BLOCK_SIZES[@]}"; do
        echo ""
        echo "----------------------------------------"
        echo "Testing block size: $bs"
        echo "----------------------------------------"

        OUTPUT_FILE="$RESULTS_DIR/bs_${bs}_write.json"

        # Clear page cache before test
        sync
        echo 3 | sudo tee /proc/sys/vm/drop_caches > /dev/null

        # Remove existing test file
        rm -f "$TEST_FILE"

        # Run the FIO test
        run_fio_byte_test "$bs" "$OUTPUT_FILE"

        # Parse results and add to CSV
        if [[ -f "$OUTPUT_FILE" ]]; then
            # Extract metrics from JSON (including sync operations)
            write_iops=$(jq -r '.jobs[0].write.iops // .jobs[0].sync.iops // 0' "$OUTPUT_FILE" 2>/dev/null || echo "0")
            write_bw=$(jq -r '.jobs[0].write.bw // .jobs[0].sync.bw // 0' "$OUTPUT_FILE" 2>/dev/null || echo "0")
            write_lat_mean=$(jq -r '.jobs[0].write.clat_ns.mean // .jobs[0].sync.clat_ns.mean // 0' "$OUTPUT_FILE" 2>/dev/null || echo "0")
            write_lat_95=$(jq -r '.jobs[0].write.clat_ns.percentile["95.000000"] // .jobs[0].sync.clat_ns.percentile["95.000000"] // 0' "$OUTPUT_FILE" 2>/dev/null || echo "0")
            write_lat_99=$(jq -r '.jobs[0].write.clat_ns.percentile["99.000000"] // .jobs[0].sync.clat_ns.percentile["99.000000"] // 0' "$OUTPUT_FILE" 2>/dev/null || echo "0")

            # Convert latencies from ns to us
            write_lat_mean_us=$(echo "$write_lat_mean / 1000" | bc -l 2>/dev/null || echo "0")
            write_lat_95_us=$(echo "$write_lat_95 / 1000" | bc -l 2>/dev/null || echo "0")
            write_lat_99_us=$(echo "$write_lat_99 / 1000" | bc -l 2>/dev/null || echo "0")

            # Format and output results
            printf "%-10s: IOPS=%-8.0f BW=%-8.0f KB/s Lat=%-8.2f us (95%%=%-8.2f us, 99%%=%-8.2f us)\n" \
                "$bs" "$write_iops" "$write_bw" "$write_lat_mean_us" "$write_lat_95_us" "$write_lat_99_us"

            # Add to CSV
            echo "${bs},${write_iops},${write_bw},${write_lat_mean_us},${write_lat_95_us},${write_lat_99_us}" >> "$SUMMARY_CSV"
        fi

        # Small delay between tests
        sleep 2
    done

    echo ""
    echo "=== Test Complete ==="
    echo "Results saved to: $RESULTS_DIR"
    echo "Summary CSV: $SUMMARY_CSV"

    # Generate report
    generate_report() {
        cat > "$RESULTS_DIR/byte_addressable_report.txt" << EOF
Byte-Addressable I/O Test Report (FIO)
======================================

Test Configuration:
- Device: $DEVICE
- Filesystem: ${FILESYSTEM_TYPE}
- Mount Point: ${FILESYSTEM_MOUNT}
- File Size: $FILE_SIZE
- I/O Engine: sync (buffer I/O)
- Direct I/O: No (using page cache)
- fdatasync: Yes (after each write)
- Runtime per test: ${RUNTIME}s

Results Summary:
EOF

        echo "" >> "$RESULTS_DIR/byte_addressable_report.txt"
        echo "Block Size | Write IOPS | Write BW (KB/s) | Mean Lat (us) | 95% Lat (us) | 99% Lat (us)" >> "$RESULTS_DIR/byte_addressable_report.txt"
        echo "-----------|------------|-----------------|---------------|--------------|-------------" >> "$RESULTS_DIR/byte_addressable_report.txt"

        tail -n +2 "$SUMMARY_CSV" | while IFS=',' read -r bs iops bw lat lat95 lat99; do
            printf "%-10s | %-10.0f | %-15.0f | %-13.2f | %-12.2f | %-12.2f\n" \
                "$bs" "$iops" "$bw" "$lat" "$lat95" "$lat99" >> "$RESULTS_DIR/byte_addressable_report.txt"
        done

        cat >> "$RESULTS_DIR/byte_addressable_report.txt" << EOF

Analysis:
- Sub-512B writes use filesystem buffer I/O with fdatasync for persistence
- Traditional NVMe SSDs must perform read-modify-write for sectors < 512B
- Performance degradation is significant for very small I/O sizes
- fdatasync after each write ensures data durability but adds latency
- CXL SSDs with native byte-addressability can bypass these limitations

Note: This test simulates the overhead of sub-sector writes on traditional
NVMe SSDs. CXL SSDs with byte-addressable support can perform these
operations natively without filesystem overhead.
EOF
    }

    generate_report
    echo "Report generated: $RESULTS_DIR/byte_addressable_report.txt"

    # Cleanup
    rm -f "$TEST_FILE"
}

# Run main function
main "$@"