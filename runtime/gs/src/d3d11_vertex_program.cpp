// Vertex-program draws on Direct3D 11 (gsr/vertex_program.h, gsr/vertex_batch.h; the rules every backend shares are in
// vertex_program_draw.h, the shader text is generated: vertex_program_hlsl.h).
//
// A batch of records is drawn by a vertex shader that repeats the VU stage with the VU's arithmetic and then runs
// the renderer's ordinary VSMain, so everything after the vertex stage is the code of every other draw. There are two
// vertex programs: the strip stages (VSProgram) and the batch path (VSBatch, a loop over each vertex's passes). This
// file is the API side only: the shaders (each compiled on background threads at the first record of its kind; FXC
// needs 6 to 10 s for the strips and 14 to 30 s for the batch path, once per machine with the shader cache; until a
// kind is ready vertexPrograms() says "not ready" for it and the core evaluates those records on the CPU), the
// buffers, and the bindings gs_draw.inl asks for. Nothing here is latched: the control word is read per record by the
// core, and a record this backend declines is evaluated on the CPU, so the path can go on and off at any frame
// boundary.
//
// A compile that fails turns that kind off for the session; a buffer or the device that fails turns both off. One log
// line each; later records are counted under vpath::Fallback::Internal and evaluated on the CPU.
//
// Diagnosis: GSR_VERTEX_ARITHMETIC=doubles|limbs|off (default: vp::chooseArithmetic, integer limbs),
// GSR_VERTEX_PROGRAM_WAIT=1 (wait for the compile at the first record of a kind instead of drawing on the CPU
// meanwhile: for replays and tests that count shader-drawn vertices; never set in play).
#include "d3d11_backend_impl.h"
#include "vertex_program_dump.h"
#include "vertex_program_hlsl.h"

#include <string>

namespace gsr {

struct D3D11Backend::Impl::ProgramGpu {
    enum State : int { Idle, Compiling, Ready, Failed };
    // One vertex program with its velocity twin. Written by its worker before `state` becomes Ready, read by the
    // render thread after.
    struct Shaders {
        std::atomic<int> state{Idle};
        std::thread worker;
        ComPtr<ID3D11VertexShader> vs, vsMotion;
        ComPtr<ID3D11InputLayout> layout, motionLayout;
        // Strips only: the raw outputs of the same evaluation (VSProgramRaw), streamed out for the ray surfaces.
        ComPtr<ID3D11VertexShader> vsRaw;
        ComPtr<ID3D11GeometryShader> soRaw;
        ComPtr<ID3D11InputLayout> rawLayout;
        bool rawReady = false;
        ~Shaders() {
            if (worker.joinable())
                worker.join();
        }
    } strips, batches;
    bool chosen = false;           // arithmetic and device options looked at (first request)
    std::atomic<bool> lost{false}; // a buffer or the device failed: both kinds off for the session
    vp::Arithmetic arithmetic = vp::Arithmetic::None;
    // Render thread only. Rings like the draw vertex buffer: NO_OVERWRITE appends, DISCARD on wrap.
    struct Ring {
        ComPtr<ID3D11Buffer> buffer;
        uint32_t capacity = 0, offset = 0, drawOffset = 0;
    } vb, ib, previousVb, constants;
    // One ring for what every draw uploads (its constants and the previous ones, vertices and indices: one Map per
    // draw instead of three), bound as vertex, index and shader-resource buffer. Needs NO_OVERWRITE on a
    // shader-resource buffer (constantsAppend); without it, or with GSR_VERTEX_PROGRAM_RINGS=1, the rings `constants`,
    // `vb` and `ib` are used. The previous stream of the velocity pass keeps `previousVb` either way.
    Ring all;
    bool combined = false;
    ComPtr<ID3D11ShaderResourceView> allSrv;
    // Where the draw being drawn lies (set by bindProgramDraw, read by the draw and velocity passes).
    ID3D11Buffer* drawVb = nullptr;
    ID3D11Buffer* drawIb = nullptr;
    uint32_t drawVbOffset = 0, drawFirstIndex = 0;
    // The ray-surface stream (kCaps.programSurfaces): the vertices of the batches whose surfaces wait for them, in the
    // order reserveProgramSurface handed out their ranges, until readProgramSurfaces copies them back.
    struct SurfaceStream {
        ComPtr<ID3D11Buffer> buffer, staging;
        // GSR_VERTEX_SURFACE=late: the copy of one world's stream, mapped at the end of the next world.
        ComPtr<ID3D11Buffer> lateStaging;
        uint32_t lateCount = 0;
        bool lateValid = false;
        uint32_t capacity = 0, used = 0; // in vertices
        bool failed = false;
        std::vector<surface::RawVertex> read;
        // The batch being drawn: its range, and whether drawProgramVertices streamed it yet.
        bool pending = false, streamed = false;
        uint32_t first = 0, count = 0;
        uint64_t batches = 0, vertices = 0, cpuFallbacks = 0, reads = 0;
    } surfaceStream;
    ComPtr<ID3D11ShaderResourceView> constantsSrv;
    ComPtr<ID3D11Buffer> cb, cbMotion;
    bool constantsAppend = false; // the device maps a shader-resource buffer with NO_OVERWRITE (Direct3D 11.1)
    std::atomic<bool> logged{false};
    // The batch being drawn.
    std::vector<motion::ProgramPrevious> previous;
    std::vector<vp::Constants> previousConstants;
    std::vector<motion::BatchPrevious> batchPrevious;
    std::vector<vp::Pass> previousPasses;
    std::vector<DrawVertex> fallback;
    program_draw::BoundsScratch boundsScratch;
    uint64_t boundsExact = 0, boundsVertices = 0; // vertices programBounds evaluated exactly, of all it bounded
    bool motionTracked = false;
    uint64_t batches_ = 0, velocityBatches = 0;

    // GSR_VP_BOUNDS_DUMP=<file>: the first strip batches drawn, for the bounds tests (vertex_program_dump.h)
    std::FILE* dumpFile = nullptr;
    uint32_t dumpLeft = [] {
        const char* e = std::getenv("GSR_VP_BOUNDS_DUMP");
        const char* n = std::getenv("GSR_VP_BOUNDS_DUMP_COUNT");
        return e && *e ? (n ? (uint32_t)std::strtoul(n, nullptr, 10) : 2000u) : 0u;
    }();
    uint32_t dumpSkip = [] { // GSR_VP_BOUNDS_DUMP_SKIP: batches drawn before the first one written
        const char* n = std::getenv("GSR_VP_BOUNDS_DUMP_SKIP");
        return n ? (uint32_t)std::strtoul(n, nullptr, 10) : 0u;
    }();
    void dump(const VertexProgramBatch& batch, int32_t offsetX, int32_t offsetY) {
        if (batch.batchPath)
            return;
        if (dumpSkip) {
            --dumpSkip;
            return;
        }
        if (!dumpFile && (dumpFile = std::fopen(std::getenv("GSR_VP_BOUNDS_DUMP"), "wb")) == nullptr) {
            dumpLeft = 0;
            return;
        }
        program_dump::write(dumpFile, batch, offsetX, offsetY);
        if (--dumpLeft == 0) {
            std::fclose(dumpFile);
            dumpFile = nullptr;
            std::fprintf(stderr, "[gsr] vertex programs: bounds dump written\n");
        }
    }

