#include "../include/wasm_scheduler.hpp"
#include <chrono>
#include <cstring>

namespace cxl {

// A stub runtime that simulates a wasm module by invoking a native callback
// stored in the module path (for demo). Replace with a real Wasm engine
// (e.g., Wasmtime) by implementing IWasmRuntime.
class StubRuntime : public IWasmRuntime {
public:
    bool load_module(const std::string& path) override {
        module_path_ = path; return true;
    }
    bool instantiate() override { progress_ = 0; return true; }
    bool call_export(const std::string& name, const std::vector<uint64_t>& args) override {
        (void)name; (void)args; // simulate long-running work
        // Increment progress in a loop to emulate compute; cooperative
        for (int i = 0; i < 100000; ++i) {
            progress_++;
            if ((progress_ % 4096) == 0) std::this_thread::yield();
        }
        return true;
    }
    std::vector<uint8_t> snapshot() override {
        std::vector<uint8_t> s(sizeof(progress_));
        std::memcpy(s.data(), &progress_, sizeof(progress_));
        return s;
    }
    bool restore(const std::vector<uint8_t>& s) override {
        if (s.size() != sizeof(progress_)) return false;
        std::memcpy(&progress_, s.data(), sizeof(progress_));
        return true;
    }
private:
    std::string module_path_;
    uint64_t progress_ = 0;
};

std::unique_ptr<IWasmRuntime> make_wasm_runtime(TargetArch /*target*/) {
    // In a real deployment, select an engine build suitable for the host arch.
    return std::make_unique<StubRuntime>();
}

WasmTask::WasmTask(const WasmTaskDesc& d, TargetArch t) : desc_(d), target_(t) {
    rt_ = make_wasm_runtime(t);
    rt_->load_module(desc_.module_path);
    rt_->instantiate();
}

WasmTask::~WasmTask() { stop(); }

void WasmTask::start() {
    if (running_.exchange(true)) return;
    thr_ = std::thread([this](){
        rt_->call_export(desc_.entry, desc_.args);
        running_.store(false);
        std::unique_lock<std::mutex> ul(mu_);
        cv_.notify_all();
    });
}

std::vector<uint8_t> WasmTask::checkpoint() {
    // Cooperative checkpoint of current state
    return rt_->snapshot();
}

bool WasmTask::restore_and_resume(const std::vector<uint8_t>& state) {
    if (!rt_->restore(state)) return false;
    start();
    return true;
}

void WasmTask::stop() {
    if (!running_.load()) {
        if (thr_.joinable()) thr_.join();
        return;
    }
    // Cooperative stop: just wait for it to finish (demo). Real engines support interrupts.
    std::unique_lock<std::mutex> ul(mu_);
    cv_.wait_for(ul, std::chrono::milliseconds(10));
    if (thr_.joinable()) thr_.join();
    running_.store(false);
}

WasmScheduler::~WasmScheduler() { shutdown(); }

int WasmScheduler::launch(const WasmTaskDesc& desc, TargetArch target) {
    std::lock_guard<std::mutex> lk(mu_);
    int id = next_id_++;
    auto task = std::make_unique<WasmTask>(desc, target);
    task->start();
    tasks_.push_back(Entry{id, std::move(task)});
    return id;
}

bool WasmScheduler::migrate(int task_id, TargetArch new_target) {
    std::unique_ptr<WasmTask> old_task;
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto it = tasks_.begin(); it != tasks_.end(); ++it) {
            if (it->id == task_id) {
                // Snapshot state
                auto state = it->task->checkpoint();
                it->task->stop();
                old_task = std::move(it->task);
                // Create new task on new arch
                auto t = std::make_unique<WasmTask>(old_task->desc(), new_target);
                if (!t->restore_and_resume(state)) return false;
                it->task = std::move(t);
                return true;
            }
        }
    }
    return false;
}

void WasmScheduler::shutdown() {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& e : tasks_) {
        if (e.task) e.task->stop();
    }
    tasks_.clear();
}

} // namespace cxl

