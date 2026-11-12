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

} // namespace gsr
