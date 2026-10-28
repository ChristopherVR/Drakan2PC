// Temporal/neural world passes: world entry and resolve, depth stretch, object motion, upscaler setup.
#include "d3d11_backend_impl.h"

namespace gsr {

bool Backend::Impl::enterWorld(RT& rt, DepthBuf& db, uint32_t zbp, float zmax, int sx1, int sy1) {
    const uint32_t fw = std::min<uint32_t>((uint32_t)sx1 + 1, rt.w), fh = std::min<uint32_t>((uint32_t)sy1 + 1, rt.h);
    if (!db.srv || !db.dsv) {
        upFault = true;
        upStatusText = "DLSS SR readable depth allocation failed";
        return false;
    }
    if (!camCur.haveVP || !camCur.haveSO)
        return false;
    rt.wgeom = tp->geometry(fw, fh, scale, upMode);
    for (auto& other : rts)
        if (other.get() != &rt)
            finalizeWorld(*other);
    if (!tp->prepare(rt.wgeom, upMode, upPreset, camCur)) {
        upStatusText = tp->status();
        upFault = true;
        return false;
    }
    rt.wcam = camCur;
    rt.wzbp = zbp;
    rt.wzmax = zmax;
    const TemporalPipeline::Geom& g = rt.wgeom;
    if (g.renderW < g.outW || g.renderH < g.outH) {
        // what was drawn so far (clear, backdrop) was drawn at full resolution: shrink it into the render rectangle
        ID3D11ShaderResourceView* srv = snapshot(rt, nullptr);
        if (!tp->stretch(srv, rt.w * scale, rt.h * scale, g.outW, g.outH, rt.rtv.Get(), g.renderW, g.renderH, true)) {
            upFault = true;
            upStatusText = "DLSS SR backdrop copy failed";
            return false;
        }
    }
    ctx->ClearDepthStencilView(db.dsv.Get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
    rt.wstate = 1;
    rt.wframe = frame;
    if (lastWorldFrame != 0 && frame - lastWorldFrame > 12)
        upReset = true; // paused / menu / loading in between: history is stale
    tp->jitter(worldFrames, g, curJx, curJy);
    if (hud::frozenWorld(sceneCamera.guiScreen, nativeMenu, nativeMenuScreen))
        curJx = curJy = 0; // a still world under the pause menu must not be resampled (hud_page.h)
    rt.wjx = curJx;
    rt.wjy = curJy;
    if (tpLog)
        std::fprintf(stderr,
                     "[dlss] f%llu enter world fbp=%u frame %ux%u render %ux%u out %ux%u jitter %.3f,%.3f reset=%d "
                     "cam=%d/%d\n",
                     (unsigned long long)frame, rt.fbp, g.frameW, g.frameH, g.renderW, g.renderH, g.outW, g.outH, curJx,
                     curJy, (int)upReset, (int)camCur.haveVP, (int)camCur.haveSO);
    return true;
}

bool Backend::Impl::stretchDepth(DepthBuf& depth, uint32_t sourceW, uint32_t sourceH, uint32_t destW, uint32_t destH,
                                 float jitterX, float jitterY) {
    if (!depth.srv || !depth.dsv || !psStretchDepth || !cbPresent)
        return false;
    if (!depth.copy) {
        D3D11_TEXTURE2D_DESC desc{};
        depth.tex->GetDesc(&desc);
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> copy;
        ComPtr<ID3D11ShaderResourceView> srv;
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_R32_FLOAT;
        view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        view.Texture2D.MipLevels = 1;
        if (FAILED(dev->CreateTexture2D(&desc, nullptr, &copy)) ||
            FAILED(dev->CreateShaderResourceView(copy.Get(), &view, &srv)))
            return false;
        depth.copy = copy;
        depth.copySrv = srv;
    }
    const float params[8] = {(float)sourceW, (float)sourceH, (float)destW, (float)destH, jitterX, jitterY, 0, 0};
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(ctx->Map(cbPresent.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return false;
    std::memcpy(mapped.pData, params, sizeof(params));
    ctx->Unmap(cbPresent.Get(), 0);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ctx->CopyResource(depth.copy.Get(), depth.tex.Get());
    ctx->OMSetRenderTargets(0, nullptr, depth.dsv.Get());
    ctx->OMSetDepthStencilState(getDepthState(D3D11_COMPARISON_ALWAYS, true), 0);
    const D3D11_VIEWPORT vp{0, 0, (float)destW, (float)destH, 0, 1};
    const D3D11_RECT sc{0, 0, (LONG)destW, (LONG)destH};
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetScissorRects(1, &sc);
    ctx->RSSetState(rast.Get());
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vsFull.Get(), nullptr, 0);
    ctx->PSSetShader(psStretchDepth.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(2, 1, cbPresent.GetAddressOf());
    ctx->PSSetShaderResources(0, 1, depth.copySrv.GetAddressOf());
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* none = nullptr;
    ctx->PSSetShaderResources(0, 1, &none);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    return true;
}

bool Backend::Impl::restoreWorldDepth(RT& rt, DepthBuf& depth, bool resolved) {
    if (rt.wgeom.renderW == rt.wgeom.outW && rt.wgeom.renderH == rt.wgeom.outH)
        return true;
    // Spatial color recovery is an unjittered stretch of the raster image;
    // only a resolved SR output uses the unjittered output-grid mapping.
    if (stretchDepth(depth, rt.wgeom.renderW, rt.wgeom.renderH, rt.wgeom.outW, rt.wgeom.outH, resolved ? rt.wjx : 0,
                     resolved ? rt.wjy : 0))
        return true;
    // Allocation/map failures happen before stretchDepth updates its copy.
    // The entry snapshot therefore still has full-coordinate depth. Restore
    // that baseline rather than handing reduced depth to native UI draws.
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    if (depth.copy)
        ctx->CopyResource(depth.tex.Get(), depth.copy.Get());
    upFault = true;
    neuralReset = true;
    upStatusText = "SR depth restore failed; native depth baseline restored and reduced rendering disabled";
    std::fprintf(stderr, "[dlss-sr] %s\n", upStatusText.c_str());
    return false;
}

void Backend::Impl::enterTemporalWorld(RT& rt, uint32_t zbp, float zmax, uint64_t xyoffset) {
    dumpTemporalStage("entry-color", rt.tex.Get());
    rt.neuralState = 1;
    rt.temporalEpoch = motionEpoch;
    rt.temporalSR = false;
    rt.neuralCamera = sceneCamera;
    rt.neuralDepth = zbp;
    rt.neuralZmax = zmax;
    rt.neuralXY[0] = (float)bitsOf(xyoffset, 0, 16);
    rt.neuralXY[1] = (float)bitsOf(xyoffset, 32, 16);
    rt.wgeom = {};
    rt.wgeom.frameW = sceneCamera.frameWidth;
    rt.wgeom.frameH = sceneCamera.frameHeight;
    rt.wgeom.outW = rt.wgeom.renderW = rt.wgeom.frameW * scale;
    rt.wgeom.outH = rt.wgeom.renderH = rt.wgeom.frameH * scale;
    rt.wjx = rt.wjy = 0;
    // One temporal feature/history owns one complete world target. Reserve
    // before any reduced draw, rather than competing at EndWorld.
    if ((temporalOwnerEpoch == motionEpoch && temporalOwner != &rt) ||
        (neuralEnabled && neuralLastPresent == presentCount)) {
        rt.neuralState = 2;
        return;
    }
    auto* readableDepth = getDepth(zbp, rt.w, rt.h);
    if (!readableDepth || !readableDepth->srv) {
        rt.neuralState = 2;
        neuralReset = true;
        return;
    }
    temporalOwner = &rt;
    temporalOwnerEpoch = motionEpoch;
    if (!neuralGuides)
        neuralGuides = std::make_unique<nr::guides::Guides>(dev.Get(), ctx.Get());
    if (!upMode || upFault || (neuralEnabled && neuralLastPresent == presentCount))
        return;
    if (!combinedSr)
        combinedSr = std::make_unique<sr_combined::Pipeline>(dev.Get(), ctx.Get());
    if (!tp)
        tp = std::make_unique<TemporalPipeline>(dev.Get(), ctx.Get());
    const auto geometry = tp->geometry(rt.wgeom.frameW, rt.wgeom.frameH, scale, upMode);
    const sr_combined::Config config{
        {geometry.renderW, geometry.renderH}, {geometry.outW, geometry.outH}, upMode, upPreset};
    if (!combinedSr->configure(config)) {
        upStatusText = combinedSr->status();
        upFault = true;
        return;
    }
    rt.wgeom = geometry;
    sr_combined::Pipeline::jitter(motionGuideSerial + 1, config, rt.wjx, rt.wjy);
    if (hud::frozenWorld(sceneCamera.guiScreen, nativeMenu, nativeMenuScreen))
        rt.wjx = rt.wjy = 0; // a still world under the pause menu must not be resampled (hud_page.h)
    if (geometry.renderW != geometry.outW || geometry.renderH != geometry.outH) {
        // Inventory clears only its preview depth rectangle. Leaving that
        // clear in full-resolution coordinates makes reduced world draws
        // test against stale UI depth, cutting holes out of the scenery.
        if (!stretchDepth(*readableDepth, geometry.outW, geometry.outH, geometry.renderW, geometry.renderH)) {
            upStatusText = "SR depth resize failed";
            upFault = true;
            rt.wgeom.renderW = geometry.outW;
            rt.wgeom.renderH = geometry.outH;
            rt.wjx = rt.wjy = 0;
            return;
        }
        auto* image = snapshot(rt, nullptr);
        if (!tp->stretch(image, rt.w * scale, rt.h * scale, geometry.outW, geometry.outH, rt.rtv.Get(),
                         geometry.renderW, geometry.renderH, true, true)) {
            ctx->OMSetRenderTargets(0, nullptr, nullptr);
            ctx->CopyResource(readableDepth->tex.Get(), readableDepth->copy.Get());
            upStatusText = "SR backdrop copy failed";
            upFault = true;
            rt.wgeom.renderW = geometry.outW;
            rt.wgeom.renderH = geometry.outH;
            rt.wjx = rt.wjy = 0;
            return;
        }
        rt.copyValid = false;
    }
    rt.temporalSR = true;
    upStatusText = combinedSr->status();
    if (tpLog)
        std::fprintf(stderr, "[dlss-sr] begin epoch=%llu render=%ux%u output=%ux%u jitter=%g,%g NR=%d\n",
                     (unsigned long long)motionEpoch, geometry.renderW, geometry.renderH, geometry.outW, geometry.outH,
                     rt.wjx, rt.wjy, (int)neuralEnabled);
}

void Backend::Impl::restoreTemporalAlpha(RT& rt, ID3D11ShaderResourceView* alpha, uint32_t width, uint32_t height) {
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ctx->OMSetRenderTargets(1, rt.rtv.GetAddressOf(), nullptr);
    ctx->OMSetDepthStencilState(getDepthState(D3D11_COMPARISON_ALWAYS, false), 0);
    ctx->OMSetBlendState(getBlendState(false, D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, 8), nullptr,
                         0xffffffffu);
    const D3D11_VIEWPORT vp{0, 0, (float)width, (float)height, 0, 1};
    const D3D11_RECT sc{0, 0, (LONG)width, (LONG)height};
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetScissorRects(1, &sc);
    ctx->RSSetState(rast.Get());
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vsFull.Get(), nullptr, 0);
    ctx->PSSetShader(psCopyRGBA.Get(), nullptr, 0);
    ctx->PSSetSamplers(0, 1, smpPt.GetAddressOf());
    ctx->PSSetShaderResources(0, 1, &alpha);
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* none = nullptr;
    ctx->PSSetShaderResources(0, 1, &none);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
}

bool Backend::Impl::prepareObjectMotion(RT& rt, const GsDrawState& s, const DrawVertex* vertices, uint32_t count,
                                        float sx, float sy) {
    const bool active = motionWorldOpen && (neuralEnabled || upMode) && rt.neuralState == 1;
    if (!active)
        return false;
    D3D11_TEXTURE2D_DESC desc{};
    if (rt.motionTex)
        rt.motionTex->GetDesc(&desc);
    if (!rt.motionTex || !rt.motionRtv || !rt.motionSrv || desc.Width != rt.w * scale || desc.Height != rt.h * scale) {
        rt.motionRtv.Reset();
        rt.motionSrv.Reset();
        rt.motionTex.Reset();
        rt.motionEpoch = 0;
        auto texture = makeTex(rt.w * scale, rt.h * scale, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
                               DXGI_FORMAT_R32G32B32A32_FLOAT);
        ComPtr<ID3D11RenderTargetView> rtv;
        ComPtr<ID3D11ShaderResourceView> srv;
        if (!texture || FAILED(dev->CreateRenderTargetView(texture.Get(), nullptr, &rtv)) ||
            FAILED(dev->CreateShaderResourceView(texture.Get(), nullptr, &srv)))
            return false;
        rt.motionTex = std::move(texture);
        rt.motionRtv = std::move(rtv);
        rt.motionSrv = std::move(srv);
    }
    if (rt.motionEpoch != motionEpoch) {
        const float clear[4]{};
        ctx->ClearRenderTargetView(rt.motionRtv.Get(), clear);
        rt.motionEpoch = motionEpoch;
    }
    auto invalidateMotion = [&]() {
        const float clear[4]{};
        ctx->ClearRenderTargetView(rt.motionRtv.Get(), clear);
        return false;
    };
    motion::Key key{objectScope.objectId, objectScope.generation, objectScope.meshId,
                    objectScope.partId,   objectScope.passKey,    0};
    key.batch = motionOrdinals[key]++;
    const uint32_t required = ObjectMotionScope::Valid | ObjectMotionScope::TopologyStable;
    const uint32_t rejected = ObjectMotionScope::Reset | ObjectMotionScope::Clipped | ObjectMotionScope::Ambiguous |
                              ObjectMotionScope::Rejected;
    const bool known = (objectScope.flags & required) == required && !(objectScope.flags & rejected);
    motionScopedBatches += known;
    motionVertices.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        const auto& v = vertices[i];
        const float iq = (s.flags & DF_FST) ? 1.0f : (v.q != 0 ? 1.0f / v.q : 0);
        motionVertices[i] = {v.x, v.y, v.z, v.s * iq, v.t * iq};
    }
    uint64_t topology = objectScope.topologyHash;
    for (uint64_t value : {uint64_t(count), s.tex0, s.alpha, uint64_t(s.flags), s.test})
        topology = (topology ^ value) * 1099511628211ull;
    const auto result = motionHistory.track(key, topology, motionVertices.data(), count, sx, sy, known, motionPrevious);
    if (result.matched) {
        ++motionMatchedBatches;
        motionMatchedVertices += count;
    } else
        ++motionRejectedBatches;
    const uint32_t bytes = count * sizeof(motion::Previous);
    // Ring buffer like the draw vertex buffer: a DISCARD per draw blocked Map once the GPU ran a frame behind.
    if (!motionVb || bytes > motionVbCapacity / 4) {
        const uint32_t capacity = std::max(bytes * 4, 4u << 20);
        ComPtr<ID3D11Buffer> buffer;
        D3D11_BUFFER_DESC d{};
        d.ByteWidth = capacity;
        d.Usage = D3D11_USAGE_DYNAMIC;
        d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(dev->CreateBuffer(&d, nullptr, &buffer)))
            return invalidateMotion();
        motionVb = std::move(buffer);
        motionVbCapacity = capacity;
        motionVbOffset = capacity; // the first map discards
    }
    D3D11_MAP mode = D3D11_MAP_WRITE_NO_OVERWRITE;
    if (bytes > motionVbCapacity - motionVbOffset) {
        mode = D3D11_MAP_WRITE_DISCARD;
        motionVbOffset = 0;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(ctx->Map(motionVb.Get(), 0, mode, 0, &mapped)))
        return invalidateMotion();
    motionVbDrawOffset = motionVbOffset;
    motionVbOffset += bytes;
    std::memcpy(static_cast<uint8_t*>(mapped.pData) + motionVbDrawOffset, motionPrevious.data(), bytes);
    ctx->Unmap(motionVb.Get(), 0);
    return true;
}

void Backend::Impl::finalizeNeuralWorld(RT& rt) {
    CpuProfile::Scope profile(cpuProfile, CpuProfile::TemporalResolve);
    if (rt.neuralState != 1)
        return;
    rt.neuralState = 2;
    bool wantNR = neuralEnabled;
    // The reconstructed triangles and the resident material projections are complete once the builder drained.
    {
        CpuProfile::Scope drainStage(cpuProfile, CpuProfile::SurfaceDrain);
        surfaceBuilder->drain();
    }
    const bool wantWater =
        modernWater && rt.surface.epoch == motionEpoch &&
        std::any_of(rt.surface.triangles.begin(), rt.surface.triangles.end(), [](const auto& t) { return t.water; });
    const bool wantShadows = rayShadows && nativeModels && !nativeModels->lighting.lights.empty();
    const bool wantRayScene = wantWater || wantShadows;
    wantNR = false;
    const bool wantSR = rt.temporalSR && combinedSr;
    if ((!wantNR && !wantSR && !wantRayScene) || !neuralGuides)
        return;
    if (wantNR && neuralLastPresent == presentCount) {
        recoverReducedWorld(rt);
        return;
    }
    auto* depth = getDepth(rt.neuralDepth, rt.w, rt.h);
    if (!depth || !depth->srv) {
        recoverReducedWorld(rt);
        neuralReset = true;
        motionHistory.reset();
        if (wantNR)
            neuralStatusText = "DLSS 5 skipped: world depth is unavailable";
        if (combinedSr)
            combinedSr->reset();
        return;
    }
    nr::guides::Frame guide;
    guide.sourceColor = rt.srv.Get();
    guide.sourceDepth = depth->srv.Get();
    guide.sourceObjectMotion = rt.motionEpoch == motionEpoch ? rt.motionSrv.Get() : nullptr;
    guide.colorTexture = guide.depthTexture = {rt.w * scale, rt.h * scale};
    guide.frame = {rt.neuralCamera.frameWidth, rt.neuralCamera.frameHeight};
    guide.guide = {guide.frame.width * scale, guide.frame.height * scale};
    if (wantSR)
        guide.guide = {rt.wgeom.renderW, rt.wgeom.renderH};
    auto camera = [&](const SceneGuideCamera& packet, nr::guides::Camera& result, const float* xy) {
        std::copy(std::begin(packet.worldToClipVU), std::end(packet.worldToClipVU), result.vp);
        std::copy(std::begin(packet.viewportScale), std::end(packet.viewportScale), result.scale);
        std::copy(std::begin(packet.viewportOffset), std::end(packet.viewportOffset), result.offset);
        result.xyOffset[0] = xy[0];
        result.xyOffset[1] = xy[1];
        result.valid = (packet.flags & SceneGuideCamera::Valid) != 0;
    };
    camera(rt.neuralCamera, guide.current, rt.neuralXY);
    camera(neuralPrevious, guide.previous, neuralPreviousXY);
    guide.zmax = rt.neuralZmax;
    // Engine projection has clip.z=32, clip.w=-view.z: reverse infinite Z.
    guide.farGs = 16.0f * rt.neuralCamera.viewportOffset[2] / guide.zmax;
    guide.nearGs = 16.0f * (rt.neuralCamera.viewportOffset[2] + rt.neuralCamera.viewportScale[2]) / guide.zmax;
    guide.jitterPixels[0] = wantSR ? rt.wjx : 0;
    guide.jitterPixels[1] = wantSR ? rt.wjy : 0;
    guide.reset = neuralReset || (rt.neuralCamera.flags & SceneGuideCamera::Reset);
    guide.frameIndex = static_cast<uint32_t>(++motionGuideSerial);
    nr::guides::FrameConstants constants;
    bool success;
    {
        CpuProfile::Scope stage(cpuProfile, CpuProfile::GuidePrepare);
        success = neuralGuides->prepare(guide) && neuralGuides->frameConstants(rt.neuralCamera.worldToView, constants);
    }
    if (success)
        validateObjectMotion(rt, guide, constants);
    auto textures = neuralGuides->textures();
    if (success) {
        dumpTemporalStage("world-color", rt.tex.Get());
        dumpTemporalStage("prepared-color", textures.color.texture);
        dumpTemporalStage("prepared-depth", textures.depth.texture);
        dumpTemporalStage("prepared-motion", textures.motion.texture);
    }
    const char* compareEffect = cstr(runtimeFlags().matchedCompareEffect);
    const bool compareWater = compareEffect && std::strcmp(compareEffect, "water") == 0 && !wantSR && !wantNR;
    const bool compareModels = compareEffect && std::strcmp(compareEffect, "models") == 0 && !wantSR && !wantNR;
    const bool compareShadows = compareEffect && std::strcmp(compareEffect, "shadows") == 0 && !wantSR && !wantNR;
    ComPtr<ID3D11Texture2D> waterBefore;
    if ((compareWater || compareModels || compareShadows) && wantRayScene && runtimeFlags().matchedCompareEpoch &&
        motionEpoch == *runtimeFlags().matchedCompareEpoch) {
        D3D11_TEXTURE2D_DESC desc{};
        rt.tex->GetDesc(&desc);
        if (SUCCEEDED(dev->CreateTexture2D(&desc, nullptr, &waterBefore))) {
            ctx->OMSetRenderTargets(0, nullptr, nullptr);
            ctx->CopyResource(waterBefore.Get(), rt.tex.Get());
        }
    }
    bool waterEvaluated = false, rayEvaluated = false;
    if (success && wantRayScene) {
        CpuProfile::Scope stage(cpuProfile, CpuProfile::RayScene);
        if (!waterRenderer)
            waterRenderer = std::make_unique<surface::RayWater>(dev.Get(), ctx.Get());
        float waterSeconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - waterStartTime).count();
        if (runtimeFlags().waterTime) {
            const float value = *runtimeFlags().waterTime;
            if (std::isfinite(value) && std::abs(value) < 1000000)
                waterSeconds = value;
        }
        // Resident level geometry keeps its acceleration structures across frames; only this frame's
        // reconstructed surface triangles are rebuilt.
        waterRenderer->shareStaticWorld(residentScene.projectedShared(), residentScene.geometryVersion,
                                        residentScene.attributeVersion());
        // The pass takes this frame's reconstructed triangles (the target's list is not read again this frame).
        std::vector<surface::Triangle> noTriangles;
        auto& rayScene = rt.surface.epoch == motionEpoch ? rt.surface.triangles : noTriangles;
        std::vector<surface::Triangle> compareScene;
        if ((compareModels || compareShadows) && waterBefore)
            compareScene = rayScene;
        if ((compareModels || compareShadows) && waterBefore && waterRenderer->ready() &&
            waterRenderer->submit(
                textures.color.texture, textures.depth.texture, textures.output.texture, constants, compareScene,
                waterSeconds, rayReflections, reflectionTextures, compareModels ? nullptr : nativeModels,
                surface::RayOptions{wantWater, compareShadows ? false : wantShadows, nullptr, true})) {
            ComPtr<ID3D11RenderTargetView> beforeRtv;
            if (SUCCEEDED(dev->CreateRenderTargetView(waterBefore.Get(), nullptr, &beforeRtv))) {
                ctx->OMSetRenderTargets(0, nullptr, nullptr);
                ctx->CopyResource(textures.color.texture, textures.output.texture);
                neuralGuides->composeRgb(beforeRtv.Get(), false);
                // Re-prepare the unchanged native image for the full scene.
                success = neuralGuides->prepare(guide);
            }
        }
        if (waterRenderer->ready() &&
            waterRenderer->submit(textures.color.texture, textures.depth.texture, textures.output.texture, constants,
                                  rayScene, waterSeconds, rayReflections, reflectionTextures, nativeModels,
                                  surface::RayOptions{wantWater, wantShadows,
                                                      motionEpoch == 200 ? cstr(runtimeFlags().raySceneDump) : nullptr,
                                                      true})) {
            ctx->OMSetRenderTargets(0, nullptr, nullptr);
            ctx->CopyResource(textures.color.texture, textures.output.texture);
            waterEvaluated = wantWater;
            rayEvaluated = true;
            if (tpLog || runtimeFlags().waterLog)
                std::fprintf(stderr, "[modern-water] epoch=%llu %s\n", (unsigned long long)motionEpoch,
                             waterRenderer->status().c_str());
            dumpTemporalStage("water-color", textures.color.texture);
        } else {
            static bool warned = false;
            if (!warned) {
                std::fprintf(stderr, "[modern-water] bypass: %s\n", waterRenderer->status().c_str());
                warned = true;
            }
        }
    }
    if (success && (wantNR || wantSR) && nativeModels) {
        CpuProfile::Scope stage(cpuProfile, CpuProfile::ModelGuides);
        {
            CpuProfile::Scope prepareStage(cpuProfile, CpuProfile::ModelPrepare);
            auto& pending = pendingModels; // complete: the builder was drained above
            if (pending.ready && pending.scene == nativeModels && pending.previous == modelMotion.previous()) {
                std::swap(modelFrame, pending.frame);
                if (constants.reset) { // what prepare(reset = true) returns
                    modelFrame.vertices.clear();
                    modelFrame.textures.clear();
                    modelFrame.matched = modelFrame.rejected = 0;
                }
            } else
                modelMotion.prepare(*nativeModels, constants.reset, modelFrame);
            pending.ready = false;
        }
        const auto& models = modelFrame;
        if (!neuralGuides->modelMotion(models.vertices, models.textures)) {
            std::fprintf(stderr, "[native-model-motion] fallback: %s\n", neuralGuides->status().c_str());
            success = false;
        }
        if (runtimeFlags().motionValidate && motionEpoch % 30 == 0)
            std::fprintf(stderr, "[native-model-motion] epoch=%llu matched=%u rejected=%u vertices=%zu reset=%u\n",
                         (unsigned long long)motionEpoch, models.matched, models.rejected, models.vertices.size(),
                         unsigned(constants.reset));
        dumpTemporalStage("model-motion", neuralGuides->textures().motion.texture);
    }
    nr::guides::Size neuralSize{};
    // Evaluates NR on the given exact-size guides into t.output. Output mode runs it on the SR output (output size);
    // render mode runs it on the reduced raster before SR. NR keeps the raster sample positions (measured: its output
    // follows the input jitter, see docs/journal/2026-10-23/nr-resolution.md), so SR still sees jittered samples.
    auto evaluateNeural = [&](const nr::guides::Textures& t, const nr::guides::FrameConstants& k) {
        bool ok = false;
        (void)t;
        (void)k;
        return ok;
    };
    // Render-resolution NR: the reduced raster goes through NR first and SR upscales the enhanced image. Only
    // meaningful with a reduced-resolution SR mode; without SR the raster already is output-sized.
    const bool neuralBeforeSr = wantNR && wantSR && neuralAtRender;
    bool neuralBeforeSrOk = false;
    bool haveSROutput = false;
    sr_combined::Outputs srOutput;
    if (!success)
        neuralStatusText = neuralGuides->status();
    if (success && neuralBeforeSr) {
        // textures.output only held the ray-scene scratch result, which is already copied into textures.color.
        neuralBeforeSrOk = evaluateNeural(textures, constants);
    }
    if (success && wantSR) {
        CpuProfile::Scope stage(cpuProfile, CpuProfile::SuperResolution);
        sr_combined::Inputs in;
        in.color = neuralBeforeSrOk ? textures.output.srv : textures.color.srv;
        in.alphaRender = neuralBeforeSrOk ? textures.color.srv : nullptr; // GS alpha, not NR output alpha
        in.depth = textures.depth.srv;
        in.motion = textures.motion.srv;
        in.jitterPixels[0] = rt.wjx;
        in.jitterPixels[1] = rt.wjy;
        in.reset = constants.reset;
        in.frameIndex = motionGuideSerial;
        haveSROutput = combinedSr->resolve(in);
        srOutput = combinedSr->outputs();
        success = haveSROutput && srOutput.srEvaluated;
        upStatusText = combinedSr->status();
        if (haveSROutput) {
            dumpTemporalStage("sr-color", srOutput.color.texture);
            textures.color = {srOutput.color.texture, srOutput.color.srv, srOutput.color.uav};
            textures.depth = {srOutput.depth.texture, srOutput.depth.srv, srOutput.depth.uav};
            textures.motion = {srOutput.motion.texture, srOutput.motion.srv, srOutput.motion.uav};
            textures.output = {srOutput.nrOutput.texture, srOutput.nrOutput.srv, srOutput.nrOutput.uav};
            textures.size = {srOutput.size.width, srOutput.size.height};
            constants.motionScale[0] = srOutput.motionScale[0];
            constants.motionScale[1] = srOutput.motionScale[1];
            constants.jitterPixels[0] = constants.jitterPixels[1] = 0;
            constants.reset = constants.reset || srOutput.historyReset;
        }
        if (tpLog)
            std::fprintf(stderr, "[dlss-sr] f%llu evaluate=%d render=%ux%u output=%ux%u serial=%llu reset=%d: %s\n",
                         (unsigned long long)frame, (int)success, rt.wgeom.renderW, rt.wgeom.renderH, rt.wgeom.outW,
                         rt.wgeom.outH, (unsigned long long)motionGuideSerial, (int)constants.reset,
                         upStatusText.c_str());
    }
    if (neuralBeforeSr) {
        // A failed render-resolution NR still presents the plain SR image this frame and resets both histories.
        if (success && !neuralBeforeSrOk && neuralStatusText.empty())
            neuralStatusText = "DLSS 5 render-resolution evaluation failed";
        success = success && neuralBeforeSrOk;
    } else if (success && wantNR)
        success = evaluateNeural(textures, constants);
    {
        CpuProfile::Scope stage(cpuProfile, CpuProfile::TemporalCompose);
        if (wantSR && haveSROutput) {
            // Render-resolution NR is already inside the SR color; only output-resolution NR has a separate image.
            const bool composed = combinedSr->composeRgb(rt.rtv.Get(), wantNR && success && !neuralBeforeSr, upSharp);
            restoreTemporalAlpha(rt, srOutput.color.srv, srOutput.size.width, srOutput.size.height);
            const bool restoredDepth = restoreWorldDepth(rt, *depth, true);
            if (!composed)
                neuralStatusText = "Temporal output composition failed: " + combinedSr->status();
            else if (!restoredDepth)
                neuralStatusText = "Temporal output depth restoration failed: " + upStatusText;
            success = success && composed && restoredDepth;
        } else if (!wantSR && success)
            success = neuralGuides->composeRgb(rt.rtv.Get(), wantNR);
        else if (wantSR) {
            auto* image = snapshot(rt, nullptr);
            tp->stretch(image, rt.w * scale, rt.h * scale, rt.wgeom.renderW, rt.wgeom.renderH, rt.rtv.Get(),
                        rt.wgeom.outW, rt.wgeom.outH, true, true);
            restoreWorldDepth(rt, *depth, false);
        }
    }
    if (tpLog && wantNR)
        std::fprintf(stderr, "[dlss-nr] f%llu present=%llu scene=%llu evaluate=%d reset=%d guides=%ux%u (%s): %s\n",
                     (unsigned long long)frame, (unsigned long long)presentCount,
                     (unsigned long long)rt.neuralCamera.serial, (int)success, (int)constants.reset, neuralSize.width,
                     neuralSize.height, neuralBeforeSr ? "render" : "output", neuralStatusText.c_str());
    neuralReset = !success;
    if (!success) {
        if (combinedSr)
            combinedSr->reset();
        motionHistory.reset();
        modelMotion.reset();
    }
    if (success) {
        dumpTemporalStage("composed-color", rt.tex.Get());
        if (wantNR)
            rt.neuralOutputEpoch = motionEpoch;
        if (waterEvaluated)
            rt.waterOutputEpoch = motionEpoch;
        if (rayEvaluated)
            rt.rayOutputEpoch = motionEpoch;
        // The NR on/off compare needs a separate NR image; render-resolution NR has none (its SR image includes it).
        if (((wantNR && !neuralBeforeSr) ||
             ((compareWater || compareModels || compareShadows) && waterBefore && rayEvaluated)) &&
            runtimeFlags().matchedCompareEpoch && motionEpoch == *runtimeFlags().matchedCompareEpoch) {
            D3D11_TEXTURE2D_DESC desc{};
            rt.tex->GetDesc(&desc);
            rt.compareTex.Reset();
            rt.compareRtv.Reset();
            rt.compareSrv.Reset();
            if (SUCCEEDED(dev->CreateTexture2D(&desc, nullptr, &rt.compareTex)) &&
                SUCCEEDED(dev->CreateRenderTargetView(rt.compareTex.Get(), nullptr, &rt.compareRtv)) &&
                SUCCEEDED(dev->CreateShaderResourceView(rt.compareTex.Get(), nullptr, &rt.compareSrv))) {
                ctx->OMSetRenderTargets(0, nullptr, nullptr);
                ctx->CopyResource(rt.compareTex.Get(), rt.tex.Get());
                bool offComposed = true;
                if (compareWater || compareModels || compareShadows) {
                    ctx->OMSetRenderTargets(0, nullptr, nullptr);
                    ctx->CopyResource(rt.compareTex.Get(), waterBefore.Get());
                } else
                    offComposed = wantSR ? combinedSr->composeRgb(rt.compareRtv.Get(), false, upSharp)
                                         : neuralGuides->composeRgb(rt.compareRtv.Get(), false);
                if (offComposed)
                    rt.compareEpoch = motionEpoch;
            }
        }
        neuralPrevious = rt.neuralCamera;
        motionHistory.commitFrame();
        modelMotion.commit(nativeModels);
        neuralPreviousXY[0] = rt.neuralXY[0];
        neuralPreviousXY[1] = rt.neuralXY[1];
        rt.copyValid = false;
        rt.dirty = true;
        ++rt.writes;
        rt.dy0 = 0;
        rt.dy1 = std::min(rt.h, guide.frame.height);
        if (wantNR)
            neuralStatusText = "DLSS 5 evaluated and composed; awaiting presentation";
    }
    rt.copyValid = false;
    rt.dirty = true;
    ++rt.writes;
    rt.dy0 = 0;
    rt.dy1 = std::min(rt.h, guide.frame.height);
}

