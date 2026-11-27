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

#include <chrono>
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

// ---- draw(): one GS draw, phase by phase ----
//
// draw() reads as the list of its phases. Each phase fills its part of the DrawPlan `d` (gs_draw_plan.h) that the
// later phases read. A phase that returns false ends the draw: it was cut by a debug hook, clipped away, refused (a
// joined draw), or drawn another way (split into sprites, replaced by a wide image).
void GSR_IMPL::draw(const GsDrawState& s, const ClutBuf& clut, const DrawVertex* v, uint32_t count) {
    CpuProfile::Scope profile(cpuProfile, CpuProfile::Draw);
    if (!ok || count == 0)
        return;
    Draw d(s, clut, v, count);
    d.index = drawIndex++;
    if (!drawDebugHooks(d))
        return;
    decodeTarget(d);
    surfacePickProbe(d);
    resolveTarget(d);
    noteWorldBoundary(d);
    if (!clipDraw(d))
        return;
    resolveDepth(d);
    if (!resolveDrawTexture(d))
        return;
    applyStartupArtwork(d);
    if (drawSpritesOneByOne(d))
        return;
    surfaceAuditProbe(d);
    submitRaySurfaces(d);
    worldPhase(d);
    drawStatistics(d);
    planWriteMask(d);
    if (!applyGuiRules(d))
        return;
    planBlend(d);
    buildConstants(d);
    prepareDrawDetail(d);
    logConstants(d);
    mapVertices(d);
    submitDraw(d);
}

