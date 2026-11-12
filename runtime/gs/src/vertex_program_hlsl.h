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
    int4 vp_offset; // xy = XYOFFSET (12.4 fixed point)
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
uint4 vpConstant(uint row) { return VpConstants.Load((int)row); }
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
    o.pf.z = (float)r.place.z;
    o.pf.w = (float)((r.place.w >> 4) & 0xFFu);
    o.col = float4(r.colour);
    o.stq = float3(asfloat(r.stq.x), asfloat(r.stq.y), asfloat(r.stq.z));
    return VSMain(o);
}
#endif
)HLSL";

// The whole text for one arithmetic library and the given stages (the game's when none are given); empty for
// Arithmetic::None. `previousPosition` adds vpEvaluateAtPrevious, the position fields alone, for a second
// evaluation with the previous frame's constants (the motion-vector twin; compile with VP_PREVIOUS defined to have
// VSProgramRaw return it).
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
// x, y, z of the vertex as the previous frame's record places it (the position passes alone)
uint4 vbPreviousPlace(VbIn i)
{
    VbRow r;
    r.pos = i.ppos;
    r.col = uint4(0u, 0u, 0u, 0u);
    r.st = uint4(0u, 0u, 0u, 0u);
    return vbEvaluatePrevious(r, i.psel).place;
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
#else
VSOut VSBatch(VbIn i)
{
    VpRaw r = vbEvaluate(vbRowOf(i), i.sel);
    VSIn o;
    o.pf.x = (float)((int)r.place.x - vp_offset.x) * (1.0 / 16.0);
    o.pf.y = (float)((int)r.place.y - vp_offset.y) * (1.0 / 16.0);
    o.pf.z = (float)r.place.z;
    o.pf.w = (float)((r.place.w >> 4) & 0xFFu);
    o.col = float4(r.colour);
    o.stq = float3(asfloat(r.stq.x), asfloat(r.stq.y), asfloat(r.stq.z));
    return VSMain(o);
}
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
