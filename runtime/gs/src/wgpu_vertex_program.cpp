// Vertex-program draws on WebGPU (gsr/vertex_program.h; the rules every backend shares are in vertex_program_draw.h,
// the shader text is generated: vertex_program_wgsl_draw.h). The same path on Vulkan and Direct3D 12 (wgpu-native)
// and, later, in a browser.
//
// A batch of records is drawn by a vertex stage that repeats the VU stage with the VU's arithmetic (the integer
// library) and then runs the body of the renderer's ordinary vertex shader, so everything after the vertex stage is
// the code of every other draw. This file is the API side only: the program (created on a background thread; until
// it exists vertexPrograms() says "not ready" and the core evaluates the records on the CPU), the per-draw data, and
// the bindings gs_draw.inl asks for. The constants reach the vertex stage as the device's draw data
// (rhi::Device::drawDataPath: a storage buffer where the device allows one in the vertex stage, else an rgba32uint
// texture). Render pipelines of the program are created in the background per blend / depth state
// (rhi::ProgramDesc::backgroundPipelines): a batch whose pipeline is not ready yet is evaluated here on the CPU and
// drawn with the ordinary program under the same state, counted as "not ready". Never a stall, never another pixel.
//
// Nothing is latched: the control word is read per record by the core and a record this backend declines is
// evaluated on the CPU, so the path can go on and off at any frame boundary. Any failure (module, pipeline, draw
// data, a lost device) turns the path off for the session with one log line; later records are counted under
// vpath::Fallback::Internal (or DeviceLost) and evaluated on the CPU.
//
// Motion vectors: this backend has none (kCaps.motionVectors is false; its temporal AA reprojects with the camera
// only), so gs_draw.inl never calls prepareProgramMotion or drawProgramVelocity here.
//
// Diagnosis: GSR_VERTEX_ARITHMETIC=off (no shader), GSR_VERTEX_CONSTANTS=texture (the texture path where a storage
// buffer would do), GSR_VERTEX_PROGRAM_WAIT=1 (wait for the program and for each pipeline instead of drawing on the
// CPU meanwhile: for replays and tests that count shader-drawn vertices; never set in play).
#include "wgpu_backend_impl.h"
#include "vertex_program_draw.h"
#include "vertex_program_wgsl_draw.h"

#include <string>
#include <thread>

namespace gsr {

struct WgpuBackend::Impl::ProgramGpu {
    enum State : int { Idle, Compiling, Ready, Failed, Unsupported };
    std::atomic<int> state{Idle};
    std::thread worker;
    // Written by the worker before `state` becomes Ready, read by the render thread after.
    rhi::ProgramPtr programHw, programSb;
    std::string error;
    bool logged = false;
    bool wait = false; // GSR_VERTEX_PROGRAM_WAIT
    // The batch being drawn (render thread).
    rhi::Program* ordinary = nullptr; // the program bindDraw bound
    rhi::Program* program = nullptr;  // its vertex-program twin, null when this batch is drawn from the CPU
    uint64_t xyoffset = 0;
    const VertexProgramBatch* evaluatedFor = nullptr;
    std::vector<DrawVertex> evaluated; // the batch evaluated on the CPU, as a triangle list
    bool shaderPass = false, cpuPass = false;
    vpath::Fallback cpuReason = vpath::Fallback::NotReady;
    // Counts for the summary line at the end of the session.
    uint64_t shaderBatches = 0, shaderVertices = 0, notReadyBatches = 0, notReadyVertices = 0, failedBatches = 0;
    uint64_t skippedBatches = 0, skippedVertices = 0;

