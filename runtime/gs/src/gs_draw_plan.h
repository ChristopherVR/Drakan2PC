// What draw() (gs_draw.inl) works out about one GS draw, filled in phase by phase and read by the phases after it.
//
// draw() is a sequence of phases (debug hooks, target, depth, texture, world, GUI rules, blend, constants, vertices,
// submit). Each phase is a member function of the backend's Impl that takes this plan by reference: the earlier phases
// fill their part, the later ones read it. It lives on the stack of draw(), so it costs no allocation.
//
// It is a template because each backend has its own target, depth buffer and texture binding types (its Impl names it
// `Draw`, after its TexBind: `using Draw = DrawPlan<RT, DepthBuf, TexBind>`).
#pragma once
#include "gs_backend_shared.h"
#include "gs_internal.h"
#include "gs_pipeline_state.h"
#include "hud_page.h"

#include <cstdint>

namespace gsr {

template <class Target, class Depth, class Texture>
struct DrawPlan {
    DrawPlan(const GsDrawState& state, const ClutBuf& palette, const DrawVertex* vertices, uint32_t vertexCount)
        : s(state), clut(palette), v(vertices), count(vertexCount) {}

    // ---- the draw as the GS received it
    const GsDrawState& s;
    const ClutBuf& clut;
    const DrawVertex* v; // native GS coordinates; the vertex phase rewrites a copy (beginVertices), never these
    uint32_t count;
    int64_t index = 0;    // drawIndex of this draw (the debug cuts and dumps name draws by it)
    bool logThis = false; // the draw log is on for this draw

    // ---- target (FRAME register, scissor, bounding box)
    uint32_t fbp = 0, fbw = 0, fpsm = 0;
    uint32_t fbmsk = 0;                           // FRAME.FBMSK: a set bit keeps the target's bit
    int sx0 = 0, sy0 = 0, sx1 = 0, sy1 = 0;       // SCISSOR, inclusive, native pixels
    float minx = 0, miny = 0, maxx = 0, maxy = 0; // bounding box of the vertices
    Target* rt = nullptr;
    int cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0; // the pixels the draw can write: scissor and target clipped bounds
    uint32_t joinKeptCount = 0;             // a joined draw: the segments not clipped away

    // ---- depth (ZBUF and TEST registers)
    uint32_t zbp = 0, zpsm = 0;
    bool zmsk = false; // ZBUF.ZMSK: no depth writes
    bool zte = false;  // TEST.ZTE: the depth test is on
    uint32_t ztst = 0; // TEST.ZTST: 0 never, 1 always, 2 greater-equal, 3 greater
    Depth* db = nullptr;
    float zmax = 0; // the largest depth value of the depth format

    // ---- texture
    Texture tb;
    bool tme = false;     // the draw is textured (PRIM.TME)
    bool haveTex = false; // ... and its texture resolved
    bool clampSpriteFootprint = false;

    // ---- world phase: render pixels per native pixel, and whether the draw lands in a reduced world image
    float dsx = 1, dsy = 1;
    bool worldDraw = false;

    // ---- write mask and blend (ALPHA register, FBMSK)
    bool abe = false;                             // alpha blending on (PRIM.ABE)
    uint32_t A = 0, B = 0, C = 0, D = 0, FIX = 0; // ALPHA: (A - B) * C + D, FIX the fixed coefficient
    bool is16 = false;                            // the target is a 16-bit format
    uint32_t maskBytes = 0;                       // FBMSK as an RGBA byte mask in target space
    bool partial = false;                         // some channel is only partly masked
    uint8_t writeMask = 0xF;                      // the colour channels written (bit 0 red .. bit 3 alpha)
    bool date = false, pabe = false;
    bool useHw = false; // the blend runs in the fixed-function blender
    rhi::BlendOp bop = rhi::BlendOp::Add;
    rhi::BlendFactor sf = rhi::BlendFactor::One, df = rhi::BlendFactor::Zero;
    bool bEnable = false;  // fixed-function blending enabled
    bool needDest = false; // the shader reads a copy of the destination (shader blend, DATE, partial masks)

    // ---- GUI rules: which HUD / page map the vertices take
    bool inventoryPageDraw = false, inventoryFontShadow = false, inventoryShade = false;
    bool menuPageDraw = false, cornerHudDraw = false, legacyHudDraw = false;
    hud::PageMap inventoryMap{}, menuMap{};
    float hudClipX0 = 0, hudClipY0 = 0, hudClipX1 = 0, hudClipY1 = 0; // the scissor after the HUD maps
    uint32_t cornerLeft = 0, cornerRight = 0, cornerUnmapped = 0;     // triangles per corner (hud corner log)

    // ---- constants and passes
    PSConsts k{};
    bool ate = false;   // TEST.ATE: the alpha test is on
    uint32_t atst = 0;  // TEST.ATST
    uint32_t afail = 0; // TEST.AFAIL: what a pixel that fails the alpha test still writes
    bool drawMotion = false;
};

// The frame the HUD maps of one draw work in (gs_draw.inl, mapHudVertices).
struct HudFrame {
    DrawVertex* dv;         // the writable vertex copy
    float fx0, fy0;         // scissor origin
    float frameW, frameH;   // scissor size
    float cornerW, cornerH; // the display frame the corner, subtitle and tutorial maps use
};

} // namespace gsr
