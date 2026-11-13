// HLSL around the generated vertex stages (gsr/vertex_stage.h, src/vertex_stage_emit.h): the bindings, the entry
// point of a draw and the entry point of the test. The stage bodies and the arithmetic library are generated at
// start-up from the one description (vertexProgramHlsl below); the shader cache keys on the text.
//
// Arithmetic::Doubles needs double support in shaders (optional in Direct3D 11); Arithmetic::Limbs needs nothing.
// vp::chooseArithmetic decides per adapter; an empty string here (Arithmetic::None) means "evaluate on the CPU".
//
// vpEvaluate returns what the program would have put into the GS packet. VSProgram (compiled after gs_shaders.h's
// kHlslMain) feeds it through the ordinary VSMain; VSProgramRaw returns it as integers for the test that compares the
// shader with the CPU evaluation (tests/test_vertex_program.cpp).
#pragma once
#include "vertex_stage_emit.h"

#include <string>
// clang-format off

namespace gsr
{

inline const char* kHlslVertexProgramHead = R"HLSL(
// ---------------------------------------------------------------- vertex programs
cbuffer CBProgram : register(b5)
{
    int4 vp_offset; // xy = XYOFFSET (12.4 fixed point), z = first row of this draw's constants in VpConstants
};
Buffer<uint4> VpConstants : register(t8);
struct VpIn
{
    uint4 pos : VPPOS;
    uint4 st : VPST;
    int4 nrm : VPNRM;
    uint4 sel : VPSEL; // x = constants index, y = program, z = constants index of the previous frame (motion)
};
struct VpRaw
{
    uint4 place;  // x, y (16 bits each), z, fog lane (bits 4..11 fog, bit 15 skip)
    uint4 colour; // r g b a (0..255)
    uint4 stq;    // float bits of s, t, q
};
uint4 vpConstant(uint row) { return VpConstants.Load((int)row + vp_offset.z); }
// The GS depth as the float the CPU path queues: the nearest float, ties to even. Written in integers because an
// adapter's own conversion is not that for every value (WARP rounds differently from 2^31 up).
float vpDepth(uint z)
{
    if (z < 0x1000000u) return (float)(int)z;
    uint shift = firstbithigh(z) - 23u;
    uint mant = z >> shift, rest = z & ((1u << shift) - 1u), half = 1u << (shift - 1u);
    uint up = (rest > half || (rest == half && (mant & 1u) != 0u)) ? 1u : 0u;
    return asfloat(((150u + shift) << 23) + (mant & 0x7FFFFFu) + up);
}
#ifdef VP_MOTION
// Motion vectors of a vertex-program draw (strip and batch path). The `history` value is computed from the two
// processed positions exactly as motion::History::track does on the CPU (program_draw::historyOf,
// vertex_program_draw.h, is the same list of operations): every float operation is `precise`, one IEEE operation
// each, in that order, and the one division is done in integers (vpDivideNearest).
cbuffer CBProgramMotion : register(b6)
{
    float4 vp_raster; // xy = render pixels per native pixel
};
float3 vpNative(uint4 place)
{
    return float3((float)((int)place.x - vp_offset.x) * (1.0 / 16.0),
                  (float)((int)place.y - vp_offset.y) * (1.0 / 16.0), vpDepth(place.z));
}
// a / b for two positive normal floats, rounded to nearest even like the CPU's division. Direct3D allows a GPU one
// unit in the last place on a float division (measured: the quotient differed from the CPU's on 30 % of the vertex
// pairs on an RTX 5080 and on 1.4 % on WARP), so the quotient is formed in integers: a long division of the
// significands, eight bits per step, then the rounding.
float vpDivideNearest(float a, float b)
{
    uint ua = asuint(a), ub = asuint(b);
    uint ma = (ua & 0x7FFFFFu) | 0x800000u, mb = (ub & 0x7FFFFFu) | 0x800000u;
    int e = (int)(ua >> 23) - (int)(ub >> 23) + 127;
    if (ma < mb) { ma <<= 1; e -= 1; }
    uint rem = ma - mb; // the leading quotient bit is 1
    uint q = 1u;
    for (int step = 0; step < 3; ++step)
    {
        uint t = rem << 8;
        uint d = t / mb;
        rem = t - d * mb;
        q = (q << 8) | d;
    }
    rem <<= 1;
    uint last = rem >= mb ? 1u : 0u;
    rem -= last * mb;
    q = (q << 1) | last; // 1 + 25 fraction bits, rem != 0: more below
    uint mant = q >> 2, low = q & 3u;
    if (low == 3u || (low == 2u && (rem != 0u || (mant & 1u) != 0u))) mant += 1u;
    if (mant == 0x1000000u) { mant = 0x800000u; e += 1; }
    return asfloat(((uint)e << 23) | (mant & 0x7FFFFFu));
}
// The history value from the processed position now and the one of the reference world.
float4 vpHistoryOf(float3 now, float3 old, bool known)
{
    bool depths = now.z > 0.0 && old.z > 0.0;
    precise float ratio = depths ? vpDivideNearest(now.z, old.z) : 1.0;
    precise float dx = old.x - now.x;
    precise float dy = old.y - now.y;
    bool valid = known && depths && ratio >= 0.01 && ratio <= 100.0 && abs(dx) <= 4096.0 && abs(dy) <= 4096.0;
    precise float hx = old.x * vp_raster.x;
    precise float hy = old.y * vp_raster.y;
    hx = hx * ratio;
    hy = hy * ratio;
    return valid ? float4(hx, hy, ratio, 1.0) : float4(0.0, 0.0, 1.0, 0.0);
}
#endif
)HLSL";

