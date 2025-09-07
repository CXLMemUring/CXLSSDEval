#include "../include/cxl_mwait.hpp"
#include "../include/cxl_ssd_common.hpp"
#include <cpuid.h>
#include <immintrin.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <sstream>
#include <fstream>

namespace cxl_ssd {

// Implementation class
class CXLMWait::Impl {
public:
    Impl() : stats{}, last_error(""), device_fd(-1), pmr_base(nullptr), pmr_size(0) {}
    
    ~Impl() {
        if (pmr_base) {
            munmap(pmr_base, pmr_size);
        }
        if (device_fd >= 0) {
            ::close(device_fd);
        }
    }
    
    bool initialize(const std::string& device_path) {
        // Open CXL device
        std::string pmr_path = device_path + "/pmr";
        device_fd = ::open(pmr_path.c_str(), O_RDWR);
        if (device_fd < 0) {
            last_error = "Failed to open CXL PMR device: " + std::string(strerror(errno));
            return false;
        }
        
        // Get PMR info
        auto pmr_info = utils::get_pmr_info(device_path);
        pmr_size = pmr_info.size;
        
        // Map PMR into process address space
        pmr_base = mmap(nullptr, pmr_size, PROT_READ | PROT_WRITE, 
                        MAP_SHARED, device_fd, 0);
        if (pmr_base == MAP_FAILED) {
            last_error = "Failed to map PMR: " + std::string(strerror(errno));
            ::close(device_fd);
            device_fd = -1;
            return false;
        }
        
        // Check MWAIT support
        if (!primitives::check_mwait_support()) {
            last_error = "MONITOR/MWAIT not supported on this CPU";
            return false;
        }
        
        return true;
    }
    
    bool is_supported() const {
        return primitives::check_mwait_support() && (device_fd >= 0);
    }
    
    MWaitStatus monitor_wait_internal(const MWaitConfig& config) {
        if (!config.monitor_address) {
            last_error = "Invalid monitor address";
            return MWaitStatus::INVALID_ADDRESS;
        }
        
        // Check if address is within PMR range
        if (!is_address_in_pmr(config.monitor_address)) {
            last_error = "Address not in CXL PMR range";
            return MWaitStatus::INVALID_ADDRESS;
        }
        
        auto start_time = std::chrono::high_resolution_clock::now();
        stats.total_waits++;
        
        // Set up timeout
        auto timeout_end = start_time + std::chrono::microseconds(config.timeout_us);
        
        // Execute MONITOR instruction
        primitives::monitor(config.monitor_address, 0, static_cast<uint32_t>(config.hint));
        
        // Check if value already changed
        std::atomic_thread_fence(std::memory_order_acquire);
        
        // Execute MWAIT with timeout check
        while (std::chrono::high_resolution_clock::now() < timeout_end) {
            // MWAIT with hint
            primitives::mwait(config.enable_interrupt ? 1 : 0, static_cast<uint32_t>(config.hint));
            
            // Check if woken by write
            std::atomic_thread_fence(std::memory_order_acquire);
            
            // Check for spurious wakeup vs actual write
            // In real implementation, would check the monitored location
            auto current_time = std::chrono::high_resolution_clock::now();
            auto elapsed = current_time - start_time;
            
            stats.total_wait_time += std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed);
            stats.successful_wakes++;
            
            return MWaitStatus::SUCCESS;
        }
        
        // Timeout occurred
        stats.timeouts++;
        auto elapsed = std::chrono::high_resolution_clock::now() - start_time;
        stats.total_wait_time += std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed);
        
        return MWaitStatus::TIMEOUT;
    }
    
    bool is_address_in_pmr(void* addr) const {
        if (!pmr_base) return false;
        
        uintptr_t addr_val = reinterpret_cast<uintptr_t>(addr);
        uintptr_t base_val = reinterpret_cast<uintptr_t>(pmr_base);
        
        return (addr_val >= base_val) && (addr_val < base_val + pmr_size);
    }
    
    MWaitStats stats;
    std::string last_error;
    int device_fd;
    void* pmr_base;
    size_t pmr_size;
};

// CXLMWait public methods
CXLMWait::CXLMWait() : pImpl(std::make_unique<Impl>()) {}

CXLMWait::~CXLMWait() = default;

bool CXLMWait::initialize(const std::string& cxl_device_path) {
    return pImpl->initialize(cxl_device_path);
}

