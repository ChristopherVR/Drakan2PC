// gsr::rhi proxy: the device lives on a dedicated GPU thread; every call is recorded (arguments copied) into ordered
// batches that thread replays. See rhi.h, "Threads".
//
// Objects created through the proxy are placeholders whose real object is made by the GPU thread when it reaches the
// creating command; later commands find it through a shared slot, and a placeholder's destructor queues the release so
// the real object always dies on the GPU thread. Calls that return a result (readTexture, createProgram,
// configureSurface) send the pending batch and wait for the answer. In the browser the GPU thread is a pthread that
// owns the canvas (an OffscreenCanvas) and never blocks: batches arrive through Emscripten's proxying queue and
// readbacks complete from its event loop.
#include "rhi_open.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#include <emscripten/proxying.h>
#include <emscripten/threading.h>
#include <pthread.h>
#endif

namespace gsr::rhi {
namespace {

using Command = std::function<void(Device&)>;
using Batch = std::vector<Command>;

constexpr size_t kBatchCommands = 1024;  // send a batch once it has this many commands
constexpr size_t kBatchBytes = 8u << 20; // ... or carries this much copied data
constexpr int kMaxBatchesInFlight = 16;  // the recording thread waits beyond this (memory bound)

struct TexSlot {
    TexturePtr real;
};
struct ProgSlot {
    ProgramPtr real;
};
struct BufSlot {
    BufferPtr real;
};

// A one-shot answer from the GPU thread.
struct Waiter {
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    void signal() {
        std::lock_guard lock(m);
        done = true;
        cv.notify_all();
    }
    void wait() {
        std::unique_lock lock(m);
        cv.wait(lock, [&] { return done; });
    }
};

// What the GPU thread learns when its device exists.
struct DeviceFacts {
    bool ok = false;
    std::string error, adapter;
    bool dualSource = false, surface = false;
    uint32_t maxTexture = 8192;
    SurfaceInfo surfaceInfo;
};

// ---- the GPU thread ----
class GpuThread {
public:
    virtual ~GpuThread() = default;
    // Queue a batch; runs in order after every batch queued before it.
    virtual void send(Batch&& batch) = 0;
    std::atomic<int> inFlight{0};     // batches queued and not yet run
    std::atomic<int> pendingReads{0}; // readbacks submitted and not yet completed (native polls for them)
    std::mutex flowM;
    std::condition_variable flowCv;
    void batchDone() {
        inFlight.fetch_sub(1);
        std::lock_guard lock(flowM);
        flowCv.notify_all();
    }
};

void runBatch(Device* device, Batch& batch) {
    for (Command& c : batch)
        if (device)
            c(*device);
    batch.clear(); // captured slots drop their references here, on the GPU thread
}

#if !defined(__EMSCRIPTEN__)
// Native: a std::thread with a batch queue (tests and DRAKAN_RHI_PROXY=1).
class NativeGpuThread final : public GpuThread {
public:
    NativeGpuThread(const OpenDesc& desc, DeviceFacts& facts) {
        Waiter ready;
        thread_ = std::thread([this, desc, &facts, &ready] {
            openDirectAsync(desc, [&](std::unique_ptr<Device> d, const std::string& error) {
                device_ = std::move(d);
                facts.error = error;
            });
            if (device_) {
                facts.ok = true;
                facts.adapter = device_->adapterName();
                facts.dualSource = device_->dualSourceBlending();
                facts.maxTexture = device_->maxTextureSize();
                facts.surface = device_->hasSurface();
                facts.surfaceInfo = device_->surfaceInfo();
            }
            ready.signal();
            if (device_)
                loop();
        });
        ready.wait();
    }
    ~NativeGpuThread() override {
        {
            std::lock_guard lock(m_);
            stop_ = true;
        }
        cv_.notify_all();
        if (thread_.joinable())
            thread_.join();
    }
    void send(Batch&& batch) override {
        {
            std::lock_guard lock(m_);
            queue_.push_back(std::move(batch));
        }
        cv_.notify_all();
    }

private:
    void loop() {
        for (;;) {
            std::deque<Batch> work;
            {
                std::unique_lock lock(m_);
                cv_.wait_for(lock, std::chrono::milliseconds(pendingReads.load() ? 0 : 50),
                             [&] { return stop_ || !queue_.empty(); });
                work.swap(queue_);
                if (stop_ && work.empty())
                    break;
            }
            for (Batch& b : work) {
                runBatch(device_.get(), b);
                batchDone();
            }
            if (pendingReads.load() > 0)
                device_->poll(true); // completes the readbacks those batches submitted
        }
        device_.reset();
    }
    std::thread thread_;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<Batch> queue_;
    bool stop_ = false;
    std::unique_ptr<Device> device_;
};
#else
// Browser: a pthread that owns the canvas (transferred as an OffscreenCanvas) and returns to its event loop between
// batches, so WebGPU promises (adapter, device, buffer maps) resolve.
class WebGpuThread final : public GpuThread {
public:
    WebGpuThread(const OpenDesc& desc, DeviceFacts& facts) : desc_(desc), facts_(facts) {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        if (desc.window.kind == drakan::gpu::NativeWindow::Kind::Canvas && !desc.window.canvasSelector.empty())
            emscripten_pthread_attr_settransferredcanvases(&attr, desc.window.canvasSelector.c_str());
        if (pthread_create(&thread_, &attr, &WebGpuThread::main, this) != 0) {
            facts.error = "cannot start the GPU thread";
            pthread_attr_destroy(&attr);
            return;
        }
        pthread_attr_destroy(&attr);
        ready_.wait();
    }
    ~WebGpuThread() override {
        // Release the device on its thread (after every queued batch); the thread itself stays parked.
        Waiter done;
        emscripten_proxy_async(emscripten_proxy_get_system_queue(), thread_, &WebGpuThread::shutdown,
                               new std::pair<WebGpuThread*, Waiter*>(this, &done));
        done.wait();
    }
    void send(Batch&& batch) override {
        auto* job = new Job{this, std::move(batch)};
        emscripten_proxy_async(emscripten_proxy_get_system_queue(), thread_, &WebGpuThread::run, job);
    }

private:
    struct Job {
        WebGpuThread* self;
        Batch batch;
    };
    static void run(void* p) {
        std::unique_ptr<Job> job(static_cast<Job*>(p));
        runBatch(job->self->device_.get(), job->batch);
        job->self->batchDone();
    }
    static void shutdown(void* p) {
        std::unique_ptr<std::pair<WebGpuThread*, Waiter*>> arg(static_cast<std::pair<WebGpuThread*, Waiter*>*>(p));
        arg->first->device_.reset();
        arg->second->signal();
    }
    static void* main(void* p) {
        auto* self = static_cast<WebGpuThread*>(p);
        emscripten_runtime_keepalive_push(); // live on after returning: batches arrive through the event loop
        openDirectAsync(self->desc_, [self](std::unique_ptr<Device> d, const std::string& error) {
            self->device_ = std::move(d);
            DeviceFacts& f = self->facts_;
            f.error = error;
            if (self->device_) {
                f.ok = true;
                f.adapter = self->device_->adapterName();
                f.dualSource = self->device_->dualSourceBlending();
                f.maxTexture = self->device_->maxTextureSize();
                f.surface = self->device_->hasSurface();
                f.surfaceInfo = self->device_->surfaceInfo();
            }
            self->ready_.signal();
        });
        return nullptr;
    }
    OpenDesc desc_;
    DeviceFacts& facts_;
    Waiter ready_;
    pthread_t thread_{};
    std::unique_ptr<Device> device_;
};
#endif

// ---- the recording side ----
// Shared by the proxy and its placeholders: the GPU thread lives until the last of them is gone, so every release
// still reaches it.
struct Core {
    std::unique_ptr<GpuThread> gpu;
    std::mutex m;
    Batch batch;
    size_t batchBytes = 0;
    ~Core() {
        if (gpu)
            sendBatch();
        gpu.reset(); // waits for the queued batches, then releases the device on its thread
    }

