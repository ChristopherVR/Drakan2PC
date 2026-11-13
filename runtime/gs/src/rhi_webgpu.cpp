// gsr::rhi on webgpu.h, see rhi.h.
//
// Recording model: one command encoder collects everything until a submit. Render passes open lazily at the first draw
// after a target change and close before any copy, clear, upload or readback. Constants, vertices and upload rows are
// written into CPU arenas; at submit their used ranges are written to the matching GPU ring buffers with one queue
// write each, then the encoder is submitted. Queue writes are ordered after earlier submits, so a ring can restart at
// offset 0 after every submit. A full ring forces a submit.
#include "rhi.h"
#include "rhi_open.h"

#include <gpu/Gpu.h>
#if defined(DRAKAN_WGPU_NATIVE)
#include <webgpu/wgpu.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <tuple>
#include <unordered_map>

namespace gsr::rhi {

uint32_t texelBytes(Format format) {
    switch (format) {
        case Format::RG16F:
        case Format::R32F:
        case Format::Depth32F:
        case Format::RGBA8:
        case Format::BGRA8:
            return 4;
        case Format::RGBA16F:
            return 8;
    }
    return 4;
}

namespace {
using drakan::gpu::view;

constexpr uint32_t kUniformSlotBytes = 256; // binding size of every uniform slot (and the dynamic-offset alignment)
constexpr uint32_t kMaxUniformSlots = 4;
// Bindings 8-15 are sampled textures; sampler bindings begin at 16. WebGPU guarantees at least 16 textures/stage.
constexpr uint32_t kMaxTextureSlots = 8;
constexpr uint32_t kMaxBufferSlots = 8;
constexpr uint32_t kMaxStorageTextureSlots = 2;
constexpr uint64_t kUniformRingBytes = 8ull << 20;
constexpr uint64_t kVertexRingBytes = 32ull << 20;
constexpr uint64_t kUploadRingBytes = 64ull << 20;
// Draw data (Device::writeDrawData): a storage buffer, or an rgba32uint texture of kDrawDataTextureWidth texels per
// row, with a CPU arena written at submit like the other rings.
constexpr uint64_t kDrawDataRingBytes = 8ull << 20;
constexpr uint32_t kDrawDataBinding = 24;
constexpr uint32_t kReservedUniformSlots = 16; // reserveDraw: room for this many setUniforms calls of one draw

WGPUTextureFormat wgpuFormat(Format format) {
    switch (format) {
        case Format::RGBA8:
            return WGPUTextureFormat_RGBA8Unorm;
        case Format::BGRA8:
            return WGPUTextureFormat_BGRA8Unorm;
        case Format::R32F:
            return WGPUTextureFormat_R32Float;
        case Format::RG16F:
            return WGPUTextureFormat_RG16Float;
        case Format::RGBA16F:
            return WGPUTextureFormat_RGBA16Float;
        case Format::Depth32F:
            return WGPUTextureFormat_Depth32Float;
    }
    return WGPUTextureFormat_RGBA8Unorm;
}

WGPUBlendFactor wgpuFactor(BlendFactor f) {
    switch (f) {
        case BlendFactor::Zero:
            return WGPUBlendFactor_Zero;
        case BlendFactor::One:
            return WGPUBlendFactor_One;
        case BlendFactor::Src1Color:
            return WGPUBlendFactor_Src1;
        case BlendFactor::OneMinusSrc1Color:
            return WGPUBlendFactor_OneMinusSrc1;
        case BlendFactor::Src1Alpha:
            return WGPUBlendFactor_Src1Alpha;
        case BlendFactor::OneMinusSrc1Alpha:
            return WGPUBlendFactor_OneMinusSrc1Alpha;
        case BlendFactor::SrcAlpha:
            return WGPUBlendFactor_SrcAlpha;
        case BlendFactor::OneMinusSrcAlpha:
            return WGPUBlendFactor_OneMinusSrcAlpha;
        case BlendFactor::DstAlpha:
            return WGPUBlendFactor_DstAlpha;
        case BlendFactor::OneMinusDstAlpha:
            return WGPUBlendFactor_OneMinusDstAlpha;
    }
    return WGPUBlendFactor_One;
}

WGPUBlendOperation wgpuOp(BlendOp op) {
    switch (op) {
        case BlendOp::Add:
            return WGPUBlendOperation_Add;
        case BlendOp::Subtract:
            return WGPUBlendOperation_Subtract;
        case BlendOp::ReverseSubtract:
            return WGPUBlendOperation_ReverseSubtract;
        case BlendOp::Min:
            return WGPUBlendOperation_Min;
        case BlendOp::Max:
            return WGPUBlendOperation_Max;
    }
    return WGPUBlendOperation_Add;
}

WGPUCompareFunction wgpuCompare(Compare c) {
    switch (c) {
        case Compare::Never:
            return WGPUCompareFunction_Never;
        case Compare::Less:
            return WGPUCompareFunction_Less;
        case Compare::Equal:
            return WGPUCompareFunction_Equal;
        case Compare::LessEqual:
            return WGPUCompareFunction_LessEqual;
        case Compare::Greater:
            return WGPUCompareFunction_Greater;
        case Compare::NotEqual:
            return WGPUCompareFunction_NotEqual;
        case Compare::GreaterEqual:
            return WGPUCompareFunction_GreaterEqual;
        case Compare::Always:
            return WGPUCompareFunction_Always;
    }
    return WGPUCompareFunction_Always;
}

uint64_t nextTextureId() {
    static std::atomic<uint64_t> id{1};
    return id.fetch_add(1);
}

class WTexture final : public Texture {
public:
    WTexture(WGPUTexture texture, const TextureDesc& desc, bool owned = true)
        : texture_(texture), id_(nextTextureId()), owned_(owned) {
        desc_ = desc;
    }
    // Never reused, unlike view addresses: identifies a view in cached bind groups.
    uint64_t viewKey(uint32_t mip, uint32_t count) const {
        mip = std::min(mip, desc_.mips - 1);
        count = std::min(count, desc_.mips - mip);
        return (id_ << 16) | (uint64_t(mip) << 8) | count;
    }
    ~WTexture() override {
        for (auto& [key, v] : views_)
            wgpuTextureViewRelease(v);
        // Release only: recorded but unsubmitted commands may still use the texture, and wgpu frees it once no
        // command needs it. (wgpuTextureDestroy would invalidate those commands.)
        if (texture_ && owned_)
            wgpuTextureRelease(texture_);
    }
    WGPUTexture handle() const { return texture_; }
    // A view of mips [mip, mip + count); count ~0 = to the last level. Cached for the texture's lifetime.
    WGPUTextureView view(uint32_t mip, uint32_t count) {
        mip = std::min(mip, desc_.mips - 1);
        count = std::min(count, desc_.mips - mip);
        const uint64_t key = (uint64_t(mip) << 32) | count;
        if (auto found = views_.find(key); found != views_.end())
            return found->second;
        auto desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        desc.baseMipLevel = mip;
        desc.mipLevelCount = count;
        desc.dimension = WGPUTextureViewDimension_2D;
        desc.aspect = WGPUTextureAspect_All;
        WGPUTextureView v = wgpuTextureCreateView(texture_, &desc);
        views_.emplace(key, v);
        return v;
    }

private:
    WGPUTexture texture_ = nullptr;
    uint64_t id_ = 0;
    bool owned_ = true;
    std::map<uint64_t, WGPUTextureView> views_;
};

class WBuffer final : public Buffer {
public:
    WBuffer(WGPUBuffer buffer, uint64_t bytes) : buffer_(buffer), id_(nextTextureId()) { size_ = bytes; }
    ~WBuffer() override {
        if (buffer_)
            wgpuBufferRelease(buffer_); // recorded commands keep it alive until they ran
    }
    WGPUBuffer handle() const { return buffer_; }
    uint64_t key() const { return id_; }

private:
    WGPUBuffer buffer_ = nullptr;
    uint64_t id_ = 0;
};

struct PipelineKey {
    WGPUTextureFormat color = WGPUTextureFormat_Undefined, depth = WGPUTextureFormat_Undefined;
    BlendState blend;
    DepthState depthState;
    Topology topology = Topology::TriangleList;
    bool operator<(const PipelineKey& o) const { return std::memcmp(this, &o, sizeof(*this)) < 0; }
};
static_assert(std::is_trivially_copyable_v<PipelineKey>);

class WProgram final : public Program, public std::enable_shared_from_this<WProgram> {
public:
    ProgramDesc desc;
    WGPUShaderModule module = nullptr;
    WGPUBindGroupLayout groupLayout = nullptr;
    WGPUPipelineLayout layout = nullptr;
    std::map<PipelineKey, WGPURenderPipeline> pipelines; // a null pipeline: its creation failed
    // desc.backgroundPipelines: `pipelines` and `building` are shared with the device's pipeline worker.
    std::mutex mutex;
    std::set<PipelineKey> building;
    // Identifies the program in cached bind groups: never reused, unlike the address of a released program.
    const uint64_t id = nextTextureId();
    WGPUComputePipeline compute = nullptr;
    ~WProgram() override {
        if (compute)
            wgpuComputePipelineRelease(compute);
        for (auto& [key, p] : pipelines)
            if (p)
                wgpuRenderPipelineRelease(p);
        if (layout)
            wgpuPipelineLayoutRelease(layout);
        if (groupLayout)
            wgpuBindGroupLayoutRelease(groupLayout);
        if (module)
            wgpuShaderModuleRelease(module);
    }
};

// A GPU ring with a CPU arena of the same size; the used prefix is written at submit.
struct Ring {
    WGPUBuffer buffer = nullptr;
    std::vector<uint8_t> cpu;
    uint64_t used = 0;
    void create(WGPUDevice device, uint64_t bytes, WGPUBufferUsage usage, const char* label) {
        auto desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = bytes;
        desc.usage = usage | WGPUBufferUsage_CopyDst;
        desc.label = view(label);
        buffer = wgpuDeviceCreateBuffer(device, &desc);
        cpu.resize(bytes);
    }
    // Offset of `bytes` bytes at `align`, or ~0 when the ring is full.
    uint64_t allocate(uint64_t bytes, uint64_t align) {
        const uint64_t at = (used + align - 1) / align * align;
        if (at + bytes > cpu.size())
            return ~0ull;
        used = at + bytes;
        return at;
    }
    void upload(WGPUQueue queue) {
        if (used)
            wgpuQueueWriteBuffer(queue, buffer, 0, cpu.data(), (used + 3) & ~3ull);
        used = 0;
    }
    ~Ring() {
        if (buffer)
            wgpuBufferRelease(buffer);
    }
};

class WDevice final : public Device {
public:
    // A device on an existing context; with `owned` it also keeps the context (and the window surface) alive.
    explicit WDevice(drakan::gpu::Context& context, std::unique_ptr<drakan::gpu::Context> owned = {},
                     std::unique_ptr<drakan::gpu::Surface> surface = {})
        : ownedContext_(std::move(owned)), surface_(std::move(surface)), context_(context), device_(context.device()) {
        queue_ = context.queue();
        uniforms_.create(device_, kUniformRingBytes, WGPUBufferUsage_Uniform, "gs uniforms");
        vertices_.create(device_, kVertexRingBytes, WGPUBufferUsage_Vertex | WGPUBufferUsage_Index, "gs vertices");
        uploads_.create(device_, kUploadRingBytes, WGPUBufferUsage_CopySrc, "gs uploads");
        createDrawData();
        auto sampler = WGPU_SAMPLER_DESCRIPTOR_INIT;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = WGPUAddressMode_ClampToEdge;
        sampler.magFilter = sampler.minFilter = WGPUFilterMode_Nearest;
        sampler.mipmapFilter = WGPUMipmapFilterMode_Nearest;
        point_ = wgpuDeviceCreateSampler(device_, &sampler);
        sampler.magFilter = sampler.minFilter = WGPUFilterMode_Linear;
        linear_ = wgpuDeviceCreateSampler(device_, &sampler);
    }
    ~WDevice() override {
        stopPipelineWorker();
        flush();
        context_.poll(true);
        clearBindGroups();
        if (drawTextureView_)
            wgpuTextureViewRelease(drawTextureView_);
        if (drawTexture_)
            wgpuTextureRelease(drawTexture_);
        if (timingQueries_)
            wgpuQuerySetRelease(timingQueries_);
        if (timingResolve_)
            wgpuBufferRelease(timingResolve_);
        if (timingRead_)
            wgpuBufferRelease(timingRead_);
        if (point_)
            wgpuSamplerRelease(point_);
        if (linear_)
            wgpuSamplerRelease(linear_);
        surface_.reset();
    }

