// Display rendering, latching and presentation, end-of-frame work.
#include "d3d11_backend_impl.h"

namespace gsr {

void D3D11Backend::Impl::ensureDisp(uint32_t w, uint32_t h) {
    if (dispTex && dispW == w && dispH == h)
        return;
    dispW = w;
    dispH = h;
    dispTex = makeTex(w, h, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    dispRtv.Reset();
    dispSrv.Reset();
    dev->CreateRenderTargetView(dispTex.Get(), nullptr, &dispRtv);
    dev->CreateShaderResourceView(dispTex.Get(), nullptr, &dispSrv);
}

// returns display size in native pixels
bool D3D11Backend::Impl::renderDisplay(const DisplayState& d, uint32_t& outW, uint32_t& outH, bool recordLatch) {
    bool recorded = false;
    // Preserve the completed game image before the first menu latch
    // overwrites it. One GPU copy per transition; CPU readback only on save.
    if (recordLatch && latchedGameWorld && sceneCamera.guiScreen >= 0 && dispTex) {
        D3D11_TEXTURE2D_DESC desc{};
        dispTex->GetDesc(&desc);
        D3D11_TEXTURE2D_DESC old{};
        if (savePreviewTex)
            savePreviewTex->GetDesc(&old);
        if (!savePreviewTex || old.Width != desc.Width || old.Height != desc.Height) {
            savePreviewTex.Reset();
            dev->CreateTexture2D(&desc, nullptr, &savePreviewTex);
        }
        if (savePreviewTex) {
            ctx->CopyResource(savePreviewTex.Get(), dispTex.Get());
            savePreviewW = dispW;
            savePreviewH = dispH;
            savePreviewAspect = gameplayAspect;
        }
    }
    if (recordLatch) {
        // Keep the last shown menu frame for the loading screen's screen grab; any other screen drops it so a level
        // change from gameplay never shows a stale menu.
        if (sceneCamera.guiScreen < 0)
            menuHoldValid = false;
        else if (sceneCamera.guiScreen != 14 && dispTex && !latchedGameWorld) {
            D3D11_TEXTURE2D_DESC desc{}, old{};
            dispTex->GetDesc(&desc);
            if (menuHoldTex)
                menuHoldTex->GetDesc(&old);
            if (!menuHoldTex || old.Width != desc.Width || old.Height != desc.Height) {
                menuHoldTex.Reset();
                menuHoldSrv.Reset();
                desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                if (SUCCEEDED(dev->CreateTexture2D(&desc, nullptr, &menuHoldTex)))
                    dev->CreateShaderResourceView(menuHoldTex.Get(), nullptr, &menuHoldSrv);
            }
            if (menuHoldTex && menuHoldSrv) {
                ctx->CopyResource(menuHoldTex.Get(), dispTex.Get());
                menuHoldValid = true;
            }
        }
        latchedGameWorld = latchedNeuralWorld = latchedWaterWorld = latchedRayWorld = false;
        latchedWorldEpoch = 0;
    }
    uint32_t w = 0, h = 0;
    for (int i = 0; i < 2; ++i)
        if (d.c[i].enable && d.c[i].width() > 0 && d.c[i].height() > 0) {
            w = d.c[i].width();
            h = d.c[i].height();
            break;
        }
    if (!w || !h) {
        w = 640;
        h = 448;
    }
    w = std::min(w, 2048u);
    h = std::min(h, 2048u);
    ensureDisp(w * scale, h * scale);
    outW = w;
    outH = h;

    DispConsts dc{};
    ID3D11ShaderResourceView* srv[2] = {nullptr, nullptr};
    for (int i = 0; i < 2; ++i) {
        const DisplayCircuit& c = d.c[i];
        if (!c.enable)
            continue;
        const uint32_t cw = c.width() ? c.width() : w, ch = c.height() ? c.height() : h;
        uint32_t psm = c.psm;
        if (psm != PSM_CT24 && psm != PSM_CT16 && psm != PSM_CT16S)
            psm = PSM_CT32;
        RT* rt = getRT(c.fbp, std::max(c.fbw, 1u), psm, c.dby + ch);
        if (rt->wstate == 1)
            finalizeWorld(*rt); // temporal upscaler: show the resolved world, never the half-drawn render rectangle
        syncRT(*rt);
        if (recordLatch) {
            rt->displaySeenFrame = frame;
            rt->displayHeight = std::min(rt->h, c.dby + ch);
        }
        if (recordLatch && !recorded) {
            recorded = true;
            latchedWorldEpoch = rt->worldEpoch;
            latchedGameWorld = (rt->worldPhaseSeen != 0 || rt->worldEpoch != 0) && sceneCamera.guiScreen < 0;
            if (latchedGameWorld)
                gameplayAspect = cfg.forcedAspect > 0 ? cfg.forcedAspect : 4.0f / 3.0f;
            latchedNeuralWorld = latchedGameWorld && rt->neuralOutputEpoch == rt->worldEpoch;
            latchedWaterWorld = latchedGameWorld && rt->waterOutputEpoch == rt->worldEpoch;
            latchedRayWorld = latchedGameWorld && rt->rayOutputEpoch == rt->worldEpoch;
        }
        srv[i] = rt->srv.Get();
        float* r = i ? dc.r2 : dc.r1;
        r[0] = (float)c.dbx / rt->w;
        r[1] = (float)c.dby / rt->h;
        r[2] = (float)(c.dbx + cw) / rt->w;
        r[3] = (float)(c.dby + ch) / rt->h;
        dc.m[i] = 1;
    }
    const uint32_t pm = (uint32_t)d.pmode;
    dc.m[2] = ((pm >> 5) & 1) | (((pm >> 6) & 1) << 1) | (((pm >> 7) & 1) << 2);
    dc.m[3] = (pm >> 8) & 0xFF;
    dc.bg[0] = (float)(d.bgcolor & 0xFF) / 255.0f;
    dc.bg[1] = (float)((d.bgcolor >> 8) & 0xFF) / 255.0f;
    dc.bg[2] = (float)((d.bgcolor >> 16) & 0xFF) / 255.0f;
    D3D11_MAPPED_SUBRESOURCE ms{};
    ctx->Map(cbDisp.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
    std::memcpy(ms.pData, &dc, sizeof(dc));
    ctx->Unmap(cbDisp.Get(), 0);

    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vsFull.Get(), nullptr, 0);
    ctx->PSSetShader(psDisplay.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(1, 1, cbDisp.GetAddressOf());
    ctx->PSSetShaderResources(0, 2, srv);
    ID3D11SamplerState* smps[2] = {smpPt.Get(), smpLin.Get()};
    ctx->PSSetSamplers(0, 2, smps);
    ctx->RSSetState(rast.Get());
    D3D11_VIEWPORT vp{0, 0, (float)dispW, (float)dispH, 0, 1};
    ctx->RSSetViewports(1, &vp);
    D3D11_RECT sr{0, 0, (LONG)dispW, (LONG)dispH};
    ctx->RSSetScissorRects(1, &sr);
    ctx->OMSetBlendState(blendOff.Get(), nullptr, 0xFFFFFFFFu);
    ctx->OMSetDepthStencilState(nullptr, 0);
    ID3D11RenderTargetView* rtv = dispRtv.Get();
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* nulls[2] = {nullptr, nullptr};
    ctx->PSSetShaderResources(0, 2, nulls);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    if (recordLatch && menuHoldValid && menuHoldSrv && sceneCamera.guiScreen == 14 &&
        sceneCamera.sourcePc == 0x43d340u) {
        // The loading screen shows the engine's native-resolution grab of the menu, which is drawn once and left in
        // the target. Show the last HD menu frame instead; the loading bar lives in the band below it.
        D3D11_TEXTURE2D_DESC hold{}, disp{};
        menuHoldTex->GetDesc(&hold);
        dispTex->GetDesc(&disp);
        if (hold.Width == disp.Width && hold.Height == disp.Height) {
            ctx->IASetInputLayout(nullptr);
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ctx->VSSetShader(vsFull.Get(), nullptr, 0);
            ctx->PSSetShader(psBlit.Get(), nullptr, 0);
            ID3D11ShaderResourceView* held = menuHoldSrv.Get();
            ctx->PSSetShaderResources(0, 1, &held);
            ctx->PSSetSamplers(0, 2, smps);
            const D3D11_RECT top{0, 0, (LONG)dispW, (LONG)(dispH * 0.855f)};
            ctx->RSSetScissorRects(1, &top);
            ctx->OMSetRenderTargets(1, &rtv, nullptr);
            ctx->Draw(3, 0);
            ctx->PSSetShaderResources(0, 1, nulls);
            ctx->OMSetRenderTargets(0, nullptr, nullptr);
        }
    }
    return true;
}

void D3D11Backend::Impl::latchDisplay(const DisplayState& d, uint32_t field) {
    CpuProfile::Scope profile(cpuProfile, CpuProfile::LatchDisplay);
    if (!ok)
        return;
    if (pendingTemporalEnd) {
        for (auto& target : rts)
            finalizeNeuralWorld(*target);
        pendingTemporalEnd = false;
    }
    if (gridCapture.open) {
        // The grid's draws can reach the GS after the EE returned from the draw call (a DMA chain kicked at the frame's
        // end), so the scope ends when its target is about to be displayed (the EE finished that frame), never while
        // the other buffer is. Bounded: a scope that never gets displayed ends after a few latches.
        bool shown = ++gridCapture.latches > 8;
        for (const auto& circuit : d.c)
            shown |= gridCapture.rt && circuit.enable && circuit.fbp == gridCapture.rt->fbp;
        if (shown)
            gridCaptureEnd();
    }
    uint32_t w, h;
    compareReady = false;
    const uint64_t wanted = runtimeFlags().matchedCompareEpoch.value_or(0);
    if (wanted && compareSaved != wanted) {
        RT* selected = nullptr;
        for (auto& target : rts)
            if (target->compareEpoch == wanted && target->worldEpoch == wanted) {
                for (const auto& circuit : d.c)
                    if (circuit.enable && circuit.fbp == target->fbp)
                        selected = target.get();
            }
        if (selected) {
            std::swap(selected->srv, selected->compareSrv);
            renderDisplay(d, w, h, false);
            std::swap(selected->srv, selected->compareSrv);
            D3D11_TEXTURE2D_DESC desc{};
            dispTex->GetDesc(&desc);
            compareDisplayTex.Reset();
            compareDisplaySrv.Reset();
            if (SUCCEEDED(dev->CreateTexture2D(&desc, nullptr, &compareDisplayTex)) &&
                SUCCEEDED(dev->CreateShaderResourceView(compareDisplayTex.Get(), nullptr, &compareDisplaySrv))) {
                ctx->OMSetRenderTargets(0, nullptr, nullptr);
                ctx->CopyResource(compareDisplayTex.Get(), dispTex.Get());
                compareReady = true;
            }
        }
    }
    renderDisplay(d, w, h, true);
    if (upMode) // targets that were just displayed start their next frame idle
        for (int i = 0; i < 2; ++i)
            if (d.c[i].enable)
                for (auto& r : rts)
                    if (r->fbp == d.c[i].fbp && r->wstate == 2)
                        r->wstate = 0;
    latchedW = w;
    latchedH = h;
    latchedField = field;
    latchedScale = scale;
    latchedHudAnchorX = inventoryHudFrame.inputAnchor();
    haveLatch = true;
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

void D3D11Backend::Impl::present(const DisplayState& d) {
    if (!ok)
        return;
    latchDisplay(d, latchedField);
    presentLatched();
}

void D3D11Backend::Impl::presentLatched() {
    CpuProfile::Scope profile(cpuProfile, CpuProfile::PresentLatched);
    if (!ok || !haveLatch || !swapRtv)
        return;
    const uint32_t w = latchedW, h = latchedH;
    float aspect = cfg.forcedAspect > 0.0f ? cfg.forcedAspect : (float)w / (float)h;
    float vw = (float)swapW, vh = (float)swapH, vx = 0, vy = 0;
    if (!cfg.stretchToWindow) {
        if (vw / vh > aspect) {
            const float nw = std::floor(vh * aspect + 0.5f);
            vx = std::floor((vw - nw) * 0.5f);
            vw = nw;
        } else {
            const float nh = std::floor(vw / aspect + 0.5f);
            vy = std::floor((vh - nh) * 0.5f);
            vh = nh;
        }
    }
    vpW = (uint32_t)vw;
    vpH = (uint32_t)vh;
    vpX = vx;
    vpY = vy;
    const float black[4] = {0, 0, 0, 1};
    ctx->ClearRenderTargetView(swapRtv.Get(), black);
    struct PC {
        float a[4];
        uint32_t m[4];
    } pc{};
    pc.a[0] = (float)dispW;
    pc.a[1] = (float)dispH;
    pc.a[2] = vw;
    pc.a[3] = vh;
    pc.m[0] = cfg.deinterlace;
    pc.m[1] = latchedField;
    pc.m[2] = cfg.sharpPresent ? 1u : 0u;
    pc.m[3] = latchedScale;
    D3D11_MAPPED_SUBRESOURCE ms{};
    ctx->Map(cbPresent.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
    std::memcpy(ms.pData, &pc, sizeof(pc));
    ctx->Unmap(cbPresent.Get(), 0);
    ID3D11RenderTargetView* rtv = swapRtv.Get();
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp{vx, vy, vw, vh, 0, 1};
    ctx->RSSetViewports(1, &vp);
    D3D11_RECT sr{0, 0, (LONG)swapW, (LONG)swapH};
    ctx->RSSetScissorRects(1, &sr);
    ctx->RSSetState(rast.Get());
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vsFull.Get(), nullptr, 0);
    ctx->PSSetShader(psPresent.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(2, 1, cbPresent.GetAddressOf());
    ID3D11ShaderResourceView* sv = dispSrv.Get();
    ctx->PSSetShaderResources(0, 1, &sv);
    ID3D11SamplerState* smps[2] = {smpPt.Get(), smpLin.Get()};
    ctx->PSSetSamplers(0, 2, smps);
    ctx->OMSetBlendState(blendOff.Get(), nullptr, 0xFFFFFFFFu);
    if (compareReady) {
        ctx->PSSetShaderResources(0, 1, compareDisplaySrv.GetAddressOf());
        ctx->Draw(3, 0);
        ComPtr<ID3D11Resource> resource;
        swapRtv->GetResource(&resource);
        ComPtr<ID3D11Texture2D> texture;
        Image image;
        const char* prefix = cstr(runtimeFlags().matchedComparePrefix);
        if (prefix && SUCCEEDED(resource.As(&texture)) && readTexture(texture.Get(), swapW, swapH, image)) {
            const char* effect = cstr(runtimeFlags().matchedCompareEffect);
            const auto path = std::string(prefix) + (effect && std::strcmp(effect, "shadows") == 0  ? "-shadows-off.png"
                                                     : effect && std::strcmp(effect, "models") == 0 ? "-models-off.png"
                                                     : effect                                       ? "-water-off.png"
                                                                                                    : "-nr-off.png");
            writePng(path.c_str(), image);
        }
        ctx->PSSetShaderResources(0, 1, &sv);
    }
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* nul = nullptr;
    ctx->PSSetShaderResources(0, 1, &nul);
    if (overlayCb) {
        OverlayContext oc;
        oc.device = dev.Get();
        oc.context = ctx.Get();
        oc.rtv = swapRtv.Get();
        oc.width = swapW;
        oc.height = swapH;
        oc.viewX = vpX;
        oc.viewY = vpY;
        oc.viewW = (float)vpW;
        oc.viewH = (float)vpH;
        oc.hudScaleX = hudScale;
        // Inventory's tutorial is still drawn in the left-anchored page,
        // although the active input screen is the message box (id 1).
        // Publish the rendered origin instead of inferring it from input.
        oc.hudAnchorX = latchedHudAnchorX;
        oc.gridCapture = true;
        if (gridCapture.srv && gridCapture.serial && frame - gridCapture.frame <= 30) {
            oc.gridSrv = gridCapture.srv.Get();
            oc.gridTexW = gridCapture.texW;
            oc.gridTexH = gridCapture.texH;
            oc.gridFrameW = gridCapture.rtW;
            oc.gridFrameH = gridCapture.rtH;
            oc.gridHudScale = gridCapture.hudScale;
            oc.gridCentred = gridCapture.centred;
            oc.gridSerial = gridCapture.serial;
        }
        overlayCb(oc);
    }
    if (compareReady) {
        ComPtr<ID3D11Resource> resource;
        swapRtv->GetResource(&resource);
        ComPtr<ID3D11Texture2D> texture;
        Image image;
        const char* prefix = cstr(runtimeFlags().matchedComparePrefix);
        if (prefix && SUCCEEDED(resource.As(&texture)) && readTexture(texture.Get(), swapW, swapH, image)) {
            const char* effect = cstr(runtimeFlags().matchedCompareEffect);
            const auto path = std::string(prefix) + (effect && std::strcmp(effect, "shadows") == 0  ? "-shadows-on.png"
                                                     : effect && std::strcmp(effect, "models") == 0 ? "-models-on.png"
                                                     : effect                                       ? "-water-on.png"
                                                                                                    : "-nr-on.png");
            writePng(path.c_str(), image);
            std::fprintf(stderr,
                         "[matched-compare] epoch=%llu same-frame world off/on; comparison excludes native HUD and "
                         "host overlays; SR mode=%u\n",
                         (unsigned long long)latchedWorldEpoch, upMode);
            compareSaved = latchedWorldEpoch;
        }
        compareReady = false;
    }
    ++presentCount;
    if (neuralEnabled && latchedGameWorld && runtimeFlags().dlssPresentLog)
        std::fprintf(stderr, "[dlss-presentation] present=%llu epoch=%llu neural=%d\n",
                     (unsigned long long)presentCount, (unsigned long long)latchedWorldEpoch, (int)latchedNeuralWorld);
    if (winShotNext < winShots.size() && (winShotByPresent ? presentCount : frame) >= winShots[winShotNext]) {
        CpuProfile::Scope screenshotProfile(cpuProfile, CpuProfile::WindowScreenshot);
        // GSR_WINSHOT=<prefix> GSR_WINSHOT_AT=<frame>[,<frame>...]: the finished window image (3D + HUD + settings
        // overlay / subtitles) as <prefix>_<frame>.png
        ComPtr<ID3D11Resource> bb;
        swapRtv->GetResource(&bb);
        ComPtr<ID3D11Texture2D> bbt;
        Image im;
        if (bb && SUCCEEDED(bb.As(&bbt)) && readTexture(bbt.Get(), swapW, swapH, im)) {
            char nm[600];
            std::snprintf(nm, sizeof nm, "%s_%llu.png", winShotPrefix.c_str(),
                          (unsigned long long)winShots[winShotNext]);
            writePng(nm, im);
            std::fprintf(stderr, "[gsr] window screenshot %ux%u -> %s\n", im.width, im.height, nm);
        }
        ++winShotNext;
    }
    if (std::string shotPath; takeWindowShotRequest(shotPath)) {
        CpuProfile::Scope screenshotProfile(cpuProfile, CpuProfile::WindowScreenshot);
        ComPtr<ID3D11Resource> bb;
        swapRtv->GetResource(&bb);
        ComPtr<ID3D11Texture2D> bbt;
        Image im;
        const bool written =
            bb && SUCCEEDED(bb.As(&bbt)) && readTexture(bbt.Get(), swapW, swapH, im) && writePng(shotPath.c_str(), im);
        std::fprintf(stderr, "[gsr] requested window screenshot %ux%u -> %s%s\n", im.width, im.height, shotPath.c_str(),
                     written ? "" : " FAILED");
    }
    // World epochs, unlike host ticks, identify the same submitted scene
    // in controlled off/on runs. Capture the finished swapchain image.
    if (runtimeFlags().worldShotAt) {
        const uint64_t epoch = *runtimeFlags().worldShotAt;
        if (epoch && latchedGameWorld && latchedWorldEpoch == epoch && worldShotEpoch != epoch) {
            ComPtr<ID3D11Resource> resource;
            swapRtv->GetResource(&resource);
            ComPtr<ID3D11Texture2D> texture;
            Image image;
            if (resource && SUCCEEDED(resource.As(&texture)) && readTexture(texture.Get(), swapW, swapH, image)) {
                const char* prefix = cstr(runtimeFlags().worldShotPrefix);
                if (prefix && *prefix) {
                    const std::string path = std::string(prefix) + ".png";
                    if (writePng(path.c_str(), image)) {
                        worldShotEpoch = epoch;
                        std::fprintf(
                            stderr, "[world-shot] epoch=%llu frame=%llu NR=%d size=%ux%u camera=%.6f,%.6f,%.6f -> %s\n",
                            (unsigned long long)epoch, (unsigned long long)frame, int(latchedNeuralWorld), swapW, swapH,
                            sceneCamera.cameraPosition[0], sceneCamera.cameraPosition[1], sceneCamera.cameraPosition[2],
                            path.c_str());
                    }
                }
            }
        }
    }
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    CpuProfile::Scope presentProfile(cpuProfile, CpuProfile::PresentCall);
    if (swap) {
        const HRESULT hr = swap->Present(vsyncOn ? 1 : 0, (!vsyncOn && tearingOk) ? DXGI_PRESENT_ALLOW_TEARING : 0u);
        presentSlotHeld = false;
        neuralPresentation.present(false, latchedGameWorld, latchedWorldEpoch,
                                   SUCCEEDED(hr) && hr != DXGI_STATUS_OCCLUDED);
        lastPresentedWater = latchedWaterWorld && SUCCEEDED(hr) && hr != DXGI_STATUS_OCCLUDED;
        lastPresentedRay = latchedRayWorld && SUCCEEDED(hr) && hr != DXGI_STATUS_OCCLUDED;
    }
}

void D3D11Backend::Impl::clusterHud() {
    std::vector<HudBox>& b = hudCur;
    // the game draws its UI only on some frames (every 2nd - 4th presented frame): keep the last clusters across
    // empty frames
    if (b.empty()) {
        if (++hudEmptyFrames > 60)
            hudPrev.clear();
        return;
    }
    hudEmptyFrames = 0;
    if (b.size() > 600)
        b.resize(600);
    // merge boxes that overlap or touch (gap <= 3 px) until stable
    bool again = true;
    while (again) {
        again = false;
        for (size_t i = 0; i < b.size(); ++i)
            for (size_t j = i + 1; j < b.size();) {
                const float g = 3.0f;
                if (b[i].x0 <= b[j].x1 + g && b[j].x0 <= b[i].x1 + g && b[i].y0 <= b[j].y1 + g &&
                    b[j].y0 <= b[i].y1 + g) {
                    b[i] = {std::min(b[i].x0, b[j].x0), std::min(b[i].y0, b[j].y0), std::max(b[i].x1, b[j].x1),
                            std::max(b[i].y1, b[j].y1)};
                    b.erase(b.begin() + j);
                    again = true;
                } else
                    ++j;
            }
    }
    if (runtimeFlags().hudLog) {
        std::fprintf(stderr, "[hud] frame %llu clusters=%zu:", (unsigned long long)frame, b.size());
        for (const HudBox& x : b)
            std::fprintf(stderr, " (%.0f,%.0f,%.0f,%.0f)", x.x0, x.y0, x.x1, x.y1);
        std::fprintf(stderr, "\n");
    }
    hudPrev = b;
    hudCur.clear();
}

void D3D11Backend::Impl::endFrame() {
    nativeSubtitleOpen = false;
    tutorialBandRows.endFrame();
    CpuProfile::Scope profile(cpuProfile, CpuProfile::EndFrame);
    // Finish offscreen targets too, before eviction/debug readback. Each new frame may enter world again.
    finalizeAllWorlds();
    for (auto& r : rts)
        r->wstate = 0;
    if (upFault) {
        const std::string failure = upStatusText;
        setUpscale(0, upPreset, upSharp);
        upStatusText = "DLSS SR disabled: " + failure;
    }
    // debug: GSR_RTDUMPF=<first>-<last>: every render target at the end of each of these frames
    // (GSR_RTDUMP_DIR/rtf_<frame>_fbp<fbp>.png)
    if (runtimeFlags().rtDumpFrames) {
        const long a = runtimeFlags().rtDumpFrames->first, b = runtimeFlags().rtDumpFrames->second;
        if ((long)frame >= a && (long)frame <= b)
            for (auto& r : rts) {
                Image im;
                if (readTexture(r->tex.Get(), r->w * scale, std::min<uint32_t>(r->h, 512u) * scale, im)) {
                    const char* d = runtimeFlags().rtDumpDir.c_str();
                    char name[512];
                    std::snprintf(name, sizeof(name), "%s/rtf_%llu_fbp%u.png", d, (unsigned long long)frame, r->fbp);
                    writePng(name, im);
                }
            }
    }
    ++frame;
    if (upMode) // safety net: a world that never got a UI draw / display, or a resolved target that is never
                // displayed, does not stay in its phase
        for (auto& r : rts) {
            if (r->wstate == 1 && frame - r->wframe >= 4)
                finalizeWorld(*r);
            else if (r->wstate == 2 && frame - r->wframe >= 8)
                r->wstate = 0;
        }
    clusterHud();
    pumpUp();
    st.texCacheBytes = texBytes + upBytes;
    texPeakBytes = std::max(texPeakBytes, st.texCacheBytes);
    if ((frame & 15) != 0)
        return;
    // age based sweep, then least-recently-used eviction while over the budget
    for (auto it = texCache.begin(); it != texCache.end();) {
        if (frame - it->second.lastFrame > 240) {
            texBytes -= it->second.bytes;
            it = texCache.erase(it);
        } else
            ++it;
    }
    for (auto it = upCache.begin(); it != upCache.end();) {
        if (frame - it->second->lastFrame > 240) {
            upBytes -= std::min(upBytes, it->second->bytes);
            it = upCache.erase(it);
        } else
            ++it;
    }
    // HD index maps of palette-animated textures (CPU memory only; rebuilt if the plane comes back)
    for (auto it = palettePlanes.begin(); it != palettePlanes.end();)
        if (it->second->ready.load(std::memory_order_acquire) && frame - it->second->lastFrame > 1800)
            it = palettePlanes.erase(it);
        else
            ++it;
    while (texBytes + upBytes > texBudget && (!texCache.empty() || !upCache.empty())) {
        // evict the least recently used quarter of both caches
        std::vector<uint64_t> ages;
        for (auto& kv : texCache)
            ages.push_back(kv.second.lastFrame);
        for (auto& kv : upCache)
            ages.push_back(kv.second->lastFrame);
        std::sort(ages.begin(), ages.end());
        const uint64_t cut = ages[ages.size() / 4];
        for (auto it = texCache.begin(); it != texCache.end();)
            if (it->second.lastFrame <= cut) {
                texBytes -= it->second.bytes;
                it = texCache.erase(it);
            } else
                ++it;
        for (auto it = upCache.begin(); it != upCache.end();)
            if (it->second->lastFrame <= cut) {
                upBytes -= std::min(upBytes, it->second->bytes);
                it = upCache.erase(it);
            } else
                ++it;
    }
    surfaceBuilder->drain(); // jobs hold target pointers
    for (size_t i = 0; i < rts.size();)
        if (!rts[i]->dirty && frame - rts[i]->lastUse > 600)
            rts.erase(rts.begin() + i);
        else
            ++i;
    for (size_t i = 0; i < depths.size();)
        if (frame - depths[i]->lastUse > 600)
            depths.erase(depths.begin() + i);
        else
            ++i;
}

} // namespace gsr
