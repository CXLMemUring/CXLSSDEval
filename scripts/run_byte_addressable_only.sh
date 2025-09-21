#!/bin/bash
#
# Run ONLY byte-addressable tests
# This script executes filesystem-based byte-addressable I/O tests with fdatasync
# Does NOT continue with raw device tests
#

set -e  # Exit on error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}===================================${NC}"
echo -e "${GREEN}BYTE-ADDRESSABLE TEST ONLY${NC}"
echo -e "${GREEN}===================================${NC}"
echo ""

# Check if config exists
if [[ ! -f "$SCRIPT_DIR/config.yaml" ]]; then
    echo -e "${RED}Error: config.yaml not found in $SCRIPT_DIR${NC}"
    exit 1
fi

# Parse device from config
DEVICE=$(grep "device:" "$SCRIPT_DIR/config.yaml" | awk '{print $2}')

if [[ -z "$DEVICE" ]]; then
    echo -e "${RED}Error: Device not specified in config.yaml${NC}"
    exit 1
fi

# Check if device exists
if [[ ! -b "$DEVICE" ]]; then
    echo -e "${RED}Error: Device $DEVICE does not exist or is not a block device${NC}"
    exit 1
fi

echo -e "${YELLOW}Device: $DEVICE${NC}"
echo -e "${YELLOW}Test: Byte-addressable I/O with filesystem buffer + fdatasync${NC}"
echo ""

# Check if FIO is installed
if ! command -v fio &> /dev/null; then
    echo -e "${RED}Error: FIO is not installed${NC}"
    echo "Please install FIO: sudo apt-get install fio"
    exit 1
fi

# Run byte-addressable test
echo -e "${GREEN}Starting byte-addressable test...${NC}"
echo "----------------------------------------"

if [[ -f "$SCRIPT_DIR/fio_scripts/test_byte_addressable.sh" ]]; then
    # Make script executable
    chmod +x "$SCRIPT_DIR/fio_scripts/test_byte_addressable.sh"

    # Run the test
    "$SCRIPT_DIR/fio_scripts/test_byte_addressable.sh"

    if [[ $? -eq 0 ]]; then
        echo -e "${GREEN}✓ Byte-addressable test completed successfully${NC}"
    else
        echo -e "${RED}✗ Byte-addressable test failed${NC}"
        exit 1
    fi
else
    echo -e "${RED}Error: test_byte_addressable.sh not found${NC}"
    exit 1
fi

echo ""
echo -e "${GREEN}===================================${NC}"
echo -e "${GREEN}BYTE-ADDRESSABLE TEST COMPLETE${NC}"
echo -e "${GREEN}===================================${NC}"

# Check for results
RESULTS_DIR="$SCRIPT_DIR/results/filesystem/byte_addressable"
if [[ -d "$RESULTS_DIR" ]]; then
    echo ""
    echo "Results saved to: $RESULTS_DIR"

    # Count JSON files
    JSON_COUNT=$(find "$RESULTS_DIR" -name "*.json" | wc -l)
    echo "Generated $JSON_COUNT test result files"

    # Show summary if exists
    if [[ -f "$RESULTS_DIR/byte_addressable_summary.csv" ]]; then
        echo ""
        echo "Summary CSV available at:"
        echo "  $RESULTS_DIR/byte_addressable_summary.csv"
    fi

    # Show report if exists
    if [[ -f "$RESULTS_DIR/byte_addressable_report.txt" ]]; then
        echo ""
        echo "Detailed report available at:"ses
exit 0