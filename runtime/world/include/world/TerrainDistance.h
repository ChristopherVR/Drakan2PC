// Ground texture distance ([video] terrain_distance): rules of the option, pure functions.
//
// The engine draws a terrain chunk once for its far colour map (4 texels per 2048 unit cell, always) and once per
// ground texture ("splat") on top. A splat pass is drawn only while the chunk is nearer than the material's "Splat
// Fade Distance" (30 cells = 61440 units for most ground, 50 cells for the outdoor rock), and its weight fades
// linearly with the view depth: alpha = weight * (1 - depth / fade). So at a third of the fade distance a third of the
// ground texture is already gone and the 4 texel colour map shows through: the blur of mid-range mountains.
//
// The guest compares fade^2 with (distance * s)^2 and uploads fade / s to the vertex program, where s is one float of
// the view (1.0 in play). The option divides s by its factor for the length of the chunk draw call, which moves the
// cut-off and the fade out by that factor for every material at once and changes nothing else.
#pragma once

#include <algorithm>
#include <cmath>

namespace world::terrain_distance {

inline constexpr float kOff = 1.0f;
inline constexpr float kMaxFactor = 4.0f;

inline float clampFactor(float factor) {
    return std::isfinite(factor) ? std::clamp(factor, kOff, kMaxFactor) : kOff;
}

// The view scale the chunk draw should see. Factor 1, or a stored value that is not a plain positive scale, returns
// the stored value itself (the caller then leaves the guest alone).
inline float viewScale(float stored, float factor) {
    if (!(factor > kOff) || !std::isfinite(stored) || !(stored > 1.0e-3f) || !(stored < 1.0e3f))
        return stored;
    return stored / factor;
}

// Weight left of a splat at view depth `depth` for a material fade distance, as the vertex program computes it.
inline float splatWeight(float depth, float fadeDistance, float factor) {
    const float fade = fadeDistance * std::max(factor, kOff);
    return fade > 0.0f ? std::clamp(1.0f - depth / fade, 0.0f, 1.0f) : 0.0f;
}

} // namespace world::terrain_distance