// Debug hooks that run before anything is decided: the draw log, the VU trigger, and the draw range cut (drawStop,
// skipA..skipB) used to bisect a bad draw. False: the draw is cut.
bool GSR_IMPL::drawDebugHooks(Draw& d) {
    const GsDrawState& s = d.s;
    const DrawVertex* v = d.v;
    const uint32_t count = d.count;
    const int64_t di = d.index;
    if (inventoryLogDelay >= 0 && sceneCamera.guiScreen == hud::kInventoryScreen && inventoryLogStart == ~uint64_t{0})
        inventoryLogStart = frame + (uint64_t)inventoryLogDelay;
    d.logThis = (drawLogFirst >= 0 && (int64_t)frame >= drawLogFirst && (int64_t)frame <= drawLogLast) ||
                (drawLogTbp >= 0 && (s.flags & DF_TME) && (int64_t)bitsOf(s.tex0, 0, 14) == drawLogTbp) ||
                (inventoryLogDelay >= 0 && inventoryLogStart != ~uint64_t{0} && frame >= inventoryLogStart &&
                 frame - inventoryLogStart < 2);
    if (d.logThis) {
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
        return false;
    if (di >= skipA && di <= skipB && skipA >= 0)
        return false;
    return true;
}

// The FRAME and SCISSOR registers of the draw, and the bounding box of its vertices.
void GSR_IMPL::decodeTarget(Draw& d) {
    const GsDrawState& s = d.s;
    d.fbp = (uint32_t)bitsOf(s.frame, 0, 9);
    d.fbw = (uint32_t)bitsOf(s.frame, 16, 6);
    d.fpsm = (uint32_t)bitsOf(s.frame, 24, 6);
    d.fbmsk = (uint32_t)(s.frame >> 32);
    // A target in a depth or unknown format is treated as a CT32 target.
    if (d.fpsm != PSM_CT32 && d.fpsm != PSM_CT24 && d.fpsm != PSM_CT16 && d.fpsm != PSM_CT16S) {
        if (isZ(d.fpsm))
            d.fpsm = PSM_CT32;
        else if (!isValidPsm(d.fpsm))
            d.fpsm = PSM_CT32;
    }
    d.sx0 = (int)bitsOf(s.scissor, 0, 11);
    d.sx1 = (int)bitsOf(s.scissor, 16, 11);
    d.sy0 = (int)bitsOf(s.scissor, 32, 11);
    d.sy1 = (int)bitsOf(s.scissor, 48, 11);

    float minx = 1e30f, miny = 1e30f, maxx = -1e30f, maxy = -1e30f;
    for (uint32_t i = 0; i < d.count; ++i) {
        minx = std::min(minx, d.v[i].x);
        maxx = std::max(maxx, d.v[i].x);
        miny = std::min(miny, d.v[i].y);
        maxy = std::max(maxy, d.v[i].y);
    }
    d.minx = minx;
    d.miny = miny;
    d.maxx = maxx;
    d.maxy = maxy;
}

// Debug (surface_pick setting): print the triangle under a picked screen point and its world position.
void GSR_IMPL::surfacePickProbe(const Draw& d) {
    const char* pick = cstr(runtimeFlags().surfacePick);
    if (!pick)
        return;
    const GsDrawState& s = d.s;
    const DrawVertex* v = d.v;
    const uint32_t count = d.count;
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
                std::fprintf(stderr, "[surface-pick] epoch=%llu world=%d xyoffset=%llx scaleZ=%g offsetZ=%g tex=%llx\n",
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

// The render target of the draw, brought up to date with what the game wrote to its memory since the last draw.
void GSR_IMPL::resolveTarget(Draw& d) {
    // Scissor coordinates are 11 bits: a target needs no more than 2048 rows, and no more than the scissor reaches.
    const uint32_t needH = (uint32_t)std::max(1, std::min(d.sy1 + 1, 2048));
    RT* rt = getRT(d.fbp, d.fbw, d.fpsm, needH);
    d.rt = rt;
    if (rt_sync::lazySyncTarget(*rt))
        syncRows(*rt, rt_sync::drawRows((uint32_t)std::max(d.sy0, 0), (uint32_t)std::max(d.sy1, 0), rt->dirty, rt->dy0,
                                        rt->dy1, rt->pageH(), (uint32_t)rt->rowStamp.size()));
    else
        syncRT(*rt);
    if (gridCapture.open && !gridCapture.rt && rt->w >= 480 && rt->h >= 320)
        gridCaptureStart(*rt); // the first display draw of a captured grid: save what it will cover
}

// The world-to-HUD boundary: resolve the finished world before the first HUD colour reaches the frame, and let a world
// draw claim a display-sized target for this world phase.
void GSR_IMPL::noteWorldBoundary(Draw& d) {
    const GsDrawState& s = d.s;
    const DrawVertex* v = d.v;
    const uint32_t count = d.count;
    RT* rt = d.rt;
    // Native EndWorld precedes the final CPU-clipped water packet drain.
    // Resolve at the first actual HUD submission, after those perspective
    // world triangles and before any HUD color reaches the framebuffer.
    if (pendingTemporalEnd && !engineWorldOpen && rt->w >= 480 && (d.fbmsk & 0xffffffu) != 0xffffffu) {
        bool perspectiveTail = false;
        if (!(s.flags & (DF_SPRITE | DF_FST)) && (s.flags & DF_TME))
            for (uint32_t i = 0; i < count; ++i)
                perspectiveTail |= !hud::nativeUiQ(v[i].q);
        // Native HUD sprites/RT-backed quads can retain FGE. Their native
        // UI Q/FST still separates them from the late perspective water.
        const bool ui = (s.flags & DF_SPRITE) || (!perspectiveTail && ((s.flags & DF_TME) || !(s.flags & DF_FGE)));
        if (ui) {
            if (runtimeFlags().waterLog && motionEpoch == 200)
                std::fprintf(stderr, "[world-tail] resolve before HUD flags=%x bounds=%g,%g..%g,%g\n", s.flags, d.minx,
                             d.miny, d.maxx, d.maxy);
            for (auto& target : rts)
                finalizeNeuralWorld(*target);
            pendingTemporalEnd = false;
        }
    }
    // A world clear/sky draw also owns the new image, even if no eligible
    // temporal geometry follows. Never inherit an earlier NR success tag.
    if (engineWorldOpen && rt->w >= 480 && rt->h >= 320 && (d.fbmsk & 0xffffffu) != 0xffffffu) {
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
}

// The pixels the draw can write: its bounding box clipped to the scissor and the target. False: nothing is left.
bool GSR_IMPL::clipDraw(Draw& d) {
    const RT* rt = d.rt;
    int cx0 = std::max(d.sx0, (int)std::floor(d.minx)), cx1 = std::min(d.sx1, (int)std::ceil(d.maxx));
    int cy0 = std::max(d.sy0, (int)std::floor(d.miny)), cy1 = std::min(d.sy1, (int)std::ceil(d.maxy));
    cx0 = std::max(cx0, 0);
    cy0 = std::max(cy0, 0);
    cx1 = std::min(cx1, (int)rt->w - 1);
    cy1 = std::min(cy1, (int)rt->h - 1);
    if (cx1 < cx0 || cy1 < cy0)
        return false;
    d.cx0 = cx0;
    d.cy0 = cy0;
    d.cx1 = cx1;
    d.cy1 = cy1;
    if (joinCount > 1)
        return clipJoinedSegments(d);
    return true;
}

// A joined draw (drawSegments): each segment is clipped as its own draw was. One that its draw would have left
// out is left out (no texture, no draw); the dirty rows are those of the others.
bool GSR_IMPL::clipJoinedSegments(Draw& d) {
    const RT* rt = d.rt;
    const int sx0 = d.sx0, sy0 = d.sy0, sx1 = d.sx1, sy1 = d.sy1;
    uint32_t joinKeptCount = 0;
    joinKept.assign(joinCount, 0);
    int ux0 = 1 << 30, uy0 = 1 << 30, ux1 = -1, uy1 = -1;
    for (uint32_t i = 0; i < joinCount; ++i) {
        const DrawVertex* sv = d.v + joinSegments[i].first;
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
    d.joinKeptCount = joinKeptCount;
    if (!joinKeptCount)
        return false;
    d.cx0 = ux0;
    d.cy0 = uy0;
    d.cx1 = ux1;
    d.cy1 = uy1;
    return true;
}

// The ZBUF and TEST depth state and the depth buffer of the draw.
void GSR_IMPL::resolveDepth(Draw& d) {
    const GsDrawState& s = d.s;
    d.zbp = (uint32_t)bitsOf(s.zbuf, 0, 9);
    d.zpsm = 0x30u | (uint32_t)bitsOf(s.zbuf, 24, 4); // ZBUF.PSM holds the low bits of the Z format (0x30..0x3a)
    d.zmsk = (s.zbuf >> 32) & 1;
    d.ztst = (uint32_t)bitsOf(s.test, 17, 2);
    d.zte = (s.test >> 16) & 1;
    if constexpr (kCaps.temporalUpscaler)
        if (tpLog && (upMode || cfg.neuralRendering))
            logTemporalDraw(*d.rt, s, d.fpsm, d.sx0, d.sy0, d.sx1, d.sy1, d.zte, d.ztst);
    d.db = getDepth(d.zbp, d.rt->w, d.rt->h);
    d.zmax = (d.zpsm == PSM_Z32) ? 4294967295.0f : (d.zpsm == PSM_Z24 ? 16777215.0f : 65535.0f);
}

// The texture of the draw (all the segments' textures for a joined draw). False: a joined draw is refused because
// one of its textures does not resolve or would bind differently; drawSegments then draws the segments one by one.
bool GSR_IMPL::resolveDrawTexture(Draw& d) {
    d.tme = d.s.flags & DF_TME;
    if (d.tme)
        d.haveTex =
            joinCount > 1 ? resolveJoined(d.tb) : resolveTexture(d.s, d.clut, d.tb, oneToOneSprite(d.s, d.v, d.count));
    if (joinCount > 1 && !d.haveTex) {
        joinRefused = true;
        return false;
    }
    return true;
}

// The retail title and menu artwork identify the first GUI draws of the game: the title switches the presentation to
// the title page, and menu artwork closes a world phase that startup opened before the first menu tick.
void GSR_IMPL::applyStartupArtwork(Draw& d) {
    const RT* rt = d.rt;
    const auto title = startupTitle.select(d.haveTex && d.tb.titleArtwork, rt->w, rt->h,
                                           nativeMenu && nativeMenuScreen == hud::kTitleScreen, cfg.forcedAspect,
                                           cfg.stretchToWindow, swapH ? float(swapW) / float(swapH) : 4.0f / 3);
    if (title.apply) {
        if (std::getenv("GSR_BACKDROP_LOG"))
            std::fprintf(stderr, "[backdrop] first title artwork: frame=%llu previous gui=%d menu=%d aspect=%.3f\n",
                         (unsigned long long)frame, sceneCamera.guiScreen, nativeMenuScreen, cfg.forcedAspect);
        cfg.forcedAspect = title.aspect;
        cfg.stretchToWindow = title.stretch;
        hudScale = title.foreground;
        hudSize = 1.0f;
        nativeMenu = true;
        nativeMenuScreen = hud::kTitleScreen;
        sceneCamera.guiScreen = hud::kTitleScreen;
    }
    // Startup may have opened a generic world phase before the first
    // menu handler tick. Exact retail artwork identifies this first GUI
    // draw without waiting a frame or classifying arbitrary world images.
    if (nativeMenu && d.haveTex && d.tb.menuArtwork && rt->w >= 480 && rt->h >= 320) {
        engineWorldOpen = false;
        sceneCamera.flags = 0;
    }
}

// A point-sampled UI sprite drawn with bilinear filtering (or at a higher internal scale) is clamped to its native
// atlas cell (spriteBounds). True: a batch of such sprites was drawn one sprite at a time instead (each needs its own
// bounds), so this draw is done.
bool GSR_IMPL::drawSpritesOneByOne(Draw& d) {
    const GsDrawState& s = d.s;
    const RT* rt = d.rt;
    d.clampSpriteFootprint = spriteFootprint && !engineWorldOpen && (scale > 1 || cfg.textureFilter == 2) &&
                             d.haveTex && rt->w >= 480 && (s.flags & DF_SPRITE) && bitsOf(s.tex1, 2, 3) == 0 &&
                             bitsOf(s.tex1, 5, 4) == 0;
    // Endpoint bounds belong to each sprite, not an entire GS batch. Keep
    // the original order and split only batches whose every member has a
    // validated native footprint (the health frame/highlight uses two).
    constexpr uint32_t kSpriteVertices = 6; // a sprite reaches the backend as two triangles
    if (d.clampSpriteFootprint && d.count > kSpriteVertices && d.count % kSpriteVertices == 0) {
        bool valid = true;
        const float tw = (float)(1u << std::min<uint64_t>(bitsOf(s.tex0, 26, 4), 10));
        const float th = (float)(1u << std::min<uint64_t>(bitsOf(s.tex0, 30, 4), 10));
        for (uint32_t i = 0; i < d.count && valid; i += kSpriteVertices)
            valid = sprite_footprint::bounds(d.v + i, kSpriteVertices, (s.flags & DF_FST) != 0, tw, th,
                                             d.tb.isRT ? (unsigned)scale : d.tb.factor)
                        .valid;
        if (valid) {
            for (uint32_t i = 0; i < d.count; i += kSpriteVertices)
                draw(s, d.clut, d.v + i, kSpriteVertices);
            return true;
        }
    }
    return false;
}

// Opt-in semantic audit: reconstruct candidate horizontal transparent
// surfaces using the captured engine camera, never screen color guesses.
void GSR_IMPL::surfaceAuditProbe(const Draw& d) {
    const GsDrawState& s = d.s;
    const DrawVertex* v = d.v;
    const uint32_t count = d.count;
    if (runtimeFlags().surfaceAudit && engineWorldOpen && sceneCamera.guiScreen < 0 && (s.flags & DF_ABE) &&
        (s.flags & DF_TME) && !(s.flags & (DF_SPRITE | DF_LINE | DF_POINT | DF_FST)) && d.zte && d.ztst >= 2 &&
        d.rt->w >= 480 && d.haveTex && count % 3 == 0) {
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
                             (unsigned long long)motionEpoch, (unsigned long long)s.tex0, int(d.zmsk),
                             (unsigned long long)s.alpha, int(d.tb.isRT), v[i].r, v[i].g, v[i].b, a.y,
                             surface::length(surface::sub(b, a)), d.minx, d.miny, d.maxx, d.maxy, v[i].a);
                Image im;
                if (d.tb.srv && readBoundTexture(d.tb, im)) {
                    char path[2048];
                    std::snprintf(path, sizeof(path), "%s/material-%llx.png", runtimeFlags().surfaceAudit->c_str(),
                                  (unsigned long long)s.tex0);
                    writePng(path, im);
                }
            }
        }
    }
}

// World geometry for the ray passes (modern water, ray shadows, reflections, GI): the draw's triangles go to the
// surface builder (surface_builder.h) in world space.
void GSR_IMPL::submitRaySurfaces(Draw& d) {
    const GsDrawState& s = d.s;
    RT* rt = d.rt;
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
        (nativeWaterPass || (d.zte && d.ztst >= 2)) && !(s.flags & (DF_SPRITE | DF_LINE | DF_POINT)) &&
        d.count % 3 == 0) {
        CpuProfile::Scope surfaceProfile(cpuProfile, CpuProfile::DrawSurface);
        surface::DrawInput in;
        in.list = &rt->surface;
        in.epoch = motionEpoch;
        in.firstOfEpoch = rt->surfaceSubmittedEpoch != motionEpoch;
        in.camera = pendingTemporalEnd ? rt->neuralCamera : surfaceSource;
        in.cameraSerial = surfaceSource.serial;
        in.s = s;
        in.nativeWaterPass = nativeWaterPass;
        in.haveTex = d.haveTex;
        in.texIsRT = d.tb.isRT;
        in.residentWater = residentScene.waterCount != 0;
        in.texMinimumAlpha = d.tb.minimumAlpha;
        in.reflectionTexture = d.tb.reflectionTexture;
        // From here the target's list belongs to this world, whether or not this draw is eligible (the submission
        // starts it at the first draw that reaches it).
        rt->surfaceSubmittedEpoch = motionEpoch;
        submitSurface(in, d.v, d.count);
    }
}

// The world phase: is this a 3D world draw (rendered into the reduced, jittered render rectangle of the temporal
// upscaler or neural renderer) or a full resolution one? Sets the render pixels per native pixel (dsx, dsy).
void GSR_IMPL::worldPhase(Draw& d) {
    const GsDrawState& s = d.s;
    RT* rt = d.rt;
    const bool zte = d.zte;
    const uint32_t ztst = d.ztst;
    const int sx0 = d.sx0, sy0 = d.sy0, sx1 = d.sx1, sy1 = d.sy1;
    float dsx = (float)scale, dsy = (float)scale; // render pixels per native pixel of this draw
    bool worldDraw = false;
    if constexpr (kCaps.temporalUpscaler)
        if (upActive()) {
            if (d.haveTex && d.tb.isRT && d.tb.rt != rt && d.tb.rt->wstate == 1)
                finalizeWorld(*d.tb.rt); // sampled as a texture: needs its final full resolution content
            const bool frameSized = (sx1 - sx0 + 1) >= 480 && (sy1 - sy0 + 1) >= 320 && rt->w >= 480 && rt->cls == 0;
            const bool depthGE = zte && ztst >= 2;
            const float area = (d.maxx - d.minx) * (d.maxy - d.miny);
            const bool backdrop =
                !(s.flags & DF_SPRITE) && area > 0.2f * (float)(sx1 - sx0 + 1) * (float)(sy1 - sy0 + 1);
            const bool isWorld = frameSized && (depthGE || backdrop);
            worldDraw = upscalePhase(*rt, *d.db, isWorld, depthGE, d.zbp, d.zmax, sx1, sy1);
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
            enterTemporalWorld(*rt, d.zbp, d.zmax, s.xyoffset);
        }
        if (rt->neuralState == 1 && rt->temporalSR) {
            worldDraw = true;
            dsx = (float)rt->wgeom.renderW / rt->wgeom.frameW;
            dsy = (float)rt->wgeom.renderH / rt->wgeom.frameH;
        }
    }
    d.dsx = dsx;
    d.dsy = dsy;
    d.worldDraw = worldDraw;
}

