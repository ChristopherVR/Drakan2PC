// D3D11Backend: the BackendApi of this backend; forwards to D3D11Backend::Impl (d3d11_backend_impl.h).
#include "d3d11_backend_impl.h"

namespace gsr {

// ====================================================================================================
D3D11Backend::D3D11Backend(LocalMemory& mem, const Config& cfg, Stats& stats)
    : m_impl(std::make_unique<Impl>(mem, cfg, stats)) {}
D3D11Backend::~D3D11Backend() {
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
        m_impl->tp.reset(); // release feature + NGX before context/device teardown
        m_impl->combinedSr.reset();
        m_impl->neuralGuides.reset();
        dlssShutdown(m_impl->dev.Get());
        m_impl->pool.reset(); // join the workers before the members they touch go away
    }
}
bool D3D11Backend::init() {
    return m_impl->init();
}
bool D3D11Backend::valid() const {
    return m_impl->ok;
}
std::string D3D11Backend::adapterName() const {
    return m_impl->adapter;
}
void D3D11Backend::draw(const GsDrawState& st, const ClutBuf& clut, const DrawVertex* v, uint32_t count) {
    m_impl->draw(st, clut, v, count);
}
void D3D11Backend::flushToMemory(uint32_t a, uint32_t b) {
    m_impl->flushToMemory(a, b);
}
bool D3D11Backend::beginTargetUpload(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t w,
                                     uint32_t h) {
    return m_impl->beginTargetUpload(psm, bp, bw, x, y, w, h);
}
void D3D11Backend::completeTargetUpload(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t w,
                                        uint32_t h, bool apply) {
    m_impl->completeTargetUpload(psm, bp, bw, x, y, w, h, apply);
}
void D3D11Backend::drawMovieFrame(uint32_t dbp, uint32_t dbw, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                  const MovieImage& image, uint64_t imageId) {
    m_impl->drawMovieFrame(dbp, dbw, x, y, w, h, image, imageId);
}
void D3D11Backend::flushAll() {
    m_impl->flushAll();
}
void D3D11Backend::present(const DisplayState& d) {
    m_impl->present(d);
}
void D3D11Backend::latchDisplay(const DisplayState& d, uint32_t field) {
    m_impl->latchDisplay(d, field);
}
void D3D11Backend::presentLatched() {
    m_impl->presentLatched();
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
void D3D11Backend::applyLive(const LiveSettings& ls) {
    m_impl->applyLive(ls);
}
uint32_t D3D11Backend::internalScale() const {
    return m_impl->scale;
}
void D3D11Backend::setOverlayCallback(std::function<void(const OverlayContext&)> cb) {
    m_impl->overlayCb = std::move(cb);
}
bool D3D11Backend::vsyncEnabled() const {
    return m_impl->vsyncOn;
}
void D3D11Backend::getViewport(uint32_t& w, uint32_t& h) const {
    w = m_impl->vpW;
    h = m_impl->vpH;
}
bool D3D11Backend::capture(const DisplayState& d, Image& out) {
    return m_impl->capture(d, out);
}
bool D3D11Backend::captureLastGameplay(Image& out, float& aspect) {
    return m_impl->captureLastGameplay(out, aspect);
}
bool D3D11Backend::captureFramebuffer(uint32_t fbp, uint32_t fbw, uint32_t psm, uint32_t w, uint32_t h, Image& out) {
    return m_impl->captureFramebuffer(fbp, fbw, psm, w, h, out);
}
void D3D11Backend::resize(uint32_t w, uint32_t h) {
    m_impl->resize(w, h);
}
void D3D11Backend::reset() {
    m_impl->reset();
}
void D3D11Backend::endFrame() {
    m_impl->endFrame();
}
void D3D11Backend::noteVuUpload(uint32_t a, uint32_t n, const float* rows) {
    m_impl->noteVuUpload(a, n, rows);
}
void D3D11Backend::noteSceneCamera(const SceneGuideCamera& camera) {
    if (camera.sourcePc == 0x161a78u) {
        m_impl->nativeWaterPass = camera.reserved != 0;
        return;
    }

    if (camera.sourcePc == 0x437170u) {
        // A host screen's native item grid, captured off-screen (Impl::GridCapture).
        auto& g = m_impl->gridCapture;
        if (g.open)
            m_impl->gridCaptureEnd();
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
            m_impl->nativeSubtitleOpen = true;
            m_impl->nativeSubtitleBounds.reset();
        }
        if (runtimeFlags().subtitleLog)
            std::fprintf(stderr, "[subtitle-phase] f=%llu active=%u\n", (unsigned long long)m_impl->frame,
                         camera.reserved);
        return;
    }
    // Private runtime phase markers preserve the public packet layout. They
    // must not replace camera matrices or start/reset object history epochs.
    if (camera.sourcePc == 0x139010u && (camera.flags & ~SceneGuideCamera::EndWorld) == 0) {
        m_impl->nativeSubtitleOpen = false;
        m_impl->engineWorldOpen = !(camera.flags & SceneGuideCamera::EndWorld);
        m_impl->nativeWaterPass = false;
        m_impl->waterWorldCamera = {};
        if (m_impl->engineWorldOpen) {
            if (m_impl->pendingTemporalEnd) {
                for (auto& target : m_impl->rts)
                    m_impl->finalizeNeuralWorld(*target);
                m_impl->pendingTemporalEnd = false;
            }
            ++m_impl->worldPhaseSerial;
            if (m_impl->neuralEnabled)
                m_impl->neuralStatusText = "Waiting for a valid world camera, depth and render target.";
        }
        return;
    }
    if (camera.flags & SceneGuideCamera::EndWorld) {
        m_impl->engineWorldOpen = false;
        m_impl->nativeWaterPass = false;
        m_impl->pendingTemporalEnd = std::any_of(m_impl->rts.begin(), m_impl->rts.end(),
                                                 [](const auto& target) { return target->neuralState == 1; });
        m_impl->neuralWorldClosed = true;
        m_impl->motionWorldOpen = false;
        m_impl->objectScope = {};
        if (runtimeFlags().motionLog)
            std::fprintf(
                stderr,
                "[object-motion] epoch=%llu matchedBatches=%llu matchedVertices=%llu rejectedBatches=%llu "
                "historyBytes=%llu scopePackets=%llu eligiblePackets=%llu scopedBatches=%llu\n",
                (unsigned long long)m_impl->motionEpoch, (unsigned long long)m_impl->motionMatchedBatches,
                (unsigned long long)m_impl->motionMatchedVertices, (unsigned long long)m_impl->motionRejectedBatches,
                (unsigned long long)m_impl->motionHistory.bytes(), (unsigned long long)m_impl->motionPackets,
                (unsigned long long)m_impl->motionEligiblePackets, (unsigned long long)m_impl->motionScopedBatches);
        if (m_impl->tpLog)
            std::fprintf(stderr, "[dlss-scene] f%llu end world\n", (unsigned long long)m_impl->frame);
        return;
    }
    // This ordered native loading draw is a full GUI phase. It can arrive
    // before the host observes the screen switch, while gameplay is still open.
    if ((camera.sourcePc == 0x43d340u && camera.guiScreen == 14) ||
        (camera.sourcePc == 0x43e4c8u && camera.guiScreen == 8))
        m_impl->engineWorldOpen = false;
    m_impl->nativeSubtitleOpen = false;
    m_impl->sceneCamera = camera;
    if ((camera.flags & (SceneGuideCamera::Valid | SceneGuideCamera::World)) ==
        (SceneGuideCamera::Valid | SceneGuideCamera::World))
        m_impl->waterWorldCamera = camera;
    if (camera.flags & SceneGuideCamera::World)
        m_impl->engineWorldOpen = true;
    if ((camera.flags & SceneGuideCamera::World) && !m_impl->motionWorldOpen) {
        m_impl->motionWorldOpen = true;
        ++m_impl->motionEpoch;
        m_impl->motionHistory.beginFrame(m_impl->motionEpoch, (camera.flags & SceneGuideCamera::Reset) != 0);
        m_impl->motionOrdinals.clear();
        m_impl->objectScope = {};
        m_impl->motionMatchedBatches = m_impl->motionMatchedVertices = m_impl->motionRejectedBatches = 0;
        m_impl->motionPackets = m_impl->motionEligiblePackets = m_impl->motionScopedBatches = 0;
    }
    m_impl->neuralWorldClosed = !(camera.flags & SceneGuideCamera::World);
    if (m_impl->tpLog && (camera.flags & SceneGuideCamera::World))
        std::fprintf(stderr,
                     "[dlss-scene] f%llu serial=%llu flags=%x screen=%d size=%ux%u camera=%g,%g,%g proj=%g,%g "
                     "scale=%g,%g,%g offset=%g,%g,%g\n",
                     (unsigned long long)m_impl->frame, (unsigned long long)camera.serial, camera.flags,
                     camera.guiScreen, camera.frameWidth, camera.frameHeight, camera.cameraPosition[0],
                     camera.cameraPosition[1], camera.cameraPosition[2], camera.projectionScale[0],
                     camera.projectionScale[1], camera.viewportScale[0], camera.viewportScale[1],
                     camera.viewportScale[2], camera.viewportOffset[0], camera.viewportOffset[1],
                     camera.viewportOffset[2]);
}
void D3D11Backend::noteObjectMotion(const ObjectMotionScope& scope) {
    if (scope.flags & ObjectMotionScope::Begin) {
        ++m_impl->motionPackets;
        const uint32_t need = ObjectMotionScope::Valid | ObjectMotionScope::TopologyStable;
        const uint32_t bad = ObjectMotionScope::Reset | ObjectMotionScope::Clipped | ObjectMotionScope::Ambiguous |
                             ObjectMotionScope::Rejected;
        m_impl->motionEligiblePackets += (scope.flags & need) == need && !(scope.flags & bad);
    }
    m_impl->objectScope = (scope.flags & ObjectMotionScope::End) ? ObjectMotionScope{} : scope;
}
std::string D3D11Backend::upscaleStatus() const {
    return m_impl->upStatusText;
}
double D3D11Backend::upscaleGpuMs() const {
    return m_impl->tp ? m_impl->tp->lastGpuMs() : 0.0;
}
void D3D11Backend::upscaleCut() {
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

} // namespace gsr

