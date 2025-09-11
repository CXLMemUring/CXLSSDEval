#include <iostream>
#include <memory>
#include <cstring>
#include <iomanip>
#include "../include/cxl_device_impl.hpp"

using namespace cxl_ssd;

// Test function for DevDax device
void test_devdax_device(const std::string& device_path) {
    std::cout << "\n=== Testing DevDax Device Access ===" << std::endl;
    std::cout << "Device path: " << device_path << std::endl;
    
    auto device = std::make_unique<DevDaxDevice>();
    
    // Open device
    if (!device->open(device_path)) {
        std::cerr << "Failed to open devdax device: " << device_path << std::endl;
        return;
    }
    
    std::cout << "Successfully opened devdax device" << std::endl;
    
    // Get and display capabilities
    auto caps = device->get_capabilities();
    std::cout << "\nDevice Capabilities:" << std::endl;
    std::cout << "  CXL Version: " << std::hex << "0x" << caps.cxl_version << std::dec << std::endl;
    std::cout << "  Supports PMR: " << (caps.supports_pmr ? "Yes" : "No") << std::endl;
    std::cout << "  PMR Size: " << caps.pmr_size / (1024 * 1024) << " MB" << std::endl;
    std::cout << "  Supports MWAIT: " << (caps.supports_mwait ? "Yes" : "No") << std::endl;
    
    // Test direct memory access
    std::cout << "\nTesting Direct Memory Access:" << std::endl;
    
    // Write test pattern
    const size_t test_size = 4096;
    std::vector<uint8_t> write_buffer(test_size);
    for (size_t i = 0; i < test_size; i++) {
        write_buffer[i] = i % 256;
    }
    
    if (device->write_direct(write_buffer.data(), 0, test_size)) {
        std::cout << "  Wrote " << test_size << " bytes successfully" << std::endl;
    } else {
        std::cerr << "  Failed to write data" << std::endl;
    }
    
    // Read back and verify
    std::vector<uint8_t> read_buffer(test_size);
    if (device->read_direct(read_buffer.data(), 0, test_size)) {
        std::cout << "  Read " << test_size << " bytes successfully" << std::endl;
        
        // Verify data
        bool match = true;
        for (size_t i = 0; i < test_size; i++) {
            if (read_buffer[i] != write_buffer[i]) {
                match = false;
                break;
            }
        }
        std::cout << "  Data verification: " << (match ? "PASSED" : "FAILED") << std::endl;
    } else {
        std::cerr << "  Failed to read data" << std::endl;
    }
    
    // Display memory mapping info
    std::cout << "\nMemory Mapping Info:" << std::endl;
    std::cout << "  Mapped address: " << device->get_mapped_memory() << std::endl;
    std::cout << "  Mapped size: " << device->get_mapped_size() / (1024 * 1024) << " MB" << std::endl;
    
    // Close device
    device->close();
    std::cout << "\nDevdax device closed successfully" << std::endl;
}

