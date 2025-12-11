#!/bin/bash

# Master Test Orchestration Script
# This script runs all CXL SSD tests in sequence

set -e  # Exit on error

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIO_SCRIPTS_DIR="${SCRIPT_DIR}/fio_scripts"
echo "Script Directory: $SCRIPT_DIR"
# Source common functions
source "${FIO_SCRIPTS_DIR}/common.sh"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored messages
print_header() {
    echo -e "${BLUE}============================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}============================================${NC}"
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠ $1${NC}"
}

print_error() {
    echo -e "${RED}✗ $1${NC}"
}

# Function to check prerequisites
check_prerequisites() {
    print_header "Checking Prerequisites"
    
    local all_good=true
    
    # Check if config file exists
    if [[ -f "$CONFIG_FILE" ]]; then
        print_success "Configuration file found: $CONFIG_FILE"
    else
        print_error "Configuration file not found: $CONFIG_FILE"
        all_good=false
    fi
    
    # Check if device exists
    if check_device "$DEVICE"; then
        print_success "Device found: $DEVICE"
    else
        print_error "Device not found: $DEVICE"
        all_good=false
    fi
    
    # Check if FIO is installed
    if command -v fio &> /dev/null; then
        print_success "FIO is installed ($(fio --version))"
    else
        print_error "FIO is not installed"
        all_good=false
    fi
    
    # Check if Python is installed (for parsing and visualization)
    if command -v python3 &> /dev/null; then
        print_success "Python3 is installed ($(python3 --version))"
    else
        print_warning "Python3 not found - result parsing and visualization will be skipped"
    fi
    
    # Check for RocksDB
    if [[ -f "${ROCKSDB_DIR:-/opt/rocksdb}/db_bench" ]]; then
        print_success "RocksDB db_bench found"
    else
        print_warning "RocksDB db_bench not found - RocksDB tests will be skipped"
    fi
    
    if [[ "$all_good" == "false" ]]; then
        print_error "Prerequisites check failed. Please fix the issues above."
        exit 1
    fi
    
    echo ""
}

# Function to run raw device tests
run_raw_device_tests() {
    print_header "Running Raw Device Tests"
    
    if [[ -f "${FIO_SCRIPTS_DIR}/raw_device_test.sh" ]]; then
        bash "${FIO_SCRIPTS_DIR}/raw_device_test.sh"
        
        if [[ $? -eq 0 ]]; then
            print_success "Raw device tests completed successfully"
        else
            print_error "Raw device tests failed"
            return 1
        fi
    else
        print_warning "Raw device test script not found"
    fi
    
    echo ""
}

# Function to run filesystem tests
run_filesystem_tests() {
    print_header "Running Filesystem Tests"
    
    if [[ -f "${FIO_SCRIPTS_DIR}/filesystem_test.sh" ]]; then
        bash "${FIO_SCRIPTS_DIR}/filesystem_test.sh"
        
        if [[ $? -eq 0 ]]; then
            print_success "Filesystem tests completed successfully"
        else
            print_error "Filesystem tests failed"
            return 1
        fi
    else
        print_warning "Filesystem test script not found"
    fi
    
    echo ""
}

# Function to run RocksDB tests
run_rocksdb_tests() {
    print_header "Running RocksDB Tests"
    
    if [[ -f "${FIO_SCRIPTS_DIR}/rocksdb_test.sh" ]]; then
        # Check if db_bench exists
        if [[ -f "${ROCKSDB_DIR:-/opt/rocksdb}/db_bench" ]]; then
            bash "${FIO_SCRIPTS_DIR}/rocksdb_test.sh"
            
            if [[ $? -eq 0 ]]; then
                print_success "RocksDB tests completed successfully"
            else
                print_error "RocksDB tests failed"
                return 1
            fi
        else
            print_warning "RocksDB not installed. Skipping RocksDB tests."
            print_warning "Run deploy_rocksdb.sh to install RocksDB"
        fi
    else
        print_warning "RocksDB test script not found"
    fi
    
    echo ""
}

