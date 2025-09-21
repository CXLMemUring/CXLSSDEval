#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <string>
#include <map>
#include <mutex>
#include <atomic>
#include <immintrin.h>
#include <errno.h>

// LD_PRELOAD library to intercept fio's read/write/fsync syscalls
// and redirect them to memory-mapped DAX device operations

namespace {

// Function pointers to original libc functions
using open_fn = int(*)(const char*, int, ...);
using close_fn = int(*)(int);
using read_fn = ssize_t(*)(int, void*, size_t);
using write_fn = ssize_t(*)(int, const void*, size_t);
using pread_fn = ssize_t(*)(int, void*, size_t, off_t);
using pwrite_fn = ssize_t(*)(int, const void*, size_t, off_t);
using fsync_fn = int(*)(int);
using lseek_fn = off_t(*)(int, off_t, int);
using ftruncate_fn = int(*)(int, off_t);

// Original function pointers
open_fn real_open = nullptr;
close_fn real_close = nullptr;
read_fn real_read = nullptr;
write_fn real_write = nullptr;
pread_fn real_pread = nullptr;
pwrite_fn real_pwrite = nullptr;
fsync_fn real_fsync = nullptr;
lseek_fn real_lseek = nullptr;
ftruncate_fn real_ftruncate = nullptr;

// DAX device management
struct DAXMapping {
    void* base;
    size_t size;
    off_t current_offset;
    std::string path;
    int real_fd;
};

std::map<int, DAXMapping> dax_mappings;
std::mutex mappings_mutex;
std::atomic<int> fake_fd_counter{10000}; // Start from high FD numbers

// Configuration from environment
bool intercept_enabled = false;
std::string dax_device_path;
size_t dax_device_size = 0;
void* global_dax_base = nullptr;
int global_dax_fd = -1;

// Initialize interception
__attribute__((constructor))
void init_intercept() {
    // Load original functions
    real_open = (open_fn)dlsym(RTLD_NEXT, "open");
    real_close = (close_fn)dlsym(RTLD_NEXT, "close");
    real_read = (read_fn)dlsym(RTLD_NEXT, "read");
    real_write = (write_fn)dlsym(RTLD_NEXT, "write");
    real_pread = (pread_fn)dlsym(RTLD_NEXT, "pread");
    real_pwrite = (pwrite_fn)dlsym(RTLD_NEXT, "pwrite");
    real_fsync = (fsync_fn)dlsym(RTLD_NEXT, "fsync");
    real_lseek = (lseek_fn)dlsym(RTLD_NEXT, "lseek");
    real_ftruncate = (ftruncate_fn)dlsym(RTLD_NEXT, "ftruncate");

    // Check environment for configuration
    const char* env_dax = getenv("FIO_DAX_DEVICE");
    const char* env_size = getenv("FIO_DAX_SIZE");
    const char* env_enable = getenv("FIO_INTERCEPT_ENABLE");

    if (env_enable && strcmp(env_enable, "1") == 0) {
        intercept_enabled = true;

        if (env_dax) {
            dax_device_path = env_dax;

            // Open and map the global DAX device
            global_dax_fd = real_open(dax_device_path.c_str(), O_RDWR | O_SYNC);
            if (global_dax_fd >= 0) {
                // Get size
                if (env_size) {
                    dax_device_size = std::stoull(env_size);
                } else {
                    struct stat st;
                    if (fstat(global_dax_fd, &st) == 0) {
                        dax_device_size = st.st_size;
                    }
                }

                if (dax_device_size > 0) {
                    global_dax_base = mmap(nullptr, dax_device_size,
                                          PROT_READ | PROT_WRITE,
                                          MAP_SHARED | MAP_SYNC,
                                          global_dax_fd, 0);

                    if (global_dax_base != MAP_FAILED) {
                        fprintf(stderr, "[FIO_INTERCEPT] DAX device mapped: %s (size: %zu)\n",
                               dax_device_path.c_str(), dax_device_size);
                        madvise(global_dax_base, dax_device_size, MADV_HUGEPAGE);
                    } else {
                        fprintf(stderr, "[FIO_INTERCEPT] Failed to map DAX device\n");
                        intercept_enabled = false;
                    }
                }
            }
        }
    }
}

// Cleanup
__attribute__((destructor))
void cleanup_intercept() {
    if (global_dax_base && global_dax_base != MAP_FAILED) {
        munmap(global_dax_base, dax_device_size);
    }
    if (global_dax_fd >= 0) {
        real_close(global_dax_fd);
    }
}

// Check if path should be intercepted
bool should_intercept(const char* path) {
    if (!intercept_enabled || !path) return false;

    // Check if it's a test file that fio would create
    const char* patterns[] = {
        "/test.", ".fio.", "fio-", "/fio/", nullptr
    };

    for (const char** p = patterns; *p; p++) {
        if (strstr(path, *p)) {
            return true;
        }
    }

    // Check environment for specific patterns
    const char* env_pattern = getenv("FIO_INTERCEPT_PATTERN");
    if (env_pattern && strstr(path, env_pattern)) {
        return true;
    }

    return false;
}

// Allocate space in DAX device
size_t allocate_dax_space(size_t requested_size) {
    static std::atomic<size_t> allocated{0};

    size_t offset = allocated.fetch_add(requested_size);
    if (offset + requested_size > dax_device_size) {
        // Wrap around or fail
        allocated.store(0);
        return 0;
    }
    return offset;
}

} // anonymous namespace

