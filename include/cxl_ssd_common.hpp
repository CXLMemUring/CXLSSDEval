#ifndef CXL_SSD_COMMON_HPP
#define CXL_SSD_COMMON_HPP

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace cxl_ssd {

// CXL protocol types
enum class CXLProtocol {
    CXL_IO,     // Traditional I/O semantics
    CXL_MEM,    // Memory semantics
    CXL_CACHE   // Cache coherent
};

// CXL SSD device capabilities
struct CXLCapabilities {
    bool supports_pmr;          // Persistent Memory Region
    bool supports_cmb;          // Controller Memory Buffer
    bool supports_compression;  // Hardware compression
    bool supports_mwait;        // MONITOR/MWAIT support
    uint32_t cxl_version;       // CXL version (e.g., 0x20 for 2.0)
    uint64_t pmr_size;          // PMR size in bytes
    uint64_t cmb_size;          // CMB size in bytes
};

// CXL SSD device handle
class CXLDevice {
public:
    virtual ~CXLDevice() = default;
    
    // Open CXL device
    virtual bool open(const std::string& device_path) = 0;
    
    // Close device
    virtual void close() = 0;
    
    // Get device capabilities
    virtual CXLCapabilities get_capabilities() const = 0;
    
    // Get device name
    virtual std::string get_name() const = 0;
    
    // Check if device is open
    virtual bool is_open() const = 0;
};

// Memory region types
enum class MemoryRegionType {
    PMR,    // Persistent Memory Region
    CMB,    // Controller Memory Buffer
    NAND    // NAND flash storage
};

// Memory region descriptor
struct MemoryRegion {
    MemoryRegionType type;
    uint64_t base_address;
    uint64_t size;
    uint32_t access_flags;  // Read/Write/Execute permissions
    bool is_cached;
    bool is_persistent;
};

// Performance counters
struct PerfCounters {
    uint64_t read_ops;
    uint64_t write_ops;
    uint64_t read_bytes;
    uint64_t write_bytes;
    uint64_t compression_ratio_x100;  // Ratio * 100 for integer math
    uint64_t pmr_hits;
    uint64_t pmr_misses;
    uint64_t cmb_utilization_percent;
    std::chrono::nanoseconds avg_read_latency;
    std::chrono::nanoseconds avg_write_latency;
};

// Error codes
enum class ErrorCode {
    SUCCESS = 0,
    DEVICE_NOT_FOUND,
    PERMISSION_DENIED,
    NOT_SUPPORTED,
    INVALID_PARAMETER,
    TIMEOUT,
    IO_ERROR,
    MEMORY_ERROR,
    UNKNOWN_ERROR
};

// Convert error code to string
std::string error_to_string(ErrorCode error);

} // namespace cxl_ssd

#endif // CXL_SSD_COMMON_HPP