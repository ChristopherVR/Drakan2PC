// Display rendering, latching and presentation, end-of-frame work: the Direct3D 11 half. The policy is shared
// (gs_present.inl); this file holds its hooks, the window-sized upscaler view and the matched compare.
#include "d3d11_backend_impl.h"
#include "ray_status.h"

namespace gsr::backend_hook {

// Backend interface of gs_present.inl.
inline GpuPassTimer::Scope gpuPassScope(D3D11Backend::Impl& s, gpupass::Pass pass) {
    return GpuPassTimer::Scope(s.gpuPasses, pass);
}
inline bool keepSavePreview(D3D11Backend::Impl& s) {
    D3D11_TEXTURE2D_DESC desc{};
    s.dispTex->GetDesc(&desc);
    D3D11_TEXTURE2D_DESC old{};
    if (s.savePreviewTex)
        s.savePreviewTex->GetDesc(&old);
    if (!s.savePreviewTex || old.Width != desc.Width || old.Height != desc.Height) {
        s.savePreviewTex.Reset();
        s.dev->CreateTexture2D(&desc, nullptr, &s.savePreviewTex);
    }
    if (!s.savePreviewTex)
        return false;
    s.ctx->CopyResource(s.savePreviewTex.Get(), s.dispTex.Get());
    return true;
}
inline bool keepMenuHold(D3D11Backend::Impl& s) {
    D3D11_TEXTURE2D_DESC desc{}, old{};
    s.dispTex->GetDesc(&desc);
    if (s.menuHoldTex)
        s.menuHoldTex->GetDesc(&old);
    if (!s.menuHoldTex || old.Width != desc.Width || old.Height != desc.Height) {
        s.menuHoldTex.Reset();
        s.menuHoldSrv.Reset();
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (SUCCEEDED(s.dev->CreateTexture2D(&desc, nullptr, &s.menuHoldTex)))
            s.dev->CreateShaderResourceView(s.menuHoldTex.Get(), nullptr, &s.menuHoldSrv);
    }
    if (!s.menuHoldTex || !s.menuHoldSrv)
        return false;
    s.ctx->CopyResource(s.menuHoldTex.Get(), s.dispTex.Get());
    return true;
}
inline bool drawDisplay(D3D11Backend::Impl& s, const DispConsts& dc, GpuView* srv) {
    auto& ctx = s.ctx;
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (FAILED(s.map(s.cbDisp.Get(), D3D11_MAP_WRITE_DISCARD, ms, "display constants")))
        return false; // a removed device (d3d11_device_loss.cpp): nothing is composed or shown any more
    std::memcpy(ms.pData, &dc, sizeof(dc));
    ctx->Unmap(s.cbDisp.Get(), 0);

    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(s.vsFull.Get(), nullptr, 0);
    ctx->PSSetShader(s.psDisplay.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(1, 1, s.cbDisp.GetAddressOf());
    ctx->PSSetShaderResources(0, 2, srv);
    ID3D11SamplerState* smps[2] = {s.smpPt.Get(), s.smpLin.Get()};
    ctx->PSSetSamplers(0, 2, smps);
    ctx->RSSetState(s.rast.Get());
    D3D11_VIEWPORT vp{0, 0, (float)s.dispW, (float)s.dispH, 0, 1};
    ctx->RSSetViewports(1, &vp);
    D3D11_RECT sr{0, 0, (LONG)s.dispW, (LONG)s.dispH};
    ctx->RSSetScissorRects(1, &sr);
    ctx->OMSetBlendState(s.blendOff.Get(), nullptr, 0xFFFFFFFFu);
    ctx->OMSetDepthStencilState(nullptr, 0);
    ID3D11RenderTargetView* rtv = s.dispRtv.Get();
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* nulls[2] = {nullptr, nullptr};
    ctx->PSSetShaderResources(0, 2, nulls);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    return true;
}
inline void drawMenuHold(D3D11Backend::Impl& s) {
    if (!s.menuHoldSrv)
        return;
    auto& ctx = s.ctx;
    D3D11_TEXTURE2D_DESC hold{}, disp{};
    s.menuHoldTex->GetDesc(&hold);
    s.dispTex->GetDesc(&disp);
    if (hold.Width != disp.Width || hold.Height != disp.Height)
        return;
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(s.vsFull.Get(), nullptr, 0);
    ctx->PSSetShader(s.psBlit.Get(), nullptr, 0);
    ID3D11ShaderResourceView* held = s.menuHoldSrv.Get();
    ctx->PSSetShaderResources(0, 1, &held);
    ID3D11SamplerState* smps[2] = {s.smpPt.Get(), s.smpLin.Get()};
    ctx->PSSetSamplers(0, 2, smps);
    const D3D11_RECT top{0, 0, (LONG)s.dispW, (LONG)(s.dispH * 0.855f)};
    ctx->RSSetScissorRects(1, &top);
    ID3D11RenderTargetView* rtv = s.dispRtv.Get();
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* nulls[1] = {nullptr};
    ctx->PSSetShaderResources(0, 1, nulls);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
}
constexpr bool kNeuralRendering = true;
inline bool deviceGone(const D3D11Backend::Impl& s) {
    return s.gone();
}
// GSR matched compare (runtimeFlags().matchedCompareEpoch): the display of the same world without its neural / ray
// output, kept for the "-off" picture of presentLatched.
inline void latchCompare(D3D11Backend::Impl& s, const DisplayState& d) {
    s.compareReady = false;
    const uint64_t wanted = runtimeFlags().matchedCompareEpoch.value_or(0);
    if (!wanted || s.compareSaved == wanted)
        return;
    RT* selected = nullptr;
    for (auto& target : s.rts)
        if (target->compareEpoch == wanted && target->worldEpoch == wanted) {
            for (const auto& circuit : d.c)
                if (circuit.enable && circuit.fbp == target->fbp)
                    selected = target.get();
        }
    if (!selected)
        return;
    uint32_t w, h;
    std::swap(selected->srv, selected->compareSrv);
    s.renderDisplay(d, w, h, false);
    std::swap(selected->srv, selected->compareSrv);
    D3D11_TEXTURE2D_DESC desc{};
    s.dispTex->GetDesc(&desc);
    s.compareDisplayTex.Reset();
    s.compareDisplaySrv.Reset();
    if (SUCCEEDED(s.dev->CreateTexture2D(&desc, nullptr, &s.compareDisplayTex)) &&
        SUCCEEDED(s.dev->CreateShaderResourceView(s.compareDisplayTex.Get(), nullptr, &s.compareDisplaySrv))) {
        s.ctx->OMSetRenderTargets(0, nullptr, nullptr);
        s.ctx->CopyResource(s.compareDisplayTex.Get(), s.dispTex.Get());
        s.compareReady = true;
    }
}
inline void displayLatched(D3D11Backend::Impl& s, const DisplayState& d, uint32_t w, uint32_t h) {
    s.renderViewImage(d, w, h);
    if (s.tpLog && s.frame % 240 == 0)
        std::fprintf(stderr,
                     "[dlss-view] f%llu display %ux%u circuits %d/%d origin %u,%u / %u,%u atOrigin=%d window %ux%u "
                     "view=%d\n",
                     (unsigned long long)s.frame, w, h, (int)d.c[0].enable, (int)d.c[1].enable, d.c[0].dbx, d.c[0].dby,
                     d.c[1].dbx, d.c[1].dby, (int)s.displayAtOrigin, s.vpW, s.vpH, (int)s.latchedView);
    if (s.upMode) // targets that were just displayed start their next frame idle
        for (int i = 0; i < 2; ++i)
            if (d.c[i].enable)
                for (auto& r : s.rts)
                    if (r->fbp == d.c[i].fbp && r->wstate == 2)
                        r->wstate = 0;
}
inline ID3D11RenderTargetView* acquireWindowImage(D3D11Backend::Impl& s) {
    return s.gone() ? nullptr : s.swapRtv.Get();
}
inline void skipPresent(D3D11Backend::Impl&) {}
inline bool readWindowImage(D3D11Backend::Impl& s, ID3D11RenderTargetView* window, Image& image) {
    ComPtr<ID3D11Resource> resource;
    window->GetResource(&resource);
    ComPtr<ID3D11Texture2D> texture;
    return resource && SUCCEEDED(resource.As(&texture)) && s.readTexture(texture.Get(), s.swapW, s.swapH, image);
}
// The matched compare's pictures: "-off" before the overlay, "-on" after it.
inline void writeComparePicture(D3D11Backend::Impl& s, ID3D11RenderTargetView* window, bool on) {
    Image image;
    const char* prefix = cstr(runtimeFlags().matchedComparePrefix);
    if (!prefix || !readWindowImage(s, window, image))
        return;
    const char* effect = cstr(runtimeFlags().matchedCompareEffect);
    const char* name = effect && std::strcmp(effect, "shadows") == 0  ? "-shadows"
                       : effect && std::strcmp(effect, "models") == 0 ? "-models"
                       : effect                                       ? "-water"
                                                                      : "-nr";
    const auto path = std::string(prefix) + name + (on ? "-on.png" : "-off.png");
    writePng(path.c_str(), image);
    if (on) {
        std::fprintf(stderr,
                     "[matched-compare] epoch=%llu same-frame world off/on; comparison excludes native HUD and "
                     "host overlays; SR mode=%u\n",
                     (unsigned long long)s.latchedWorldEpoch, s.upMode);
        s.compareSaved = s.latchedWorldEpoch;
    }
}
inline bool drawWindowImage(D3D11Backend::Impl& s, ID3D11RenderTargetView* window, float vx, float vy, float vw,
                            float vh) {
    auto& ctx = s.ctx;
    const float black[4] = {0, 0, 0, 1};
    s.gpuPasses.mark(gpupass::Pass::PresentBlit);
    ctx->ClearRenderTargetView(window, black);
    struct PC {
        float a[4];
        uint32_t m[4];
    } pc{};
    // A latched window-sized image (renderViewImage) is shown one to one; a window resized since is scaled again.
    const bool showView = s.latchedView && s.viewSrv && s.viewTexW == (uint32_t)vw && s.viewTexH == (uint32_t)vh &&
                          s.cfg.deinterlace == 0 && !s.compareReady;
    pc.a[0] = (float)(showView ? s.viewTexW : s.dispW);
    pc.a[1] = (float)(showView ? s.viewTexH : s.dispH);
    pc.a[2] = vw;
    pc.a[3] = vh;
    pc.m[0] = s.cfg.deinterlace;
    pc.m[1] = s.latchedField;
    pc.m[2] = s.cfg.sharpPresent ? 1u : 0u;
    pc.m[3] = s.latchedScale;
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (FAILED(s.map(s.cbPresent.Get(), D3D11_MAP_WRITE_DISCARD, ms, "present constants")))
        return false; // a removed device: no Present (d3d11_device_loss.cpp)
    std::memcpy(ms.pData, &pc, sizeof(pc));
    ctx->Unmap(s.cbPresent.Get(), 0);
    ctx->OMSetRenderTargets(1, &window, nullptr);
    D3D11_VIEWPORT vp{vx, vy, vw, vh, 0, 1};
    ctx->RSSetViewports(1, &vp);
    D3D11_RECT sr{0, 0, (LONG)s.swapW, (LONG)s.swapH};
    ctx->RSSetScissorRects(1, &sr);
    ctx->RSSetState(s.rast.Get());
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(s.vsFull.Get(), nullptr, 0);
    ctx->PSSetShader(s.psPresent.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(2, 1, s.cbPresent.GetAddressOf());
    ID3D11ShaderResourceView* sv = showView ? s.viewSrv.Get() : s.dispSrv.Get();
    ctx->PSSetShaderResources(0, 1, &sv);
    ID3D11SamplerState* smps[2] = {s.smpPt.Get(), s.smpLin.Get()};
    ctx->PSSetSamplers(0, 2, smps);
    ctx->OMSetBlendState(s.blendOff.Get(), nullptr, 0xFFFFFFFFu);
    if (s.compareReady) {
        ctx->PSSetShaderResources(0, 1, s.compareDisplaySrv.GetAddressOf());
        ctx->Draw(3, 0);
        writeComparePicture(s, window, false);
        ctx->PSSetShaderResources(0, 1, &sv);
    }
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* nul = nullptr;
    ctx->PSSetShaderResources(0, 1, &nul);
    s.gpuPasses.mark(gpupass::Pass::Overlay);
    return true;
}
inline void overlayDevice(D3D11Backend::Impl& s, OverlayContext& oc, ID3D11RenderTargetView* window) {
    oc.device = s.dev.Get();
    oc.context = s.ctx.Get();
    oc.rtv = window;
}
inline void* gridCaptureView(D3D11Backend::Impl& s) {
    return s.gridCapture.srv.Get();
}
inline void afterOverlay(D3D11Backend::Impl& s, ID3D11RenderTargetView* window) {
    if (s.compareReady) {
        writeComparePicture(s, window, true);
        s.compareReady = false;
    }
    // World epochs, unlike host ticks, identify the same submitted scene
    // in controlled off/on runs. Capture the finished swapchain image.
    if (runtimeFlags().worldShotAt) {
        const uint64_t epoch = *runtimeFlags().worldShotAt;
        if (epoch && s.latchedGameWorld && s.latchedWorldEpoch == epoch && s.worldShotEpoch != epoch) {
            Image image;
            if (readWindowImage(s, window, image)) {
                const char* prefix = cstr(runtimeFlags().worldShotPrefix);
                if (prefix && *prefix) {
                    const std::string path = std::string(prefix) + ".png";
                    if (writePng(path.c_str(), image)) {
                        s.worldShotEpoch = epoch;
                        std::fprintf(
                            stderr, "[world-shot] epoch=%llu frame=%llu NR=%d size=%ux%u camera=%.6f,%.6f,%.6f -> %s\n",
                            (unsigned long long)epoch, (unsigned long long)s.frame, int(s.latchedNeuralWorld), s.swapW,
                            s.swapH, s.sceneCamera.cameraPosition[0], s.sceneCamera.cameraPosition[1],
                            s.sceneCamera.cameraPosition[2], path.c_str());
                    }
                }
            }
        }
    }
}
inline int presentWindowImage(D3D11Backend::Impl& s, ID3D11RenderTargetView*) {
    s.ctx->OMSetRenderTargets(0, nullptr, nullptr);
    s.gpuPasses.frameBoundary();
    if (!s.swap)
        return -1;
    HRESULT hr = (HRESULT)device_loss::kDeviceRemoved;
    if (!s.testLoss(device_loss::Site::Present))
        hr = s.swap->Present(s.vsyncOn ? 1 : 0, (!s.vsyncOn && s.tearingOk) ? DXGI_PRESENT_ALLOW_TEARING : 0u);
    else
        s.deviceFailed(hr, "Present", true);
    s.deviceOk(hr, "Present"); // DXGI_ERROR_DEVICE_REMOVED / _RESET end the session (d3d11_device_loss.cpp)
    s.presentSlotHeld = false;
    const bool shown = SUCCEEDED(hr) && hr != DXGI_STATUS_OCCLUDED;
    s.neuralPresentation.present(false, s.latchedGameWorld, s.latchedWorldEpoch, shown);
    return shown ? 1 : 0;
}
inline bool frameEnding(D3D11Backend::Impl& s) {
    s.testLossFrameEnd(); // GSR_TEST_DEVICE_LOSS (d3d11_device_loss.cpp)
    if (s.upFault)        // DLSS / neural rendering failed this frame: a removed device is not retried as a fault
        s.probeDevice("upscaler fault");
    return !s.gone();
}
inline void endFrameWorlds(D3D11Backend::Impl& s) {
    // Finish offscreen targets too, before eviction/debug readback. Each new frame may enter world again.
    s.finalizeAllWorlds();
    for (auto& r : s.rts)
        r->wstate = 0;
    if (s.upFault) {
        const std::string failure = s.upStatusText;
        s.setUpscale(0, s.upPreset, s.upSharp);
        s.upStatusText = "DLSS SR disabled: " + failure;
    }
}
inline void frameAdvanced(D3D11Backend::Impl& s) {
    if (s.upMode) // safety net: a world that never got a UI draw / display, or a resolved target that is never
                  // displayed, does not stay in its phase
        for (auto& r : s.rts) {
            if (r->wstate == 1 && s.frame - r->wframe >= 4)
                s.finalizeWorld(*r);
            else if (r->wstate == 2 && s.frame - r->wframe >= 8)
                r->wstate = 0;
        }
}
inline void frameEnded(D3D11Backend::Impl& s) {
    // GSR_RT_REFRESH_STATS: what the render targets and depth buffers hold on the GPU, about every 10 s.
    static const bool memoryLog = std::getenv("GSR_RT_REFRESH_STATS") != nullptr;
    if (memoryLog && s.frame % 608 == 0) {
        uint64_t targetBytes = 0, depthBytes = 0;
        unsigned dirtyCount = 0;
        for (const auto& r : s.rts) {
            const uint64_t pixels = uint64_t(r->w) * s.scale * r->h * s.scale;
            targetBytes += pixels * 4 * (r->copy ? 2 : 1) + (r->motionTex ? pixels * 16 : 0);
            dirtyCount += r->dirty;
        }
        for (const auto& d : s.depths)
            depthBytes += uint64_t(d->w) * s.scale * d->h * s.scale * 4 * (d->copy ? 2 : 1);
        std::fprintf(stderr, "[rt-memory] frame=%llu targets=%zu dirty=%u MiB=%.0f depth=%zu MiB=%.0f\n",
                     (unsigned long long)s.frame, s.rts.size(), dirtyCount, targetBytes / 1048576.0, s.depths.size(),
                     depthBytes / 1048576.0);
    }
}

} // namespace gsr::backend_hook

#define GSR_IMPL D3D11Backend::Impl
#include "gs_present.inl"
#undef GSR_IMPL

namespace gsr {

void D3D11Backend::Impl::ensureDisp(uint32_t w, uint32_t h) {
    if (dispTex && dispW == w && dispH == h)
        return;
    dispW = w;
    dispH = h;
    dispTex = makeTex(w, h, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    dispRtv.Reset();
    dispSrv.Reset();
    if (!dispTex) {
        dispW = dispH = 0; // made again by the next display; a removed device ends the session first
        return;
    }
    deviceOk(dev->CreateRenderTargetView(dispTex.Get(), nullptr, &dispRtv), "display view");
    deviceOk(dev->CreateShaderResourceView(dispTex.Get(), nullptr, &dispSrv), "display view");
}

// The latched display at window size for a world that came from a window-sized upscaler output: the upscaler's own
// pixels wherever the target still holds them, the usual area average elsewhere (PSPresentView, gs_shaders.h).
// Sets latchedView; presentLatched then shows viewTex one to one instead of scaling dispTex. Also records whether
// this display can be shown that way at all (displayAtOrigin), which the next world's plan reads.
void D3D11Backend::Impl::renderViewImage(const DisplayState& d, uint32_t w, uint32_t h) {
    latchedView = false;
    const DisplayCircuit* only = nullptr;
    int enabled = 0;
    for (const DisplayCircuit& c : d.c)
        if (c.enable) {
            ++enabled;
            only = &c;
        }
    const bool atOrigin = enabled == 1 && only->dbx == 0 && only->dby == 0 &&
                          (only->width() ? only->width() : w) == w && (only->height() ? only->height() : h) == h;
    if (latchedGameWorld || !worldDisplayW) { // menus in between do not move the plan (presentView)
        displayAtOrigin = atOrigin;
        if (latchedGameWorld) {
            worldDisplayW = w;
            worldDisplayH = h;
        }
    }
    if (!atOrigin || cfg.deinterlace != 0 || !psPresentView || !vpW || !vpH || !dispSrv || compareReady)
        return;
    uint32_t psm = only->psm;
    if (psm != PSM_CT24 && psm != PSM_CT16 && psm != PSM_CT16S)
        psm = PSM_CT32;
    RT* rt = getRT(only->fbp, std::max(only->fbw, 1u), psm, only->dby + h);
    // The picture must be this window's pixels of this display: a resized window or another display size since the
    // world was drawn falls back to the scaled display (the next world is planned for the new size).
    const upscale::Plan plan = upscale::plan(rt->wgeom.frameW, rt->wgeom.frameH, scale, 1.0f, false, {w, h, vpW, vpH});
    if (!rt->srViewSrv || !plan.windowSized || plan.resolve.width != rt->srViewW ||
        plan.resolve.height != rt->srViewH || plan.target.width != rt->srTargetW ||
        plan.target.height != rt->srTargetH || dispW != w * scale || dispH != h * scale)
        return;
    if (!viewTex || viewTexW != vpW || viewTexH != vpH) {
        viewRtv.Reset();
        viewSrv.Reset();
        viewTex = makeTex(vpW, vpH, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        viewTexW = vpW;
        viewTexH = vpH;
        if (!viewTex || FAILED(dev->CreateRenderTargetView(viewTex.Get(), nullptr, &viewRtv)) ||
            FAILED(dev->CreateShaderResourceView(viewTex.Get(), nullptr, &viewSrv))) {
            viewTex.Reset();
            return;
        }
    }
    if (!cbView) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 16;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(dev->CreateBuffer(&bd, nullptr, &cbView)))
            return;
    }
    GpuPassTimer::Scope gpuPass(gpuPasses, gpupass::Pass::Display);
    struct PC {
        float a[4];
        uint32_t m[4];
    } pc{};
    pc.a[0] = (float)dispW;
    pc.a[1] = (float)dispH;
    pc.a[2] = (float)vpW;
    pc.a[3] = (float)vpH;
    static const bool debugTint = std::getenv("GSR_UPSCALE_VIEW_DEBUG") != nullptr;
    pc.m[0] = debugTint ? 1u : 0u;
    pc.m[2] = cfg.sharpPresent ? 1u : 0u;
    pc.m[3] = scale;
    const uint32_t sizes[4] = {rt->srTargetW, rt->srTargetH, rt->srViewW, rt->srViewH};
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (FAILED(map(cbPresent.Get(), D3D11_MAP_WRITE_DISCARD, ms, "view constants")))
        return;
    std::memcpy(ms.pData, &pc, sizeof(pc));
    ctx->Unmap(cbPresent.Get(), 0);
    if (FAILED(map(cbView.Get(), D3D11_MAP_WRITE_DISCARD, ms, "view constants")))
        return;
    std::memcpy(ms.pData, sizes, sizeof(sizes));
    ctx->Unmap(cbView.Get(), 0);
    ID3D11RenderTargetView* rtv = viewRtv.Get();
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    const D3D11_VIEWPORT vp{0, 0, (float)vpW, (float)vpH, 0, 1};
    ctx->RSSetViewports(1, &vp);
    const D3D11_RECT sr{0, 0, (LONG)vpW, (LONG)vpH};
    ctx->RSSetScissorRects(1, &sr);
    ctx->RSSetState(rast.Get());
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vsFull.Get(), nullptr, 0);
    ctx->PSSetShader(psPresentView.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(2, 1, cbPresent.GetAddressOf());
    ctx->PSSetConstantBuffers(3, 1, cbView.GetAddressOf());
    ID3D11ShaderResourceView* srvs[2] = {dispSrv.Get(), rt->srViewSrv.Get()};
    ctx->PSSetShaderResources(0, 2, srvs);
    ctx->OMSetBlendState(blendOff.Get(), nullptr, 0xFFFFFFFFu);
    ctx->OMSetDepthStencilState(nullptr, 0);
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* nulls[2] = {nullptr, nullptr};
    ctx->PSSetShaderResources(0, 2, nulls);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    latchedView = true;
}

HANDLE D3D11Backend::Impl::beginPresentWait() {
    HANDLE source = swapWait;
    // Duplicate: the swapchain (and its waitable) may be recreated while the caller waits without the lock.
    HANDLE wait = nullptr;
    if (source && !presentSlotHeld &&
        !DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), &wait, SYNCHRONIZE, FALSE, 0))
        wait = nullptr;
    return wait;
}

} // namespace gsr
