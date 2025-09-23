// Minimal io_uring interception shim that implements a userspace ring
// and completes operations using memory/DAX paths, waiting via MONITOR/MWAIT.
// Intended for LD_PRELOAD ahead of liburing. For non-intercepted FDs, falls back
// to libc pread/pwrite. Controlled by IOURING_INTERCEPT_ENABLE=1 and
// FIO_DAX_DEVICE/FIO_DAX_SIZE.

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <immintrin.h>
#include <condition_variable>
#include <deque>

#include "../include/cxl_mwait.hpp"

// Use cxl primitives for MONITOR/MWAIT
namespace cxl = cxl_ssd;
using cxl::primitives::monitor;
using cxl::primitives::mwait;

extern "C" {

// Forward declarations to match liburing ABI
struct io_uring {};
struct io_uring_sqe {
    uint8_t opcode;
    uint8_t flags;
    uint16_t ioprio;
    int32_t fd;
    uint64_t off;
    uint64_t addr; // userspace buffer pointer
    uint32_t len;
    uint32_t rw_flags;
    uint64_t user_data;
};
struct io_uring_cqe {
    uint64_t user_data;
    int32_t res;
    uint32_t flags;
};

// Opcodes we minimally support
static constexpr uint8_t IORING_OP_READV = 1;   // treat as READ
static constexpr uint8_t IORING_OP_WRITEV = 2;  // treat as WRITE
static constexpr uint8_t IORING_OP_READ = 18;
static constexpr uint8_t IORING_OP_WRITE = 19;

// Real libc fallbacks
using pread_fn = ssize_t(*)(int, void*, size_t, off_t);
using pwrite_fn = ssize_t(*)(int, const void*, size_t, off_t);
static pread_fn real_pread = nullptr;
static pwrite_fn real_pwrite = nullptr;

// Intercepted open/close to hand out fake FDs that map into a DAX region
using open_fn = int(*)(const char*, int, ...);
using close_fn = int(*)(int);
static open_fn real_open = nullptr;
static close_fn real_close = nullptr;

struct DAXMapping {
    void* base{nullptr};
    size_t size{0};
    off_t current_offset{0};
    std::string path;
};

static std::map<int, DAXMapping> g_dax_fds;
static std::mutex g_dax_mu;
static std::atomic<int> g_fake_fd{20000};

// Global DAX device info
static bool g_intercept_enabled = false;
static std::string g_dax_device_path;
static size_t g_dax_device_size = 0;
static void* g_dax_base = nullptr;
static int g_dax_fd = -1;

// Userspace ring context keyed by the app-provided ring pointer value
struct RingCtx {
    unsigned capacity;
    std::mutex mu;
    std::vector<io_uring_sqe*> pending; // collected SQEs since last submit
    std::vector<io_uring_cqe> cqes;     // completion queue

    // Worker queue for async processing
    std::mutex wq_mu;
    std::condition_variable wq_cv;
    std::deque<io_uring_sqe*> wq;
    std::atomic<bool> stop{false};
    pthread_t worker_thr{};

    // Wait primitive: a 64B aligned counter to monitor
    alignas(64) std::atomic<uint32_t> cqe_tail{0};
};

static std::map<const io_uring*, RingCtx*> g_rings;
static std::mutex g_rings_mu;

// Utility: check whether fd is our fake DAX fd
static inline bool is_dax_fd(int fd) {
    std::lock_guard<std::mutex> lk(g_dax_mu);
    return g_dax_fds.find(fd) != g_dax_fds.end();
}

// Utility: copy to/from DAX mapping
static ssize_t dax_pread(int fd, void* buf, size_t count, off_t offset) {
    std::lock_guard<std::mutex> lk(g_dax_mu);
    auto it = g_dax_fds.find(fd);
    if (it == g_dax_fds.end()) return -1;
    auto& m = it->second;
    if (offset < 0 || (size_t)offset >= m.size) return 0;
    size_t to_read = count;
    if (offset + (off_t)to_read > (off_t)m.size) to_read = m.size - offset;
    memcpy(buf, static_cast<char*>(m.base) + offset, to_read);
    return (ssize_t)to_read;
}
static ssize_t dax_pwrite(int fd, const void* buf, size_t count, off_t offset) {
    std::lock_guard<std::mutex> lk(g_dax_mu);
    auto it = g_dax_fds.find(fd);
    if (it == g_dax_fds.end()) return -1;
    auto& m = it->second;
    if (offset < 0 || (size_t)offset >= m.size) return 0;
    size_t to_write = count;
    if (offset + (off_t)to_write > (off_t)m.size) to_write = m.size - offset;
    void* dest = static_cast<char*>(m.base) + offset;
    memcpy(dest, buf, to_write);
    // persist via CLFLUSHOPT
    for (size_t i = 0; i < to_write; i += 64) {
        _mm_clflushopt(static_cast<char*>(dest) + i);
    }
    _mm_sfence();
    return (ssize_t)to_write;
}

// Environment config and real function pointers
__attribute__((constructor)) static void iouring_intercept_init() {
    real_open = (open_fn)dlsym(RTLD_NEXT, "open");
    real_close = (close_fn)dlsym(RTLD_NEXT, "close");
    real_pread = (pread_fn)dlsym(RTLD_NEXT, "pread");
    real_pwrite = (pwrite_fn)dlsym(RTLD_NEXT, "pwrite");

    const char* env_enable = getenv("IOURING_INTERCEPT_ENABLE");
    if (env_enable && strcmp(env_enable, "1") == 0) {
        g_intercept_enabled = true;
        const char* env_dax = getenv("FIO_DAX_DEVICE");
        const char* env_size = getenv("FIO_DAX_SIZE");
        if (env_dax) g_dax_device_path = env_dax;
        if (env_size) g_dax_device_size = strtoull(env_size, nullptr, 0);

        if (!g_dax_device_path.empty()) {
            g_dax_fd = real_open(g_dax_device_path.c_str(), O_RDWR | O_SYNC);
            if (g_dax_fd >= 0) {
                if (g_dax_device_size == 0) {
                    struct stat st{};
                    if (fstat(g_dax_fd, &st) == 0) g_dax_device_size = st.st_size;
                }
                if (g_dax_device_size) {
                    g_dax_base = mmap(nullptr, g_dax_device_size,
                                      PROT_READ | PROT_WRITE, MAP_SHARED | MAP_SYNC,
                                      g_dax_fd, 0);
                    if (g_dax_base == MAP_FAILED) {
                        g_dax_base = nullptr;
                        g_intercept_enabled = false;
                    }
                }
            }
        }
    }
}

__attribute__((destructor)) static void iouring_intercept_fini() {
    if (g_dax_base && g_dax_base != MAP_FAILED) munmap(g_dax_base, g_dax_device_size);
    if (g_dax_fd >= 0) real_close(g_dax_fd);
}

// Simple path matching to decide whether to hand out fake fds
static bool should_intercept_path(const char* path) {
    if (!g_intercept_enabled || !path) return false;
    const char* pat = getenv("FIO_INTERCEPT_PATTERN");
    if (pat && strstr(path, pat)) return true;
    const char* defaults[] = {"/test.", ".fio.", "fio-", "/fio/", nullptr};
    for (const char** p = defaults; *p; ++p) if (strstr(path, *p)) return true;
    return false;
}

int open(const char* pathname, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, mode_t); va_end(ap); }
    if (should_intercept_path(pathname)) {
        if (!g_dax_base) return -1;
        int fd = g_fake_fd.fetch_add(1);
        size_t file_size = 1ULL << 30; // default 1GB chunk
        const char* env_file_size = getenv("FIO_FILE_SIZE");
        if (env_file_size) file_size = strtoull(env_file_size, nullptr, 0);
        static std::atomic<size_t> allocated{0};
        size_t offset = allocated.fetch_add(file_size) % g_dax_device_size;
        std::lock_guard<std::mutex> lk(g_dax_mu);
        g_dax_fds[fd] = DAXMapping{static_cast<char*>(g_dax_base) + offset, file_size, 0, pathname};
        return fd;
    }
    return real_open ? real_open(pathname, flags, mode) : -1;
}