    Shaders& of(bool batchPath) { return batchPath ? batches : strips; }
    ~ProgramGpu() {
        if (dumpFile)
            std::fclose(dumpFile);
        if (boundsVertices && std::getenv("DRAKAN_VERTEX_LOG"))
            std::fprintf(stderr,
                         "[gsr] vertex programs: bounds over %llu vertices, %llu evaluated exactly; pieces of strip "
                         "records skipped by their box %llu, estimated %llu\n",
                         (unsigned long long)boundsVertices, (unsigned long long)boundsExact,
                         (unsigned long long)boundsScratch.piecesSkipped,
                         (unsigned long long)boundsScratch.piecesEstimated);
    }
};

namespace {
const D3D11_INPUT_ELEMENT_DESC kProgramLayout[] = {
    {"VPPOS", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"VPST", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"VPNRM", 0, DXGI_FORMAT_R32G32B32A32_SINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"VPSEL", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"VPPREVPOS", 0, DXGI_FORMAT_R32G32B32A32_UINT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"VPPREVSEL", 0, DXGI_FORMAT_R32G32B32A32_UINT, 1, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
};
// gpu-vertex-batch-stage.md section 4: a vp::BatchVertex, then a motion::BatchPrevious in the second stream.
const D3D11_INPUT_ELEMENT_DESC kBatchLayout[] = {
    {"VBPOS", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"VBCOL", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"VBST", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"VBSEL", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"VBPPOS", 0, DXGI_FORMAT_R32G32B32A32_UINT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"VBPSEL", 0, DXGI_FORMAT_R32G32B32A32_UINT, 1, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
};
static_assert(sizeof(vp::BatchVertex) == 64 && sizeof(motion::BatchPrevious) == 32, "the batch layout");

// Compiles the two entry points of one kind and creates their shader objects. Runs on the worker; device methods are
// free-threaded and the immediate context is not touched.
bool buildProgramShaders(ID3D11Device* dev, vp::Arithmetic arithmetic, bool batchPath, ComPtr<ID3D11VertexShader>& vs,
                         ComPtr<ID3D11InputLayout>& layout, ComPtr<ID3D11VertexShader>& vsMotion,
                         ComPtr<ID3D11InputLayout>& motionLayout, std::string& error) {
    const std::string source =
        std::string(kHlslMain) +
        (batchPath ? vertexBatchHlsl(arithmetic, true) : vertexProgramHlsl(arithmetic, vp::gameStages(), true));
    // The flags and HW_BLEND of the renderer's own VSMain: the inlined VSMain must compile to the same instructions.
    // The batch path's velocity entry point needs the previous-frame inputs in its vertex (VP_PREVIOUS); its colour
    // entry point does not have them.
    static const D3D_SHADER_MACRO kMacros[] = {{"HW_BLEND", "0"}, {"VP_MOTION", "1"}, {nullptr, nullptr}};
    static const D3D_SHADER_MACRO kPreviousMacros[] = {
        {"HW_BLEND", "0"}, {"VP_MOTION", "1"}, {"VP_PREVIOUS", "1"}, {nullptr, nullptr}};
    const char* const entries[2] = {batchPath ? "VSBatch" : "VSProgram",
                                    batchPath ? "VSBatchMotion" : "VSProgramMotion"};
    const D3D_SHADER_MACRO* const macros[2] = {kMacros, batchPath ? kPreviousMacros : kMacros};
    const char* const name = batchPath ? "gs_vertex_batch" : "gs_vertex_program";
    ComPtr<ID3DBlob> blob[2];
    std::string errors[2];
    const auto compileOne = [&](int which) {
        ComPtr<ID3DBlob> err;
        if (FAILED(shader_compile::compile(source.c_str(), source.size(), name, macros[which], entries[which], "vs_5_0",
                                           D3DCOMPILE_OPTIMIZATION_LEVEL3, &blob[which], &err)))
            errors[which] = err ? std::string((const char*)err->GetBufferPointer(), err->GetBufferSize()) : "?";
    };
    std::thread second([&] {
        shader_compile::markPrewarmThread();
        compileOne(1);
    });
    compileOne(0);
    second.join();
    if (!blob[0] || !blob[1]) {
        error = "compile: " + (blob[0] ? errors[1] : errors[0]);
        return false;
    }
    const D3D11_INPUT_ELEMENT_DESC* const elements = batchPath ? kBatchLayout : kProgramLayout;
    if (FAILED(dev->CreateVertexShader(blob[0]->GetBufferPointer(), blob[0]->GetBufferSize(), nullptr, &vs)) ||
        FAILED(dev->CreateInputLayout(elements, 4, blob[0]->GetBufferPointer(), blob[0]->GetBufferSize(), &layout)) ||
        FAILED(dev->CreateVertexShader(blob[1]->GetBufferPointer(), blob[1]->GetBufferSize(), nullptr, &vsMotion)) ||
        FAILED(dev->CreateInputLayout(elements, 6, blob[1]->GetBufferPointer(), blob[1]->GetBufferSize(),
                                      &motionLayout))) {
        error = "the device refused the shaders";
        return false;
    }
    return true;
}

// VSProgramRaw of the strip programs with a stream-out stage: per vertex the place, colour and ST/Q words the colour
// shader turns into its outputs (surface::RawVertex). Nothing is rasterised.
bool buildRawProgramShader(ID3D11Device* dev, vp::Arithmetic arithmetic, ComPtr<ID3D11VertexShader>& vs,
                           ComPtr<ID3D11GeometryShader>& so, ComPtr<ID3D11InputLayout>& layout, std::string& error) {
    const std::string source = vertexProgramHlsl(arithmetic, vp::gameStages(), false);
    static const D3D_SHADER_MACRO kRawMacros[] = {{"VP_RAW_ONLY", "1"}, {nullptr, nullptr}};
    ComPtr<ID3DBlob> blob, err;
    if (FAILED(shader_compile::compile(source.c_str(), source.size(), "gs_vertex_program_raw", kRawMacros,
                                       "VSProgramRaw", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, &blob, &err)) ||
        !blob) {
        error = err ? std::string((const char*)err->GetBufferPointer(), err->GetBufferSize()) : "compile";
        return false;
    }
    static const D3D11_SO_DECLARATION_ENTRY kOut[] = {
        {0, "RAWPLACE", 0, 0, 4, 0}, {0, "RAWCOLOUR", 0, 0, 4, 0}, {0, "RAWSTQ", 0, 0, 4, 0}};
    const UINT stride = sizeof(surface::RawVertex);
    if (FAILED(dev->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &vs)) ||
        FAILED(dev->CreateGeometryShaderWithStreamOutput(blob->GetBufferPointer(), blob->GetBufferSize(), kOut, 3,
                                                         &stride, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &so)) ||
        FAILED(dev->CreateInputLayout(kProgramLayout, 4, blob->GetBufferPointer(), blob->GetBufferSize(), &layout))) {
        error = "the device refused the stream-out shader";
        return false;
    }
    return true;
}
} // namespace

D3D11Backend::Impl::ProgramGpu& D3D11Backend::Impl::programState() {
    if (!programGpu)
        programGpu = std::make_shared<ProgramGpu>();
    return *programGpu;
}

// A buffer or the device failed: both kinds off for the session.
void D3D11Backend::Impl::programFailed(const char* what) {
    ProgramGpu& g = programState();
    g.lost.store(true, std::memory_order_release);
    if (!g.logged.exchange(true))
        std::fprintf(stderr,
                     "[gsr] vertex programs: %s; records are evaluated on the CPU for the rest of the session\n", what);
}

// Whether a record of the kind may be drawn by the shader right now. Starts the background compile of that kind at
// its first request.
bool D3D11Backend::Impl::programTakes(vpath::Fallback* why, bool batchPath) {
    ProgramGpu& g = programState();
    if (!g.chosen) {
        g.chosen = true;
        D3D11_FEATURE_DATA_DOUBLES doubles{};
        const bool doubleSupport =
            SUCCEEDED(dev->CheckFeatureSupport(D3D11_FEATURE_DOUBLES, &doubles, sizeof(doubles))) &&
            doubles.DoublePrecisionFloatShaderOps;
        g.arithmetic = vp::chooseArithmetic(vp::ShaderBackend::Direct3D11, doubleSupport,
                                            vp::parseArithmeticRequest(std::getenv("GSR_VERTEX_ARITHMETIC")));
        D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
        g.constantsAppend =
            SUCCEEDED(dev->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options))) &&
            options.MapNoOverwriteOnDynamicBufferSRV;
        const char* rings = std::getenv("GSR_VERTEX_PROGRAM_RINGS");
        g.combined = g.constantsAppend && !(rings && rings[0] == '1');
        if (g.arithmetic == vp::Arithmetic::None)
            std::fprintf(stderr, "[gsr] vertex programs: no exact arithmetic chosen for this adapter, records are "
                                 "evaluated on the CPU\n");
    }
    ProgramGpu::Shaders& sh = g.of(batchPath);
    int state = sh.state.load(std::memory_order_acquire);
    if (state == ProgramGpu::Idle && g.arithmetic != vp::Arithmetic::None) {
        sh.state.store(state = ProgramGpu::Compiling, std::memory_order_release);
        ComPtr<ID3D11Device> device = dev;
        ProgramGpu::Shaders* target = &sh;
        const vp::Arithmetic arithmetic = g.arithmetic;
        sh.worker = std::thread([device, target, arithmetic, batchPath] {
            shader_compile::markPrewarmThread();
            const auto t0 = std::chrono::steady_clock::now();
            std::string error;
            const bool built = buildProgramShaders(device.Get(), arithmetic, batchPath, target->vs, target->layout,
                                                   target->vsMotion, target->motionLayout, error);
            if (built && !batchPath) {
                std::string rawError;
                target->rawReady = buildRawProgramShader(device.Get(), arithmetic, target->vsRaw, target->soRaw,
                                                         target->rawLayout, rawError);
                if (!target->rawReady)
                    std::fprintf(stderr,
                                 "[gsr] vertex programs: the surface stream-out shader failed (%s); the ray surfaces "
                                 "of shader-drawn records are evaluated on the CPU\n",
                                 rawError.c_str());
            }
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            const char* kind = batchPath ? "batch path" : "strips";
            if (built)
                std::fprintf(stderr, "[gsr] vertex programs (%s): shaders ready after %.0f ms (%s arithmetic)\n", kind,
                             ms, vp::arithmeticName(arithmetic));
            else
                std::fprintf(stderr,
                             "[gsr] vertex programs (%s): %s; these records are evaluated on the CPU for the rest of "
                             "the session\n",
                             kind, error.c_str());
            target->state.store(built ? ProgramGpu::Ready : ProgramGpu::Failed, std::memory_order_release);
        });
        if (const char* wait = std::getenv("GSR_VERTEX_PROGRAM_WAIT"); wait && wait[0] == '1') {
            sh.worker.join();
            state = sh.state.load(std::memory_order_acquire);
        }
    }
    program_draw::BackendState b;
    b.shaderFailed = state == ProgramGpu::Failed || g.lost.load(std::memory_order_acquire) || !ok;
    b.arithmetic = g.arithmetic != vp::Arithmetic::None;
    b.shaderReady = state == ProgramGpu::Ready;
    b.worldOpen = engineWorldOpen;
    b.hudMapped = hudScale < 0.999f || std::fabs(hudSize - 1.0f) > 0.001f || sceneCamera.guiScreen == 14 ||
                  inventoryHudFrame.applies(false, 640) || nativeSubtitleOpen;
    publishVertexReaders(b);
    return program_draw::backendTakes(b, why);
}