void Backend::Impl::finalizeWorld(RT& rt) {
    if (rt.wstate != 1)
        return;
    finalizeNeuralWorld(rt); // neural RGB is reconstructed before SR and native HUD draws
    DepthBuf* db = worldDepth(rt);
    TemporalPipeline::ResolveIn in;
    in.colorRT = rt.tex.Get();
    in.colorSrv = rt.srv.Get();
    in.colorRtv = rt.rtv.Get();
    in.depthTex = db->tex.Get();
    in.depthSrv = db->srv.Get();
    in.g = rt.wgeom;
    in.zmax = rt.wzmax;
    in.cam = &rt.wcam;
    in.prev = camPrevValid ? &camPrev : nullptr;
    in.jx = rt.wjx;
    in.jy = rt.wjy;
    in.reset = upReset || lastWorldGeom.renderW != rt.wgeom.renderW || lastWorldGeom.renderH != rt.wgeom.renderH ||
               lastWorldGeom.outW != rt.wgeom.outW || lastWorldGeom.outH != rt.wgeom.outH;
    in.mode = upMode;
    in.preset = upPreset;
    in.sharpness = upSharp;
    const bool ok = tp->resolve(in);
    if (tpLog)
        std::fprintf(stderr, "[dlss] f%llu resolve fbp=%u dlss=%d reset=%d gpu=%.2fms\n", (unsigned long long)frame,
                     rt.fbp, (int)ok, (int)upReset, tp->lastGpuMs());
    upReset = !ok;
    if (!ok) {
        upFault = true;
        upStatusText = tp->status();
    } else
        upStatusText = tp->status();
    camPrev = rt.wcam;
    camPrevValid = ok && rt.wcam.haveVP && rt.wcam.haveSO;
    ++worldFrames;
    lastWorldFrame = frame;
    lastWorldGeom = rt.wgeom;
    rt.wstate = 2;
    rt.wframe = frame;
    rt.dirty = true;
    ++rt.writes;
    rt.dy0 = 0;
    rt.dy1 = std::min(rt.h, rt.wgeom.frameH);
    rt.copyValid = false;
    ctx->ClearDepthStencilView(db->dsv.Get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
}

void Backend::Impl::finalizeAllWorlds() {
    if (!upMode)
        return;
    for (auto& r : rts)
        finalizeWorld(*r);
}

// returns true if this draw goes into the render rectangle (world phase), after any phase transition
bool Backend::Impl::upscalePhase(RT& rt, DepthBuf& db, bool isWorld, bool depthGE, uint32_t zbp, float zmax, int sx1,
                                 int sy1) {
    if (rt.wstate == 0) {
        if (!(isWorld && depthGE))
            return false;
        return enterWorld(rt, db, zbp, zmax, sx1, sy1);
    }
    if (rt.wstate == 1) {
        if (isWorld && rt.wzbp == zbp && rt.wzmax == zmax &&
            rt.wgeom.frameW == std::min<uint32_t>((uint32_t)sx1 + 1, rt.w) &&
            rt.wgeom.frameH == std::min<uint32_t>((uint32_t)sy1 + 1, rt.h))
            return true;
        finalizeWorld(rt);
    }
    return false;
}

void Backend::Impl::recoverReducedWorld(RT& rt) {
    if (rt.temporalSR && tp) {
        auto* image = snapshot(rt, nullptr);
        auto* depth = getDepth(rt.neuralDepth, rt.w, rt.h);
        if (depth)
            restoreWorldDepth(rt, *depth, false);
        if (!tp->stretch(image, rt.w * scale, rt.h * scale, rt.wgeom.renderW, rt.wgeom.renderH, rt.rtv.Get(),
                         rt.wgeom.outW, rt.wgeom.outH, true, true)) {
            upFault = true;
            neuralReset = true;
            upStatusText = "SR full-size recovery failed; reduced rendering disabled";
            std::fprintf(stderr, "[dlss-sr] %s\n", upStatusText.c_str());
        }
        rt.dirty = true;
        ++rt.writes;
        rt.dy0 = 0;
        rt.dy1 = std::min(rt.h, rt.wgeom.frameH);
        rt.copyValid = false;
    }
    rt.neuralState = 2;
    rt.temporalSR = false;
}

void Backend::Impl::discardTemporalWorlds() {
    pendingTemporalEnd = false;
    for (auto& target : rts) {
        auto& rt = *target;
        if (rt.neuralState != 1)
            continue;
        recoverReducedWorld(rt);
    }
    temporalOwner = nullptr;
    temporalOwnerEpoch = 0;
}

void Backend::Impl::setUpscale(uint32_t mode, uint32_t preset, float sharp) {
    const bool requestedOff = mode == 0;
    mode = mode <= 4 ? mode : 0;
    preset = (preset >= 10 && preset <= 13) ? preset : 0;
    sharp = std::isfinite(sharp) ? std::clamp(sharp, 0.0f, 1.0f) : 0.0f;
    if (!upscaleNeedsReset(upMode, upPreset, mode, preset, upFault)) {
        upPreset = preset;
        upSharp = sharp;
        return;
    }
    discardTemporalWorlds();
    finalizeAllWorlds();
    if (combinedSr)
        combinedSr.reset();
    neuralReset = true;
    neuralPrevious = {};
    motionHistory.reset();
    motionOrdinals.clear();
    motionWorldOpen = false;
    for (auto& target : rts)
        target->neuralState = 0;
    // A fresh pipeline drops feature history, failure latches and NGX state on each explicit toggle.
    tp.reset();
    upFault = false;
    if (mode && !tp)
        tp = std::make_unique<TemporalPipeline>(dev.Get(), ctx.Get());
    if (mode && tp && !tp->supported()) {
        upStatusText = tp->status();
        std::fprintf(stderr, "[gsr] upscaler unavailable: %s\n", upStatusText.c_str());
        mode = 0;
    }
    const bool was = upMode != 0;
    upMode = mode;
    upPreset = preset;
    upSharp = sharp;
    if (tp)
        tp->setGpuTiming(tpTiming);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    if (was != (mode != 0))
        depths.clear(); // depth buffers are typeless + shader readable while the upscaler runs
    for (auto& r : rts)
        r->wstate = 0;
    upReset = true;
    camPrevValid = false;
    camCur = {};
    camPrev = {};
    worldFrames = 0;
    lastWorldFrame = 0;
    curJx = curJy = 0;
    if (requestedOff)
        upStatusText = "DLSS SR off";
    if (tp && mode)
        upStatusText = tp->status();
    if (!mode)
        tp.reset();
    if (mode)
        std::fprintf(stderr, "[gsr] upscaler mode %u preset %u: %s\n", mode, preset, upStatusText.c_str());
}

void Backend::Impl::noteVuUpload(uint32_t a, uint32_t n, const float* rows) {
    if ((!upMode && !cfg.neuralRendering) || !rows)
        return;
    static uint64_t loggedFrame = ~0ull;
    static unsigned loggedMask = 0;
    if (loggedFrame != frame) {
        loggedFrame = frame;
        loggedMask = 0;
    }
    const unsigned logBit = a < 4 ? 1u : 2u;
    const bool logCamera = tpLog && frame < 4000 && !(loggedMask & logBit);
    if (logCamera) {
        loggedMask |= logBit;
        std::fprintf(stderr, "[dlss] f%llu vu upload addr=%u n=%u row0=%g %g %g %g\n", (unsigned long long)frame, a, n,
                     rows[0], rows[1], rows[2], rows[3]);
    }
    if (a >= 7 || !n)
        return;
    for (unsigned i = 0; i < 28; ++i)
        if ((i < 16 || i >= 20) && !std::isfinite(rows[i])) {
            upReset = true;
            camCur = {};
            return;
        }
    const bool vpChanged = a < 4 && std::memcmp(camCur.vp, rows, sizeof(camCur.vp)) != 0;
    const bool soChanged =
        a < 7 && a + n > 5 && (std::memcmp(camCur.offset, rows + 20, 16) || std::memcmp(camCur.scale, rows + 24, 16));
    if (vpChanged || soChanged)
        finalizeAllWorlds();
    if (a < 4) {
        std::memcpy(camCur.vp, rows, sizeof(camCur.vp));
        camCur.haveVP = true;
        ++camCur.serial;
    }
    if (a + n > 5) {
        if (logCamera)
            std::fprintf(stderr,
                         "[dlss-camera] f%llu viewport a=%u n=%u row4=%g,%g,%g,%g row5=%g,%g,%g,%g row6=%g,%g,%g,%g\n",
                         (unsigned long long)frame, a, n, rows[16], rows[17], rows[18], rows[19], rows[20], rows[21],
                         rows[22], rows[23], rows[24], rows[25], rows[26], rows[27]);
        std::memcpy(camCur.offset, rows + 20, 16);
        std::memcpy(camCur.scale, rows + 24, 16);
        camCur.haveSO = std::fabs(rows[24]) > 1e-6f && std::fabs(rows[25]) > 1e-6f && std::fabs(rows[26]) > 1e-6f;
    }
}

} // namespace gsr