// Debug statistics: GSR_LODSTAT, and the target list of a logged draw.
void GSR_IMPL::drawStatistics(const Draw& d) {
    if (lodStat && d.haveTex && !d.tb.isRT)
        accumulateLod(d.s, d.v, d.count);
    if (d.logThis && d.haveTex && !d.tb.isRT && runtimeFlags().rtList)
        for (auto& r : rts)
            std::fprintf(stderr, "    rt fbp=%u(blk %x) fbw=%u cls=%u psm=%x %ux%u dirty=%d dy=%u..%u lastUse=%llu\n",
                         r->fbp, r->base(), r->fbw, r->cls, r->psm, r->w, r->h, (int)r->dirty, r->dy0, r->dy1,
                         (unsigned long long)r->lastUse);
}

// The ALPHA register and the colour channels the draw writes (FBMSK in target space).
void GSR_IMPL::planWriteMask(Draw& d) {
    const GsDrawState& s = d.s;
    const uint32_t fbmsk = d.fbmsk;
    d.abe = s.flags & DF_ABE;
    d.A = (uint32_t)bitsOf(s.alpha, 0, 2);
    d.B = (uint32_t)bitsOf(s.alpha, 2, 2);
    d.C = (uint32_t)bitsOf(s.alpha, 4, 2);
    d.D = (uint32_t)bitsOf(s.alpha, 6, 2);
    d.FIX = (uint32_t)bitsOf(s.alpha, 32, 8);
    d.is16 = d.rt->cls == 1;
    uint32_t maskBytes = fbmsk; // RGBA byte mask in target space
    if (d.is16) {
        // A 16-bit target masks 5:5:5:1 channels: widen each to the byte the shader compares.
        const uint32_t a = (fbmsk & 0x8000) ? 0xFFu : 0u;
        // only fully-masked 5-bit channels count as byte-masked
        auto full = [&](uint32_t sh) {
            return ((fbmsk >> sh) & 31) == 31 ? 0xFFu : (((fbmsk >> sh) & 31) ? 0x7Fu : 0u);
        };
        maskBytes = full(0) | (full(5) << 8) | (full(10) << 16) | (a << 24);
    }
    d.maskBytes = maskBytes;
    bool partial = false;
    uint8_t writeMask = 0xF;
    for (int c = 0; c < 4; ++c) {
        const uint32_t m = (maskBytes >> (8 * c)) & 0xFF;
        if (m == 0xFF)
            writeMask &= ~(1u << c);
        else if (m != 0)
            partial = true;
    }
    if (d.fpsm == PSM_CT24)
        writeMask &= 0x7; // CT24 has no alpha to write
    d.partial = partial;
    d.writeMask = writeMask;
    inventoryHudFrame.observe(frame, sceneCamera.guiScreen, (sceneCamera.flags & SceneGuideCamera::CentredPage) != 0);
}

