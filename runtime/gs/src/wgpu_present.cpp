// Display rendering, latching and presentation, end-of-frame work: the WebGPU half. The policy is shared
// (gs_present.inl); this file holds its hooks.
#include "wgpu_backend_impl.h"
#include "gpu_pass_stats.h"
#include "ray_status.h"

namespace gsr::backend_hook {

// Backend interface of gs_present.inl.
struct NoGpuPassScope {}; // no GPU pass timer on this backend
inline NoGpuPassScope gpuPassScope(WgpuBackend::Impl&, gpupass::Pass) {
    return {};
}
// A copy of the display image into `copy` (created at the display's size with `usage`); false when it has none.
inline bool keepDisplayCopy(WgpuBackend::Impl& s, rhi::TexturePtr& copy, uint32_t usage, const char* label) {
    if (!copy || copy->width() != s.dispTex->width() || copy->height() != s.dispTex->height())
        copy = s.createTexture(s.dispTex->width(), s.dispTex->height(), usage, rhi::Format::RGBA8, 1, label);
    if (!copy)
        return false;
    s.gpu->copyTexture(*s.dispTex, 0, 0, 0, *copy, 0, 0, 0, s.dispTex->width(), s.dispTex->height());
    return true;
}
inline bool keepSavePreview(WgpuBackend::Impl& s) {
    return keepDisplayCopy(s, s.savePreviewTex, rhi::CopyDst | rhi::CopySrc, "save preview");
}
inline bool keepMenuHold(WgpuBackend::Impl& s) {
    return keepDisplayCopy(s, s.menuHoldTex, rhi::Sampled | rhi::CopyDst, "menu hold");
}
inline bool drawDisplay(WgpuBackend::Impl& s, const DispConsts& dc, GpuView* srv) {
    auto& gpu = s.gpu;
    gpu->setTargets(s.dispTex.get(), nullptr, 0);
    gpu->setProgram(s.progDisplay.get());
    gpu->setBlend({});
    gpu->setDepth({});
    gpu->setTopology(rhi::Topology::TriangleList);
    gpu->setViewport(0, 0, (float)s.dispW, (float)s.dispH);
    gpu->setScissor({0, 0, s.dispW, s.dispH});
    gpu->setUniforms(0, &dc, sizeof(dc));
    gpu->setTexture(0, srv[0] ? srv[0] : s.dummy.get());
    gpu->setTexture(1, srv[1] ? srv[1] : s.dummy.get());
    gpu->drawNoInput(3);
    return true;
}
inline void drawMenuHold(WgpuBackend::Impl& s) {
    if (!s.menuHoldTex || s.menuHoldTex->width() != s.dispTex->width() ||
        s.menuHoldTex->height() != s.dispTex->height())
        return;
    const float full[4] = {0, 0, (float)s.dispW, (float)s.dispH};
    const float opaque[4] = {0, 0, 0, 1};
    s.drawRect(*s.dispTex, full, s.menuHoldTex.get(), opaque, {0, 0, s.dispW, (uint32_t)(s.dispH * 0.855f)});
}
constexpr bool kNeuralRendering = false;
inline bool deviceGone(const WgpuBackend::Impl&) {
    return false; // a lost device is latched by gsr::Backend at the frame boundary (deviceLost)
}
inline void latchCompare(WgpuBackend::Impl&, const DisplayState&) {}
inline void displayLatched(WgpuBackend::Impl&, const DisplayState&, uint32_t, uint32_t) {}
inline rhi::TexturePtr acquireWindowImage(WgpuBackend::Impl& s) {
    return s.gpu->hasSurface() ? s.gpu->acquireSurface() : nullptr;
}
inline void skipPresent(WgpuBackend::Impl& s) {
    s.gpu->flush();
}
inline bool drawWindowImage(WgpuBackend::Impl& s, const rhi::TexturePtr& back, float vx, float vy, float vw, float vh) {
    auto& gpu = s.gpu;
    const float black[4] = {0, 0, 0, 1};
    gpu->clearColor(*back, black);
    struct PC {
        float a[4];
        uint32_t m[4];
    } pc{};
    pc.a[0] = (float)s.dispW;
    pc.a[1] = (float)s.dispH;
    pc.a[2] = vw;
    pc.a[3] = vh;
    pc.m[0] = s.cfg.deinterlace;
    pc.m[1] = s.latchedField;
    pc.m[2] = s.cfg.sharpPresent ? 1u : 0u;
    pc.m[3] = s.latchedScale;
    gpu->setTargets(back.get(), nullptr, 0);
    gpu->setProgram(s.progPresent.get());
    gpu->setBlend({});
    gpu->setDepth({});
    gpu->setTopology(rhi::Topology::TriangleList);
    gpu->setViewport(vx, vy, vw, vh);
    gpu->setScissor({0, 0, s.swapW, s.swapH});
    gpu->setUniforms(0, &pc, sizeof(pc));
    gpu->setTexture(0, s.dispTex.get());
    gpu->drawNoInput(3);
    return true;
}
inline void overlayDevice(WgpuBackend::Impl& s, OverlayContext& oc, const rhi::TexturePtr& back) {
    oc.api = OverlayApi::WebGpu;
    oc.device = s.gpu.get();
    oc.rtv = back.get();
}
inline void* gridCaptureView(WgpuBackend::Impl& s) {
    return s.gridCapture.tex.get();
}
inline void afterOverlay(WgpuBackend::Impl&, const rhi::TexturePtr&) {}
inline bool readWindowImage(WgpuBackend::Impl& s, const rhi::TexturePtr& back, Image& image) {
    return (back->desc().usage & rhi::CopySrc) != 0 && s.readTexture(*back, s.swapW, s.swapH, image);
}
inline int presentWindowImage(WgpuBackend::Impl& s, rhi::TexturePtr& back) {
    back.reset();
    s.gpu->present();
    return 1;
}
inline bool frameEnding(WgpuBackend::Impl& s) {
    // Test aid: GSR_TEST_DEVICE_LOSS=<frame> destroys the device at that frame, as a driver reset would.
    static const long long loseAt = [] {
        const char* value = std::getenv("GSR_TEST_DEVICE_LOSS");
        return value && *value ? std::atoll(value) : -1ll;
    }();
    if (loseAt >= 0 && (long long)s.frame == loseAt && s.gpu)
        s.gpu->loseForTest();
    return true;
}
inline void endFrameWorlds(WgpuBackend::Impl& s) {
    for (auto& r : s.rts)
        r->wstate = 0;
}
inline void frameAdvanced(WgpuBackend::Impl&) {}
inline void frameEnded(WgpuBackend::Impl&) {}

} // namespace gsr::backend_hook

#define GSR_IMPL WgpuBackend::Impl
#include "gs_present.inl"
#undef GSR_IMPL

namespace gsr {

void WgpuBackend::Impl::ensureDisp(uint32_t w, uint32_t h) {
    if (dispTex && dispW == w && dispH == h)
        return;
    dispW = w;
    dispH = h;
    dispTex = createTexture(w, h, rhi::Sampled | rhi::RenderTarget | rhi::CopySrc, rhi::Format::RGBA8, 1, "display");
}

} // namespace gsr
