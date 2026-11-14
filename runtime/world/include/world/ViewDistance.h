// View distance ([video] view_distance): rules of the option, pure functions.
//
// The engine keeps one view distance per world, in cells of 2048 units (75 in every level seen, Drakan 2 and converted
// Order of the Flame alike), together with the distance fog that ends there. The world's fog setter (PAL 0x142f70,
// a0 = world, f12 = far in cells, f13 = fog range in cells) writes:
//
//   +0x388 far (cells)          read by the terrain grid (radius in cells), the object draw cap, the frustum,
//                               particles and the per-layer object activity loop
//   +0x38c F^2                  read by the sound code (sounds beyond the view distance are not played)
//   +0x394 fog record           F, S^2, F^2, 1 / (F^2 - S^2), F, R, 1 / R, S (then the fog colour word at +0x3b4),
//                               handed to the VU1 fog constants of every draw
//
// with F = far * 2048, R = range * 2048 and S = F - R the depth where the fog starts. The option multiplies far and
// range by its factor, so the fog starts and ends that many times further out and the terrain grid, the frustum and the
// object cap follow; the sound distance (+0x38c) and the object activity loop keep the original numbers.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace world::view_distance {

inline constexpr float kOff = 1.0f;
// The terrain grid of the engine spans (2 * far + 1) rows of 64 bytes in a static table with room for about 470 rows
// before the next variable; 75 cells times 3 is 451 rows.
inline constexpr float kMaxFactor = 3.0f;
inline constexpr float kCell = 2048.0f;

inline float clampFactor(float factor) {
    return std::isfinite(factor) ? std::clamp(factor, kOff, kMaxFactor) : kOff;
}

// Arguments the fog setter receives. Valid when both are finite, the far distance is positive and the range lies in
// (0, far]: the setter divides by the range and by F^2 - S^2.
struct FogArgs {
    float farCells = 0.0f;
    float rangeCells = 0.0f;
};

inline bool valid(FogArgs a) {
    return std::isfinite(a.farCells) && std::isfinite(a.rangeCells) && a.farCells > 0.0f && a.rangeCells > 0.0f &&
           a.rangeCells <= a.farCells && a.farCells < 10000.0f;
}

inline FogArgs scaled(FogArgs a, float factor) {
    const float k = clampFactor(factor);
    return {a.farCells * k, a.rangeCells * k};
}

// What the setter writes, computed as the guest does (single precision, the same operation order).
struct FogFields {
    float farCells;              // +0x388
    float farSquared;            // +0x38c
    std::array<float, 8> record; // +0x394 .. +0x3b0
};

inline FogFields fields(FogArgs a) {
    const float rangeUnits = a.rangeCells * kCell;           // f5
    const float start = (a.farCells - a.rangeCells) * kCell; // f13
    const float far = a.farCells * kCell;                    // f12
    const float farSq = far * far;                           // f2
    const float startSq = start * start;                     // f1
    FogFields f{};
    f.farCells = a.farCells;
    f.farSquared = farSq;
    f.record = {far, startSq, far * far, 1.0f / (far * far - startSq), far, rangeUnits, 1.0f / rangeUnits, start};
    return f;
}

} // namespace world::view_distance
