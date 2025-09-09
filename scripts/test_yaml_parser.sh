#!/bin/bash

# Test YAML parser to verify it correctly handles quoted strings with comments

# Create a test YAML file
TEST_YAML="/tmp/test_parser.yaml"
cat > "$TEST_YAML" << 'EOF'
# Test Configuration
device:
  raw_device: "/dev/nvme1n2"      # Raw device path for CXL SSD  
  filesystem_mount: "/nv1"         # Mount point for filesystem tests
  simple: /dev/test                # No quotes
  with_spaces: "/dev/test device"  # Path with spaces
EOF

echo "Testing YAML parser..."
echo "====================="
echo "Test YAML content:"
cat "$TEST_YAML"
echo ""
echo "====================="

# Source the common functions
source ./fio_scripts/common.sh

# Set config file to test YAML
export CONFIG_FILE="$TEST_YAML"

# Parse the YAML
eval $(parse_yaml $CONFIG_FILE "CONFIG_")

# Display parsed values
echo "Parsed values:"
echo "  raw_device: [$CONFIG_device_raw_device]"
echo "  filesystem_mount: [$CONFIG_device_filesystem_mount]"
echo "  simple: [$CONFIG_device_simple]"
echo "  with_spaces: [$CONFIG_device_with_spaces]"

# Test if device value is clean
if [[ "$CONFIG_device_raw_device" == "/dev/nvme1n2" ]]; then
    echo ""
    echo "✓ SUCCESS: Parser correctly extracted device path"
else
    echo ""
    echo "✗ FAILED: Parser included extra characters"
    echo "  Expected: [/dev/nvme1n2]"
    echo "  Got: [$CONFIG_device_raw_device]"
fi

# Clean up
rm -f "$TEST_YAML"