    std::string adapterName() const override {
        return context_.adapterName() + " (" + drakan::gpu::backendName(context_.backend()) + ")";
    }
    bool lost() const override { return context_.deviceLost(); }
    std::string lostReason() const override { return context_.lastError(); }
    void loseForTest() override { context_.loseDeviceForTest(); }
    void poll(bool wait) override {
        if (!context_.deviceLost())
            context_.poll(wait);
    }

    bool hasSurface() const override { return surface_ != nullptr; }
    bool configureSurface(uint32_t w, uint32_t h, bool vsync) override {
        if (gone())
            return false;
        if (!surface_)
            return false;
        submit();
        return surface_->configure(w, h, vsync);
    }
    SurfaceInfo surfaceInfo() const override {
        SurfaceInfo i;
        if (surface_) {
            i.width = surface_->width();
            i.height = surface_->height();
            i.format = surface_->format() == WGPUTextureFormat_BGRA8Unorm ? Format::BGRA8 : Format::RGBA8;
            i.copySource = surface_->copySource();
#if !defined(DRAKAN_WGPU_NATIVE)
            i.copySource = true; // the back buffer (see acquireSurface)
#endif
        }
        return i;
    }
    TexturePtr acquireSurface() override {
#if defined(DRAKAN_WGPU_NATIVE)
        // Before it hands out the next window image wgpu waits for the GPU to finish the previous frame, for one second
        // at most, and when that runs out (compute ray tracing at a large internal scale on a busy GPU) it gives the
        // device up and ends the process. So the wait is done here, without a time limit, when a frame can be that
        // slow: whenever this frame or the previous one ran compute work, or when the GPU is already two frames
        // behind. Waiting always would be safe too, but on a GPU other programs load it costs frame rate (a third to
        // a half in the measurement of 2026-11-12), because it holds the renderer to one frame in flight.
        // GSR_PRESENT_WAIT: 0 = never (diagnosis), 2 = always, anything else = the rule above.
        static const int presentWait = [] {
            const char* value = std::getenv("GSR_PRESENT_WAIT");
            return value && value[0] == '0' ? 0 : value && value[0] == '2' ? 2 : 1;
        }();
        if (!gone())
            context_.poll(false); // runs the completion callbacks of the frames that finished
        if (presentWait && havePresent_ && !gone()) {
            const uint64_t pending = presentsSubmitted_ - presentsDone_->load();
            if (presentWait == 2 || computeThisFrame_ || computeLastFrame_ || pending >= 2)
                wgpuDevicePoll(device_, true, &presentSubmission_);
            havePresent_ = false;
        }
        if (!gone())
            context_.poll(false);
#endif
        if (gone())
            return {};
        if (!surface_)
            return {};
#if !defined(DRAKAN_WGPU_NATIVE)
        // A canvas texture only lives until the current task returns, and a frame's commands can arrive in several
        // tasks: draw into a back buffer and copy it to the canvas at present().
        const SurfaceInfo info = surfaceInfo();
        if (!backBuffer_ || backBuffer_->width() != info.width || backBuffer_->height() != info.height ||
            backBuffer_->desc().format != info.format)
            backBuffer_ = createTexture(
                {info.width, info.height, 1, info.format, RenderTarget | CopySrc | CopyDst | Sampled, "back buffer"});
        return backBuffer_;
#else
        WGPUTexture current = surface_->acquire();
        if (!current)
            return {};
        const SurfaceInfo info = surfaceInfo();
        TextureDesc d;
        d.width = info.width;
        d.height = info.height;
        d.format = info.format;
        d.usage = RenderTarget | (info.copySource ? CopySrc : 0u);
        d.label = "window";
        return std::make_shared<WTexture>(current, d, false); // the surface owns the image
#endif
    }
    void present() override {
        if (gone())
            return;
        if (!surface_)
            return;
#if !defined(DRAKAN_WGPU_NATIVE)
        if (backBuffer_) {
            if (WGPUTexture current = surface_->acquire()) {
                WTexture canvas(current, backBuffer_->desc(), false);
                copyTexture(*backBuffer_, 0, 0, 0, canvas, 0, 0, 0, backBuffer_->width(), backBuffer_->height());
                flush(); // within this task, while the canvas texture is valid
            }
        }
#endif
        flush();
        surface_->present();
#if defined(DRAKAN_WGPU_NATIVE)
        presentSubmission_ = lastSubmission_; // the last work that wrote this frame's window image
        havePresent_ = haveSubmission_;
        computeLastFrame_ = computeThisFrame_;
        computeThisFrame_ = false;
        if (haveSubmission_ && !gone()) { // count the frames whose work the GPU has not finished
            ++presentsSubmitted_;
            auto done = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
            done.mode = WGPUCallbackMode_AllowSpontaneous;
            done.userdata1 = new std::shared_ptr<std::atomic<uint64_t>>(presentsDone_);
            done.callback = [](WGPUQueueWorkDoneStatus, WGPUStringView, void* user, void*) {
                std::unique_ptr<std::shared_ptr<std::atomic<uint64_t>>> counter(
                    static_cast<std::shared_ptr<std::atomic<uint64_t>>*>(user));
                ++**counter;
            };
            wgpuQueueOnSubmittedWorkDone(queue_, done);
        }
#endif
    }
    bool dualSourceBlending() const override { return context_.dualSourceBlending(); }
    uint32_t maxTextureSize() const override { return context_.limits().maxTextureDimension2D; }
    bool compute(uint32_t storageBuffers, uint64_t bufferBytes) const override {
        if (context_.fxcShaderCompiler())
            return false;
        const WGPULimits& l = context_.limits();
        return l.maxStorageBuffersPerShaderStage >= storageBuffers && l.maxStorageBufferBindingSize >= bufferBytes &&
               l.maxStorageTexturesPerShaderStage >= 1 && l.maxComputeInvocationsPerWorkgroup >= 256 &&
               l.maxComputeWorkgroupStorageSize >= 16384;
    }
#if defined(DRAKAN_WGPU_NATIVE)
    // Device-level creation in wgpu-native is thread-safe, and its error scopes (Context::shaderModule) are per thread.
    bool concurrentPrograms() const override { return true; }
#endif

