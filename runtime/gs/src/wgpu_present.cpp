// Display rendering, latching and presentation, end-of-frame work (WebGPU backend; policy as d3d11_present.cpp).
#include "wgpu_backend_impl.h"

namespace gsr {

void WgpuBackend::Impl::ensureDisp(uint32_t w, uint32_t h) {
    if (dispTex && dispW == w && dispH == h)
        return;
    dispW = w;
    dispH = h;
    dispTex = createTexture(w, h, rhi::Sampled | rhi::RenderTarget | rhi::CopySrc, rhi::Format::RGBA8, 1, "display");
}

// returns display size in native pixels
bool WgpuBackend::Impl::renderDisplay(const DisplayState& d, uint32_t& outW, uint32_t& outH, bool recordLatch) {
    bool recorded = false;
    // Preserve the completed game image before the first menu latch overwrites it.
    if (recordLatch && latchedGameWorld && sceneCamera.guiScreen >= 0 && dispTex) {
        if (!savePreviewTex || savePreviewTex->width() != dispTex->width() ||
            savePreviewTex->height() != dispTex->height())
            savePreviewTex = createTexture(dispTex->width(), dispTex->height(), rhi::CopyDst | rhi::CopySrc,
                                           rhi::Format::RGBA8, 1, "save preview");
        if (savePreviewTex) {
            gpu->copyTexture(*dispTex, 0, 0, 0, *savePreviewTex, 0, 0, 0, dispTex->width(), dispTex->height());
            savePreviewW = dispW;
            savePreviewH = dispH;
            savePreviewAspect = gameplayAspect;
        }
    }
    if (recordLatch) {
        // Keep the last shown menu frame for the loading screen's screen grab; any other screen drops it.
        if (sceneCamera.guiScreen < 0)
            menuHoldValid = false;
        else if (sceneCamera.guiScreen != 14 && dispTex && !latchedGameWorld) {
            if (!menuHoldTex || menuHoldTex->width() != dispTex->width() || menuHoldTex->height() != dispTex->height())
                menuHoldTex = createTexture(dispTex->width(), dispTex->height(), rhi::Sampled | rhi::CopyDst,
                                            rhi::Format::RGBA8, 1, "menu hold");
            if (menuHoldTex) {
                gpu->copyTexture(*dispTex, 0, 0, 0, *menuHoldTex, 0, 0, 0, dispTex->width(), dispTex->height());
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
    rhi::Texture* srv[2] = {nullptr, nullptr};
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
            finalizeWorld(*rt);
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
            latchedWaterWorld = latchedGameWorld && rt->waterOutputEpoch == rt->worldEpoch;
            latchedRayWorld = latchedGameWorld && rt->rayOutputEpoch == rt->worldEpoch;
        }
        srv[i] = rt->tex.get();
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

    gpu->setTargets(dispTex.get(), nullptr, 0);
    gpu->setProgram(progDisplay.get());
    gpu->setBlend({});
    gpu->setDepth({});
    gpu->setTopology(rhi::Topology::TriangleList);
    gpu->setViewport(0, 0, (float)dispW, (float)dispH);
    gpu->setScissor({0, 0, dispW, dispH});
    gpu->setUniforms(0, &dc, sizeof(dc));
    gpu->setTexture(0, srv[0] ? srv[0] : dummy.get());
    gpu->setTexture(1, srv[1] ? srv[1] : dummy.get());
    gpu->drawNoInput(3);
    if (recordLatch && menuHoldValid && menuHoldTex && sceneCamera.guiScreen == 14 &&
        sceneCamera.sourcePc == 0x43d340u && menuHoldTex->width() == dispTex->width() &&
        menuHoldTex->height() == dispTex->height()) {
        // The loading screen shows the engine's native-resolution grab of the menu: show the last HD menu frame
        // instead; the loading bar lives in the band below it.
        const float full[4] = {0, 0, (float)dispW, (float)dispH};
        const float opaque[4] = {0, 0, 0, 1};
        drawRect(*dispTex, full, menuHoldTex.get(), opaque, {0, 0, dispW, (uint32_t)(dispH * 0.855f)});
    }
    return true;
}

void WgpuBackend::Impl::latchDisplay(const DisplayState& d, uint32_t field) {
    CpuProfile::Scope profile(cpuProfile, CpuProfile::LatchDisplay);
    if (!ok)
        return;
    if (pendingTemporalEnd) {
        for (auto& target : rts)
            finalizeNeuralWorld(*target);
        pendingTemporalEnd = false;
    }
    if (gridCapture.open) {
        // The scope ends when its target is about to be displayed (see d3d11_present.cpp); bounded.
        bool shown = ++gridCapture.latches > 8;
        for (const auto& circuit : d.c)
            shown |= gridCapture.rt && circuit.enable && circuit.fbp == gridCapture.rt->fbp;
        if (shown)
            gridCaptureEnd();
    }
    uint32_t w, h;
    renderDisplay(d, w, h, true);
    latchedW = w;
    latchedH = h;
    latchedField = field;
    latchedScale = scale;
    latchedHudAnchorX = inventoryHudFrame.inputAnchor();
    haveLatch = true;
}

void WgpuBackend::Impl::present(const DisplayState& d) {
    if (!ok)
        return;
    latchDisplay(d, latchedField);
    presentLatched();
}

void WgpuBackend::Impl::presentLatched() {
    CpuProfile::Scope profile(cpuProfile, CpuProfile::PresentLatched);
    if (!ok || !haveLatch || !gpu->hasSurface()) {
        if (ok)
            gpu->flush();
        return;
    }
    auto back = gpu->acquireSurface();
    if (!back) {
        gpu->flush();
        return;
    }
    const bool copySource = (back->desc().usage & rhi::CopySrc) != 0;
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
    gpu->clearColor(*back, black);
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
    gpu->setTargets(back.get(), nullptr, 0);
    gpu->setProgram(progPresent.get());
    gpu->setBlend({});
    gpu->setDepth({});
    gpu->setTopology(rhi::Topology::TriangleList);
    gpu->setViewport(vx, vy, vw, vh);
    gpu->setScissor({0, 0, swapW, swapH});
    gpu->setUniforms(0, &pc, sizeof(pc));
    gpu->setTexture(0, dispTex.get());
    gpu->drawNoInput(3);
    if (overlayCb) {
        OverlayContext oc;
        oc.api = OverlayApi::WebGpu;
        oc.device = gpu.get();
        oc.rtv = back.get();
        oc.width = swapW;
        oc.height = swapH;
        oc.viewX = vx;
        oc.viewY = vy;
        oc.viewW = vw;
        oc.viewH = vh;
        oc.hudScaleX = hudScale;
        oc.hudAnchorX = latchedHudAnchorX;
        oc.gridCapture = true;
        if (gridCapture.tex && gridCapture.serial && frame - gridCapture.frame <= 30) {
            oc.gridSrv = gridCapture.tex.get();
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
    ++presentCount;
    const auto backShot = [&](const std::string& path) {
        Image im;
        return copySource && readTexture(*back, swapW, swapH, im) && writePng(path.c_str(), im);
    };
    if (winShotNext < winShots.size() && (winShotByPresent ? presentCount : frame) >= winShots[winShotNext]) {
        CpuProfile::Scope screenshotProfile(cpuProfile, CpuProfile::WindowScreenshot);
        char nm[600];
        std::snprintf(nm, sizeof nm, "%s_%llu.png", winShotPrefix.c_str(), (unsigned long long)winShots[winShotNext]);
        if (backShot(nm))
            std::fprintf(stderr, "[gsr] window screenshot %ux%u -> %s\n", swapW, swapH, nm);
        ++winShotNext;
    }
    if (std::string shotPath; takeWindowShotRequest(shotPath)) {
        CpuProfile::Scope screenshotProfile(cpuProfile, CpuProfile::WindowScreenshot);
        const bool written = backShot(shotPath);
        std::fprintf(stderr, "[gsr] requested window screenshot %ux%u -> %s%s\n", swapW, swapH, shotPath.c_str(),
                     written ? "" : " FAILED");
    }
    CpuProfile::Scope presentProfile(cpuProfile, CpuProfile::PresentCall);
    back.reset();
    gpu->present();
    lastPresentedWater = latchedWaterWorld;
    lastPresentedRay = latchedRayWorld;
}

void WgpuBackend::Impl::clusterHud() {
    std::vector<HudBox>& b = hudCur;
    // the game draws its UI only on some frames: keep the last clusters across empty frames
    if (b.empty()) {
        if (++hudEmptyFrames > 60)
            hudPrev.clear();
        return;
    }
    hudEmptyFrames = 0;
    if (b.size() > 600)
        b.resize(600);
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

void WgpuBackend::Impl::endFrame() {
    nativeSubtitleOpen = false;
    tutorialBandRows.endFrame();
    CpuProfile::Scope profile(cpuProfile, CpuProfile::EndFrame);
    for (auto& r : rts)
        r->wstate = 0;
    // debug: GSR_RTDUMPF=<first>-<last>: every render target at the end of each of these frames
    if (runtimeFlags().rtDumpFrames) {
        const long a = runtimeFlags().rtDumpFrames->first, b = runtimeFlags().rtDumpFrames->second;
        if ((long)frame >= a && (long)frame <= b)
            for (auto& r : rts) {
                Image im;
                if (readTexture(*r->tex, r->w * scale, std::min<uint32_t>(r->h, 512u) * scale, im)) {
                    const char* d = runtimeFlags().rtDumpDir.c_str();
                    char name[512];
                    std::snprintf(name, sizeof(name), "%s/rtf_%llu_fbp%u.png", d, (unsigned long long)frame, r->fbp);
                    writePng(name, im);
                }
            }
    }
    ++frame;
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
    for (auto it = palettePlanes.begin(); it != palettePlanes.end();)
        if (it->second->ready.load(std::memory_order_acquire) && frame - it->second->lastFrame > 1800)
            it = palettePlanes.erase(it);
        else
            ++it;
    while (texBytes + upBytes > texBudget && (!texCache.empty() || !upCache.empty())) {
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
