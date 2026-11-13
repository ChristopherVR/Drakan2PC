// drakan::gpu device context, see gpu/Gpu.h.
#include "gpu/Gpu.h"

#if defined(DRAKAN_WGPU_NATIVE)
#include <webgpu/wgpu.h>
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <utility>

#if defined(DRAKAN_WGPU_NATIVE) && defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace drakan::gpu {

struct Context::State {
    std::atomic<bool> failed{false};
    std::atomic<bool> lost{false}; // the device is gone: no further call may reach it
    mutable std::mutex mutex;
    std::string lastError;
    void report(const char* kind, WGPUStringView message) {
        const std::string text = str(message);
        std::fprintf(stderr, "[gpu] %s: %s\n", kind, text.c_str());
        std::lock_guard lock(mutex);
        lastError = text;
        failed = true;
    }
};

const char* backendName(Backend backend) {
    switch (backend) {
        case Backend::Vulkan:
            return "Vulkan";
        case Backend::D3D12:
            return "D3D12";
        case Backend::GL:
            return "OpenGL";
        case Backend::Browser:
            return "WebGPU";
        default:
            return "auto";
    }
}

Backend parseBackend(const std::string& text) {
    std::string lower = text;
    for (char& c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower == "vulkan" || lower == "vk")
        return Backend::Vulkan;
    if (lower == "dx12" || lower == "d3d12")
        return Backend::D3D12;
    if (lower == "gl" || lower == "opengl" || lower == "gles")
        return Backend::GL;
    return Backend::Auto;
}

namespace {
#if defined(DRAKAN_WGPU_NATIVE) && defined(_WIN32)
// dxcompiler.dll beside the executable (UTF-8), when dxil.dll, which signs its output, is there too; empty otherwise.
std::string dxcBesideExecutable() {
    std::wstring exe(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, exe.data(), DWORD(exe.size()));
        if (n == 0)
            return {};
        if (n < exe.size()) {
            exe.resize(n);
            break;
        }
        exe.resize(exe.size() * 2);
    }
    const std::filesystem::path dir = std::filesystem::path(exe).parent_path();
    std::error_code ec;
    if (!std::filesystem::exists(dir / L"dxcompiler.dll", ec) || !std::filesystem::exists(dir / L"dxil.dll", ec))
        return {};
    const auto utf8 = (dir / L"dxcompiler.dll").u8string();
    return std::string(utf8.begin(), utf8.end());
}
#endif

Backend fromBackendType(WGPUBackendType type) {
    switch (type) {
        case WGPUBackendType_Vulkan:
            return Backend::Vulkan;
        case WGPUBackendType_D3D12:
            return Backend::D3D12;
        case WGPUBackendType_OpenGL:
        case WGPUBackendType_OpenGLES:
            return Backend::GL;
        case WGPUBackendType_WebGPU:
            return Backend::Browser;
        default:
            return Backend::Auto;
    }
}

// Lower is better: discrete, integrated, virtual / unknown, then software.
unsigned adapterRank(const WGPUAdapterInfo& info) {
    switch (info.adapterType) {
        case WGPUAdapterType_DiscreteGPU:
            return 0;
        case WGPUAdapterType_IntegratedGPU:
            return 1;
        case WGPUAdapterType_CPU:
            return 10;
        default:
            return 2;
    }
}

// Vulkan first everywhere, then the platform's other API (D3D12 on Windows, GL on Linux).
unsigned backendRank(Backend backend) {
    switch (backend) {
        case Backend::Vulkan:
            return 0;
        case Backend::D3D12:
            return 1;
        case Backend::GL:
            return 2;
        default:
            return 3;
    }
}
} // namespace

std::unique_ptr<Context> Context::create(const ContextOptions& options, std::string* error) {
    std::unique_ptr<Context> context(new Context());
    context->state_ = std::make_unique<State>();
    if (!context->init(options, error))
        return {};
    return context;
}

