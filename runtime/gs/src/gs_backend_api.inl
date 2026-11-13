// The BackendApi of a hardware backend: forwarding to its Impl, the scene-camera phase machine, object motion scopes,
// level changes and the ray-tracing status lines. Shared by the hardware backends (gs_backend_shared.h): compiled once
// per backend with GSR_BACKEND naming its BackendApi class and GSR_IMPL its Impl, after the backend's private header.
//
// Backend interface: hooks in namespace backend_hook, defined by the backend file before it includes this unit. They
// are temporary: free functions over the Impl so that this unit needed no new declarations in the backends' private
// headers, to become members of the Impl when the backend state is shared.
//   constructed(s, lostNow)          the Impl exists (BackendApi::lostNow is the device-loss latch it may set)
//   releaseBeforeWorkers(s)          what must go before the texture workers are joined at destruction
//   markGpuPass(s, pass, begin)      a GPU pass boundary for GSR_GPU_PASS_TIMING (no-op without the timer)
//   worldPhaseOpened(s)              a world phase marker opened the world (status text of a world feature)
//   motionWorldOpened(s)             a new object motion epoch started (per-epoch state of a motion feature)
//   levelHistoryReset(s)             another level's geometry arrived: the temporal history the backend keeps
//   sceneModelsNoted(s)              new scene models were published (s.nativeModels is set)
//   rayDeviceMissing(s, status)      true when the backend cannot run a ray pass at all (status says why)
//   acquireRayPass(s), rayPassOf(s)  the ray pass: start or adopt the background build / the adopted pass, or null
//   kRayReflectionsName, kRayImageSuffix   the wording of the status lines
// Everything else a backend answers on its own (device, present wait, vsync, draw with motion notes, upscaler and
// neural rendering status) stays in its file.

#include "gpu_pass_stats.h"
#include "ray_status.h"

#ifndef GSR_IMPL
#error "define GSR_IMPL (the backend's Impl) and include the backend's private header first"
#endif
#ifndef GSR_BACKEND
#error "define GSR_BACKEND (the backend's BackendApi class)"
#endif

