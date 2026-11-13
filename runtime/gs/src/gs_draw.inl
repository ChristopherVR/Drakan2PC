// Per-draw state setup and submission (Impl::draw) and draw-level diagnostics. Shared by the hardware backends
// (gs_backend_shared.h): compiled once per backend with GSR_IMPL naming its Impl, after the backend's private header.
// Everything the GS decides about a draw is here: its classification (world, HUD, inventory, menu page, title,
// subtitles), the widescreen and HUD mapping of its vertices, blend / depth / alpha-test state, the constants block,
// the scissor, the passes, and what the target remembers afterwards.
//
// Backend interface (members of GSR_IMPL):
//   clearTarget              clear a target's colour
//   fixedFunctionBlend       the dual-source blend path is available (else every blend runs in the shader)
//   beginVertices / endVertices   a writable copy of the draw's vertices, then its hand-over to the GPU
//   RT::view / RT::copyView, snapshot   what a draw binds to sample a target, and the copy of a target region
//   bindDraw                 targets, programs, textures, viewport and scissor of the draw (samplesTarget: the texture
//                            is a render target, so no binding of an earlier draw may still hold it as a target)
//   setPass                  constants, blend and depth state of one pass (gs_pipeline_state.h)
//   drawVertices             one pass over the vertices handed to beginVertices
//   endDraw                  release what bindDraw bound
//   readTarget, readBoundTexture   debug dumps
//   kCaps.terrainDetail      prepareTerrain
//   kCaps.materialDetail     prepareMaterial
//   kCaps.motionVectors      prepareObjectMotion, drawVelocity
//   kCaps.vertexPrograms     programBatch (set by the backend around the draw() of a vertex-program batch, whose
//                            vertices are three stand-ins: vertex_program_draw.h), prepareProgramMotion,
//                            bindProgramDraw, drawProgramVertices, drawProgramVelocity
//   kCaps.temporalUpscaler   upscalePhase (the dormant per-object world detection), logTemporalDraw
//   kCaps.readbackPrefetch   prefetchReadback
//   kCaps.readbackVerify     verifyRTBlocks
#include "gs_solid_mirror.h"

#include <cstdlib>

#ifndef GSR_IMPL
#error "define GSR_IMPL (the backend's Impl) and include the backend's private header first"
#endif
#include <optional>

