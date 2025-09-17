#include <cstdint>
#include <cstring>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <immintrin.h>
#include <cpuid.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace cxl_dax {

class DAXDevice {
private:
    int fd;
    void* mapped_base;
    size_t mapped_size;
    std::string device_path;

public:
    DAXDevice() : fd(-1), mapped_base(nullptr), mapped_size(0) {}

    ~DAXDevice() {
        cleanup();
    }

    bool init(const std::string& dax_path, size_t size = 0) {
        cleanup();

        device_path = dax_path;

        // Open DAX device (e.g., /dev/dax0.0 or /dev/pmem0)
        fd = ::open(dax_path.c_str(), O_RDWR | O_SYNC);
        if (fd < 0) {
            std::cerr << "Failed to open DAX device: " << dax_path
                     << " - " << strerror(errno) << std::endl;
            return false;
        }

        // Get device size if not specified
        if (size == 0) {
            off_t device_size = lseek(fd, 0, SEEK_END);
            if (device_size < 0) {
                std::cerr << "Failed to get DAX device size" << std::endl;
                ::close(fd);
                fd = -1;
                return false;
            }
            size = static_cast<size_t>(device_size);
            lseek(fd, 0, SEEK_SET);
        }

        mapped_size = size;

        // Map the DAX device into memory
        // Use MAP_SYNC for DAX devices to ensure persistent memory semantics
        mapped_base = mmap(nullptr, mapped_size,
                          PROT_READ | PROT_WRITE,
                          MAP_SHARED | MAP_SYNC, fd, 0);

        if (mapped_base == MAP_FAILED) {
            std::cerr << "Failed to mmap DAX device: " << strerror(errno) << std::endl;
            ::close(fd);
            fd = -1;
            mapped_base = nullptr;
            return false;
        }

        // Advise kernel about usage pattern
        madvise(mapped_base, mapped_size, MADV_HUGEPAGE);

        return true;
    }

    void cleanup() {
        if (mapped_base && mapped_base != MAP_FAILED) {
            munmap(mapped_base, mapped_size);
            mapped_base = nullptr;
        }
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
        mapped_size = 0;
    }

    void* get_base() const { return mapped_base; }
    size_t get_size() const { return mapped_size; }

    // Direct load/store operations
    template<typename T>
    T load(size_t offset) const {
        if (offset + sizeof(T) > mapped_size) {
            throw std::out_of_range("DAX load out of bounds");
        }
        T* ptr = reinterpret_cast<T*>(static_cast<char*>(mapped_base) + offset);
        return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
    }

    template<typename T>
    void store(size_t offset, T value) {
        if (offset + sizeof(T) > mapped_size) {
            throw std::out_of_range("DAX store out of bounds");
        }
        T* ptr = reinterpret_cast<T*>(static_cast<char*>(mapped_base) + offset);
        __atomic_store_n(ptr, value, __ATOMIC_RELEASE);

        // Ensure persistence for DAX
        _mm_clflushopt(ptr);
        _mm_sfence();
    }

    // Bulk operations
    void read(size_t offset, void* buffer, size_t size) const {
        if (offset + size > mapped_size) {
            throw std::out_of_range("DAX read out of bounds");
        }
        memcpy(buffer, static_cast<char*>(mapped_base) + offset, size);
    }

    void write(size_t offset, const void* buffer, size_t size) {
        if (offset + size > mapped_size) {
            throw std::out_of_range("DAX write out of bounds");
        }

        void* dest = static_cast<char*>(mapped_base) + offset;
        memcpy(dest, buffer, size);

        // Flush cache lines for persistence
        for (size_t i = 0; i < size; i += 64) {
            _mm_clflushopt(static_cast<char*>(dest) + i);
        }
        _mm_sfence();
    }

    // MWAIT support with DAX memory
    bool monitor_wait(size_t offset, uint32_t expected_value,
                      uint32_t timeout_us = 1000) {
        if (offset + sizeof(uint32_t) > mapped_size) {
            return false;
        }

        volatile uint32_t* monitor_addr =
            reinterpret_cast<volatile uint32_t*>(static_cast<char*>(mapped_base) + offset);

        auto start = std::chrono::high_resolution_clock::now();
        auto timeout = std::chrono::microseconds(timeout_us);

        // Check CPU support for MONITOR/MWAIT
        unsigned int eax, ebx, ecx, edx;
        if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx) || !(ecx & (1 << 3))) {
            // Fallback to polling if MWAIT not supported
            while (*monitor_addr == expected_value) {
                auto elapsed = std::chrono::high_resolution_clock::now() - start;
                if (elapsed >= timeout) {
                    return false; // Timeout
                }
                _mm_pause(); // CPU pause for power efficiency
            }
            return true;
        }

        // Use MONITOR/MWAIT
        while (*monitor_addr == expected_value) {
            // Set up monitoring
            asm volatile("monitor"
                        :
                        : "a"(monitor_addr), "c"(0), "d"(0)
                        : "memory");

            // Check again after monitor setup (avoid race)
            if (*monitor_addr != expected_value) {
                break;
            }

            // Calculate remaining timeout
            auto elapsed = std::chrono::high_resolution_clock::now() - start;
            if (elapsed >= timeout) {
                return false; // Timeout
            }

            // Wait with C1 state hint
            asm volatile("mwait"
                        :
                        : "a"(0x01), "c"(0x01)
                        : "memory");
        }

        return true;
    }

    // Flush entire mapped region
    void flush() {
        for (size_t i = 0; i < mapped_size; i += 64) {
            _mm_clflushopt(static_cast<char*>(mapped_base) + i);
        }
        _mm_sfence();
    }
};

} // namespace cxl_dax