void D3D11Backend::Impl::fillVertexReaders(program_draw::BackendState& b) const {
    // The ray surfaces read every draw's vertices; with the surface stream (kCaps.programSurfaces) a shader-drawn
    // strip batch streams them out instead, so a ray effect refuses no record while the stream works.
    const bool rays = modernWater || rayShadows || rayReflections || rayGlobalIllumination;
    const ProgramGpu* g = programGpu.get();
    const bool streamFailed =
        g && (g->surfaceStream.failed ||
              (g->strips.state.load(std::memory_order_acquire) == ProgramGpu::Ready && !g->strips.rawReady));
    b.rayEffects = rays && (!runtimeFlags().vertexSurfaceStream || streamFailed);
    // Terrain detail and the debug view shade per pixel from the camera and depth (prepareTerrain); the cliff side
    // projection and the audit read the first triangles of the draw on the CPU.
    b.terrainDetail = terrainCfg.cliffs || runtimeFlags().terrainAudit;
    b.lodStatistic = lodStat;
    b.vertexDiagnosis = runtimeFlags().surfaceAudit || cstr(runtimeFlags().surfacePick) != nullptr;
}

void D3D11Backend::Impl::publishVertexReaders() {
    program_draw::BackendState b;
    fillVertexReaders(b);
    publishVertexReaders(b);
}