    TexturePtr createTexture(const TextureDesc& in) override {
        if (gone())
            return {};
        TextureDesc d = in;
        d.width = std::max(1u, d.width);
        d.height = std::max(1u, d.height);
        d.mips = std::max(1u, d.mips);
        auto desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.label = view(d.label);
        desc.size = {d.width, d.height, 1};
        desc.mipLevelCount = d.mips;
        desc.format = wgpuFormat(d.format);
        WGPUTextureUsage usage = WGPUTextureUsage_None;
        if (d.usage & Sampled)
            usage |= WGPUTextureUsage_TextureBinding;
        if (d.usage & RenderTarget)
            usage |= WGPUTextureUsage_RenderAttachment;
        if (d.usage & CopySrc)
            usage |= WGPUTextureUsage_CopySrc;
        if (d.usage & CopyDst)
            usage |= WGPUTextureUsage_CopyDst;
        if (d.usage & Storage)
            usage |= WGPUTextureUsage_StorageBinding;
        desc.usage = usage;
        WGPUTexture t = wgpuDeviceCreateTexture(device_, &desc);
        if (!t)
            return {};
        return std::make_shared<WTexture>(t, d);
    }

    TexturePtr wrapTexture(void* wgpuTexture, const TextureDesc& desc) override {
        if (!wgpuTexture)
            return {};
        return std::make_shared<WTexture>(static_cast<WGPUTexture>(wgpuTexture), desc, false);
    }

    ProgramPtr createProgram(const ProgramDesc& in, std::string* error) override {
        if (gone())
            return {};
        if (in.textureSlots > kMaxTextureSlots) {
            if (error)
                *error = "RHI program exceeds the eight sampled-texture binding slots";
            return {};
        }
        auto program = std::make_shared<WProgram>();
        program->desc = in;
        program->module = context_.shaderModule(in.wgsl, in.label);
        if (!program->module) {
            if (error)
                *error = context_.lastError();
            return {};
        }
        const bool isCompute = !in.computeEntry.empty();
        const WGPUShaderStage stages =
            isCompute ? WGPUShaderStage_Compute : WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        std::vector<WGPUBindGroupLayoutEntry> entries;
        for (uint32_t i = 0; i < std::min(in.uniformSlots, kMaxUniformSlots); ++i) {
            auto e = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            e.binding = i;
            e.visibility = stages;
            e.buffer.type = WGPUBufferBindingType_Uniform;
            e.buffer.hasDynamicOffset = true;
            e.buffer.minBindingSize = 0;
            entries.push_back(e);
        }
        for (uint32_t i = 0; i < std::min(in.textureSlots, kMaxTextureSlots); ++i) {
            auto e = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            e.binding = 8 + i;
            e.visibility = stages;
            e.texture.viewDimension = WGPUTextureViewDimension_2D;
            e.texture.sampleType = (in.depthTextureMask >> i) & 1u   ? WGPUTextureSampleType_Depth
                                   : (in.unfilterableMask >> i) & 1u ? WGPUTextureSampleType_UnfilterableFloat
                                                                     : WGPUTextureSampleType_Float;
            entries.push_back(e);
        }
        if (in.samplers)
            for (uint32_t i = 0; i < 2; ++i) {
                auto e = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
                e.binding = 16 + i;
                e.visibility = stages;
                e.sampler.type = i ? WGPUSamplerBindingType_Filtering : WGPUSamplerBindingType_NonFiltering;
                entries.push_back(e);
            }
        if (!isCompute && in.drawData) {
            if (drawDataPath_ == DrawDataPath::None) {
                if (error)
                    *error = "the device has no draw data for the vertex stage";
                return {};
            }
            auto e = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            e.binding = kDrawDataBinding;
            e.visibility = WGPUShaderStage_Vertex;
            if (drawDataPath_ == DrawDataPath::StorageBuffer) {
                e.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
            } else {
                e.texture.viewDimension = WGPUTextureViewDimension_2D;
                e.texture.sampleType = WGPUTextureSampleType_Uint;
            }
            entries.push_back(e);
        }
        if (isCompute) {
            for (uint32_t i = 0; i < std::min(in.storageBuffers, kMaxBufferSlots); ++i) {
                auto e = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
                e.binding = 20 + i;
                e.visibility = stages;
                e.buffer.type = (in.readWriteMask >> i) & 1u ? WGPUBufferBindingType_Storage
                                                             : WGPUBufferBindingType_ReadOnlyStorage;
                entries.push_back(e);
            }
            for (uint32_t i = 0; i < std::min(in.storageTextures, kMaxStorageTextureSlots); ++i) {
                auto e = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
                e.binding = 28 + i;
                e.visibility = stages;
                e.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
                e.storageTexture.format = wgpuFormat(in.storageFormat);
                e.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
                entries.push_back(e);
            }
        }
        auto groupDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        groupDesc.entryCount = entries.size();
        groupDesc.entries = entries.data();
        program->groupLayout = wgpuDeviceCreateBindGroupLayout(device_, &groupDesc);
        auto layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        layoutDesc.bindGroupLayoutCount = 1;
        layoutDesc.bindGroupLayouts = &program->groupLayout;
        program->layout = wgpuDeviceCreatePipelineLayout(device_, &layoutDesc);
        if (isCompute) {
            auto desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
            desc.label = view(in.label);
            desc.layout = program->layout;
            desc.compute.module = program->module;
            desc.compute.entryPoint = view(program->desc.computeEntry.c_str());
            program->compute = wgpuDeviceCreateComputePipeline(device_, &desc);
            if (!program->compute || context_.failed()) {
                if (error)
                    *error = context_.lastError();
                return {};
            }
        }
        return program;
    }

    BufferPtr createBuffer(uint64_t bytes, const char* label) override {
        if (gone())
            return {};
        bytes = std::max<uint64_t>(16, (bytes + 15) & ~15ull);
        auto desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = bytes;
        desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
        desc.label = view(label);
        WGPUBuffer b = wgpuDeviceCreateBuffer(device_, &desc);
        if (!b)
            return {};
        return std::make_shared<WBuffer>(b, bytes);
    }

    void writeBuffer(Buffer& buffer, uint64_t offset, const void* data, uint64_t bytes) override {
        if (gone())
            return;
        bytes &= ~3ull;
        if (!bytes || !data || offset + bytes > buffer.size())
            return;
        endPass();
        WGPUBuffer source = uploads_.buffer;
        uint64_t at = uploads_.allocate(bytes, 16);
        if (at == ~0ull && bytes <= uploads_.cpu.size()) {
            submit();
            at = uploads_.allocate(bytes, 16);
        }
        WGPUBuffer dedicated = nullptr;
        if (at != ~0ull) {
            std::memcpy(uploads_.cpu.data() + at, data, bytes);
        } else { // larger than the ring: a one-off staging buffer, ordered by the encoder
            auto desc = WGPU_BUFFER_DESCRIPTOR_INIT;
            desc.size = bytes;
            desc.usage = WGPUBufferUsage_CopySrc;
            desc.mappedAtCreation = true;
            dedicated = wgpuDeviceCreateBuffer(device_, &desc);
            std::memcpy(wgpuBufferGetMappedRange(dedicated, 0, bytes), data, bytes);
            wgpuBufferUnmap(dedicated);
            source = dedicated;
            at = 0;
        }
        wgpuCommandEncoderCopyBufferToBuffer(encoder(), source, at, static_cast<WBuffer&>(buffer).handle(), offset,
                                             bytes);
        if (dedicated)
            wgpuBufferRelease(dedicated);
    }

    void writeTexture(Texture& texture, uint32_t mip, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const void* data,
                      uint32_t rowBytes) override {
        if (gone())
            return;
        if (!w || !h || !data)
            return;
        auto& t = static_cast<WTexture&>(texture);
        const uint32_t tight = w * texelBytes(t.desc().format);
        if (!rowBytes)
            rowBytes = tight;
        const uint32_t padded = (tight + 255u) & ~255u;
        const uint64_t bytes = uint64_t(padded) * h;
        endPass();
        WGPUBuffer source = uploads_.buffer;
        uint64_t offset = uploads_.allocate(bytes, 256);
        uint8_t* dst = nullptr;
        WGPUBuffer dedicated = nullptr;
        if (offset == ~0ull && bytes <= uploads_.cpu.size()) {
            submit();
            offset = uploads_.allocate(bytes, 256);
        }
        if (offset != ~0ull) {
            dst = uploads_.cpu.data() + offset;
        } else {
            // Larger than the whole ring (very large HD images): a one-off staging buffer, ordered by the encoder.
            auto desc = WGPU_BUFFER_DESCRIPTOR_INIT;
            desc.size = bytes;
            desc.usage = WGPUBufferUsage_CopySrc;
            desc.mappedAtCreation = true;
            dedicated = wgpuDeviceCreateBuffer(device_, &desc);
            dst = static_cast<uint8_t*>(wgpuBufferGetMappedRange(dedicated, 0, bytes));
            source = dedicated;
            offset = 0;
        }
        const auto* src = static_cast<const uint8_t*>(data);
        for (uint32_t row = 0; row < h; ++row)
            std::memcpy(dst + size_t(row) * padded, src + size_t(row) * rowBytes, tight);
        if (dedicated)
            wgpuBufferUnmap(dedicated);
        auto from = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        from.buffer = source;
        from.layout.offset = offset;
        from.layout.bytesPerRow = padded;
        from.layout.rowsPerImage = h;
        auto to = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        to.texture = t.handle();
        to.mipLevel = mip;
        to.origin = {x, y, 0};
        const WGPUExtent3D extent{w, h, 1};
        wgpuCommandEncoderCopyBufferToTexture(encoder(), &from, &to, &extent);
        if (dedicated)
            wgpuBufferRelease(dedicated); // the encoder keeps it alive until the copy ran
    }

