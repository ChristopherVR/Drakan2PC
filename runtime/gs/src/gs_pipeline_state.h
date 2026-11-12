// Fixed-function pipeline state as the GS draw code decides it (gs_draw.inl): blend, depth test and topology in
// backend-neutral terms. The WebGPU device (rhi.h) takes these types as they are; the Direct3D 11 backend maps them
// to its own enums.
#pragma once
#include <cstdint>

namespace gsr::rhi {

// ---- fixed-function state (the subset of D3D11 the GS pipeline uses) ----
enum class BlendFactor : uint8_t {
    Zero,
    One,
    Src1Color,
    OneMinusSrc1Color,
    Src1Alpha,
    OneMinusSrc1Alpha,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha
};
enum class BlendOp : uint8_t { Add, Subtract, ReverseSubtract, Min, Max };
enum class Compare : uint8_t { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
enum class Topology : uint8_t { TriangleList, LineList, PointList };

struct BlendState {
    bool enable = false;
    BlendOp op = BlendOp::Add;
    BlendFactor src = BlendFactor::One, dst = BlendFactor::Zero;
    BlendOp alphaOp = BlendOp::Add;
    BlendFactor alphaSrc = BlendFactor::One, alphaDst = BlendFactor::Zero;
    uint8_t writeMask = 0xf; // R=1 G=2 B=4 A=8
    bool operator==(const BlendState&) const = default;
};

struct DepthState {
    bool enable = false; // false = no depth test and no write
    Compare compare = Compare::Always;
    bool write = false;
    bool operator==(const DepthState&) const = default;
};

} // namespace gsr::rhi