void D3D11Backend::Impl::publishVertexReaders(const program_draw::BackendState& b) {
    vertexReaders.store(program_draw::cpuReadsVertices(b), std::memory_order_relaxed);
    raySurfaceStream.store(!b.rayEffects && (modernWater || rayShadows || rayReflections || rayGlobalIllumination),
                           std::memory_order_relaxed);
}

namespace {
// Room for `bytes` in a ring: the buffer (re)created when it is too small. False: the device refused it.
template <class Ring>
bool reserveRing(ID3D11Device* dev, Ring& ring, uint32_t bytes, uint32_t minimum, UINT bind) {
    if (ring.buffer && bytes <= ring.capacity / 2)
        return true;
    const uint64_t wanted = std::max<uint64_t>(uint64_t(bytes) * 4u, minimum);
    if (wanted > (256u << 20))
        return false; // more than any batch the core forms
    const uint32_t capacity = (uint32_t)wanted;
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = capacity;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = bind;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ComPtr<ID3D11Buffer> buffer;
    if (FAILED(dev->CreateBuffer(&bd, nullptr, &buffer)) || !buffer)
        return false;
    ring.buffer = std::move(buffer);
    ring.capacity = capacity;
    ring.offset = capacity; // the first map discards
    return true;
}
// Appends to a ring (`unit`: the offset is kept a multiple of it). `count` blocks are copied one after the other.
// False: the blocks do not fit, or the map failed (a removed device); nothing was written then.
template <class Ring>
bool appendRing(ID3D11DeviceContext* ctx, Ring& ring, bool append, uint32_t unit, const void* const* data,
                const uint32_t* sizes, uint32_t count) {
    uint64_t bytes = 0;
    for (uint32_t i = 0; i < count; ++i)
        bytes += sizes[i];
    uint64_t offset = (uint64_t(ring.offset) + unit - 1) / unit * unit;
    D3D11_MAP mode = D3D11_MAP_WRITE_NO_OVERWRITE;
    if (!append || offset > ring.capacity || bytes > ring.capacity - offset) {
        mode = D3D11_MAP_WRITE_DISCARD;
        offset = 0;
    }
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (!ring.buffer || bytes > ring.capacity || FAILED(ctx->Map(ring.buffer.Get(), 0, mode, 0, &ms)) || !ms.pData)
        return false;
    uint8_t* out = static_cast<uint8_t*>(ms.pData) + offset;
    for (uint32_t i = 0; i < count; ++i) {
        if (sizes[i])
            std::memcpy(out, data[i], sizes[i]);
        out += sizes[i];
    }
    ctx->Unmap(ring.buffer.Get(), 0);
    ring.drawOffset = (uint32_t)offset;
    ring.offset = (uint32_t)(offset + bytes);
    return true;
}
// appendRing for the combined ring: the parts one after the other, each starting on a 16-byte row (a part of size
// zero takes no room); `start` receives each part's byte offset. One Map.
template <class Ring>
bool appendRows(ID3D11DeviceContext* ctx, Ring& ring, const void* const* data, const uint32_t* sizes, uint32_t count,
                uint32_t* start) {
    uint64_t bytes = 0;
    for (uint32_t i = 0; i < count; ++i)
        bytes += (uint64_t(sizes[i]) + 15u) & ~uint64_t(15u);
    uint64_t offset = (uint64_t(ring.offset) + 15u) & ~uint64_t(15u);
    D3D11_MAP mode = D3D11_MAP_WRITE_NO_OVERWRITE;
    if (offset > ring.capacity || bytes > ring.capacity - offset) {
        mode = D3D11_MAP_WRITE_DISCARD;
        offset = 0;
    }
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (!ring.buffer || bytes > ring.capacity || FAILED(ctx->Map(ring.buffer.Get(), 0, mode, 0, &ms)) || !ms.pData)
        return false;
    uint8_t* const base = static_cast<uint8_t*>(ms.pData);
    uint64_t at = offset;
    for (uint32_t i = 0; i < count; ++i) {
        start[i] = (uint32_t)at;
        if (sizes[i])
            std::memcpy(base + at, data[i], sizes[i]);
        at += (uint64_t(sizes[i]) + 15u) & ~uint64_t(15u);
    }
    ctx->Unmap(ring.buffer.Get(), 0);
    ring.drawOffset = (uint32_t)offset;
    ring.offset = (uint32_t)(offset + bytes);
    return true;
}
constexpr uint32_t kPassRows = sizeof(vp::Pass) / 16u;
// the own constants (or passes) end on a row, so the previous ones follow them without a gap (appendRows)
static_assert(sizeof(vp::Constants) % 16u == 0 && sizeof(vp::Pass) % 16u == 0, "constants in whole rows");
} // namespace

// The previous state of every record of the batch, for the velocity pass. Called by gs_draw.inl where an ordinary
// draw calls prepareObjectMotion, under the same condition, so both histories see the same worlds.
bool D3D11Backend::Impl::prepareProgramMotion(RT& rt, const GsDrawState& s, const VertexProgramBatch& batch, float sx,
                                              float sy) {
    ProgramGpu& g = programState();
    const bool active = motionWorldOpen && (neuralEnabled || upMode) && rt.neuralState == 1;
    if (!active || !ensureMotionTarget(rt))
        return false;
    CpuProfile::Scope profile(cpuProfile, CpuProfile::ProgramMotion);
    const auto apply = [&](const ObjectMotionScope& scope) { applyObjectMotion(scope); };
    const auto current = [&]() -> const ObjectMotionScope& { return objectScope; };
    // The previous constants (strips) or position passes (batch path) follow the batch's own in the table.
    const auto counts = batch.batchPath
                            ? program_draw::trackBatchPath(motionHistory, batch, s, batch.passCount * kPassRows,
                                                           g.batchPrevious, g.previousPasses, apply, current)
                            : program_draw::trackBatch(motionHistory, batch, s, batch.constantCount, g.previous,
                                                       g.previousConstants, apply, current);
    g.motionTracked = true;
    motionScopedBatches += counts.scoped;
    motionMatchedBatches += counts.matched;
    motionMatchedVertices += counts.matchedVertices;
    motionRejectedBatches += counts.records - counts.matched;
    const void* data[1] = {batch.batchPath ? (const void*)g.batchPrevious.data() : (const void*)g.previous.data()};
    const uint32_t stride =
        batch.batchPath ? (uint32_t)sizeof(motion::BatchPrevious) : (uint32_t)sizeof(motion::ProgramPrevious);
    const uint32_t sizes[1] = {batch.vertexCount * stride};
    if (!appendRing(ctx.Get(), g.previousVb, true, stride, data, sizes, 1)) {
        probeDevice("vertex program previous stream");
        programFailed("the previous-frame vertex stream could not be written");
        g.previousConstants.clear();
        g.previousPasses.clear();
        foreignContext();
        clearMotionTarget(rt);
        return false;
    }
    const float raster[4] = {sx, sy, 0, 0};
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (FAILED(map(g.cbMotion.Get(), D3D11_MAP_WRITE_DISCARD, ms, "vertex program motion constants")) || !ms.pData) {
        programFailed("the motion constants could not be written");
        return false;
    }
    std::memcpy(ms.pData, raster, sizeof(raster));
    ctx->Unmap(g.cbMotion.Get(), 0);
    ++g.velocityBatches;
    // [video] velocity_skip: a batch of records may write valid entries anywhere; its pass is drawn into the real
    // target only, so a verify comparison of this world would not compare like with like.
    rt.motionCoverage.markAll();
    rt.motionShadowBlind = true;
    return true;
}