int close(int fd) {
    {
        std::lock_guard<std::mutex> lk(g_dax_mu);
        auto it = g_dax_fds.find(fd);
        if (it != g_dax_fds.end()) { g_dax_fds.erase(it); return 0; }
    }
    return real_close ? real_close(fd) : -1;
}

// io_uring minimal API
int io_uring_queue_init(unsigned entries, struct io_uring* ring, unsigned /*flags*/) {
    std::lock_guard<std::mutex> lk(g_rings_mu);
    if (g_rings.count(ring)) return 0;
    auto* ctx = new RingCtx();
    ctx->capacity = entries ? entries : 64;
    g_rings[ring] = ctx;

    // Launch worker thread to process SQEs asynchronously
    auto worker_fn = [](void* arg) -> void* {
        RingCtx* c = static_cast<RingCtx*>(arg);
        for (;;) {
            io_uring_sqe* sqe = nullptr;
            {
                std::unique_lock<std::mutex> ul(c->wq_mu);
                c->wq_cv.wait(ul, [&]{ return c->stop.load(std::memory_order_acquire) || !c->wq.empty(); });
                if (c->stop.load(std::memory_order_acquire) && c->wq.empty()) break;
                sqe = c->wq.front();
                c->wq.pop_front();
            }

            int fd = sqe->fd; ssize_t res = -EINVAL;
            if (sqe->opcode == IORING_OP_READ || sqe->opcode == IORING_OP_READV) {
                if (is_dax_fd(fd)) res = dax_pread(fd, (void*)sqe->addr, sqe->len, sqe->off);
                else if (real_pread) res = real_pread(fd, (void*)sqe->addr, sqe->len, sqe->off);
            } else if (sqe->opcode == IORING_OP_WRITE || sqe->opcode == IORING_OP_WRITEV) {
                if (is_dax_fd(fd)) res = dax_pwrite(fd, (const void*)sqe->addr, sqe->len, sqe->off);
                else if (real_pwrite) res = real_pwrite(fd, (const void*)sqe->addr, sqe->len, sqe->off);
            } else {
                res = -EOPNOTSUPP;
            }

            io_uring_cqe cqe{}; cqe.user_data = sqe->user_data; cqe.res = (int32_t)res; cqe.flags = 0;
            {
                std::lock_guard<std::mutex> lk2(c->mu);
                c->cqes.push_back(cqe);
                c->cqe_tail.fetch_add(1, std::memory_order_release);
            }
            free(sqe);
        }
        return nullptr;
    };
    pthread_create(&ctx->worker_thr, nullptr, worker_fn, ctx);
    return 0;
}

