#!/bin/bash

# RocksDB Deployment Automation Script
# This script downloads, compiles, and configures RocksDB

set -e  # Exit on error

# Configuration
ROCKSDB_VERSION=${ROCKSDB_VERSION:-"v8.5.3"}
INSTALL_DIR=${INSTALL_DIR:-"/opt/rocksdb"}
BUILD_DIR="/tmp/rocksdb_build"
GITHUB_URL="https://github.com/facebook/rocksdb.git"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to print colored messages
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to check prerequisites
check_prerequisites() {
    log_info "Checking prerequisites..."
    
    local missing_deps=()
    
    # Check for required tools
    for tool in git make gcc g++ cmake; do
        if ! command -v $tool &> /dev/null; then
            missing_deps+=($tool)
        fi
    done
    
    # Check for required libraries
    if ! ldconfig -p | grep -q libsnappy; then
        missing_deps+=("libsnappy-dev")
    fi
    
    if ! ldconfig -p | grep -q libzstd; then
        missing_deps+=("libzstd-dev")
    fi
    
    if ! ldconfig -p | grep -q liblz4; then
        missing_deps+=("liblz4-dev")
    fi
    
    if ! ldconfig -p | grep -q libbz2; then
        missing_deps+=("libbz2-dev")
    fi
    
    if ! ldconfig -p | grep -q libz; then
        missing_deps+=("zlib1g-dev")
    fi
    
    if [ ${#missing_deps[@]} -gt 0 ]; then
        log_error "Missing dependencies: ${missing_deps[*]}"
        log_info "Installing missing dependencies..."
        
        # Detect package manager
        if command -v apt-get &> /dev/null; then
            sudo apt-get update
            sudo apt-get install -y git make gcc g++ cmake libsnappy-dev libzstd-dev liblz4-dev libbz2-dev zlib1g-dev
        elif command -v yum &> /dev/null; then
            sudo yum install -y git make gcc gcc-c++ cmake snappy-devel zstd-devel lz4-devel bzip2-devel zlib-devel
        else
            log_error "Unsupported package manager. Please install dependencies manually."
            exit 1
        fi
    else
        log_info "All prerequisites are installed"
    fi
}

# Function to download RocksDB
download_rocksdb() {
    log_info "Downloading RocksDB ${ROCKSDB_VERSION}..."
    
    # Clean up any existing build directory
    if [ -d "$BUILD_DIR" ]; then
        log_warn "Removing existing build directory"
        rm -rf "$BUILD_DIR"
    fi
    
    # Clone RocksDB repository
    git clone --depth 1 --branch "$ROCKSDB_VERSION" "$GITHUB_URL" "$BUILD_DIR"
    
    if [ $? -eq 0 ]; then
        log_info "RocksDB downloaded successfully"
    else
        log_error "Failed to download RocksDB"
        exit 1
    fi
}

# Function to compile RocksDB
compile_rocksdb() {
    log_info "Compiling RocksDB..."
    
    cd "$BUILD_DIR"
    
    # Detect number of CPU cores
    CORES=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
    log_info "Using $CORES CPU cores for compilation"
    
    # Compile with optimizations
    PORTABLE=1 make -j$CORES release
    
    if [ $? -eq 0 ]; then
        log_info "RocksDB compiled successfully"
    else
        log_error "Failed to compile RocksDB"
        exit 1
    fi
    
    # Also build db_bench tool
    log_info "Building db_bench tool..."
    make -j$CORES db_bench
    
    if [ $? -eq 0 ]; then
        log_info "db_bench built successfully"
    else
        log_error "Failed to build db_bench"
        exit 1
    fi
}

# Function to install RocksDB
install_rocksdb() {
    log_info "Installing RocksDB to ${INSTALL_DIR}..."
    
    # Create installation directory
    sudo mkdir -p "$INSTALL_DIR"
    
    # Copy library files
    sudo cp -r "$BUILD_DIR/include" "$INSTALL_DIR/"
    sudo cp "$BUILD_DIR/librocksdb.a" "$INSTALL_DIR/"
    if [ -f "$BUILD_DIR/librocksdb.so" ]; then
        sudo cp "$BUILD_DIR/librocksdb.so"* "$INSTALL_DIR/"
    fi
    
    # Copy tools
    sudo cp "$BUILD_DIR/db_bench" "$INSTALL_DIR/"
    sudo cp "$BUILD_DIR/ldb" "$INSTALL_DIR/" 2>/dev/null || true
    sudo cp "$BUILD_DIR/sst_dump" "$INSTALL_DIR/" 2>/dev/null || true
    
    # Make tools executable
    sudo chmod +x "$INSTALL_DIR/db_bench"
    sudo chmod +x "$INSTALL_DIR/ldb" 2>/dev/null || true
    sudo chmod +x "$INSTALL_DIR/sst_dump" 2>/dev/null || true
    
    # Create symlinks in /usr/local/bin for easy access
    log_info "Creating symlinks..."
    sudo ln -sf "$INSTALL_DIR/db_bench" /usr/local/bin/db_bench
    sudo ln -sf "$INSTALL_DIR/ldb" /usr/local/bin/ldb 2>/dev/null || true
    sudo ln -sf "$INSTALL_DIR/sst_dump" /usr/local/bin/sst_dump 2>/dev/null || true
    
    # Update library cache
    if [ -f "$INSTALL_DIR/librocksdb.so" ]; then
        echo "$INSTALL_DIR" | sudo tee /etc/ld.so.conf.d/rocksdb.conf
        sudo ldconfig
    fi
    
    log_info "RocksDB installed successfully"
}

# Function to verify installation
verify_installation() {
    log_info "Verifying RocksDB installation..."
    
    # Check if db_bench is accessible
    if "$INSTALL_DIR/db_bench" --version &> /dev/null; then
        log_info "db_bench is working correctly"
        "$INSTALL_DIR/db_bench" --version
    else
        log_error "db_bench is not working properly"
        exit 1
    fi
    
    # Check library
    if [ -f "$INSTALL_DIR/librocksdb.a" ]; then
        log_info "RocksDB library found at $INSTALL_DIR/librocksdb.a"
    else
        log_error "RocksDB library not found"
        exit 1
    fi
    
    log_info "RocksDB installation verified successfully"
}

# Function to create configuration file
create_config() {
    log_info "Creating RocksDB configuration..."
    
    # Create a sample configuration file
    cat > "$INSTALL_DIR/rocksdb.conf" << EOF
# RocksDB Configuration
# Generated by deploy_rocksdb.sh

# Installation directory
ROCKSDB_DIR=$INSTALL_DIR

# db_bench binary
DB_BENCH=$INSTALL_DIR/db_bench

# Default database path
DB_PATH=/tmp/rocksdb_test

# Default benchmark parameters
NUM_KEYS=1000000
VALUE_SIZE=1024
KEY_SIZE=16
THREADS=8
CACHE_SIZE=134217728  # 128MB

# Export for use in scripts
export ROCKSDB_DIR DB_BENCH DB_PATH
EOF
    
    log_info "Configuration file created at $INSTALL_DIR/rocksdb.conf"
}

# Function to run a simple test
run_test() {
    log_info "Running a simple benchmark test..."
    
    local TEST_DB="/tmp/rocksdb_test_$$"
    
    "$INSTALL_DIR/db_bench" \
        --benchmarks="fillseq,readrandom,readseq" \
        --db="$TEST_DB" \
        --num=10000 \
        --value_size=100 \
        --compression_type=none \
        --use_existing_db=false
    
    if [ $? -eq 0 ]; then
        log_info "Benchmark test completed successfully"
    else
        log_error "Benchmark test failed"
    fi
    
    # Cleanup test database
    rm -rf "$TEST_DB"
}

# Function to cleanup build directory
cleanup() {
    log_info "Cleaning up build directory..."
    rm -rf "$BUILD_DIR"
    log_info "Cleanup completed"
}

# Function to uninstall RocksDB
uninstall_rocksdb() {
    log_warn "Uninstalling RocksDB..."
    
    # Remove installation directory
    sudo rm -rf "$INSTALL_DIR"
    
    # Remove symlinks
    sudo rm -f /usr/local/bin/db_bench
    sudo rm -f /usr/local/bin/ldb
    sudo rm -f /usr/local/bin/sst_dump
    
    # Remove library config
    sudo rm -f /etc/ld.so.conf.d/rocksdb.conf
    sudo ldconfig
    
    log_info "RocksDB uninstalled"
}

# Main installation process
main() {
    echo "========================================="
    echo "RocksDB Deployment Script"
    echo "Version: ${ROCKSDB_VERSION}"
    echo "Install Directory: ${INSTALL_DIR}"
    echo "========================================="
    
    # Parse command line arguments
    case "${1:-install}" in
        install)
            check_prerequisites
            download_rocksdb
            compile_rocksdb
            install_rocksdb
            create_config
            verify_installation
            run_test
            cleanup
            
            echo ""
            log_info "RocksDB installation completed successfully!"
            log_info "db_bench is available at: ${INSTALL_DIR}/db_bench"
            log_info "Configuration file: ${INSTALL_DIR}/rocksdb.conf"
            log_info "To use RocksDB in your scripts, source the config:"
            echo "    source ${INSTALL_DIR}/rocksdb.conf"
            ;;
        
        uninstall)
            uninstall_rocksdb
            ;;
        
        test)
            run_test
            ;;
        
        *)
            echo "Usage: $0 [install|uninstall|test]"
            echo "  install   - Download, compile and install RocksDB (default)"
            echo "  uninstall - Remove RocksDB installation"
            echo "  test      - Run a simple benchmark test"
            exit 1
            ;;
    esac
}

# Run main function
main "$@"