namespace gsr {
bool D3D11Backend::waterRenderingAvailable(std::string& status) const {
    auto& s = *m_impl;
    if (!s.waterRenderer)
        s.waterRenderer = std::make_unique<surface::RayWater>(s.dev.Get(), s.ctx.Get());
    status = s.waterRenderer->status();
    return s.waterRenderer->ready();
}
std::string D3D11Backend::waterRenderingStatus() const {
    const auto& s = *m_impl;
    if (!s.modernWater)
        return "Modern water off";
    if (s.waterRenderer && !s.waterRenderer->ready())
        return s.waterRenderer->status();
    if (s.lastPresentedWater)
        return std::string("Modern water pass completed for the last gameplay image; ") +
               (s.rayReflections ? "DXR reflections on. " : "DXR reflections off. ") + s.waterRenderer->status();
    if (s.waterRenderer)
        return "Modern water enabled; no water output in the last displayed image. Last pass: " +
               s.waterRenderer->status();
    return "Modern water enabled; no eligible water world in the current frame. Coverage follows native water layers.";
}
std::string D3D11Backend::rayTracingStatus() const {
    const auto& s = *m_impl;
    if (!s.modernWater && !s.rayShadows)
        return "Ray tracing off";
    if (s.waterRenderer && !s.waterRenderer->ready())
        return s.waterRenderer->status();
    if (s.rayShadows && s.nativeModels && s.nativeModels->lighting.lights.empty())
        return "Ray-traced shadows enabled; waiting for valid native world lighting";
    if (s.lastPresentedRay && s.waterRenderer) {
        std::string text = s.rayShadows ? "Ray-traced shadows rendered in the last gameplay image."
                                        : "Ray-traced water rendered in the last gameplay image.";
        if (s.lastPresentedWater && s.rayReflections)
            text += " Water reflections active.";
        return text;
    }
    return "Ray tracing enabled; no new ray-traced gameplay image displayed (menu/loading or pass bypass)";
}
} // namespace gsr