// After bindDraw: the batch's vertices, indices, constants (its own, then the previous ones prepareProgramMotion
// found) and vertex shader replace the stand-ins'.
void D3D11Backend::Impl::bindProgramDraw(const GsDrawState& s, const VertexProgramBatch& batch) {
    CpuProfile::Scope profile(cpuProfile, CpuProfile::ProgramUpload);
    ProgramGpu& g = programState();
    const bool bp = batch.batchPath;
    const void* constantData[2] = {bp ? (const void*)batch.passes : (const void*)batch.constants,
                                   bp ? (const void*)g.previousPasses.data() : (const void*)g.previousConstants.data()};
    const uint32_t constantSizes[2] = {bp ? batch.passCount * (uint32_t)sizeof(vp::Pass)
                                          : batch.constantCount * (uint32_t)sizeof(vp::Constants),
                                       bp ? (uint32_t)(g.previousPasses.size() * sizeof(vp::Pass))
                                          : (uint32_t)(g.previousConstants.size() * sizeof(vp::Constants))};
    const uint32_t stride = bp ? (uint32_t)sizeof(vp::BatchVertex) : (uint32_t)sizeof(VertexProgramVertex);
    const void* vertexData[1] = {bp ? (const void*)batch.batchVertices : (const void*)batch.vertices};
    const uint32_t vertexSizes[1] = {batch.vertexCount * stride};
    const void* indexData[1] = {batch.indices};
    const uint32_t indexSizes[1] = {batch.indexCount * (uint32_t)sizeof(uint32_t)};
    uint32_t constantRow = 0;
    if (g.combined) {
        // One Map: the constants (own, then previous: contiguous rows), the vertices and the indices, each part
        // starting on a 16-byte row. The ring's room was reserved before the draw started. The previous stream of
        // the velocity pass keeps its own ring (prepareProgramMotion).
        const void* parts[4] = {constantData[0], constantData[1], vertexData[0], indexData[0]};
        const uint32_t sizes[4] = {constantSizes[0], constantSizes[1], vertexSizes[0], indexSizes[0]};
        uint32_t start[4];
        if (!appendRows(ctx.Get(), g.all, parts, sizes, 4, start)) {
            probeDevice("vertex program buffers");
            programFailed("a buffer of the draw could not be written");
            return;
        }
        constantRow = start[0] / 16u;
        g.drawVb = g.drawIb = g.all.buffer.Get();
        g.drawVbOffset = start[2];
        g.drawFirstIndex = start[3] / (uint32_t)sizeof(uint32_t);
    } else {
        if (!appendRing(ctx.Get(), g.constants, g.constantsAppend, bp ? 16u : (uint32_t)sizeof(vp::Constants),
                        constantData, constantSizes, 2) ||
            !appendRing(ctx.Get(), g.vb, true, stride, vertexData, vertexSizes, 1) ||
            !appendRing(ctx.Get(), g.ib, true, (uint32_t)sizeof(uint32_t), indexData, indexSizes, 1)) {
            // Only a removed device gets here (the buffers were reserved before the draw started);
            // drawProgramVertices and drawProgramVelocity then draw nothing.
            probeDevice("vertex program buffers");
            programFailed("a buffer of the draw could not be written");
            return;
        }
        constantRow = g.constants.drawOffset / 16u;
        g.drawVb = g.vb.buffer.Get();
        g.drawVbOffset = g.vb.drawOffset;
        g.drawIb = g.ib.buffer.Get();
        g.drawFirstIndex = g.ib.drawOffset / (uint32_t)sizeof(uint32_t);
    }
    const int32_t words[4] = {(int32_t)(s.xyoffset & 0xFFFF), (int32_t)((s.xyoffset >> 32) & 0xFFFF),
                              (int32_t)constantRow, 0};
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (FAILED(map(g.cb.Get(), D3D11_MAP_WRITE_DISCARD, ms, "vertex program constants")) || !ms.pData) {
        programFailed("the draw constants could not be written");
        return;
    }
    std::memcpy(ms.pData, words, sizeof(words));
    ctx->Unmap(g.cb.Get(), 0);
    ProgramGpu::Shaders& sh = g.of(bp);
    const UINT offset = g.drawVbOffset;
    ctx->IASetInputLayout(sh.layout.Get());
    ctx->IASetVertexBuffers(0, 1, &g.drawVb, &stride, &offset);
    ctx->IASetIndexBuffer(g.drawIb, DXGI_FORMAT_R32_UINT, 0);
    ctx->VSSetShader(sh.vs.Get(), nullptr, 0);
    ID3D11Buffer* buffers[2] = {g.cb.Get(), g.cbMotion.Get()};
    ctx->VSSetConstantBuffers(5, 2, buffers);
    ctx->VSSetShaderResources(8, 1, g.combined ? g.allSrv.GetAddressOf() : g.constantsSrv.GetAddressOf());
    ++g.batches_;
}

