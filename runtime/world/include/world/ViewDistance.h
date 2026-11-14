// View distance ([video] view_distance): rules of the option, pure functions.
//
// The engine keeps one view distance per world, in cells of 2048 units (each level sets its own: 75 in Surdana and the
// converted Order of the Flame levels, 45 in Ravenshold), together with the distance fog that ends there. The world's
// fog setter (PAL 0x142f70, a0 = world, f12 = far in cells, f13 = fog range in cells) writes:
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
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

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

// Far scenery ([video] far_scenery, with a factor above 1). The terrain draw (PAL 0x152330) draws the layers of the
// view layer's `visible` list, and the level lists were made for the original view distance: the layers listed from
// one place reach a little past it (Ravenshold: 72 cells for a 45 cell view distance), so a larger view distance
// shows where the list ends (the sea as a flat plane with a straight edge, sea without the islands in it). The draw
// list is widened to the layers the game itself lists from nearby places: the closure of the lists from the view
// layer over layers whose box lies within `reach` cells of the view layer's box. The original entries keep their
// order and come first; the added ones follow by distance. Only the draw sees the wider list.
namespace world::view_distance {

struct LayerBox {
    float x0 = 0, z0 = 0, x1 = 0, z1 = 0; // cells
};

// Distance in cells between two boxes on the ground plane (0 when they touch or overlap).
inline float boxDistance(const LayerBox& a, const LayerBox& b) {
    const float dx = std::max({b.x0 - a.x1, a.x0 - b.x1, 0.0f});
    const float dz = std::max({b.z0 - a.z1, a.z0 - b.z1, 0.0f});
    return std::sqrt(dx * dx + dz * dz);
}

// How far (cells) the draw list reaches for a level view distance `farCells` at `factor`: the scaled view distance
// plus two cells, so a layer that the frustum can just reach is in.
inline float scenicReach(float farCells, float factor) {
    return farCells * clampFactor(factor) + 2.0f;
}

// `boxOf(i, box)` gives layer i's box (false when the layer is not usable), `listOf(i, out)` its visible list,
// `keep(i)` whether a reached layer is added (the walk goes on through layers that are not).
// Returns the widened list for view layer `view` (at most `maxEntries` long): the original list first, as it is.
template <class BoxOf, class ListOf, class Keep>
std::vector<uint16_t> widenedList(uint32_t view, uint32_t layerCount, float reach, size_t maxEntries, BoxOf boxOf,
                                  ListOf listOf, Keep keep) {
    std::vector<uint16_t> out;
    std::vector<uint16_t> list;
    if (view >= layerCount || !listOf(view, list))
        return out;
    out = list;
    LayerBox home;
    if (!boxOf(view, home) || !(reach > 0.0f) || out.size() >= maxEntries)
        return out;
    std::vector<uint8_t> seen(layerCount, 0);
    seen[view] = 1;
    for (uint16_t i : out)
        if (i < layerCount)
            seen[i] = 1;
    std::vector<std::pair<float, uint16_t>> added;
    std::vector<uint16_t> queue(out.begin(), out.end());
    std::vector<uint16_t> links;
    while (!queue.empty()) {
        const uint16_t at = queue.back();
        queue.pop_back();
        if (at >= layerCount || !listOf(at, links))
            continue;
        for (uint16_t j : links) {
            if (j >= layerCount || seen[j])
                continue;
            seen[j] = 1;
            LayerBox b;
            if (!boxOf(j, b))
                continue;
            const float d = boxDistance(home, b);
            if (!(d <= reach))
                continue;
            if (keep(j))
                added.push_back({d, j});
            queue.push_back(j);
        }
    }
    std::stable_sort(added.begin(), added.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [d, j] : added) {
        if (out.size() >= maxEntries)
            break;
        out.push_back(j);
    }
    return out;
}

template <class BoxOf, class ListOf>
std::vector<uint16_t> widenedList(uint32_t view, uint32_t layerCount, float reach, size_t maxEntries, BoxOf boxOf,
                                  ListOf listOf) {
    return widenedList(view, layerCount, reach, maxEntries, boxOf, listOf, [](uint32_t) { return true; });
}

} // namespace world::view_distance