    ~ProgramGpu() {
        if (worker.joinable())
            worker.join();
        if (shaderBatches || notReadyBatches || failedBatches || skippedBatches)
            std::fprintf(stderr,
                         "[gsr-webgpu] vertex programs: %llu batches (%llu vertices) drawn by the shader, %llu batches "
                         "(%llu vertices) on the CPU while their pipeline was created, %llu after a failure, %llu "
                         "(%llu vertices) with nothing to draw\n",
                         (unsigned long long)shaderBatches, (unsigned long long)shaderVertices,
                         (unsigned long long)notReadyBatches, (unsigned long long)notReadyVertices,
                         (unsigned long long)failedBatches, (unsigned long long)skippedBatches,
                         (unsigned long long)skippedVertices);
    }
};

namespace {
// The program of a vertex-program draw: the renderer's draw program (`hw`: fixed-function blend) with the vertex
// stage of vertex_program_wgsl_draw.h, integer attributes of VertexProgramVertex, a second uniform slot (vpDraw)
// and the draw data.
rhi::ProgramDesc programDesc(bool hw, rhi::Device::DrawDataPath path) {
    rhi::ProgramDesc d;
    d.wgsl = vertexProgramDrawWgsl(hw, path == rhi::Device::DrawDataPath::Texture ? VpConstantsPath::Texture
                                                                                  : VpConstantsPath::StorageBuffer);
    d.vertexEntry = "vsProgram";
    d.uniformSlots = 2;
    d.textureSlots = hw ? 1 : 2;
    d.samplers = true;
    d.vertexStride = sizeof(VertexProgramVertex);
    using T = rhi::ProgramDesc::AttributeType;
    d.attributes = {{0, 0, 4, T::Uint}, {1, 16, 4, T::Uint}, {2, 32, 4, T::Sint}, {3, 48, 4, T::Uint}};
    d.dualSource = hw;
    d.drawData = true;
    d.backgroundPipelines = true;
    d.label = hw ? "gs vertex program, hw blend" : "gs vertex program, shader blend";
    return d;
}

bool waitRequested() {
    const char* wait = std::getenv("GSR_VERTEX_PROGRAM_WAIT");
    return wait && wait[0] == '1';
}

// Moves a batch's vertices from the core's "drawn by a shader" count to "evaluated on the CPU": the core counts a
// record when it hands it over, before this backend knows whether its pipeline is ready.
void countOnCpu(uint64_t vertices) {
    auto& c = vpath::state().counters;
    c.gpuVertices.fetch_sub(vertices, std::memory_order_relaxed);
    c.cpuVertices.fetch_add(vertices, std::memory_order_relaxed);
}
} // namespace

WgpuBackend::Impl::ProgramGpu& WgpuBackend::Impl::programState() {
    if (!programGpu)
        programGpu = std::make_shared<ProgramGpu>();
    return *programGpu;
}

void WgpuBackend::Impl::programFailed(const char* what) {
    ProgramGpu& g = programState();
    if (g.state.exchange(ProgramGpu::Failed) != ProgramGpu::Failed && !g.logged) {
        g.logged = true;
        std::fprintf(stderr,
                     "[gsr-webgpu] vertex programs: %s; records are evaluated on the CPU for the rest of the session\n",
                     what);
    }
}

// Whether a record may be drawn by the shader right now. Starts the background build at the first request.
bool WgpuBackend::Impl::programTakes(vpath::Fallback* why) {
    ProgramGpu& g = programState();
    if (!ok || !gpu || gpu->lost()) {
        if (why)
            *why = gpu && gpu->lost() ? vpath::Fallback::DeviceLost : vpath::Fallback::Internal;
        return false;
    }
    int state = g.state.load(std::memory_order_acquire);
    if (state == ProgramGpu::Idle) {
        const rhi::Device::DrawDataPath path = gpu->drawDataPath();
        const vp::Arithmetic arithmetic = vp::chooseArithmetic(
            vp::ShaderBackend::WebGpu, false, vp::parseArithmeticRequest(std::getenv("GSR_VERTEX_ARITHMETIC")));
        g.wait = waitRequested();
        if (path == rhi::Device::DrawDataPath::None || arithmetic == vp::Arithmetic::None) {
            g.state.store(state = ProgramGpu::Unsupported, std::memory_order_release);
            std::fprintf(stderr, "[gsr-webgpu] vertex programs: %s; records are evaluated on the CPU\n",
                         path == rhi::Device::DrawDataPath::None ? "the device has no data path to the vertex stage"
                                                                 : "no exact arithmetic chosen");
        } else {
            std::fprintf(stderr, "[gsr-webgpu] vertex programs: building the program (constants in a %s)\n",
                         path == rhi::Device::DrawDataPath::StorageBuffer ? "storage buffer" : "rgba32uint texture");
            g.state.store(state = ProgramGpu::Compiling, std::memory_order_release);
            rhi::Device* device = gpu.get();
            ProgramGpu* target = &g;
            const bool hw = progHw != nullptr;
            const auto build = [device, target, hw, path] {
                const auto t0 = std::chrono::steady_clock::now();
                std::string error;
                if (hw)
                    target->programHw = device->createProgram(programDesc(true, path), &error);
                if (!hw || target->programHw)
                    target->programSb = device->createProgram(programDesc(false, path), &error);
                const bool built = (!hw || target->programHw) && target->programSb;
                const double ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                if (built)
                    std::fprintf(stderr, "[gsr-webgpu] vertex programs: program ready after %.0f ms\n", ms);
                else
                    std::fprintf(stderr,
                                 "[gsr-webgpu] vertex programs: the program failed (%s); records are evaluated on the "
                                 "CPU for the rest of the session\n",
                                 error.c_str());
                target->logged = !built;
                target->state.store(built ? ProgramGpu::Ready : ProgramGpu::Failed, std::memory_order_release);
            };
            if (gpu->concurrentPrograms()) {
                g.worker = std::thread(build);
                if (g.wait)
                    g.worker.join();
            } else {
                build();
            }
            state = g.state.load(std::memory_order_acquire);
        }
    }
    program_draw::BackendState b;
    b.shaderFailed = state == ProgramGpu::Failed;
    b.arithmetic = state != ProgramGpu::Unsupported;
    b.shaderReady = state == ProgramGpu::Ready;
    b.worldOpen = engineWorldOpen;
    b.hudMapped = hudScale < 0.999f || std::fabs(hudSize - 1.0f) > 0.001f || sceneCamera.guiScreen == 14 ||
                  inventoryHudFrame.applies(false, 640) || nativeSubtitleOpen;
    b.rayEffects = modernWater || rayShadows || rayReflections || rayGlobalIllumination;
    b.terrainDetail = runtimeFlags().terrainAudit;
    b.lodStatistic = lodStat;
    b.vertexDiagnosis = runtimeFlags().surfaceAudit || cstr(runtimeFlags().surfacePick) != nullptr;
    return program_draw::backendTakes(b, why);
}

// Behind kCaps.motionVectors, which is false here: never called.
bool WgpuBackend::Impl::prepareProgramMotion(RT&, const GsDrawState&, const VertexProgramBatch&, float, float) {
    return false;
}
void WgpuBackend::Impl::drawProgramVelocity(RT&, DepthBuf&, const VertexProgramBatch&, rhi::Compare, bool, bool, bool) {
}

// After bindDraw: the batch's constants and vertex program replace the stand-ins'. The program is the twin of the
// one bindDraw bound (fixed-function or shader blend).
void WgpuBackend::Impl::bindProgramDraw(const GsDrawState& s, const VertexProgramBatch& batch) {
    ProgramGpu& g = programState();
    g.ordinary = gpu->currentProgram();
    g.program = nullptr;
    if (g.state.load(std::memory_order_relaxed) != ProgramGpu::Ready)
        return;
    rhi::Program* twin = g.ordinary == progHw.get() ? g.programHw.get() : g.programSb.get();
    if (!twin || g.ordinary == nullptr)
        return;
    const uint32_t first = gpu->writeDrawData(batch.constants, batch.constantCount * (uint32_t)sizeof(vp::Constants));
    if (first == ~0u) {
        // drawVertexProgram reserved the room; only a lost device gets here
        programFailed(gpu->lost() ? "the device was lost" : "the constants could not be written");
        g.cpuReason = gpu->lost() ? vpath::Fallback::DeviceLost : vpath::Fallback::Internal;
        return;
    }
    const int32_t words[4] = {(int32_t)(s.xyoffset & 0xFFFF), (int32_t)((s.xyoffset >> 32) & 0xFFFF), (int32_t)first,
                              0};
    gpu->setProgram(twin);
    gpu->setUniforms(1, words, sizeof(words));
    g.program = twin;
}

// One pass over the batch: by the shader when the pipeline of this pass's state exists, otherwise the batch
// evaluated on the CPU through the ordinary program under the same state (the vertex the ordinary vertex shader
// gets there is the one the program's stage makes: tests/test_vertex_program_webgpu.cpp).
void WgpuBackend::Impl::drawProgramVertices(const VertexProgramBatch& batch) {
    ProgramGpu& g = programState();
    if (g.program) {
        rhi::Device::PipelineState pipeline = gpu->pipelineState();
        if (pipeline == rhi::Device::PipelineState::Pending && g.wait) {
            gpu->waitPipelines();
            pipeline = gpu->pipelineState();
        }
        if (pipeline == rhi::Device::PipelineState::Ready &&
            gpu->drawIndexed(batch.vertices, batch.vertexCount, batch.indices, batch.indexCount)) {
            g.shaderPass = true;
            return;
        }
        if (pipeline == rhi::Device::PipelineState::Pending) {
            g.cpuReason = vpath::Fallback::NotReady;
        } else {
            programFailed(gpu->lost()                                      ? "the device was lost"
                          : pipeline == rhi::Device::PipelineState::Failed ? "a pipeline could not be created"
                                                                           : "a draw could not be recorded");
            g.cpuReason = gpu->lost() ? vpath::Fallback::DeviceLost : vpath::Fallback::Internal;
        }
    }
    // The exact CPU evaluation as a triangle list, as the core would have drawn the records.
    if (g.evaluatedFor != &batch) {
        const int ofx = (int)(g.xyoffset & 0xFFFF), ofy = (int)((g.xyoffset >> 32) & 0xFFFF);
        g.evaluated.resize(batch.indexCount);
        for (uint32_t n = 0; n < batch.indexCount; ++n) {
            const VertexProgramVertex& v = batch.vertices[batch.indices[n]];
            const vp::Output o = vp::evaluate(v.program, batch.constants[v.constants], v.in);
            DrawVertex& d = g.evaluated[n];
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
        g.evaluatedFor = &batch;
    }
    if (!g.ordinary)
        return;
    gpu->setProgram(g.ordinary);
    gpu->draw(g.evaluated.data(), batch.indexCount);
    if (g.program) // a second pass of this draw may find its pipeline
        gpu->setProgram(g.program);
    g.cpuPass = true;
}

bool WgpuBackend::vertexPrograms(vpath::Fallback* why) const {
    return m_impl->programTakes(why);
}

vpath::Capability WgpuBackend::vertexPathCapability() const {
    // The integer library needs nothing optional. What the device must have is a way to the vertex stage for the
    // constants (rhi::Device::drawDataPath; the browser proxy has none yet). `proven` is set by the project per
    // combination after the full proof, not here.
    const Impl& i = *m_impl;
    vpath::Capability c;
    c.backendHasPath = i.ok && i.gpu && i.gpu->drawDataPath() != rhi::Device::DrawDataPath::None;
    c.adapterExact = c.backendHasPath && !i.gpu->lost() &&
                     vp::chooseArithmetic(vp::ShaderBackend::WebGpu, false,
                                          vp::parseArithmeticRequest(std::getenv("GSR_VERTEX_ARITHMETIC"))) !=
                         vp::Arithmetic::None &&
                     !(i.programGpu && i.programGpu->state.load() == Impl::ProgramGpu::Failed);
    c.proven = false;
    c.shaderStages = vpath::kStrips; // the batch path has no shader on this backend yet
    return c;
}

void WgpuBackend::drawVertexProgram(const GsDrawState& st, const ClutBuf& clut, const VertexProgramBatch& batch) {
    Impl& i = *m_impl;
    Impl::ProgramGpu& g = i.programState();
    const auto applyNotes = [&] {
        program_draw::applyNotes(batch, [&](const ObjectMotionScope& scope) { noteObjectMotion(scope); });
    };
    if (!batch.indexCount || !batch.vertexCount) {
        applyNotes();
        return;
    }
    // Room for everything the draw writes, before it starts: nothing in between may submit (rhi::reserveDraw).
    // The vertex ring holds either the batch's vertices and indices or, for a pass drawn from the CPU, the evaluated
    // triangle list.
    const uint64_t vertexBytes = std::max<uint64_t>(uint64_t(batch.vertexCount) * sizeof(VertexProgramVertex),
                                                    uint64_t(batch.indexCount) * sizeof(DrawVertex));
    g.cpuReason = vpath::Fallback::NotReady;
    if (!i.gpu->reserveDraw(vertexBytes, uint64_t(batch.indexCount) * 4u,
                            uint64_t(batch.constantCount) * sizeof(vp::Constants))) {
        i.programFailed(i.gpu->lost() ? "the device was lost" : "a batch does not fit the draw rings");
        g.cpuReason = i.gpu->lost() ? vpath::Fallback::DeviceLost : vpath::Fallback::Internal;
    }
    g.evaluatedFor = nullptr;
    g.shaderPass = g.cpuPass = false;
    g.xyoffset = st.xyoffset;
    DrawVertex standIns[3];
    program_draw::standIns(st, standIns);
    i.programBatch = &batch;
    i.draw(st, clut, standIns, 3);
    i.programBatch = nullptr;
    g.ordinary = g.program = nullptr;
    applyNotes(); // no motion vectors here: the scope still advances exactly as for an ordinary batch
    if (g.cpuPass) {
        countOnCpu(batch.vertexCount);
        vpath::countFallback(g.cpuReason);
        if (g.cpuReason == vpath::Fallback::NotReady) {
            ++g.notReadyBatches;
            g.notReadyVertices += batch.vertexCount;
        } else {
            ++g.failedBatches;
        }
    } else if (g.shaderPass) {
        ++g.shaderBatches;
        g.shaderVertices += batch.vertexCount;
    } else { // the shared draw ended before its passes (nothing to draw), as it would for the CPU evaluation
        ++g.skippedBatches;
        g.skippedVertices += batch.vertexCount;
    }
}

} // namespace gsr
