// WGSL entry points of vertex-program draws on the WebGPU backend (wgpu_vertex_program.cpp): the generated stages of
// vertex_program_wgsl.h in a vertex stage, followed by the body of the renderer's own vertex shader (`vs` of
// gs_shaders_wgsl.h), so that everything after the stage is the code of every other draw.
//
// Inputs: one vertex of a batch (VertexProgramVertex, 64 bytes) as four integer attributes, and the constants of
// the batch as rows of four words in the device's draw data (rhi::Device::writeDrawData): a read-only storage buffer
// in the vertex stage where the device allows it, otherwise an rgba32uint texture read with textureLoad
// (rhi::DrawDataPath). vpDraw.offset carries XYOFFSET (12.4 fixed point) and the first row of this draw's constants.
//
// The renderer's vertex shader is not called (an entry point cannot be) but copied from its text at start-up
// (vertexShaderBody), so a change to it reaches this path without a second edit; the copy is checked by the tests.
// The vertex the stage makes is handed to that body as the bits the CPU path would put into its vertex buffer:
// integers, colours and the depth converted exactly, the position formed from its bits (vpSixteenth), so that no
// compiler can merge the stage's arithmetic with the body's (WGSL has no `precise`). Proven bit for bit against the
// ordinary vertex shader fed the CPU evaluation (tests/test_vertex_program_webgpu.cpp).
#pragma once
#include "gs_shaders_wgsl.h"
#include "vertex_program_wgsl.h"

#include <string>

namespace gsr {

// How the vertex stage reaches the constants (rhi::DrawDataPath without the rhi header: the tests use raw WebGPU).
enum class VpConstantsPath : uint8_t { StorageBuffer, Texture };
inline constexpr uint32_t kVpDrawDataBinding = 24;        // rhi: the device's draw data
inline constexpr uint32_t kVpDrawDataTextureWidth = 4096; // texels (rows) per texture row on the texture path

inline const char* kWgslVertexProgramDraw = R"WGSL(
struct VpDraw {
    offset: vec4i, // xy = XYOFFSET (12.4 fixed point), z = first row of this draw's constants in the draw data
}
@group(0) @binding(1) var<uniform> vpDraw: VpDraw;
struct VpAttr {
    @location(0) pos: vec4u,
    @location(1) st: vec4u,
    @location(2) nrm: vec4i,
    @location(3) sel: vec4u, // x = constants index, y = program
}
fn vpInOf(a: VpAttr) -> VpIn {
    return VpIn(a.pos, a.st, a.nrm, a.sel);
}
// k / 16 for a 16-bit coordinate difference: exact, formed from the float's bits (the exponent less four).
fn vpSixteenth(k: i32) -> f32 {
    if (k == 0) { return 0.0; }
    return bitcast<f32>(bitcast<u32>(f32(k)) - 0x02000000u);
}
// The GS depth as the float the CPU path queues: the nearest float, ties to even, in integers.
fn vpDepth(z: u32) -> f32 {
    if (z < 0x1000000u) { return f32(i32(z)); }
    let shift = firstLeadingBit(z) - 23u;
    let mant = z >> shift;
    let rest = z & ((1u << shift) - 1u);
    let half = 1u << (shift - 1u);
    let up = select(0u, 1u, rest > half || (rest == half && (mant & 1u) != 0u));
    return bitcast<f32>(((150u + shift) << 23u) + (mant & 0x7FFFFFu) + up);
}
// The vertex the CPU path draws (DrawVertex, gs_internal.h) from what the stage put into the packet.
fn vpVertexOf(r: VpRaw) -> VSIn {
    var o: VSIn;
    o.pf = vec4f(vpSixteenth(i32(r.place.x) - vpDraw.offset.x), vpSixteenth(i32(r.place.y) - vpDraw.offset.y),
                 vpDepth(r.place.z), f32((r.place.w >> 4u) & 0xFFu));
    o.col = vec4f(r.colour);
    o.stq = vec3f(bitcast<f32>(r.stq.x), bitcast<f32>(r.stq.y), bitcast<f32>(r.stq.z));
    return o;
}
)WGSL";

inline const char* kWgslVertexProgramStorage = R"WGSL(
@group(0) @binding(24) var<storage, read> vpConstants: array<vec4u>;
fn vpConstant(row: u32) -> vec4u { return vpConstants[row + u32(vpDraw.offset.z)]; }
)WGSL";

inline const char* kWgslVertexProgramTexture = R"WGSL(
@group(0) @binding(24) var vpConstants: texture_2d<u32>;
fn vpConstant(row: u32) -> vec4u {
    let r = row + u32(vpDraw.offset.z);
    return textureLoad(vpConstants, vec2u(r & 4095u, r >> 12u), 0);
}
)WGSL";

inline const char* kWgslVertexProgramEntry = R"WGSL(
@vertex fn vsProgram(a: VpAttr) -> VSOut {
    return vpVertexShader(vpVertexOf(vpEvaluate(vpInOf(a))));
}
)WGSL";

// The body of the renderer's vertex shader as a function `vpVertexShader(i: VSIn) -> VSOut` (the text of `vs` in
// kWgslGsCommon from its signature to the closing brace at the start of a line). Empty when the text is not found.
inline std::string vertexShaderBody(const char* common = kWgslGsCommon, const char* name = "vpVertexShader") {
    const std::string text = common;
    const std::string signature = "@vertex fn vs(i: VSIn) -> VSOut {";
    const size_t at = text.find(signature);
    if (at == std::string::npos)
        return {};
    const size_t end = text.find("\n}\n", at);
    if (end == std::string::npos)
        return {};
    return std::string("fn ") + name + "(i: VSIn) -> VSOut {" +
           text.substr(at + signature.size(), end + 3 - at - signature.size());
}

// The module of a vertex-program draw: the renderer's draw program (fixed-function blend with `hw`, else shader
// blend), the stages, the bindings for `path` and the entry point vsProgram. Empty when the vertex shader body is
// not found. `extra` is appended (the tests' probe entry points).
inline std::string vertexProgramDrawWgsl(bool hw, VpConstantsPath path, bool previousPosition = false,
                                         const std::vector<vp::StageEntry>& stages = vp::gameStages(),
                                         const std::string& extra = {}) {
    const std::string body = vertexShaderBody();
    if (body.empty())
        return {};
    return std::string(hw ? "enable dual_source_blending;\n" : "") + kWgslGsCommon + (hw ? kWgslGsHw : kWgslGsSb) +
           vertexProgramWgsl(stages, previousPosition) + kWgslVertexProgramDraw +
           (path == VpConstantsPath::StorageBuffer ? kWgslVertexProgramStorage : kWgslVertexProgramTexture) + body +
           kWgslVertexProgramEntry + extra;
}

} // namespace gsr