void D3D11Backend::Impl::drawProgramVertices(const VertexProgramBatch& batch) {
    ProgramGpu& g = programState();
    ProgramGpu::Shaders& sh = g.of(batch.batchPath);
    if (sh.state.load(std::memory_order_relaxed) != ProgramGpu::Ready || g.lost.load(std::memory_order_relaxed))
        return;
    auto& st = g.surfaceStream;
    if (st.pending && !st.streamed && !batch.batchPath && sh.rawReady && st.buffer) {
        // The ray surfaces of this batch (submitProgramSurface reserved the range): the same vertices and indices
        // through the raw entry point into the stream, once, before its first colour pass. In index order, so the
        // stream holds the triangle list the CPU path would have queued.
        ctx->IASetInputLayout(sh.rawLayout.Get());
        ctx->VSSetShader(sh.vsRaw.Get(), nullptr, 0);
        ctx->GSSetShader(sh.soRaw.Get(), nullptr, 0);
        const UINT offset = st.first * (UINT)sizeof(surface::RawVertex);
        ctx->SOSetTargets(1, st.buffer.GetAddressOf(), &offset);
        ctx->DrawIndexed(batch.indexCount, g.drawFirstIndex, 0);
        ID3D11Buffer* none = nullptr;
        const UINT zero = 0;
        ctx->SOSetTargets(1, &none, &zero);
        ctx->GSSetShader(nullptr, nullptr, 0);
        ctx->VSSetShader(sh.vs.Get(), nullptr, 0);
        ctx->IASetInputLayout(sh.layout.Get());
        st.streamed = true;
        ++st.batches;
        st.vertices += batch.indexCount;
    }
    ctx->DrawIndexed(batch.indexCount, g.drawFirstIndex, 0);
}

bool D3D11Backend::Impl::reserveProgramSurface(uint32_t count, uint32_t& first) {
    ProgramGpu& g = programState();
    auto& st = g.surfaceStream;
    if (st.failed || !g.strips.rawReady || g.strips.state.load(std::memory_order_acquire) != ProgramGpu::Ready ||
        g.lost.load(std::memory_order_acquire) || !count)
        return false;
    if (st.buffer && st.used + uint64_t(count) > st.capacity)
        flushSurfaces(); // the stream is full: hand the waiting draws over now (reads the stream back)
    if (!st.buffer || count > st.capacity) {
        const uint64_t want = std::max<uint64_t>(uint64_t(count) * 2u, 1u << 18); // 262,144 vertices = 12 MiB
        if (want * sizeof(surface::RawVertex) > (256ull << 20)) {
            st.failed = true;
            return false;
        }
        if (st.used)
            flushSurfaces();
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = (UINT)(want * sizeof(surface::RawVertex));
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_STREAM_OUTPUT;
        D3D11_BUFFER_DESC sd = bd;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.BindFlags = 0;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> buffer, staging, lateStaging;
        if (FAILED(dev->CreateBuffer(&bd, nullptr, &buffer)) || FAILED(dev->CreateBuffer(&sd, nullptr, &staging)) ||
            FAILED(dev->CreateBuffer(&sd, nullptr, &lateStaging))) {
            st.failed = true;
            std::fprintf(stderr, "[gsr] vertex programs: no surface stream buffer; the ray surfaces of shader-drawn "
                                 "records are evaluated on the CPU\n");
            return false;
        }
        st.buffer = std::move(buffer);
        st.staging = std::move(staging);
        st.lateStaging = std::move(lateStaging);
        st.lateValid = false; // a copy in the old buffer is lost (its waiting draws are dropped)
        st.capacity = (uint32_t)want;
        st.used = 0;
    }
    first = st.used;
    st.used += count;
    st.pending = true;
    st.streamed = false;
    st.first = first;
    st.count = count;
    return true;
}

bool D3D11Backend::Impl::readProgramSurfaces(const surface::RawVertex*& vertices, uint32_t& count) {
    ProgramGpu& g = programState();
    auto& st = g.surfaceStream;
    vertices = nullptr;
    count = 0;
    if (!st.buffer || !st.used)
        return false;
    CpuProfile::Scope profile(cpuProfile, CpuProfile::ProgramSurfaceRead);
    const uint32_t used = st.used;
    st.used = 0;
    D3D11_BOX box{0, 0, 0, used * (UINT)sizeof(surface::RawVertex), 1, 1};
    ctx->CopySubresourceRegion(st.staging.Get(), 0, 0, 0, 0, st.buffer.Get(), 0, &box);
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (FAILED(map(st.staging.Get(), D3D11_MAP_READ, ms, "vertex program surfaces")) || !ms.pData)
        return false;
    st.read.resize(used);
    std::memcpy(st.read.data(), ms.pData, size_t(used) * sizeof(surface::RawVertex));
    ctx.raw()->Unmap(st.staging.Get(), 0);
    ++st.reads;
    vertices = st.read.data();
    count = used;
    return true;
}

bool D3D11Backend::Impl::copyLateProgramSurfaces() {
    auto& st = programState().surfaceStream;
    st.lateValid = false;
    if (!st.buffer || !st.lateStaging || !st.used)
        return false;
    D3D11_BOX box{0, 0, 0, st.used * (UINT)sizeof(surface::RawVertex), 1, 1};
    ctx->CopySubresourceRegion(st.lateStaging.Get(), 0, 0, 0, 0, st.buffer.Get(), 0, &box);
    st.lateCount = st.used;
    st.lateValid = true;
    st.used = 0; // later stream writes follow the copy in the GPU's order
    return true;
}

bool D3D11Backend::Impl::mapLateProgramSurfaces(const surface::RawVertex*& vertices, uint32_t& count) {
    auto& st = programState().surfaceStream;
    vertices = nullptr;
    count = 0;
    if (!st.lateValid)
        return false;
    st.lateValid = false;
    CpuProfile::Scope profile(cpuProfile, CpuProfile::ProgramSurfaceRead);
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (FAILED(map(st.lateStaging.Get(), D3D11_MAP_READ, ms, "vertex program surfaces (late)")) || !ms.pData)
        return false;
    st.read.resize(st.lateCount);
    std::memcpy(st.read.data(), ms.pData, size_t(st.lateCount) * sizeof(surface::RawVertex));
    ctx.raw()->Unmap(st.lateStaging.Get(), 0);
    ++st.reads;
    vertices = st.read.data();
    count = st.lateCount;
    return true;
}