    void push(Command&& c, size_t bytes = 0) {
        bool send;
        {
            std::lock_guard lock(m);
            batch.push_back(std::move(c));
            batchBytes += bytes;
            send = batch.size() >= kBatchCommands || batchBytes >= kBatchBytes;
        }
        if (send)
            sendBatch();
    }
    void sendBatch() {
        Batch out;
        {
            std::lock_guard lock(m);
            if (batch.empty() || !gpu)
                return;
            out.swap(batch);
            batchBytes = 0;
        }
        {
            std::unique_lock lock(gpu->flowM);
            gpu->flowCv.wait(lock, [&] { return gpu->inFlight.load() < kMaxBatchesInFlight; });
        }
        gpu->inFlight.fetch_add(1);
        gpu->send(std::move(out));
    }
    // Run `c` on the GPU thread now and wait for it.
    void call(Command&& c) {
        auto w = std::make_shared<Waiter>();
        push([c = std::move(c), w](Device& d) {
            c(d);
            w->signal();
        });
        sendBatch();
        w->wait();
    }
};

class PTexture final : public Texture {
public:
    PTexture(const TextureDesc& desc, std::shared_ptr<TexSlot> slot, std::shared_ptr<Core> core)
        : slot(std::move(slot)), core_(std::move(core)) {
        desc_ = desc;
    }
    ~PTexture() override {
        core_->push([s = std::move(slot)](Device&) mutable { s.reset(); });
    }
    std::shared_ptr<TexSlot> slot;

private:
    std::shared_ptr<Core> core_;
};

class PProgram final : public Program {
public:
    PProgram(std::shared_ptr<ProgSlot> slot, uint32_t stride, std::shared_ptr<Core> core)
        : slot(std::move(slot)), stride(stride), core_(std::move(core)) {}
    ~PProgram() override {
        core_->push([s = std::move(slot)](Device&) mutable { s.reset(); });
    }
    std::shared_ptr<ProgSlot> slot;
    uint32_t stride;

private:
    std::shared_ptr<Core> core_;
};

class PBuffer final : public Buffer {
public:
    PBuffer(uint64_t bytes, std::shared_ptr<BufSlot> slot, std::shared_ptr<Core> core)
        : slot(std::move(slot)), core_(std::move(core)) {
        size_ = bytes;
    }
    ~PBuffer() override {
        core_->push([s = std::move(slot)](Device&) mutable { s.reset(); });
    }
    std::shared_ptr<BufSlot> slot;

private:
    std::shared_ptr<Core> core_;
};

std::shared_ptr<TexSlot> slotOf(Texture* t) {
    return t ? static_cast<PTexture*>(t)->slot : nullptr;
}
Texture* realOf(const std::shared_ptr<TexSlot>& s) {
    return s ? s->real.get() : nullptr;
}

class ProxyDevice final : public Device {
public:
    explicit ProxyDevice(std::shared_ptr<Core> core, const DeviceFacts& facts)
        : core_(std::move(core)), facts_(facts) {}
    ~ProxyDevice() override {
        core_->push([](Device& d) { d.flush(); });
        core_->sendBatch();
    }