namespace gsr {
void D3D11Backend::noteSceneGeometry(const SceneGeometry& geometry) {
    m_impl->surfaceBuilder->drain();
    m_impl->residentScene.replace(geometry);
    m_impl->nativeModels.reset();
    // References into material texel storage never survive a different level.
    m_impl->reflectionTextures.clear();
    m_impl->reflectionTextureHashes.clear();
    m_impl->texCache.clear();
    m_impl->texBytes = 0;
}
} // namespace gsr

namespace gsr {
void D3D11Backend::noteSceneModels(std::shared_ptr<const SceneModels> models) {
    auto& s = *m_impl;
    s.nativeModels = std::move(models);
    // Prepare this world's model motion against the committed previous scene while the world is drawn.
    if (s.nativeModels && s.modelMotion.previous() && (s.neuralEnabled || s.combinedSr) && s.surfaceBuilder->async())
        s.surfaceBuilder->submitTask([&s, scene = s.nativeModels, previous = s.modelMotion.previous()] {
            auto& pending = s.pendingModels;
            motion::Models::prepareFrom(previous.get(), *scene, false, pending.frame, pending.scratch);
            pending.scene = scene;
            pending.previous = previous;
            pending.ready = true;
        });
}
std::unique_ptr<BackendApi> createD3D11Backend(LocalMemory& mem, const Config& cfg, Stats& stats) {
    return std::make_unique<D3D11Backend>(mem, cfg, stats);
}
bool d3d11WaitPresentHandle(void* wait, uint32_t timeoutMs) {
    return D3D11Backend::waitPresentHandle(wait, timeoutMs);
}

} // namespace gsr