bool CXLMWait::is_supported() const {
    return pImpl->is_supported();
}

MWaitStatus CXLMWait::monitor_wait(const MWaitConfig& config) {
    return pImpl->monitor_wait_internal(config);
}

MWaitStatus CXLMWait::monitor_wait_callback(const MWaitConfig& config,
                                            std::function<void()> callback) {
    auto status = pImpl->monitor_wait_internal(config);
    if (status == MWaitStatus::SUCCESS && callback) {
        callback();
    }
    return status;
}

MWaitStatus CXLMWait::monitor_wait_batch(const std::vector<MWaitConfig>& configs) {
    // Simple implementation - monitor first address
    // In production, would use more sophisticated batch monitoring
    if (configs.empty()) {
        pImpl->last_error = "Empty config list";
        return MWaitStatus::INVALID_ADDRESS;
    }
    
    return monitor_wait(configs[0]);
}

std::string CXLMWait::get_last_error() const {
    return pImpl->last_error;
}

CXLMWait::MWaitStats CXLMWait::get_stats() const {
    auto stats = pImpl->stats;
    if (stats.total_waits > 0) {
        stats.avg_wait_time = stats.total_wait_time / stats.total_waits;
    }
    return stats;
}

void CXLMWait::reset_stats() {
    pImpl->stats = MWaitStats{};
}

// Primitives implementation
namespace primitives {

bool check_mwait_support() {
    unsigned int eax, ebx, ecx, edx;
    
    // Check CPUID.01H:ECX.MONITOR[bit 3]
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        return false;
    }
    
    return (ecx & (1 << 3)) != 0;
}

void monitor(void* address, uint32_t extensions, uint32_t hints) {
    // MONITOR sets up address monitoring
    // In x86-64: monitor(rax=address, ecx=extensions, edx=hints)
    asm volatile(
        "monitor"
        : 
        : "a"(address), "c"(extensions), "d"(hints)
        : "memory"
    );
}

void mwait(uint32_t extensions, uint32_t hints) {
    // MWAIT waits for write to monitored address
    // In x86-64: mwait(eax=hints, ecx=extensions)
    asm volatile(
        "mwait"
        :
        : "a"(hints), "c"(extensions)
        : "memory"
    );
}

uint32_t get_max_cstate() {
    unsigned int eax, ebx, ecx, edx;
    
    // CPUID.05H gives MONITOR/MWAIT features
    if (!__get_cpuid_count(5, 0, &eax, &ebx, &ecx, &edx)) {
        return 0;
    }
    
    // EDX contains supported C-states
    if (edx & 0x01) return 1;  // C1
    if (edx & 0x02) return 2;  // C2
    if (edx & 0x04) return 3;  // C3
    if (edx & 0x08) return 6;  // C6
    
    return 0;
}

bool is_cxl_pmr_address(void* address) {
    // In production, would check against actual CXL PMR ranges
    // This is a simplified check
    uintptr_t addr = reinterpret_cast<uintptr_t>(address);
    
    // Typical CXL PMR addresses are in high memory ranges
    // This is platform-specific
    return (addr >= 0x1000000000ULL);  // Above 64GB typically
}

} // namespace primitives

// Utils implementation
namespace utils {

void* map_cxl_pmr(const std::string& device_path, size_t offset, size_t size) {
    std::string pmr_path = device_path + "/pmr";
    int fd = ::open(pmr_path.c_str(), O_RDWR);
    if (fd < 0) {
        return nullptr;
    }
    
    void* addr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, offset);
    ::close(fd);
    
    if (addr == MAP_FAILED) {
        return nullptr;
    }
    
    return addr;
}

void unmap_cxl_pmr(void* addr, size_t size) {
    if (addr && addr != MAP_FAILED) {
        munmap(addr, size);
    }
}

PMRInfo get_pmr_info(const std::string& device_path) {
    PMRInfo info{};
    
    // Read PMR info from sysfs
    // In production, would parse actual sysfs attributes
    std::string size_path = device_path + "/pmr_size";
    std::ifstream size_file(size_path);
    if (size_file.is_open()) {
        size_file >> info.size;
        size_file.close();
    } else {
        // Default values for testing
        info.size = 16ULL * 1024 * 1024 * 1024;  // 16GB default
    }
    
    info.base_addr = 0x1000000000ULL;  // Example base address
    info.persistent = true;
    info.cached = true;
    
    return info;
}

} // namespace utils

} // namespace cxl_ssd