namespace gsr {

void GSR_IMPL::logDraw(const GsDrawState& s, const DrawVertex* v, uint32_t count) {
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
bool GSR_IMPL::oneToOneSprite(const GsDrawState& s, const DrawVertex* v, uint32_t count) {
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
void GSR_IMPL::accumulateLod(const GsDrawState& s, const DrawVertex* v, uint32_t count) {
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

void GSR_IMPL::printLodStat() {
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

namespace draw_blend {
// The fixed-function form of the GS blend ALPHA (ABE set): op, source and destination factor (the second colour
// output's alpha is the GS coefficient). False when the blend must run in the shader.
inline bool fixedForm(const GsDrawState& s, rhi::BlendOp& bop, rhi::BlendFactor& sf, rhi::BlendFactor& df) {
    const uint32_t A = (uint32_t)bitsOf(s.alpha, 0, 2), B = (uint32_t)bitsOf(s.alpha, 2, 2),
                   C = (uint32_t)bitsOf(s.alpha, 4, 2), D = (uint32_t)bitsOf(s.alpha, 6, 2);
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
    return cok && mapped && !(s.flags & DF_PABE) && ((s.flags & DF_COLCLAMP) || lerpForm);
}
} // namespace draw_blend

// [video] terrain_batching (draw_join.h): the segments of a batch whose draws differ only in their texture. Drawn as
// one draw (one classification, binding and constants upload, the texture switched between the segments in their
// order) when that gives the same picture as one draw per segment, else one draw per segment. What makes one draw
// the same: the blend runs in the fixed-function blender (it blends the primitives of a draw in their order; the
// shader blend reads a copy of the target taken before the draw), no alpha-failure pass (it would run after all the
// segments instead of after each), a depth-tested world draw (the classification does not depend on the vertices),
// and none of the per-draw users of the vertices that see a batch differently from its parts (surfaces, terrain
// layers, object motion under a scope). The texture checks follow in draw() (resolveJoined).
void GSR_IMPL::drawSegments(const DrawSegment* segments, uint32_t segmentCount, const DrawVertex* v, uint32_t count) {
    const GsDrawState& s = *segments[0].state;
    const uint32_t atst = (uint32_t)bitsOf(s.test, 1, 3), afail = (uint32_t)bitsOf(s.test, 12, 2);
    const bool failPass = (s.test & 1) && afail != 0 && atst != 1;
    rhi::BlendOp bop = rhi::BlendOp::Add;
    rhi::BlendFactor sf = rhi::BlendFactor::One, df = rhi::BlendFactor::Zero;
    // Outside the engine's world phase the HUD maps, the loading-screen backdrop and the world-tail resolve look at
    // the vertices of a draw: only when none of them can apply (no HUD resize, not the loading screen, no world
    // resolve pending). What else differs outside the world phase is decided by the state or by the texture (menu
    // and font artwork, refused in resolveJoined) or happens once a frame (the canvas clears).
    const bool phaseFree = engineWorldOpen || (!pendingTemporalEnd && sceneCamera.guiScreen != 14 &&
                                               hudScale >= 0.999f && std::fabs(hudSize - 1.0f) <= 0.001f);
    bool join = segmentCount > 1 && phaseFree && fixedFunctionBlend() && (s.frame >> 32) == 0 &&
                !((s.test >> 14) & 1) && !failPass && ((s.test >> 16) & 1) && bitsOf(s.test, 17, 2) >= 2 &&
                (!(s.flags & DF_ABE) || draw_blend::fixedForm(s, bop, sf, df)) && !modernWater && !rayShadows &&
                !rayReflections && !rayGlobalIllumination && !runtimeFlags().surfaceAudit &&
                !runtimeFlags().surfacePick && drawStop < 0 && skipA < 0;
    if constexpr (kCaps.vertexPrograms)
        join = join && !programBatch;
    if constexpr (kCaps.terrainDetail)
        join = join && !terrainLayersActive();
    if constexpr (kCaps.motionVectors)
        join = join && joinKeepsMotion();
    if (join) {
        joinSegments = segments;
        joinCount = segmentCount;
        joinRefused = false;
        draw(s, *segments[0].clut, v, count);
        joinSegments = nullptr;
        joinCount = 0;
        joinDraws.clear();
        if (!joinRefused)
            return;
        st.joinRefused++;
    }
    for (uint32_t i = 0; i < segmentCount; ++i)
        draw(*segments[i].state, *segments[i].clut, v + segments[i].first, segments[i].count);
}

// The textures of the segments of a joined draw that are not clipped away (joinKept), in order; `tb` gets the first.
// False (the draw is refused and drawSegments draws the segments one by one) when one does not resolve or would bind
// differently from the first: a render target, menu or font artwork, a material map, another upscale factor or mip
// count (the constants of the draw hold one of each).
bool GSR_IMPL::resolveJoined(TexBind& tb) {
    joinTextures.resize(joinCount);
    bool first = true;
    for (uint32_t i = 0; i < joinCount; ++i) {
        if (!joinKept[i])
            continue;
        TexBind& t = joinTextures[i];
        t = TexBind{};
        if (!resolveTexture(*joinSegments[i].state, *joinSegments[i].clut, t, false) || t.isRT || t.fontAtlas ||
            t.menuArtwork || t.titleArtwork || t.materialSrv)
            return false;
        if (first) {
            tb = t;
            first = false;
        } else if (t.factor != tb.factor || t.mxl != tb.mxl)
            return false;
    }
    return !first;
}

void GSR_IMPL::draw(const GsDrawState& s, const ClutBuf& clut, const DrawVertex* v, uint32_t count) {
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
    {
        // DRAKAN_VU_TRIGGER=<file>: create <file> once when a perspective-textured draw has a vertex behind the camera
        // (q < 0); the runtime's DRAKAN_VU_RING recorder then dumps the VU1 runs that produced it.
        static const char* trigger = std::getenv("DRAKAN_VU_TRIGGER");
        static bool fired = false;
        if (trigger && *trigger && !fired && (s.flags & DF_TME) && !(s.flags & DF_FST) && !(s.flags & DF_SPRITE)) {
            for (uint32_t i = 0; i < count; ++i)
                if (v[i].q < -0.01f) {
                    fired = true;
                    if (FILE* t = std::fopen(trigger, "wb")) {
                        std::fclose(t);
                        if (FILE* t2 = std::fopen((std::string(trigger) + ".2").c_str(), "wb"))
                            std::fclose(t2); // second trigger for PS2X_GS_CAPTURE_ROLL
                        std::fprintf(stderr, "[vu-trigger] frame %llu draw %lld n=%u flags=%03x vertex %u q=%g\n",
                                     (unsigned long long)frame, (long long)di, count, s.flags, i, v[i].q);
                    }
                    break;
                }
        }
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
    if (const char* pick = cstr(runtimeFlags().surfacePick)) {
        const uint64_t wanted = runtimeFlags().surfacePickEpoch;
        float px = 0, py = 0;
        if (motionEpoch == wanted && std::sscanf(pick, "%f,%f", &px, &py) == 2 && count % 3 == 0) {
            for (uint32_t i = 0; i < count; i += 3) {
                const auto& a = v[i];
                const auto& b = v[i + 1];
                const auto& c = v[i + 2];
                const float d0 = (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
                const float d1 = (c.x - b.x) * (py - b.y) - (c.y - b.y) * (px - b.x);
                const float d2 = (a.x - c.x) * (py - c.y) - (a.y - c.y) * (px - c.x);
                if ((d0 > 0 && d1 > 0 && d2 > 0) || (d0 < 0 && d1 < 0 && d2 < 0)) {
                    std::fprintf(
                        stderr, "[surface-pick] epoch=%llu world=%d xyoffset=%llx scaleZ=%g offsetZ=%g tex=%llx\n",
                        (unsigned long long)motionEpoch, int(engineWorldOpen), (unsigned long long)s.xyoffset,
                        sceneCamera.viewportScale[2], sceneCamera.viewportOffset[2], (unsigned long long)s.tex0);
                    logDraw(s, v + i, 3);
                    surface::RasterToWorld audit;
                    audit.configure(sceneCamera, s.xyoffset);
                    std::fprintf(stderr, "[surface-pick] waterPhase=%d cameraFrame=%ux%u\n", int(nativeWaterPass),
                                 sceneCamera.frameWidth, sceneCamera.frameHeight);
                    for (unsigned j = 0; j < 3; ++j) {
                        surface::Point p;
                        audit.point(v[i + j].x, v[i + j].y, v[i + j].z, p);
                        std::fprintf(stderr, "[surface-pick] world=%g,%g,%g\n", p.x, p.y, p.z);
                    }
                    break;
                }
            }
        }
    }
    const uint32_t needH = (uint32_t)std::max(1, std::min(sy1 + 1, 2048));
    RT* rt = getRT(fbp, fbw, fpsm, needH);
    if (rt_sync::lazySyncTarget(*rt))
        syncRows(*rt, rt_sync::drawRows((uint32_t)std::max(sy0, 0), (uint32_t)std::max(sy1, 0), rt->dirty, rt->dy0,
                                        rt->dy1, rt->pageH(), (uint32_t)rt->rowStamp.size()));
    else
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
                clearTarget(*rt, black);
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
    // A joined draw (drawSegments): each segment is clipped as its own draw was. One that its draw would have left
    // out is left out (no texture, no draw); the dirty rows are those of the others.
    uint32_t joinKeptCount = 0;
    if (joinCount > 1) {
        joinKept.assign(joinCount, 0);
        int ux0 = 1 << 30, uy0 = 1 << 30, ux1 = -1, uy1 = -1;
        for (uint32_t i = 0; i < joinCount; ++i) {
            const DrawVertex* sv = v + joinSegments[i].first;
            float mnx = 1e30f, mny = 1e30f, mxx = -1e30f, mxy = -1e30f;
            for (uint32_t j = 0; j < joinSegments[i].count; ++j) {
                mnx = std::min(mnx, sv[j].x);
                mxx = std::max(mxx, sv[j].x);
                mny = std::min(mny, sv[j].y);
                mxy = std::max(mxy, sv[j].y);
            }
            const int x0 = std::max(std::max(sx0, (int)std::floor(mnx)), 0);
            const int y0 = std::max(std::max(sy0, (int)std::floor(mny)), 0);
            const int x1 = std::min(std::min(sx1, (int)std::ceil(mxx)), (int)rt->w - 1);
            const int y1 = std::min(std::min(sy1, (int)std::ceil(mxy)), (int)rt->h - 1);
            if (joinSegments[i].count == 0 || x1 < x0 || y1 < y0)
                continue;
            joinKept[i] = 1;
            ++joinKeptCount;
            ux0 = std::min(ux0, x0);
            uy0 = std::min(uy0, y0);
            ux1 = std::max(ux1, x1);
            uy1 = std::max(uy1, y1);
        }
        if (!joinKeptCount)
            return;
        cx0 = ux0;
        cy0 = uy0;
        cx1 = ux1;
        cy1 = uy1;
    }

    // --- depth
    const uint32_t zbp = (uint32_t)bitsOf(s.zbuf, 0, 9);
    const uint32_t zpsm = 0x30u | (uint32_t)bitsOf(s.zbuf, 24, 4);
    const bool zmsk = (s.zbuf >> 32) & 1;
    const uint32_t ztst = (uint32_t)bitsOf(s.test, 17, 2);
    const bool zte = (s.test >> 16) & 1;
    if constexpr (kCaps.temporalUpscaler)
        if (tpLog && (upMode || cfg.neuralRendering))
            logTemporalDraw(*rt, s, fpsm, sx0, sy0, sx1, sy1, zte, ztst);
    DepthBuf* db = getDepth(zbp, rt->w, rt->h);
    float zmax = (zpsm == PSM_Z32) ? 4294967295.0f : (zpsm == PSM_Z24 ? 16777215.0f : 65535.0f);

    // --- texture
    TexBind tb;
    const bool tme = s.flags & DF_TME;
    bool haveTex = false;
    if (tme)
        haveTex = joinCount > 1 ? resolveJoined(tb) : resolveTexture(s, clut, tb, oneToOneSprite(s, v, count));
    if (joinCount > 1 && !haveTex) {
        joinRefused = true;
        return;
    }
    const auto title =
        startupTitle.select(haveTex && tb.titleArtwork, rt->w, rt->h, nativeMenu && nativeMenuScreen == 8,
                            cfg.forcedAspect, cfg.stretchToWindow, swapH ? float(swapW) / float(swapH) : 4.0f / 3);
    if (title.apply) {
        if (std::getenv("GSR_BACKDROP_LOG"))
            std::fprintf(stderr, "[backdrop] first title artwork: frame=%llu previous gui=%d menu=%d aspect=%.3f\n",
                         (unsigned long long)frame, sceneCamera.guiScreen, nativeMenuScreen, cfg.forcedAspect);
        cfg.forcedAspect = title.aspect;
        cfg.stretchToWindow = title.stretch;
        hudScale = title.foreground;
        hudSize = 1.0f;
        nativeMenu = true;
        nativeMenuScreen = 8;
        sceneCamera.guiScreen = 8;
    }
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
    if (spriteFootprint && !engineWorldOpen && scale > 1 && haveTex && rt->w >= 480 && (s.flags & DF_SPRITE) &&
        count > 6 && count % 6 == 0 && bitsOf(s.tex1, 2, 3) == 0 && bitsOf(s.tex1, 5, 4) == 0 &&
        cfg.textureFilter != 2) {
        bool valid = true;
        const float tw = (float)(1u << std::min<uint64_t>(bitsOf(s.tex0, 26, 4), 10));
        const float th = (float)(1u << std::min<uint64_t>(bitsOf(s.tex0, 30, 4), 10));
        for (uint32_t i = 0; i < count && valid; i += 6)
            valid = sprite_footprint::bounds(v + i, 6, (s.flags & DF_FST) != 0, tw, th,
                                             tb.isRT ? (unsigned)scale : tb.factor)
                        .valid;
        if (valid) {
            for (uint32_t i = 0; i < count; i += 6)
                draw(s, clut, v + i, 6);
            return;
        }
    }
    // Opt-in semantic audit: reconstruct candidate horizontal transparent
    // surfaces using the captured engine camera, never screen color guesses.
    if (runtimeFlags().surfaceAudit && engineWorldOpen && sceneCamera.guiScreen < 0 && (s.flags & DF_ABE) &&
        (s.flags & DF_TME) && !(s.flags & (DF_SPRITE | DF_LINE | DF_POINT | DF_FST)) && zte && ztst >= 2 &&
        rt->w >= 480 && haveTex && count % 3 == 0) {
        if (surfaceCameraSerial != sceneCamera.serial || surfaceXY != s.xyoffset) {
            surfaceCamera.configure(sceneCamera, s.xyoffset);
            surfaceCameraSerial = sceneCamera.serial;
            surfaceXY = s.xyoffset;
        }
        for (uint32_t i = 0; i < count; i += 3) {
            surface::Point a, b, c;
            if (surfaceCamera.point(v[i].x, v[i].y, v[i].z, a) &&
                surfaceCamera.point(v[i + 1].x, v[i + 1].y, v[i + 1].z, b) &&
                surfaceCamera.point(v[i + 2].x, v[i + 2].y, v[i + 2].z, c) && surface::horizontal(a, b, c) &&
                surfaceAuditMaterials.insert(s.tex0).second) {
                std::fprintf(stderr,
                             "[surface-audit] epoch=%llu tex0=%llx zmask=%d alpha=%llx rt=%d rgb=%g,%g,%g y=%g "
                             "extent=%g screen=%g,%g..%g,%g vertexAlpha=%g\n",
                             (unsigned long long)motionEpoch, (unsigned long long)s.tex0, int(zmsk),
                             (unsigned long long)s.alpha, int(tb.isRT), v[i].r, v[i].g, v[i].b, a.y,
                             surface::length(surface::sub(b, a)), minx, miny, maxx, maxy, v[i].a);
                Image im;
                if (tb.srv && readBoundTexture(tb, im)) {
                    char path[2048];
                    std::snprintf(path, sizeof(path), "%s/material-%llx.png", runtimeFlags().surfaceAudit->c_str(),
                                  (unsigned long long)s.tex0);
                    writePng(path, im);
                }
            }
        }
    }
    // The native preview viewport (0x1e97a0) is published once per gameplay frame inside the open world and
    // replaces the scene camera without Valid/World until the world viewport is published again. The water scope
    // often falls in that window, but its EE-transformed draws stay in the world camera's screen space.
    const SceneGuideCamera& surfaceSource = nativeWaterPass && !(sceneCamera.flags & SceneGuideCamera::World) &&
                                                    (waterWorldCamera.flags & SceneGuideCamera::World)
                                                ? waterWorldCamera
                                                : sceneCamera;
    if ((modernWater || rayShadows || rayReflections || rayGlobalIllumination) &&
        (engineWorldOpen || pendingTemporalEnd) && surface::surfaceCameraAccepted(surfaceSource, pendingTemporalEnd) &&
        rt->w >= surfaceSource.frameWidth && rt->h >= surfaceSource.frameHeight &&
        (nativeWaterPass || (zte && ztst >= 2)) && !(s.flags & (DF_SPRITE | DF_LINE | DF_POINT)) && count % 3 == 0) {
        CpuProfile::Scope surfaceProfile(cpuProfile, CpuProfile::DrawSurface);
        // Same camera cache key as before the builder: every draw reaching this point may reconfigure it.
        if (surfaceBuilder->needsConfigure(surfaceSource.serial, s.xyoffset))
            surfaceBuilder->stageCamera(pendingTemporalEnd ? rt->neuralCamera : surfaceSource, s.xyoffset);
        // Eligibility is proved against native layer geometry, independent
        // of level, texture format, palette animation or vertex tint.
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
        // A passing alpha test does not make a blended terrain/detail
        // layer opaque. Only a fully replacing GS blend may occlude rays.
        const bool opaqueBlend =
            !(s.flags & DF_ABE) || (s.alpha == 0x44 && vertexAlpha >= 128 &&
                                    (!textureAlpha || (bitsOf(s.tex0, 35, 2) == 0 && tb.minimumAlpha >= 128)));
        const bool opaqueEligible = !zmsk && opaqueBlend && (!(s.test & 1) || alphaTest == 1 || constantAlphaPass);
        const bool materialEligible =
            haveTex && !tb.isRT && (!(s.flags & DF_ABE) || s.alpha == 0x44) && (!(s.test & 1) || alphaTest != 0);
        const bool eligible = opaqueEligible || materialEligible || waterState;
        if (eligible || rt->surfaceSubmittedEpoch != motionEpoch) {
            // Conversion to world space, the resident match/observe and the append run on the builder (in
            // submission order); readers of the results drain it first. A job without vertices only starts the
            // target's list for this world, like the clear the inline loop did at every draw reaching it.
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
    // --- temporal upscaler: is this a 3D world draw (rendered into the reduced render rectangle) or a full
    // resolution one?
    float dsx = (float)scale, dsy = (float)scale; // render pixels per native pixel of this draw
    bool worldDraw = false;
    if constexpr (kCaps.temporalUpscaler)
        if (upActive()) {
            if (haveTex && tb.isRT && tb.rt != rt && tb.rt->wstate == 1)
                finalizeWorld(*tb.rt); // sampled as a texture: needs its final full resolution content
            const bool frameSized = (sx1 - sx0 + 1) >= 480 && (sy1 - sy0 + 1) >= 320 && rt->w >= 480 && rt->cls == 0;
            const bool depthGE = zte && ztst >= 2;
            const float area = (maxx - minx) * (maxy - miny);
            const bool backdrop =
                !(s.flags & DF_SPRITE) && area > 0.2f * (float)(sx1 - sx0 + 1) * (float)(sy1 - sy0 + 1);
            const bool isWorld = frameSized && (depthGE || backdrop);
            worldDraw = upscalePhase(*rt, *db, isWorld, depthGE, zbp, zmax, sx1, sy1);
            if (worldDraw) {
                dsx = (float)rt->wgeom.renderW / (float)rt->wgeom.frameW;
                dsy = (float)rt->wgeom.renderH / (float)rt->wgeom.frameH;
            }
        }
    if (engineWorldOpen &&
        (sceneCamera.flags & (SceneGuideCamera::Valid | SceneGuideCamera::World)) ==
            (SceneGuideCamera::Valid | SceneGuideCamera::World) &&
        rt->w >= sceneCamera.frameWidth && rt->h >= sceneCamera.frameHeight && zte && ztst >= 2 &&
        !(s.flags & DF_SPRITE))
        rt->worldEpoch = motionEpoch;
    if (neuralEnabled || upMode || modernWater || rayShadows || rayReflections || rayGlobalIllumination) {
        const bool worldCamera =
            !neuralWorldClosed &&
            (sceneCamera.flags &
             (SceneGuideCamera::Valid | SceneGuideCamera::World | SceneGuideCamera::HaveViewport)) ==
                (SceneGuideCamera::Valid | SceneGuideCamera::World | SceneGuideCamera::HaveViewport);
        const bool fullFrame = worldCamera && sceneCamera.frameWidth >= 480 && sceneCamera.frameHeight >= 320 &&
                               sx1 + 1 >= (int)sceneCamera.frameWidth && sy1 + 1 >= (int)sceneCamera.frameHeight &&
                               rt->w >= sceneCamera.frameWidth && rt->h >= sceneCamera.frameHeight;
        const bool depthWorld = zte && ztst >= 2;
        if (rt->temporalEpoch != motionEpoch)
            rt->neuralState = 0;
        // The engine EndWorld packet resolves all world draws before native HUD callbacks.
        if (!rt->neuralState && fullFrame && depthWorld && !(s.flags & DF_SPRITE)) {
            enterTemporalWorld(*rt, zbp, zmax, s.xyoffset);
        }
        if (rt->neuralState == 1 && rt->temporalSR) {
            worldDraw = true;
            dsx = (float)rt->wgeom.renderW / rt->wgeom.frameW;
            dsy = (float)rt->wgeom.renderH / rt->wgeom.frameH;
        }
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
    if (haveTex && tb.titleArtwork && rt->w >= 480 && (writeMask & 7)) {
        static const bool log = std::getenv("GSR_BACKDROP_LOG") != nullptr;
        static int left = 400;
        if (log && left > 0) {
            --left;
            std::fprintf(
                stderr,
                "[backdrop] frame %llu: native title tile (gui=%d menu=%d nativeMenu=%d world=%d hudScale=%.3f "
                "replaced=%llu)\n",
                (unsigned long long)frame, sceneCamera.guiScreen, nativeMenuScreen, (int)nativeMenu,
                (int)engineWorldOpen, (double)hudScale, (unsigned long long)wideReplaceFrame);
        }
    }
    // Any other screen (the intro movie, the world) ends the title: a later loading screen shows no title art.
    if (sceneCamera.guiScreen >= 0 && sceneCamera.guiScreen != 8 && sceneCamera.guiScreen != 14)
        titleBackdropAt = {};
    // Loading after the title: the engine redraws the grabbed title page as large tiles (stretched 4:3 art); draw the
    // wide title image instead and skip them. The bar and text are smaller draws and stay.
    if (!engineWorldOpen && nativeMenu && sceneCamera.guiScreen == 14 && haveTex && titleBackdropRecent() &&
        (writeMask & 7) && rt->w >= 480 && maxx - minx >= 120.0f && maxy - miny >= 120.0f) {
        clearInventoryCanvas(*rt, true);
        if (wideReplaceFrame == frame && wideReplaceRT == rt)
            return;
    }
    const bool date = (s.test >> 14) & 1;
    const bool pabe = s.flags & DF_PABE;
    bool useHw = !partial && !date && fixedFunctionBlend();
    rhi::BlendOp bop = rhi::BlendOp::Add;
    rhi::BlendFactor sf = rhi::BlendFactor::One, df = rhi::BlendFactor::Zero;
    bool bEnable = false;
    if (abe) {
        if (!draw_blend::fixedForm(s, bop, sf, df))
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
    // world draws: footprint LOD is measured in (fewer) render pixels; the bias is upscale::lodBias of the plan
    // (RT::wgeom, 0 on a backend whose world is drawn at full resolution)
    k.g[0] = userMipBias + (worldDraw ? rt->wgeom.lodBias : 0.0f);
    if (worldDraw) // sub-pixel jitter in NDC (render pixels -> NDC; the viewport spans the whole target)
    {
        k.g[1] = 2.0f * rt->wjx / (float)(rt->w * dsx);
        k.g[2] = -2.0f * rt->wjy / (float)(rt->h * dsy);
    }
    k.h[0] = worldDraw ? rt->wjx : 0;
    k.h[1] = worldDraw ? rt->wjy : 0;
    k.h[2] = 16384;
    // Single native UI sprite, point sampling only. Work from original
    // endpoints before HUD transforms; never infer borders from colours.
    if (spriteFootprint && !engineWorldOpen && scale > 1 && haveTex && rt->w >= 480 && (s.flags & DF_SPRITE) &&
        count == 6 && mxl == 0 && mmag == 0 && mmin == 0 && cfg.textureFilter != 2) {
        const auto b = sprite_footprint::bounds(v, count, (s.flags & DF_FST) != 0, (float)k.b[1], (float)k.b[2],
                                                tb.isRT ? (unsigned)scale : tb.factor);
        if (b.valid) {
            k.a[0] |= 1u << 24;
            k.spriteBounds[0] = b.u0;
            k.spriteBounds[1] = b.v0;
            k.spriteBounds[2] = b.u1;
            k.spriteBounds[3] = b.v1;
        }
    }
    // Terrain layers (terrain_detail.h): world-space detail and the side projection of steep faces. Adds nothing
    // unless a terrain setting is on.
    if constexpr (kCaps.terrainDetail)
        k.a[0] |= prepareTerrain(s, v, count, haveTex, haveTex && tb.isRT, *rt, zmsk, dsx, dsy, k);
    bool drawMotion = false;
    if constexpr (kCaps.motionVectors) {
        CpuProfile::Scope motionProfile(cpuProfile, CpuProfile::DrawObjectMotion);
        // [vertex programs] a batch of records has its own history and previous stream
        const VertexProgramBatch* records = nullptr;
        if constexpr (kCaps.vertexPrograms)
            records = programBatch;
        if (!records)
            drawMotion = prepareObjectMotion(*rt, s, v, count, dsx, dsy);
        else if constexpr (kCaps.vertexPrograms)
            drawMotion = prepareProgramMotion(*rt, s, *records, dsx, dsy);
    }
    if (haveTex && tb.isRT) {
        k.src[0] = tb.ox;
        k.src[1] = tb.oy;
        const bool reduced = tb.rt->wstate == 1 || (tb.rt->neuralState == 1 && tb.rt->temporalSR);
        k.src[2] = 1.0f / (float)tb.rt->w *
                   (reduced ? (float)tb.rt->wgeom.renderW / (float)tb.rt->wgeom.frameW / (float)scale : 1.0f);
        k.src[3] = 1.0f / (float)tb.rt->h *
                   (reduced ? (float)tb.rt->wgeom.renderH / (float)tb.rt->wgeom.frameH / (float)scale : 1.0f);
    }

    if (logThis)
        std::fprintf(stderr,
                     "    consts: haveTex=%d isRT=%d useHw=%d needDest=%d bEnable=%d fl=%08x aref/ate=%u abcd=%02x "
                     "fix=%u maskB=%08x tex=%ux%u wm=%u clampc=%u,%u,%u,%u d0=%08x e=(%.2f,%.1f) fba=%u fbmsk=%08x "
                     "src=(%.1f,%.1f,%g,%g) rtdim=%ux%u\n",
                     (int)haveTex, (int)(haveTex && tb.isRT), (int)useHw, (int)needDest, (int)bEnable, k.a[0], k.a[1],
                     k.a[2], k.a[3], k.b[0], k.b[1], k.b[2], k.b[3], k.c[0], k.c[1], k.c[2], k.c[3], k.d[0], k.e[0],
                     k.e[1], (unsigned)s.fba, fbmsk, k.src[0], k.src[1], k.src[2], k.src[3], rt->w, rt->h);
    // --- vertices: a writable copy (the HUD / menu maps below rewrite positions), handed to the GPU by endVertices
    std::optional<CpuProfile::Scope> uploadProfile(std::in_place, cpuProfile, CpuProfile::DrawVertexUpload);
    DrawVertex* const mapped = beginVertices(v, count);
    if (inventoryFontShadow) {
        // Native batching can combine colored foregrounds and black
        // shadows in the same draw. Classify complete glyph triangles.
        auto* glyph = mapped;
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
            DrawVertex* mv = mapped;
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
            DrawVertex* dv = mapped;
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
                            // Only the background stays full width. A long
                            // text batch must share the map of its shadows
                            // and individual glyph packets too.
                            if (!band)
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
    endVertices();
    uploadProfile.reset();

    // --- dest / feedback snapshot
    CpuProfile::Scope submitProfile(cpuProfile, CpuProfile::DrawSubmit);
    GpuView destSrv = nullptr;
    GpuView texSrv = haveTex ? tb.srv : nullptr;
    const bool feedback = haveTex && tb.isRT && tb.rt == rt;
    uint32_t box[4] = {(uint32_t)std::floor(cx0 * dsx), (uint32_t)std::floor(cy0 * dsy),
                       (uint32_t)std::ceil((cx1 + 1) * dsx), (uint32_t)std::ceil((cy1 + 1) * dsy)}; // x0 y0 x1 y1
    box[2] = std::min<uint32_t>(box[2], rt->w * scale);
    box[3] = std::min<uint32_t>(box[3], rt->h * scale);
    std::optional<CpuProfile::Scope> destProfile(std::in_place, cpuProfile, CpuProfile::DrawDest);
    if (feedback) {
        destSrv = snapshot(*rt, nullptr);
        texSrv = destSrv;
        if (needDest)
            destSrv = rt->copyView();
    } else {
        if (haveTex && tb.isRT)
            texSrv = tb.rt->view();
        if (needDest)
            destSrv = snapshot(*rt, box);
    }
    destProfile.reset();
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
        rhi::BlendState blend;
        blend.enable = alpha::blendForPass(bEnable, wm);
        blend.op = bop;
        blend.src = sf;
        blend.dst = df;
        blend.writeMask = wm;
        setPass(k, blend, {true, zf, zw});
    };

    int32_t sr[4] = {(int32_t)std::floor(sx0 * dsx), (int32_t)std::floor(sy0 * dsy),
                     (int32_t)std::ceil((sx1 + 1) * dsx), (int32_t)std::ceil((sy1 + 1) * dsy)}; // left top right bottom
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
    {
        CpuProfile::Scope bindProfile(cpuProfile, CpuProfile::DrawBind);
        bindDraw(*rt, *db, useHw, texSrv, destSrv, (float)rt->w * dsx, (float)rt->h * dsy, sr, haveTex && tb.isRT);
    }
    // A joined draw: drawVertices and drawVelocity draw the kept segments in order, each with its own texture.
    joinDraws.clear();
    if (joinCount > 1)
        for (uint32_t i = 0; i < joinCount; ++i)
            if (joinKept[i])
                joinDraws.push_back({joinTextures[i].srv, joinSegments[i].first, joinSegments[i].count});
    // Material detail (material_detail.h): binds its own pixel program variant for the draws that take it. Does
    // nothing unless the setting is on and the bound HD texture has a material map.
    bool materialDraw = false;
    if constexpr (kCaps.materialDetail)
        materialDraw = haveTex && tb.materialSrv &&
                       prepareMaterial(s, tb.materialSrv, tb.materialClass, haveTex, tb.isRT, *rt, zte && ztst >= 2,
                                       k.a[0], dsx, dsy, useHw);
    // [vertex programs] the batch's own vertices and vertex program replace the stand-ins bindDraw has bound
    const VertexProgramBatch* records = nullptr;
    if constexpr (kCaps.vertexPrograms) {
        records = programBatch;
        if (records)
            bindProgramDraw(s, *records);
    }
    const auto drawPass = [&] {
        if (!records)
            drawVertices(count);
        else if constexpr (kCaps.vertexPrograms)
            drawProgramVertices(*records);
    };

    // The motion vectors of the pass just drawn (kCaps.motionVectors). restore: another color pass of this draw
    // follows and needs the color pipeline back.
    auto velocityPass = [&](uint32_t passMode, uint8_t wm, bool zw, bool restore) {
        if constexpr (kCaps.motionVectors) {
            if (!drawMotion || !(wm & 7))
                return;
            CpuProfile::Scope velocityProfile(cpuProfile, CpuProfile::DrawVelocity);
            // GREATER has ambiguous coplanar replays after a depth write. Do
            // not assert object correspondence for those pixels.
            k.h[3] = (wm & 7) == 7 && !(zw && zf == rhi::Compare::Greater) ? 1.0f : 0.0f;
            setup(passMode, wm, zw);
            if (!records)
                drawVelocity(*rt, *db, count, zw ? rhi::Compare::Equal : zf, restore, materialDraw, useHw);
            else if constexpr (kCaps.vertexPrograms) // [vertex programs]
                drawProgramVelocity(*rt, *db, *records, zw ? rhi::Compare::Equal : zf, restore, materialDraw, useHw);
            // The next pass's setup() uploads its constants and blend/depth state.
        }
    };

    // pass 0
    const bool failPass = ate && afail != 0 && atst != 1;
    {
        CpuProfile::Scope passProfile(cpuProfile, CpuProfile::DrawPass);
        setup(0, writeMask, !zmsk);
        drawPass();
    }
    velocityPass(0, writeMask, !zmsk, failPass);
    if (failPass) {
        const auto fail = alpha::failurePass(afail, writeMask, !zmsk);
        {
            CpuProfile::Scope passProfile(cpuProfile, CpuProfile::DrawPass);
            setup(1, fail.colorMask, fail.depthWrite);
            drawPass();
        }
        velocityPass(1, fail.colorMask, fail.depthWrite, false);
    }
    {
        CpuProfile::Scope endProfile(cpuProfile, CpuProfile::DrawEnd);
        endDraw();
    }

    if (joinCount > 1) {
        st.drawCalls += joinKeptCount; // as many as the segments' own draws
        st.joinedDraws++;
        st.joinedPasses += joinKeptCount;
        joinDraws.clear();
    } else
        st.drawCalls++;
    // Constant-colour sprites into a clean scratch target are written to memory as well, so the game's next read of
    // those blocks needs no GPU readback (one blocking readback per gameplay frame without it): gs_solid_mirror.h.
    const auto mirrored = planSolidMirror(
        s, v, count, haveTex, *rt, uploadActive,
        dsx == scale && dsy == scale && k.h[0] == 0 && k.h[1] == 0 && writeMask == 15, rts,
        [&](const RT& other, uint32_t y0, uint32_t y1, uint32_t& lo, uint32_t& hi) { rowSpan(other, y0, y1, lo, hi); });
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
    if (!mirrored.empty()) {
        applySolidMirror(mem, mirrored, *rt);
        if constexpr (kCaps.readbackVerify) {
            static const bool verify = std::getenv("GSR_RB_VERIFY") != nullptr;
            if (verify)
                verifyRTBlocks(*rt, 0, ~0u, 0, ~0u);
        }
    }
    if constexpr (kCaps.readbackPrefetch) {
        static const bool traceReadbackDraw = std::getenv("GSR_RB_DRAW_TRACE") != nullptr;
        static unsigned readbackDrawsLeft = 64;
        if (traceReadbackDraw && readbackDrawsLeft && writeMask && rt->readbackFrame != ~0ull &&
            frame <= rt->readbackFrame + 2 && uint64_t(rt->w) * (rt->dy1 - rt->dy0) <= 16384) {
            --readbackDrawsLeft;
            std::fprintf(stderr, "[rb-draw] target=%x rows=%u..%u scissor=%llx writeMask=%x zmsk=%u\n", rt->fbp,
                         rt->dy0, rt->dy1, (unsigned long long)s.scissor, writeMask, unsigned(zmsk));
            logDraw(s, v, count);
        }
        if (writeMask)
            prefetchReadback(*rt);
    }
    if ((!rtDump.empty() && rtDump.count(di)) ||
        (rtDumpFrame >= 0 && (int64_t)frame >= rtDumpFrame && (int64_t)frame <= rtDumpFrameLast)) {
        // debug: GSR_RTDUMP=<draw>[,<draw>...] writes the scissor area of the target as rtdump_<draw>.png
        // (GSR_RTDUMP_DIR, default cwd) right after that draw
        Image im;
        const uint32_t w = (uint32_t)(sx1 + 1) - (uint32_t)sx0, h = (uint32_t)(sy1 + 1) - (uint32_t)sy0;
        if (readTarget(*rt, rt->w * scale,
                       std::min<uint32_t>(rt->h, std::max<uint32_t>((uint32_t)(sy1 + 1), dumpRows)) * scale, im)) {
            (void)w;
            (void)h;
            const char* d = runtimeFlags().rtDumpDir.c_str();
            char name[512];
            std::snprintf(name, sizeof(name), "%s/rtdump_%lld.png", d, (long long)di);
            writePng(name, im);
            if (readTarget(*rt, rt->w * scale,
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