namespace gsr {

namespace {
// An object motion scope packet (ObjectMotionScope): counted, and the scope the next batches belong to.
inline void applyObjectScope(GSR_IMPL& s, const ObjectMotionScope& scope) {
    if (scope.flags & ObjectMotionScope::Begin) {
        ++s.motionPackets;
        const uint32_t need = ObjectMotionScope::Valid | ObjectMotionScope::TopologyStable;
        const uint32_t bad = ObjectMotionScope::Reset | ObjectMotionScope::Clipped | ObjectMotionScope::Ambiguous |
                             ObjectMotionScope::Rejected;
        s.motionEligiblePackets += (scope.flags & need) == need && !(scope.flags & bad);
    }
    s.objectScope = (scope.flags & ObjectMotionScope::End) ? ObjectMotionScope{} : scope;
}
} // namespace

// ====================================================================================================
GSR_BACKEND::GSR_BACKEND(LocalMemory& mem, const Config& cfg, Stats& stats)
    : m_impl(std::make_unique<Impl>(mem, cfg, stats)) {
    backend_hook::constructed(*m_impl, lostNow);
}
GSR_BACKEND::~GSR_BACKEND() {
    if (m_impl) {
        m_impl->printLodStat();
        if (m_impl->upLog)
            std::fprintf(stderr,
                         "[texup] exit: replaced=%llu upscaled=%llu pending=%llu cache now %.0f MB, peak %.0f MB, "
                         "budget %.0f MB; volatile slots=%llu palette index maps=%llu palette steps=%llu\n",
                         (unsigned long long)m_impl->st.texReplaced, (unsigned long long)m_impl->st.texUpscaled,
                         (unsigned long long)m_impl->st.texUpPending, m_impl->st.texCacheBytes / 1048576.0,
                         m_impl->texPeakBytes / 1048576.0, m_impl->texBudget / 1048576.0,
                         (unsigned long long)m_impl->st.texVolatile, (unsigned long long)m_impl->st.texPalettePlanes,
                         (unsigned long long)m_impl->st.texPaletteHd);
        backend_hook::releaseBeforeWorkers(*m_impl);
        m_impl->pool.reset(); // join the workers before the members they touch go away
    }
}
bool GSR_BACKEND::init() {
    return m_impl->init();
}
bool GSR_BACKEND::valid() const {
    return m_impl->ok;
}
std::string GSR_BACKEND::adapterName() const {
    return m_impl->adapter;
}
void GSR_BACKEND::flushToMemory(uint32_t a, uint32_t b) {
    m_impl->flushToMemory(a, b);
}
bool GSR_BACKEND::copyLocal(uint32_t spsm, uint32_t sbp, uint32_t sbw, uint32_t sx, uint32_t sy, uint32_t dpsm,
                            uint32_t dbp, uint32_t dbw, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) {
    return m_impl->copyLocal(spsm, sbp, sbw, sx, sy, dpsm, dbp, dbw, dx, dy, w, h);
}
void GSR_BACKEND::flushForRead(uint32_t a, uint32_t b, const VramRect* rect) {
    m_impl->flushForRead(a, b, rect);
}
bool GSR_BACKEND::beginTargetUpload(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t w,
                                    uint32_t h) {
    return m_impl->beginTargetUpload(psm, bp, bw, x, y, w, h);
}
void GSR_BACKEND::completeTargetUpload(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t w,
                                       uint32_t h, bool apply, uint64_t written) {
    m_impl->completeTargetUpload(psm, bp, bw, x, y, w, h, apply, written);
}
void GSR_BACKEND::drawMovieFrame(uint32_t dbp, uint32_t dbw, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                 const MovieImage& image, uint64_t imageId) {
    m_impl->drawMovieFrame(dbp, dbw, x, y, w, h, image, imageId);
}
void GSR_BACKEND::flushAll() {
    m_impl->flushAll();
}
void GSR_BACKEND::present(const DisplayState& d) {
    m_impl->present(d);
}
void GSR_BACKEND::latchDisplay(const DisplayState& d, uint32_t field) {
    m_impl->latchDisplay(d, field);
}
void GSR_BACKEND::presentLatched() {
    m_impl->presentLatched();
}
void GSR_BACKEND::applyLive(const LiveSettings& ls) {
    m_impl->applyLive(ls);
}
uint32_t GSR_BACKEND::internalScale() const {
    return m_impl->scale;
}
void GSR_BACKEND::setOverlayCallback(std::function<void(const OverlayContext&)> cb) {
    m_impl->overlayCb = std::move(cb);
}
bool GSR_BACKEND::vsyncEnabled() const {
    return m_impl->vsyncOn;
}
void GSR_BACKEND::getViewport(uint32_t& w, uint32_t& h) const {
    w = m_impl->vpW;
    h = m_impl->vpH;
}
bool GSR_BACKEND::capture(const DisplayState& d, Image& out) {
    return m_impl->capture(d, out);
}
bool GSR_BACKEND::captureLastGameplay(Image& out, float& aspect) {
    return m_impl->captureLastGameplay(out, aspect);
}
bool GSR_BACKEND::captureFramebuffer(uint32_t fbp, uint32_t fbw, uint32_t psm, uint32_t w, uint32_t h, Image& out) {
    return m_impl->captureFramebuffer(fbp, fbw, psm, w, h, out);
}
void GSR_BACKEND::resize(uint32_t w, uint32_t h) {
    m_impl->resize(w, h);
}
void GSR_BACKEND::reset() {
    m_impl->reset();
}
void GSR_BACKEND::endFrame() {
    m_impl->endFrame();
}
void GSR_BACKEND::noteVuUpload(uint32_t a, uint32_t n, const float* rows) {
    m_impl->noteVuUpload(a, n, rows);
}

// The scene-camera packets of the host (SceneGuideCamera): private phase markers (water pass, item grid, subtitles,
// world begin / end) and the camera of a world or GUI phase.
void GSR_BACKEND::noteSceneCamera(const SceneGuideCamera& camera) {
    auto& s = *m_impl;
    if (camera.sourcePc == 0x161a78u) {
        s.nativeWaterPass = camera.reserved != 0;
        return;
    }

    if (camera.sourcePc == 0x437170u) {
        // A host screen's native item grid, captured off-screen (Impl::GridCapture).
        auto& g = s.gridCapture;
        if (g.open)
            s.gridCaptureEnd();
        if (camera.reserved == 1) {
            g.open = true;
            g.rt = nullptr;
            g.latches = 0;
            for (int i = 0; i < 4; ++i)
                g.box[i] = camera.viewportOffset[i];
        }
        return;
    }
    if (camera.sourcePc == 0x3afd18u) {
        // The final font DMA chain is kicked after Subtitle_Draw returns.
        // Preserve this map through the frame tail, until endFrame or the
        // next explicit world/UI phase; the return marker alone is too early.
        if (camera.reserved == 1) {
            s.nativeSubtitleOpen = true;
            s.nativeSubtitleBounds.reset();
        }
        if (runtimeFlags().subtitleLog)
            std::fprintf(stderr, "[subtitle-phase] f=%llu active=%u\n", (unsigned long long)s.frame, camera.reserved);
        return;
    }
    // Private runtime phase markers preserve the public packet layout. They
    // must not replace camera matrices or start/reset object history epochs.
    if (camera.sourcePc == 0x139010u && (camera.flags & ~SceneGuideCamera::EndWorld) == 0) {
        s.nativeSubtitleOpen = false;
        s.engineWorldOpen = !(camera.flags & SceneGuideCamera::EndWorld);
        backend_hook::markGpuPass(s, s.engineWorldOpen ? gpupass::Pass::World : gpupass::Pass::AfterWorld,
                                  s.engineWorldOpen);
        s.nativeWaterPass = false;
        s.waterWorldCamera = {};
        if (s.engineWorldOpen) {
            if (s.pendingTemporalEnd) {
                for (auto& target : s.rts)
                    s.finalizeNeuralWorld(*target);
                s.pendingTemporalEnd = false;
            }
            ++s.worldPhaseSerial;
            // A world phase ends the loading screen. Its packet stays the scene camera until a validated world camera
            // replaces it, and a level whose draws never match the world camera (a converted level with no terrain,
            // OotF's Ruined Village Book) kept it: the held menu frame then covered the top of every frame.
            if (isLoadingScreenPacket(s.sceneCamera))
                s.sceneCamera = {};
            backend_hook::worldPhaseOpened(s);
        }
        return;
    }
    if (camera.flags & SceneGuideCamera::EndWorld) {
        s.engineWorldOpen = false;
        s.nativeWaterPass = false;
        s.pendingTemporalEnd =
            std::any_of(s.rts.begin(), s.rts.end(), [](const auto& target) { return target->neuralState == 1; });
        s.neuralWorldClosed = true;
        s.motionWorldOpen = false;
        s.objectScope = {};
        if (runtimeFlags().motionLog)
            std::fprintf(stderr,
                         "[object-motion] epoch=%llu matchedBatches=%llu matchedVertices=%llu rejectedBatches=%llu "
                         "historyBytes=%llu scopePackets=%llu eligiblePackets=%llu scopedBatches=%llu\n",
                         (unsigned long long)s.motionEpoch, (unsigned long long)s.motionMatchedBatches,
                         (unsigned long long)s.motionMatchedVertices, (unsigned long long)s.motionRejectedBatches,
                         (unsigned long long)s.motionHistory.bytes(), (unsigned long long)s.motionPackets,
                         (unsigned long long)s.motionEligiblePackets, (unsigned long long)s.motionScopedBatches);
        if (s.tpLog)
            std::fprintf(stderr, "[dlss-scene] f%llu end world\n", (unsigned long long)s.frame);
        return;
    }
    // This ordered native loading draw is a full GUI phase. It can arrive
    // before the host observes the screen switch, while gameplay is still open.
    if ((camera.sourcePc == 0x43d340u && camera.guiScreen == 14) ||
        (camera.sourcePc == 0x43e4c8u && camera.guiScreen == 8))
        s.engineWorldOpen = false;
    s.nativeSubtitleOpen = false;
    s.sceneCamera = camera;
    if ((camera.flags & (SceneGuideCamera::Valid | SceneGuideCamera::World)) ==
        (SceneGuideCamera::Valid | SceneGuideCamera::World))
        s.waterWorldCamera = camera;
    if (camera.flags & SceneGuideCamera::World)
        s.engineWorldOpen = true;
    if ((camera.flags & SceneGuideCamera::World) && !s.motionWorldOpen) {
        s.motionWorldOpen = true;
        ++s.motionEpoch;
        s.motionHistory.beginFrame(s.motionEpoch, (camera.flags & SceneGuideCamera::Reset) != 0);
        backend_hook::motionWorldOpened(s);
        s.objectScope = {};
        s.motionMatchedBatches = s.motionMatchedVertices = s.motionRejectedBatches = 0;
        s.motionPackets = s.motionEligiblePackets = s.motionScopedBatches = 0;
    }
    s.neuralWorldClosed = !(camera.flags & SceneGuideCamera::World);
    if (s.tpLog && (camera.flags & SceneGuideCamera::World))
        std::fprintf(stderr,
                     "[dlss-scene] f%llu serial=%llu flags=%x screen=%d size=%ux%u camera=%g,%g,%g proj=%g,%g "
                     "scale=%g,%g,%g offset=%g,%g,%g\n",
                     (unsigned long long)s.frame, (unsigned long long)camera.serial, camera.flags, camera.guiScreen,
                     camera.frameWidth, camera.frameHeight, camera.cameraPosition[0], camera.cameraPosition[1],
                     camera.cameraPosition[2], camera.projectionScale[0], camera.projectionScale[1],
                     camera.viewportScale[0], camera.viewportScale[1], camera.viewportScale[2],
                     camera.viewportOffset[0], camera.viewportOffset[1], camera.viewportOffset[2]);
}
void GSR_BACKEND::noteObjectMotion(const ObjectMotionScope& scope) {
    applyObjectScope(*m_impl, scope);
}
std::string GSR_BACKEND::upscaleStatus() const {
    return m_impl->upStatusText;
}

void GSR_BACKEND::noteSceneGeometry(const SceneGeometry& geometry) {
    auto& s = *m_impl;
    s.surfaceBuilder->drain();
    if (s.residentScene.identity != geometry.identity) {
        // The scene camera arrives before resident geometry. Invalidate previous-level history without clearing
        // that newly published current camera (upscaleCut() also clears it).
        s.discardTemporalWorlds();
        backend_hook::levelHistoryReset(s);
        s.motionHistory.reset();
        for (auto& target : s.rts)
            target->wstate = 0;
    }
    s.residentScene.replace(geometry);
    s.nativeModels.reset();
    // References into material texel storage never survive a different level.
    s.reflectionTextures.clear();
    s.reflectionTextureHashes.clear();
    s.texCache.clear();
    s.texBytes = 0;
}
void GSR_BACKEND::noteSceneModels(std::shared_ptr<const SceneModels> models) {
    m_impl->nativeModels = std::move(models);
    backend_hook::sceneModelsNoted(*m_impl);
}

bool GSR_BACKEND::waterRenderingAvailable(std::string& status) const {
    auto& s = *m_impl;
    if (backend_hook::rayDeviceMissing(s, status))
        return false;
    // The settings host asks from the window thread: never build or wait for the pass here, only start the background
    // build or adopt the finished one (the renderer lock serialises this with the GS worker). "Starting" counts as
    // available so the menu offers the effects; it re-reads its capabilities.
    const auto* pass = backend_hook::acquireRayPass(s);
    if (!pass) {
        status = raystatus::kStarting;
        return true;
    }
    status = pass->status();
    return pass->ready();
}
std::string GSR_BACKEND::waterRenderingStatus() const {
    const auto& s = *m_impl;
    raystatus::PassView pass;
    const auto* ray = backend_hook::rayPassOf(s);
    if (s.modernWater && ray) // null until a world frame adopted the background build
        pass = {true, ray->ready(), ray->status()};
    else if (s.modernWater && s.rayStarting())
        return raystatus::kStarting;
    return raystatus::waterStatus(s.modernWater, pass, s.lastPresentedWater, s.rayReflections,
                                  backend_hook::kRayReflectionsName);
}
std::string GSR_BACKEND::rayTracingStatus() const {
    const auto& s = *m_impl;
    if (!s.modernWater && !s.rayShadows && !s.rayReflections && !s.rayGlobalIllumination)
        return "Ray tracing off";
    if (s.rayStarting())
        return raystatus::kStarting;
    const auto* ray = backend_hook::rayPassOf(s);
    if (ray && !ray->ready())
        return ray->status();
    if (s.rayShadows && !s.rayReflections && !s.rayGlobalIllumination && s.nativeModels &&
        !s.nativeModels->lighting.hasContribution())
        return "Ray-traced shadows enabled; waiting for valid native world lighting";
    if (s.lastPresentedRay && ray) {
        std::string text = std::string(s.rayShadows ? "Ray-traced shadows rendered in the last gameplay image"
                                                    : "Ray-traced scene rendered in the last gameplay image") +
                           backend_hook::kRayImageSuffix + ".";
        if (s.rayGlobalIllumination)
            text += " Diffuse global illumination active.";
        if (s.rayReflections)
            text += " Opaque-material reflections active.";
        if (s.lastPresentedWater && s.rayReflections)
            text += " Water reflections active.";
        return text;
    }
    return "Ray tracing enabled; no new ray-traced gameplay image displayed (menu/loading or pass bypass)";
}

} // namespace gsr
