// Model detail options of the PC port (pure rules, no game bytes, no platform code).
//
// The engine picks a character's level of detail (LOD) by comparing the squared camera distance with the squared switch
// distances of the model's LOD table, and hides an object beyond its draw distance (a per-model value, a per-class
// value, or 70.7 times the object's radius). Both limits are PS2 budgets. The port leaves the engine's code and the
// model data alone and changes only the numbers the engine compares:
//
//   * detail distance: the squared camera distance handed to the LOD selector is divided by factor^2, so every switch
//     distance acts factor times further out. A LOD that draws the character from other texture sheets than the full
//     model (a different hair colour, a missing blood stain) is never selected while the option is on.
//   * draw distance: an object's own draw distance is multiplied by a factor for the visibility test and for the
//     distance fade of the draw call. The level's view distance still caps it, as in the original code.
//
// A factor of 1 returns every input unchanged, bit for bit: "off" is the original picture.
// Evidence and numbers: docs/journal/2026-11-12/gfx-models.md.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace world::model_detail {

inline constexpr float kOff = 1.0f;
inline constexpr float kMaxLodFactor = 8.0f;
inline constexpr float kMaxDrawFactor = 4.0f;
// Defaults of the settings (the reasons are in the journal note): LOD switches happen four times further out, where a
// character is under about 45 pixels tall at 2160 lines (under 1 % more vertex work). The draw distance stays at
// the original: 1.5 cost 42 % more vertex work in the Surdana meadow, mostly grass and flowers.
inline constexpr float kDefaultLodFactor = 4.0f;
inline constexpr float kDefaultDrawFactor = 1.0f;

// The selector's hysteresis: it switches to a finer LOD below distance^2 - 2^20 and to a coarser one from
// distance^2 + 2^20 (guest 0x102c90).
inline constexpr float kLodHysteresisSquared = 1048576.0f;
// A draw distance at or above this is the engine's "no limit" value (0x5f0ac723, about 1e19).
inline constexpr float kUnlimitedDrawDistance = 1.0e18f;

inline float clampLodFactor(float factor) {
    return std::isfinite(factor) ? std::clamp(factor, kOff, kMaxLodFactor) : kOff;
}
inline float clampDrawFactor(float factor) {
    return std::isfinite(factor) ? std::clamp(factor, kOff, kMaxDrawFactor) : kOff;
}

// The squared distance the LOD selector should see. factor 1 returns the input itself.
inline float lodDistanceSquared(float distanceSquared, float factor) {
    if (!(factor > kOff) || !std::isfinite(distanceSquared) || distanceSquared <= 0.0f)
        return distanceSquared;
    return distanceSquared / (factor * factor);
}

// Keeps the selector below a LOD whose squared switch distance is `limitSquared`: the result stays under the point
// where the selector would step into that LOD and, when the model is already on it, under the point where it steps
// back to the finer one.
inline float belowLod(float distanceSquared, float limitSquared) {
    if (!std::isfinite(limitSquared))
        return distanceSquared;
    const float ceiling = std::max(0.0f, limitSquared - 2.0f * kLodHysteresisSquared);
    return std::isfinite(distanceSquared) ? std::min(distanceSquared, ceiling) : ceiling;
}

// The first LOD (1 ..) that uses a material LOD 0 does not use, or 0 when every lower LOD draws from the sheets of the
// full model. `materials[l]` lists the material indices of LOD l's polygons (any order, duplicates allowed).
inline unsigned firstLookChangingLod(const std::vector<std::vector<uint16_t>>& materials) {
    if (materials.size() < 2)
        return 0;
    std::vector<uint16_t> base = materials[0];
    std::sort(base.begin(), base.end());
    for (size_t lod = 1; lod < materials.size(); ++lod)
        for (const uint16_t m : materials[lod])
            if (!std::binary_search(base.begin(), base.end(), m))
                return static_cast<unsigned>(lod);
    return 0;
}

// An object's draw distance for the visibility test and the distance fade. Zero, negative, non-finite and "no limit"
// values are returned unchanged, and so is everything at factor 1.
inline float drawDistance(float distance, float factor) {
    if (!(factor > kOff) || !(distance > 0.0f) || !(distance < kUnlimitedDrawDistance))
        return distance;
    return distance * factor;
}

} // namespace world::model_detail
