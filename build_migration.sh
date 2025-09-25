#!/bin/bash
# Build script for CXLSSDEval with live migration support
# Generates both x86_64 and ARM64 binaries

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== CXLSSDEval Live Migration Build Script ===${NC}"

# Check for cross-compiler
if ! command -v aarch64-linux-gnu-gcc &> /dev/null; then
    echo -e "${YELLOW}Warning: ARM cross-compiler not found.${NC}"
    echo "Install with: sudo apt-get install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu"
    echo "Continuing with x86_64 build only..."
    ARM_BUILD=0
else
    ARM_BUILD=1
fi

# Clean previous builds
echo -e "${GREEN}Cleaning previous builds...${NC}"
rm -rf build_x86 build_arm

# Build for x86_64
echo -e "${GREEN}Building for x86_64...${NC}"
mkdir -p build_x86
cd build_x86

cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_ARM_TARGET=OFF \
    -DCXL_ENABLE_MVVM=ON

make -j$(nproc) wasm_scheduler

echo -e "${GREEN}x86_64 build complete: $(pwd)/libwasm_scheduler.a${NC}"
cd ..

# Build for ARM64 if cross-compiler is available
if [ $ARM_BUILD -eq 1 ]; then
    echo -e "${GREEN}Building for ARM64...${NC}"
    mkdir -p build_arm
    cd build_arm

    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_ARM_TARGET=ON \
        -DCXL_ENABLE_MVVM=ON \
        -DCMAKE_TOOLCHAIN_FILE=../arm_toolchain.cmake

    make -j$(nproc) wasm_scheduler

    echo -e "${GREEN}ARM64 build complete: $(pwd)/libwasm_scheduler.a${NC}"
    cd ..
fi

# Create migration test program
echo -e "${GREEN}Creating migration test program...${NC}"
cat > test_migration.cpp << 'EOF'
#include <iostream>
#include <chrono>
#include <thread>
#include <cstdlib>
#include "include/wasm_scheduler.hpp"

int main(int argc, char* argv[]) {
    using namespace cxl;

    // Set migration environment variables
    setenv("MIGRATION_CACHE_THRESHOLD_MB", "4096", 1);  // 4GB cache threshold
    setenv("MIGRATION_TEMP_THRESHOLD_C", "75", 1);      // 75°C temperature threshold
    setenv("MIGRATION_PORT", "9876", 1);

    std::cout << "=== CXLSSDEval Live Migration Test ===" << std::endl;
    std::cout << "Architecture: "
#ifdef __x86_64__
              << "x86_64 (SOURCE)"
#elif defined(__aarch64__)
              << "ARM64 (TARGET)"
#else
              << "Unknown"
#endif
              << std::endl;

    WasmScheduler scheduler;

#ifdef __x86_64__
    // x86 source: launch a task
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <wasm_file> [ARM_HOST_IP]" << std::endl;
        return 1;
    }

    if (argc >= 3) {
        setenv("ARM_MIGRATION_HOST", argv[2], 1);
        std::cout << "ARM migration target set to: " << argv[2] << std::endl;
    }

    WasmTaskDesc desc;
    desc.module_path = argv[1];
    desc.entry = "main";
    desc.args = {};

    int task_id = scheduler.launch(desc, TargetArch::X86_64);
    std::cout << "Launched task " << task_id << " on x86_64" << std::endl;
    std::cout << "Monitoring system metrics for migration triggers..." << std::endl;

    // Keep running until migration occurs
    std::this_thread::sleep_for(std::chrono::minutes(5));

#elif defined(__aarch64__)
    // ARM target: wait for incoming migrations
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <wasm_file>" << std::endl;
        return 1;
    }

    WasmTaskDesc desc;
    desc.module_path = argv[1];
    desc.entry = "main";
    desc.args = {};

    std::cout << "Waiting for incoming migration on port 9876..." << std::endl;

    while (true) {
        if (scheduler.receive_migration(desc)) {
            std::cout << "Migration received and resumed successfully!" << std::endl;
            std::this_thread::sleep_for(std::chrono::minutes(5));
            break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
#endif

    std::cout << "Test complete." << std::endl;
    return 0;
}
EOF

# Build test programs
echo -e "${GREEN}Building test programs...${NC}"

# Build x86 test
if [ -d build_x86 ]; then
    cd build_x86
    g++ -std=c++20 -O2 -o test_migration_x86 \
        ../test_migration.cpp \
        -I.. \
        -L. -lwasm_scheduler \
        -lpthread
    echo -e "${GREEN}x86 test program: $(pwd)/test_migration_x86${NC}"
    cd ..
fi

# Build ARM test if possible
if [ $ARM_BUILD -eq 1 ] && [ -d build_arm ]; then
    cd build_arm
    aarch64-linux-gnu-g++ -std=c++20 -O2 -o test_migration_arm \
        ../test_migration.cpp \
        -I.. \
        -L. -lwasm_scheduler \
        -lpthread
    echo -e "${GREEN}ARM test program: $(pwd)/test_migration_arm${NC}"
    cd ..
fi

echo -e "${GREEN}=== Build Complete ===${NC}"
echo ""
echo "To test live migration:"
echo "1. On x86 machine:"
echo "   export ARM_MIGRATION_HOST=<arm_host_ip>"
echo "   ./build_x86/test_migration_x86 <wasm_file>"
echo ""
if [ $ARM_BUILD -eq 1 ]; then
    echo "2. On ARM machine (or copy binary):"
    echo "   ./build_arm/test_migration_arm <wasm_file>"
    echo ""
fi
echo "The system will automatically trigger migration when:"
echo "- Page cache exceeds 4GB (configurable via MIGRATION_CACHE_THRESHOLD_MB)"
echo "- CPU temperature exceeds 75°C (configurable via MIGRATION_TEMP_THRESHOLD_C)"
echo "- Memory pressure exceeds 85%"