inline const char* kHlslVertexProgramEntry = R"HLSL(
#ifdef VP_RAW_ONLY
struct VpRawOut
{
    uint4 place : RAWPLACE;
    uint4 colour : RAWCOLOUR;
    uint4 stq : RAWSTQ;
#ifdef VP_PREVIOUS
    uint4 previous : RAWPREVIOUS; // x, y, z of the same vertex with the constants sel.z names
#endif
    float4 pos : SV_Position;
};
VpRawOut VSProgramRaw(VpIn i)
{
    VpRaw r = vpEvaluate(i);
    VpRawOut o;
    o.place = r.place;
    o.colour = r.colour;
    o.stq = r.stq;
#ifdef VP_PREVIOUS
    o.previous = vpEvaluateAtPrevious(i, i.sel.z).place;
#endif
    o.pos = float4(0, 0, 0, 1);
    return o;
}
#else
VSOut VSProgram(VpIn i)
{
    VpRaw r = vpEvaluate(i);
    VSIn o;
    o.pf.x = (float)((int)r.place.x - vp_offset.x) * (1.0 / 16.0);
    o.pf.y = (float)((int)r.place.y - vp_offset.y) * (1.0 / 16.0);
    o.pf.z = vpDepth(r.place.z);
    o.pf.w = (float)((r.place.w >> 4) & 0xFFu);
    o.col = float4(r.colour);
    o.stq = float3(asfloat(r.stq.x), asfloat(r.stq.y), asfloat(r.stq.z));
    return VSMain(o);
}
#endif
#ifdef VP_MOTION
// Motion vectors of a strip draw. The second vertex stream carries what motion::ProgramHistory kept of the reference
// world: the raw position and where its constants are. The position part of the stage runs again on them, and
// vpHistoryOf (kHlslVertexProgramHead) turns the two processed positions into `history`.
struct VpPrevIn
{
    uint4 pos : VPPREVPOS; // previous raw position
    uint4 sel : VPPREVSEL; // x = constants index of the previous world, y = 1 when there is a previous state
};
float4 vpHistory(VpIn i, VpPrevIn p, float3 now)
{
    VpIn before = i;
    before.pos = p.pos;
    precise float3 old = vpNative(vpEvaluateAtPrevious(before, p.sel.x).place);
    return vpHistoryOf(now, old, p.sel.y != 0u);
}
#ifdef VP_RAW_ONLY
struct VpHistoryOut
{
    uint4 history : RAWHISTORY; // float bits
    float4 pos : SV_Position;
};
VpHistoryOut VSProgramHistoryRaw(VpIn i, VpPrevIn p)
{
    VpHistoryOut o;
    o.history = asuint(vpHistory(i, p, vpNative(vpEvaluate(i).place)));
    o.pos = float4(0, 0, 0, 1);
    return o;
}
#else
MotionOut VSProgramMotion(VpIn i, VpPrevIn p)
{
    VpRaw r = vpEvaluate(i);
    VSIn o;
    o.pf.x = (float)((int)r.place.x - vp_offset.x) * (1.0 / 16.0);
    o.pf.y = (float)((int)r.place.y - vp_offset.y) * (1.0 / 16.0);
    o.pf.z = vpDepth(r.place.z);
    o.pf.w = (float)((r.place.w >> 4) & 0xFFu);
    o.col = float4(r.colour);
    o.stq = float3(asfloat(r.stq.x), asfloat(r.stq.y), asfloat(r.stq.z));
    VSOut v = VSMain(o);
    MotionOut m;
    m.pos = v.pos;
    m.col = v.col;
    m.stq = v.stq;
    m.fog = v.fog;
    m.history = vpHistory(i, p, o.pf.xyz);
    return m;
}
#endif
#endif
)HLSL";