# Function to parse results
parse_results() {
    print_header "Parsing Test Results"
    
    if command -v python3 &> /dev/null; then
        if [[ -f "${SCRIPT_DIR}/parse_results.py" ]]; then
            python3 "${SCRIPT_DIR}/parse_results.py" --results-dir "${RESULTS_BASE_DIR}"
            
            if [[ $? -eq 0 ]]; then
                print_success "Results parsed successfully"
            else
                print_warning "Result parsing encountered issues"
            fi
        else
            print_warning "Result parser script not found"
        fi
    else
        print_warning "Python3 not available - skipping result parsing"
    fi
    
    echo ""
}

# Function to validate results
validate_results() {
    print_header "Validating Test Results"
    
    if command -v python3 &> /dev/null; then
        if [[ -f "${SCRIPT_DIR}/validate_results.py" ]]; then
            python3 "${SCRIPT_DIR}/validate_results.py" --results-dir "${RESULTS_BASE_DIR}"
            
            if [[ $? -eq 0 ]]; then
                print_success "Results validation passed"
            else
                print_warning "Results validation found issues"
            fi
        else
            print_warning "Result validator script not found"
        fi
    else
        print_warning "Python3 not available - skipping result validation"
    fi
    
    echo ""
}

# Function to generate report
generate_report() {
    print_header "Generating Test Report"
    
    if command -v python3 &> /dev/null; then
        if [[ -f "${SCRIPT_DIR}/generate_report.py" ]]; then
            # Generate both HTML and Markdown reports
            local timestamp=$(date +%Y%m%d_%H%M%S)
            
            # HTML report
            python3 "${SCRIPT_DIR}/generate_report.py" \
                --results-dir "${RESULTS_BASE_DIR}" \
                --format html \
                --output "${RESULTS_BASE_DIR}/report_${timestamp}.html"
            
            # Markdown report
            python3 "${SCRIPT_DIR}/generate_report.py" \
                --results-dir "${RESULTS_BASE_DIR}" \
                --format markdown \
                --output "${RESULTS_BASE_DIR}/report_${timestamp}.md"
            
            if [[ $? -eq 0 ]]; then
                print_success "Reports generated successfully"
                print_success "HTML Report: ${RESULTS_BASE_DIR}/report_${timestamp}.html"
                print_success "Markdown Report: ${RESULTS_BASE_DIR}/report_${timestamp}.md"
            else
                print_warning "Report generation encountered issues"
            fi
        else
            print_warning "Report generator script not found"
        fi
    else
        print_warning "Python3 not available - skipping report generation"
    fi
    
    echo ""
}

# Function to generate visualizations
generate_visualizations() {
    print_header "Generating Visualizations"
    
    if command -v python3 &> /dev/null; then
        if [[ -f "${SCRIPT_DIR}/visualize_results.py" ]]; then
            python3 "${SCRIPT_DIR}/visualize_results.py" \
                --results-dir "${RESULTS_BASE_DIR}" \
                --output-dir "${RESULTS_BASE_DIR}/plots"
            
            if [[ $? -eq 0 ]]; then
                print_success "Visualizations generated successfully"
                print_success "Plots saved to: ${RESULTS_BASE_DIR}/plots"
            else
                print_warning "Visualization generation encountered issues"
            fi
        else
            print_warning "Visualization script not found"
        fi
    else
        print_warning "Python3 not available - skipping visualizations"
    fi
    
    echo ""
}

# Function to show test summary
show_summary() {
    print_header "Test Execution Summary"
    
    echo "Test Start Time: $START_TIME"
    echo "Test End Time: $(date '+%Y-%m-%d %H:%M:%S')"
    
    # Calculate duration
    local end_seconds=$(date +%s)
    local duration=$((end_seconds - START_SECONDS))
    local hours=$((duration / 3600))
    local minutes=$(((duration % 3600) / 60))
    local seconds=$((duration % 60))
    
    echo "Total Duration: ${hours}h ${minutes}m ${seconds}s"
    echo ""
    
    echo "Results Directory: ${RESULTS_BASE_DIR}"
    
    # Count result files
    if [[ -d "${RESULTS_BASE_DIR}" ]]; then
        local json_count=$(find "${RESULTS_BASE_DIR}" -name "*.json" | wc -l)
        local log_count=$(find "${RESULTS_BASE_DIR}" -name "*.log" | wc -l)
        echo "JSON Result Files: $json_count"
        echo "Log Files: $log_count"
    fi
    
    echo ""
    print_success "All tests completed!"
}