#if defined(DRAKAN_WGPU_NATIVE)
void Context::createAsync(const ContextOptions& options, CreateCallback done) {
    std::string error;
    auto context = create(options, &error);
    done(std::move(context), error);
}
#else
void Context::createAsync(const ContextOptions& options, CreateCallback done) {
    struct Pending {
        std::unique_ptr<Context> context;
        CreateCallback done;
        void finish(const std::string& error) {
            if (!error.empty()) {
                std::fprintf(stderr, "[gpu] %s\n", error.c_str());
                context.reset();
            }
            CreateCallback cb = std::move(done);
            auto ctx = std::move(context);
            delete this;
            cb(std::move(ctx), error);
        }
    };
    auto* pending = new Pending{std::unique_ptr<Context>(new Context()), std::move(done)};
    Context& c = *pending->context;
    c.wantTimestamps_ = options.timestampQuery;
    c.state_ = std::make_unique<State>();
    c.instance_ = wgpuCreateInstance(nullptr);
    if (!c.instance_)
        return pending->finish("cannot create a WebGPU instance (navigator.gpu unavailable)");
    auto adapterOptions = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    adapterOptions.powerPreference = WGPUPowerPreference_HighPerformance;
    auto callback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowSpontaneous;
    callback.userdata1 = pending;
    callback.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* user,
                           void*) {
        auto* p = static_cast<Pending*>(user);
        if (status != WGPURequestAdapterStatus_Success || !adapter)
            return p->finish("no WebGPU adapter: " + str(message));
        Context& ctx = *p->context;
        ctx.adapter_ = adapter;
        auto info = WGPU_ADAPTER_INFO_INIT;
        if (wgpuAdapterGetInfo(adapter, &info) == WGPUStatus_Success) {
            ctx.adapterName_ = str(info.device);
            if (ctx.adapterName_.empty())
                ctx.adapterName_ = str(info.description);
            ctx.software_ = info.adapterType == WGPUAdapterType_CPU;
            wgpuAdapterInfoFreeMembers(info);
        }
        if (ctx.adapterName_.empty())
            ctx.adapterName_ = "browser adapter";
        ctx.backend_ = Backend::Browser;
        ctx.requestDevice([p](bool ok, const std::string& why) { p->finish(ok ? std::string() : why); });
    };
    wgpuInstanceRequestAdapter(c.instance_, &adapterOptions, callback);
}
#endif