// The velocity pass of the colour pass just drawn (compare drawVelocity in d3d11_draw.cpp): the same targets, depth
// rule and pixel shader, with the program's velocity entry point and the previous stream in slot 1.
void D3D11Backend::Impl::drawProgramVelocity(RT& rt, DepthBuf& db, const VertexProgramBatch& batch,
                                             rhi::Compare depthCompare, bool restore, bool materialDraw, bool useHw) {
    ProgramGpu& g = programState();
    ProgramGpu::Shaders& sh = g.of(batch.batchPath);
    if (sh.state.load(std::memory_order_relaxed) != ProgramGpu::Ready || g.lost.load(std::memory_order_relaxed))
        return;
    static const D3D11_COMPARISON_FUNC kCompare[] = {D3D11_COMPARISON_NEVER,         D3D11_COMPARISON_LESS,
                                                     D3D11_COMPARISON_EQUAL,         D3D11_COMPARISON_LESS_EQUAL,
                                                     D3D11_COMPARISON_GREATER,       D3D11_COMPARISON_NOT_EQUAL,
                                                     D3D11_COMPARISON_GREATER_EQUAL, D3D11_COMPARISON_ALWAYS};
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ctx->OMSetRenderTargets(1, rt.motionRtv.GetAddressOf(), db.dsv.Get());
    ctx->OMSetBlendState(blendOff.Get(), nullptr, 0xffffffffu);
    ctx->OMSetDepthStencilState(getDepthState(kCompare[(size_t)depthCompare], false), 0);
    ctx->IASetInputLayout(sh.motionLayout.Get());
    const UINT stride = batch.batchPath ? sizeof(motion::BatchPrevious) : sizeof(motion::ProgramPrevious),
               offset = g.previousVb.drawOffset;
    ctx->IASetVertexBuffers(1, 1, g.previousVb.buffer.GetAddressOf(), &stride, &offset);
    ctx->VSSetShader(sh.vsMotion.Get(), nullptr, 0);
    ctx->PSSetShader(psMotion.Get(), nullptr, 0);
    ctx->DrawIndexed(batch.indexCount, g.drawFirstIndex, 0);
    if (!restore)
        return;
    ID3D11RenderTargetView* rtvs[1] = {rt.rtv.Get()};
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ctx->OMSetRenderTargets(1, rtvs, db.dsv.Get());
    ctx->IASetInputLayout(sh.layout.Get());
    ID3D11Buffer* nullBuffer = nullptr;
    const UINT zero = 0;
    ctx->IASetVertexBuffers(1, 1, &nullBuffer, &zero, &zero);
    ctx->VSSetShader(sh.vs.Get(), nullptr, 0);
    ID3D11PixelShader* const colorShader =
        materialDraw ? (useHw ? materialGpu.psHw.Get() : materialGpu.psSb.Get()) : (useHw ? psHw.Get() : psSb.Get());
    ctx->PSSetShader(colorShader, nullptr, 0);
}

bool D3D11Backend::vertexPrograms(vpath::Fallback* why, bool batchPath) const {
    return m_impl->programTakes(why, batchPath);
}

vpath::Capability D3D11Backend::vertexPathCapability() const {
    // The integer library needs nothing optional, so every Direct3D 11 adapter has the arithmetic unless the
    // diagnosis switch asks for doubles on an adapter without them, or for none. `proven` is set by the project
    // per combination after the full proof, not here.
    D3D11_FEATURE_DATA_DOUBLES doubles{};
    const bool doubleSupport =
        m_impl->dev && SUCCEEDED(m_impl->dev->CheckFeatureSupport(D3D11_FEATURE_DOUBLES, &doubles, sizeof(doubles))) &&
        doubles.DoublePrecisionFloatShaderOps;
    const Impl::ProgramGpu* g = m_impl->programGpu.get();
    vpath::Capability c;
    c.backendHasPath = m_impl->ok;
    c.adapterExact = m_impl->ok &&
                     vp::chooseArithmetic(vp::ShaderBackend::Direct3D11, doubleSupport,
                                          vp::parseArithmeticRequest(std::getenv("GSR_VERTEX_ARITHMETIC"))) !=
                         vp::Arithmetic::None &&
                     !(g && g->lost.load());
    c.proven = false;
    // The kinds with a shader, or whose compile has not started (their first record starts it). A kind that is
    // compiling or failed is not offered: its runs stay in the microprogram, which is faster than evaluating its
    // records on the CPU (drakan_vertex_path follows the change).
    const auto offered = [](const Impl::ProgramGpu::Shaders* sh) {
        const int state = sh ? sh->state.load() : Impl::ProgramGpu::Idle;
        return state == Impl::ProgramGpu::Idle || state == Impl::ProgramGpu::Ready;
    };
    c.shaderStages = (offered(g ? &g->strips : nullptr) ? vpath::kStrips : 0u) |
                     (offered(g ? &g->batches : nullptr) ? (vpath::kAllStages & ~vpath::kStrips) : 0u);
    // A feature that reads every draw's vertices on the CPU would refuse each record (Fallback::Feature) and the
    // renderer would evaluate them one by one: three times the frame time in Surdana with modern water on
    // (docs/journal/2026-11-16/perf-cpu-jitter.md). Offer nothing; drakan_vertex_path follows within 250 ms.
    c.cpuVertexReaders = m_impl->vertexReaders.load(std::memory_order_relaxed);
    if (c.cpuVertexReaders)
        c.shaderStages = 0;
    else if (m_impl->raySurfaceStream.load(std::memory_order_relaxed))
        c.shaderStages &= vpath::kStrips; // the surface stream carries strip batches only (surface::evaluateBatch)
    return c;
}