    void copyTexture(Texture& source, uint32_t sourceMip, uint32_t sx, uint32_t sy, Texture& dest, uint32_t destMip,
                     uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) override {
        if (gone())
            return;
        if (!w || !h)
            return;
        endPass();
        auto from = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        from.texture = static_cast<WTexture&>(source).handle();
        from.mipLevel = sourceMip;
        from.origin = {sx, sy, 0};
        auto to = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        to.texture = static_cast<WTexture&>(dest).handle();
        to.mipLevel = destMip;
        to.origin = {dx, dy, 0};
        const WGPUExtent3D extent{w, h, 1};
        wgpuCommandEncoderCopyTextureToTexture(encoder(), &from, &to, &extent);
    }

    void clearColor(Texture& target, const float rgba[4]) override {
        if (gone())
            return;
        endPass();
        auto attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        attachment.view = static_cast<WTexture&>(target).view(0, 1);
        attachment.loadOp = WGPULoadOp_Clear;
        attachment.storeOp = WGPUStoreOp_Store;
        attachment.clearValue = {rgba[0], rgba[1], rgba[2], rgba[3]};
        auto pass = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        pass.colorAttachmentCount = 1;
        pass.colorAttachments = &attachment;
        WGPURenderPassEncoder p = wgpuCommandEncoderBeginRenderPass(encoder(), &pass);
        wgpuRenderPassEncoderEnd(p);
        wgpuRenderPassEncoderRelease(p);
    }

    void clearDepth(Texture& target, float depth) override {
        if (gone())
            return;
        endPass();
        auto attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        attachment.view = static_cast<WTexture&>(target).view(0, 1);
        attachment.depthLoadOp = WGPULoadOp_Clear;
        attachment.depthStoreOp = WGPUStoreOp_Store;
        attachment.depthClearValue = depth;
        auto pass = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        pass.depthStencilAttachment = &attachment;
        WGPURenderPassEncoder p = wgpuCommandEncoderBeginRenderPass(encoder(), &pass);
        wgpuRenderPassEncoderEnd(p);
        wgpuRenderPassEncoderRelease(p);
    }

    bool readTexture(Texture& texture, uint32_t mip, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                     std::vector<uint8_t>& out) override {
        if (gone())
            return false;
        struct Result {
            std::atomic<bool> done{false};
            bool ok = false;
        } result;
        readTextureAsync(texture, mip, x, y, w, h, [&](bool ok, std::vector<uint8_t>&& pixels) {
            if (ok)
                out = std::move(pixels);
            result.ok = ok;
            result.done = true;
        });
#if defined(DRAKAN_WGPU_NATIVE)
        while (!result.done.load() && !context_.failed())
            context_.poll(true);
#endif
        return result.done.load() && result.ok;
    }
    void readTextureAsync(Texture& texture, uint32_t mip, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                          ReadCallback done) override {
        if (gone())
            return done(false, {});
        submit();
        const uint32_t bpp = texelBytes(texture.desc().format);
        if (texture.desc().format != Format::Depth32F)
            return context_.readTextureAsync(static_cast<WTexture&>(texture).handle(), x, y, w, h, bpp, mip,
                                             std::move(done));
        // Depth: GL-class backends cannot copy depth to buffers and WebGPU only copies whole depth subresources, so
        // draw the region's depth bits into an RGBA8 target and read that (one path for every backend).
        TexturePtr scratch = drawDepthBits(static_cast<WTexture&>(texture), mip, x, y, w, h);
        if (!scratch)
            return done(false, {});
        context_.readTextureAsync(static_cast<WTexture&>(*scratch).handle(), 0, 0, w, h, 4, 0, std::move(done));
    }

    void setTargets(Texture* color, Texture* depth, uint32_t colorMip) override {
        if (color == color_ && depth == depth_ && colorMip == colorMip_)
            return;
        endPass();
        color_ = static_cast<WTexture*>(color);
        depth_ = static_cast<WTexture*>(depth);
        colorMip_ = colorMip;
        viewportSet_ = false;
    }
    void setViewport(float x, float y, float w, float h) override {
        viewport_ = {x, y, w, h};
        viewportSet_ = true;
        viewportDirty_ = true;
    }
    void setScissor(const Rect& rect) override {
        scissor_ = rect;
        scissorDirty_ = true;
    }
    void setProgram(Program* program) override { program_ = static_cast<WProgram*>(program); }
    Program* currentProgram() const override { return program_; }
    void setBlend(const BlendState& blend) override { blend_ = blend; }
    void setDepth(const DepthState& depth) override { depthState_ = depth; }
    void setTopology(Topology topology) override { topology_ = topology; }
    void setUniforms(uint32_t slot, const void* data, uint32_t bytes) override {
        if (slot >= kMaxUniformSlots || bytes > kUniformSlotBytes)
            return;
        uint64_t at = uniforms_.allocate(kUniformSlotBytes, kUniformSlotBytes);
        if (at == ~0ull) {
            submit();
            at = uniforms_.allocate(kUniformSlotBytes, kUniformSlotBytes);
        }
        std::memcpy(uniforms_.cpu.data() + at, data, bytes);
        uniformOffsets_[slot] = uint32_t(at);
    }
    void setTexture(uint32_t slot, Texture* texture, uint32_t mip, uint32_t mipCount) override {
        if (gone())
            return;
        if (slot >= kMaxTextureSlots)
            return;
        auto* t = static_cast<WTexture*>(texture);
        textures_[slot] = t ? t->view(mip, mipCount) : nullptr;
        textureKeys_[slot] = t ? t->viewKey(mip, mipCount) : 0;
    }

    void draw(const void* vertices, uint32_t count) override {
        if (gone())
            return;
        if (!program_ || !count || !program_->desc.vertexStride)
            return;
        const uint64_t bytes = uint64_t(count) * program_->desc.vertexStride;
        uint64_t at = vertices_.allocate(bytes, 16);
        if (at == ~0ull) {
            submit();
            at = vertices_.allocate(bytes, 16);
            if (at == ~0ull)
                return; // larger than the ring: not a GS draw size
        }
        std::memcpy(vertices_.cpu.data() + at, vertices, bytes);
        if (!prepare())
            return;
        wgpuRenderPassEncoderSetVertexBuffer(pass_, 0, vertices_.buffer, at, bytes);
        wgpuRenderPassEncoderDraw(pass_, count, 1, 0, 0);
    }

    void drawNoInput(uint32_t count) override {
        if (gone())
            return;
        if (!program_ || !count || !prepare())
            return;
        wgpuRenderPassEncoderDraw(pass_, count, 1, 0, 0);
    }

