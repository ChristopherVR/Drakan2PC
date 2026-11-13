// The WGSL of a vertex-program draw on WebGPU (src/vertex_program_wgsl_draw.h) in a VERTEX stage, against the CPU
// evaluation and against the renderer's ordinary vertex shader: the vertex-stage twin of
// tests/test_vertex_stage_webgpu.cpp (a compute pass) and of the entry point check of tests/test_vertex_program.cpp.
//
// Every vertex of tests/vertex_program_cases.h is drawn as one point onto its own pixel of two rgba32uint targets;
// probe entry points around the functions the draw's entry point is made of pass what they computed as flat
// integer varyings, and the fragment stage writes two of them per pass:
//   * the packet (place, colour, stq) and the position again with the previous frame's constants, from integer
//     vertex attributes and the constants in the vertex stage (both paths: a read-only storage buffer and an
//     rgba32uint texture read with textureLoad), against the CPU evaluation, bit for bit;
//   * the output of the renderer's vertex shader body (clip position, colour, stq, fog) on the vertex the stage
//     makes, against the same body fed the CPU-evaluated vertex as float attributes (the vertex buffer the ordinary
//     draw would have), without a jitter, with short binary jitter values and with temporal AA values that are not.
//
// DRAKAN_GPU_BACKEND=vulkan | dx12 picks the graphics API under WebGPU. Exit 77 = no adapter.
// GSR_VP_RECORDS=<file> adds the recorded vertices (vertex_program_cases.h). GSR_VP_BENCH=<repeats> also times the lit
// strip program through the draw's vertex stage (vertices per millisecond).
#include "vertex_program_cases.h"
#include "vertex_program_wgsl_draw.h"

#include <gpu/Gpu.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

using namespace gsr;
using drakan::gpu::view;

namespace {
constexpr uint32_t kSide = 512, kChunk = kSide * kSide; // vertices per draw: one pixel each
constexpr int32_t kOffsetX = 27648, kOffsetY = 29184;   // XYOFFSET of the comparison (12.4)

const char* kProbe = R"WGSL(
struct VpProbe {
    @builtin(position) pos: vec4f,
    @location(0) @interpolate(flat) w0: vec4u,
    @location(1) @interpolate(flat) w1: vec4u,
    @location(2) @interpolate(flat) w2: vec4u,
    @location(3) @interpolate(flat) w3: vec4u,
    @location(4) @interpolate(flat) w4: vec4u,
    @location(5) @interpolate(flat) w5: vec4u,
    @location(6) @interpolate(flat) w6: vec4u,
}
struct ProbeSelect { which: vec4u }
@group(0) @binding(2) var<uniform> probe: ProbeSelect;
fn probeOut(index: u32, raw: VpRaw, previous: vec4u, v: VSOut) -> VpProbe {
    var o: VpProbe;
    let x = f32(index % 512u) + 0.5;
    let y = f32(index / 512u) + 0.5;
    o.pos = vec4f(x / 256.0 - 1.0, 1.0 - y / 256.0, 0.5, 1.0);
    o.w0 = raw.place;
    o.w1 = raw.colour;
    o.w2 = raw.stq;
    o.w3 = previous;
    o.w4 = bitcast<vec4u>(v.pos);
    o.w5 = bitcast<vec4u>(v.col);
    o.w6 = vec4u(bitcast<vec3u>(v.stq), bitcast<u32>(v.fog));
    return o;
}
@vertex fn vsProgramProbe(a: VpAttr, @builtin(vertex_index) index: u32) -> VpProbe {
    let i = vpInOf(a);
    let raw = vpEvaluate(i);
    return probeOut(index, raw, vpEvaluateAtPrevious(i, i.sel.z).place, vpVertexShader(vpVertexOf(raw)));
}
@vertex fn vsPlainProbe(i: VSIn, @builtin(vertex_index) index: u32) -> VpProbe {
    return probeOut(index, VpRaw(vec4u(0u), vec4u(0u), vec4u(0u)), vec4u(0u), vpPlainShader(i));
}
struct ProbeTargets {
    @location(0) a: vec4u,
    @location(1) b: vec4u,
}
@fragment fn fsProbe(p: VpProbe) -> ProbeTargets {
    switch probe.which.x {
        case 0u: { return ProbeTargets(p.w0, p.w1); }
        case 1u: { return ProbeTargets(p.w2, p.w3); }
        case 2u: { return ProbeTargets(p.w4, p.w5); }
        default: { return ProbeTargets(p.w6, vec4u(0u)); }
    }
}
struct VpBench {
    @builtin(position) pos: vec4f,
    @location(0) @interpolate(flat) sum: u32,
}
// GSR_VP_BENCH: the draw's whole vertex stage per point, its output folded into one word so nothing is left out
@vertex fn vsProgramBench(a: VpAttr, @builtin(vertex_index) index: u32) -> VpBench {
    let v = vpVertexShader(vpVertexOf(vpEvaluate(vpInOf(a))));
    var o: VpBench;
    let x = f32(index % 512u) + 0.5;
    let y = f32(index / 512u) + 0.5;
    o.pos = vec4f(x / 256.0 - 1.0, 1.0 - y / 256.0, 0.5, 1.0);
    let p = bitcast<vec4u>(v.pos) ^ bitcast<vec4u>(v.col) ^ vec4u(bitcast<vec3u>(v.stq), bitcast<u32>(v.fog));
    o.sum = p.x ^ p.y ^ p.z ^ p.w;
    return o;
}
@fragment fn fsBench(p: VpBench) -> ProbeTargets {
    return ProbeTargets(vec4u(p.sum), vec4u(0u));
}
)WGSL";