// Intercepted functions
extern "C" {

int open(const char* pathname, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, mode_t);
        va_end(args);
    }

    if (should_intercept(pathname)) {
        std::lock_guard<std::mutex> lock(mappings_mutex);

        int fake_fd = fake_fd_counter.fetch_add(1);

        // Allocate space in DAX device (default 1GB per file)
        size_t file_size = 1ULL << 30; // 1GB
        const char* env_file_size = getenv("FIO_FILE_SIZE");
        if (env_file_size) {
            file_size = std::stoull(env_file_size);
        }

        size_t offset = allocate_dax_space(file_size);

        DAXMapping mapping;
        mapping.base = static_cast<char*>(global_dax_base) + offset;
        mapping.size = file_size;
        mapping.current_offset = 0;
        mapping.path = pathname;
        mapping.real_fd = -1; // No real file

        dax_mappings[fake_fd] = mapping;

        if (getenv("FIO_DEBUG")) {
            fprintf(stderr, "[INTERCEPT] open(%s) -> DAX fd=%d\n", pathname, fake_fd);
        }

        return fake_fd;
    }

    return real_open(pathname, flags, mode);
}

int close(int fd) {
    {
        std::lock_guard<std::mutex> lock(mappings_mutex);
        auto it = dax_mappings.find(fd);
        if (it != dax_mappings.end()) {
            if (getenv("FIO_DEBUG")) {
                fprintf(stderr, "[INTERCEPT] close(DAX fd=%d)\n", fd);
            }
            dax_mappings.erase(it);
            return 0;
        }
    }
    return real_close(fd);
}

ssize_t read(int fd, void* buf, size_t count) {
    {
        std::lock_guard<std::mutex> lock(mappings_mutex);
        auto it = dax_mappings.find(fd);
        if (it != dax_mappings.end()) {
            DAXMapping& mapping = it->second;

            size_t to_read = count;
            if (mapping.current_offset + to_read > mapping.size) {
                to_read = mapping.size - mapping.current_offset;
            }

            if (to_read > 0) {
                memcpy(buf, static_cast<char*>(mapping.base) + mapping.current_offset, to_read);
                mapping.current_offset += to_read;
            }

            if (getenv("FIO_DEBUG")) {
                fprintf(stderr, "[INTERCEPT] read(DAX fd=%d, size=%zu) -> %zu\n",
                       fd, count, to_read);
            }

            return to_read;
        }
    }
    return real_read(fd, buf, count);
}

ssize_t write(int fd, const void* buf, size_t count) {
    {
        std::lock_guard<std::mutex> lock(mappings_mutex);
        auto it = dax_mappings.find(fd);
        if (it != dax_mappings.end()) {
            DAXMapping& mapping = it->second;

            size_t to_write = count;
            if (mapping.current_offset + to_write > mapping.size) {
                to_write = mapping.size - mapping.current_offset;
            }

            if (to_write > 0) {
                void* dest = static_cast<char*>(mapping.base) + mapping.current_offset;
                memcpy(dest, buf, to_write);

                // Flush for persistence
                for (size_t i = 0; i < to_write; i += 64) {
                    _mm_clflushopt(static_cast<char*>(dest) + i);
                }
                _mm_sfence();

                mapping.current_offset += to_write;
            }

            if (getenv("FIO_DEBUG")) {
                fprintf(stderr, "[INTERCEPT] write(DAX fd=%d, size=%zu) -> %zu\n",
                       fd, count, to_write);
            }

            return to_write;
        }
    }
    return real_write(fd, buf, count);
}