    DrawDataPath drawDataPath() const override { return drawDataPath_; }
    bool reserveDraw(uint64_t vertexBytes, uint64_t indexBytes, uint64_t drawDataBytes) override {
        if (gone())
            return false;
        const uint64_t vertexNeed = vertexBytes + indexBytes + 32;
        const uint64_t uniformNeed = uint64_t(kReservedUniformSlots) * kUniformSlotBytes;
        const uint64_t dataNeed = drawDataBytes + 16;
        if (vertexNeed > vertices_.cpu.size() || uniformNeed > uniforms_.cpu.size() ||
            (drawDataBytes && (drawDataPath_ == DrawDataPath::None || dataNeed > drawData_.cpu.size())))
            return false;
        if (vertices_.used + vertexNeed > vertices_.cpu.size() || uniforms_.used + uniformNeed > uniforms_.cpu.size() ||
            (drawDataBytes && drawData_.used + dataNeed > drawData_.cpu.size()))
            submit();
        return true;
    }
    uint32_t writeDrawData(const void* data, uint32_t bytes) override {
        if (gone() || drawDataPath_ == DrawDataPath::None || !data || !bytes || (bytes & 15u))
            return ~0u;
        const uint64_t at = drawData_.allocate(bytes, 16);
        if (at == ~0ull)
            return ~0u;
        std::memcpy(drawData_.cpu.data() + at, data, bytes);
        return uint32_t(at / 16);
    }
    bool drawIndexed(const void* vertices, uint32_t vertexCount, const uint32_t* indices,
                     uint32_t indexCount) override {
        if (gone())
            return false;
        if (!program_ || !vertexCount || !indexCount || !program_->desc.vertexStride)
            return false;
        // No submission here (see reserveDraw): a draw that does not fit draws nothing and says so.
        const uint64_t vertexBytes = uint64_t(vertexCount) * program_->desc.vertexStride;
        const uint64_t indexBytes = uint64_t(indexCount) * 4u;
        const uint64_t mark = vertices_.used;
        const uint64_t at = vertices_.allocate(vertexBytes, 16);
        const uint64_t indexAt = at == ~0ull ? ~0ull : vertices_.allocate(indexBytes, 16);
        if (indexAt == ~0ull) {
            vertices_.used = mark;
            return false;
        }
        std::memcpy(vertices_.cpu.data() + at, vertices, vertexBytes);
        std::memcpy(vertices_.cpu.data() + indexAt, indices, indexBytes);
        if (!prepare())
            return false;
        wgpuRenderPassEncoderSetVertexBuffer(pass_, 0, vertices_.buffer, at, vertexBytes);
        wgpuRenderPassEncoderSetIndexBuffer(pass_, vertices_.buffer, WGPUIndexFormat_Uint32, indexAt, indexBytes);
        wgpuRenderPassEncoderDrawIndexed(pass_, indexCount, 1, 0, 0, 0);
        return true;
    }
    PipelineState pipelineState() override {
        if (gone() || !program_)
            return PipelineState::Failed;
        if (!program_->desc.backgroundPipelines || program_->compute)
            return PipelineState::Ready;
        const PipelineKey key = currentKey();
        {
            std::lock_guard lock(program_->mutex);
            if (auto found = program_->pipelines.find(key); found != program_->pipelines.end())
                return found->second ? PipelineState::Ready : PipelineState::Failed;
            if (program_->building.count(key))
                return PipelineState::Pending;
            program_->building.insert(key);
        }
        if (!concurrentPrograms()) { // the browser's device thread: created now (the browser compiles it later)
            WGPURenderPipeline p = buildPipeline(*program_, key);
            std::lock_guard lock(program_->mutex);
            program_->building.erase(key);
            program_->pipelines.emplace(key, p);
            return p ? PipelineState::Ready : PipelineState::Failed;
        }
        startPipelineWorker();
        {
            std::lock_guard lock(pipelineMutex_);
            pipelineJobs_.push_back({program_->shared_from_this(), key});
            ++pipelinesPending_;
        }
        pipelineWake_.notify_one();
        return PipelineState::Pending;
    }
    void waitPipelines() override {
        std::unique_lock lock(pipelineMutex_);
        pipelineIdle_.wait(lock, [&] { return pipelinesPending_ == 0; });
    }

    void setBuffer(uint32_t slot, Buffer* buffer) override {
        if (slot >= kMaxBufferSlots)
            return;
        auto* b = static_cast<WBuffer*>(buffer);
        buffers_[slot] = b ? b->handle() : nullptr;
        bufferKeys_[slot] = b ? b->key() : 0;
        bufferSizes_[slot] = b ? b->size() : 0;
    }
    void setStorageTexture(uint32_t slot, Texture* texture) override {
        if (gone())
            return;
        if (slot >= kMaxStorageTextureSlots)
            return;
        auto* t = static_cast<WTexture*>(texture);
        storage_[slot] = t ? t->view(0, 1) : nullptr;
        storageKeys_[slot] = t ? t->viewKey(0, 1) : 0;
    }
    void dispatch(uint32_t x, uint32_t y, uint32_t z) override {
        if (gone())
            return;
#if defined(DRAKAN_WGPU_NATIVE)
        computeThisFrame_ = true; // see acquireSurface
#endif
        if (!program_ || !program_->compute || !x || !y || !z)
            return;
        WGPUBindGroup group = bindGroup();
        if (!group)
            return;
        endPass();
        auto timestamps = WGPU_PASS_TIMESTAMP_WRITES_INIT;
        auto passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        if (timing_ && timed_ < kTimedDispatches) {
            timestamps.querySet = timingQueries_;
            timestamps.beginningOfPassWriteIndex = 2 * timed_;
            timestamps.endOfPassWriteIndex = 2 * timed_ + 1;
            passDesc.timestampWrites = &timestamps;
            ++timed_;
        }
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder(), &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, program_->compute);
        std::array<uint32_t, kMaxUniformSlots> offsets{};
        const uint32_t uniformCount = std::min(program_->desc.uniformSlots, kMaxUniformSlots);
        for (uint32_t i = 0; i < uniformCount; ++i)
            offsets[i] = uniformOffsets_[i];
        wgpuComputePassEncoderSetBindGroup(pass, 0, group, uniformCount, offsets.data());
        wgpuComputePassEncoderDispatchWorkgroups(pass, x, y, z);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
    }

    bool beginDispatchTiming() override {
        if (gone())
            return false;
        if (timing_ || timingBusy_ || !context_.timestampQuery())
            return false;
        if (!timingQueries_) {
            auto q = WGPU_QUERY_SET_DESCRIPTOR_INIT;
            q.type = WGPUQueryType_Timestamp;
            q.count = 2 * kTimedDispatches;
            timingQueries_ = wgpuDeviceCreateQuerySet(device_, &q);
            auto b = WGPU_BUFFER_DESCRIPTOR_INIT;
            b.size = 2 * kTimedDispatches * sizeof(uint64_t);
            b.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
            timingResolve_ = wgpuDeviceCreateBuffer(device_, &b);
            b.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
            timingRead_ = wgpuDeviceCreateBuffer(device_, &b);
            if (!timingQueries_ || !timingResolve_ || !timingRead_)
                return false;
        }
        timing_ = true;
        timed_ = 0;
        return true;
    }
    void endDispatchTiming(std::function<void(std::vector<double>&&)> done) override {
        if (gone())
            return;
        if (!timing_)
            return;
        timing_ = false;
        const uint32_t count = timed_;
        if (!count)
            return;
        endPass();
        const uint64_t bytes = uint64_t(count) * 2 * sizeof(uint64_t);
        wgpuCommandEncoderResolveQuerySet(encoder(), timingQueries_, 0, 2 * count, timingResolve_, 0);
        wgpuCommandEncoderCopyBufferToBuffer(encoder(), timingResolve_, 0, timingRead_, 0, bytes);
        submit();
        timingBusy_ = true;
        struct Read {
            WDevice* self;
            uint32_t count;
            std::function<void(std::vector<double>&&)> done;
        };
        auto callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_AllowSpontaneous;
        callback.userdata1 = new Read{this, count, std::move(done)};
        callback.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* user, void*) {
            std::unique_ptr<Read> r(static_cast<Read*>(user));
            std::vector<double> ms;
            if (status == WGPUMapAsyncStatus_Success) {
                const auto* ticks = static_cast<const uint64_t*>(
                    wgpuBufferGetConstMappedRange(r->self->timingRead_, 0, r->count * 2 * sizeof(uint64_t)));
                for (uint32_t i = 0; ticks && i < r->count; ++i) // nanoseconds (WebGPU timestamps)
                    ms.push_back(ticks[2 * i + 1] > ticks[2 * i] ? double(ticks[2 * i + 1] - ticks[2 * i]) * 1e-6
                                                                 : 0.0);
                wgpuBufferUnmap(r->self->timingRead_);
            }
            r->self->timingBusy_ = false;
            if (r->done && !ms.empty())
                r->done(std::move(ms));
        };
        wgpuBufferMapAsync(timingRead_, WGPUMapMode_Read, 0, bytes, callback);
    }

