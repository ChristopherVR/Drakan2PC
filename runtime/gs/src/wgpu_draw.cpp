// Per-draw state setup and submission (Impl::draw) and draw-level diagnostics (WebGPU backend; policy as
// d3d11_draw.cpp).
#include "wgpu_backend_impl.h"

#include <optional>

namespace gsr {

void WgpuBackend::Impl::logDraw(const GsDrawState& s, const DrawVertex* v, uint32_t count) {
    std::fprintf(stderr,
                 "[draw f%llu] n=%u flags=%03x tex0=%016llx tex1=%016llx clamp=%llx test=%llx alpha=%llx "
                 "frame=%llx zbuf=%llx fogcol=%llx texa=%llx\n",
                 (unsigned long long)frame, count, s.flags, (unsigned long long)s.tex0, (unsigned long long)s.tex1,
                 (unsigned long long)s.clamp, (unsigned long long)s.test, (unsigned long long)s.alpha,
                 (unsigned long long)s.frame, (unsigned long long)s.zbuf, (unsigned long long)s.fogcol,
                 (unsigned long long)s.texa);
    {
        uint32_t garb = 0, edge = 0;
        for (uint32_t i = 0; i < count; ++i) {
            if (std::fabs(v[i].s) > 1e9f || std::fabs(v[i].t) > 1e9f || std::fabs(v[i].q) > 1e9f)
                ++garb;
            if (v[i].x >= 2047.0f && v[i].y >= 2047.0f)
                ++edge;
        }
        if (garb || edge)
            std::fprintf(stderr, "    GARBAGE: %u of %u vertices have |stq|>1e9, %u at (2047.9,2047.9)\n", garb, count,
                         edge);
    }
    for (uint32_t i = 0; i < std::min(count, drawLogVerts); ++i)
        std::fprintf(stderr, "    v%u xy=(%.1f,%.1f) z=%.0f fog=%.0f rgba=(%.0f,%.0f,%.0f,%.0f) stq=(%.6g,%.6g,%.6g)\n",
                     i, v[i].x, v[i].y, v[i].z, v[i].fog, v[i].r, v[i].g, v[i].b, v[i].a, v[i].s, v[i].t, v[i].q);
}

// texel / pixel density of the first triangle: ~1 for sprites drawn 1:1 (HUD, fonts, menu art)
bool WgpuBackend::Impl::oneToOneSprite(const GsDrawState& s, const DrawVertex* v, uint32_t count) {
    if (!(s.flags & DF_SPRITE) || count < 3)
        return false;
    const uint32_t W = 1u << std::min<uint64_t>(bitsOf(s.tex0, 26, 4), 10),
                   H = 1u << std::min<uint64_t>(bitsOf(s.tex0, 30, 4), 10);
    const bool fst = s.flags & DF_FST;
    auto uv = [&](const DrawVertex& d, float& u, float& t) {
        if (fst) {
            u = d.s;
            t = d.t;
        } else {
            const float iq = d.q != 0.0f ? 1.0f / d.q : 0.0f;
            u = d.s * iq * (float)W;
            t = d.t * iq * (float)H;
        }
    };
    float u0, t0, u1, t1, u2, t2;
    uv(v[0], u0, t0);
    uv(v[1], u1, t1);
    uv(v[2], u2, t2);
    const float pa = std::fabs((v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[2].x - v[0].x) * (v[1].y - v[0].y));
    const float ta = std::fabs((u1 - u0) * (t2 - t0) - (u2 - u0) * (t1 - t0));
    if (pa < 1e-3f || ta < 1e-3f)
        return false;
    const float dens = std::sqrt(ta / pa);
    return dens > 0.7f && dens < 1.45f;
}

// GSR_LODSTAT=1: accumulate GS-formula LOD (from Q, K, L) against the screen-derivative LOD of every mip-mapped
// textured triangle
void WgpuBackend::Impl::accumulateLod(const GsDrawState& s, const DrawVertex* v, uint32_t count) {
    const uint32_t mxl = (uint32_t)bitsOf(s.tex1, 2, 3), lcm = (uint32_t)bitsOf(s.tex1, 0, 1),
                   L = (uint32_t)bitsOf(s.tex1, 19, 2);
    if (mxl == 0)
        return;
    int32_t kk = (int32_t)bitsOf(s.tex1, 32, 12);
    if (kk & 0x800)
        kk -= 0x1000;
    const double K = kk / 16.0;
    const uint32_t W = 1u << std::min<uint64_t>(bitsOf(s.tex0, 26, 4), 10),
                   H = 1u << std::min<uint64_t>(bitsOf(s.tex0, 30, 4), 10);
    const bool fst = s.flags & DF_FST;
    for (uint32_t i = 0; i + 2 < count; i += 3) {
        const DrawVertex* t = v + i;
        const double det = (t[1].x - t[0].x) * (t[2].y - t[0].y) - (t[2].x - t[0].x) * (t[1].y - t[0].y);
        const double area = std::fabs(det) * 0.5;
        if (area < 1.0)
            continue;
        // plane gradients of S,T,Q (linear in screen space)
        auto grad = [&](double a0, double a1, double a2, double& gx, double& gy) {
            gx = ((a1 - a0) * (t[2].y - t[0].y) - (a2 - a0) * (t[1].y - t[0].y)) / det;
            gy = ((a2 - a0) * (t[1].x - t[0].x) - (a1 - a0) * (t[2].x - t[0].x)) / det;
        };
        double sx, sy, tx, ty, qx = 0, qy = 0;
        grad(t[0].s, t[1].s, t[2].s, sx, sy);
        grad(t[0].t, t[1].t, t[2].t, tx, ty);
        const double S = (t[0].s + t[1].s + t[2].s) / 3, T = (t[0].t + t[1].t + t[2].t) / 3;
        double Q = 1;
        double dudx, dudy, dvdx, dvdy;
        if (fst) {
            dudx = sx;
            dudy = sy;
            dvdx = tx;
            dvdy = ty;
        } else {
            grad(t[0].q, t[1].q, t[2].q, qx, qy);
            Q = (t[0].q + t[1].q + t[2].q) / 3;
            if (Q <= 0)
                continue;
            dudx = (sx * Q - S * qx) / (Q * Q) * W;
            dudy = (sy * Q - S * qy) / (Q * Q) * W;
            dvdx = (tx * Q - T * qx) / (Q * Q) * H;
            dvdy = (ty * Q - T * qy) / (Q * Q) * H;
        }
        // singular values of the 2x2 jacobian
        const double a = dudx * dudx + dvdx * dvdx + dudy * dudy + dvdy * dvdy;
        const double b = (dudx * dvdy - dudy * dvdx);
        const double disc = std::sqrt(std::max(0.0, a * a - 4 * b * b));
        const double s1 = std::sqrt(std::max(1e-12, (a + disc) / 2)), s2 = std::sqrt(std::max(1e-12, (a - disc) / 2));
        const double gsLod = (lcm || fst) ? K : (-std::log2(Q) * (double)(1u << L) + K);
        const double lmaj = std::log2(s1), lmin = std::log2(s2);
        lodAcc.area += area;
        lodAcc.gs += area * gsLod;
        lodAcc.major += area * lmaj;
        lodAcc.minor += area * lmin;
        const double ratio = s1 / s2;
        lodAcc.ratio += area * std::log2(ratio);
        lodAcc.hist[ratio < 1.25 ? 0 : ratio < 2 ? 1 : ratio < 4 ? 2 : ratio < 8 ? 3 : 4] += area;
        const double d = lmaj - gsLod;
        lodAcc.histD[d < -2     ? 0
                     : d < -1   ? 1
                     : d < -0.5 ? 2
                     : d < 0.5  ? 3
                     : d < 1    ? 4
                     : d < 2    ? 5
                     : d < 3    ? 6
                                : 7] += area;
        lodAcc.tris++;
    }
}

void WgpuBackend::Impl::printLodStat() {
    if (!lodStat || lodAcc.area <= 0)
        return;
    const double A = lodAcc.area;
    std::fprintf(stderr,
                 "[lodstat] %llu mip-mapped triangles, %.0f px (native area-weighted)\n  mean GS LOD %.3f | mean "
                 "log2(major axis texels/px) %.3f | mean log2(minor) %.3f | mean log2 aniso ratio %.3f\n",
                 (unsigned long long)lodAcc.tris, A, lodAcc.gs / A, lodAcc.major / A, lodAcc.minor / A,
                 lodAcc.ratio / A);
    std::fprintf(stderr, "  aniso ratio buckets (area): <1.25 %.1f%%  <2 %.1f%%  <4 %.1f%%  <8 %.1f%%  >=8 %.1f%%\n",
                 100 * lodAcc.hist[0] / A, 100 * lodAcc.hist[1] / A, 100 * lodAcc.hist[2] / A, 100 * lodAcc.hist[3] / A,
                 100 * lodAcc.hist[4] / A);
    std::fprintf(stderr,
                 "  log2(major) - GS LOD buckets (area): <-2 %.1f%%  <-1 %.1f%%  <-.5 %.1f%%  [-.5,.5) %.1f%%  <1 "
                 "%.1f%%  <2 %.1f%%  <3 %.1f%%  >=3 %.1f%%\n",
                 100 * lodAcc.histD[0] / A, 100 * lodAcc.histD[1] / A, 100 * lodAcc.histD[2] / A,
                 100 * lodAcc.histD[3] / A, 100 * lodAcc.histD[4] / A, 100 * lodAcc.histD[5] / A,
                 100 * lodAcc.histD[6] / A, 100 * lodAcc.histD[7] / A);
}

void WgpuBackend::Impl::draw(const GsDrawState& s, const ClutBuf& clut, const DrawVertex* v, uint32_t count) {
    CpuProfile::Scope profile(cpuProfile, CpuProfile::Draw);
    if (!ok || count == 0)
        return;
    const int64_t di = drawIndex++;
    if (inventoryLogDelay >= 0 && sceneCamera.guiScreen == 21 && inventoryLogStart == ~uint64_t{0})
        inventoryLogStart = frame + (uint64_t)inventoryLogDelay;
    const bool logThis = (drawLogFirst >= 0 && (int64_t)frame >= drawLogFirst && (int64_t)frame <= drawLogLast) ||
                         (drawLogTbp >= 0 && (s.flags & DF_TME) && (int64_t)bitsOf(s.tex0, 0, 14) == drawLogTbp) ||
                         (inventoryLogDelay >= 0 && inventoryLogStart != ~uint64_t{0} && frame >= inventoryLogStart &&
                          frame - inventoryLogStart < 2);
    if (logThis) {
        std::fprintf(stderr, "#%lld ", (long long)di);
        logDraw(s, v, count);
    }
    if (drawStop >= 0 && di >= drawStop)
        return;
    if (di >= skipA && di <= skipB && skipA >= 0)
        return;
    // --- target
    uint32_t fbp = (uint32_t)bitsOf(s.frame, 0, 9), fbw = (uint32_t)bitsOf(s.frame, 16, 6),
             fpsm = (uint32_t)bitsOf(s.frame, 24, 6);
    const uint32_t fbmsk = (uint32_t)(s.frame >> 32);
    if (fpsm != PSM_CT32 && fpsm != PSM_CT24 && fpsm != PSM_CT16 && fpsm != PSM_CT16S) {
        if (isZ(fpsm))
            fpsm = PSM_CT32;
        else if (!isValidPsm(fpsm))
            fpsm = PSM_CT32;
    }
    const int sx0 = (int)bitsOf(s.scissor, 0, 11), sx1 = (int)bitsOf(s.scissor, 16, 11),
              sy0 = (int)bitsOf(s.scissor, 32, 11), sy1 = (int)bitsOf(s.scissor, 48, 11);

    // bbox
    float minx = 1e30f, miny = 1e30f, maxx = -1e30f, maxy = -1e30f;
    for (uint32_t i = 0; i < count; ++i) {
        minx = std::min(minx, v[i].x);
        maxx = std::max(maxx, v[i].x);
        miny = std::min(miny, v[i].y);
        maxy = std::max(maxy, v[i].y);
    }
    const uint32_t needH = (uint32_t)std::max(1, std::min(sy1 + 1, 2048));
    RT* rt = getRT(fbp, fbw, fpsm, needH);
    syncRT(*rt);
    if (gridCapture.open && !gridCapture.rt && rt->w >= 480 && rt->h >= 320)
        gridCaptureStart(*rt); // the first display draw of a captured grid: save what it will cover
    // Native EndWorld precedes the final CPU-clipped water packet drain.
    // Resolve at the first actual HUD submission, after those perspective
    // world triangles and before any HUD color reaches the framebuffer.
    if (pendingTemporalEnd && !engineWorldOpen && rt->w >= 480 && (fbmsk & 0xffffffu) != 0xffffffu) {
        bool perspectiveTail = false;
        if (!(s.flags & (DF_SPRITE | DF_FST)) && (s.flags & DF_TME))
            for (uint32_t i = 0; i < count; ++i)
                perspectiveTail |= !hud::nativeUiQ(v[i].q);
        // Native HUD sprites/RT-backed quads can retain FGE. Their native
        // UI Q/FST still separates them from the late perspective water.
        const bool ui = (s.flags & DF_SPRITE) || (!perspectiveTail && ((s.flags & DF_TME) || !(s.flags & DF_FGE)));
        if (ui) {
            if (runtimeFlags().waterLog && motionEpoch == 200)
                std::fprintf(stderr, "[world-tail] resolve before HUD flags=%x bounds=%g,%g..%g,%g\n", s.flags, minx,
                             miny, maxx, maxy);
            for (auto& target : rts)
                finalizeNeuralWorld(*target);
            pendingTemporalEnd = false;
        }
    }
    // A world clear/sky draw also owns the new image, even if no eligible
    // temporal geometry follows. Never inherit an earlier NR success tag.
    if (engineWorldOpen && rt->w >= 480 && rt->h >= 320 && (fbmsk & 0xffffffu) != 0xffffffu) {
        if (rt->worldPhaseSeen != worldPhaseSerial) {
            rt->worldPhaseSeen = worldPhaseSerial;
            rt->worldEpoch = 0;
            rt->neuralOutputEpoch = rt->waterOutputEpoch = rt->rayOutputEpoch = 0;
            if (inventoryHudFrame.centredRecently(frame)) {
                const float black[4] = {0, 0, 0, 128.0f / 255.0f};
                gpu->clearColor(*rt->tex, black);
            }
        }
        if ((sceneCamera.flags & (SceneGuideCamera::Valid | SceneGuideCamera::World)) ==
            (SceneGuideCamera::Valid | SceneGuideCamera::World))
            rt->worldEpoch = motionEpoch;
    }

    int cx0 = std::max(sx0, (int)std::floor(minx)), cx1 = std::min(sx1, (int)std::ceil(maxx));
    int cy0 = std::max(sy0, (int)std::floor(miny)), cy1 = std::min(sy1, (int)std::ceil(maxy));
    cx0 = std::max(cx0, 0);
    cy0 = std::max(cy0, 0);
    cx1 = std::min(cx1, (int)rt->w - 1);
    cy1 = std::min(cy1, (int)rt->h - 1);
    if (cx1 < cx0 || cy1 < cy0)
        return;

    // --- depth
    const uint32_t zbp = (uint32_t)bitsOf(s.zbuf, 0, 9);
    const uint32_t zpsm = 0x30u | (uint32_t)bitsOf(s.zbuf, 24, 4);
    const bool zmsk = (s.zbuf >> 32) & 1;
    const uint32_t ztst = (uint32_t)bitsOf(s.test, 17, 2);
    const bool zte = (s.test >> 16) & 1;
    DepthBuf* db = getDepth(zbp, rt->w, rt->h);
    float zmax = (zpsm == PSM_Z32) ? 4294967295.0f : (zpsm == PSM_Z24 ? 16777215.0f : 65535.0f);

    // --- texture
    TexBind tb;
    const bool tme = s.flags & DF_TME;
    bool haveTex = false;
    if (tme)
        haveTex = resolveTexture(s, clut, tb, oneToOneSprite(s, v, count));
    // Startup may have opened a generic world phase before the first
    // menu handler tick. Exact retail artwork identifies this first GUI
    // draw without waiting a frame or classifying arbitrary world images.
    if (nativeMenu && haveTex && tb.menuArtwork && rt->w >= 480 && rt->h >= 320) {
        engineWorldOpen = false;
        sceneCamera.flags = 0;
    }
    // Endpoint bounds belong to each sprite, not an entire GS batch. Keep
    // the original order and split only batches whose every member has a
    // validated native footprint (the health frame/highlight uses two).
    if (spriteFootprint && !engineWorldOpen && scale > 1 && haveTex && !tb.isRT && rt->w >= 480 &&
        (s.flags & DF_SPRITE) && count > 6 && count % 6 == 0 && bitsOf(s.tex1, 2, 3) == 0 &&
        bitsOf(s.tex1, 5, 4) == 0 && cfg.textureFilter != 2) {
        bool valid = true;
        const float tw = (float)(1u << std::min<uint64_t>(bitsOf(s.tex0, 26, 4), 10));
        const float th = (float)(1u << std::min<uint64_t>(bitsOf(s.tex0, 30, 4), 10));
        for (uint32_t i = 0; i < count && valid; i += 6)
            valid = sprite_footprint::bounds(v + i, 6, (s.flags & DF_FST) != 0, tw, th, tb.factor).valid;
        if (valid) {
            for (uint32_t i = 0; i < count; i += 6)
                draw(s, clut, v + i, 6);
            return;
        }
    }
    // The ray pass's world triangles, reconstructed from this frame's world draws (as on D3D11, d3d11_draw.cpp). The
    // native preview viewport replaces the scene camera during the water scope; its draws stay in world screen space.
    const SceneGuideCamera& surfaceSource = nativeWaterPass && !(sceneCamera.flags & SceneGuideCamera::World) &&
                                                    (waterWorldCamera.flags & SceneGuideCamera::World)
                                                ? waterWorldCamera
                                                : sceneCamera;
    if ((modernWater || rayShadows) && (engineWorldOpen || pendingTemporalEnd) &&
        surface::surfaceCameraAccepted(surfaceSource, pendingTemporalEnd) && rt->w >= surfaceSource.frameWidth &&
        rt->h >= surfaceSource.frameHeight && (nativeWaterPass || (zte && ztst >= 2)) &&
        !(s.flags & (DF_SPRITE | DF_LINE | DF_POINT)) && count % 3 == 0) {
        if (surfaceBuilder->needsConfigure(surfaceSource.serial, s.xyoffset))
            surfaceBuilder->stageCamera(pendingTemporalEnd ? rt->taaCamera : surfaceSource, s.xyoffset);
        const bool waterState =
            nativeWaterPass || (residentScene.waterCount && zmsk && haveTex && !tb.isRT && (s.flags & DF_ABE));
        bool constantAlphaPass = false;
        const uint32_t alphaTest = uint32_t(bitsOf(s.test, 1, 3)), alphaRef = uint32_t(bitsOf(s.test, 4, 8));
        float vertexAlpha = 255;
        for (uint32_t i = 0; i < count; ++i)
            vertexAlpha = std::min(vertexAlpha, v[i].a);
        const bool textureAlpha = haveTex && bitsOf(s.tex0, 34, 1) != 0;
        if ((!textureAlpha || (bitsOf(s.tex0, 35, 2) == 0 && vertexAlpha >= 128)) &&
            ((alphaTest == 5 && (textureAlpha ? tb.minimumAlpha : vertexAlpha) >= alphaRef) ||
             (alphaTest == 6 && (textureAlpha ? tb.minimumAlpha : vertexAlpha) > alphaRef)))
            constantAlphaPass = true;
        // Only a fully replacing GS blend may occlude rays.
        const bool opaqueBlend =
            !(s.flags & DF_ABE) || (s.alpha == 0x44 && vertexAlpha >= 128 &&
                                    (!textureAlpha || (bitsOf(s.tex0, 35, 2) == 0 && tb.minimumAlpha >= 128)));
        const bool opaqueEligible = !zmsk && opaqueBlend && (!(s.test & 1) || alphaTest == 1 || constantAlphaPass);
        const bool materialEligible =
            haveTex && !tb.isRT && (!(s.flags & DF_ABE) || s.alpha == 0x44) && (!(s.test & 1) || alphaTest != 0);
        const bool eligible = opaqueEligible || materialEligible || waterState;
        if (eligible || rt->surfaceSubmittedEpoch != motionEpoch) {
            rt->surfaceSubmittedEpoch = motionEpoch;
            auto job = surfaceBuilder->makeJob(&rt->surface, motionEpoch);
            auto& d = job.params;
            d.waterState = waterState;
            d.nativeWaterPass = nativeWaterPass;
            d.opaqueEligible = opaqueEligible;
            d.materialEligible = materialEligible;
            d.transparent = (s.flags & DF_ABE) != 0 && !opaqueBlend;
            d.textureAlpha = textureAlpha;
            d.fst = (s.flags & DF_FST) != 0;
            d.alphaTest = (s.test & 1) ? alphaTest : 1;
            d.alphaReference = float(alphaRef);
            d.textureFunction = uint32_t(bitsOf(s.tex0, 35, 2));
            if (haveTex && !tb.isRT)
                d.texture = tb.reflectionTexture;
            d.texWidth = float(1u << bitsOf(s.tex0, 26, 4));
            d.texHeight = float(1u << bitsOf(s.tex0, 30, 4));
            surfaceBuilder->submit(std::move(job), v, eligible ? count : 0);
        }
    }
    // Every draw is full resolution (the temporal AA world only adds a sub-pixel jitter).
    const float dsx = (float)scale, dsy = (float)scale; // render pixels per native pixel of this draw
    if (engineWorldOpen &&
        (sceneCamera.flags & (SceneGuideCamera::Valid | SceneGuideCamera::World)) ==
            (SceneGuideCamera::Valid | SceneGuideCamera::World) &&
        rt->w >= sceneCamera.frameWidth && rt->h >= sceneCamera.frameHeight && zte && ztst >= 2 &&
        !(s.flags & DF_SPRITE))
        rt->worldEpoch = motionEpoch;
    bool worldDraw = false; // a jittered draw of the open temporal AA world (wgpu_temporal.cpp)
    if (upMode || modernWater || rayShadows) {
        constexpr uint32_t kWorldCamera =
            SceneGuideCamera::Valid | SceneGuideCamera::World | SceneGuideCamera::HaveViewport;
        const bool fullFrame = !neuralWorldClosed && (sceneCamera.flags & kWorldCamera) == kWorldCamera &&
                               sceneCamera.frameWidth >= 480 && sceneCamera.frameHeight >= 320 &&
                               sx1 + 1 >= (int)sceneCamera.frameWidth && sy1 + 1 >= (int)sceneCamera.frameHeight &&
                               rt->w >= sceneCamera.frameWidth && rt->h >= sceneCamera.frameHeight;
        if (rt->temporalEpoch != motionEpoch)
            rt->neuralState = 0;
        // The engine EndWorld packet resolves the world before the native HUD.
        if (!rt->neuralState && fullFrame && zte && ztst >= 2 && !(s.flags & DF_SPRITE))
            enterTemporalWorld(*rt, zbp, zmax, s.xyoffset);
        worldDraw = rt->neuralState == 1 && rt->temporalSR;
    }
    if (lodStat && haveTex && !tb.isRT)
        accumulateLod(s, v, count);
    if (logThis && haveTex && !tb.isRT && runtimeFlags().rtList)
        for (auto& r : rts)
            std::fprintf(stderr, "    rt fbp=%u(blk %x) fbw=%u cls=%u psm=%x %ux%u dirty=%d dy=%u..%u lastUse=%llu\n",
                         r->fbp, r->base(), r->fbw, r->cls, r->psm, r->w, r->h, (int)r->dirty, r->dy0, r->dy1,
                         (unsigned long long)r->lastUse);

    // --- blend plan
    const bool abe = s.flags & DF_ABE;
    const uint32_t A = (uint32_t)bitsOf(s.alpha, 0, 2), B = (uint32_t)bitsOf(s.alpha, 2, 2),
                   C = (uint32_t)bitsOf(s.alpha, 4, 2), D = (uint32_t)bitsOf(s.alpha, 6, 2),
                   FIX = (uint32_t)bitsOf(s.alpha, 32, 8);
    const bool is16 = rt->cls == 1;
    uint32_t maskBytes = fbmsk; // RGBA byte mask in target space
    if (is16) {
        const uint32_t r = ((fbmsk & 31) << 3) | ((fbmsk & 31) ? 7 : 0);
        const uint32_t g = (((fbmsk >> 5) & 31) << 3) | (((fbmsk >> 5) & 31) ? 7 : 0);
        const uint32_t b = (((fbmsk >> 10) & 31) << 3) | (((fbmsk >> 10) & 31) ? 7 : 0);
        const uint32_t a = (fbmsk & 0x8000) ? 0xFFu : 0u;
        maskBytes = r | (g << 8) | (b << 16) | (a << 24);
        // only fully-masked 5-bit channels count as byte-masked
        auto full = [&](uint32_t sh) {
            return ((fbmsk >> sh) & 31) == 31 ? 0xFFu : (((fbmsk >> sh) & 31) ? 0x7Fu : 0u);
        };
        maskBytes = full(0) | (full(5) << 8) | (full(10) << 16) | (a << 24);
    }
    bool partial = false;
    uint8_t writeMask = 0xF;
    for (int c = 0; c < 4; ++c) {
        const uint32_t m = (maskBytes >> (8 * c)) & 0xFF;
        if (m == 0xFF)
            writeMask &= ~(1u << c);
        else if (m != 0)
            partial = true;
    }
    if (fpsm == PSM_CT24)
        writeMask &= 0x7;
    inventoryHudFrame.observe(frame, sceneCamera.guiScreen, (sceneCamera.flags & SceneGuideCamera::CentredPage) != 0);
    // Alpha-only DATE masks occupy the same page coordinates as RGB.
    // A depth-only clear has no framebuffer writes and stays native.
    const bool inventoryPageDraw = inventoryHudFrame.applies(engineWorldOpen, rt->w) && (writeMask & 0xf);
    // The native inventory submits black glyph shadows two native pixels
    // below/right of their foreground. With the sharper host font this is
    // an oversized opaque duplicate. Keep a one-pixel, translucent shadow.
    // Explicit font metadata excludes black panel art, masks and world draws.
    const bool inventoryFontShadow = inventoryPageDraw && haveTex && tb.fontAtlas && abe && (writeMask & 7);
    if (logThis)
        std::fprintf(stderr, "    ui: screen=%d world=%d inventory=%d bbox=%g,%g..%g,%g scissor=%d,%d..%d,%d\n",
                     sceneCamera.guiScreen, (int)engineWorldOpen, (int)inventoryPageDraw, minx, miny, maxx, maxy, sx0,
                     sy0, sx1, sy1);
    // Current left-anchored policy returns no margins. Retain the guarded
    // canvas path for layouts with margins; never clear the live preview.
    if (inventoryPageDraw && (writeMask & 0x7))
        clearInventoryCanvas(*rt);
    if (!engineWorldOpen && nativeMenu && haveTex && tb.menuArtwork && rt->w >= 480 && (writeMask & 7)) {
        clearInventoryCanvas(*rt, true);
        if (wideReplaceFrame == frame && wideReplaceRT == rt)
            return; // a wide image drew the whole picture
    }
    const bool date = (s.test >> 14) & 1;
    const bool pabe = s.flags & DF_PABE;
    bool useHw = !partial && !date && progHw; // fixed-function blend needs dual-source blending
    rhi::BlendOp bop = rhi::BlendOp::Add;
    rhi::BlendFactor sf = rhi::BlendFactor::One, df = rhi::BlendFactor::Zero;
    bool bEnable = false;
    if (abe) {
        const bool cok = (C != 1);
        const rhi::BlendFactor Cf = rhi::BlendFactor::Src1Alpha, iCf = rhi::BlendFactor::OneMinusSrc1Alpha;
        bool mapped = true;
        if (A == 0 && B == 1 && D == 1) {
            sf = Cf;
            df = iCf;
        } else if (A == 0 && B == 2 && D == 1) {
            sf = Cf;
            df = rhi::BlendFactor::One;
        } else if (A == 1 && B == 0 && D == 0) {
            sf = iCf;
            df = Cf;
        } else if (A == 0 && B == 1 && D == 2) {
            bop = rhi::BlendOp::Subtract;
            sf = Cf;
            df = Cf;
        } else if (A == 2 && B == 0 && D == 1) {
            bop = rhi::BlendOp::ReverseSubtract;
            sf = Cf;
            df = rhi::BlendFactor::One;
        } else if (A == 1 && B == 2 && D == 0) {
            sf = rhi::BlendFactor::One;
            df = Cf;
        } else if (A == 0 && B == 2 && D == 2) {
            sf = Cf;
            df = rhi::BlendFactor::Zero;
        } else if (A == 1 && B == 2 && D == 2) {
            sf = rhi::BlendFactor::Zero;
            df = Cf;
        } else
            mapped = false;
        // With COLCLAMP = 0 the GS wraps the blend result modulo 256. For the two lerp forms (Cs-Cd)*As+Cd and
        // (Cd-Cs)*As+Cs the result stays inside [min(Cs,Cd), max(Cs,Cd)] whenever As <= 0x80 (always the case for
        // vertex/texture alpha in this game), so the fixed-function blender is exact there and, unlike the shader
        // path with its pre-draw destination copy, handles overlapping primitives of one draw correctly. The game
        // never writes COLCLAMP (power-on value 0), so without this nearly every alpha blended draw took the slow
        // path.
        const bool lerpForm = (A == 0 && B == 1 && D == 1) || (A == 1 && B == 0 && D == 0);
        if (!cok || !mapped || pabe || (!(s.flags & DF_COLCLAMP) && !lerpForm))
            useHw = false;
        bEnable = useHw;
    }
    const bool needDest = !useHw;

    // --- constants
    PSConsts k{};
    k.rt[0] = (float)rt->w;
    k.rt[1] = (float)rt->h;
    k.rt[2] = zmax;
    k.rt[3] = dsx;
    uint32_t fl = 0;
    if (tme && haveTex)
        fl |= PF_TME;
    if (s.flags & DF_FGE)
        fl |= PF_FGE;
    if (s.flags & DF_FST)
        fl |= PF_FST;
    if (bitsOf(s.tex0, 34, 1))
        fl |= PF_TCC;
    fl |= (uint32_t)bitsOf(s.tex0, 35, 2) << 4;
    const bool ate = (s.test & 1);
    const uint32_t atst = (uint32_t)bitsOf(s.test, 1, 3), afail = (uint32_t)bitsOf(s.test, 12, 2);
    if (ate)
        fl |= PF_ATE;
    fl |= atst << 7;
    fl |= afail << 10;
    if (date)
        fl |= PF_DATE;
    if ((s.test >> 15) & 1)
        fl |= PF_DATM;
    if (abe)
        fl |= PF_ABE;
    if (pabe)
        fl |= PF_PABE;
    if (s.flags & DF_DTHE)
        fl |= PF_DTHE;
    if (s.flags & DF_COLCLAMP)
        fl |= PF_CCLAMP;
    if (s.fba)
        fl |= PF_FBA;
    if (is16)
        fl |= PF_FB16;
    if (haveTex && tb.isRT)
        fl |= PF_TEXRT;
    k.a[0] = fl;
    k.a[1] = (uint32_t)bitsOf(s.test, 4, 8);
    k.a[2] = A | (B << 2) | (C << 4) | (D << 6);
    k.a[3] = FIX;
    k.b[0] = needDest ? maskBytes : 0;
    k.b[1] = 1u << std::min<uint64_t>(bitsOf(s.tex0, 26, 4), 10);
    k.b[2] = 1u << std::min<uint64_t>(bitsOf(s.tex0, 30, 4), 10);
    k.b[3] = (uint32_t)bitsOf(s.clamp, 0, 2) | ((uint32_t)bitsOf(s.clamp, 2, 2) << 2);
    k.c[0] = (uint32_t)bitsOf(s.clamp, 4, 10);
    k.c[1] = (uint32_t)bitsOf(s.clamp, 14, 10);
    k.c[2] = (uint32_t)bitsOf(s.clamp, 24, 10);
    k.c[3] = (uint32_t)bitsOf(s.clamp, 34, 10);
    k.fog[0] = (float)(s.fogcol & 0xFF);
    k.fog[1] = (float)((s.fogcol >> 8) & 0xFF);
    k.fog[2] = (float)((s.fogcol >> 16) & 0xFF);
    k.fog[3] = (float)(s.texa & 0xFF);
    const uint32_t lcm = (uint32_t)bitsOf(s.tex1, 0, 1), mxl = (uint32_t)bitsOf(s.tex1, 2, 3),
                   mmag = (uint32_t)bitsOf(s.tex1, 5, 1), mmin = (uint32_t)bitsOf(s.tex1, 6, 3),
                   L = (uint32_t)bitsOf(s.tex1, 19, 2);
    int32_t kk = (int32_t)bitsOf(s.tex1, 32, 12);
    if (kk & 0x800)
        kk -= 0x1000;
    k.d[0] = (haveTex && !tb.isRT ? tb.mxl : 0) | (lcm << 4) | (mmag << 5) | (mmin << 6) | (L << 9);
    k.d[2] = (uint32_t)(s.dimx & 0xFFFFFFFFull);
    k.d[3] = (uint32_t)(s.dimx >> 32);
    k.e[0] = (float)kk / 16.0f;
    k.e[1] = (float)cfg.textureFilter;
    if (haveTex && tb.isRT) {
        const uint32_t tpsm = (uint32_t)bitsOf(s.tex0, 20, 6);
        k.e[2] = (tpsm == PSM_CT24) ? 1.0f : ((tpsm == PSM_CT16 || tpsm == PSM_CT16S) ? 2.0f : 0.0f);
        k.e[3] = (float)((s.texa >> 32) & 0xFF);
        if ((s.texa >> 15) & 1)
            k.d[0] |= 1u << 12;
    }
    (void)mxl;
    k.f[0] = (haveTex && !tb.isRT) ? (float)tb.factor : 1.0f;
    k.f[1] = mipBias;
    k.f[2] = (float)aniso;
    k.f[3] = std::log2(k.f[0]);
    k.g[0] = userMipBias;
    if (worldDraw) { // sub-pixel jitter in NDC (render pixels -> NDC; the viewport spans the whole target)
        k.g[1] = 2.0f * rt->wjx / ((float)rt->w * dsx);
        k.g[2] = -2.0f * rt->wjy / ((float)rt->h * dsy);
        k.h[0] = rt->wjx;
        k.h[1] = rt->wjy;
    }
    k.h[2] = 16384;
    // Single native UI sprite, point sampling only. Work from original
    // endpoints before HUD transforms; never infer borders from colours.
    if (spriteFootprint && !engineWorldOpen && scale > 1 && haveTex && !tb.isRT && rt->w >= 480 &&
        (s.flags & DF_SPRITE) && count == 6 && mxl == 0 && mmag == 0 && mmin == 0 && cfg.textureFilter != 2) {
        const auto b =
            sprite_footprint::bounds(v, count, (s.flags & DF_FST) != 0, (float)k.b[1], (float)k.b[2], tb.factor);
        if (b.valid) {
            k.a[0] |= 1u << 24;
            k.spriteBounds[0] = b.u0;
            k.spriteBounds[1] = b.v0;
            k.spriteBounds[2] = b.u1;
            k.spriteBounds[3] = b.v1;
        }
    }
    if (haveTex && tb.isRT) {
        k.src[0] = tb.ox;
        k.src[1] = tb.oy;
        k.src[2] = 1.0f / (float)tb.rt->w;
        k.src[3] = 1.0f / (float)tb.rt->h;
    }

    if (logThis)
        std::fprintf(stderr,
                     "    consts: haveTex=%d isRT=%d useHw=%d needDest=%d bEnable=%d fl=%08x aref/ate=%u abcd=%02x "
                     "fix=%u maskB=%08x tex=%ux%u wm=%u clampc=%u,%u,%u,%u d0=%08x e=(%.2f,%.1f) fba=%u fbmsk=%08x "
                     "src=(%.1f,%.1f,%g,%g) rtdim=%ux%u\n",
                     (int)haveTex, (int)(haveTex && tb.isRT), (int)useHw, (int)needDest, (int)bEnable, k.a[0], k.a[1],
                     k.a[2], k.a[3], k.b[0], k.b[1], k.b[2], k.b[3], k.c[0], k.c[1], k.c[2], k.c[3], k.d[0], k.e[0],
                     k.e[1], (unsigned)s.fba, fbmsk, k.src[0], k.src[1], k.src[2], k.src[3], rt->w, rt->h);
    // --- vertices (copied: the HUD / menu maps below rewrite positions; the device copies them at draw time)
    std::optional<CpuProfile::Scope> uploadProfile(std::in_place, cpuProfile, CpuProfile::DrawVertexUpload);
    std::vector<DrawVertex> mapped(v, v + count);
    if (inventoryFontShadow) {
        // Native batching can combine colored foregrounds and black
        // shadows in the same draw. Classify complete glyph triangles.
        auto* glyph = mapped.data();
        for (uint32_t i = 0; i + 2 < count; i += 3) {
            bool shadow = true;
            for (uint32_t j = i; j < i + 3; ++j) {
                const float glyphY = (s.flags & DF_FST) ? v[j].t : v[j].t * k.b[2];
                // Both identified Drakan fonts keep generated ASCII in
                // the upper half of their stored atlas. Keyboard caps and
                // other retained pictograms below it keep native shadows.
                if (v[j].r != 0 || v[j].g != 0 || v[j].b != 0 || !hud::nativeUiQ(v[j].q) || glyphY < k.b[2] * .5f)
                    shadow = false;
            }
            if (shadow)
                for (uint32_t j = i; j < i + 3; ++j) {
                    glyph[j].x -= 1;
                    glyph[j].y -= 1;
                    glyph[j].a *= 0.5f;
                }
        }
    }
    {
        // Menu highlight (128x128 fire heat map T8 + glyph overlay CT16, additive, ALPHA 0x48): the game sizes the
        // sprite to the measured text width, but the rendered glyphs reach about one texel further right (the heat
        // map has content up to column 98, the sprite samples 2.5 .. 97.5), so the last letter of a highlighted row
        // ("New Game") was cut. Extend the right edge by 2 texels (position and S together, 1:1 stays 1:1).
        const uint32_t hpsm = (uint32_t)bitsOf(s.tex0, 20, 6);
        if (haveTex && (s.flags & DF_SPRITE) && (s.flags & DF_ABE) && (s.alpha & 0xFF) == 0x48 &&
            bitsOf(s.tex0, 26, 4) == 7 && bitsOf(s.tex0, 30, 4) == 7 && (hpsm == PSM_T8 || hpsm == PSM_CT16) &&
            (s.flags & DF_FST) == 0 && count % 3 == 0) {
            DrawVertex* mv = mapped.data();
            float maxx = mv[0].x;
            for (uint32_t i = 1; i < count; ++i)
                maxx = std::max(maxx, mv[i].x);
            for (uint32_t i = 0; i < count; ++i)
                if (mv[i].x >= maxx - 0.01f && mv[i].q > 0.0f) {
                    mv[i].x += 2.0f;
                    mv[i].s += 2.0f / 128.0f * mv[i].q;
                }
        }
    }
    // Engine world boundaries are ordered with GIF submissions, including
    // when temporal features are off. Outside an explicit menu, reject fogged triangles:
    // small sky triangles use the same ALWAYS/ZMSK state as menu quads.
    bool perspectiveTriangle = false;
    if (!(s.flags & (DF_SPRITE | DF_FST)) && haveTex)
        for (uint32_t i = 0; i < count; ++i)
            if (!hud::nativeUiQ(v[i].q)) {
                perspectiveTriangle = true;
                break;
            }
    // Late transparent world draws (including the equipped weapon's
    // glint) can arrive after EndWorld. Phase alone does not make their
    // perspective triangles HUD elements, even over a screen corner.
    float minFog = 255.0f;
    for (uint32_t i = 0; i < count; ++i)
        minFog = std::min(minFog, v[i].fog);
    const bool hudWorldTriangle =
        !(s.flags & DF_SPRITE) &&
        (perspectiveTriangle || ((s.flags & DF_FGE) && !hud::screenTextTriangles(haveTex && !(s.flags & DF_FST),
                                                                                 perspectiveTriangle, minFog)));
    const auto inventoryMap = hud::inventoryMap(rt->w, hudScale, inventoryHudFrame.centred);
    // The map view's world shade stays unmapped: it covers the whole output, not the centred 4:3 window.
    const bool inventoryShade =
        inventoryPageDraw &&
        hud::inventoryViewShade(inventoryHudFrame.centred, !haveTex || (k.b[1] <= 32 && k.b[2] <= 32), minx, miny, maxx,
                                maxy, (float)rt->w, (float)std::min(rt->h, 512u));
    const auto menuMap = hud::menuMap(rt->w, hudScale);
    // Corner pieces may be RT-backed triangle lists with depth state unlike
    // their rings, including a retained FGE bit. Once the engine has closed
    // the world, classify each triangle before legacy UI state exclusions.
    bool menuPageDraw = hud::menuSubmission(engineWorldOpen, hud::nativeMenuDraw(nativeMenu, sceneCamera.guiScreen),
                                            rt->w, rt->h, (writeMask & 0x7) != 0, perspectiveTriangle);
    if (menuPageDraw && hud::pauseHudDraw(hud::pauseScreen(sceneCamera.guiScreen, nativeMenu, nativeMenuScreen), minx,
                                          miny, maxx, maxy, (float)rt->w, (float)std::min(rt->h, 512u)))
        menuPageDraw = false;
    const bool cornerHudDraw = !menuPageDraw && !perspectiveTriangle &&
                               hud::gameplayCornerSubmission(engineWorldOpen, inventoryPageDraw, rt->w, rt->h);
    const float cornerFrameW = (float)rt->w;
    const float cornerFrameH = (float)std::min(rt->h, 512u);
    const bool legacyHudDraw =
        !hudWorldTriangle && ((s.flags & DF_SPRITE) || (zmsk && zte && ztst == 1)) && !(zte && ztst >= 2);
    float hudClipX0 = (float)sx0, hudClipY0 = (float)sy0;
    float hudClipX1 = (float)(sx1 + 1), hudClipY1 = (float)(sy1 + 1);
    uint32_t cornerLeft = 0, cornerRight = 0, cornerUnmapped = 0;
    if (!engineWorldOpen &&
        (hudScale < 0.999f || std::fabs(hudSize - 1.0f) > 0.001f || inventoryFontShadow ||
         sceneCamera.guiScreen == 14) &&
        ((writeMask & 0x7) || inventoryPageDraw) &&
        (inventoryPageDraw || menuPageDraw || cornerHudDraw || legacyHudDraw)) {
        // HUD de-stretch / resize (anamorphic widescreen, hud_scale): a UI sprite drawn into the full display-sized
        // frame. Sprites are grouped into clusters (overlapping / touching UI sprites of the previous frame, e.g.
        // an orb's fill, ring and highlights), and a cluster is anchored by its bounding box: x' = a + (x - a) * k
        // * size, y' = ay + (y - ay) * size, a = nearest screen edge / centre. Every sprite of a cluster uses the
        // same map, so the pieces stay together and edge anchored elements stay on screen.
        const float frameW = (float)(sx1 - sx0 + 1), frameH = (float)(sy1 - sy0 + 1);
        if (inventoryPageDraw || menuPageDraw || cornerHudDraw ||
            (frameW >= 480.0f && frameH >= 320.0f && rt->w >= 480)) {
            const float fx0 = (float)sx0, fy0 = (float)sy0;
            DrawVertex* dv = mapped.data();
            auto anchor = [](float lo, float hi, float f0, float F) {
                const float t = (0.5f * (lo + hi) - f0) / F;
                return t < 0.4f ? f0 : (t > 0.6f ? f0 + F : f0 + 0.5f * F);
            };
            // one range = one sprite (6 vertices) of a batch, or the whole draw for triangle quads
            auto doRange = [&](uint32_t b, uint32_t e) {
                if (nativeSubtitleOpen) {
                    const auto map = hud::subtitleMap(cornerFrameW, cornerFrameH, hudScale, hudSize);
                    float lo = 1e30f, hi = -1e30f, top = 1e30f, bottom = -1e30f;
                    for (uint32_t i = b; i < e; ++i) {
                        lo = std::min(lo, v[i].x);
                        hi = std::max(hi, v[i].x);
                        top = std::min(top, v[i].y);
                        bottom = std::max(bottom, v[i].y);
                    }
                    // Native solid-color UI uses the 32x32 white tile,
                    // rather than an untextured GS rectangle.
                    const bool solid = !haveTex || (k.b[1] <= 32 && k.b[2] <= 32);
                    const bool band = solid && hi - lo >= .85f * cornerFrameW && top >= .6f * cornerFrameH &&
                                      bottom - top < .4f * cornerFrameH;
                    if (band)
                        nativeSubtitleBounds.set(top, bottom);
                    // Only native font cells inside the observed subtitle
                    // band may follow its delayed DMA tail. Orb artwork
                    // and subsequent HUD packets keep their own anchors.
                    const bool glyph = haveTex && nativeSubtitleBounds.glyph(k.b[1], k.b[2], top, bottom);
                    if (band || glyph) {
                        for (uint32_t i = b; i < e; ++i) {
                            if (hi - lo < .85f * cornerFrameW)
                                dv[i].x = map.x(dv[i].x);
                            dv[i].y = map.y(dv[i].y);
                        }
                        // Clip and destination reads follow the same map.
                        hudClipX0 = std::min(hudClipX0, map.x((float)sx0));
                        hudClipX1 = std::max(hudClipX1, map.x((float)(sx1 + 1)));
                        hudClipY0 = std::min(hudClipY0, map.y((float)sy0));
                        hudClipY1 = std::max(hudClipY1, map.y((float)(sy1 + 1)));
                        st.hudSprites++;
                        return;
                    }
                }
                // Every post-world inventory component shares this exact
                // native-coordinate map: RT fills, rings, tiled panels,
                // clipped text, and preview geometry. No source/size
                // heuristic proves that a draw is a world snapshot, so
                // none is exempted here. Keep page Y and size native.
                if (inventoryPageDraw) {
                    for (uint32_t i = b; i < e && !inventoryShade; ++i) {
                        dv[i].x = inventoryMap.x(dv[i].x);
                    }
                    st.hudSprites++;
                    return;
                }
                float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
                for (uint32_t i = b; i < e; ++i) {
                    x0 = std::min(x0, v[i].x);
                    x1 = std::max(x1, v[i].x);
                    y0 = std::min(y0, v[i].y);
                    y1 = std::max(y1, v[i].y);
                }
                if (!menuPageDraw && legacyHudDraw && hud::tutorialBand(y0, y1, cornerFrameH) &&
                    (hud::tutorialBandDraw(minx, maxx, cornerFrameW) || tutorialBandRows.covers(y1))) {
                    tutorialBandRows.note(x0, x1, y1, cornerFrameW);
                    const auto map = hud::tutorialMap(cornerFrameW, hudScale, hudSize);
                    for (uint32_t i = b; i < e; ++i) {
                        // Keep the full-width background band full width.
                        if (x1 - x0 < 0.85f * cornerFrameW)
                            dv[i].x = map.x(dv[i].x);
                        dv[i].y = map.y(dv[i].y);
                    }
                    st.hudSprites++;
                    return;
                }
                const auto corner =
                    cornerHudDraw ? hud::gameplayCorner(x0, y0, x1, y1, cornerFrameW, cornerFrameH) : hud::Corner::None;
                if (corner != hud::Corner::None) {
                    (corner == hud::Corner::Left ? cornerLeft : cornerRight) += (e - b) / 3;
                    const auto map = hud::cornerMap(corner, cornerFrameW, cornerFrameH, hudScale, hudSize);
                    for (uint32_t i = b; i < e; ++i) {
                        dv[i].x = map.x(dv[i].x);
                        dv[i].y = map.y(dv[i].y);
                    }
                    // One draw can contain both corners. Conservatively
                    // cover both mapped clip rectangles for destination reads.
                    hudClipX0 = std::min(hudClipX0, map.x((float)sx0));
                    hudClipY0 = std::min(hudClipY0, map.y((float)sy0));
                    hudClipX1 = std::max(hudClipX1, map.x((float)(sx1 + 1)));
                    hudClipY1 = std::max(hudClipY1, map.y((float)(sy1 + 1)));
                    st.hudSprites++;
                    return;
                }
                if (cornerHudDraw)
                    cornerUnmapped += (e - b) / 3;
                const float w = x1 - x0, h = y1 - y0;
                const bool tile128 =
                    w >= 127.0f && h >= 127.0f && std::fmod(x0 - fx0 + 0.5f, 128.0f) < 1.0f &&
                    std::fmod(y0 - fy0 + 0.5f, 128.0f) < 1.0f && std::fabs(std::fmod(w + 0.5f, 128.0f)) < 1.0f &&
                    std::fabs(std::fmod(h + 0.5f, 128.0f)) < 1.0f; // screen-grab tiles redrawn as backdrop
                if (menuPageDraw) {
                    // Menu foreground candidates share one centered map;
                    // never inherit gameplay cluster anchors or HUD size.
                    // Captured world tiles and full-page backdrops fill the
                    // selected aspect. The guest's UI projection stays native.
                    const bool columnBackdrop =
                        hud::menuColumnBackdrop(x0, y0, x1, y1, (float)rt->w, haveTex && (s.flags & DF_SPRITE));
                    const bool authoredArt = haveTex && tb.menuArtwork;
                    if (authoredArt || hud::menuForeground(w, (float)rt->w, tile128 || columnBackdrop)) {
                        float loadingOffset = 0;
                        if (sceneCamera.guiScreen == 14 && sceneCamera.sourcePc == 0x43d340u) {
                            std::memcpy(&loadingOffset, &sceneCamera.reserved, sizeof(loadingOffset));
                            if (!std::isfinite(loadingOffset) || std::fabs(loadingOffset) > 256)
                                loadingOffset = 0;
                        }
                        for (uint32_t i = b; i < e; ++i)
                            dv[i].x = menuMap.x(dv[i].x - loadingOffset);
                        hudClipX0 = std::min(hudClipX0, menuMap.x((float)sx0));
                        hudClipX1 = std::max(hudClipX1, menuMap.x((float)(sx1 + 1)));
                        st.hudSprites++;
                    }
                    return;
                }
                if (!legacyHudDraw)
                    return;
                // the sky dome (and similar big backdrops) is drawn with ZTST ALWAYS / ZMSK like UI quads when the
                // camera looks up: a large triangle draw is not UI (it would be squeezed into a 4:3 rectangle and
                // merge the HUD clusters into one centred 'page') a draw that samples a render target (screen grab
                // / full-frame effect) is UI only when it is small: the HP / mana orb gem is textured from memory
                // that aliases a render target while the camera looks up at the sky, and must move with its ring
                const bool rtGrab = haveTex && tb.isRT && (w > 0.3f * frameW || h > 0.3f * frameH);
                const bool bigBackdrop = rtGrab || (!(s.flags & DF_SPRITE) && w * h > 0.2f * frameW * frameH);
                if (w < 0.85f * frameW && !tile128 && !bigBackdrop) {
                    if (hudCur.size() < 600)
                        hudCur.push_back({x0, y0, x1, y1});
                    HudBox c{x0, y0, x1, y1};
                    const float mx = 0.5f * (x0 + x1), my = 0.5f * (y0 + y1);
                    float best = 1e30f;
                    for (const HudBox& bx : hudPrev)
                        if (mx >= bx.x0 && mx <= bx.x1 && my >= bx.y0 && my <= bx.y1 &&
                            (bx.x1 - bx.x0) * (bx.y1 - bx.y0) < best) {
                            best = (bx.x1 - bx.x0) * (bx.y1 - bx.y0);
                            c = bx;
                        }
                    const bool big = (c.x1 - c.x0) * (c.y1 - c.y0) > 0.07f * frameW * frameH;
                    const float ax = big ? fx0 + 0.5f * frameW : hud::clusterAnchorX(c.x0, c.x1, fx0, frameW),
                                ay = anchor(c.y0, c.y1, fy0, frameH);
                    // menus / pages / popups (big clusters, centred) keep their size (only the aspect squeeze
                    // applies): the menu mouse maps through that squeeze
                    const float sz = big ? 1.0f : hudSize;
                    const float kx = hudScale * sz, ky = sz;
                    for (uint32_t i = b; i < e; ++i) {
                        dv[i].x = ax + (dv[i].x - ax) * kx;
                        dv[i].y = ay + (dv[i].y - ay) * ky;
                    }
                    st.hudSprites++;
                } else if (!tile128 && !rtGrab && h < 0.4f * frameH && w >= 0.85f * frameW &&
                           std::fabs(hudSize - 1.0f) > 0.001f) {
                    // full-width band (subtitle bar): resized vertically about the nearest vertical edge
                    const float ay = anchor(y0, y1, fy0, frameH);
                    for (uint32_t i = b; i < e; ++i)
                        dv[i].y = ay + (dv[i].y - ay) * hudSize;
                }
            };
            if ((s.flags & DF_SPRITE) && count % 6 == 0)
                for (uint32_t b = 0; b < count; b += 6)
                    doRange(b, b + 6);
            else if ((menuPageDraw ||
                      (cornerHudDraw && hud::splitCornerBatch(minx, miny, maxx, maxy, cornerFrameW, cornerFrameH))) &&
                     count % 3 == 0)
                for (uint32_t b = 0; b < count; b += 3)
                    doRange(b, b + 3);
            else
                doRange(0, count);

            // Shader blending/DATE reads destination pixels at the moved
            // raster positions. Retain conservative original bounds too,
            // but include every transformed vertex in snapshot/dirty rows.
            const bool pageMapped = inventoryPageDraw && !inventoryShade;
            const int hudClipLeft = (int)std::floor(pageMapped ? inventoryMap.x((float)sx0) : hudClipX0);
            const int hudClipRight = (int)std::ceil(pageMapped ? inventoryMap.x((float)(sx1 + 1)) : hudClipX1) - 1;
            for (uint32_t i = 0; i < count; ++i) {
                cx0 = std::min(cx0, std::max(std::max(hudClipLeft, 0), (int)std::floor(dv[i].x)));
                cy0 = std::min(cy0, std::max(std::max((int)std::floor(hudClipY0), 0), (int)std::floor(dv[i].y)));
                cx1 = std::max(cx1, std::min(std::min(hudClipRight, (int)rt->w - 1), (int)std::ceil(dv[i].x)));
                cy1 = std::max(
                    cy1, std::min(std::min((int)std::ceil(hudClipY1) - 1, (int)rt->h - 1), (int)std::ceil(dv[i].y)));
            }
        }
    }
    // Opt-in and bounded per renderer. Counts partition the submitted
    // triangles; correlate draw/flags/source/bounds to verify every ring
    // piece rather than inferring success from one mapped fill.
    if (runtimeFlags().hudCornerLog && cornerHudDraw && (writeMask & 0x7) &&
        (hudScale < 0.999f || std::fabs(hudSize - 1.0f) > 0.001f) &&
        (cornerLeft || cornerRight || (s.flags & DF_FGE)) && hudCornerLogLines < 128 &&
        frame >= runtimeFlags().hudCornerFrom) {
        ++hudCornerLogLines;
        std::fprintf(stderr,
                     "[hud-corner] f=%llu draw=%lld flags=%03x n=%u rtSource=%d world=%d page=%d target=%ux%u "
                     "native=(%g,%g)..(%g,%g) mappedL=%u mappedR=%u unmapped=%u scale=%g,%g\n",
                     (unsigned long long)frame, (long long)di, s.flags, count, (int)(haveTex && tb.isRT),
                     (int)motionWorldOpen, (int)inventoryPageDraw, rt->w, rt->h, minx, miny, maxx, maxy, cornerLeft,
                     cornerRight, cornerUnmapped, hudScale * hudSize, hudSize);
    }
    uploadProfile.reset();

    // --- dest / feedback snapshot
    CpuProfile::Scope submitProfile(cpuProfile, CpuProfile::DrawSubmit);
    rhi::Texture* destSrv = nullptr;
    rhi::Texture* texSrv = haveTex ? tb.srv : nullptr;
    const bool feedback = haveTex && tb.isRT && tb.rt == rt;
    uint32_t box[4] = {(uint32_t)std::floor(cx0 * dsx), (uint32_t)std::floor(cy0 * dsy),
                       (uint32_t)std::ceil((cx1 + 1) * dsx), (uint32_t)std::ceil((cy1 + 1) * dsy)};
    box[2] = std::min<uint32_t>(box[2], rt->w * scale);
    box[3] = std::min<uint32_t>(box[3], rt->h * scale);
    if (feedback) {
        destSrv = snapshot(*rt, nullptr);
        texSrv = destSrv;
        if (needDest)
            destSrv = rt->copy.get();
    } else {
        if (haveTex && tb.isRT)
            texSrv = tb.rt->tex.get();
        if (needDest)
            destSrv = snapshot(*rt, box);
    }
    if (haveTex && tb.isRT && !feedback)
        tb.rt->lastUse = frame;
    if (needDest)
        st.shaderBlendDraws++;
    else
        st.hwBlendDraws++;

    // --- pipeline
    rhi::Compare zf = rhi::Compare::Always;
    if (zte) {
        static const rhi::Compare fm[4] = {rhi::Compare::Never, rhi::Compare::Always, rhi::Compare::GreaterEqual,
                                           rhi::Compare::Greater};
        zf = fm[ztst];
    }

    auto setup = [&](uint32_t passMode, uint8_t wm, bool zw) {
        k.d[1] = passMode;
        gpu->setUniforms(0, &k, sizeof(k));
        rhi::BlendState blend;
        blend.enable = alpha::blendForPass(bEnable, wm);
        blend.op = bop;
        blend.src = sf;
        blend.dst = df;
        blend.writeMask = wm;
        gpu->setBlend(blend);
        gpu->setDepth({true, zf, zw});
    };

    gpu->setTargets(rt->tex.get(), db->tex.get(), 0);
    gpu->setProgram(useHw ? progHw.get() : progSb.get());
    gpu->setTopology(rhi::Topology::TriangleList);
    gpu->setTexture(0, texSrv ? texSrv : dummy.get());
    if (!useHw)
        gpu->setTexture(1, destSrv ? destSrv : dummy.get());
    gpu->setViewport(0, 0, (float)rt->w * dsx, (float)rt->h * dsy);
    int32_t sr[4] = {(int32_t)std::floor(sx0 * dsx), (int32_t)std::floor(sy0 * dsy),
                     (int32_t)std::ceil((sx1 + 1) * dsx), (int32_t)std::ceil((sy1 + 1) * dsy)};
    if (inventoryPageDraw && !inventoryShade) {
        sr[0] = std::max<int32_t>(0, (int32_t)std::floor(inventoryMap.x((float)sx0) * dsx));
        sr[2] = std::max<int32_t>(0, (int32_t)std::ceil(inventoryMap.x((float)(sx1 + 1)) * dsx));
    } else if (cornerHudDraw || menuPageDraw) {
        sr[0] = std::max<int32_t>(0, (int32_t)std::floor(hudClipX0 * dsx));
        sr[1] = std::max<int32_t>(0, (int32_t)std::floor(hudClipY0 * dsy));
        sr[2] = (int32_t)std::ceil(hudClipX1 * dsx);
        sr[3] = (int32_t)std::ceil(hudClipY1 * dsy);
    }
    sr[2] = std::min<int32_t>(sr[2], rt->w * scale);
    sr[3] = std::min<int32_t>(sr[3], rt->h * scale);
    gpu->setScissor({sr[0], sr[1], (uint32_t)std::max(0, sr[2] - sr[0]), (uint32_t)std::max(0, sr[3] - sr[1])});

    // pass 0
    const bool failPass = ate && afail != 0 && atst != 1;
    setup(0, writeMask, !zmsk);
    gpu->draw(mapped.data(), count);
    if (failPass) {
        const auto fail = alpha::failurePass(afail, writeMask, !zmsk);
        setup(1, fail.colorMask, fail.depthWrite);
        gpu->draw(mapped.data(), count);
    }

    st.drawCalls++;
    if (writeMask) {
        ++rt->writes;
        rt->drawFrame = frame;
        if (!rt->dirty) {
            rt->dirty = true;
            rt->dy0 = (uint32_t)cy0;
            rt->dy1 = (uint32_t)cy1 + 1;
        } else {
            rt->dy0 = std::min(rt->dy0, (uint32_t)cy0);
            rt->dy1 = std::max(rt->dy1, (uint32_t)cy1 + 1);
        }
    }
    rt->copyValid = false;
    if ((!rtDump.empty() && rtDump.count(di)) ||
        (rtDumpFrame >= 0 && (int64_t)frame >= rtDumpFrame && (int64_t)frame <= rtDumpFrameLast)) {
        // debug: GSR_RTDUMP=<draw>[,<draw>...] writes the scissor area of the target as rtdump_<draw>.png
        // (GSR_RTDUMP_DIR, default cwd) right after that draw
        Image im;
        const uint32_t w = (uint32_t)(sx1 + 1) - (uint32_t)sx0, h = (uint32_t)(sy1 + 1) - (uint32_t)sy0;
        if (readTexture(*rt->tex, rt->w * scale,
                        std::min<uint32_t>(rt->h, std::max<uint32_t>((uint32_t)(sy1 + 1), dumpRows)) * scale, im)) {
            (void)w;
            (void)h;
            const char* d = runtimeFlags().rtDumpDir.c_str();
            char name[512];
            std::snprintf(name, sizeof(name), "%s/rtdump_%lld.png", d, (long long)di);
            writePng(name, im);
            if (readTexture(*rt->tex, rt->w * scale,
                            std::min<uint32_t>(rt->h, std::max<uint32_t>((uint32_t)(sy1 + 1), dumpRows)) * scale, im,
                            true)) {
                for (size_t i = 0; i < im.rgba.size(); i += 4) {
                    im.rgba[i] = im.rgba[i + 1] = im.rgba[i + 2] = im.rgba[i + 3];
                    im.rgba[i + 3] = 255;
                }
                std::snprintf(name, sizeof(name), "%s/rtdump_%lld_alpha.png", d ? d : ".", (long long)di);
                writePng(name, im);
            }
        }
    }
}

} // namespace gsr
