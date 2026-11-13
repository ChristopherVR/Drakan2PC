// D3D11Backend: the BackendApi of this backend. The forwarding and the phase machine are shared (gs_backend_api.inl);
// this file holds the hooks it asks for and what only this backend answers (present wait, motion-scope batching,
// DLSS and neural rendering).
#include "d3d11_backend_impl.h"
#include "ray_status.h"

namespace gsr::backend_hook {

// Backend interface of gs_backend_api.inl.
inline void constructed(D3D11Backend::Impl& s, std::atomic<bool>& lostNow) {
    s.lost = &lostNow; // a removed device closes gsr::Backend's latch at the next forwarded call
}
inline void releaseBeforeWorkers(D3D11Backend::Impl& s) {
    if (s.upLog || std::getenv("GSR_MATERIAL_LOG"))
        std::fprintf(stderr, "[material] exit: mode %u, %llu material maps loaded (%.1f MB), %llu draws shaded\n",
                     (unsigned)s.materialMode, (unsigned long long)s.materialGpu.texturesLoaded,
                     s.materialGpu.bytes / 1048576.0, (unsigned long long)s.materialGpu.draws);
    s.joinPrewarm(); // its shader objects and ray device use the device
    s.tp.reset();    // release feature + NGX before context/device teardown
    s.combinedSr.reset();
    s.neuralGuides.reset();
    dlssShutdown(s.dev.Get());
}
inline void markGpuPass(D3D11Backend::Impl& s, gpupass::Pass pass, bool begin) {
    s.gpuPasses.mark(pass, begin);
}
inline void worldPhaseOpened(D3D11Backend::Impl& s) {
    if (s.neuralEnabled)
        s.neuralStatusText = "Waiting for a valid world camera, depth and render target.";
}
inline void motionWorldOpened(D3D11Backend::Impl& s) {
    s.motionOrdinals.clear();
}
inline void levelHistoryReset(D3D11Backend::Impl& s) {
    s.upReset = s.neuralReset = true;
    s.camPrevValid = false;
    s.neuralPrevious = {};
    s.modelMotion.reset();
    s.pendingModels.ready = false;
    s.pendingModels.scene.reset();
    s.pendingModels.previous.reset();
    s.foreignContext(); // the combined upscaler may use the context through its own pointer
    if (s.combinedSr)
        s.combinedSr->reset();
}
inline void sceneModelsNoted(D3D11Backend::Impl& s) {
    // Keep both snapshots alive until this world's motion preparation runs.
    if (s.nativeModels && s.modelMotion.previous() && (s.neuralEnabled || s.combinedSr) && s.surfaceBuilder->async()) {
        auto prepare = [&s, scene = s.nativeModels, previous = s.modelMotion.previous()] {
            auto& pending = s.pendingModels;
            motion::Models::prepareFrom(previous.get(), *scene, false, pending.frame, pending.scratch);
            pending.scene = scene;
            pending.previous = previous;
            pending.ready = true;
        };
        if (s.surfaceBuilder->preferIndependentPreparation())
            s.surfaceBuilder->stageIndependentTask(std::move(prepare));
        else
            s.surfaceBuilder->submitTask(std::move(prepare));
    }
}
inline bool rayDeviceMissing(D3D11Backend::Impl&, std::string&) {
    return false;
}
inline surface::RayWater* acquireRayPass(D3D11Backend::Impl& s) {
    return s.rayPass();
}
inline const surface::RayWater* rayPassOf(const D3D11Backend::Impl& s) {
    return s.waterRenderer.get();
}
constexpr const char* kRayReflectionsName = "DXR reflections";
constexpr const char* kRayImageSuffix = "";

} // namespace gsr::backend_hook

#define GSR_IMPL D3D11Backend::Impl
#define GSR_BACKEND D3D11Backend
#include "gs_backend_api.inl"
#undef GSR_BACKEND
#undef GSR_IMPL

namespace gsr {

void D3D11Backend::draw(const GsDrawState& st, const ClutBuf& clut, const DrawVertex* v, uint32_t count) {
    m_impl->pendingMotionBase = v;
    m_impl->pendingMotionCount = count;
    m_impl->draw(st, clut, v, count);
    // Notes the draw did not walk (no temporal world, a skipped or split draw): the scope still advances.
    if (m_impl->pendingMotionNotes)
        for (uint32_t i = 0; i < m_impl->pendingMotionNoteCount; ++i)
            m_impl->applyObjectMotion(m_impl->pendingMotionNotes[i].scope);
    m_impl->pendingMotionNotes = nullptr;
    m_impl->pendingMotionNoteCount = 0;
}
void D3D11Backend::drawSegments(const DrawSegment* segments, uint32_t segmentCount, const DrawVertex* v,
                                uint32_t count) {
    m_impl->pendingMotionNotes = nullptr; // the core never joins a batch with object-motion notes
    m_impl->pendingMotionNoteCount = 0;
    m_impl->drawSegments(segments, segmentCount, v, count);
}
bool D3D11Backend::deferObjectMotion() const {
    static const bool enabled = [] {
        const char* value = std::getenv("GSR_MOTION_BATCH");
        return !value || value[0] != '0';
    }();
    return enabled;
}
void D3D11Backend::setMotionNotes(const motion::Note* notes, uint32_t count) {
    m_impl->pendingMotionNotes = count ? notes : nullptr;
    m_impl->pendingMotionNoteCount = count;
}
void* D3D11Backend::beginPresentWait() {
    return m_impl->beginPresentWait();
}
bool D3D11Backend::waitPresentHandle(void* wait, uint32_t timeoutMs) {
    const bool ready = WaitForSingleObjectEx(wait, timeoutMs, FALSE) == WAIT_OBJECT_0;
    CloseHandle(wait);
    return ready;
}
void D3D11Backend::endPresentWait(bool ready) {
    m_impl->presentSlotHeld = ready;
}
void D3D11Backend::setVsync(bool on) {
    m_impl->vsyncOn = on;
}
void D3D11Backend::Impl::applyObjectMotion(const ObjectMotionScope& scope) {
    applyObjectScope(*this, scope);
}
double D3D11Backend::upscaleGpuMs() const {
    return m_impl->tp ? m_impl->tp->lastGpuMs() : 0.0;
}
void D3D11Backend::upscaleCut() {
    m_impl->foreignContext(); // the upscalers may use the context through their own pointers
    m_impl->discardTemporalWorlds();
    m_impl->neuralReset = true;
    m_impl->neuralPrevious = {};
    m_impl->motionHistory.reset();
    if (m_impl->combinedSr)
        m_impl->combinedSr->reset();
    m_impl->finalizeAllWorlds();
    m_impl->upReset = true;
    m_impl->camPrevValid = false;
    m_impl->camCur = {};
    for (auto& r : m_impl->rts)
        r->wstate = 0;
}
bool D3D11Backend::upscaleAvailable(std::string& status) const {
    return dlssAvailable(m_impl->dev.Get(), status);
}
bool D3D11Backend::neuralRenderingAvailable(std::string& status) const {
    return m_impl->neuralAvailable(status);
}
std::string D3D11Backend::neuralRenderingStatus() const {
    return m_impl->neuralStatus();
}

std::unique_ptr<BackendApi> createD3D11Backend(LocalMemory& mem, const Config& cfg, Stats& stats) {
    return std::make_unique<D3D11Backend>(mem, cfg, stats);
}
bool d3d11WaitPresentHandle(void* wait, uint32_t timeoutMs) {
    return D3D11Backend::waitPresentHandle(wait, timeoutMs);
}

} // namespace gsr