void D3D11Backend::drawVertexProgram(const GsDrawState& st, const ClutBuf& clut, const VertexProgramBatch& batch) {
    Impl& i = *m_impl;
    Impl::ProgramGpu& g = i.programState();
    const auto applyNotes = [&] {
        program_draw::applyNotes(batch, [&](const ObjectMotionScope& scope) { i.applyObjectMotion(scope); });
    };
    if (!batch.indexCount || !batch.vertexCount) {
        applyNotes();
        return;
    }
    const bool bp = batch.batchPath;
    // Everything that can fail for want of memory happens before the draw starts, so a failure can still be drawn
    // exactly: the batch is then evaluated here like the core evaluates a record.
    bool ready = g.of(bp).state.load(std::memory_order_acquire) == Impl::ProgramGpu::Ready &&
                 !g.lost.load(std::memory_order_acquire) && i.ok;
    if (ready) {
        const ID3D11Buffer* const before = g.constants.buffer.Get();
        const uint32_t constantBytes = bp ? 2u * batch.passCount * (uint32_t)sizeof(vp::Pass)
                                          : 2u * batch.constantCount * (uint32_t)sizeof(vp::Constants);
        const uint32_t vertexBytes =
            batch.vertexCount * (bp ? (uint32_t)sizeof(vp::BatchVertex) : (uint32_t)sizeof(VertexProgramVertex));
        const uint32_t indexBytes = batch.indexCount * (uint32_t)sizeof(uint32_t);
        const uint32_t previousBytes = batch.vertexCount * (uint32_t)sizeof(motion::ProgramPrevious);
        static_assert(sizeof(motion::ProgramPrevious) >= sizeof(motion::BatchPrevious), "the previous stream's room");
        if (g.combined) {
            const ID3D11Buffer* const allBefore = g.all.buffer.Get();
            // the four parts of appendRows, each rounded up to a row
            ready = reserveRing(i.dev.Get(), g.all, constantBytes + vertexBytes + indexBytes + 64u, 16u << 20,
                                D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER | D3D11_BIND_SHADER_RESOURCE) &&
                    reserveRing(i.dev.Get(), g.previousVb, previousBytes, 4u << 20, D3D11_BIND_VERTEX_BUFFER);
            if (ready && (!g.allSrv || allBefore != g.all.buffer.Get())) {
                D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
                sd.Format = DXGI_FORMAT_R32G32B32A32_UINT;
                sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
                sd.Buffer.NumElements = g.all.capacity / 16u;
                g.allSrv.Reset();
                ready = SUCCEEDED(i.dev->CreateShaderResourceView(g.all.buffer.Get(), &sd, &g.allSrv)) && g.allSrv;
            }
        } else {
            ready = reserveRing(i.dev.Get(), g.vb, vertexBytes, 8u << 20, D3D11_BIND_VERTEX_BUFFER) &&
                    reserveRing(i.dev.Get(), g.ib, indexBytes, 2u << 20, D3D11_BIND_INDEX_BUFFER) &&
                    reserveRing(i.dev.Get(), g.previousVb, previousBytes, 4u << 20, D3D11_BIND_VERTEX_BUFFER) &&
                    reserveRing(i.dev.Get(), g.constants, constantBytes, 4u << 20, D3D11_BIND_SHADER_RESOURCE);
        }
        if (ready && !g.combined && (!g.constantsSrv || before != g.constants.buffer.Get())) {
            D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
            sd.Format = DXGI_FORMAT_R32G32B32A32_UINT;
            sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            sd.Buffer.NumElements = g.constants.capacity / 16u;
            g.constantsSrv.Reset();
            ready = SUCCEEDED(i.dev->CreateShaderResourceView(g.constants.buffer.Get(), &sd, &g.constantsSrv)) &&
                    g.constantsSrv;
        }
        if (ready && (!g.cb || !g.cbMotion)) {
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = 16;
            bd.Usage = D3D11_USAGE_DYNAMIC;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            g.cb.Reset();
            g.cbMotion.Reset();
            ready = SUCCEEDED(i.dev->CreateBuffer(&bd, nullptr, &g.cb)) &&
                    SUCCEEDED(i.dev->CreateBuffer(&bd, nullptr, &g.cbMotion)) && g.cb && g.cbMotion;
        }
        if (!ready) {
            i.probeDevice("vertex program buffer creation");
            i.programFailed("a buffer could not be created");
        }
    }
    if (!ready) {
        // The exact CPU evaluation of the batch as a triangle list, through the ordinary draw (no motion history
        // for this one batch). The core took only records whose vertices are all drawn (no skip bit).
        const int ofx = (int)(st.xyoffset & 0xFFFF), ofy = (int)((st.xyoffset >> 32) & 0xFFFF);
        g.fallback.resize(batch.indexCount);
        for (uint32_t n = 0; n < batch.indexCount; ++n) {
            vp::Output o;
            if (bp) {
                const vp::BatchVertex& v = batch.batchVertices[batch.indices[n]];
                o = vp::evaluateVertex(batch.passes + v.slot / kPassRows, vp::kBatchMaxPasses, v);
            } else {
                const VertexProgramVertex& v = batch.vertices[batch.indices[n]];
                o = vp::evaluate(v.program, batch.constants[v.constants], v.in);
            }
            DrawVertex& d = g.fallback[n];
            d.x = ((int)o.x - ofx) * (1.0f / 16.0f);
            d.y = ((int)o.y - ofy) * (1.0f / 16.0f);
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
        if (bp)
            vpath::countBatchFallback(vpath::Fallback::Internal);
        else
            vpath::countFallback(vpath::Fallback::Internal);
        // the core counted these vertices as drawn by a shader: they were evaluated on the CPU after all
        vpath::Counters& c = vpath::state().counters;
        c.gpuVertices.fetch_sub(batch.countedVertices, std::memory_order_relaxed);
        c.cpuVertices.fetch_add(batch.countedVertices, std::memory_order_relaxed);
        if (bp)
            c.gpuBatchVertices.fetch_sub(batch.countedVertices, std::memory_order_relaxed);
        applyNotes();
        i.pendingMotionBase = g.fallback.data();
        i.pendingMotionCount = batch.indexCount;
        i.draw(st, clut, g.fallback.data(), batch.indexCount);
        return;
    }
    DrawVertex standIns[3];
    program_draw::standIns(st, standIns);
    {
        // The exact bounds of the batch: the stand-ins span them instead of the scissor (program_draw::programBounds)
        if (g.dumpLeft) // GSR_VP_BOUNDS_DUMP (vertex_program_dump.h)
            g.dump(batch, (int32_t)(st.xyoffset & 0xFFFF), (int32_t)((st.xyoffset >> 32) & 0xFFFF));
        CpuProfile::Scope profile(i.cpuProfile, CpuProfile::ProgramBounds);
        const program_draw::Bounds bounds = program_draw::programBounds(
            batch, (int32_t)(st.xyoffset & 0xFFFF), (int32_t)((st.xyoffset >> 32) & 0xFFFF), g.boundsScratch);
        g.boundsExact += bounds.exact;
        g.boundsVertices += batch.vertexCount;
        if (bounds.any)
            program_draw::standInsOver(bounds, standIns);
    }
    g.previousConstants.clear();
    g.previousPasses.clear();
    g.motionTracked = false;
    i.programBatch = &batch;
    i.pendingMotionBase = standIns;
    i.pendingMotionCount = 3;
    i.draw(st, clut, standIns, 3);
    i.programBatch = nullptr;
    auto& stream = g.surfaceStream;
    if (stream.pending && !stream.streamed) {
        // The draw reserved its surfaces but ended before a colour pass (a skipped or empty draw): its range of the
        // stream stays unwritten, so the waiting draw takes the same vertices evaluated on the CPU.
        auto& pending = i.pendingSurfaces;
        for (size_t k = pending.draws.size(); k-- > 0;) {
            auto& d = pending.draws[k];
            if (!d.gpu || d.first != stream.first)
                continue;
            d.gpu = false;
            d.first = (uint32_t)pending.vertices.size();
            surface::evaluateBatch(batch, st, pending.vertices);
            ++stream.cpuFallbacks;
            break;
        }
    }
    stream.pending = stream.streamed = false;
    // Notes the draw did not walk (no temporal world, a skipped draw): the scope still advances.
    if (!g.motionTracked)
        applyNotes();
}

} // namespace gsr