struct PlainVertex { // DrawVertex (src/gs_internal.h): what the ordinary draw's vertex buffer holds
    float x, y, z, fog, r, g, b, a, s, t, q;
};
static_assert(sizeof(PlainVertex) == 44, "the renderer's vertex layout");

struct Gpu {
    drakan::gpu::Context& context;
    WGPUDevice device;
    WGPUBuffer buffer(uint64_t bytes, WGPUBufferUsage usage, const void* data = nullptr) const {
        auto desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = (bytes + 255) & ~255ull;
        desc.usage = usage | WGPUBufferUsage_CopyDst;
        WGPUBuffer b = wgpuDeviceCreateBuffer(device, &desc);
        if (data)
            wgpuQueueWriteBuffer(context.queue(), b, 0, data, (bytes + 3) & ~3ull);
        return b;
    }
    WGPUTexture texture(uint32_t w, uint32_t h, WGPUTextureUsage usage) const {
        auto desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size = {w, h, 1};
        desc.format = WGPUTextureFormat_RGBA32Uint;
        desc.usage = usage;
        return wgpuDeviceCreateTexture(device, &desc);
    }
    bool map(WGPUBuffer b, uint64_t bytes, void* out) const {
        int state = 0;
        auto callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_AllowSpontaneous;
        callback.userdata1 = &state;
        callback.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* user, void*) {
            *static_cast<int*>(user) = status == WGPUMapAsyncStatus_Success ? 1 : 2;
        };
        wgpuBufferMapAsync(b, WGPUMapMode_Read, 0, bytes, callback);
        while (state == 0 && !context.failed())
            context.poll(true);
        const void* data = state == 1 ? wgpuBufferGetConstMappedRange(b, 0, bytes) : nullptr;
        if (data)
            std::memcpy(out, data, bytes);
        if (state == 1)
            wgpuBufferUnmap(b);
        return data != nullptr;
    }
};

double milliseconds() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// One draw path (program probe or plain probe) with its pipeline and bindings.
struct Probe {
    WGPURenderPipeline pipeline = nullptr;
    WGPUBindGroup group = nullptr;
};

WGPURenderPipeline makePipeline(const Gpu& gpu, WGPUShaderModule module, const char* entry, uint32_t stride,
                                const std::vector<WGPUVertexAttribute>& attributes,
                                const char* fragmentEntry = "fsProbe") {
    auto layout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    layout.arrayStride = stride;
    layout.attributeCount = attributes.size();
    layout.attributes = attributes.data();
    WGPUColorTargetState targets[2] = {WGPU_COLOR_TARGET_STATE_INIT, WGPU_COLOR_TARGET_STATE_INIT};
    targets[0].format = targets[1].format = WGPUTextureFormat_RGBA32Uint;
    auto fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = view(fragmentEntry);
    fragment.targetCount = 2;
    fragment.targets = targets;
    auto desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.vertex.module = module;
    desc.vertex.entryPoint = view(entry);
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &layout;
    desc.primitive.topology = WGPUPrimitiveTopology_PointList;
    desc.fragment = &fragment;
    gpu.context.poll(false);
    WGPURenderPipeline p = wgpuDeviceCreateRenderPipeline(gpu.device, &desc);
    gpu.context.poll(false);
    return gpu.context.failed() ? nullptr : p;
}