    std::string adapterName() const override { return facts_.adapter; }
    bool dualSourceBlending() const override { return facts_.dualSource; }
    uint32_t maxTextureSize() const override { return facts_.maxTexture; }
    bool compute(uint32_t storageBuffers, uint64_t bufferBytes) const override {
        bool ok = false;
        core_->call([&](Device& d) { ok = d.compute(storageBuffers, bufferBytes); });
        return ok;
    }
    void poll(bool wait) override {
        core_->sendBatch();
        if (wait)
            core_->call([](Device& d) { d.poll(true); });
    }

    bool hasSurface() const override { return facts_.surface; }
    bool configureSurface(uint32_t w, uint32_t h, bool vsync) override {
        bool ok = false;
        SurfaceInfo info;
        core_->call([&](Device& d) {
            ok = d.configureSurface(w, h, vsync);
            info = d.surfaceInfo();
        });
        facts_.surfaceInfo = info;
        return ok;
    }
    SurfaceInfo surfaceInfo() const override { return facts_.surfaceInfo; }
    TexturePtr acquireSurface() override {
        if (!facts_.surface)
            return {};
        const SurfaceInfo info = facts_.surfaceInfo;
        TextureDesc d;
        d.width = std::max(1u, info.width);
        d.height = std::max(1u, info.height);
        d.format = info.format;
        d.usage = RenderTarget | (info.copySource ? CopySrc : 0u);
        d.label = "window";
        auto slot = std::make_shared<TexSlot>();
        core_->push([slot, d](Device& dev) {
            slot->real = dev.acquireSurface();
            if (!slot->real) { // outdated / lost surface: draw this frame into a stand-in, nothing is shown
                TextureDesc stand = d;
                stand.usage |= CopySrc | Sampled;
                stand.label = "window stand-in";
                slot->real = dev.createTexture(stand);
            }
        });
        return std::make_shared<PTexture>(d, slot, core_);
    }
    void present() override {
        core_->push([](Device& d) { d.present(); });
        core_->sendBatch();
    }