ssize_t pread(int fd, void* buf, size_t count, off_t offset) {
    {
        std::lock_guard<std::mutex> lock(mappings_mutex);
        auto it = dax_mappings.find(fd);
        if (it != dax_mappings.end()) {
            DAXMapping& mapping = it->second;

            size_t to_read = count;
            if (offset + to_read > mapping.size) {
                to_read = mapping.size - offset;
            }

            if (to_read > 0 && offset >= 0) {
                memcpy(buf, static_cast<char*>(mapping.base) + offset, to_read);
            }

            if (getenv("FIO_DEBUG")) {
                fprintf(stderr, "[INTERCEPT] pread(DAX fd=%d, off=%ld, size=%zu) -> %zu\n",
                       fd, offset, count, to_read);
            }

            return to_read;
        }
    }
    return real_pread(fd, buf, count, offset);
}

ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset) {
    {
        std::lock_guard<std::mutex> lock(mappings_mutex);
        auto it = dax_mappings.find(fd);
        if (it != dax_mappings.end()) {
            DAXMapping& mapping = it->second;

            size_t to_write = count;
            if (offset + to_write > mapping.size) {
                to_write = mapping.size - offset;
            }

            if (to_write > 0 && offset >= 0) {
                void* dest = static_cast<char*>(mapping.base) + offset;
                memcpy(dest, buf, to_write);

                // Flush for persistence
                for (size_t i = 0; i < to_write; i += 64) {
                    _mm_clflushopt(static_cast<char*>(dest) + i);
                }
                _mm_sfence();
            }

            if (getenv("FIO_DEBUG")) {
                fprintf(stderr, "[INTERCEPT] pwrite(DAX fd=%d, off=%ld, size=%zu) -> %zu\n",
                       fd, offset, count, to_write);
            }

            return to_write;
        }
    }
    return real_pwrite(fd, buf, count, offset);
}

int fsync(int fd) {
    {
        std::lock_guard<std::mutex> lock(mappings_mutex);
        auto it = dax_mappings.find(fd);
        if (it != dax_mappings.end()) {
            // DAX memory is already persistent after clflush
            if (getenv("FIO_DEBUG")) {
                fprintf(stderr, "[INTERCEPT] fsync(DAX fd=%d) -> 0\n", fd);
            }
            return 0;
        }
    }
    return real_fsync(fd);
}

off_t lseek(int fd, off_t offset, int whence) {
    {
        std::lock_guard<std::mutex> lock(mappings_mutex);
        auto it = dax_mappings.find(fd);
        if (it != dax_mappings.end()) {
            DAXMapping& mapping = it->second;

            off_t new_offset = 0;
            switch (whence) {
                case SEEK_SET:
                    new_offset = offset;
                    break;
                case SEEK_CUR:
                    new_offset = mapping.current_offset + offset;
                    break;
                case SEEK_END:
                    new_offset = mapping.size + offset;
                    break;
                default:
                    errno = EINVAL;
                    return -1;
            }

            if (new_offset < 0) {
                errno = EINVAL;
                return -1;
            }

            mapping.current_offset = new_offset;

            if (getenv("FIO_DEBUG")) {
                fprintf(stderr, "[INTERCEPT] lseek(DAX fd=%d, off=%ld, whence=%d) -> %ld\n",
                       fd, offset, whence, new_offset);
            }

            return new_offset;
        }
    }
    return real_lseek(fd, offset, whence);
}

int ftruncate(int fd, off_t length) {
    {
        std::lock_guard<std::mutex> lock(mappings_mutex);
        auto it = dax_mappings.find(fd);
        if (it != dax_mappings.end()) {
            // DAX mapping size is fixed, just return success
            if (getenv("FIO_DEBUG")) {
                fprintf(stderr, "[INTERCEPT] ftruncate(DAX fd=%d, len=%ld) -> 0\n",
                       fd, length);
            }
            return 0;
        }
    }
    return real_ftruncate(fd, length);
}

} // extern "C"