#ifndef CXL_MWAIT_HPP
#define CXL_MWAIT_HPP

#include <cstdint>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>

namespace cxl_ssd {

// MWAIT hint values for different C-states
enum class MWaitHint : uint32_t {
    C0 = 0x00,  // No power saving
    C1 = 0x01,  // Light sleep
    C2 = 0x10,  // Medium sleep  
    C3 = 0x20,  // Deep sleep
    C6 = 0x30   // Deepest sleep
};

// Monitor granularity for CXL memory regions
enum class MonitorGranularity {
    BYTE = 1,
    CACHE_LINE = 64,
    PAGE = 4096
};

// CXL PMR monitoring configuration
struct MWaitConfig {
    void* monitor_address;           // Address to monitor in CXL PMR
    uint32_t timeout_us;             // Timeout in microseconds
    MWaitHint hint;                  // Power saving hint
    MonitorGranularity granularity;  // Monitor granularity
    bool enable_interrupt;           // Enable interrupt on wake
    
    MWaitConfig() : 
        monitor_address(nullptr),
        timeout_us(1000),
        hint(MWaitHint::C1),
        granularity(MonitorGranularity::CACHE_LINE),
        enable_interrupt(false) {}
};

// MWAIT result status
enum class MWaitStatus {
    SUCCESS,           // Woken by memory write
    TIMEOUT,          // Timeout expired
    INTERRUPTED,      // Interrupted by signal
    INVALID_ADDRESS,  // Invalid CXL address
    NOT_SUPPORTED     // MWAIT not supported
};

// Main MWAIT class for CXL SSD monitoring
class CXLMWait {
public:
    CXLMWait();
    ~CXLMWait();
    
    // Initialize MWAIT for CXL device
    bool initialize(const std::string& cxl_device_path);
    
    // Check if MWAIT is supported on current CPU and CXL device
    bool is_supported() const;
    
    // Monitor CXL PMR address with MWAIT
    MWaitStatus monitor_wait(const MWaitConfig& config);
    
    // Monitor with callback on wake
    MWaitStatus monitor_wait_callback(const MWaitConfig& config,
                                      std::function<void()> callback);
    
    // Batch monitor multiple addresses
    MWaitStatus monitor_wait_batch(const std::vector<MWaitConfig>& configs);
    
    // Get last error message
    std::string get_last_error() const;
    
    // Get MWAIT statistics
    struct MWaitStats {
        uint64_t total_waits;
        uint64_t successful_wakes;
        uint64_t timeouts;
        uint64_t interrupts;
        std::chrono::nanoseconds total_wait_time;
        std::chrono::nanoseconds avg_wait_time;
    };
    
    MWaitStats get_stats() const;
    void reset_stats();
    
private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

// Low-level MWAIT primitives
namespace primitives {
    
    // Check CPUID for MONITOR/MWAIT support
    bool check_mwait_support();
    
    // Execute MONITOR instruction
    void monitor(void* address, uint32_t extensions, uint32_t hints);
    
    // Execute MWAIT instruction  
    void mwait(uint32_t extensions, uint32_t hints);
    
    // Get maximum MWAIT C-state supported
    uint32_t get_max_cstate();
    
    // Check if address is in CXL PMR range
    bool is_cxl_pmr_address(void* address);
}

// Utility functions
namespace utils {
    
    // Map CXL PMR into process address space
    void* map_cxl_pmr(const std::string& device_path, size_t offset, size_t size);
    
    // Unmap CXL PMR
    void unmap_cxl_pmr(void* addr, size_t size);
    
    // Get CXL device PMR info
    struct PMRInfo {
        uint64_t base_addr;
        uint64_t size;
        bool persistent;
        bool cached;
    };
    
    PMRInfo get_pmr_info(const std::string& device_path);
}

} // namespace cxl_ssd

#endif // CXL_MWAIT_HPP