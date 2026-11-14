#pragma once

// Terrain chunk record layouts of the PAL engine and of the `wide_terrain_chunks` function variant
// (configs/drakan.toml [[variants]], runtime ps2xRuntime/src/lib/drakan_terrain_chunks.cpp).
//
// A terrain layer holds a vector of chunk records (layer + 0x10c: {begin, capacity, count}). The disc record is 0x4c
// bytes: x0 x1 z0 z1, ntex, ref[10] at +0x14, then the weight-array descriptor {vw, vh, n, pointer} at +0x3c. The
// stream reader FUN_00165840 copies ntex refs without a check, and the terrain mesh drawer FUN_00158148 keeps one flag
// per texture group in a 10-word array on its stack (group 0 is the base pass), so the disc engine holds at most 9
// textures per chunk safely; the PAL discs use at most 8. The wide variant reads and walks a 0xa4-byte record with
// ref[32] and the descriptor at +0x94, and gives the drawer 64 flags.

#include <cstdint>

namespace world::terrain_chunks {

inline constexpr uint32_t kRefsOffset = 0x14u;

inline constexpr uint32_t kNarrowRefs = 10u;
inline constexpr uint32_t kNarrowDescriptor = kRefsOffset + 4u * kNarrowRefs; // 0x3c
inline constexpr uint32_t kNarrowStride = kNarrowDescriptor + 16u;            // 0x4c
inline constexpr uint32_t kNarrowDrawerFlags = 10u;                           // group 0 (base) + 9 textures

inline constexpr uint32_t kWideRefs = 32u;
inline constexpr uint32_t kWideDescriptor = kRefsOffset + 4u * kWideRefs; // 0x94
inline constexpr uint32_t kWideStride = kWideDescriptor + 16u;            // 0xa4
inline constexpr uint32_t kWideDrawerFlags = 64u;

static_assert(kNarrowDescriptor == 0x3cu && kNarrowStride == 0x4cu, "disc chunk record");
static_assert(kWideDescriptor == 0x94u && kWideStride == 0xa4u, "values patched into the variant");

// Most textures one chunk may list for the engine in the given mode: the record's refs, and one fewer than the
// drawer's flags (group 0 is the base pass).
inline constexpr uint32_t maxTextures(bool wide) {
    const uint32_t refs = wide ? kWideRefs : kNarrowRefs;
    const uint32_t groups = (wide ? kWideDrawerFlags : kNarrowDrawerFlags) - 1u;
    return refs < groups ? refs : groups;
}

static_assert(maxTextures(false) == 9u && maxTextures(true) == 32u, "chunk texture limits");

} // namespace world::terrain_chunks