// The whole text for one arithmetic library and the given stages (the game's when none are given); empty for
// Arithmetic::None. `previousPosition` adds vpEvaluateAtPrevious, the position fields alone, for a second
// evaluation with the previous frame's constants (the motion-vector twin; compile with VP_PREVIOUS defined to have
// VSProgramRaw return it). With `previousPosition`, compiling with VP_MOTION defined adds the velocity entry point
// VSProgramMotion (after gs_shaders.h's kHlslMain) or, with VP_RAW_ONLY, the test's VSProgramHistoryRaw.
inline std::string vertexProgramHlsl(vp::Arithmetic arithmetic,
                                     const std::vector<vp::StageEntry>& stages = vp::gameStages(),
                                     bool previousPosition = false)
{
    if (arithmetic == vp::Arithmetic::None) return {};
    std::string text = std::string(kHlslVertexProgramHead) + vp::emitLibrary(vp::ShaderLanguage::Hlsl, arithmetic) +
                       vp::emitStages(vp::ShaderLanguage::Hlsl, stages.data(), stages.size());
    if (previousPosition)
        text += vp::emitStages(vp::ShaderLanguage::Hlsl, stages.data(), stages.size(), vp::ir::kPositionFields,
                               "Previous");
    return text + kHlslVertexProgramEntry;
}

// ---- the batch path (gsr/vertex_batch_stage.h): a record's vertices through its pass list ----
// A vertex is a vp::BatchVertex as the record holds it, with its fourth row turned into `sel`: the `slot` word is
// replaced by the row of the record's first vp::Pass in the constants table (the passes are uploaded as they lie in
// the record, thirteen rows each). With VP_PREVIOUS two more rows follow: the position row the same vertex had in
// the previous frame and its `sel` then (pass mask, grid values and pass table of the previous frame's record).
inline const char* kHlslVertexBatchHead = R"HLSL(
struct VbRow
{
    uint4 pos; // the three quadwords of a vertex of the batch window, as VU memory holds them
    uint4 col;
    uint4 st;
};
struct VbIn
{
    uint4 pos : VBPOS;
    uint4 col : VBCOL;
    uint4 st : VBST;
    uint4 sel : VBSEL; // x = pass mask, y = grid x, z = grid z (float bits), w = first row of the pass table
#ifdef VP_PREVIOUS
    uint4 ppos : VBPPOS; // previous frame: the position row
    uint4 psel : VBPSEL; // previous frame: sel
#endif
};
)HLSL";