    void flush() override {
        if (gone())
            return;
        submit();
        // Bind groups reference views of textures the caller may now free; drop them at frame boundaries.
        if (bindGroups_.size() > 4096)
            clearBindGroups();
    }

private:
    // The depth region's float bits in a new RGBA8 texture (drawn and submitted).
    TexturePtr drawDepthBits(WTexture& depth, uint32_t mip, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
        if (!(depth.desc().usage & Sampled))
            return {};
        if (!depthProgram_) {
            ProgramDesc d;
            d.wgsl = R"(
struct U { origin: vec4u }
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(8) var depth: texture_2d<f32>;
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    let p = array(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
    return vec4f(p[i], 0.0, 1.0);
}
@fragment fn fs(@builtin(position) pos: vec4f) -> @location(0) vec4f {
    // Depth32Float bound as unfilterable float: GLSL backends map texture_depth_2d to shadow samplers.
    let p = vec2i(pos.xy) + vec2i(u.origin.xy);
    // The float's bits as RGBA8 bytes (little-endian): RGBA8 is renderable on every backend, R32F is not on GL.
    return unpack4x8unorm(bitcast<u32>(textureLoad(depth, p, i32(u.origin.z)).r));
}
)";
            d.uniformSlots = 1;
            d.textureSlots = 1;
            d.unfilterableMask = 1;
            d.vertexInput = false;
            d.label = "depth readback";
            depthProgram_ = createProgram(d, nullptr);
            if (!depthProgram_)
                return {};
        }
        auto scratch = createTexture({w, h, 1, Format::RGBA8, RenderTarget | CopySrc, "depth readback"});
        // Internal draw: keep the caller's sticky state.
        const auto saved = std::make_tuple(color_, depth_, colorMip_, program_, blend_, depthState_, topology_,
                                           viewport_, viewportSet_, scissor_, uniformOffsets_, textures_, textureKeys_);
        setTargets(scratch.get(), nullptr, 0);
        setProgram(depthProgram_.get());
        setBlend({});
        setDepth({});
        setTopology(Topology::TriangleList);
        viewportSet_ = false;
        viewportDirty_ = true;
        setScissor({0, 0, w, h});
        const uint32_t origin[4] = {x, y, mip, 0};
        setUniforms(0, origin, sizeof(origin));
        setTexture(0, &depth, mip, 1);
        drawNoInput(3);
        endPass();
        std::tie(color_, depth_, colorMip_, program_, blend_, depthState_, topology_, viewport_, viewportSet_, scissor_,
                 uniformOffsets_, textures_, textureKeys_) = saved;
        viewportDirty_ = scissorDirty_ = true;
        submit();
        return scratch;
    }

    // The device is lost: no call may reach it any more. Also lets a pending loss be reported (the device-lost
    // callback runs from a poll), so it is found before the next call rather than inside it.
    bool gone() const { return context_.deviceLost(); }
    WGPUCommandEncoder encoder() {
        if (!encoder_)
            encoder_ = wgpuDeviceCreateCommandEncoder(device_, nullptr);
        return encoder_;
    }
    void endPass() {
        if (pass_) {
            wgpuRenderPassEncoderEnd(pass_);
            wgpuRenderPassEncoderRelease(pass_);
            pass_ = nullptr;
        }
    }
    void submit() {
        if (gone())
            return;
        endPass();
        uniforms_.upload(queue_);
        vertices_.upload(queue_);
        uploads_.upload(queue_);
        uploadDrawData();
        if (encoder_) {
            WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder_, nullptr);
#if defined(DRAKAN_WGPU_NATIVE)
            lastSubmission_ = wgpuQueueSubmitForIndex(queue_, 1, &commands);
            haveSubmission_ = true;
#else
            wgpuQueueSubmit(queue_, 1, &commands);
#endif
            wgpuCommandBufferRelease(commands);
            wgpuCommandEncoderRelease(encoder_);
            encoder_ = nullptr;
#if defined(DRAKAN_WGPU_NATIVE)
            context_.poll(false); // a loss this submit ran into is reported now, not inside the next call
#endif
        }
    }
    void clearBindGroups() {
        for (auto& [key, group] : bindGroups_)
            wgpuBindGroupRelease(group);
        bindGroups_.clear();
    }

    uint32_t targetWidth() const {
        const auto* t = color_ ? color_ : depth_;
        return t ? std::max(1u, t->width() >> colorMip_) : 1;
    }
    uint32_t targetHeight() const {
        const auto* t = color_ ? color_ : depth_;
        return t ? std::max(1u, t->height() >> colorMip_) : 1;
    }

    PipelineKey currentKey() const {
        PipelineKey key;
        key.color = color_ ? wgpuFormat(color_->desc().format) : WGPUTextureFormat_Undefined;
        key.depth = depth_ ? wgpuFormat(depth_->desc().format) : WGPUTextureFormat_Undefined;
        key.blend = blend_;
        key.depthState = depth_ ? depthState_ : DepthState{};
        key.topology = topology_;
        return key;
    }
    WGPURenderPipeline pipeline() {
        const PipelineKey key = currentKey();
        if (program_->desc.backgroundPipelines) { // only what the worker has finished (pipelineState starts it)
            std::lock_guard lock(program_->mutex);
            const auto found = program_->pipelines.find(key);
            return found != program_->pipelines.end() ? found->second : nullptr;
        }
        if (auto found = program_->pipelines.find(key); found != program_->pipelines.end())
            return found->second;
        WGPURenderPipeline p = buildPipeline(*program_, key);
        program_->pipelines.emplace(key, p);
        return p;
    }
    // The pipeline of `key` for `program`. Reads only immutable program state and the device: callable from the
    // pipeline worker.
    WGPURenderPipeline buildPipeline(const WProgram& program, const PipelineKey& key) const {
        const auto& d = program.desc;
        static constexpr WGPUVertexFormat kFloat[4] = {WGPUVertexFormat_Float32, WGPUVertexFormat_Float32x2,
                                                       WGPUVertexFormat_Float32x3, WGPUVertexFormat_Float32x4};
        static constexpr WGPUVertexFormat kUint[4] = {WGPUVertexFormat_Uint32, WGPUVertexFormat_Uint32x2,
                                                      WGPUVertexFormat_Uint32x3, WGPUVertexFormat_Uint32x4};
        static constexpr WGPUVertexFormat kSint[4] = {WGPUVertexFormat_Sint32, WGPUVertexFormat_Sint32x2,
                                                      WGPUVertexFormat_Sint32x3, WGPUVertexFormat_Sint32x4};
        std::vector<WGPUVertexAttribute> attributes;
        for (const auto& a : d.attributes) {
            auto attribute = WGPU_VERTEX_ATTRIBUTE_INIT;
            attribute.shaderLocation = a.location;
            attribute.offset = a.offset;
            const uint32_t n = std::clamp(a.components, 1u, 4u) - 1u;
            attribute.format = a.type == ProgramDesc::AttributeType::Uint   ? kUint[n]
                               : a.type == ProgramDesc::AttributeType::Sint ? kSint[n]
                                                                            : kFloat[n];
            attributes.push_back(attribute);
        }
        auto vertexLayout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
        vertexLayout.arrayStride = d.vertexStride;
        vertexLayout.attributeCount = attributes.size();
        vertexLayout.attributes = attributes.data();
        vertexLayout.stepMode = WGPUVertexStepMode_Vertex;

        const BlendState& blendState = key.blend;
        auto blend = WGPU_BLEND_STATE_INIT;
        blend.color.operation = wgpuOp(blendState.op);
        blend.color.srcFactor = wgpuFactor(blendState.src);
        blend.color.dstFactor = wgpuFactor(blendState.dst);
        blend.alpha.operation = wgpuOp(blendState.alphaOp);
        blend.alpha.srcFactor = wgpuFactor(blendState.alphaSrc);
        blend.alpha.dstFactor = wgpuFactor(blendState.alphaDst);
        // Min / Max ignore the factors but WebGPU requires them to be One.
        for (WGPUBlendComponent* c : {&blend.color, &blend.alpha})
            if (c->operation == WGPUBlendOperation_Min || c->operation == WGPUBlendOperation_Max)
                c->srcFactor = c->dstFactor = WGPUBlendFactor_One;
        auto colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format = key.color;
        colorTarget.blend = blendState.enable ? &blend : nullptr;
        colorTarget.writeMask = blendState.writeMask;
        const bool hasColor = key.color != WGPUTextureFormat_Undefined;
        const bool hasDepth = key.depth != WGPUTextureFormat_Undefined;
        auto fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = program.module;
        fragment.entryPoint = view(d.fragmentEntry.c_str());
        fragment.targetCount = hasColor ? 1 : 0;
        fragment.targets = &colorTarget;

        auto depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        depthStencil.format = key.depth;
        depthStencil.depthWriteEnabled =
            key.depthState.enable && key.depthState.write ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        depthStencil.depthCompare =
            key.depthState.enable ? wgpuCompare(key.depthState.compare) : WGPUCompareFunction_Always;

        auto desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        desc.label = view(d.label);
        desc.layout = program.layout;
        desc.vertex.module = program.module;
        desc.vertex.entryPoint = view(d.vertexEntry.c_str());
        if (d.vertexInput && d.vertexStride) {
            desc.vertex.bufferCount = 1;
            desc.vertex.buffers = &vertexLayout;
        }
        desc.primitive.topology = key.topology == Topology::LineList    ? WGPUPrimitiveTopology_LineList
                                  : key.topology == Topology::PointList ? WGPUPrimitiveTopology_PointList
                                                                        : WGPUPrimitiveTopology_TriangleList;
        desc.primitive.cullMode = WGPUCullMode_None;
        desc.depthStencil = hasDepth ? &depthStencil : nullptr;
        // A fragment stage is still needed for depth-only programs that write frag_depth.
        desc.fragment = (hasColor || d.writesDepth) ? &fragment : nullptr;
        return wgpuDeviceCreateRenderPipeline(device_, &desc);
    }

    // ---- the pipeline worker (ProgramDesc::backgroundPipelines) ----
    struct PipelineJob {
        std::shared_ptr<WProgram> program;
        PipelineKey key;
    };
    void startPipelineWorker() {
        if (pipelineThread_.joinable())
            return;
        pipelineThread_ = std::thread([this] {
            for (;;) {
                PipelineJob job;
                {
                    std::unique_lock lock(pipelineMutex_);
                    pipelineWake_.wait(lock, [&] { return pipelineStop_ || !pipelineJobs_.empty(); });
                    if (pipelineStop_)
                        return;
                    job = std::move(pipelineJobs_.front());
                    pipelineJobs_.pop_front();
                }
                WGPURenderPipeline p = nullptr;
                if (!gone()) {
                    const auto t0 = std::chrono::steady_clock::now();
                    p = buildCheckedPipeline(*job.program, job.key);
                    const double ms =
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                    std::fprintf(stderr, "[rhi] pipeline '%s' %s after %.0f ms in the background\n",
                                 job.program->desc.label ? job.program->desc.label : "", p ? "ready" : "failed", ms);
                }
                // GSR_TEST_PIPELINE_DELAY_MS=<n> (tests only): hold a finished pipeline back for n ms before it is
                // published, so the render thread keeps drawing while the pipeline is pending (a stress test of the
                // hand-over window).
                static const int delayMs = [] {
                    const char* env = std::getenv("GSR_TEST_PIPELINE_DELAY_MS");
                    return env ? std::atoi(env) : 0;
                }();
                if (delayMs > 0)
                    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
                {
                    std::lock_guard lock(job.program->mutex);
                    job.program->building.erase(job.key);
                    job.program->pipelines.emplace(job.key, p);
                }
                job.program.reset(); // before the count says idle: a waiter may release the program next
                std::lock_guard lock(pipelineMutex_);
                --pipelinesPending_;
                if (!pipelinesPending_)
                    pipelineIdle_.notify_all();
            }
        });
    }
    // buildPipeline inside error scopes (per thread in wgpu-native): a refused pipeline is null and logged here
    // instead of becoming an uncaptured error of the device.
    WGPURenderPipeline buildCheckedPipeline(const WProgram& program, const PipelineKey& key) const {
#if defined(DRAKAN_WGPU_NATIVE)
        wgpuDevicePushErrorScope(device_, WGPUErrorFilter_OutOfMemory);
        wgpuDevicePushErrorScope(device_, WGPUErrorFilter_Validation); // wgpu-native has no Internal filter
#endif
        WGPURenderPipeline p = buildPipeline(program, key);
#if defined(DRAKAN_WGPU_NATIVE)
        struct Scope {
            std::atomic<int> done{0};
            std::string message;
        } scopes[2];
        for (Scope& scope : scopes) {
            auto callback = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
            callback.mode = WGPUCallbackMode_AllowSpontaneous;
            callback.userdata1 = &scope;
            callback.callback = [](WGPUPopErrorScopeStatus, WGPUErrorType type, WGPUStringView message, void* user,
                                   void*) {
                auto& s = *static_cast<Scope*>(user);
                if (type != WGPUErrorType_NoError)
                    s.message = message.data ? std::string(message.data, message.length == WGPU_STRLEN
                                                                             ? std::strlen(message.data)
                                                                             : message.length)
                                             : std::string("error");
                s.done = 1;
            };
            wgpuDevicePopErrorScope(device_, callback);
        }
        // wgpu-native answers a pop at once; a scope that has not answered is treated as an error.
        std::string error;
        for (Scope& scope : scopes)
            if (!scope.done.load())
                error = "no answer from the error scope";
            else if (!scope.message.empty())
                error = scope.message;
        if (!error.empty()) {
            std::fprintf(stderr, "[rhi] pipeline '%s': %s\n", program.desc.label ? program.desc.label : "",
                         error.c_str());
            if (p)
                wgpuRenderPipelineRelease(p);
            p = nullptr;
        }
#endif
        return p;
    }
    void stopPipelineWorker() {
        {
            std::lock_guard lock(pipelineMutex_);
            pipelineStop_ = true;
            pipelinesPending_ -= uint32_t(pipelineJobs_.size());
            pipelineJobs_.clear();
        }
        pipelineWake_.notify_all();
        if (pipelineThread_.joinable())
            pipelineThread_.join();
        std::lock_guard lock(pipelineMutex_);
        pipelinesPending_ = 0;
        pipelineIdle_.notify_all();
    }

    // The draw data (Device::drawDataPath): a read-only storage buffer in the vertex stage where the device allows
    // one (every core WebGPU device: maxStorageBuffersPerShaderStage applies to the vertex stage too; a
    // compatibility-mode device may report none there), else an rgba32uint texture read with textureLoad.
    // GSR_VERTEX_CONSTANTS=texture takes the texture path where both exist (diagnosis).
    void createDrawData() {
        const WGPULimits& l = context_.limits();
        const char* ask = std::getenv("GSR_VERTEX_CONSTANTS");
        const bool storage =
            l.maxStorageBuffersPerShaderStage >= 1 && l.maxStorageBufferBindingSize >= kDrawDataRingBytes;
        const uint32_t textureRows = uint32_t(kDrawDataRingBytes / (uint64_t(kDrawDataTextureWidth) * 16u));
        const bool texture =
            l.maxTextureDimension2D >= kDrawDataTextureWidth && l.maxSampledTexturesPerShaderStage >= 1;
        const bool wantTexture = ask && !std::strcmp(ask, "texture");
        DrawDataPath path = storage && !wantTexture ? DrawDataPath::StorageBuffer
                            : texture               ? DrawDataPath::Texture
                            : storage               ? DrawDataPath::StorageBuffer
                                                    : DrawDataPath::None;
        if (path == DrawDataPath::StorageBuffer) {
            drawData_.create(device_, kDrawDataRingBytes, WGPUBufferUsage_Storage, "gs draw data");
            if (!drawData_.buffer)
                path = DrawDataPath::None;
        } else if (path == DrawDataPath::Texture) {
            auto desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
            desc.label = view("gs draw data");
            desc.size = {kDrawDataTextureWidth, textureRows, 1};
            desc.format = WGPUTextureFormat_RGBA32Uint;
            desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            drawTexture_ = wgpuDeviceCreateTexture(device_, &desc);
            if (drawTexture_) {
                drawTextureView_ = wgpuTextureCreateView(drawTexture_, nullptr);
                drawData_.cpu.resize(kDrawDataRingBytes);
            } else {
                path = DrawDataPath::None;
            }
        }
        drawDataPath_ = path;
    }
    void uploadDrawData() {
        if (!drawData_.used)
            return;
        if (drawDataPath_ == DrawDataPath::StorageBuffer) {
            drawData_.upload(queue_);
            return;
        }
        if (drawDataPath_ == DrawDataPath::Texture) {
            const uint32_t rowBytes = kDrawDataTextureWidth * 16u;
            const uint32_t rows = uint32_t((drawData_.used + rowBytes - 1) / rowBytes);
            auto destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            destination.texture = drawTexture_;
            auto layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
            layout.bytesPerRow = rowBytes;
            layout.rowsPerImage = rows;
            const WGPUExtent3D size{kDrawDataTextureWidth, rows, 1};
            wgpuQueueWriteTexture(queue_, &destination, drawData_.cpu.data(), uint64_t(rows) * rowBytes, &layout,
                                  &size);
        }
        drawData_.used = 0;
    }

    WGPUBindGroup bindGroup() {
        const auto& d = program_->desc;
        GroupKey key{program_->id, {}, {}, {}};
        for (uint32_t i = 0; i < std::min(d.textureSlots, kMaxTextureSlots); ++i)
            key.textures[i] = textureKeys_[i];
        const bool isCompute = program_->compute != nullptr;
        if (isCompute) {
            for (uint32_t i = 0; i < std::min(d.storageBuffers, kMaxBufferSlots); ++i)
                key.buffers[i] = bufferKeys_[i];
            for (uint32_t i = 0; i < std::min(d.storageTextures, kMaxStorageTextureSlots); ++i)
                key.storage[i] = storageKeys_[i];
        }
        if (auto found = bindGroups_.find(key); found != bindGroups_.end())
            return found->second;
        std::vector<WGPUBindGroupEntry> entries;
        for (uint32_t i = 0; i < std::min(d.uniformSlots, kMaxUniformSlots); ++i) {
            auto e = WGPU_BIND_GROUP_ENTRY_INIT;
            e.binding = i;
            e.buffer = uniforms_.buffer;
            e.size = kUniformSlotBytes;
            entries.push_back(e);
        }
        for (uint32_t i = 0; i < std::min(d.textureSlots, kMaxTextureSlots); ++i) {
            if (!textures_[i])
                return nullptr; // an unbound slot: WebGPU has no null bindings
            auto e = WGPU_BIND_GROUP_ENTRY_INIT;
            e.binding = 8 + i;
            e.textureView = textures_[i];
            entries.push_back(e);
        }
        if (d.samplers)
            for (uint32_t i = 0; i < 2; ++i) {
                auto e = WGPU_BIND_GROUP_ENTRY_INIT;
                e.binding = 16 + i;
                e.sampler = i ? linear_ : point_;
                entries.push_back(e);
            }
        if (!isCompute && d.drawData) {
            auto e = WGPU_BIND_GROUP_ENTRY_INIT;
            e.binding = kDrawDataBinding;
            if (drawDataPath_ == DrawDataPath::StorageBuffer) {
                e.buffer = drawData_.buffer;
                e.size = kDrawDataRingBytes;
            } else {
                e.textureView = drawTextureView_;
            }
            entries.push_back(e);
        }
        if (isCompute) {
            for (uint32_t i = 0; i < std::min(d.storageBuffers, kMaxBufferSlots); ++i) {
                if (!buffers_[i])
                    return nullptr;
                auto e = WGPU_BIND_GROUP_ENTRY_INIT;
                e.binding = 20 + i;
                e.buffer = buffers_[i];
                e.size = bufferSizes_[i];
                entries.push_back(e);
            }
            for (uint32_t i = 0; i < std::min(d.storageTextures, kMaxStorageTextureSlots); ++i) {
                if (!storage_[i])
                    return nullptr;
                auto e = WGPU_BIND_GROUP_ENTRY_INIT;
                e.binding = 28 + i;
                e.textureView = storage_[i];
                entries.push_back(e);
            }
        }
        auto desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        desc.layout = program_->groupLayout;
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        WGPUBindGroup group = wgpuDeviceCreateBindGroup(device_, &desc);
        bindGroups_.emplace(key, group);
        return group;
    }

    // Open the pass for the current targets and bind pipeline, bind group and dynamic state. False = skip the draw.
    bool prepare() {
        if (!color_ && !depth_)
            return false;
        const uint32_t tw = targetWidth(), th = targetHeight();
        // Clip the scissor to the target; an empty scissor draws nothing.
        const int32_t x0 = std::clamp(scissor_.x, 0, int32_t(tw)), y0 = std::clamp(scissor_.y, 0, int32_t(th));
        const int32_t x1 = std::clamp(int32_t(scissor_.x + int64_t(scissor_.w)), 0, int32_t(tw));
        const int32_t y1 = std::clamp(int32_t(scissor_.y + int64_t(scissor_.h)), 0, int32_t(th));
        if (x1 <= x0 || y1 <= y0)
            return false;
        WGPURenderPipeline p = pipeline();
        WGPUBindGroup group = bindGroup();
        if (!p || !group)
            return false;
        if (!pass_) {
            auto colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            auto depthAttachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
            auto desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            if (color_) {
                colorAttachment.view = color_->view(colorMip_, 1);
                colorAttachment.loadOp = WGPULoadOp_Load;
                colorAttachment.storeOp = WGPUStoreOp_Store;
                desc.colorAttachmentCount = 1;
                desc.colorAttachments = &colorAttachment;
            }
            if (depth_) {
                depthAttachment.view = depth_->view(0, 1);
                depthAttachment.depthLoadOp = WGPULoadOp_Load;
                depthAttachment.depthStoreOp = WGPUStoreOp_Store;
                depthAttachment.depthClearValue = 0.0f; // unused with Load, but browsers require a finite value
                desc.depthStencilAttachment = &depthAttachment;
            }
            pass_ = wgpuCommandEncoderBeginRenderPass(encoder(), &desc);
            viewportDirty_ = scissorDirty_ = true;
            boundPipeline_ = nullptr;
            boundGroup_ = nullptr;
        }
        if (p != boundPipeline_) {
            wgpuRenderPassEncoderSetPipeline(pass_, p);
            boundPipeline_ = p;
        }
        std::array<uint32_t, kMaxUniformSlots> offsets{};
        const uint32_t uniformCount = std::min(program_->desc.uniformSlots, kMaxUniformSlots);
        for (uint32_t i = 0; i < uniformCount; ++i)
            offsets[i] = uniformOffsets_[i];
        wgpuRenderPassEncoderSetBindGroup(pass_, 0, group, uniformCount, offsets.data());
        boundGroup_ = group;
        if (viewportDirty_) {
            if (viewportSet_)
                wgpuRenderPassEncoderSetViewport(pass_, viewport_[0], viewport_[1], viewport_[2], viewport_[3], 0, 1);
            else
                wgpuRenderPassEncoderSetViewport(pass_, 0, 0, float(tw), float(th), 0, 1);
            viewportDirty_ = false;
        }
        if (scissorDirty_ || x0 != appliedScissor_[0] || y0 != appliedScissor_[1] || x1 != appliedScissor_[2] ||
            y1 != appliedScissor_[3]) {
            wgpuRenderPassEncoderSetScissorRect(pass_, uint32_t(x0), uint32_t(y0), uint32_t(x1 - x0),
                                                uint32_t(y1 - y0));
            appliedScissor_ = {x0, y0, x1, y1};
            scissorDirty_ = false;
        }
        return true;
    }