WGPUBindGroup makeGroup(const Gpu& gpu, WGPURenderPipeline pipeline, const std::vector<WGPUBindGroupEntry>& entries) {
    // The probes use different bindings; an automatic layout has only those an entry point reads.
    WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
    auto desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    WGPUBindGroup g = wgpuDeviceCreateBindGroup(gpu.device, &desc);
    wgpuBindGroupLayoutRelease(layout);
    return g;
}

// Draws `count` points with `probe` and reads the two targets for each pass in `passes` (probe.which = pass): words
// [8 * pass, 8 * pass + 8) of the vertex's 32 in `out`.
bool drawPasses(const Gpu& gpu, const Probe& probe, WGPUBuffer vertices, uint64_t vertexBytes, uint32_t count,
                WGPUBuffer select, WGPUTexture targets[2], WGPUBuffer staging[2],
                std::initializer_list<uint32_t> passes, std::vector<uint32_t>& out, size_t first) {
    std::vector<uint32_t> pixels(size_t(kChunk) * 4);
    for (const uint32_t pass : passes) {
        const uint32_t which[4] = {pass, 0, 0, 0};
        wgpuQueueWriteBuffer(gpu.context.queue(), select, 0, which, sizeof(which));
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu.device, nullptr);
        WGPURenderPassColorAttachment colour[2] = {WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT,
                                                   WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT};
        WGPUTextureView views[2];
        for (int t = 0; t < 2; ++t) {
            views[t] = wgpuTextureCreateView(targets[t], nullptr);
            colour[t].view = views[t];
            colour[t].loadOp = WGPULoadOp_Clear;
            colour[t].storeOp = WGPUStoreOp_Store;
            colour[t].clearValue = {0, 0, 0, 0};
        }
        auto passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 2;
        passDesc.colorAttachments = colour;
        WGPURenderPassEncoder rp = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(rp, probe.pipeline);
        wgpuRenderPassEncoderSetBindGroup(rp, 0, probe.group, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(rp, 0, vertices, 0, vertexBytes);
        wgpuRenderPassEncoderDraw(rp, count, 1, 0, 0);
        wgpuRenderPassEncoderEnd(rp);
        wgpuRenderPassEncoderRelease(rp);
        for (int t = 0; t < 2; ++t) {
            auto from = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            from.texture = targets[t];
            auto to = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
            to.buffer = staging[t];
            to.layout.bytesPerRow = kSide * 16;
            to.layout.rowsPerImage = kSide;
            const WGPUExtent3D size{kSide, kSide, 1};
            wgpuCommandEncoderCopyTextureToBuffer(encoder, &from, &to, &size);
        }
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(gpu.context.queue(), 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);
        for (int t = 0; t < 2; ++t) {
            wgpuTextureViewRelease(views[t]);
            if (!gpu.map(staging[t], uint64_t(kChunk) * 16, pixels.data()))
                return false;
            for (uint32_t i = 0; i < count; ++i)
                std::memcpy(&out[(first + i) * 32 + pass * 8 + t * 4], &pixels[size_t(i) * 4], 16);
        }
    }
    return true;
}