# Main execution
main() {
    # Record start time
    START_TIME=$(date '+%Y-%m-%d %H:%M:%S')
    START_SECONDS=$(date +%s)
    
    print_header "CXL SSD Test Suite"
    echo "Configuration: $CONFIG_FILE"
    echo "Device: $DEVICE"
    echo "Results Directory: $RESULTS_BASE_DIR"
    echo "Start Time: $START_TIME"
    echo ""
    
    # Parse command line arguments
    RUN_RAW=true
    RUN_FS=false
    RUN_ROCKSDB=false
    SKIP_CLEANUP=true

    for arg in "$@"; do
        case $arg in
            --raw-only)
                RUN_FS=false
                RUN_ROCKSDB=false
                ;;
            --fs-only)
                RUN_RAW=false
                RUN_ROCKSDB=false
                RUN_FS=true
                ;;
            --rocksdb-only)
                RUN_RAW=false
                RUN_FS=false
                RUN_ROCKSDB=true
                ;;
            --byte-addressable-only)
                # Run only byte-addressable test and exit
                check_prerequisites
                print_header "Running Byte-Addressable I/O Test (Standalone)"
                if [[ -f "${FIO_SCRIPTS_DIR}/test_byte_addressable.sh" ]]; then
                    bash "${FIO_SCRIPTS_DIR}/test_byte_addressable.sh"
                    if [[ $? -eq 0 ]]; then
                        print_success "Byte-addressable IO test completed successfully"
                    else
                        print_error "Byte-addressable IO test failed"
                    fi
                else
                    print_error "Byte-addressable test script not found"
                fi
                exit 0
                ;;
            --skip-cleanup)
                SKIP_CLEANUP=true
                ;;
            --help|-h)
                echo "Usage: $0 [OPTIONS]"
                echo "Options:"
                echo "  --raw-only              Run only raw device tests"
                echo "  --fs-only               Run only filesystem tests (includes byte-addressable)"
                echo "  --rocksdb-only          Run only RocksDB tests"
                echo "  --byte-addressable-only Run only byte-addressable IO tests"
                echo "  --skip-cleanup          Skip cleanup after tests"
                echo "  --help, -h              Show this help message"
                exit 0
                ;;
        esac
    done
    
    # Check prerequisites
    check_prerequisites
    
    # Create results directory structure
    mkdir -p "${RESULTS_BASE_DIR}"/{raw,filesystem,rocksdb,summary,plots}

    # Run tests based on selection
    if [[ "$RUN_RAW" == "true" ]]; then
        run_raw_device_tests || print_warning "Raw device tests had issues"
    fi

    if [[ "$RUN_FS" == "true" ]]; then
        run_filesystem_tests || print_warning "Filesystem tests had issues"
        # Note: byte-addressable test is included in filesystem tests
    fi

    if [[ "$RUN_ROCKSDB" == "true" ]]; then
        run_rocksdb_tests || print_warning "RocksDB tests had issues"
    fi
    
    # Process results
    parse_results
    validate_results
    generate_report
    generate_visualizations
    
    # Cleanup if requested
    if [[ "$SKIP_CLEANUP" == "false" ]]; then
        print_header "Cleaning Up Test Environment"
        if [[ -f "${SCRIPT_DIR}/cleanup_environment.sh" ]]; then
            bash "${SCRIPT_DIR}/cleanup_environment.sh" --force
            print_success "Cleanup completed"
        fi
    fi
    
    # Show summary
    show_summary
}

# Run main function
main "$@"