#if defined(DRAKAN_WGPU_NATIVE)
    WGPUSubmissionIndex lastSubmission_ = 0, presentSubmission_ = 0;
    bool haveSubmission_ = false, havePresent_ = false;
    bool computeThisFrame_ = false, computeLastFrame_ = false;
    uint64_t presentsSubmitted_ = 0;
    // Presented frames the GPU has finished; shared with the completion callbacks, which may outlive this object.
    std::shared_ptr<std::atomic<uint64_t>> presentsDone_ = std::make_shared<std::atomic<uint64_t>>(0);
#endif
    std::unique_ptr<drakan::gpu::Context> ownedContext_; // devices from open()
    TexturePtr backBuffer_;                              // browser: the frame before it is copied to the canvas
    std::unique_ptr<drakan::gpu::Surface> surface_;
    drakan::gpu::Context& context_;
    WGPUDevice device_;
    WGPUQueue queue_ = nullptr;
    WGPUCommandEncoder encoder_ = nullptr;
    WGPURenderPassEncoder pass_ = nullptr;
    WGPUSampler point_ = nullptr, linear_ = nullptr;
    WGPUQuerySet timingQueries_ = nullptr; // dispatch timing: begin / end per timed dispatch
    WGPUBuffer timingResolve_ = nullptr, timingRead_ = nullptr;
    bool timing_ = false, timingBusy_ = false;
    uint32_t timed_ = 0;
    Ring uniforms_, vertices_, uploads_;
    Ring drawData_; // Device::writeDrawData: the storage buffer, or the CPU arena of drawTexture_
    DrawDataPath drawDataPath_ = DrawDataPath::None;
    WGPUTexture drawTexture_ = nullptr;
    WGPUTextureView drawTextureView_ = nullptr;
    std::thread pipelineThread_;
    std::mutex pipelineMutex_;
    std::condition_variable pipelineWake_, pipelineIdle_;
    std::deque<PipelineJob> pipelineJobs_;
    uint32_t pipelinesPending_ = 0;
    bool pipelineStop_ = false;
    ProgramPtr depthProgram_;

    WTexture* color_ = nullptr;
    WTexture* depth_ = nullptr;
    uint32_t colorMip_ = 0;
    WProgram* program_ = nullptr;
    BlendState blend_;
    DepthState depthState_;
    Topology topology_ = Topology::TriangleList;
    std::array<float, 4> viewport_{};
    bool viewportSet_ = false, viewportDirty_ = true, scissorDirty_ = true;
    Rect scissor_{0, 0, 1u << 15, 1u << 15};
    std::array<int32_t, 4> appliedScissor_{-1, -1, -1, -1};
    std::array<uint32_t, kMaxUniformSlots> uniformOffsets_{};
    std::array<WGPUTextureView, kMaxTextureSlots> textures_{};
    std::array<uint64_t, kMaxTextureSlots> textureKeys_{};
    std::array<WGPUBuffer, kMaxBufferSlots> buffers_{};
    std::array<uint64_t, kMaxBufferSlots> bufferKeys_{}, bufferSizes_{};
    std::array<WGPUTextureView, kMaxStorageTextureSlots> storage_{};
    std::array<uint64_t, kMaxStorageTextureSlots> storageKeys_{};
    WGPURenderPipeline boundPipeline_ = nullptr;
    WGPUBindGroup boundGroup_ = nullptr;
    struct GroupKey {
        uint64_t program; // WProgram::id
        std::array<uint64_t, kMaxTextureSlots> textures;
        std::array<uint64_t, kMaxBufferSlots> buffers;
        std::array<uint64_t, kMaxStorageTextureSlots> storage;
        bool operator<(const GroupKey& o) const { return std::memcmp(this, &o, sizeof(*this)) < 0; }
    };
    std::map<GroupKey, WGPUBindGroup> bindGroups_;
};

} // namespace