// Test function for NVMe device
void test_nvme_device(const std::string& device_path) {
    std::cout << "\n=== Testing NVMe Device Access ===" << std::endl;
    std::cout << "Device path: " << device_path << std::endl;
    
    auto device = std::make_unique<NVMeDevice>();
    
    // Open device
    if (!device->open(device_path)) {
        std::cerr << "Failed to open NVMe device: " << device_path << std::endl;
        return;
    }
    
    std::cout << "Successfully opened NVMe device" << std::endl;
    
    // Get and display capabilities
    auto caps = device->get_capabilities();
    std::cout << "\nDevice Capabilities:" << std::endl;
    std::cout << "  CXL Version: " << std::hex << "0x" << caps.cxl_version << std::dec << std::endl;
    std::cout << "  Supports PMR: " << (caps.supports_pmr ? "Yes" : "No") << std::endl;
    if (caps.supports_pmr) {
        std::cout << "  PMR Size: " << caps.pmr_size / (1024 * 1024) << " MB" << std::endl;
    }
    std::cout << "  Supports CMB: " << (caps.supports_cmb ? "Yes" : "No") << std::endl;
    if (caps.supports_cmb) {
        std::cout << "  CMB Size: " << caps.cmb_size / (1024 * 1024) << " MB" << std::endl;
    }
    
    // Get namespace info
    std::cout << "\nNamespace Information:" << std::endl;
    std::cout << "  Namespace size: " << device->get_namespace_size() << " blocks" << std::endl;
    std::cout << "  Logical block size: " << device->get_lba_size() << " bytes" << std::endl;
    
    // Test NVMe I/O operations
    std::cout << "\nTesting NVMe I/O Operations:" << std::endl;
    
    const uint32_t lba_size = device->get_lba_size();
    const uint32_t num_blocks = 8;
    const size_t buffer_size = lba_size * num_blocks;
    
    // Allocate aligned buffer for NVMe I/O
    void* write_buffer_aligned = nullptr;
    void* read_buffer_aligned = nullptr;
    
    if (posix_memalign(&write_buffer_aligned, 4096, buffer_size) != 0 ||
        posix_memalign(&read_buffer_aligned, 4096, buffer_size) != 0) {
        std::cerr << "Failed to allocate aligned memory" << std::endl;
        device->close();
        return;
    }
    
    // Initialize write buffer with test pattern
    uint8_t* write_buffer = static_cast<uint8_t*>(write_buffer_aligned);
    for (size_t i = 0; i < buffer_size; i++) {
        write_buffer[i] = (i % 256) ^ 0xAA;
    }
    
    // Write data
    if (device->nvme_write(write_buffer, 0, num_blocks)) {
        std::cout << "  Wrote " << num_blocks << " blocks (" << buffer_size << " bytes) successfully" << std::endl;
    } else {
        std::cerr << "  Failed to write blocks (may require sudo)" << std::endl;
    }
    
    // Read data back
    uint8_t* read_buffer = static_cast<uint8_t*>(read_buffer_aligned);
    if (device->nvme_read(read_buffer, 0, num_blocks)) {
        std::cout << "  Read " << num_blocks << " blocks (" << buffer_size << " bytes) successfully" << std::endl;
        
        // Verify data
        bool match = true;
        for (size_t i = 0; i < buffer_size; i++) {
            if (read_buffer[i] != write_buffer[i]) {
                match = false;
                break;
            }
        }
        std::cout << "  Data verification: " << (match ? "PASSED" : "FAILED") << std::endl;
    } else {
        std::cerr << "  Failed to read blocks (may require sudo)" << std::endl;
    }
    
    // Free aligned buffers
    free(write_buffer_aligned);
    free(read_buffer_aligned);
    
    // Close device
    device->close();
    std::cout << "\nNVMe device closed successfully" << std::endl;
}

// Helper function to test factory creation
void test_factory_creation() {
    std::cout << "\n=== Testing Factory Creation ===" << std::endl;
    
    auto devdax_device = create_cxl_device("devdax");
    if (devdax_device) {
        std::cout << "DevDax device created successfully via factory" << std::endl;
    } else {
        std::cerr << "Failed to create DevDax device via factory" << std::endl;
    }
    
    auto nvme_device = create_cxl_device("nvme");
    if (nvme_device) {
        std::cout << "NVMe device created successfully via factory" << std::endl;
    } else {
        std::cerr << "Failed to create NVMe device via factory" << std::endl;
    }
    
    auto invalid_device = create_cxl_device("invalid");
    if (!invalid_device) {
        std::cout << "Invalid device type correctly returned nullptr" << std::endl;
    }
}

int main(int argc, char* argv[]) {
    std::cout << "CXL Device Access Methods Test" << std::endl;
    std::cout << "==============================" << std::endl;
    
    // Set logging level
    Logger::set_level(LogLevel::INFO);
    
    // Test factory creation
    test_factory_creation();
    
    // Parse command line arguments
    if (argc > 1) {
        for (int i = 1; i < argc; i += 2) {
            if (i + 1 < argc) {
                std::string type = argv[i];
                std::string path = argv[i + 1];
                
                if (type == "--devdax" || type == "-d") {
                    test_devdax_device(path);
                } else if (type == "--nvme" || type == "-n") {
                    test_nvme_device(path);
                } else {
                    std::cerr << "Unknown option: " << type << std::endl;
                }
            }
        }
    } else {
        // Print usage if no arguments provided
        std::cout << "\nUsage:" << std::endl;
        std::cout << "  " << argv[0] << " [options]" << std::endl;
        std::cout << "\nOptions:" << std::endl;
        std::cout << "  --devdax, -d <device>  Test devdax device (e.g., /dev/dax0.0)" << std::endl;
        std::cout << "  --nvme, -n <device>    Test NVMe device (e.g., /dev/nvme0n1)" << std::endl;
        std::cout << "\nExamples:" << std::endl;
        std::cout << "  " << argv[0] << " --devdax /dev/dax0.0" << std::endl;
        std::cout << "  " << argv[0] << " --nvme /dev/nvme0n1" << std::endl;
        std::cout << "  " << argv[0] << " -d /dev/dax0.0 -n /dev/nvme0n1" << std::endl;
        
        // Run demo with mock paths
        std::cout << "\n=== Running Demo with Mock Paths ===" << std::endl;
        std::cout << "(These will fail unless the devices actually exist)" << std::endl;
        test_devdax_device("/dev/dax0.0");
        test_nvme_device("/dev/nvme0n1");
    }
    
    return 0;
}