// The GUI rules decided by the screen and the texture: the inventory page and its font shadow, the menu and title
// canvas clears, and the loading screen's title tiles. False: the draw is replaced by a wide image already drawn.
bool GSR_IMPL::applyGuiRules(Draw& d) {
    const RT* rt = d.rt;
    const uint8_t writeMask = d.writeMask;
    const bool haveTex = d.haveTex;
    // Alpha-only DATE masks occupy the same page coordinates as RGB.
    // A depth-only clear has no framebuffer writes and stays native.
    d.inventoryPageDraw = inventoryHudFrame.applies(engineWorldOpen, rt->w) && (writeMask & 0xf);
    // The native inventory submits black glyph shadows two native pixels
    // below/right of their foreground. With the sharper host font this is
    // an oversized opaque duplicate. Keep a one-pixel, translucent shadow.
    // Explicit font metadata excludes black panel art, masks and world draws.
    d.inventoryFontShadow = d.inventoryPageDraw && haveTex && d.tb.fontAtlas && d.abe && (writeMask & 7);
    if (d.logThis)
        std::fprintf(stderr, "    ui: screen=%d world=%d inventory=%d bbox=%g,%g..%g,%g scissor=%d,%d..%d,%d\n",
                     sceneCamera.guiScreen, (int)engineWorldOpen, (int)d.inventoryPageDraw, d.minx, d.miny, d.maxx,
                     d.maxy, d.sx0, d.sy0, d.sx1, d.sy1);
    // Current left-anchored policy returns no margins. Retain the guarded
    // canvas path for layouts with margins; never clear the live preview.
    if (d.inventoryPageDraw && (writeMask & 0x7))
        clearInventoryCanvas(*d.rt);
    if (!engineWorldOpen && nativeMenu && haveTex && d.tb.menuArtwork && rt->w >= 480 && (writeMask & 7)) {
        clearInventoryCanvas(*d.rt, true);
        if (wideReplaceFrame == frame && wideReplaceRT == rt)
            return false; // a wide image drew the whole picture
    }
    if (haveTex && d.tb.titleArtwork && rt->w >= 480 && (writeMask & 7)) {
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
    if (sceneCamera.guiScreen >= 0 && sceneCamera.guiScreen != hud::kTitleScreen &&
        sceneCamera.guiScreen != hud::kLoadingScreen)
        titleBackdropAt = {};
    // Loading after the title: the engine redraws the grabbed title page as large tiles (stretched 4:3 art); draw the
    // wide title image instead and skip them. The bar and text are smaller draws and stay.
    if (!engineWorldOpen && nativeMenu && sceneCamera.guiScreen == hud::kLoadingScreen && haveTex &&
        titleBackdropRecent() && (writeMask & 7) && rt->w >= 480 && d.maxx - d.minx >= 120.0f &&
        d.maxy - d.miny >= 120.0f) {
        clearInventoryCanvas(*d.rt, true);
        if (wideReplaceFrame == frame && wideReplaceRT == rt)
            return false;
    }
    return true;
}

// Where the blend runs: the fixed-function blender when it can reproduce the GS blend exactly, else the shader, which
// then reads a copy of the destination (needDest). DATE and partly masked channels also need the destination.
void GSR_IMPL::planBlend(Draw& d) {
    const GsDrawState& s = d.s;
    d.date = (s.test >> 14) & 1;
    d.pabe = s.flags & DF_PABE;
    d.useHw = !d.partial && !d.date && fixedFunctionBlend();
    if (d.abe) {
        if (!draw_blend::fixedForm(s, d.bop, d.sf, d.df))
            d.useHw = false;
        d.bEnable = d.useHw;
    }
    d.needDest = !d.useHw;
}

// The constants block of the draw (PSConsts, read by the pixel program in gs_shaders.h / gs_shaders_wgsl.h).
void GSR_IMPL::buildConstants(Draw& d) {
    const GsDrawState& s = d.s;
    const RT* rt = d.rt;
    const bool haveTex = d.haveTex;
    const TexBind& tb = d.tb;
    PSConsts& k = d.k;
    k.rt[0] = (float)rt->w;
    k.rt[1] = (float)rt->h;
    k.rt[2] = d.zmax;
    // Some CPU-clipped ripple triangles reach the GS after the native water scope closes.
    // The native ripple pass uses a repeating 64x64 CT16 texture and the same masked depth and blend state.
    const uint32_t rippleFlags = DF_TME | DF_FGE | DF_ABE;
    constexpr uint64_t kRippleTest = 0x5002d; // ATE, ATST GREATER, AREF 2, ZTE, ZTST GEQUAL
    constexpr uint64_t kRippleAlpha = 0x44;   // (Cs - Cd) * As + Cd
    constexpr uint64_t kRippleTexSize = 0x66; // TEX0 TW = TH = 6 (64x64)
    const bool lateWaterRipple = waterWorldCamera.flags & SceneGuideCamera::World && haveTex && !tb.isRT &&
                                 (s.flags & (rippleFlags | DF_FST | DF_SPRITE | DF_LINE | DF_POINT)) == rippleFlags &&
                                 s.test == kRippleTest && s.alpha == kRippleAlpha && (s.clamp & 0xf) == 0 &&
                                 bitsOf(s.tex0, 20, 6) == PSM_CT16 && bitsOf(s.tex0, 26, 8) == kRippleTexSize &&
                                 bitsOf(s.tex0, 34, 3) == 1;
    // The shore and water can cover the same plane with different triangle edges. The GS rounds their raster
    // coordinates to sixteenths of a pixel before interpolation, so the depth error can exceed a few Z units.
    // Bias only the water colour pass toward the far plane. The depth buffer and ray geometry keep their depths.
    if ((nativeWaterPass || lateWaterRipple) && d.abe && d.zmsk && d.zte && d.ztst >= 2)
        k.rt[2] *= 1.0f + 0x1p-11f;
    k.rt[3] = d.dsx;
    uint32_t fl = 0;
    if (d.tme && haveTex)
        fl |= PF_TME;
    if (s.flags & DF_FGE)
        fl |= PF_FGE;
    if (s.flags & DF_FST)
        fl |= PF_FST;
    if (bitsOf(s.tex0, 34, 1))
        fl |= PF_TCC;
    fl |= (uint32_t)bitsOf(s.tex0, 35, 2) << 4;
    d.ate = (s.test & 1);
    d.atst = (uint32_t)bitsOf(s.test, 1, 3);
    d.afail = (uint32_t)bitsOf(s.test, 12, 2);
    if (d.ate)
        fl |= PF_ATE;
    fl |= d.atst << 7;
    fl |= d.afail << 10;
    if (d.date)
        fl |= PF_DATE;
    if ((s.test >> 15) & 1)
        fl |= PF_DATM;
    if (d.abe)
        fl |= PF_ABE;
    if (d.pabe)
        fl |= PF_PABE;
    if (s.flags & DF_DTHE)
        fl |= PF_DTHE;
    if (s.flags & DF_COLCLAMP)
        fl |= PF_CCLAMP;
    if (s.fba)
        fl |= PF_FBA;
    if (d.is16)
        fl |= PF_FB16;
    if (haveTex && tb.isRT)
        fl |= PF_TEXRT;
    k.a[0] = fl;
    k.a[1] = (uint32_t)bitsOf(s.test, 4, 8);
    k.a[2] = d.A | (d.B << 2) | (d.C << 4) | (d.D << 6);
    k.a[3] = d.FIX;
    k.b[0] = d.needDest ? d.maskBytes : 0;
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
    // TEX1.K: the LOD bias, a signed 12-bit fixed-point number with 4 fraction bits
    int32_t kk = (int32_t)bitsOf(s.tex1, 32, 12);
    if (kk & 0x800)
        kk -= 0x1000;
    k.d[0] = (haveTex && !tb.isRT ? tb.mxl : 0) | (lcm << 4) | (mmag << 5) | (mmin << 6) | (L << 9);
    if (hdMips && haveTex && !tb.isRT && tb.factor > 1 && mxl > 0 && mmin >= 2)
        k.d[0] |= 1u << 13; // hd_texture_mips: the level is chosen on the copy's own texels (gs_shaders.h)
    if (cfg.hardwareTextureFilter && haveTex && !tb.isRT)
        k.d[0] |= 1u << 14; // hardware_texture_filter: bilinear taps by the texture unit (gs_shaders.h sampleLvlHw)
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
    k.f[0] = (haveTex && !tb.isRT) ? (float)tb.factor : 1.0f;
    k.f[1] = mipBias;
    k.f[2] = (float)aniso;
    k.f[3] = std::log2(k.f[0]);
    // world draws: footprint LOD is measured in (fewer) render pixels; the bias is upscale::lodBias of the plan
    // (RT::wgeom, 0 on a backend whose world is drawn at full resolution)
    k.g[0] = userMipBias + (d.worldDraw ? rt->wgeom.lodBias : 0.0f);
    if (d.worldDraw) // sub-pixel jitter in NDC (render pixels -> NDC; the viewport spans the whole target)
    {
        k.g[1] = 2.0f * rt->wjx / (float)(rt->w * d.dsx);
        k.g[2] = -2.0f * rt->wjy / (float)(rt->h * d.dsy);
    }
    k.h[0] = d.worldDraw ? rt->wjx : 0;
    k.h[1] = d.worldDraw ? rt->wjy : 0;
    k.h[2] = 16384;
    // Keep an originally point-sampled UI sprite inside its native atlas cell even when the player forces bilinear.
    // Compute the endpoints before applying HUD scaling.
    if (d.clampSpriteFootprint && d.count == 6) {
        const auto b = sprite_footprint::bounds(d.v, d.count, (s.flags & DF_FST) != 0, (float)k.b[1], (float)k.b[2],
                                                tb.isRT ? (unsigned)scale : tb.factor);
        if (b.valid) {
            k.a[0] |= 1u << 24;
            k.spriteBounds[0] = b.u0;
            k.spriteBounds[1] = b.v0;
            k.spriteBounds[2] = b.u1;
            k.spriteBounds[3] = b.v1;
        }
    }
}

// The optional per-draw detail that adds to the constants: terrain layers, object motion vectors, and the sampling
// rectangle of a render target texture.
void GSR_IMPL::prepareDrawDetail(Draw& d) {
    const GsDrawState& s = d.s;
    RT* rt = d.rt;
    PSConsts& k = d.k;
    // Terrain layers: world-space shading and optional side projection of steep faces.
    if constexpr (kCaps.terrainDetail)
        k.a[0] |= prepareTerrain(s, d.v, d.count, d.haveTex, d.haveTex && d.tb.isRT, *rt, d.zmsk, d.dsx, d.dsy, k);
    if constexpr (kCaps.motionVectors) {
        CpuProfile::Scope motionProfile(cpuProfile, CpuProfile::DrawObjectMotion);
        // [vertex programs] a batch of records has its own history and previous stream
        const VertexProgramBatch* records = nullptr;
        if constexpr (kCaps.vertexPrograms)
            records = programBatch;
        if (!records)
            d.drawMotion = prepareObjectMotion(*rt, s, d.v, d.count, d.dsx, d.dsy);
        else if constexpr (kCaps.vertexPrograms)
            d.drawMotion = prepareProgramMotion(*rt, s, *records, d.dsx, d.dsy);
    }
    if (d.haveTex && d.tb.isRT) {
        const TexBind& tb = d.tb;
        k.src[0] = tb.ox;
        k.src[1] = tb.oy;
        // A world image still in its reduced render rectangle is sampled through that rectangle.
        const bool reduced = tb.rt->wstate == 1 || (tb.rt->neuralState == 1 && tb.rt->temporalSR);
        k.src[2] = 1.0f / (float)tb.rt->w *
                   (reduced ? (float)tb.rt->wgeom.renderW / (float)tb.rt->wgeom.frameW / (float)scale : 1.0f);
        k.src[3] = 1.0f / (float)tb.rt->h *
                   (reduced ? (float)tb.rt->wgeom.renderH / (float)tb.rt->wgeom.frameH / (float)scale : 1.0f);
    }
}

void GSR_IMPL::logConstants(const Draw& d) {
    if (!d.logThis)
        return;
    const PSConsts& k = d.k;
    std::fprintf(stderr,
                 "    consts: haveTex=%d isRT=%d useHw=%d needDest=%d bEnable=%d fl=%08x aref/ate=%u abcd=%02x "
                 "fix=%u maskB=%08x tex=%ux%u wm=%u clampc=%u,%u,%u,%u d0=%08x e=(%.2f,%.1f) fba=%u fbmsk=%08x "
                 "src=(%.1f,%.1f,%g,%g) rtdim=%ux%u\n",
                 (int)d.haveTex, (int)(d.haveTex && d.tb.isRT), (int)d.useHw, (int)d.needDest, (int)d.bEnable, k.a[0],
                 k.a[1], k.a[2], k.a[3], k.b[0], k.b[1], k.b[2], k.b[3], k.c[0], k.c[1], k.c[2], k.c[3], k.d[0], k.e[0],
                 k.e[1], (unsigned)d.s.fba, d.fbmsk, k.src[0], k.src[1], k.src[2], k.src[3], d.rt->w, d.rt->h);
}

// The vertices: a writable copy (the HUD / menu maps rewrite positions), handed to the GPU by endVertices. Fixes for
// native quirks first, then the GUI classification and the HUD maps.
void GSR_IMPL::mapVertices(Draw& d) {
    const GsDrawState& s = d.s;
    const DrawVertex* v = d.v;
    const uint32_t count = d.count;
    const RT* rt = d.rt;
    const PSConsts& k = d.k;
    const bool haveTex = d.haveTex;
    CpuProfile::Scope uploadProfile(cpuProfile, CpuProfile::DrawVertexUpload);
    DrawVertex* const mapped = beginVertices(v, count);
    // The guest clipper can truncate a vertex on the right plane into the final 12.4 subpixels.
    // At high internal scales that can exclude a raster sample and expose the sky behind a wall.
    const float rightEdge = static_cast<float>(rt->w);
    constexpr float clipGuard = 2.0f / 16.0f;
    if (engineWorldOpen && d.zte && d.ztst >= 2 && !(s.flags & (DF_SPRITE | DF_LINE | DF_POINT)) && d.sx0 == 0 &&
        d.sx1 + 1 == static_cast<int>(rt->w) && d.maxx >= rightEdge - clipGuard) {
        for (uint32_t i = 0; i < count; ++i) {
            float& x = mapped[i].x;
            if (x >= rightEdge - clipGuard && x < rightEdge)
                x = rightEdge;
        }
    }
    if (d.inventoryFontShadow) {
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
        constexpr uint64_t kAdditiveAlpha = 0x48; // ALPHA (Cs - 0) * As + Cd
        constexpr uint64_t kSize128Log2 = 7;      // TEX0 TW = TH = 7 (128x128)
        const uint32_t hpsm = (uint32_t)bitsOf(s.tex0, 20, 6);
        if (haveTex && (s.flags & DF_SPRITE) && (s.flags & DF_ABE) && (s.alpha & 0xFF) == kAdditiveAlpha &&
            bitsOf(s.tex0, 26, 4) == kSize128Log2 && bitsOf(s.tex0, 30, 4) == kSize128Log2 &&
            (hpsm == PSM_T8 || hpsm == PSM_CT16) && (s.flags & DF_FST) == 0 && count % 3 == 0) {
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
    classifyHudDraw(d);
    mapHudVertices(d, mapped);
    logHudCorners(d);
    endVertices();
}

// Which GUI map the draw's vertices take: the inventory page, a menu page, a gameplay HUD corner, or the legacy HUD
// cluster map. A perspective triangle is world geometry whatever the phase.
void GSR_IMPL::classifyHudDraw(Draw& d) {
    const GsDrawState& s = d.s;
    const DrawVertex* v = d.v;
    const uint32_t count = d.count;
    const RT* rt = d.rt;
    const bool haveTex = d.haveTex;
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
    d.inventoryMap = hud::inventoryMap(rt->w, hudScale, inventoryHudFrame.centred);
    // The map view's world shade stays unmapped: it covers the whole output, not the centred 4:3 window.
    d.inventoryShade =
        d.inventoryPageDraw &&
        hud::inventoryViewShade(inventoryHudFrame.centred, !haveTex || (d.k.b[1] <= 32 && d.k.b[2] <= 32), d.minx,
                                d.miny, d.maxx, d.maxy, (float)rt->w, (float)std::min(rt->h, 512u));
    d.menuMap = hud::menuMap(rt->w, hudScale);
    // Corner pieces may be RT-backed triangle lists with depth state unlike
    // their rings, including a retained FGE bit. Once the engine has closed
    // the world, classify each triangle before legacy UI state exclusions.
    bool menuPageDraw = hud::menuSubmission(engineWorldOpen, hud::nativeMenuDraw(nativeMenu, sceneCamera.guiScreen),
                                            rt->w, rt->h, (d.writeMask & 0x7) != 0, perspectiveTriangle);
    if (menuPageDraw && hud::pauseHudDraw(hud::pauseScreen(sceneCamera.guiScreen, nativeMenu, nativeMenuScreen), d.minx,
                                          d.miny, d.maxx, d.maxy, (float)rt->w, (float)std::min(rt->h, 512u)))
        menuPageDraw = false;
    if (menuPageDraw && hud::pauseScreen(sceneCamera.guiScreen, nativeMenu, nativeMenuScreen)) {
        // GSR_PAUSE_HUD_LOG=1: the scale each frame's first pause page draw gets (hud_page.h pauseTransitionHudScale).
        static const bool pauseLog = [] {
            const char* e = std::getenv("GSR_PAUSE_HUD_LOG");
            return e && e[0] == '1';
        }();
        static uint64_t pauseLogFrame = ~uint64_t{0};
        if (pauseLog && pauseLogFrame != frame) {
            pauseLogFrame = frame;
            std::fprintf(
                stderr,
                "[pause-hud] frame %llu: pause page drawn at hud scale %.3f (camera %d, host menu %d screen %d%s)\n",
                (unsigned long long)frame, (double)hudScale, sceneCamera.guiScreen, nativeMenu ? 1 : 0,
                nativeMenuScreen, pauseHud.active ? ", override" : "");
        }
    }
    d.menuPageDraw = menuPageDraw;
    d.cornerHudDraw = !menuPageDraw && !perspectiveTriangle &&
                      hud::gameplayCornerSubmission(engineWorldOpen, d.inventoryPageDraw, rt->w, rt->h);
    d.legacyHudDraw =
        !hudWorldTriangle && ((s.flags & DF_SPRITE) || (d.zmsk && d.zte && d.ztst == 1)) && !(d.zte && d.ztst >= 2);
    d.hudClipX0 = (float)d.sx0;
    d.hudClipY0 = (float)d.sy0;
    d.hudClipX1 = (float)(d.sx1 + 1);
    d.hudClipY1 = (float)(d.sy1 + 1);
}

// HUD de-stretch / resize (anamorphic widescreen, hud_scale): a UI sprite drawn into the full display-sized frame is
// moved by the map of its GUI class, one range (a sprite, a triangle, or the whole draw) at a time. Widens the dirty
// rectangle (cx0..cy1) to the moved vertices.
void GSR_IMPL::mapHudVertices(Draw& d, DrawVertex* dv) {
    const GsDrawState& s = d.s;
    const uint32_t count = d.count;
    const RT* rt = d.rt;
    const int sx0 = d.sx0, sy0 = d.sy0, sx1 = d.sx1, sy1 = d.sy1;
    const float cornerFrameW = (float)rt->w;
    const float cornerFrameH = (float)std::min(rt->h, 512u); // the native display is at most 512 lines high
    if (!(!engineWorldOpen &&
          (hudScale < 0.999f || std::fabs(hudSize - 1.0f) > 0.001f || d.inventoryFontShadow ||
           sceneCamera.guiScreen == hud::kLoadingScreen) &&
          ((d.writeMask & 0x7) || d.inventoryPageDraw) &&
          (d.inventoryPageDraw || d.menuPageDraw || d.cornerHudDraw || d.legacyHudDraw)))
        return;
    // Sprites are grouped into clusters (overlapping / touching UI sprites of the previous frame, e.g. an orb's fill,
    // ring and highlights), and a cluster is anchored by its bounding box: x' = a + (x - a) * k * size,
    // y' = ay + (y - ay) * size, a = nearest screen edge / centre. Every sprite of a cluster uses the same map, so
    // the pieces stay together and edge anchored elements stay on screen.
    const float frameW = (float)(sx1 - sx0 + 1), frameH = (float)(sy1 - sy0 + 1);
    if (!(d.inventoryPageDraw || d.menuPageDraw || d.cornerHudDraw ||
          (frameW >= 480.0f && frameH >= 320.0f && rt->w >= 480)))
        return;
    const HudFrame f{dv, (float)sx0, (float)sy0, frameW, frameH, cornerFrameW, cornerFrameH};
    // one range = one sprite (6 vertices) of a batch, or one triangle, or the whole draw for triangle quads
    if ((s.flags & DF_SPRITE) && count % 6 == 0)
        for (uint32_t b = 0; b < count; b += 6)
            mapHudRange(d, f, b, b + 6);
    else if ((d.menuPageDraw ||
              (d.cornerHudDraw && hud::splitCornerBatch(d.minx, d.miny, d.maxx, d.maxy, cornerFrameW, cornerFrameH))) &&
             count % 3 == 0)
        for (uint32_t b = 0; b < count; b += 3)
            mapHudRange(d, f, b, b + 3);
    else
        mapHudRange(d, f, 0, count);

    // Shader blending/DATE reads destination pixels at the moved
    // raster positions. Retain conservative original bounds too,
    // but include every transformed vertex in snapshot/dirty rows.
    const bool pageMapped = d.inventoryPageDraw && !d.inventoryShade;
    const int hudClipLeft = (int)std::floor(pageMapped ? d.inventoryMap.x((float)sx0) : d.hudClipX0);
    const int hudClipRight = (int)std::ceil(pageMapped ? d.inventoryMap.x((float)(sx1 + 1)) : d.hudClipX1) - 1;
    for (uint32_t i = 0; i < count; ++i) {
        d.cx0 = std::min(d.cx0, std::max(std::max(hudClipLeft, 0), (int)std::floor(dv[i].x)));
        d.cy0 = std::min(d.cy0, std::max(std::max((int)std::floor(d.hudClipY0), 0), (int)std::floor(dv[i].y)));
        d.cx1 = std::max(d.cx1, std::min(std::min(hudClipRight, (int)rt->w - 1), (int)std::ceil(dv[i].x)));
        d.cy1 = std::max(d.cy1,
                         std::min(std::min((int)std::ceil(d.hudClipY1) - 1, (int)rt->h - 1), (int)std::ceil(dv[i].y)));
    }
}

// The HUD map of one range [b, e) of the draw's vertices. The first rule that claims the range wins: subtitles, the
// inventory page, the tutorial band, a gameplay corner, a menu page, then the legacy cluster map.
void GSR_IMPL::mapHudRange(Draw& d, const HudFrame& f, uint32_t b, uint32_t e) {
    const GsDrawState& s = d.s;
    const DrawVertex* v = d.v;
    const RT* rt = d.rt;
    const PSConsts& k = d.k;
    const bool haveTex = d.haveTex;
    const int sx0 = d.sx0, sy0 = d.sy0, sx1 = d.sx1, sy1 = d.sy1;
    DrawVertex* dv = f.dv;
    const float fx0 = f.fx0, fy0 = f.fy0, frameW = f.frameW, frameH = f.frameH;
    const float cornerFrameW = f.cornerW, cornerFrameH = f.cornerH;
    auto anchor = [](float lo, float hi, float f0, float F) {
        const float t = (0.5f * (lo + hi) - f0) / F;
        return t < 0.4f ? f0 : (t > 0.6f ? f0 + F : f0 + 0.5f * F);
    };
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
        const bool band =
            solid && hi - lo >= .85f * cornerFrameW && top >= .6f * cornerFrameH && bottom - top < .4f * cornerFrameH;
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
            d.hudClipX0 = std::min(d.hudClipX0, map.x((float)sx0));
            d.hudClipX1 = std::max(d.hudClipX1, map.x((float)(sx1 + 1)));
            d.hudClipY0 = std::min(d.hudClipY0, map.y((float)sy0));
            d.hudClipY1 = std::max(d.hudClipY1, map.y((float)(sy1 + 1)));
            st.hudSprites++;
            return;
        }
    }
    // Every post-world inventory component shares this exact
    // native-coordinate map: RT fills, rings, tiled panels,
    // clipped text, and preview geometry. No source/size
    // heuristic proves that a draw is a world snapshot, so
    // none is exempted here. Keep page Y and size native.
    if (d.inventoryPageDraw) {
        for (uint32_t i = b; i < e && !d.inventoryShade; ++i) {
            dv[i].x = d.inventoryMap.x(dv[i].x);
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
    if (!d.menuPageDraw && d.legacyHudDraw && hud::tutorialBand(y0, y1, cornerFrameH) &&
        (hud::tutorialBandDraw(d.minx, d.maxx, cornerFrameW) || tutorialBandRows.covers(y1))) {
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
        d.cornerHudDraw ? hud::gameplayCorner(x0, y0, x1, y1, cornerFrameW, cornerFrameH) : hud::Corner::None;
    if (corner != hud::Corner::None) {
        (corner == hud::Corner::Left ? d.cornerLeft : d.cornerRight) += (e - b) / 3;
        const auto map = hud::cornerMap(corner, cornerFrameW, cornerFrameH, hudScale, hudSize);
        for (uint32_t i = b; i < e; ++i) {
            dv[i].x = map.x(dv[i].x);
            dv[i].y = map.y(dv[i].y);
        }
        // One draw can contain both corners. Conservatively
        // cover both mapped clip rectangles for destination reads.
        d.hudClipX0 = std::min(d.hudClipX0, map.x((float)sx0));
        d.hudClipY0 = std::min(d.hudClipY0, map.y((float)sy0));
        d.hudClipX1 = std::max(d.hudClipX1, map.x((float)(sx1 + 1)));
        d.hudClipY1 = std::max(d.hudClipY1, map.y((float)(sy1 + 1)));
        st.hudSprites++;
        return;
    }
    if (d.cornerHudDraw)
        d.cornerUnmapped += (e - b) / 3;
    const float w = x1 - x0, h = y1 - y0;
    const bool tile128 = w >= 127.0f && h >= 127.0f && std::fmod(x0 - fx0 + 0.5f, 128.0f) < 1.0f &&
                         std::fmod(y0 - fy0 + 0.5f, 128.0f) < 1.0f && std::fabs(std::fmod(w + 0.5f, 128.0f)) < 1.0f &&
                         std::fabs(std::fmod(h + 0.5f, 128.0f)) < 1.0f; // screen-grab tiles redrawn as backdrop
    if (d.menuPageDraw) {
        // Menu foreground candidates share one centered map;
        // never inherit gameplay cluster anchors or HUD size.
        // Captured world tiles and full-page backdrops fill the
        // selected aspect. The guest's UI projection stays native.
        const bool columnBackdrop =
            hud::menuColumnBackdrop(x0, y0, x1, y1, (float)rt->w, haveTex && (s.flags & DF_SPRITE));
        const bool authoredArt = haveTex && d.tb.menuArtwork;
        if (authoredArt || hud::menuForeground(w, (float)rt->w, tile128 || columnBackdrop)) {
            // The loading screen's packet carries the horizontal offset of its art in its payload word.
            float loadingOffset = 0;
            if (isLoadingScreenPacket(sceneCamera)) {
                std::memcpy(&loadingOffset, &sceneCamera.reserved, sizeof(loadingOffset));
                if (!std::isfinite(loadingOffset) || std::fabs(loadingOffset) > 256)
                    loadingOffset = 0;
            }
            for (uint32_t i = b; i < e; ++i)
                dv[i].x = d.menuMap.x(dv[i].x - loadingOffset);
            d.hudClipX0 = std::min(d.hudClipX0, d.menuMap.x((float)sx0));
            d.hudClipX1 = std::max(d.hudClipX1, d.menuMap.x((float)(sx1 + 1)));
            st.hudSprites++;
        }
        return;
    }
    if (!d.legacyHudDraw)
        return;
    // the sky dome (and similar big backdrops) is drawn with ZTST ALWAYS / ZMSK like UI quads when the
    // camera looks up: a large triangle draw is not UI (it would be squeezed into a 4:3 rectangle and
    // merge the HUD clusters into one centred 'page') a draw that samples a render target (screen grab
    // / full-frame effect) is UI only when it is small: the HP / mana orb gem is textured from memory
    // that aliases a render target while the camera looks up at the sky, and must move with its ring
    const bool rtGrab = haveTex && d.tb.isRT && (w > 0.3f * frameW || h > 0.3f * frameH);
    const bool bigBackdrop = rtGrab || (!(s.flags & DF_SPRITE) && w * h > 0.2f * frameW * frameH);
    if (w < 0.85f * frameW && !tile128 && !bigBackdrop) {
        if (hudCur.size() < 600)
            hudCur.push_back({x0, y0, x1, y1});
        HudBox c{x0, y0, x1, y1};
        const float mx = 0.5f * (x0 + x1), my = 0.5f * (y0 + y1);
        float best = 1e30f;
        for (const HudBox& bx : hudPrev)
            if (mx >= bx.x0 && mx <= bx.x1 && my >= bx.y0 && my <= bx.y1 && (bx.x1 - bx.x0) * (bx.y1 - bx.y0) < best) {
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
    } else if (!tile128 && !rtGrab && h < 0.4f * frameH && w >= 0.85f * frameW && std::fabs(hudSize - 1.0f) > 0.001f) {
        // full-width band (subtitle bar): resized vertically about the nearest vertical edge
        const float ay = anchor(y0, y1, fy0, frameH);
        for (uint32_t i = b; i < e; ++i)
            dv[i].y = ay + (dv[i].y - ay) * hudSize;
    }
}

// Opt-in and bounded per renderer. Counts partition the submitted
// triangles; correlate draw/flags/source/bounds to verify every ring
// piece rather than inferring success from one mapped fill.
void GSR_IMPL::logHudCorners(const Draw& d) {
    const GsDrawState& s = d.s;
    if (runtimeFlags().hudCornerLog && d.cornerHudDraw && (d.writeMask & 0x7) &&
        (hudScale < 0.999f || std::fabs(hudSize - 1.0f) > 0.001f) &&
        (d.cornerLeft || d.cornerRight || (s.flags & DF_FGE)) && hudCornerLogLines < 128 &&
        frame >= runtimeFlags().hudCornerFrom) {
        ++hudCornerLogLines;
        std::fprintf(stderr,
                     "[hud-corner] f=%llu draw=%lld flags=%03x n=%u rtSource=%d world=%d page=%d target=%ux%u "
                     "native=(%g,%g)..(%g,%g) mappedL=%u mappedR=%u unmapped=%u scale=%g,%g\n",
                     (unsigned long long)frame, (long long)d.index, s.flags, d.count, (int)(d.haveTex && d.tb.isRT),
                     (int)motionWorldOpen, (int)d.inventoryPageDraw, d.rt->w, d.rt->h, d.minx, d.miny, d.maxx, d.maxy,
                     d.cornerLeft, d.cornerRight, d.cornerUnmapped, hudScale * hudSize, hudSize);
    }
}

// Submit: the destination / feedback snapshot, the bindings, the colour (and alpha-failure) passes with their motion
// vectors, and what the target remembers afterwards.
void GSR_IMPL::submitDraw(Draw& d) {
    CpuProfile::Scope submitProfile(cpuProfile, CpuProfile::DrawSubmit);
    GpuView texSrv = nullptr, destSrv = nullptr;
    snapshotDestination(d, texSrv, destSrv);
    bindDrawTargets(d, texSrv, destSrv);
    runDrawPasses(d);
    noteTargetWritten(d);
    dumpTargetAfterDraw(d);
}

// The views the draw samples: its texture (a render target it samples is read through a copy when it is also the
// draw's own target) and, for a shader blend, a copy of the destination.
void GSR_IMPL::snapshotDestination(Draw& d, GpuView& texSrv, GpuView& destSrv) {
    RT* rt = d.rt;
    const TexBind& tb = d.tb;
    const bool haveTex = d.haveTex;
    destSrv = nullptr;
    texSrv = haveTex ? tb.srv : nullptr;
    const bool feedback = haveTex && tb.isRT && tb.rt == rt;
    uint32_t box[4] = {(uint32_t)std::floor(d.cx0 * d.dsx), (uint32_t)std::floor(d.cy0 * d.dsy),
                       (uint32_t)std::ceil((d.cx1 + 1) * d.dsx),
                       (uint32_t)std::ceil((d.cy1 + 1) * d.dsy)}; // x0 y0 x1 y1
    box[2] = std::min<uint32_t>(box[2], rt->w * scale);
    box[3] = std::min<uint32_t>(box[3], rt->h * scale);
    {
        CpuProfile::Scope destProfile(cpuProfile, CpuProfile::DrawDest);
        if (feedback) {
            destSrv = snapshot(*rt, nullptr);
            texSrv = destSrv;
            if (d.needDest)
                destSrv = rt->copyView();
        } else {
            if (haveTex && tb.isRT)
                texSrv = tb.rt->view();
            if (d.needDest)
                destSrv = snapshot(*rt, box);
        }
    }
    if (haveTex && tb.isRT && !feedback)
        tb.rt->lastUse = frame;
    if (d.needDest)
        st.shaderBlendDraws++;
    else
        st.hwBlendDraws++;
}

// Targets, programs, textures, viewport and scissor (the scissor follows the HUD maps of the vertex phase).
void GSR_IMPL::bindDrawTargets(Draw& d, GpuView texSrv, GpuView destSrv) {
    RT* rt = d.rt;
    const float dsx = d.dsx, dsy = d.dsy;
    const int sx0 = d.sx0, sy0 = d.sy0, sx1 = d.sx1, sy1 = d.sy1;
    int32_t sr[4] = {(int32_t)std::floor(sx0 * dsx), (int32_t)std::floor(sy0 * dsy),
                     (int32_t)std::ceil((sx1 + 1) * dsx), (int32_t)std::ceil((sy1 + 1) * dsy)}; // left top right bottom
    if (d.inventoryPageDraw && !d.inventoryShade) {
        sr[0] = std::max<int32_t>(0, (int32_t)std::floor(d.inventoryMap.x((float)sx0) * dsx));
        sr[2] = std::max<int32_t>(0, (int32_t)std::ceil(d.inventoryMap.x((float)(sx1 + 1)) * dsx));
    } else if (d.cornerHudDraw || d.menuPageDraw) {
        sr[0] = std::max<int32_t>(0, (int32_t)std::floor(d.hudClipX0 * dsx));
        sr[1] = std::max<int32_t>(0, (int32_t)std::floor(d.hudClipY0 * dsy));
        sr[2] = (int32_t)std::ceil(d.hudClipX1 * dsx);
        sr[3] = (int32_t)std::ceil(d.hudClipY1 * dsy);
    }
    sr[2] = std::min<int32_t>(sr[2], rt->w * scale);
    sr[3] = std::min<int32_t>(sr[3], rt->h * scale);
    {
        CpuProfile::Scope bindProfile(cpuProfile, CpuProfile::DrawBind);
        bindDraw(*rt, *d.db, d.useHw, texSrv, destSrv, (float)rt->w * dsx, (float)rt->h * dsy, sr,
                 d.haveTex && d.tb.isRT, d.k.a[0]);
    }
}

// The passes: the colour pass, then (when the alpha test keeps some channels of failing pixels: TEST.AFAIL) a
// second pass for the failing pixels, each followed by its motion vectors.
void GSR_IMPL::runDrawPasses(Draw& d) {
    RT* rt = d.rt;
    PSConsts& k = d.k;
    const uint8_t writeMask = d.writeMask;
    const bool zmsk = d.zmsk;
    const uint32_t count = d.count;
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
        materialDraw = d.haveTex && d.tb.materialSrv &&
                       prepareMaterial(d.s, d.tb.materialSrv, d.tb.materialClass, d.haveTex, d.tb.isRT, *rt,
                                       d.zte && d.ztst >= 2, k.a[0], d.dsx, d.dsy, d.useHw);
    // [vertex programs] the batch's own vertices and vertex program replace the stand-ins bindDraw has bound
    const VertexProgramBatch* records = nullptr;
    if constexpr (kCaps.vertexPrograms) {
        records = programBatch;
        if (records)
            bindProgramDraw(d.s, *records);
    }

    // TEST.ZTST: never, always, greater-equal, greater (the GS depth grows toward the camera)
    rhi::Compare zf = rhi::Compare::Always;
    if (d.zte) {
        static const rhi::Compare fm[4] = {rhi::Compare::Never, rhi::Compare::Always, rhi::Compare::GreaterEqual,
                                           rhi::Compare::Greater};
        zf = fm[d.ztst];
    }
    auto setup = [&](uint32_t passMode, uint8_t wm, bool zw) {
        k.d[1] = passMode;
        rhi::BlendState blend;
        blend.enable = alpha::blendForPass(d.bEnable, wm);
        blend.op = d.bop;
        blend.src = d.sf;
        blend.dst = d.df;
        blend.writeMask = wm;
        setPass(k, blend, {true, zf, zw});
    };
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
            if (!d.drawMotion || !(wm & 7))
                return;
            CpuProfile::Scope velocityProfile(cpuProfile, CpuProfile::DrawVelocity);
            // GREATER has ambiguous coplanar replays after a depth write. Do
            // not assert object correspondence for those pixels.
            k.h[3] = (wm & 7) == 7 && !(zw && zf == rhi::Compare::Greater) ? 1.0f : 0.0f;
            setup(passMode, wm, zw);
            if (!records)
                drawVelocity(*rt, *d.db, count, zw ? rhi::Compare::Equal : zf, restore, materialDraw, d.useHw);
            else if constexpr (kCaps.vertexPrograms) // [vertex programs]
                drawProgramVelocity(*rt, *d.db, *records, zw ? rhi::Compare::Equal : zf, restore, materialDraw,
                                    d.useHw);
            // The next pass's setup() uploads its constants and blend/depth state.
        }
    };

    // pass 0
    const bool failPass = d.ate && d.afail != 0 && d.atst != 1; // ATST 1 = ALWAYS: no pixel fails
    {
        CpuProfile::Scope passProfile(cpuProfile, CpuProfile::DrawPass);
        setup(0, writeMask, !zmsk);
        drawPass();
    }
    velocityPass(0, writeMask, !zmsk, failPass);
    if (failPass) {
        const auto fail = alpha::failurePass(d.afail, writeMask, !zmsk);
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
        st.drawCalls += d.joinKeptCount; // as many as the segments' own draws
        st.joinedDraws++;
        st.joinedPasses += d.joinKeptCount;
        joinDraws.clear();
    } else
        st.drawCalls++;
}

// What the target remembers after the draw: its dirty rows (read back when the game reads that memory), the solid
// colour mirror, and the readback prefetch.
void GSR_IMPL::noteTargetWritten(Draw& d) {
    const GsDrawState& s = d.s;
    RT* rt = d.rt;
    const uint8_t writeMask = d.writeMask;
    // Constant-colour sprites into a clean scratch target are written to memory as well, so the game's next read of
    // those blocks needs no GPU readback (one blocking readback per gameplay frame without it): gs_solid_mirror.h.
    const auto mirrored = planSolidMirror(
        s, d.v, d.count, d.haveTex, *rt, uploadActive,
        d.dsx == scale && d.dsy == scale && d.k.h[0] == 0 && d.k.h[1] == 0 && writeMask == 15, rts,
        [&](const RT& other, uint32_t y0, uint32_t y1, uint32_t& lo, uint32_t& hi) { rowSpan(other, y0, y1, lo, hi); });
    if (writeMask) {
        ++rt->writes;
        rt->drawFrame = frame;
        if (!rt->dirty) {
            rt->dirty = true;
            rt->dy0 = (uint32_t)d.cy0;
            rt->dy1 = (uint32_t)d.cy1 + 1;
        } else {
            rt->dy0 = std::min(rt->dy0, (uint32_t)d.cy0);
            rt->dy1 = std::max(rt->dy1, (uint32_t)d.cy1 + 1);
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
                         rt->dy0, rt->dy1, (unsigned long long)s.scissor, writeMask, unsigned(d.zmsk));
            logDraw(s, d.v, d.count);
        }
        if (writeMask)
            prefetchReadback(*rt);
    }
}

// debug: GSR_RTDUMP=<draw>[,<draw>...] writes the scissor area of the target as rtdump_<draw>.png
// (GSR_RTDUMP_DIR, default cwd) right after that draw
void GSR_IMPL::dumpTargetAfterDraw(const Draw& d) {
    const int64_t di = d.index;
    if (!((!rtDump.empty() && rtDump.count(di)) ||
          (rtDumpFrame >= 0 && (int64_t)frame >= rtDumpFrame && (int64_t)frame <= rtDumpFrameLast)))
        return;
    RT* rt = d.rt;
    const int sy1 = d.sy1;
    Image im;
    if (readTarget(*rt, rt->w * scale,
                   std::min<uint32_t>(rt->h, std::max<uint32_t>((uint32_t)(sy1 + 1), dumpRows)) * scale, im)) {
        const char* dir = runtimeFlags().rtDumpDir.c_str();
        char name[512];
        std::snprintf(name, sizeof(name), "%s/rtdump_%lld.png", dir, (long long)di);
        writePng(name, im);
        if (readTarget(*rt, rt->w * scale,
                       std::min<uint32_t>(rt->h, std::max<uint32_t>((uint32_t)(sy1 + 1), dumpRows)) * scale, im,
                       true)) {
            for (size_t i = 0; i < im.rgba.size(); i += 4) {
                im.rgba[i] = im.rgba[i + 1] = im.rgba[i + 2] = im.rgba[i + 3];
                im.rgba[i + 3] = 255;
            }
            std::snprintf(name, sizeof(name), "%s/rtdump_%lld_alpha.png", dir ? dir : ".", (long long)di);
            writePng(name, im);
        }
    }
}

// ---- ray-surface submissions (surface_builder.h) ----
//
// A draw of ordinary vertices goes to the builder at once, unless a vertex-program batch before it is still waiting for
// its vertices on the GPU: then it waits behind it, so the builder takes the draws in their order (its camera, its
// lists and the resident material updates depend on that order). flushSurfaces hands the waiting draws over; every
// reader of the builder's results calls drainSurfaces, which flushes first.

void GSR_IMPL::submitSurface(const surface::DrawInput& in, const DrawVertex* v, uint32_t count) {
    if constexpr (kCaps.vertexPrograms)
        if (programBatch) {
            submitProgramSurface(in, *programBatch); // the three stand-ins are no geometry: the batch's own vertices
            return;
        }
    submitOrHoldSurface(in, v, count);
}

void GSR_IMPL::submitOrHoldSurface(const surface::DrawInput& in, const DrawVertex* v, uint32_t count) {
    surface::PendingDraws& p = pendingSurfaces;
    if (p.empty()) {
        surface::submitDraw(*surfaceBuilder, in, v, count);
        return;
    }
    surface::PendingDraws::Draw d;
    d.in = in;
    d.first = (uint32_t)p.vertices.size();
    d.count = count;
    p.vertices.insert(p.vertices.end(), v, v + count);
    p.draws.push_back(d);
}

void GSR_IMPL::submitProgramSurface(const surface::DrawInput& drawInput, const VertexProgramBatch& batch) {
    surface::PendingDraws& p = pendingSurfaces;
    surface::DrawInput in = drawInput;
    in.streamed = true;
    if (batch.batchPath) {
        // Not offered while a ray effect is on (vertexPathCapability: strips only); the list still starts.
        if (in.firstOfEpoch)
            submitOrHoldSurface(in, nullptr, 0);
        return;
    }
    if constexpr (kCaps.gpuSurfaces)
        if (gpuSurfacesOn()) {
            // GSR_VERTEX_SURFACE=gpu: the world's ray pass builds this draw's triangles on the GPU. Not taken (a
            // resident-water state, no room in the stream): the same vertices on the CPU, exactly.
            if (!submitGpuSurface(in, batch)) {
                p.scratch.clear();
                surface::evaluateBatch(batch, in.s, p.scratch);
                submitOrHoldSurface(in, p.scratch.data(), (uint32_t)p.scratch.size());
                ++gpuSurfaces.cpuDraws;
            }
            return;
        }
    uint32_t first = 0;
    bool streamed = false;
    if constexpr (kCaps.programSurfaces)
        streamed = reserveProgramSurface(batch.indexCount, first);
    if (!streamed) {
        // No stream (no backend support or a failed buffer): the same vertices on the CPU, exactly.
        p.scratch.clear();
        surface::evaluateBatch(batch, in.s, p.scratch);
        submitOrHoldSurface(in, p.scratch.data(), (uint32_t)p.scratch.size());
        return;
    }
    surface::PendingDraws::Draw d;
    d.in = in;
    d.first = first;
    d.count = batch.indexCount;
    d.gpu = true;
    if (runtimeFlags().vertexSurfaceVerify) {
        d.check = (uint32_t)p.checks.size();
        surface::evaluateBatch(batch, in.s, p.checks);
    }
    p.draws.push_back(d);
    ++p.streamed;
}

void GSR_IMPL::flushSurfaces() {
    surface::PendingDraws& p = pendingSurfaces;
    if (p.empty())
        return;
    CpuProfile::Scope profile(cpuProfile, CpuProfile::DrawSurface);
    const bool verify = runtimeFlags().vertexSurfaceVerify;
    const surface::RawVertex* raw = nullptr;
    uint32_t rawCount = 0;
    bool streamed = false;
    for (const auto& d : p.draws)
        streamed |= d.gpu;
    bool read = false;
    if constexpr (kCaps.programSurfaces)
        if (streamed)
            read = readProgramSurfaces(raw, rawCount);
    ++p.flushes;
    // The draws are taken out first: a submission below must not see them as waiting.
    std::vector<surface::PendingDraws::Draw> draws;
    draws.swap(p.draws);
    std::vector<DrawVertex> vertices, checks, converted;
    vertices.swap(p.vertices);
    checks.swap(p.checks);
    converted.swap(p.converted); // reused: a fresh vector per flush reallocated as it grew
    for (const auto& d : draws) {
        if (!d.gpu) {
            surface::submitDraw(*surfaceBuilder, d.in, vertices.data() + d.first, d.count);
            continue;
        }
        converted.resize(d.count);
        const bool have = read && uint64_t(d.first) + d.count <= rawCount;
        if (have) {
            for (uint32_t i = 0; i < d.count; ++i)
                converted[i] = surface::drawVertexOf(raw[d.first + i], d.in.s.xyoffset);
            if (!(d.in.s.flags & DF_IIP))
                surface::flatShade(converted.data(), d.count);
        }
        if (verify) {
            const DrawVertex* want = checks.data() + d.check;
            p.compared += d.count;
            for (uint32_t i = 0; i < d.count; ++i)
                if (!have || std::memcmp(&converted[i], &want[i], sizeof(DrawVertex)) != 0)
                    ++p.differing;
            surface::submitDraw(*surfaceBuilder, d.in, want, d.count); // the reference values
        } else if (have) {
            surface::submitDraw(*surfaceBuilder, d.in, converted.data(), d.count);
        } else if (d.in.firstOfEpoch) {
            surface::submitDraw(*surfaceBuilder, d.in, nullptr, 0); // the read failed: the list still starts
        }
    }
    static const bool log = verify || std::getenv("DRAKAN_VERTEX_LOG") != nullptr;
    if (log) {
        static auto last = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        if (now - last >= std::chrono::seconds(5)) {
            last = now;
            std::fprintf(stderr,
                         "[gsr] vertex surfaces: %llu batches streamed, %llu flushes; verify: %llu vertices compared, "
                         "%llu differ%s\n",
                         (unsigned long long)p.streamed, (unsigned long long)p.flushes, (unsigned long long)p.compared,
                         (unsigned long long)p.differing, verify ? "" : " (GSR_VERTEX_SURFACE_VERIFY off)");
        }
    }
    // Keep the storage for the next frame.
    vertices.clear();
    checks.clear();
    p.vertices.swap(vertices);
    p.checks.swap(checks);
    p.converted.swap(converted);
    draws.clear();
    p.draws.swap(draws);
}

void GSR_IMPL::drainSurfaces(CpuProfile* profile, bool worldEnd) {
    if constexpr (kCaps.programSurfaces)
        if (worldEnd && runtimeFlags().vertexSurfaceLate) {
            flushLateSurfaces();
            surfaceBuilder->drain(profile);
            return;
        }
    flushSurfaces();
    surfaceBuilder->drain(profile);
    if (worldEnd)
        surface::logSurfaceStats();
}

// GSR_VERTEX_SURFACE=late (test only, not exact): at a world's end the streamed draws of the previous world are
// submitted from the copy taken then (the GPU finished it a world ago), into this world's list of the same target;
// this world's ordinary draws are submitted now and its streamed draws wait for the next world, behind a copy taken
// now. Nothing waits for the GPU. A streamed draw whose target is gone or has no list in this world is dropped.
void GSR_IMPL::flushLateSurfaces() {
    if constexpr (kCaps.programSurfaces) {
        surface::PendingDraws& p = pendingSurfaces;
        CpuProfile::Scope profile(cpuProfile, CpuProfile::DrawSurface);
        if (!p.carried.empty()) {
            const surface::RawVertex* raw = nullptr;
            uint32_t rawCount = 0;
            std::vector<DrawVertex> converted;
            if (mapLateProgramSurfaces(raw, rawCount))
                for (const auto& d : p.carried) {
                    RT* owner = nullptr;
                    for (auto& target : rts)
                        if (&target->surface == d.in.list)
                            owner = target.get();
                    if (!owner || owner->surfaceSubmittedEpoch != motionEpoch || uint64_t(d.first) + d.count > rawCount)
                        continue;
                    converted.resize(d.count);
                    for (uint32_t i = 0; i < d.count; ++i)
                        converted[i] = surface::drawVertexOf(raw[d.first + i], d.in.s.xyoffset);
                    if (!(d.in.s.flags & DF_IIP))
                        surface::flatShade(converted.data(), d.count);
                    surface::DrawInput in = d.in;
                    in.epoch = motionEpoch;
                    in.firstOfEpoch = false;
                    surface::submitDraw(*surfaceBuilder, in, converted.data(), d.count);
                }
            p.carried.clear();
        }
        for (const auto& d : p.draws) {
            if (d.gpu)
                p.carried.push_back(d); // its range of the stream, copied below
            else
                surface::submitDraw(*surfaceBuilder, d.in, p.vertices.data() + d.first, d.count);
        }
        if (!p.carried.empty() && !copyLateProgramSurfaces())
            p.carried.clear();
        ++p.flushes;
        p.draws.clear();
        p.vertices.clear();
        p.checks.clear();
    }
}

} // namespace gsr
