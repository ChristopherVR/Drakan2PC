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
    uint64_t boundsExact = 0, boundsVertices = 0; // vertices batchBounds evaluated exactly, of all it bounded
    bool motionTracked = false;
    uint64_t batches_ = 0, velocityBatches = 0;

    Shaders& of(bool batchPath) { return batchPath ? batches : strips; }
    ~ProgramGpu() {
        if (boundsVertices && std::getenv("DRAKAN_VERTEX_LOG"))
            std::fprintf(stderr, "[gsr] vertex programs: batch bounds over %llu vertices, %llu evaluated exactly\n",
                         (unsigned long long)boundsVertices, (unsigned long long)boundsExact);
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
    b.rayEffects = modernWater || rayShadows || rayReflections || rayGlobalIllumination;
    // Terrain detail and the debug view shade per pixel from the camera and depth (prepareTerrain); the cliff side
    // projection and the audit read the first triangles of the draw on the CPU.
    b.terrainDetail = terrainCfg.cliffs || runtimeFlags().terrainAudit;
    b.lodStatistic = lodStat;
    b.vertexDiagnosis = runtimeFlags().surfaceAudit || cstr(runtimeFlags().surfacePick) != nullptr;
    return program_draw::backendTakes(b, why);
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
constexpr uint32_t kPassRows = sizeof(vp::Pass) / 16u;
} // namespace

// The previous state of every record of the batch, for the velocity pass. Called by gs_draw.inl where an ordinary
// draw calls prepareObjectMotion, under the same condition, so both histories see the same worlds.
bool D3D11Backend::Impl::prepareProgramMotion(RT& rt, const GsDrawState& s, const VertexProgramBatch& batch, float sx,
                                              float sy) {
    ProgramGpu& g = programState();
    const bool active = motionWorldOpen && (neuralEnabled || upMode) && rt.neuralState == 1;
    if (!active || !ensureMotionTarget(rt))
        return false;
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
        const float clear[4]{};
        ctx->ClearRenderTargetView(rt.motionRtv.Get(), clear);
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
    return true;
}

// After bindDraw: the batch's vertices, indices, constants (its own, then the previous ones prepareProgramMotion
// found) and vertex shader replace the stand-ins'.
void D3D11Backend::Impl::bindProgramDraw(const GsDrawState& s, const VertexProgramBatch& batch) {
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
    if (!appendRing(ctx.Get(), g.constants, g.constantsAppend, bp ? 16u : (uint32_t)sizeof(vp::Constants), constantData,
                    constantSizes, 2) ||
        !appendRing(ctx.Get(), g.vb, true, stride, vertexData, vertexSizes, 1) ||
        !appendRing(ctx.Get(), g.ib, true, (uint32_t)sizeof(uint32_t), indexData, indexSizes, 1)) {
        // Only a removed device gets here (the buffers were reserved before the draw started); drawProgramVertices
        // and drawProgramVelocity then draw nothing.
        probeDevice("vertex program buffers");
        programFailed("a buffer of the draw could not be written");
        return;
    }
    const int32_t words[4] = {(int32_t)(s.xyoffset & 0xFFFF), (int32_t)((s.xyoffset >> 32) & 0xFFFF),
                              (int32_t)(g.constants.drawOffset / 16u), 0};
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (FAILED(map(g.cb.Get(), D3D11_MAP_WRITE_DISCARD, ms, "vertex program constants")) || !ms.pData) {
        programFailed("the draw constants could not be written");
        return;
    }
    std::memcpy(ms.pData, words, sizeof(words));
    ctx->Unmap(g.cb.Get(), 0);
    ProgramGpu::Shaders& sh = g.of(bp);
    const UINT offset = g.vb.drawOffset;
    ctx->IASetInputLayout(sh.layout.Get());
    ctx->IASetVertexBuffers(0, 1, g.vb.buffer.GetAddressOf(), &stride, &offset);
    ctx->IASetIndexBuffer(g.ib.buffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    ctx->VSSetShader(sh.vs.Get(), nullptr, 0);
    ID3D11Buffer* buffers[2] = {g.cb.Get(), g.cbMotion.Get()};
    ctx->VSSetConstantBuffers(5, 2, buffers);
    ctx->VSSetShaderResources(8, 1, g.constantsSrv.GetAddressOf());
    ++g.batches_;
}

void D3D11Backend::Impl::drawProgramVertices(const VertexProgramBatch& batch) {
    ProgramGpu& g = programState();
    if (g.of(batch.batchPath).state.load(std::memory_order_relaxed) != ProgramGpu::Ready ||
        g.lost.load(std::memory_order_relaxed))
        return;
    ctx->DrawIndexed(batch.indexCount, g.ib.drawOffset / (uint32_t)sizeof(uint32_t), 0);
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
    ctx->DrawIndexed(batch.indexCount, g.ib.drawOffset / (uint32_t)sizeof(uint32_t), 0);
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
        ready = reserveRing(i.dev.Get(), g.vb, vertexBytes, 8u << 20, D3D11_BIND_VERTEX_BUFFER) &&
                reserveRing(i.dev.Get(), g.ib, batch.indexCount * (uint32_t)sizeof(uint32_t), 2u << 20,
                            D3D11_BIND_INDEX_BUFFER) &&
                reserveRing(i.dev.Get(), g.previousVb, batch.vertexCount * (uint32_t)sizeof(motion::ProgramPrevious),
                            4u << 20, D3D11_BIND_VERTEX_BUFFER) &&
                reserveRing(i.dev.Get(), g.constants, constantBytes, 4u << 20, D3D11_BIND_SHADER_RESOURCE);
        if (ready && (!g.constantsSrv || before != g.constants.buffer.Get())) {
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
    if (bp) {
        // The exact bounds of the batch: the stand-ins span them instead of the scissor (program_draw::batchBounds)
        const program_draw::Bounds bounds = program_draw::batchBounds(
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
    // Notes the draw did not walk (no temporal world, a skipped draw): the scope still advances.
    if (!g.motionTracked)
        applyNotes();
}

} // namespace gsr
