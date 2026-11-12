// The VU's float arithmetic in 32-bit integers (gsr/vu_exact.inl), twice from the one source: compiled as C++
// (gsr::vp::limbs::vuAdd and friends, what the tests check and what a CPU evaluation may use), and as text
// (kLibrary) for the generators of the HLSL and WGSL libraries (src/vertex_stage_emit.h). Portable: no platform
// header, no floating point, no 64-bit integer.
#pragma once
#include <cstdint>

namespace gsr::vp::limbs {

using uvar = uint32_t;
constexpr uint32_t ubit(bool condition) {
    return condition ? 1u : 0u;
}

// clang-format off
#define VUX_FN1(group, name, a, ...) inline uint32_t name(uint32_t a) __VA_ARGS__
#define VUX_FN2(group, name, a, b, ...) inline uint32_t name(uint32_t a, uint32_t b) __VA_ARGS__
#define VUX_FN3(group, name, a, b, c, ...) inline uint32_t name(uint32_t a, uint32_t b, uint32_t c) __VA_ARGS__
#include "vu_exact.inl"
#undef VUX_FN1
#undef VUX_FN2
#undef VUX_FN3

enum class Group : uint8_t { Primitive, Base, Limb, Composite };
struct LibraryFunction {
    Group group;
    const char* name;
    uint32_t parameterCount;
    const char* parameters[3];
    const char* body; // "{ ... }" in the shared subset, on one line
};
inline constexpr LibraryFunction kLibrary[] = {
#define VUX_FN1(group, name, a, ...) {Group::group, #name, 1u, {#a, "", ""}, #__VA_ARGS__},
#define VUX_FN2(group, name, a, b, ...) {Group::group, #name, 2u, {#a, #b, ""}, #__VA_ARGS__},
#define VUX_FN3(group, name, a, b, c, ...) {Group::group, #name, 3u, {#a, #b, #c}, #__VA_ARGS__},
#include "vu_exact.inl"
#undef VUX_FN1
#undef VUX_FN2
#undef VUX_FN3
};
// clang-format on

} // namespace gsr::vp::limbs