    TexturePtr createTexture(const TextureDesc& in) override {
        TextureDesc d = in;
        d.width = std::max(1u, d.width);
        d.height = std::max(1u, d.height);
        d.mips = std::max(1u, d.mips);
        d.label = nullptr; // the caller's label may not outlive the recorded command
        auto slot = std::make_shared<TexSlot>();
        std::string label = in.label ? in.label : "";
        core_->push([slot, d, label](Device& dev) {
            TextureDesc real = d;
            real.label = label.empty() ? nullptr : label.c_str();
            slot->real = dev.createTexture(real);
        });
        return std::make_shared<PTexture>(d, slot, core_);
    }
    TexturePtr wrapTexture(void*, const TextureDesc&) override {
        std::fprintf(stderr, "[rhi-proxy] wrapTexture is not available through the proxy\n");
        return {};
    }
    ProgramPtr createProgram(const ProgramDesc& desc, std::string* error) override {
        auto slot = std::make_shared<ProgSlot>();
        std::string why;
        core_->call([&](Device& dev) { slot->real = dev.createProgram(desc, &why); });
        if (!slot->real) {
            if (error)
                *error = why;
            return {};
        }
        return std::make_shared<PProgram>(slot, desc.vertexStride, core_);
    }
    BufferPtr createBuffer(uint64_t bytes, const char* label) override {
        bytes = std::max<uint64_t>(16, (bytes + 15) & ~15ull);
        auto slot = std::make_shared<BufSlot>();
        std::string name = label ? label : "";
        core_->push([slot, bytes, name](Device& dev) {
            slot->real = dev.createBuffer(bytes, name.empty() ? nullptr : name.c_str());
        });
        return std::make_shared<PBuffer>(bytes, slot, core_);
    }
    void writeBuffer(Buffer& buffer, uint64_t offset, const void* data, uint64_t bytes) override {
        bytes &= ~3ull;
        if (!bytes || !data || offset + bytes > buffer.size())
            return;
        std::vector<uint8_t> copy(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + bytes);
        core_->push(
            [s = static_cast<PBuffer&>(buffer).slot, offset, copy = std::move(copy)](Device& d) {
                if (s->real)
                    d.writeBuffer(*s->real, offset, copy.data(), copy.size());
            },
            bytes);
    }
    void writeTexture(Texture& texture, uint32_t mip, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const void* data,
                      uint32_t rowBytes) override {
        if (!w || !h || !data)
            return;
        const uint32_t tight = w * texelBytes(texture.desc().format);
        const uint32_t stride = rowBytes ? rowBytes : tight;
        const size_t bytes = size_t(stride) * (h - 1) + tight;
        std::vector<uint8_t> copy(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + bytes);
        core_->push(
            [s = slotOf(&texture), mip, x, y, w, h, copy = std::move(copy), rowBytes](Device& d) {
                if (Texture* t = realOf(s))
                    d.writeTexture(*t, mip, x, y, w, h, copy.data(), rowBytes);
            },
            bytes);
    }
    void copyTexture(Texture& source, uint32_t sourceMip, uint32_t sx, uint32_t sy, Texture& dest, uint32_t destMip,
                     uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) override {
        core_->push([src = slotOf(&source), dst = slotOf(&dest), sourceMip, sx, sy, destMip, dx, dy, w, h](Device& d) {
            Texture *a = realOf(src), *b = realOf(dst);
            if (a && b)
                d.copyTexture(*a, sourceMip, sx, sy, *b, destMip, dx, dy, w, h);
        });
    }
    void clearColor(Texture& target, const float rgba[4]) override {
        std::array<float, 4> c{rgba[0], rgba[1], rgba[2], rgba[3]};
        core_->push([s = slotOf(&target), c](Device& d) {
            if (Texture* t = realOf(s))
                d.clearColor(*t, c.data());
        });
    }
    void clearDepth(Texture& target, float depth) override {
        core_->push([s = slotOf(&target), depth](Device& d) {
            if (Texture* t = realOf(s))
                d.clearDepth(*t, depth);
        });
    }
    bool readTexture(Texture& texture, uint32_t mip, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                     std::vector<uint8_t>& out) override {
        auto w8 = std::make_shared<Waiter>();
        auto result = std::make_shared<std::pair<bool, std::vector<uint8_t>>>();
        GpuThread* gpu = core_->gpu.get();
        gpu->pendingReads.fetch_add(1);
        core_->push([s = slotOf(&texture), mip, x, y, w, h, w8, result, gpu](Device& d) {
            Texture* t = realOf(s);
            if (!t) {
                gpu->pendingReads.fetch_sub(1);
                w8->signal();
                return;
            }
            d.readTextureAsync(*t, mip, x, y, w, h, [w8, result, gpu](bool ok, std::vector<uint8_t>&& pixels) {
                result->first = ok;
                result->second = std::move(pixels);
                gpu->pendingReads.fetch_sub(1);
                w8->signal();
            });
        });
        core_->sendBatch();
        w8->wait();
        if (result->first)
            out = std::move(result->second);
        return result->first;
    }
    void readTextureAsync(Texture& texture, uint32_t mip, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                          ReadCallback done) override {
        // `done` runs on the GPU thread.
        GpuThread* gpu = core_->gpu.get();
        gpu->pendingReads.fetch_add(1);
        core_->push([s = slotOf(&texture), mip, x, y, w, h, done = std::move(done), gpu](Device& d) mutable {
            Texture* t = realOf(s);
            if (!t) {
                gpu->pendingReads.fetch_sub(1);
                return done(false, {});
            }
            d.readTextureAsync(*t, mip, x, y, w, h,
                               [done = std::move(done), gpu](bool ok, std::vector<uint8_t>&& pixels) {
                                   gpu->pendingReads.fetch_sub(1);
                                   done(ok, std::move(pixels));
                               });
        });
        core_->sendBatch();
    }

