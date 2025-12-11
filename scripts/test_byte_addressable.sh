#!/bin/bash

# Compatibility wrapper for the raw byte-addressable test.
# The implementation now lives under fio_scripts to keep all
# FIO-oriented helpers together.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET_SCRIPT="${SCRIPT_DIR}/fio_scripts/test_raw_byte_addressable.sh"

if [[ ! -f "$TARGET_SCRIPT" ]]; then
    echo "Error: ${TARGET_SCRIPT} not found"
    exit 1
fi

exec "$TARGET_SCRIPT" "$@"
