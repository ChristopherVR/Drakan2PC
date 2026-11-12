// WGSL around the generated vertex stages (gsr/vertex_stage.h, src/vertex_stage_emit.h): the two structures the
// generated functions use, and the text of library plus stages. The includer adds its bindings, a function
//   fn vpConstant(row: u32) -> vec4u      a row of the constants table (a storage buffer, or textureLoad of an
//                                         rgba32uint texture where the vertex stage has no storage buffers)
// and its entry point, which fills a VpIn and calls vpEvaluate (or vpEvaluateAt with another constants index).
// WGSL has no 64-bit float, so the only arithmetic here is the integer library (vp::Arithmetic::Limbs).
#pragma once
#include "vertex_stage_emit.h"

#include <string>

namespace gsr {

inline const char* kWgslVertexProgramHead = R"WGSL(
struct VpIn {
    pos: vec4u,
    st: vec4u,
    nrm: vec4i,
    sel: vec4u, // x = constants index, y = program, z = constants index of the previous frame (motion)
}
struct VpRaw {
    place: vec4u,  // x, y (16 bits each), z, fog lane (bits 4..11 fog, bit 15 skip)
    colour: vec4u, // r g b a (0..255)
    stq: vec4u,    // float bits of s, t, q
}
)WGSL";

// Structures, library and the given stages (the game's when none are given). `previousPosition` adds
// vpEvaluateAtPrevious, the position fields alone, for a second evaluation with the previous frame's constants (the
// motion-vector twin).
inline std::string vertexProgramWgsl(const std::vector<vp::StageEntry>& stages = vp::gameStages(),
                                     bool previousPosition = false) {
    std::string text = std::string(kWgslVertexProgramHead) +
                       vp::emitLibrary(vp::ShaderLanguage::Wgsl, vp::Arithmetic::Limbs) +
                       vp::emitStages(vp::ShaderLanguage::Wgsl, stages.data(), stages.size());
    if (previousPosition)
        text +=
            vp::emitStages(vp::ShaderLanguage::Wgsl, stages.data(), stages.size(), vp::ir::kPositionFields, "Previous");
    return text;
}

// ---- the batch path (gsr/vertex_batch_stage.h) ----
// The includer fills a VbRow from a vertex (the three rows of a vp::BatchVertex) and passes its fourth row as
// `sel`, with the `slot` word replaced by the row of the record's first vp::Pass in the constants table.
inline const char* kWgslVertexBatchHead = R"WGSL(
struct VbRow {
    pos: vec4u, // the three quadwords of a vertex of the batch window, as VU memory holds them
    col: vec4u,
    st: vec4u,
}
)WGSL";

// Structures, library and the batch functions: vbEvaluate(row, sel) and, with `previousPosition`,
// vbEvaluatePrevious(row, sel): the position passes and fields alone, for the previous frame's position row, sel
// and pass table (the motion-vector twin).
inline std::string vertexBatchWgsl(bool previousPosition = false) {
    std::string text = std::string(kWgslVertexProgramHead) + kWgslVertexBatchHead +
                       vp::emitLibrary(vp::ShaderLanguage::Wgsl, vp::Arithmetic::Limbs) +
                       vp::emitBatch(vp::ShaderLanguage::Wgsl);
    if (previousPosition)
        text += vp::emitBatch(vp::ShaderLanguage::Wgsl, vp::ir::kPositionFields, "Previous");
    return text;
}

} // namespace gsr