bool Context::init(const ContextOptions& options, std::string* error) {
    const auto fail = [&](const std::string& why) {
        if (error)
            *error = why;
        std::fprintf(stderr, "[gpu] %s\n", why.c_str());
        return false;
    };
    wantTimestamps_ = options.timestampQuery;
#if defined(DRAKAN_WGPU_NATIVE)
    // DRAKAN_GPU_LOG=error|warn|info|debug: wgpu-native's own messages as "[wgpu] ..." lines (diagnosis only).
    if (const char* level = std::getenv("DRAKAN_GPU_LOG"); level && *level) {
        wgpuSetLogCallback([](WGPULogLevel, WGPUStringView message,
                              void*) { std::fprintf(stderr, "[wgpu] %s\n", str(message).c_str()); },
                           nullptr);
        const std::string text = level;
        wgpuSetLogLevel(text == "debug"  ? WGPULogLevel_Debug
                        : text == "info" ? WGPULogLevel_Info
                        : text == "warn" ? WGPULogLevel_Warn
                                         : WGPULogLevel_Error);
    }
    Backend wanted = options.backend;
    if (const char* env = std::getenv("DRAKAN_GPU_BACKEND"); env && *env && wanted == Backend::Auto)
        wanted = parseBackend(env);
    WGPUInstanceExtras extras{};
    extras.chain.sType = static_cast<WGPUSType>(WGPUSType_InstanceExtras);
    std::string dxc;
#if defined(_WIN32)
    // FXC, wgpu's default D3D12 compiler, takes minutes or never finishes on the ray tracing shader. The build stages
    // DXC from the Windows SDK beside the executable.
    dxc = dxcBesideExecutable();
    if (!dxc.empty()) {
        extras.dx12ShaderCompiler = WGPUDx12Compiler_Dxc;
        extras.dxcPath = {dxc.data(), dxc.size()};
    }
#endif
    switch (wanted) {
        case Backend::Vulkan:
            extras.backends = WGPUInstanceBackend_Vulkan;
            break;
        case Backend::D3D12:
            extras.backends = WGPUInstanceBackend_DX12;
            break;
        case Backend::GL:
            extras.backends = WGPUInstanceBackend_GL;
            break;
        default:
            extras.backends = WGPUInstanceBackend_All;
            break;
    }
    auto instanceDesc = WGPU_INSTANCE_DESCRIPTOR_INIT;
    instanceDesc.nextInChain = &extras.chain;
    instance_ = wgpuCreateInstance(&instanceDesc);
    if (!instance_)
        return fail("cannot create a WebGPU instance");

    WGPUInstanceEnumerateAdapterOptions enumerate{};
    enumerate.backends = extras.backends;
    std::vector<WGPUAdapter> adapters(wgpuInstanceEnumerateAdapters(instance_, &enumerate, nullptr));
    wgpuInstanceEnumerateAdapters(instance_, &enumerate, adapters.data());
    // No adapter means no backend of the instance started (e.g. Vulkan asked for and no Vulkan driver installed):
    // creating a surface on such an instance aborts inside wgpu-native, so report it before the probe below.
    if (adapters.empty())
        return fail(std::string("no usable GPU adapter (backend ") + backendName(wanted) + ")");

    // A surface for the target window lets adapter selection skip adapters that cannot present to it.
    WGPUSurface probe = nullptr;
    if (options.compatibleWith && options.compatibleWith->kind != NativeWindow::Kind::None) {
        if (auto surface = createSurface(*options.compatibleWith)) {
            probe = surface->surface_;
            surface->surface_ = nullptr; // keep the handle for the probe below
        }
    }
    // DRAKAN_GPU_SOFTWARE_ONLY=1 (tests only): a context that accepts a CPU adapter takes nothing else, as on a
    // machine without a graphics card (a CI runner), so a test's software case can be timed on a developer machine.
    const char* softwareOnlyEnv = std::getenv("DRAKAN_GPU_SOFTWARE_ONLY");
    const bool softwareOnly = options.allowSoftwareAdapter && softwareOnlyEnv && *softwareOnlyEnv == '1';
    unsigned best = ~0u;
    for (WGPUAdapter candidate : adapters) {
        auto info = WGPU_ADAPTER_INFO_INIT;
        const bool haveInfo = wgpuAdapterGetInfo(candidate, &info) == WGPUStatus_Success;
        bool usable = haveInfo;
        if (usable && info.adapterType == WGPUAdapterType_CPU && !options.allowSoftwareAdapter)
            usable = false;
        if (usable && softwareOnly && info.adapterType != WGPUAdapterType_CPU)
            usable = false;
        if (usable && probe) {
            auto caps = WGPU_SURFACE_CAPABILITIES_INIT;
            usable = wgpuSurfaceGetCapabilities(probe, candidate, &caps) == WGPUStatus_Success && caps.formatCount > 0;
            wgpuSurfaceCapabilitiesFreeMembers(caps);
        }
        const Backend backend = fromBackendType(info.backendType);
        const unsigned rank = adapterRank(info) * 16 + backendRank(backend);
        std::fprintf(stderr, "[gpu] adapter candidate: %s (%s, type %u)%s\n", str(info.device).c_str(),
                     backendName(backend), unsigned(info.adapterType), usable ? "" : " - skipped");
        if (usable && rank < best) {
            if (adapter_)
                wgpuAdapterRelease(adapter_);
            adapter_ = candidate;
            candidate = nullptr;
            best = rank;
            adapterName_ = str(info.device);
            adapterDriver_ = str(info.description);
            backend_ = backend;
            software_ = info.adapterType == WGPUAdapterType_CPU;
        }
        if (haveInfo)
            wgpuAdapterInfoFreeMembers(info);
        if (candidate)
            wgpuAdapterRelease(candidate);
    }
    if (probe)
        wgpuSurfaceRelease(probe);
    if (!adapter_)
        return fail(std::string("no usable GPU adapter (backend ") + backendName(wanted) + ")");
    fxc_ = backend_ == Backend::D3D12 && dxc.empty();

    bool ok = false;
    std::string why;
    // wgpu-native completes device requests before returning.
    requestDevice([&](bool success, const std::string& message) {
        ok = success;
        why = message;
    });
    if (!ok)
        return fail(why.empty() ? "device request did not complete" : why);
    return true;
#else
    (void)options;
    return fail("this build has no WebGPU device support yet");
#endif
}

