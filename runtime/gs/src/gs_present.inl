// Display rendering, latching and presentation, end-of-frame work: which circuits make the display image, the save
// preview and menu hold copies, the window viewport (aspect, letterbox), the overlay's context, window screenshots,
// HUD clusters and the end-of-frame cache sweep. Shared by the hardware backends (gs_backend_shared.h): compiled once
// per backend with GSR_IMPL naming its Impl, after the backend's private header.
//
// Backend interface: the members ensureDisp and readTarget, and hooks in namespace backend_hook defined by the backend
// file before it includes this unit. The hooks are temporary: free functions over the Impl so that this unit needed no
// new declarations in the backends' private headers, to become members of the Impl when the backend state is shared.
//   gpuPassScope(s, pass)                   a GSR_GPU_PASS_TIMING scope (an empty object without the timer)
//   keepSavePreview(s), keepMenuHold(s)     copy the display image into the save preview / the menu hold image
//   drawDisplay(s, consts, views)           the display pass (two circuits, PMODE merge); false: device gone
//   drawMenuHold(s)                         the held menu image over the loading screen's grab, when it fits
//   kNeuralRendering                        the backend renders neural worlds (rt->neuralOutputEpoch is meaningful)
//   deviceGone(s)                           a removed device was found mid-frame (Direct3D 11, d3d11_device_loss.cpp)
//   latchCompare(s, d), displayLatched(s, d, w, h)   before and after the latch's display image (upscaler view)
//   acquireWindowImage(s)                   the window image to present into, or null; skipPresent(s) when none
//   drawWindowImage(s, window, x, y, w, h)  the display image scaled into the window image; false: device gone
//   overlayDevice(s, oc, window), gridCaptureView(s)   the API half of the overlay's context
//   afterOverlay(s, window)                 debug output after the overlay (matched compare, world shot)
//   readWindowImage(s, window, image)       the finished window image on the CPU (screenshots)
//   presentWindowImage(s, window)           present: -1 not presented, 0 presented but not shown, 1 shown
//   frameEnding(s) (false: device gone), endFrameWorlds(s), frameAdvanced(s), frameEnded(s)   parts of endFrame

#ifndef GSR_IMPL
#error "define GSR_IMPL (the backend's Impl) and include the backend's private header first"
#endif

