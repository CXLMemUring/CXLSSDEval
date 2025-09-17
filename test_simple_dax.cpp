#include <iostream>
#include <cstring>
#include <vector>
#include "src/cxl_mwait_dax.cpp"

int main() {
    using namespace cxl_dax;

    std::cout << "Simple DAX Device Test\n";
    std::cout << "======================\n\n";

    DAXDevice device;

    // Try to use /tmp as a test (won't be real DAX but tests the code)
    // In production, use /dev/dax0.0 or /dev/pmem0
    std::string test_file = "/tmp/test_dax_file";

    // Create a test file
    int fd = open(test_file.c_str(), O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        std::cerr << "Failed to create test file\n";
        return 1;
    }

    // Make it 1MB
    if (ftruncate(fd, 1024 * 1024) < 0) {
        std::cerr << "Failed to set file size\n";
        close(fd);
        return 1;
    }
    close(fd);

    // Now try to map it
    if (!device.init(test_file, 1024 * 1024)) {
        std::cerr << "Failed to initialize DAX device with test file\n";
        std::cerr << "In production, use a real DAX device like /dev/dax0.0\n";
        return 1;
    }

    std::cout << "✓ Device initialized\n";

    // Test 1: Basic write/read
    const char* test_data = "Hello DAX World!";
    device.write(0, test_data, strlen(test_data) + 1);

    char buffer[100] = {0};
    device.read(0, buffer, strlen(test_data) + 1);

    if (strcmp(buffer, test_data) == 0) {
        std::cout << "✓ Basic write/read test passed\n";
    } else {
        std::cout << "✗ Basic write/read test failed\n";
    }

    // Test 2: Byte-addressable operations
    std::vector<size_t> sizes = {1, 7, 15, 63, 127, 255, 511};
    bool all_passed = true;

    for (size_t size : sizes) {
        std::vector<uint8_t> write_data(size);
        std::vector<uint8_t> read_data(size);

        // Fill with test pattern
        for (size_t i = 0; i < size; i++) {
            write_data[i] = (i * 7 + 13) % 256;
        }

        size_t offset = 1000 + size * 3; // Unaligned offset
        device.write(offset, write_data.data(), size);
        device.read(offset, read_data.data(), size);

        if (memcmp(write_data.data(), read_data.data(), size) != 0) {
            std::cout << "✗ Byte-addressable test failed for size " << size << "\n";
            all_passed = false;
        }
    }

    if (all_passed) {
        std::cout << "✓ All byte-addressable tests passed (sizes: 1-511 bytes)\n";
    }

    // Test 3: Atomic operations
    uint64_t test_value = 0xDEADBEEFCAFEBABE;
    device.store<uint64_t>(2048, test_value);
    uint64_t read_value = device.load<uint64_t>(2048);

    if (read_value == test_value) {
        std::cout << "✓ Atomic store/load test passed\n";
    } else {
        std::cout << "✗ Atomic store/load test failed\n";
    }

    std::cout << "\nTest completed successfully!\n";
    std::cout << "Note: For real CXL/DAX testing, use actual DAX devices:\n";
    std::cout << "  - /dev/dax0.0 (DAX device)\n";
    std::cout << "  - /dev/pmem0 (Persistent memory)\n";

    // Cleanup
    unlink(test_file.c_str());

    return 0;
}