// GSR_VP_BENCH: the lit strip program through the draw's whole vertex stage, in vertices per millisecond.
void bench(const Gpu& gpu, const vpcases::Cases& cases, WGPUShaderModule module,
           const std::vector<WGPUVertexAttribute>& attributes, const std::vector<WGPUBindGroupEntry>& entries,
           WGPUBuffer vertices, WGPUTexture targets[2], WGPUBuffer staging, const char* pathName, int repeats) {
    std::vector<vpcases::GpuVertex> lit;
    for (const vpcases::GpuVertex& v : cases.vertices)
        if (v.program == vp::StripsLit && lit.size() < kChunk)
            lit.push_back(v);
    while (!lit.empty() && lit.size() < kChunk)
        lit.push_back(lit[lit.size() % 1000]);
    Probe b;
    b.pipeline = makePipeline(gpu, module, "vsProgramBench", 64, attributes, "fsBench");
    b.group = b.pipeline ? makeGroup(gpu, b.pipeline, entries) : nullptr;
    if (lit.empty() || !b.group) {
        std::printf("[FAIL] bench set-up\n");
        return;
    }
    wgpuQueueWriteBuffer(gpu.context.queue(), vertices, 0, lit.data(), uint64_t(kChunk) * 64);
    const auto draws = [&](int count) {
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu.device, nullptr);
        WGPUTextureView views[2] = {wgpuTextureCreateView(targets[0], nullptr),
                                    wgpuTextureCreateView(targets[1], nullptr)};
        for (int d = 0; d < count; ++d) {
            WGPURenderPassColorAttachment colour[2] = {WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT,
                                                       WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT};
            for (int t = 0; t < 2; ++t) {
                colour[t].view = views[t];
                colour[t].loadOp = WGPULoadOp_Load;
                colour[t].storeOp = WGPUStoreOp_Store;
            }
            auto passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.colorAttachmentCount = 2;
            passDesc.colorAttachments = colour;
            WGPURenderPassEncoder rp = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
            wgpuRenderPassEncoderSetPipeline(rp, b.pipeline);
            wgpuRenderPassEncoderSetBindGroup(rp, 0, b.group, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(rp, 0, vertices, 0, uint64_t(kChunk) * 64);
            wgpuRenderPassEncoderDraw(rp, kChunk, 1, 0, 0);
            wgpuRenderPassEncoderEnd(rp);
            wgpuRenderPassEncoderRelease(rp);
        }
        auto from = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        from.texture = targets[0];
        auto to = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        to.buffer = staging;
        to.layout.bytesPerRow = 256;
        to.layout.rowsPerImage = 1;
        const WGPUExtent3D one{16, 1, 1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &from, &to, &one);
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(gpu.context.queue(), 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);
        wgpuTextureViewRelease(views[0]);
        wgpuTextureViewRelease(views[1]);
        uint32_t word[64];
        gpu.map(staging, 256, word); // the time covers the finished work
    };
    draws(1);
    std::vector<double> rates;
    constexpr int kDraws = 8;
    for (int r = 0; r < std::max(3, repeats); ++r) {
        const double t0 = milliseconds();
        draws(kDraws);
        rates.push_back(double(kChunk) * kDraws / (milliseconds() - t0));
    }
    std::sort(rates.begin(), rates.end());
    std::printf(
        "lit strip program through the draw's vertex stage (%s) on %s: %.0f vertices per ms (median of %zu runs "
        "of %d x %u points; lowest %.0f, highest %.0f)\n",
        pathName, drakan::gpu::backendName(gpu.context.backend()), rates[rates.size() / 2], rates.size(), kDraws,
        kChunk, rates.front(), rates.back());
    wgpuBindGroupRelease(b.group);
    wgpuRenderPipelineRelease(b.pipeline);
}
} // namespace