void io_uring_queue_exit(struct io_uring* ring) {
    std::lock_guard<std::mutex> lk(g_rings_mu);
    auto it = g_rings.find(ring);
    if (it != g_rings.end()) {
        RingCtx* ctx = it->second;
        ctx->stop.store(true, std::memory_order_release);
        ctx->wq_cv.notify_all();
        if (ctx->worker_thr) pthread_join(ctx->worker_thr, nullptr);
        delete ctx;
        g_rings.erase(it);
    }
}

struct io_uring_sqe* io_uring_get_sqe(struct io_uring* ring) {
    std::lock_guard<std::mutex> lk(g_rings_mu);
    auto it = g_rings.find(ring);
    if (it == g_rings.end()) return nullptr;
    // Allocate a fresh SQE owned by the ring ctx until submit
    auto* sqe = (io_uring_sqe*)aligned_alloc(64, sizeof(io_uring_sqe));
    memset(sqe, 0, sizeof(io_uring_sqe));
    it->second->pending.push_back(sqe);
    return sqe;
}

// Prep helpers
void io_uring_prep_read(struct io_uring_sqe* sqe, int fd, void* buf, unsigned nbytes, off_t offset) {
    if (!sqe) return;
    sqe->opcode = IORING_OP_READ;
    sqe->fd = fd;
    sqe->addr = (uint64_t)buf;
    sqe->len = nbytes;
    sqe->off = offset;
}
void io_uring_prep_write(struct io_uring_sqe* sqe, int fd, const void* buf, unsigned nbytes, off_t offset) {
    if (!sqe) return;
    sqe->opcode = IORING_OP_WRITE;
    sqe->fd = fd;
    sqe->addr = (uint64_t)buf;
    sqe->len = nbytes;
    sqe->off = offset;
}

