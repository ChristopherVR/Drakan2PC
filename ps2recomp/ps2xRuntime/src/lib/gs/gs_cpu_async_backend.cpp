#include "gs_cpu_async_backend.h"
#include "runtime/gs/gs_cpu_backend.h"
#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <functional>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#if !defined(__EMSCRIPTEN__) && (defined(__SSE__) || defined(_M_X64))
#include <xmmintrin.h>
#endif

namespace {
unsigned floatControl() {
#if !defined(__EMSCRIPTEN__) && (defined(__SSE__) || defined(_M_X64))
    return _mm_getcsr();
#else
    return 0;
#endif
}
class FloatScope {
    unsigned saved_;

public:
    explicit FloatScope(unsigned control) : saved_(floatControl()) {
#if !defined(__EMSCRIPTEN__) && (defined(__SSE__) || defined(_M_X64))
        _mm_setcsr(control);
#else
        (void)control;
#endif
    }
    ~FloatScope() {
#if !defined(__EMSCRIPTEN__) && (defined(__SSE__) || defined(_M_X64))
        _mm_setcsr(saved_);
#endif
    }
};
struct ClutCommand {
    GSTex0Reg tex0;
    GSTexClutReg clut;
};
struct UploadCommand {
    std::vector<uint8_t> bytes;
};
struct WriteCommand {
    uint32_t psm, base, width, x, y, value;
};
struct FlushCommand {};
struct TextureFlushCommand {};
struct TaskCommand {
    std::function<void(GSCpuBackend&, const std::exception_ptr&)> run;
};
using Payload = std::variant<GSPrimitiveBatch, ClutCommand, GSTransferCommand, UploadCommand, WriteCommand,
                             FlushCommand, TextureFlushCommand, TaskCommand>;
struct Command {
    Payload payload;
    unsigned rounding;
};
} // namespace

struct GSAsyncCpuBackend::Impl {
    static constexpr size_t kCommandLimit = 8192;
    static constexpr size_t kUploadLimit = 8 * 1024 * 1024;
    GSCpuBackend backend;
    std::mutex mutex;
    std::condition_variable ready, space;
    std::vector<Command> commands;
    std::thread worker;
    std::exception_ptr failure;
    size_t uploadBytes = 0;
    bool stopping = false;

    template <class T>
    void enqueue(T&& payload, size_t bytes = 0) {
        const unsigned rounding = floatControl();
        std::unique_lock lock(mutex);
        if (!worker.joinable()) {
            commands.reserve(kCommandLimit);
            worker = std::thread([this] { run(); });
        }
        space.wait(lock, [&] {
            return stopping || (commands.size() < kCommandLimit &&
                                (commands.empty() || bytes <= kUploadLimit - std::min(uploadBytes, kUploadLimit)));
        });
        if (stopping)
            throw std::runtime_error("CPU renderer queue is stopped");
        const bool wake = commands.empty();
        commands.push_back({Payload{std::forward<T>(payload)}, rounding});
        uploadBytes += bytes;
        lock.unlock();
        if (wake)
            ready.notify_one();
    }
    template <class F>
    auto sync(F&& action) {
        using Result = std::invoke_result_t<F, GSCpuBackend&>;
        auto promise = std::make_shared<std::promise<Result>>();
        auto result = promise->get_future();
        enqueue(TaskCommand{
            [promise, action = std::forward<F>(action)](GSCpuBackend& target, const std::exception_ptr& error) mutable {
                try {
                    if (error)
                        std::rethrow_exception(error);
                    if constexpr (std::is_void_v<Result>) {
                        action(target);
                        promise->set_value();
                    } else {
                        promise->set_value(action(target));
                    }
                } catch (...) {
                    promise->set_exception(std::current_exception());
                }
            }});
        return result.get();
    }
    void run() {
        std::vector<Command> batch;
        batch.reserve(kCommandLimit);
        for (;;) {
            {
                std::unique_lock lock(mutex);
                ready.wait(lock, [&] { return stopping || !commands.empty(); });
                if (commands.empty() && stopping)
                    return;
                batch.swap(commands);
                uploadBytes = 0;
            }
            space.notify_all();
            for (auto& command : batch) {
                FloatScope rounding(command.rounding);
                if (auto* task = std::get_if<TaskCommand>(&command.payload)) {
                    task->run(backend, failure);
                    continue;
                }
                if (failure)
                    continue;
                try {
                    std::visit(
                        [&](auto& payload) {
                            using T = std::decay_t<decltype(payload)>;
                            if constexpr (std::is_same_v<T, GSPrimitiveBatch>)
                                backend.SubmitUnlocked(payload);
                            else if constexpr (std::is_same_v<T, ClutCommand>)
                                backend.LoadClut(payload.tex0, payload.clut);
                            else if constexpr (std::is_same_v<T, GSTransferCommand>)
                                backend.BeginTransfer(payload);
                            else if constexpr (std::is_same_v<T, UploadCommand>)
                                backend.UploadImage(payload.bytes.data(), static_cast<uint32_t>(payload.bytes.size()));
                            else if constexpr (std::is_same_v<T, WriteCommand>)
                                backend.WriteVram(payload.psm, payload.base, payload.width, payload.x, payload.y,
                                                  payload.value);
                            else if constexpr (std::is_same_v<T, FlushCommand>)
                                backend.Flush();
                            else if constexpr (std::is_same_v<T, TextureFlushCommand>)
                                backend.TextureFlush();
                        },
                        command.payload);
                } catch (...) {
                    failure = std::current_exception();
                }
            }
            batch.clear();
        }
    }
    ~Impl() {
        if (!worker.joinable())
            return;
        try {
            sync([](GSCpuBackend& target) { target.Flush(); });
        } catch (...) {
        }
        {
            std::lock_guard lock(mutex);
            stopping = true;
        }
        ready.notify_one();
        space.notify_all();
        worker.join();
    }
};

