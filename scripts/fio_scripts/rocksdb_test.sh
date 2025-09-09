#!/bin/bash

# RocksDB Benchmark Testing Script

# Source common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

# RocksDB specific variables
ROCKSDB_DIR="${ROCKSDB_DIR:-/opt/rocksdb}"
DB_BENCH="${ROCKSDB_DIR}/db_bench"
DB_PATH="${CONFIG_rocksdb_db_path:-${FILESYSTEM_MOUNT}/rocksdb_test}"
NUM_KEYS="${CONFIG_rocksdb_num_keys:-10000000}"
VALUE_SIZE="${CONFIG_rocksdb_value_size:-1024}"
RESULTS_DIR="${RESULTS_BASE_DIR}/rocksdb"

# Create results directory
mkdir -p "$RESULTS_DIR"

# Function to check if db_bench exists
check_db_bench() {
    if [[ ! -f "$DB_BENCH" ]]; then
        log_message "Error: db_bench not found at $DB_BENCH"
        log_message "Please install RocksDB first or set ROCKSDB_DIR environment variable"
        return 1
    fi
    return 0
}

# Function to setup database directory
setup_db_directory() {
    log_message "Setting up database directory: $DB_PATH"
    
    # Create mount point if using filesystem
    if [[ "$DB_PATH" == *"$FILESYSTEM_MOUNT"* ]]; then
        if [[ ! -d "$FILESYSTEM_MOUNT" ]]; then
            sudo mkdir -p "$FILESYSTEM_MOUNT"
        fi
        
        # Mount filesystem if not mounted
        if ! mountpoint -q "$FILESYSTEM_MOUNT"; then
            log_message "Mounting filesystem for RocksDB"
            sudo mkfs.${FILESYSTEM_TYPE} -F "$DEVICE" 2>/dev/null
            sudo mount -t ${FILESYSTEM_TYPE} "$DEVICE" "$FILESYSTEM_MOUNT"
            sudo chmod 777 "$FILESYSTEM_MOUNT"
        fi
    fi
    
    # Create database directory
    mkdir -p "$DB_PATH"
    mkdir -p "$DB_PATH/wal"
    
    # Clean up existing database
    log_message "Cleaning up existing database"
    rm -rf "$DB_PATH"/*
}

# Function to run RocksDB benchmark
run_rocksdb_benchmark() {
    local benchmark_name=$1
    local output_file=$2
    shift 2
    
    log_message "Running RocksDB benchmark: $benchmark_name"
    
    "$DB_BENCH" \
        --db="$DB_PATH" \
        --wal_dir="$DB_PATH/wal" \
        --num="$NUM_KEYS" \
        --value_size="$VALUE_SIZE" \
        --key_size=16 \
        --use_direct_io_for_flush_and_compaction=true \
        --statistics=true \
        --stats_per_interval=1 \
        --stats_interval_seconds=10 \
        --histogram=1 \
        "$@" \
        2>&1 | tee "$output_file"
    
    if [[ $? -eq 0 ]]; then
        log_message "Benchmark completed: $benchmark_name"
    else
        log_message "Benchmark failed: $benchmark_name"
        return 1
    fi
}

# Test 1: Sequential Write Performance
test_fillseq() {
    log_message "Testing Sequential Write Performance"
    
    local output_file="${RESULTS_DIR}/fillseq.log"
    
    run_rocksdb_benchmark "fillseq" "$output_file" \
        --benchmarks="fillseq,stats" \
        --threads=1 \
        --disable_wal=false \
        --sync=false
}

# Test 2: Random Write Performance
test_fillrandom() {
    log_message "Testing Random Write Performance"
    
    local output_file="${RESULTS_DIR}/fillrandom.log"
    
    run_rocksdb_benchmark "fillrandom" "$output_file" \
        --benchmarks="fillrandom,stats" \
        --threads=1 \
        --disable_wal=false \
        --sync=false
}

# Test 3: Sequential Read Performance
test_readseq() {
    log_message "Testing Sequential Read Performance"
    
    local output_file="${RESULTS_DIR}/readseq.log"
    
    # First fill the database
    log_message "  Filling database for read test..."
    "$DB_BENCH" \
        --db="$DB_PATH" \
        --benchmarks="fillseq" \
        --num="$NUM_KEYS" \
        --value_size="$VALUE_SIZE" \
        --key_size=16 \
        --use_existing_db=false \
        > /dev/null 2>&1
    
    run_rocksdb_benchmark "readseq" "$output_file" \
        --benchmarks="readseq,stats" \
        --threads=1 \
        --use_existing_db=true \
        --cache_size=134217728
}

# Test 4: Random Read Performance
test_readrandom() {
    log_message "Testing Random Read Performance"
    
    local output_file="${RESULTS_DIR}/readrandom.log"
    
    # First fill the database
    log_message "  Filling database for read test..."
    "$DB_BENCH" \
        --db="$DB_PATH" \
        --benchmarks="fillrandom" \
        --num="$NUM_KEYS" \
        --value_size="$VALUE_SIZE" \
        --key_size=16 \
        --use_existing_db=false \
        > /dev/null 2>&1
    
    run_rocksdb_benchmark "readrandom" "$output_file" \
        --benchmarks="readrandom,stats" \
        --threads=1 \
        --use_existing_db=true \
        --cache_size=134217728
}

# Test 5: Mixed Workload (Read While Writing)
test_readwhilewriting() {
    log_message "Testing Mixed Workload (Read While Writing)"
    
    local output_file="${RESULTS_DIR}/readwhilewriting.log"
    
    # First fill the database
    log_message "  Filling database for mixed workload test..."
    "$DB_BENCH" \
        --db="$DB_PATH" \
        --benchmarks="fillrandom" \
        --num="$NUM_KEYS" \
        --value_size="$VALUE_SIZE" \
        --key_size=16 \
        --use_existing_db=false \
        > /dev/null 2>&1
    
    run_rocksdb_benchmark "readwhilewriting" "$output_file" \
        --benchmarks="readwhilewriting,stats" \
        --threads=8 \
        --use_existing_db=true \
        --cache_size=134217728 \
        --duration=300
}

# Test 6: Multi-threaded Performance
test_multithreaded() {
    log_message "Testing Multi-threaded Performance"
    
    THREAD_COUNTS=(1 2 4 8 16)
    
    for threads in "${THREAD_COUNTS[@]}"; do
        local output_file="${RESULTS_DIR}/multithread_${threads}.log"
        
        log_message "  Testing with ${threads} threads"
        
        # Clean database
        rm -rf "$DB_PATH"/*
        
        run_rocksdb_benchmark "multithread_${threads}" "$output_file" \
            --benchmarks="fillrandom,readrandom,stats" \
            --threads="$threads" \
            --cache_size=134217728
    done
}

# Test 7: Different Value Sizes
test_value_sizes() {
    log_message "Testing Different Value Sizes"
    
    VALUE_SIZES=(256 1024 4096 16384 65536)
    
    for vsize in "${VALUE_SIZES[@]}"; do
        local output_file="${RESULTS_DIR}/value_size_${vsize}.log"
        
        log_message "  Testing with value size ${vsize} bytes"
        
        # Clean database
        rm -rf "$DB_PATH"/*
        
        run_rocksdb_benchmark "value_size_${vsize}" "$output_file" \
            --benchmarks="fillrandom,readrandom,stats" \
            --value_size="$vsize" \
            --threads=4 \
            --cache_size=134217728
    done
}

# Test 8: Compaction Performance
test_compaction() {
    log_message "Testing Compaction Performance"
    
    local output_file="${RESULTS_DIR}/compaction.log"
    
    run_rocksdb_benchmark "compaction" "$output_file" \
        --benchmarks="fillrandom,compact,stats" \
        --threads=1 \
        --num="$NUM_KEYS" \
        --cache_size=134217728
}

# Function to cleanup database
cleanup_database() {
    log_message "Cleaning up database"
    
    if [[ -d "$DB_PATH" ]]; then
        rm -rf "$DB_PATH"
    fi
    
    # Unmount filesystem if using it
    if [[ "$DB_PATH" == *"$FILESYSTEM_MOUNT"* ]]; then
        if mountpoint -q "$FILESYSTEM_MOUNT"; then
            sudo umount "$FILESYSTEM_MOUNT"
        fi
    fi
}

# Main execution
main() {
    log_message "========================================="
    log_message "Starting RocksDB Test Suite"
    log_message "Database Path: ${DB_PATH}"
    log_message "Number of Keys: ${NUM_KEYS}"
    log_message "Value Size: ${VALUE_SIZE}"
    log_message "Results Directory: ${RESULTS_DIR}"
    log_message "========================================="
    
    # Check if db_bench exists
    check_db_bench || exit 1
    
    # Setup database directory
    setup_db_directory
    
    # Run all RocksDB tests
    test_fillseq
    test_fillrandom
    test_readseq
    test_readrandom
    test_readwhilewriting
    test_multithreaded
    test_value_sizes
    test_compaction
    
    # Cleanup
    cleanup_database
    
    log_message "========================================="
    log_message "RocksDB Test Suite Completed"
    log_message "========================================="
}

# Run if executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi