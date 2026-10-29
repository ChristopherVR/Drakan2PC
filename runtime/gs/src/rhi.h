// The GS renderer's GPU interface: an immediate-mode device with exactly the operations Backend::Impl needs (render
// targets, depth buffers, texture uploads / copies, state-keyed draws, constants, readback), implemented once on
// webgpu.h (rhi_webgpu.cpp) for Windows, Linux and the browser. See docs/proposals/webgpu-renderer.md.
//
// Ordering: every operation takes effect in call order, as on an immediate context. Uploads and copies are recorded in
// the command stream (not queue writes), so a draw recorded earlier still reads the previous contents.
//
// Threads: a browser thread that owns WebGPU objects can never block (its readbacks complete through its event loop),
// while the GS emulation reads back synchronously. Device::open therefore returns, in the browser, a proxy that records
// every call into ordered batches for a dedicated GPU thread (rhi_proxy.cpp); natively it is the device itself
// (DRAKAN_RHI_PROXY=1 selects the proxy there too, for tests).
#pragma once
#include <gpu/Gpu.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gsr::rhi {

enum class Format : uint8_t { RGBA8, BGRA8, R32F, RG16F, RGBA16F, Depth32F };

enum Usage : uint32_t {
    Sampled = 1u << 0,      // shader input
    RenderTarget = 1u << 1, // colour or depth attachment
    CopySrc = 1u << 2,
    CopyDst = 1u << 3,
    Storage = 1u << 4, // written by compute programs (texture_storage_2d)
};

struct TextureDesc {
    uint32_t width = 1, height = 1, mips = 1;
    Format format = Format::RGBA8;
    uint32_t usage = Sampled | CopyDst;
    const char* label = nullptr;
};

class Texture {
public:
    virtual ~Texture() = default;
    const TextureDesc& desc() const { return desc_; }
    uint32_t width() const { return desc_.width; }
    uint32_t height() const { return desc_.height; }

protected:
    TextureDesc desc_;
};
using TexturePtr = std::shared_ptr<Texture>;

// A storage buffer for compute programs (contents written with Device::writeBuffer).
class Buffer {
public:
    virtual ~Buffer() = default;
    uint64_t size() const { return size_; }

protected:
    uint64_t size_ = 0;
};
using BufferPtr = std::shared_ptr<Buffer>;

// ---- fixed-function state (the subset of D3D11 the GS pipeline uses) ----
enum class BlendFactor : uint8_t {
    Zero,
    One,
    Src1Color,
    OneMinusSrc1Color,
    Src1Alpha,
    OneMinusSrc1Alpha,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha
};
enum class BlendOp : uint8_t { Add, Subtract, ReverseSubtract, Min, Max };
enum class Compare : uint8_t { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
enum class Topology : uint8_t { TriangleList, LineList, PointList };

struct BlendState {
    bool enable = false;
    BlendOp op = BlendOp::Add;
    BlendFactor src = BlendFactor::One, dst = BlendFactor::Zero;
    BlendOp alphaOp = BlendOp::Add;
    BlendFactor alphaSrc = BlendFactor::One, alphaDst = BlendFactor::Zero;
    uint8_t writeMask = 0xf; // R=1 G=2 B=4 A=8
    bool operator==(const BlendState&) const = default;
};

struct DepthState {
    bool enable = false; // false = no depth test and no write
    Compare compare = Compare::Always;
    bool write = false;
    bool operator==(const DepthState&) const = default;
};

// A shader program: WGSL with a vertex and a fragment entry point, and its bindings, all in @group(0):
//   @binding(0 .. uniformSlots-1)                     uniform buffers (one struct each; dynamic offsets)
//   @binding(8 .. 8+textureSlots-1)                   texture_2d<f32> (filterable) or, for depthTextureMask bits,
//                                                     texture_depth_2d
//   @binding(16) sampler (point), @binding(17) sampler (linear) when samplers = true
// A compute program (computeEntry set) has no vertex / fragment stage and, besides the bindings above,
//   @binding(20 .. 20+storageBuffers-1)   var<storage, read>, or read_write for readWriteMask bits
//   @binding(28 .. 28+storageTextures-1)  texture_storage_2d<storageFormat, write>
struct ProgramDesc {
    std::string wgsl;
    std::string vertexEntry = "vs", fragmentEntry = "fs";
    uint32_t uniformSlots = 0;
    uint32_t textureSlots = 0;
    uint32_t depthTextureMask = 0;
    uint32_t unfilterableMask = 0; // textures read only with textureLoad (e.g. R32F)
    bool samplers = false;
    bool vertexInput = true;   // false = fullscreen-style programs using @builtin(vertex_index) only
    uint32_t vertexStride = 0; // bytes per vertex
    // vertex attributes: (shaderLocation, offset bytes, float components 1..4)
    struct Attribute {
        uint32_t location, offset, components;
    };
    std::vector<Attribute> attributes;
    bool dualSource = false;  // the fragment writes @blend_src(0/1) (needs Device::dualSourceBlending())
    bool writesDepth = false; // fragment writes @builtin(frag_depth)
    std::string computeEntry; // non-empty = compute program
    uint32_t storageBuffers = 0, readWriteMask = 0, storageTextures = 0;
    Format storageFormat = Format::RGBA8;
    const char* label = nullptr;
};

class Program {
public:
    virtual ~Program() = default;
};
using ProgramPtr = std::shared_ptr<Program>;

struct Rect {
    int32_t x = 0, y = 0;
    uint32_t w = 0, h = 0;
};

using ReadCallback = drakan::gpu::ReadCallback;

// A device that owns its context and, when `window` is set, the window's surface.
struct OpenDesc {
    drakan::gpu::ContextOptions options; // options.compatibleWith is filled from `window`
    drakan::gpu::NativeWindow window;    // Kind::None = headless
    uint32_t width = 0, height = 0;      // initial surface size (0 = query the window)
    bool vsync = true;
    bool proxy = false; // force the threaded proxy (the browser always uses it)
};

struct SurfaceInfo {
    uint32_t width = 0, height = 0;
    Format format = Format::BGRA8;
    bool copySource = false; // the images can be copied from (screenshots, overlay blur)
};

class Device {
public:
    // A device on an existing context (tests; the context must outlive it).
    static std::unique_ptr<Device> create(drakan::gpu::Context& context);
    // A device with its own context (and surface): the renderer's entry point. Null with *error on failure.
    static std::unique_ptr<Device> open(const OpenDesc& desc, std::string* error = nullptr);
    virtual ~Device() = default;