inline const char* kHlslVertexBatchEntry = R"HLSL(
VbRow vbRowOf(VbIn i)
{
    VbRow r;
    r.pos = i.pos;
    r.col = i.col;
    r.st = i.st;
    return r;
}
#ifdef VP_PREVIOUS
// x, y, z of the vertex as the previous frame's record places it (the position passes alone). Bit 31 of the mask is
// not a pass (a record has at most 24): a draw sets it when the vertex has a previous state (motion::BatchPrevious).
uint4 vbPreviousPlace(VbIn i)
{
    VbRow r;
    r.pos = i.ppos;
    r.col = uint4(0u, 0u, 0u, 0u);
    r.st = uint4(0u, 0u, 0u, 0u);
    return vbEvaluatePrevious(r, uint4(i.psel.x & 0x7FFFFFFFu, i.psel.yzw)).place;
}
#endif
#if defined(VP_MOTION) && defined(VP_PREVIOUS)
// Motion vectors of a batch-path draw: the previous place from the previous position row, mask, grid values and
// position passes (motion::ProgramHistory::trackBatch), then the strip path's vpHistoryOf.
float4 vbHistory(VbIn i, float3 now)
{
    precise float3 old = vpNative(vbPreviousPlace(i));
    return vpHistoryOf(now, old, (i.psel.x >> 31) != 0u);
}
#endif
#ifdef VP_RAW_ONLY
struct VbRawOut
{
    uint4 place : RAWPLACE;
    uint4 colour : RAWCOLOUR;
    uint4 stq : RAWSTQ;
#ifdef VP_PREVIOUS
    uint4 previous : RAWPREVIOUS;
#endif
#ifdef VB_ROW
    uint4 rowPos : RAWROWPOS; // the row after the chain (the test reads every lane a pass can leave)
    uint4 rowCol : RAWROWCOL;
    uint4 rowSt : RAWROWST;
#endif
    float4 pos : SV_Position;
};
VbRawOut VSBatchRaw(VbIn i)
{
    VbRow row = vbChain(vbRowOf(i), i.sel);
    VpRaw r = vbFinish(row);
    VbRawOut o;
    o.place = r.place;
    o.colour = r.colour;
    o.stq = r.stq;
#ifdef VP_PREVIOUS
    o.previous = vbPreviousPlace(i);
#endif
#ifdef VB_ROW
    o.rowPos = row.pos;
    o.rowCol = row.col;
    o.rowSt = row.st;
#endif
    o.pos = float4(0, 0, 0, 1);
    return o;
}
#if defined(VP_MOTION) && defined(VP_PREVIOUS)
struct VbHistoryOut
{
    uint4 history : RAWHISTORY; // float bits
    float4 pos : SV_Position;
};
VbHistoryOut VSBatchHistoryRaw(VbIn i)
{
    VbHistoryOut o;
    o.history = asuint(vbHistory(i, vpNative(vbEvaluate(vbRowOf(i), i.sel).place)));
    o.pos = float4(0, 0, 0, 1);
    return o;
}
#endif
#else
// The packet of a vertex as the vertex kick queues it (as VSProgram makes it; the depth is the nearest float, vpDepth).
VSIn vbPacket(VpRaw r)
{
    VSIn o;
    o.pf.x = (float)((int)r.place.x - vp_offset.x) * (1.0 / 16.0);
    o.pf.y = (float)((int)r.place.y - vp_offset.y) * (1.0 / 16.0);
    o.pf.z = vpDepth(r.place.z);
    o.pf.w = (float)((r.place.w >> 4) & 0xFFu);
    o.col = float4(r.colour);
    o.stq = float3(asfloat(r.stq.x), asfloat(r.stq.y), asfloat(r.stq.z));
    return o;
}
VSOut VSBatch(VbIn i)
{
    return VSMain(vbPacket(vbEvaluate(vbRowOf(i), i.sel)));
}
#if defined(VP_MOTION) && defined(VP_PREVIOUS)
// The velocity pass of a batch-path draw (compare VSProgramMotion).
MotionOut VSBatchMotion(VbIn i)
{
    VSIn o = vbPacket(vbEvaluate(vbRowOf(i), i.sel));
    VSOut v = VSMain(o);
    MotionOut m;
    m.pos = v.pos;
    m.col = v.col;
    m.stq = v.stq;
    m.fog = v.fog;
    m.history = vbHistory(i, o.pf.xyz);
    return m;
}
#endif
#endif
)HLSL";

// The whole text of the batch path for one arithmetic library; empty for Arithmetic::None. `previousPosition` adds
// vbEvaluatePrevious (the position passes and fields alone; compile with VP_PREVIOUS defined to have the vertex
// carry the previous frame's inputs). Entry points: VSBatch (after gs_shaders.h's kHlslMain), or with VP_RAW_ONLY
// VSBatchRaw.
inline std::string vertexBatchHlsl(vp::Arithmetic arithmetic, bool previousPosition = false)
{
    if (arithmetic == vp::Arithmetic::None) return {};
    std::string text = std::string(kHlslVertexProgramHead) + kHlslVertexBatchHead +
                       vp::emitLibrary(vp::ShaderLanguage::Hlsl, arithmetic) + vp::emitBatch(vp::ShaderLanguage::Hlsl);
    if (previousPosition)
        text += vp::emitBatch(vp::ShaderLanguage::Hlsl, vp::ir::kPositionFields, "Previous");
    return text + kHlslVertexBatchEntry;
}

} // namespace gsr