std::unique_ptr<Device> Device::create(drakan::gpu::Context& context) {
    return std::make_unique<WDevice>(context);
}

namespace {
// The window surface for a new context; failures leave the device headless (reported, not fatal).
std::unique_ptr<Device> finishOpen(std::unique_ptr<drakan::gpu::Context> context, const OpenDesc& desc) {
    std::unique_ptr<drakan::gpu::Surface> surface;
    if (desc.window.kind != drakan::gpu::NativeWindow::Kind::None) {
        std::string why;
        surface = context->createSurface(desc.window, &why);
        if (!surface) {
            std::fprintf(stderr, "[rhi] no window surface: %s\n", why.c_str());
        } else {
            uint32_t w = desc.width, h = desc.height;
            if ((!w || !h) && !drakan::gpu::windowClientSize(desc.window, w, h)) {
                w = 1280;
                h = 960;
            }
            surface->configure(w, h, desc.vsync);
        }
    }
    drakan::gpu::Context& ref = *context;
    return std::make_unique<WDevice>(ref, std::move(context), std::move(surface));
}
} // namespace

void openDirectAsync(const OpenDesc& desc, OpenCallback done) {
    drakan::gpu::ContextOptions options = desc.options;
    options.compatibleWith = desc.window.kind != drakan::gpu::NativeWindow::Kind::None ? &desc.window : nullptr;
    drakan::gpu::Context::createAsync(
        options,
        [desc, done = std::move(done)](std::unique_ptr<drakan::gpu::Context> context, const std::string& error) {
            if (!context)
                return done({}, error.empty() ? "no WebGPU device" : error);
            done(finishOpen(std::move(context), desc), {});
        });
}

std::unique_ptr<Device> Device::open(const OpenDesc& desc, std::string* error) {
#if !defined(DRAKAN_WGPU_NATIVE)
    return openProxy(desc, error); // the browser: a GPU thread owns the device
#else
    const char* proxy = std::getenv("DRAKAN_RHI_PROXY");
    if (desc.proxy || (proxy && *proxy == '1'))
        return openProxy(desc, error);
    std::unique_ptr<Device> device;
    std::string why;
    openDirectAsync(desc, [&](std::unique_ptr<Device> d, const std::string& e) { // completes synchronously natively
        device = std::move(d);
        why = e;
    });
    if (!device && error)
        *error = why;
    return device;
#endif
}

} // namespace gsr::rhi