    virtual std::string adapterName() const = 0;
    virtual bool dualSourceBlending() const = 0;
    virtual uint32_t maxTextureSize() const = 0;
    // Compute programs with this many storage buffers of this size are available (false on GL-class backends).
    virtual bool compute(uint32_t storageBuffers, uint64_t bufferBytes) const = 0;

    // ---- window surface (devices from open() with a window) ----
    virtual bool hasSurface() const { return false; }
    virtual bool configureSurface(uint32_t w, uint32_t h, bool vsync) { return false; }
    virtual SurfaceInfo surfaceInfo() const { return {}; }
    // This frame's image as a render target (null without a surface). Valid until present().
    virtual TexturePtr acquireSurface() { return {}; }
    virtual void present() {}

    // ---- resources ----
    virtual TexturePtr createTexture(const TextureDesc& desc) = 0;
    // A texture owned elsewhere (a WGPUTexture, e.g. the window surface's current image): not destroyed with the
    // wrapper; valid while its owner keeps it.
    virtual TexturePtr wrapTexture(void* wgpuTexture, const TextureDesc& desc) = 0;
    virtual ProgramPtr createProgram(const ProgramDesc& desc, std::string* error = nullptr) = 0;
    virtual BufferPtr createBuffer(uint64_t bytes, const char* label = nullptr) = 0;
    // Ordered with draws and dispatches, like writeTexture. Offset and size are multiples of 4.
    virtual void writeBuffer(Buffer& buffer, uint64_t offset, const void* data, uint64_t bytes) = 0;
    // Rows of `rowBytes` bytes (tightly packed when 0). Ordered with draws.
    virtual void writeTexture(Texture& texture, uint32_t mip, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                              const void* data, uint32_t rowBytes = 0) = 0;
    virtual void copyTexture(Texture& source, uint32_t sourceMip, uint32_t sx, uint32_t sy, Texture& dest,
                             uint32_t destMip, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) = 0;
    virtual void clearColor(Texture& target, const float rgba[4]) = 0;
    virtual void clearDepth(Texture& target, float depth) = 0;
    // Synchronous: submits everything recorded so far and waits for the copy. Tight rows of texel size bytes. Not on a
    // browser GPU thread (the proxy turns it into readTextureAsync there).
    virtual bool readTexture(Texture& texture, uint32_t mip, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                             std::vector<uint8_t>& out) = 0;
    // Submits everything recorded so far; `done` runs once the pixels are on the CPU (natively from poll()).
    virtual void readTextureAsync(Texture& texture, uint32_t mip, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                  ReadCallback done) = 0;
    // Process completion callbacks (native); wait = until submitted work finished.
    virtual void poll(bool wait) = 0;

    // ---- draw state (sticky until changed, like an immediate context) ----
    // Colour target (nullable = depth only) and depth target (nullable). Changing targets ends the current pass.
    virtual void setTargets(Texture* color, Texture* depth, uint32_t colorMip = 0) = 0;
    virtual void setViewport(float x, float y, float w, float h) = 0;
    virtual void setScissor(const Rect& rect) = 0; // clipped to the target
    virtual void setProgram(Program* program) = 0;
    virtual void setBlend(const BlendState& blend) = 0;
    virtual void setDepth(const DepthState& depth) = 0;
    virtual void setTopology(Topology topology) = 0;
    virtual void setUniforms(uint32_t slot, const void* data, uint32_t bytes) = 0; // copied now
    virtual void setTexture(uint32_t slot, Texture* texture, uint32_t mip = 0, uint32_t mipCount = ~0u) = 0;
    // Vertices are copied now; draws `count` vertices with the current state.
    virtual void draw(const void* vertices, uint32_t count) = 0;
    // Program without vertex input: draws `count` vertices (fullscreen triangle = 3).
    virtual void drawNoInput(uint32_t count) = 0;
    // Compute: the current (compute) program with the current uniforms, textures, buffers and storage textures.
    virtual void setBuffer(uint32_t slot, Buffer* buffer) = 0;
    virtual void setStorageTexture(uint32_t slot, Texture* texture) = 0;
    virtual void dispatch(uint32_t x, uint32_t y, uint32_t z = 1) = 0;

    // Submit recorded work (frame end / before handing a texture to another API).
    virtual void flush() = 0;
};

uint32_t texelBytes(Format format);

} // namespace gsr::rhi
