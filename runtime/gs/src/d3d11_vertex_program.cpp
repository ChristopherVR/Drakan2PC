// Vertex-program draws on Direct3D 11 (gsr/vertex_program.h; the rules every backend shares are in
// vertex_program_draw.h, the shader text is generated: vertex_program_hlsl.h).
//
// A batch of records is drawn by a vertex shader that repeats the VU stage with the VU's arithmetic and then runs
// the renderer's ordinary VSMain, so everything after the vertex stage is the code of every other draw. This file is
// the API side only: the shaders (compiled on a background thread, FXC needs 6 to 10 s for them; until they exist
// vertexPrograms() says "not ready" and the core evaluates the records on the CPU), the buffers, and the bindings
// gs_draw.inl asks for. Nothing here is latched: the control word is read per record by the core, and a record this
// backend declines is evaluated on the CPU, so the path can go on and off at any frame boundary.
//
// Any failure (compile, buffer, device) turns the path off for the session with one log line; later records are
// counted under vpath::Fallback::Internal and evaluated on the CPU.
//
// Diagnosis: GSR_VERTEX_ARITHMETIC=doubles|limbs|off (default: vp::chooseArithmetic, integer limbs),
// GSR_VERTEX_PROGRAM_WAIT=1 (wait for the compile at the first record instead of drawing on the CPU meanwhile: for
// replays and tests that count shader-drawn vertices; never set in play).
#include "d3d11_backend_impl.h"
#include "vertex_program_hlsl.h"

#include <string>

namespace gsr {

struct D3D11Backend::Impl::ProgramGpu {
    enum State : int { Idle, Compiling, Ready, Failed, Unsupported };
    std::atomic<int> state{Idle};
    std::thread worker;
    vp::Arithmetic arithmetic = vp::Arithmetic::None;
    // Written by the worker before `state` becomes Ready, read by the render thread after.
    ComPtr<ID3D11VertexShader> vs, vsMotion;
    ComPtr<ID3D11InputLayout> layout, motionLayout;
    // Render thread only. Rings like the draw vertex buffer: NO_OVERWRITE appends, DISCARD on wrap.
    struct Ring {
        ComPtr<ID3D11Buffer> buffer;
        uint32_t capacity = 0, offset = 0, drawOffset = 0;
    } vb, ib, previousVb, constants;
    ComPtr<ID3D11ShaderResourceView> constantsSrv;
    ComPtr<ID3D11Buffer> cb, cbMotion;
    bool constantsAppend = false; // the device maps a shader-resource buffer with NO_OVERWRITE (Direct3D 11.1)
    bool logged = false;
    // The batch being drawn.
    std::vector<motion::ProgramPrevious> previous;
    std::vector<vp::Constants> previousConstants;
    std::vector<DrawVertex> fallback;
    bool motionTracked = false;
    uint64_t batches = 0, velocityBatches = 0;