void Context::requestDevice(std::function<void(bool, const std::string&)> done) {
    // Hardware ray queries are an experimental wgpu feature that the C API cannot enable (v29); ray tracing uses
    // WGSL compute instead, which also runs in the browser.
    std::vector<WGPUFeatureName> features;
    // The GS blend unit maps onto dual-source blending; without it the renderer blends in the shader. Not in the
    // browser: Emscripten 4.0.23's emdawnwebgpu passes the src1 blend factors as "one-minus-src1alpha" (the spec's
    // name is "one-minus-src1-alpha"), which Chrome rejects.
#if defined(DRAKAN_WGPU_NATIVE)
    if (wgpuAdapterHasFeature(adapter_, WGPUFeatureName_DualSourceBlending)) {
        features.push_back(WGPUFeatureName_DualSourceBlending);
        dualSource_ = true;
    }
#endif
    // GPU timestamps for profiling only (GSR_RAY_GPU_TIMING): a feature no shipped path needs.
    if ((wantTimestamps_ || std::getenv("GSR_RAY_GPU_TIMING")) &&
        wgpuAdapterHasFeature(adapter_, WGPUFeatureName_TimestampQuery)) {
        features.push_back(WGPUFeatureName_TimestampQuery);
        timestamps_ = true;
    }
    // Ask for everything the adapter offers: large render targets and many bindings for the GS caches.
    wgpuAdapterGetLimits(adapter_, &limits_);

    auto desc = WGPU_DEVICE_DESCRIPTOR_INIT;
    desc.label = view("drakan");
    desc.requiredFeatureCount = features.size();
    desc.requiredFeatures = features.data();
    desc.requiredLimits = &limits_;
    desc.uncapturedErrorCallbackInfo.userdata1 = state_.get();
    desc.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const*, WGPUErrorType type, WGPUStringView message,
                                                   void* user, void*) {
        static_cast<State*>(user)->report(type == WGPUErrorType_OutOfMemory ? "out of memory" : "error", message);
    };
    desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    desc.deviceLostCallbackInfo.userdata1 = state_.get();
    desc.deviceLostCallbackInfo.callback = [](WGPUDevice const*, WGPUDeviceLostReason reason, WGPUStringView message,
                                              void* user, void*) {
        if (reason != WGPUDeviceLostReason_Destroyed && reason != WGPUDeviceLostReason_CallbackCancelled) {
            static_cast<State*>(user)->lost = true;
            static_cast<State*>(user)->report("device lost", message);
        }
    };
    struct Request {
        Context* self;
        std::function<void(bool, const std::string&)> done;
    };
    auto callback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowSpontaneous;
    callback.userdata1 = new Request{this, std::move(done)};
    callback.callback = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* user,
                           void*) {
        std::unique_ptr<Request> r(static_cast<Request*>(user));
        Context& c = *r->self;
        if (status != WGPURequestDeviceStatus_Success || !device)
            return r->done(false, "cannot create a device on " + c.adapterName_ + ": " + str(message));
        c.device_ = device;
        c.queue_ = wgpuDeviceGetQueue(device);
        wgpuDeviceGetLimits(device, &c.limits_);
        // Name, API and driver text are the adapter's own answers (wgpuAdapterGetInfo), not this build's choice.
        std::fprintf(stderr, "[gpu] device: %s via %s%s%s%s%s%s\n", c.adapterName_.c_str(), backendName(c.backend_),
                     c.software_ ? " (software)" : "", c.dualSource_ ? ", dual-source blending" : "",
                     c.timestamps_ ? ", timestamp queries" : "", c.adapterDriver_.empty() ? "" : ", driver ",
                     c.adapterDriver_.c_str());
        r->done(true, {});
    };
    wgpuAdapterRequestDevice(adapter_, &desc, callback);
}

Context::~Context() {
    if (queue_)
        wgpuQueueRelease(queue_);
    if (device_) {
        wgpuDeviceDestroy(device_);
        wgpuDeviceRelease(device_);
    }
    if (adapter_)
        wgpuAdapterRelease(adapter_);
    if (instance_)
        wgpuInstanceRelease(instance_);
}

bool Context::failed() const {
    return state_->failed.load();
}

bool Context::deviceLost() const {
    return state_->lost.load();
}

void Context::loseDeviceForTest() {
    if (state_->lost.exchange(true))
        return;
    std::fprintf(stderr, "[gpu] device lost: destroyed on request (test)\n");
    {
        std::lock_guard lock(state_->mutex);
        state_->lastError = "destroyed on request (test)";
    }
    state_->failed = true;
    if (device_)
        wgpuDeviceDestroy(device_);
}

std::string Context::lastError() const {
    std::lock_guard lock(state_->mutex);
    return state_->lastError;
}

void Context::poll(bool wait) {
#if defined(DRAKAN_WGPU_NATIVE)
    if (state_->lost.load())
        return; // a lost device is not called into again
    wgpuDevicePoll(device_, wait, nullptr);
#else
    (void)wait;
    wgpuInstanceProcessEvents(instance_);
#endif
}