    void setTargets(Texture* color, Texture* depth, uint32_t colorMip) override {
        core_->push([c = slotOf(color), z = slotOf(depth), colorMip](Device& d) {
            d.setTargets(realOf(c), realOf(z), colorMip);
        });
    }
    void setViewport(float x, float y, float w, float h) override {
        core_->push([x, y, w, h](Device& d) { d.setViewport(x, y, w, h); });
    }
    void setScissor(const Rect& rect) override {
        core_->push([rect](Device& d) { d.setScissor(rect); });
    }
    void setProgram(Program* program) override {
        auto* p = static_cast<PProgram*>(program);
        stride_ = p ? p->stride : 0;
        core_->push([s = p ? p->slot : nullptr](Device& d) { d.setProgram(s ? s->real.get() : nullptr); });
    }
    void setBlend(const BlendState& blend) override {
        core_->push([blend](Device& d) { d.setBlend(blend); });
    }
    void setDepth(const DepthState& depth) override {
        core_->push([depth](Device& d) { d.setDepth(depth); });
    }
    void setTopology(Topology topology) override {
        core_->push([topology](Device& d) { d.setTopology(topology); });
    }
    void setUniforms(uint32_t slot, const void* data, uint32_t bytes) override {
        std::vector<uint8_t> copy(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + bytes);
        core_->push([slot, copy = std::move(copy)](Device& d) {
            d.setUniforms(slot, copy.data(), static_cast<uint32_t>(copy.size()));
        });
    }
    void setTexture(uint32_t slot, Texture* texture, uint32_t mip, uint32_t mipCount) override {
        core_->push(
            [slot, t = slotOf(texture), mip, mipCount](Device& d) { d.setTexture(slot, realOf(t), mip, mipCount); });
    }
    void draw(const void* vertices, uint32_t count) override {
        if (!count || !stride_ || !vertices)
            return;
        const size_t bytes = size_t(count) * stride_;
        std::vector<uint8_t> copy(static_cast<const uint8_t*>(vertices), static_cast<const uint8_t*>(vertices) + bytes);
        core_->push([copy = std::move(copy), count](Device& d) { d.draw(copy.data(), count); }, bytes);
    }
    void drawNoInput(uint32_t count) override {
        core_->push([count](Device& d) { d.drawNoInput(count); });
    }
    void setBuffer(uint32_t slot, Buffer* buffer) override {
        core_->push([slot, b = buffer ? static_cast<PBuffer*>(buffer)->slot : nullptr](Device& d) {
            d.setBuffer(slot, b ? b->real.get() : nullptr);
        });
    }
    void setStorageTexture(uint32_t slot, Texture* texture) override {
        core_->push([slot, t = slotOf(texture)](Device& d) { d.setStorageTexture(slot, realOf(t)); });
    }
    void dispatch(uint32_t x, uint32_t y, uint32_t z) override {
        core_->push([x, y, z](Device& d) { d.dispatch(x, y, z); });
    }
    void flush() override {
        core_->push([](Device& d) { d.flush(); });
        core_->sendBatch();
    }

private:
    std::shared_ptr<Core> core_;
    DeviceFacts facts_;
    uint32_t stride_ = 0; // the current program's vertex stride (draw copies count * stride bytes)
};

} // namespace

std::unique_ptr<Device> openProxy(const OpenDesc& desc, std::string* error) {
    auto core = std::make_shared<Core>();
    DeviceFacts facts;
#if defined(__EMSCRIPTEN__)
    core->gpu = std::make_unique<WebGpuThread>(desc, facts);
#else
    core->gpu = std::make_unique<NativeGpuThread>(desc, facts);
#endif
    if (!facts.ok) {
        if (error)
            *error = facts.error.empty() ? "the GPU thread has no device" : facts.error;
        core->gpu.reset();
        return {};
    }
    std::fprintf(stderr, "[rhi-proxy] device on its own GPU thread: %s\n", facts.adapter.c_str());
    return std::make_unique<ProxyDevice>(core, facts);
}

} // namespace gsr::rhi