    ~ProgramGpu() {
        if (worker.joinable())
            worker.join();
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

// Compiles the two entry points and creates their shader objects. Runs on the worker; device methods are
// free-threaded and the immediate context is not touched.
bool buildProgramShaders(ID3D11Device* dev, vp::Arithmetic arithmetic, ComPtr<ID3D11VertexShader>& vs,
                         ComPtr<ID3D11InputLayout>& layout, ComPtr<ID3D11VertexShader>& vsMotion,
                         ComPtr<ID3D11InputLayout>& motionLayout, std::string& error) {
    const std::string source = std::string(kHlslMain) + vertexProgramHlsl(arithmetic, vp::gameStages(), true);
    // The flags and HW_BLEND of the renderer's own VSMain: the inlined VSMain must compile to the same instructions.
    static const D3D_SHADER_MACRO kMacros[] = {{"HW_BLEND", "0"}, {"VP_MOTION", "1"}, {nullptr, nullptr}};
    ComPtr<ID3DBlob> blob[2];
    std::string errors[2];
    const auto compileOne = [&](int which, const char* entry) {
        ComPtr<ID3DBlob> err;
        if (FAILED(shader_compile::compile(source.c_str(), source.size(), "gs_vertex_program", kMacros, entry, "vs_5_0",
                                           D3DCOMPILE_OPTIMIZATION_LEVEL3, &blob[which], &err)))
            errors[which] = err ? std::string((const char*)err->GetBufferPointer(), err->GetBufferSize()) : "?";
    };
    std::thread second([&] {
        shader_compile::markPrewarmThread();
        compileOne(1, "VSProgramMotion");
    });
    compileOne(0, "VSProgram");
    second.join();
    if (!blob[0] || !blob[1]) {
        error = "compile: " + (blob[0] ? errors[1] : errors[0]);
        return false;
    }
    if (FAILED(dev->CreateVertexShader(blob[0]->GetBufferPointer(), blob[0]->GetBufferSize(), nullptr, &vs)) ||
        FAILED(dev->CreateInputLayout(kProgramLayout, 4, blob[0]->GetBufferPointer(), blob[0]->GetBufferSize(),
                                      &layout)) ||
        FAILED(dev->CreateVertexShader(blob[1]->GetBufferPointer(), blob[1]->GetBufferSize(), nullptr, &vsMotion)) ||
        FAILED(dev->CreateInputLayout(kProgramLayout, 6, blob[1]->GetBufferPointer(), blob[1]->GetBufferSize(),
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

void D3D11Backend::Impl::programFailed(const char* what) {
    ProgramGpu& g = programState();
    if (g.state.exchange(ProgramGpu::Failed) != ProgramGpu::Failed && !g.logged) {
        g.logged = true;
        std::fprintf(stderr,
                     "[gsr] vertex programs: %s; records are evaluated on the CPU for the rest of the session\n", what);
    }
}

// Whether a record may be drawn by the shader right now. Starts the background compile at the first request.
bool D3D11Backend::Impl::programTakes(vpath::Fallback* why) {
    ProgramGpu& g = programState();
    int state = g.state.load(std::memory_order_acquire);
    if (state == ProgramGpu::Idle) {
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
        if (g.arithmetic == vp::Arithmetic::None) {
            g.state.store(state = ProgramGpu::Unsupported, std::memory_order_release);
            std::fprintf(stderr, "[gsr] vertex programs: no exact arithmetic chosen for this adapter, records are "
                                 "evaluated on the CPU\n");
        } else {
            g.state.store(state = ProgramGpu::Compiling, std::memory_order_release);
            ComPtr<ID3D11Device> device = dev;
            ProgramGpu* target = &g;
            g.worker = std::thread([device, target] {
                shader_compile::markPrewarmThread();
                const auto t0 = std::chrono::steady_clock::now();
                std::string error;
                const bool built = buildProgramShaders(device.Get(), target->arithmetic, target->vs, target->layout,
                                                       target->vsMotion, target->motionLayout, error);
                const double ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                if (built)
                    std::fprintf(stderr, "[gsr] vertex programs: shaders ready after %.0f ms (%s arithmetic)\n", ms,
                                 vp::arithmeticName(target->arithmetic));
                else
                    std::fprintf(stderr,
                                 "[gsr] vertex programs: %s; records are evaluated on the CPU for the rest of the "
                                 "session\n",
                                 error.c_str());
                target->logged = !built;
                target->state.store(built ? ProgramGpu::Ready : ProgramGpu::Failed, std::memory_order_release);
            });
            if (const char* wait = std::getenv("GSR_VERTEX_PROGRAM_WAIT"); wait && wait[0] == '1') {
                g.worker.join();
                state = g.state.load(std::memory_order_acquire);
            }
        }
    }
    program_draw::BackendState b;
    b.shaderFailed = state == ProgramGpu::Failed || !ok;
    b.arithmetic = state != ProgramGpu::Unsupported;
    b.shaderReady = state == ProgramGpu::Ready;
    b.worldOpen = engineWorldOpen;
    b.hudMapped = hudScale < 0.999f || std::fabs(hudSize - 1.0f) > 0.001f || sceneCamera.guiScreen == 14 ||
                  inventoryHudFrame.applies(false, 640) || nativeSubtitleOpen;
    b.rayEffects = modernWater || rayShadows || rayReflections || rayGlobalIllumination;
    b.terrainDetail = terrainCfg.any() || runtimeFlags().terrainAudit;
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
    const uint32_t capacity = std::max(bytes * 4u, minimum);
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = capacity;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = bind;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ComPtr<ID3D11Buffer> buffer;
    if (FAILED(dev->CreateBuffer(&bd, nullptr, &buffer)))
        return false;
    ring.buffer = std::move(buffer);
    ring.capacity = capacity;
    ring.offset = capacity; // the first map discards
    return true;
}
// Appends to a ring (`unit`: the offset is kept a multiple of it). `count` blocks are copied one after the other.
template <class Ring>
bool appendRing(ID3D11DeviceContext* ctx, Ring& ring, bool append, uint32_t unit, const void* const* data,
                const uint32_t* sizes, uint32_t count) {
    uint32_t bytes = 0;
    for (uint32_t i = 0; i < count; ++i)
        bytes += sizes[i];
    uint32_t offset = (ring.offset + unit - 1) / unit * unit;
    D3D11_MAP mode = D3D11_MAP_WRITE_NO_OVERWRITE;
    if (!append || offset > ring.capacity || bytes > ring.capacity - offset) {
        mode = D3D11_MAP_WRITE_DISCARD;
        offset = 0;
    }
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (bytes > ring.capacity || FAILED(ctx->Map(ring.buffer.Get(), 0, mode, 0, &ms)))
        return false;
    uint8_t* out = static_cast<uint8_t*>(ms.pData) + offset;
    for (uint32_t i = 0; i < count; ++i) {
        std::memcpy(out, data[i], sizes[i]);
        out += sizes[i];
    }
    ctx->Unmap(ring.buffer.Get(), 0);
    ring.drawOffset = offset;
    ring.offset = offset + bytes;
    return true;
}
} // namespace

// The previous state of every record of the batch, for the velocity pass. Called by gs_draw.inl where an ordinary
// draw calls prepareObjectMotion, under the same condition, so both histories see the same worlds.
bool D3D11Backend::Impl::prepareProgramMotion(RT& rt, const GsDrawState& s, const VertexProgramBatch& batch, float sx,
                                              float sy) {
    ProgramGpu& g = programState();
    const bool active = motionWorldOpen && (neuralEnabled || upMode) && rt.neuralState == 1;
    if (!active || !ensureMotionTarget(rt))
        return false;
    const auto counts = program_draw::trackBatch(
        motionHistory, batch, s, batch.constantCount, g.previous, g.previousConstants,
        [&](const ObjectMotionScope& scope) { applyObjectMotion(scope); },
        [&]() -> const ObjectMotionScope& { return objectScope; });
    g.motionTracked = true;
    motionScopedBatches += counts.scoped;
    motionMatchedBatches += counts.matched;
    motionMatchedVertices += counts.matchedVertices;
    motionRejectedBatches += counts.records - counts.matched;
    const void* data[1] = {g.previous.data()};
    const uint32_t sizes[1] = {batch.vertexCount * (uint32_t)sizeof(motion::ProgramPrevious)};
    if (!appendRing(ctx.Get(), g.previousVb, true, (uint32_t)sizeof(motion::ProgramPrevious), data, sizes, 1)) {
        probeDevice("vertex program previous stream");
        programFailed("the previous-frame vertex stream could not be written");
        g.previousConstants.clear();
        const float clear[4]{};
        ctx->ClearRenderTargetView(rt.motionRtv.Get(), clear);
        return false;
    }
    const float raster[4] = {sx, sy, 0, 0};
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (FAILED(map(g.cbMotion.Get(), D3D11_MAP_WRITE_DISCARD, ms, "vertex program motion constants")))
        return false;
    std::memcpy(ms.pData, raster, sizeof(raster));
    ctx->Unmap(g.cbMotion.Get(), 0);
    ++g.velocityBatches;
    return true;
}

// After bindDraw: the batch's vertices, indices, constants (its own, then the previous ones prepareProgramMotion
// found) and vertex shader replace the stand-ins'.
void D3D11Backend::Impl::bindProgramDraw(const GsDrawState& s, const VertexProgramBatch& batch) {
    ProgramGpu& g = programState();
    const void* constantData[2] = {batch.constants, g.previousConstants.data()};
    const uint32_t constantSizes[2] = {batch.constantCount * (uint32_t)sizeof(vp::Constants),
                                       (uint32_t)(g.previousConstants.size() * sizeof(vp::Constants))};
    const void* vertexData[1] = {batch.vertices};
    const uint32_t vertexSizes[1] = {batch.vertexCount * (uint32_t)sizeof(VertexProgramVertex)};
    const void* indexData[1] = {batch.indices};
    const uint32_t indexSizes[1] = {batch.indexCount * (uint32_t)sizeof(uint32_t)};
    if (!appendRing(ctx.Get(), g.constants, g.constantsAppend, (uint32_t)sizeof(vp::Constants), constantData,
                    constantSizes, 2) ||
        !appendRing(ctx.Get(), g.vb, true, (uint32_t)sizeof(VertexProgramVertex), vertexData, vertexSizes, 1) ||
        !appendRing(ctx.Get(), g.ib, true, (uint32_t)sizeof(uint32_t), indexData, indexSizes, 1)) {
        // Only a removed device gets here (the buffers were reserved before the draw started); nothing can be drawn
        // with it any more.
        probeDevice("vertex program buffers");
        programFailed("a buffer of the draw could not be written");
        return;
    }
    const int32_t words[4] = {(int32_t)(s.xyoffset & 0xFFFF), (int32_t)((s.xyoffset >> 32) & 0xFFFF),
                              (int32_t)(g.constants.drawOffset / 16u), 0};
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (SUCCEEDED(map(g.cb.Get(), D3D11_MAP_WRITE_DISCARD, ms, "vertex program constants"))) {
        std::memcpy(ms.pData, words, sizeof(words));
        ctx->Unmap(g.cb.Get(), 0);
    }
    const UINT stride = sizeof(VertexProgramVertex), offset = g.vb.drawOffset;
    ctx->IASetInputLayout(g.layout.Get());
    ctx->IASetVertexBuffers(0, 1, g.vb.buffer.GetAddressOf(), &stride, &offset);
    ctx->IASetIndexBuffer(g.ib.buffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    ctx->VSSetShader(g.vs.Get(), nullptr, 0);
    ID3D11Buffer* buffers[2] = {g.cb.Get(), g.cbMotion.Get()};
    ctx->VSSetConstantBuffers(5, 2, buffers);
    ctx->VSSetShaderResources(8, 1, g.constantsSrv.GetAddressOf());
    ++g.batches;
}

void D3D11Backend::Impl::drawProgramVertices(const VertexProgramBatch& batch) {
    ProgramGpu& g = programState();
    if (g.state.load(std::memory_order_relaxed) != ProgramGpu::Ready)
        return;
    ctx->DrawIndexed(batch.indexCount, g.ib.drawOffset / (uint32_t)sizeof(uint32_t), 0);
}

// The velocity pass of the colour pass just drawn (compare drawVelocity in d3d11_draw.cpp): the same targets, depth
// rule and pixel shader, with the program's velocity entry point and the previous stream in slot 1.
void D3D11Backend::Impl::drawProgramVelocity(RT& rt, DepthBuf& db, const VertexProgramBatch& batch,
                                             rhi::Compare depthCompare, bool restore, bool materialDraw, bool useHw) {
    ProgramGpu& g = programState();
    if (g.state.load(std::memory_order_relaxed) != ProgramGpu::Ready)
        return;
    static const D3D11_COMPARISON_FUNC kCompare[] = {D3D11_COMPARISON_NEVER,         D3D11_COMPARISON_LESS,
                                                     D3D11_COMPARISON_EQUAL,         D3D11_COMPARISON_LESS_EQUAL,
                                                     D3D11_COMPARISON_GREATER,       D3D11_COMPARISON_NOT_EQUAL,
                                                     D3D11_COMPARISON_GREATER_EQUAL, D3D11_COMPARISON_ALWAYS};
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ctx->OMSetRenderTargets(1, rt.motionRtv.GetAddressOf(), db.dsv.Get());
    ctx->OMSetBlendState(blendOff.Get(), nullptr, 0xffffffffu);
    ctx->OMSetDepthStencilState(getDepthState(kCompare[(size_t)depthCompare], false), 0);
    ctx->IASetInputLayout(g.motionLayout.Get());
    const UINT stride = sizeof(motion::ProgramPrevious), offset = g.previousVb.drawOffset;
    ctx->IASetVertexBuffers(1, 1, g.previousVb.buffer.GetAddressOf(), &stride, &offset);
    ctx->VSSetShader(g.vsMotion.Get(), nullptr, 0);
    ctx->PSSetShader(psMotion.Get(), nullptr, 0);
    ctx->DrawIndexed(batch.indexCount, g.ib.drawOffset / (uint32_t)sizeof(uint32_t), 0);
    if (!restore)
        return;
    ID3D11RenderTargetView* rtvs[1] = {rt.rtv.Get()};
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ctx->OMSetRenderTargets(1, rtvs, db.dsv.Get());
    ctx->IASetInputLayout(g.layout.Get());
    ID3D11Buffer* nullBuffer = nullptr;
    const UINT zero = 0;
    ctx->IASetVertexBuffers(1, 1, &nullBuffer, &zero, &zero);
    ctx->VSSetShader(g.vs.Get(), nullptr, 0);
    ID3D11PixelShader* const colorShader =
        materialDraw ? (useHw ? materialGpu.psHw.Get() : materialGpu.psSb.Get()) : (useHw ? psHw.Get() : psSb.Get());
    ctx->PSSetShader(colorShader, nullptr, 0);
}

bool D3D11Backend::vertexPrograms(vpath::Fallback* why) const {
    return m_impl->programTakes(why);
}

vpath::Capability D3D11Backend::vertexPathCapability() const {
    // The integer library needs nothing optional, so every Direct3D 11 adapter has the arithmetic unless the
    // diagnosis switch asks for doubles on an adapter without them, or for none. `proven` is set by the project
    // per combination after the full proof, not here.
    D3D11_FEATURE_DATA_DOUBLES doubles{};
    const bool doubleSupport =
        m_impl->dev && SUCCEEDED(m_impl->dev->CheckFeatureSupport(D3D11_FEATURE_DOUBLES, &doubles, sizeof(doubles))) &&
        doubles.DoublePrecisionFloatShaderOps;
    vpath::Capability c;
    c.backendHasPath = m_impl->ok;
    c.adapterExact = m_impl->ok &&
                     vp::chooseArithmetic(vp::ShaderBackend::Direct3D11, doubleSupport,
                                          vp::parseArithmeticRequest(std::getenv("GSR_VERTEX_ARITHMETIC"))) !=
                         vp::Arithmetic::None &&
                     !(m_impl->programGpu && m_impl->programGpu->state.load() == Impl::ProgramGpu::Failed);
    c.proven = false;
    c.shaderStages = vpath::kStrips; // the batch path has no shader on this backend yet
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
    // Everything that can fail for want of memory happens before the draw starts, so a failure can still be drawn
    // exactly: the batch is then evaluated here like the core evaluates a record.
    bool ready = g.state.load(std::memory_order_acquire) == Impl::ProgramGpu::Ready && i.ok;
    if (ready) {
        const bool hadConstants = g.constants.buffer != nullptr;
        const ID3D11Buffer* const before = g.constants.buffer.Get();
        ready = reserveRing(i.dev.Get(), g.vb, batch.vertexCount * (uint32_t)sizeof(VertexProgramVertex), 8u << 20,
                            D3D11_BIND_VERTEX_BUFFER) &&
                reserveRing(i.dev.Get(), g.ib, batch.indexCount * (uint32_t)sizeof(uint32_t), 2u << 20,
                            D3D11_BIND_INDEX_BUFFER) &&
                reserveRing(i.dev.Get(), g.previousVb, batch.vertexCount * (uint32_t)sizeof(motion::ProgramPrevious),
                            4u << 20, D3D11_BIND_VERTEX_BUFFER) &&
                reserveRing(i.dev.Get(), g.constants, 2u * batch.constantCount * (uint32_t)sizeof(vp::Constants),
                            4u << 20, D3D11_BIND_SHADER_RESOURCE);
        if (ready && (!hadConstants || before != g.constants.buffer.Get())) {
            D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
            sd.Format = DXGI_FORMAT_R32G32B32A32_UINT;
            sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            sd.Buffer.NumElements = g.constants.capacity / 16u;
            g.constantsSrv.Reset();
            ready = SUCCEEDED(i.dev->CreateShaderResourceView(g.constants.buffer.Get(), &sd, &g.constantsSrv));
        }
        if (ready && !g.cb) {
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = 16;
            bd.Usage = D3D11_USAGE_DYNAMIC;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            ready = SUCCEEDED(i.dev->CreateBuffer(&bd, nullptr, &g.cb)) &&
                    SUCCEEDED(i.dev->CreateBuffer(&bd, nullptr, &g.cbMotion));
        }
        if (!ready) {
            i.probeDevice("vertex program buffer creation");
            i.programFailed("a buffer could not be created");
        }
    }
    if (!ready) {
        // The exact CPU evaluation of the batch as a triangle list, through the ordinary draw (no motion history
        // for this one batch).
        const int ofx = (int)(st.xyoffset & 0xFFFF), ofy = (int)((st.xyoffset >> 32) & 0xFFFF);
        g.fallback.resize(batch.indexCount);
        for (uint32_t n = 0; n < batch.indexCount; ++n) {
            const VertexProgramVertex& v = batch.vertices[batch.indices[n]];
            const vp::Output o = vp::evaluate(v.program, batch.constants[v.constants], v.in);
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
        vpath::countFallback(vpath::Fallback::Internal);
        applyNotes();
        i.pendingMotionBase = g.fallback.data();
        i.pendingMotionCount = batch.indexCount;
        i.draw(st, clut, g.fallback.data(), batch.indexCount);
        return;
    }
    DrawVertex standIns[3];
    program_draw::standIns(st, standIns);
    g.previousConstants.clear();
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