WGPUShaderModule Context::shaderModule(const std::string& wgsl, const char* label) {
    auto source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = {wgsl.data(), wgsl.size()};
    auto desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    desc.nextInChain = &source.chain;
    desc.label = view(label);
#if !defined(DRAKAN_WGPU_NATIVE)
    // The browser reports compile errors asynchronously (the uncaptured-error callback); this thread cannot wait.
    return wgpuDeviceCreateShaderModule(device_, &desc);
#else
    wgpuDevicePushErrorScope(device_, WGPUErrorFilter_Validation);
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(device_, &desc);
    struct Scope {
        bool done = false;
        std::string message;
    } scope;
    auto callback = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowSpontaneous;
    callback.userdata1 = &scope;
    callback.callback = [](WGPUPopErrorScopeStatus, WGPUErrorType type, WGPUStringView message, void* user, void*) {
        auto& s = *static_cast<Scope*>(user);
        if (type != WGPUErrorType_NoError)
            s.message = str(message);
        s.done = true;
    };
    wgpuDevicePopErrorScope(device_, callback);
    while (!scope.done)
        poll(true);
    if (!scope.message.empty()) {
        std::fprintf(stderr, "[gpu] shader '%s': %s\n", label ? label : "", scope.message.c_str());
        std::lock_guard lock(state_->mutex);
        state_->lastError = scope.message;
        if (module)
            wgpuShaderModuleRelease(module);
        return nullptr;
    }
    return module;
#endif
}

bool Context::readTexture(WGPUTexture texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                          uint32_t bytesPerPixel, std::vector<uint8_t>& out, uint32_t mipLevel) {
#if defined(DRAKAN_WGPU_NATIVE)
    struct Result {
        std::atomic<bool> done{false};
        bool ok = false;
    } result;
    readTextureAsync(texture, x, y, width, height, bytesPerPixel, mipLevel,
                     [&](bool ok, std::vector<uint8_t>&& pixels) {
                         if (ok)
                             out = std::move(pixels);
                         result.ok = ok;
                         result.done = true;
                     });
    while (!result.done.load() && !failed())
        poll(true);
    return result.done.load() && result.ok;
#else
    (void)texture, (void)x, (void)y, (void)width, (void)height, (void)bytesPerPixel, (void)out, (void)mipLevel;
    std::fprintf(stderr, "[gpu] readTexture: a browser thread cannot wait for a readback (readTextureAsync)\n");
    return false;
#endif
}

void Context::readTextureAsync(WGPUTexture texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                               uint32_t bytesPerPixel, uint32_t mipLevel, ReadCallback done) {
    if (!texture || !width || !height || !bytesPerPixel)
        return done(false, {});
    // Buffer rows must be 256-byte aligned; repack into tight rows afterwards.
    const uint32_t tightRow = width * bytesPerPixel;
    const uint32_t paddedRow = (tightRow + 255u) & ~255u;
    const uint64_t size = uint64_t(paddedRow) * height;
    auto bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufferDesc.size = size;
    bufferDesc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(device_, &bufferDesc);
    if (!buffer)
        return done(false, {});
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device_, nullptr);
    auto source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    source.texture = texture;
    source.mipLevel = mipLevel;
    source.origin = {x, y, 0};
    auto destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    destination.buffer = buffer;
    destination.layout.bytesPerRow = paddedRow;
    destination.layout.rowsPerImage = height;
    const WGPUExtent3D extent{width, height, 1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &extent);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(queue_, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    struct Map {
        WGPUBuffer buffer;
        uint64_t size;
        uint32_t tightRow, paddedRow, height;
        ReadCallback done;
    };
    auto callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowSpontaneous;
    callback.userdata1 = new Map{buffer, size, tightRow, paddedRow, height, std::move(done)};
    callback.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* user, void*) {
        std::unique_ptr<Map> m(static_cast<Map*>(user));
        std::vector<uint8_t> out;
        bool ok = status == WGPUMapAsyncStatus_Success;
        if (ok) {
            const auto* data = static_cast<const uint8_t*>(wgpuBufferGetConstMappedRange(m->buffer, 0, m->size));
            ok = data != nullptr;
            if (ok) {
                out.resize(size_t(m->tightRow) * m->height);
                for (uint32_t row = 0; row < m->height; ++row)
                    std::memcpy(out.data() + size_t(row) * m->tightRow, data + size_t(row) * m->paddedRow, m->tightRow);
            }
            wgpuBufferUnmap(m->buffer);
        }
        wgpuBufferRelease(m->buffer);
        m->done(ok, std::move(out));
    };
    wgpuBufferMapAsync(buffer, WGPUMapMode_Read, 0, size, callback);
}

} // namespace drakan::gpu