// Submit all pending SQEs; return count submitted
int io_uring_submit(struct io_uring* ring) {
    RingCtx* ctx = nullptr; {
        std::lock_guard<std::mutex> lk(g_rings_mu);
        auto it = g_rings.find(ring); if (it == g_rings.end()) return -EINVAL; ctx = it->second;
    }

    std::vector<io_uring_sqe*> to_process;
    {
        std::lock_guard<std::mutex> lk(ctx->mu);
        to_process.swap(ctx->pending);
    }

    int submitted = 0;
    if (!to_process.empty()) {
        {
            std::lock_guard<std::mutex> ul(ctx->wq_mu);
            for (auto* sqe : to_process) ctx->wq.push_back(sqe);
        }
        ctx->wq_cv.notify_all();
        submitted = (int)to_process.size();
    }
    return submitted;
}

int io_uring_peek_cqe(struct io_uring* ring, struct io_uring_cqe** cqe_ptr) {
    RingCtx* ctx = nullptr; { std::lock_guard<std::mutex> lk(g_rings_mu); auto it=g_rings.find(ring); if (it==g_rings.end()) return -EINVAL; ctx=it->second; }
    std::lock_guard<std::mutex> lk(ctx->mu);
    if (ctx->cqes.empty()) { *cqe_ptr = nullptr; return -EAGAIN; }
    *cqe_ptr = &ctx->cqes.front(); return 0;
}

int io_uring_wait_cqe(struct io_uring* ring, struct io_uring_cqe** cqe_ptr) {
    RingCtx* ctx = nullptr; { std::lock_guard<std::mutex> lk(g_rings_mu); auto it=g_rings.find(ring); if (it==g_rings.end()) return -EINVAL; ctx=it->second; }
    // Fast path
    {
        std::lock_guard<std::mutex> lk(ctx->mu);
        if (!ctx->cqes.empty()) { *cqe_ptr = &ctx->cqes.front(); return 0; }
    }

    // Monitor the cqe_tail cache line and mwait until it changes
    uint32_t before = ctx->cqe_tail.load(std::memory_order_acquire);
    monitor((void*)&ctx->cqe_tail, 0, 0);
    for (;;) {
        // mwait extensions=0, hint=C1 (0x01)
        mwait(0, (uint32_t)cxl::MWaitHint::C1);
        uint32_t now = ctx->cqe_tail.load(std::memory_order_acquire);
        if (now != before) break;
        // Re-arm monitor if spurious wake
        monitor((void*)&ctx->cqe_tail, 0, 0);
    }
    // Now there should be at least one cqe
    std::lock_guard<std::mutex> lk2(ctx->mu);
    if (ctx->cqes.empty()) return -EAGAIN; // should not happen
    *cqe_ptr = &ctx->cqes.front();
    return 0;
}

void io_uring_cqe_seen(struct io_uring* ring, struct io_uring_cqe* cqe) {
    (void)cqe;
    RingCtx* ctx = nullptr; { std::lock_guard<std::mutex> lk(g_rings_mu); auto it=g_rings.find(ring); if (it==g_rings.end()) return; ctx=it->second; }
    std::lock_guard<std::mutex> lk(ctx->mu);
    if (!ctx->cqes.empty()) ctx->cqes.erase(ctx->cqes.begin());
}

// Convenience: submit and wait for at least one cqe
int io_uring_submit_and_wait(struct io_uring* ring, unsigned wait_nr) {
    (void)wait_nr; // treat as 1
    int sub = io_uring_submit(ring);
    struct io_uring_cqe* cqe = nullptr;
    (void)io_uring_wait_cqe(ring, &cqe);
    return sub;
}

} // extern "C"