namespace gsr {

// returns display size in native pixels
bool GSR_IMPL::renderDisplay(const DisplayState& d, uint32_t& outW, uint32_t& outH, bool recordLatch) {
    bool recorded = false;
    // Preserve the completed game image before the first menu latch
    // overwrites it. One GPU copy per transition; CPU readback only on save.
    if (recordLatch && latchedGameWorld && sceneCamera.guiScreen >= 0 && dispTex) {
        if (backend_hook::keepSavePreview(*this)) {
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
            if (backend_hook::keepMenuHold(*this))
                menuHoldValid = true;
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
    [[maybe_unused]] const auto gpuPass = backend_hook::gpuPassScope(*this, gpupass::Pass::Display);
    outW = w;
    outH = h;

    DispConsts dc{};
    GpuView srv[2] = {nullptr, nullptr};
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
            if (backend_hook::kNeuralRendering)
                latchedNeuralWorld = latchedGameWorld && rt->neuralOutputEpoch == rt->worldEpoch;
            latchedWaterWorld = raystatus::outputLatched(latchedGameWorld, rt->waterOutputEpoch, rt->worldEpoch);
            latchedRayWorld = raystatus::outputLatched(latchedGameWorld, rt->rayOutputEpoch, rt->worldEpoch);
        }
        srv[i] = rt->view();
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
    if (!backend_hook::drawDisplay(*this, dc, srv))
        return false; // the device is gone: nothing is composed or shown any more
    // The loading screen shows the engine's native-resolution grab of the menu, which is drawn once and left in the
    // target. Show the last HD menu frame instead; the loading bar lives in the band below it.
    if (recordLatch && menuHoldValid && isLoadingScreenPacket(sceneCamera))
        backend_hook::drawMenuHold(*this);
    return true;
}

void GSR_IMPL::latchDisplay(const DisplayState& d, uint32_t field) {
    CpuProfile::Scope profile(cpuProfile, CpuProfile::LatchDisplay);
    if (!ok || backend_hook::deviceGone(*this))
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
    backend_hook::latchCompare(*this, d);
    renderDisplay(d, w, h, true);
    backend_hook::displayLatched(*this, d, w, h);
    latchedW = w;
    latchedH = h;
    latchedField = field;
    latchedScale = scale;
    latchedHudAnchorX = inventoryHudFrame.inputAnchor();
    haveLatch = true;
}

void GSR_IMPL::present(const DisplayState& d) {
    if (!ok)
        return;
    latchDisplay(d, latchedField);
    presentLatched();
}

void GSR_IMPL::presentLatched() {
    CpuProfile::Scope profile(cpuProfile, CpuProfile::PresentLatched);
    if (!ok || !haveLatch) {
        if (ok)
            backend_hook::skipPresent(*this);
        return;
    }
    auto window = backend_hook::acquireWindowImage(*this);
    if (!window) {
        backend_hook::skipPresent(*this);
        return;
    }
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
    if (!backend_hook::drawWindowImage(*this, window, vx, vy, vw, vh))
        return; // the device is gone: no overlay, no screenshot, no present
    if (overlayCb) {
        OverlayContext oc;
        backend_hook::overlayDevice(*this, oc, window);
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
        if (void* grid = backend_hook::gridCaptureView(*this);
            grid && gridCapture.serial && frame - gridCapture.frame <= 30) {
            oc.gridSrv = grid;
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
    backend_hook::afterOverlay(*this, window);
    ++presentCount;
    if (neuralEnabled && latchedGameWorld && runtimeFlags().dlssPresentLog)
        std::fprintf(stderr, "[dlss-presentation] present=%llu epoch=%llu neural=%d\n",
                     (unsigned long long)presentCount, (unsigned long long)latchedWorldEpoch, (int)latchedNeuralWorld);
    if (winShotNext < winShots.size() && (winShotByPresent ? presentCount : frame) >= winShots[winShotNext]) {
        CpuProfile::Scope screenshotProfile(cpuProfile, CpuProfile::WindowScreenshot);
        // GSR_WINSHOT=<prefix> GSR_WINSHOT_AT=<frame>[,<frame>...]: the finished window image (3D + HUD + settings
        // overlay / subtitles) as <prefix>_<frame>.png
        Image im;
        if (backend_hook::readWindowImage(*this, window, im)) {
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
        Image im;
        const bool written = backend_hook::readWindowImage(*this, window, im) && writePng(shotPath.c_str(), im);
        std::fprintf(stderr, "[gsr] requested window screenshot %ux%u -> %s%s\n", swapW, swapH, shotPath.c_str(),
                     written ? "" : " FAILED");
    }
    CpuProfile::Scope presentProfile(cpuProfile, CpuProfile::PresentCall);
    const int shown = backend_hook::presentWindowImage(*this, window);
    // The status reports the last gameplay image: F1 opens over the pause menu, which must not reset it.
    if (shown >= 0 && latchedGameWorld) {
        lastPresentedWater = latchedWaterWorld && shown > 0;
        lastPresentedRay = latchedRayWorld && shown > 0;
    }
}

void GSR_IMPL::clusterHud() {
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

void GSR_IMPL::endFrame() {
    if (!backend_hook::frameEnding(*this))
        return; // the device is gone
    nativeSubtitleOpen = false;
    tutorialBandRows.endFrame();
    CpuProfile::Scope profile(cpuProfile, CpuProfile::EndFrame);
    backend_hook::endFrameWorlds(*this);
    // debug: GSR_RTDUMPF=<first>-<last>: every render target at the end of each of these frames
    // (GSR_RTDUMP_DIR/rtf_<frame>_fbp<fbp>.png)
    if (runtimeFlags().rtDumpFrames) {
        const long a = runtimeFlags().rtDumpFrames->first, b = runtimeFlags().rtDumpFrames->second;
        if ((long)frame >= a && (long)frame <= b)
            for (auto& r : rts) {
                Image im;
                if (readTarget(*r, r->w * scale, std::min<uint32_t>(r->h, 512u) * scale, im)) {
                    const char* d = runtimeFlags().rtDumpDir.c_str();
                    char name[512];
                    std::snprintf(name, sizeof(name), "%s/rtf_%llu_fbp%u.png", d, (unsigned long long)frame, r->fbp);
                    writePng(name, im);
                }
            }
    }
    // Scratch targets drawn last frame but not in this one: stage their dirty rows now (see prefetchReadback).
    if constexpr (kCaps.readbackPrefetch)
        for (auto& r : rts)
            if (r->dirty && r->drawFrame + 1 == frame && rt_sync::lazySyncTarget(*r))
                prefetchReadback(*r, true);
    ++frame;
    backend_hook::frameAdvanced(*this);
    clusterHud();
    pumpUp(std::chrono::milliseconds(2));
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
    if (upLog && frame % 1200 == 0)
        std::fprintf(stderr,
                     "[texup] frame %llu: cache %.0f MB (%zu HD copies), dropped for age %llu / budget %llu, rebuilt "
                     "%llu, native draws while pending %llu, replaced %llu, upscaled %llu\n",
                     (unsigned long long)frame, (texBytes + upBytes) / 1048576.0, upCache.size(),
                     (unsigned long long)st.texUpAgeEvicted, (unsigned long long)st.texUpBudgetEvicted,
                     (unsigned long long)st.texUpRebuilt, (unsigned long long)st.texUpNativeDraws,
                     (unsigned long long)st.texReplaced, (unsigned long long)st.texUpscaled);
    // HD / upscaled copies: kept hd_texture_keep_s seconds after their last draw (0 = 240 frames, the old rule). The
    // copies are found again by content hash, so one kept longer is shown at once when the game uploads the texture
    // again (after turning back, a cut scene, a menu) instead of the native texture until a worker rebuilt it.
    uint64_t upMaxAge = 240;
    if (hdKeepSeconds) {
        const auto now = std::chrono::steady_clock::now();
        sweepTimes.emplace_back(frame, now);
        const auto horizon = now - std::chrono::seconds(hdKeepSeconds);
        size_t drop = 0;
        while (drop + 1 < sweepTimes.size() && sweepTimes[drop + 1].second <= horizon)
            ++drop;
        sweepTimes.erase(sweepTimes.begin(), sweepTimes.begin() + (std::ptrdiff_t)drop);
        // the oldest sample still at or before the horizon; until the game has run that long, nothing is old enough
        upMaxAge = sweepTimes.front().second <= horizon ? std::max<uint64_t>(240, frame - sweepTimes.front().first)
                                                         : UINT64_MAX;
    }
    for (auto it = upCache.begin(); it != upCache.end();) {
        if (frame - it->second->lastFrame > upMaxAge) {
            upBytes -= std::min(upBytes, it->second->bytes);
            st.texUpAgeEvicted++;
            upDropped.insert(it->first);
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
                st.texUpBudgetEvicted++;
                upDropped.insert(it->first);
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
    backend_hook::frameEnded(*this);
}

} // namespace gsr