GSAsyncCpuBackend::GSAsyncCpuBackend() : m_impl(std::make_unique<Impl>()) {}
GSAsyncCpuBackend::~GSAsyncCpuBackend() = default;
void GSAsyncCpuBackend::Initialize(uint8_t* vram, uint32_t size) {
    m_impl->sync([=](GSCpuBackend& target) { target.Initialize(vram, size); });
}
void GSAsyncCpuBackend::Reset() {
    m_impl->sync([](GSCpuBackend& target) { target.Reset(); });
}
void GSAsyncCpuBackend::Submit(const GSPrimitiveBatch& batch) {
    m_impl->enqueue(batch);
}
void GSAsyncCpuBackend::LoadClut(const GSTex0Reg& tex0, const GSTexClutReg& clut) {
    m_impl->enqueue(ClutCommand{tex0, clut});
}
void GSAsyncCpuBackend::BeginTransfer(const GSTransferCommand& command) {
    m_impl->enqueue(command);
}
void GSAsyncCpuBackend::UploadImage(const uint8_t* data, uint32_t size) {
    if (data && size)
        m_impl->enqueue(UploadCommand{std::vector<uint8_t>(data, data + size)}, size);
}
void GSAsyncCpuBackend::Flush() {
    m_impl->enqueue(FlushCommand{});
}
void GSAsyncCpuBackend::TextureFlush() {
    m_impl->enqueue(TextureFlushCommand{});
}
void GSAsyncCpuBackend::Sync(GSSyncReason reason) {
    m_impl->sync([=](GSCpuBackend& target) { target.Sync(reason); });
}
PresentationFrame GSAsyncCpuBackend::Present(const GSPresentationRequest& request) {
    return m_impl->sync([&](GSCpuBackend& target) {
        // This private backend is accessed only by the ordered renderer worker.
        // Complete its tiles, then convert directly while subsequent commands wait.
        target.Flush();
        return target.PresentFromLocalMemory(request);
    });
}
bool GSAsyncCpuBackend::ClearFramebuffer(const GSContext& context, uint32_t rgba) {
    return m_impl->sync([&](GSCpuBackend& target) { return target.ClearFramebuffer(context, rgba); });
}
uint32_t GSAsyncCpuBackend::ConsumeLocalToHostBytes(uint8_t* dst, uint32_t size) {
    return m_impl->sync([=](GSCpuBackend& target) { return target.ConsumeLocalToHostBytes(dst, size); });
}
uint32_t GSAsyncCpuBackend::ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const {
    return m_impl->sync([=](GSCpuBackend& target) { return target.ReadVram(psm, base, bw, x, y); });
}
void GSAsyncCpuBackend::WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value) {
    m_impl->enqueue(WriteCommand{psm, base, bw, x, y, value});
}
void GSAsyncCpuBackend::SnapshotVram(std::vector<uint8_t>& out) const {
    m_impl->sync([&](GSCpuBackend& target) { target.SnapshotVram(out); });
}
GSTransferSnapshot GSAsyncCpuBackend::GetTransferSnapshot() const {
    return m_impl->sync([](GSCpuBackend& target) { return target.GetTransferSnapshot(); });
}
std::unique_ptr<GSRasterBackend> makeCpuRasterBackend() {
#if defined(PS2X_CPU_ASYNC_DEFAULT)
    bool enabled = true;
#else
    bool enabled = false;
#endif
    if (const char* value = std::getenv("PS2X_CPU_ASYNC"))
        enabled = std::strtol(value, nullptr, 10) != 0;
    if (enabled && std::thread::hardware_concurrency() > 1)
        return std::make_unique<GSAsyncCpuBackend>();
    return std::make_unique<GSCpuBackend>();
}
