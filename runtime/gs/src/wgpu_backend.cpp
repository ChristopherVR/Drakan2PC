// WgpuBackend: the BackendApi of this backend (WebGPU backend, wgpu_backend_impl.h). The forwarding and the phase
// machine are shared (gs_backend_api.inl); this file holds the hooks it asks for and what only this backend answers
// (device loss, surface pacing, temporal AA status).
#include "wgpu_backend_impl.h"
#include "gpu_pass_stats.h"
#include "ray_status.h"

namespace gsr::backend_hook {

// Backend interface of gs_backend_api.inl.
inline void constructed(WgpuBackend::Impl&, std::atomic<bool>&) {} // a lost device is reported by deviceLost
inline void releaseBeforeWorkers(WgpuBackend::Impl&) {}
inline void markGpuPass(WgpuBackend::Impl&, gpupass::Pass, bool) {} // no GPU pass timer on this backend
inline void worldPhaseOpened(WgpuBackend::Impl&) {}
inline void motionWorldOpened(WgpuBackend::Impl&) {}
inline void levelHistoryReset(WgpuBackend::Impl& s) {
    s.taaReset = true;
    s.taaPrevValid = false;
    s.taaPrevCamera = {};
}
inline void sceneModelsNoted(WgpuBackend::Impl&) {}
inline bool rayDeviceMissing(WgpuBackend::Impl& s, std::string& status) {
    if (s.gpu)
        return false;
    status = "Ray tracing needs the WebGPU device";
    return true;
}
inline ray::WgpuRay* acquireRayPass(WgpuBackend::Impl& s) {
    return s.acquireRayPass();
}
inline const ray::WgpuRay* rayPassOf(const WgpuBackend::Impl& s) {
    return s.rayPass.get();
}
constexpr const char* kRayReflectionsName = "ray-traced reflections";
constexpr const char* kRayImageSuffix = " (WebGPU compute)";

} // namespace gsr::backend_hook

#define GSR_IMPL WgpuBackend::Impl
#define GSR_BACKEND WgpuBackend
#include "gs_backend_api.inl"
#undef GSR_BACKEND
#undef GSR_IMPL

namespace gsr {

bool WgpuBackend::deviceLost(std::string* reason) const {
    if (!m_impl->gpu || !m_impl->gpu->lost())
        return false;
    if (reason)
        *reason = m_impl->gpu->lostReason();
    return true;
}
void WgpuBackend::draw(const GsDrawState& st, const ClutBuf& clut, const DrawVertex* v, uint32_t count) {
    m_impl->draw(st, clut, v, count);
}
void WgpuBackend::drawSegments(const DrawSegment* segments, uint32_t segmentCount, const DrawVertex* v,
                               uint32_t count) {
    m_impl->drawSegments(segments, segmentCount, v, count);
}
void* WgpuBackend::beginPresentWait() {
    return nullptr;
}
// The surface's present mode paces presentation; there is no separate frame-latency wait.
bool WgpuBackend::waitPresentHandle(void*, uint32_t) {
    return true;
}
void WgpuBackend::endPresentWait(bool) {}
void WgpuBackend::setVsync(bool on) {
    if (m_impl->vsyncOn != on) {
        m_impl->vsyncOn = on;
        m_impl->configureSurface();
    }
}
double WgpuBackend::upscaleGpuMs() const {
    return 0.0;
}
void WgpuBackend::upscaleCut() {
    m_impl->discardTemporalWorlds();
    m_impl->motionHistory.reset();
    for (auto& r : m_impl->rts)
        r->wstate = 0;
}
bool WgpuBackend::upscaleAvailable(std::string& status) const {
    status = "Temporal AA (portable): DLSS needs the Direct3D 11 renderer (renderer = d3d11)";
    return true;
}
bool WgpuBackend::neuralRenderingAvailable(std::string& status) const {
    status = "DLSS 5 neural rendering is not available on the WebGPU renderer";
    return false;
}
std::string WgpuBackend::neuralRenderingStatus() const {
    return "DLSS 5 neural rendering is not available on the WebGPU renderer";
}

std::unique_ptr<BackendApi> createWgpuBackend(LocalMemory& mem, const Config& cfg, Stats& stats) {
    return std::make_unique<WgpuBackend>(mem, cfg, stats);
}

} // namespace gsr