int main() {
    drakan::gpu::ContextOptions options;
    options.allowSoftwareAdapter = true;
    std::string error;
    auto context = drakan::gpu::Context::create(options, &error);
    if (!context) {
        std::fprintf(stderr, "SKIP: %s\n", error.c_str());
        return 77;
    }
    const WGPULimits limits = context->limits();
    std::printf("adapter: %s (%s%s); storage buffers per stage %u, binding size %llu, texture side %u\n",
                context->adapterName().c_str(), drakan::gpu::backendName(context->backend()),
                context->fxcShaderCompiler() ? ", shaders through FXC" : "", limits.maxStorageBuffersPerShaderStage,
                (unsigned long long)limits.maxStorageBufferBindingSize, limits.maxTextureDimension2D);
    const Gpu gpu{*context, context->device()};
    if (vertexShaderBody().empty()) {
        std::printf("[FAIL] the renderer's vertex shader `vs` was not found in kWgslGsCommon\n");
        return 1;
    }

    // A machine without a graphics card (a CI runner) gives WebGPU the software rasteriser; the full set takes over two
    // minutes there, so it runs the reduced set (the same kinds of vertices, a tenth of them).
    const bool reduced = context->softwareAdapter();
    if (reduced)
        std::printf("software adapter: reduced case (a tenth of the generated vertices and probes)\n");
    const vpcases::StandardCases standard = vpcases::standardCases(reduced);
    const vpcases::Cases& cases = standard.cases;
    const std::vector<vp::ir::Raw> want = vpcases::reference(cases);
    const std::vector<vpcases::PreviousPlace> previous = vpcases::referencePrevious(cases);
    const size_t n = cases.vertices.size();
    // The ordinary draw's vertices: what gs_core's kick() makes of each packet vertex.
    std::vector<PlainVertex> plain(n);
    for (size_t i = 0; i < n; ++i) {
        const vp::Output o = vp::ir::Evaluator<>::toOutput(want[i]);
        PlainVertex& d = plain[i];
        d.x = ((int)o.x - kOffsetX) * (1.0f / 16.0f);
        d.y = ((int)o.y - kOffsetY) * (1.0f / 16.0f);
        d.z = (float)o.z;
        d.fog = o.fog;
        d.r = o.r;
        d.g = o.g;
        d.b = o.b;
        d.a = o.a;
        std::memcpy(&d.s, &o.s, 4);
        std::memcpy(&d.t, &o.t, 4);
        std::memcpy(&d.q, &o.q, 4);
    }

    const uint64_t constantBytes = cases.constants.size() * sizeof(vp::Constants);
    const uint32_t constantRows = uint32_t(constantBytes / 16);
    const uint32_t textureRows = (constantRows + kVpDrawDataTextureWidth - 1) / kVpDrawDataTextureWidth;
    WGPUBuffer select = gpu.buffer(16, WGPUBufferUsage_Uniform);
    WGPUBuffer cbBuffer = gpu.buffer(192, WGPUBufferUsage_Uniform);
    const int32_t drawWords[4] = {kOffsetX, kOffsetY, 0, 0};
    WGPUBuffer drawBuffer = gpu.buffer(16, WGPUBufferUsage_Uniform, drawWords);
    WGPUBuffer programVertices = gpu.buffer(uint64_t(kChunk) * 64, WGPUBufferUsage_Vertex);
    WGPUBuffer plainVertices = gpu.buffer(uint64_t(kChunk) * sizeof(PlainVertex), WGPUBufferUsage_Vertex);
    WGPUTexture targets[2] = {gpu.texture(kSide, kSide, WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc),
                              gpu.texture(kSide, kSide, WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc)};
    WGPUBuffer staging[2] = {gpu.buffer(uint64_t(kChunk) * 16, WGPUBufferUsage_MapRead),
                             gpu.buffer(uint64_t(kChunk) * 16, WGPUBufferUsage_MapRead)};

    // cb: rt = target size, depth range and scale; g.yz = the NDC jitter of a temporal draw
    const auto setJitter = [&](float jx, float jy) {
        float k[48] = {};
        k[0] = 640.0f;
        k[1] = 448.0f;
        k[2] = 16777215.0f;
        k[3] = 3.0f;
        k[9 * 4 + 1] = jx;
        k[9 * 4 + 2] = jy;
        wgpuQueueWriteBuffer(context->queue(), cbBuffer, 0, k, sizeof(k));
    };
    // The temporal AA's jitter as gs_draw.inl forms it (2 * jitter / render width), for a 1920 x 1080 target and a
    // Halton-like offset: values that are not short binary fractions.
    const float taaX = 2.0f * 0.3721f / 1920.0f, taaY = -2.0f * -0.1913f / 1080.0f;
    struct Jitter {
        const char* name;
        float x, y;
    } const jitters[3] = {{"no jitter", 0.0f, 0.0f},
                          {"a short binary jitter", 0.000732421875f, -0.00048828125f},
                          {"a temporal AA jitter", taaX, taaY}};

    uint64_t failures = 0;
    for (const VpConstantsPath path : {VpConstantsPath::StorageBuffer, VpConstantsPath::Texture}) {
        const char* pathName = path == VpConstantsPath::StorageBuffer ? "storage buffer" : "rgba32uint texture";
        if (path == VpConstantsPath::StorageBuffer &&
            (limits.maxStorageBuffersPerShaderStage < 1 || limits.maxStorageBufferBindingSize < constantBytes)) {
            std::printf("constants in a %s: not available on this device, skipped\n", pathName);
            continue;
        }
        if (path == VpConstantsPath::Texture && textureRows > limits.maxTextureDimension2D) {
            std::printf("constants in a %s: the table does not fit, skipped\n", pathName);
            continue;
        }
        const std::string extra = vertexShaderBody(kWgslGsCommon, "vpPlainShader") + kProbe;
        const std::string source = vertexProgramDrawWgsl(false, path, true, vpcases::testStages(), extra);
        const double start = milliseconds();
        WGPUShaderModule module = context->shaderModule(source, "vertex program probes");
        if (!module) {
            std::printf("[FAIL] the WGSL does not compile (%s): %s\n", pathName, context->lastError().c_str());
            return 1;
        }
        std::vector<WGPUVertexAttribute> programAttributes(4, WGPU_VERTEX_ATTRIBUTE_INIT);
        const WGPUVertexFormat programFormats[4] = {WGPUVertexFormat_Uint32x4, WGPUVertexFormat_Uint32x4,
                                                    WGPUVertexFormat_Sint32x4, WGPUVertexFormat_Uint32x4};
        for (uint32_t a = 0; a < 4; ++a) {
            programAttributes[a].shaderLocation = a;
            programAttributes[a].offset = a * 16;
            programAttributes[a].format = programFormats[a];
        }
        std::vector<WGPUVertexAttribute> plainAttributes(3, WGPU_VERTEX_ATTRIBUTE_INIT);
        const WGPUVertexFormat plainFormats[3] = {WGPUVertexFormat_Float32x4, WGPUVertexFormat_Float32x4,
                                                  WGPUVertexFormat_Float32x3};
        for (uint32_t a = 0; a < 3; ++a) {
            plainAttributes[a].shaderLocation = a;
            plainAttributes[a].offset = a * 16;
            plainAttributes[a].format = plainFormats[a];
        }
        Probe program, ordinary;
        program.pipeline = makePipeline(gpu, module, "vsProgramProbe", 64, programAttributes);
        ordinary.pipeline = makePipeline(gpu, module, "vsPlainProbe", sizeof(PlainVertex), plainAttributes);
        if (!program.pipeline || !ordinary.pipeline) {
            std::printf("[FAIL] render pipeline (%s): %s\n", pathName, context->lastError().c_str());
            return 1;
        }
        std::printf("constants in a %s: WGSL of %zu characters, module and the two probe pipelines in %.0f ms\n",
                    pathName, source.size(), milliseconds() - start);
        WGPUBuffer constants = nullptr;
        WGPUTexture constantTexture = nullptr;
        WGPUTextureView constantView = nullptr;
        auto constantEntry = WGPU_BIND_GROUP_ENTRY_INIT;
        constantEntry.binding = kVpDrawDataBinding;
        if (path == VpConstantsPath::StorageBuffer) {
            constants = gpu.buffer(constantBytes, WGPUBufferUsage_Storage, cases.constants.data());
            constantEntry.buffer = constants;
            constantEntry.size = constantBytes;
        } else {
            constantTexture = gpu.texture(kVpDrawDataTextureWidth, textureRows,
                                          WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst);
            std::vector<uint8_t> rows(size_t(textureRows) * kVpDrawDataTextureWidth * 16, 0);
            std::memcpy(rows.data(), cases.constants.data(), constantBytes);
            auto to = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            to.texture = constantTexture;
            auto layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
            layout.bytesPerRow = kVpDrawDataTextureWidth * 16;
            layout.rowsPerImage = textureRows;
            const WGPUExtent3D size{kVpDrawDataTextureWidth, textureRows, 1};
            wgpuQueueWriteTexture(context->queue(), &to, rows.data(), rows.size(), &layout, &size);
            constantView = wgpuTextureCreateView(constantTexture, nullptr);
            constantEntry.textureView = constantView;
        }
        std::vector<WGPUBindGroupEntry> entries(3, WGPU_BIND_GROUP_ENTRY_INIT);
        entries[0].binding = 0;
        entries[0].buffer = cbBuffer;
        entries[0].size = 192;
        entries[1].binding = 1;
        entries[1].buffer = drawBuffer;
        entries[1].size = 16;
        entries[2].binding = 2;
        entries[2].buffer = select;
        entries[2].size = 16;
        std::vector<WGPUBindGroupEntry> programEntries = entries;
        programEntries.push_back(constantEntry);
        program.group = makeGroup(gpu, program.pipeline, programEntries);
        ordinary.group = makeGroup(gpu, ordinary.pipeline, {entries[0], entries[2]});

        // 32 words per vertex: packet and previous place (passes 0, 1), then the vertex shader's output (2, 3)
        std::vector<uint32_t> got(n * 32), plainGot(n * 32), plainUnjittered;
        bool first = true;
        for (const Jitter& j : jitters) {
            setJitter(j.x, j.y);
            for (size_t at = 0; at < n; at += kChunk) {
                const uint32_t count = uint32_t(std::min<size_t>(kChunk, n - at));
                wgpuQueueWriteBuffer(context->queue(), programVertices, 0, cases.vertices.data() + at,
                                     uint64_t(count) * 64);
                wgpuQueueWriteBuffer(context->queue(), plainVertices, 0, plain.data() + at,
                                     (uint64_t(count) * sizeof(PlainVertex) + 3) & ~3ull);
                const bool drawn = (first ? drawPasses(gpu, program, programVertices, uint64_t(count) * 64, count,
                                                       select, targets, staging, {0, 1, 2, 3}, got, at)
                                          : drawPasses(gpu, program, programVertices, uint64_t(count) * 64, count,
                                                       select, targets, staging, {2, 3}, got, at)) &&
                                   drawPasses(gpu, ordinary, plainVertices, uint64_t(count) * sizeof(PlainVertex),
                                              count, select, targets, staging, {2, 3}, plainGot, at);
                if (!drawn || context->failed()) {
                    std::printf("[FAIL] draw or read back (%s): %s\n", pathName, context->lastError().c_str());
                    return 1;
                }
            }
            if (first) {
                std::vector<uint32_t> packet(n * 16);
                for (size_t i = 0; i < n; ++i)
                    std::memcpy(&packet[i * 16], &got[i * 32], 64);
                const uint64_t bad = vpcases::compareShader("WGSL vertex stage", cases, want, previous, packet.data());
                std::printf("generated WGSL in a vertex stage (integer attributes, constants in a %s) against the CPU "
                            "evaluation (packet and previous place) on %s: %zu vertices (%zu generated, %zu probes, "
                            "%zu recorded), %llu differ\n",
                            pathName, drakan::gpu::backendName(context->backend()), n, standard.generated,
                            standard.probes, standard.recorded, (unsigned long long)bad);
                failures += bad;
                first = false;
            }
            uint64_t differ = 0;
            for (size_t i = 0; i < n; ++i)
                if (std::memcmp(&got[i * 32 + 16], &plainGot[i * 32 + 16], 48) != 0 && ++differ <= 4) {
                    std::printf("  [FAIL] vertex shader output (%s, %s), vertex %zu:", pathName, j.name, i);
                    for (int w = 0; w < 12; ++w)
                        std::printf(" %08x/%08x", got[i * 32 + 16 + w], plainGot[i * 32 + 16 + w]);
                    std::printf("\n");
                }
            std::printf("the draw's vertex stage against the renderer's vertex shader on the CPU evaluation (%s, %s): "
                        "%zu vertices, %llu differ in clip position, colour, stq or fog\n",
                        pathName, j.name, n, (unsigned long long)differ);
            failures += differ;
            if (plainUnjittered.empty())
                plainUnjittered = plainGot;
        }
        // Control: the comparison sees the temporal AA jitter (the program's jittered output against the ordinary
        // shader's unjittered one), and every output was written (clip w = 1).
        uint64_t seen = 0, written = 0;
        for (size_t i = 0; i < n; ++i) {
            seen += std::memcmp(&got[i * 32 + 16], &plainUnjittered[i * 32 + 16], 16) != 0;
            written += got[i * 32 + 16 + 3] == 0x3F800000u && plainGot[i * 32 + 16 + 3] == 0x3F800000u;
        }
        std::printf("control (%s): the temporal AA jitter changes the clip position of %llu of %zu vertices; %llu "
                    "vertices have clip w = 1 on both sides\n",
                    pathName, (unsigned long long)seen, n, (unsigned long long)written);
        if (seen == 0 || written != n) {
            std::printf("  [FAIL] control\n");
            ++failures;
        }
        if (const char* text = std::getenv("GSR_VP_BENCH"))
            bench(gpu, cases, module, programAttributes, {entries[0], entries[1], constantEntry}, programVertices,
                  targets, staging[0], pathName, std::atoi(text));
        wgpuBindGroupRelease(program.group);
        wgpuBindGroupRelease(ordinary.group);
        wgpuRenderPipelineRelease(program.pipeline);
        wgpuRenderPipelineRelease(ordinary.pipeline);
        wgpuShaderModuleRelease(module);
        if (constants)
            wgpuBufferRelease(constants);
        if (constantView)
            wgpuTextureViewRelease(constantView);
        if (constantTexture)
            wgpuTextureRelease(constantTexture);
    }
    if (context->failed()) {
        std::printf("[FAIL] device error: %s\n", context->lastError().c_str());
        return 1;
    }
    std::printf("%s\n", failures ? "FAILED" : "all vertices equal");
    return failures ? 1 : 0;
}
