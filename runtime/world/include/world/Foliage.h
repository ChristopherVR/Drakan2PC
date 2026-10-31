// Wind model of the "living foliage" option: pure functions (std only), no guest memory. The adapter
// (ps2xRuntime/src/lib/drakan_foliage.cpp) turns a Lean into a shear of the model's up axis for one draw call.
// A lean is dimensionless: the horizontal displacement of the top of a model as a fraction of its height.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace world::foliage {

enum Kind : int { Tree = 0, Plant = 1 };

struct Lean {
    float x = 0, z = 0;
};

inline constexpr double kTwoPi = 6.283185307179586;
inline constexpr float kPushRadius = 4800.0f; // world units (a cell is 2048): about two and a half cells around the actor
inline constexpr float kMaxLean = 0.8f;
inline constexpr float kMinStrength = 0.25f, kMaxStrength = 2.0f;

inline float smoothstep(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Per-object phase in [0, 2 pi) from any stable id (the handle address).
inline float seedPhase(uint32_t seed) {
    seed ^= seed >> 16;
    seed *= 0x7feb352dU;
    seed ^= seed >> 15;
    seed *= 0x846ca68bU;
    seed ^= seed >> 16;
    return static_cast<float>(seed & 0xffffu) * (static_cast<float>(kTwoPi) / 65536.0f);
}

// Wind direction (unit vector in the x / z plane) at time t seconds: slowly veers around one heading.
// Cached per thread for the last time asked: one frame draws hundreds of objects at (almost) the same t.
inline Lean windDirection(double t) {
    thread_local double lastT = -1e300;
    thread_local Lean last;
    if (t != lastT) {
        const double a = 0.6 + 0.45 * std::sin(t * 0.031) + 0.25 * std::sin(t * 0.0113 + 1.7);
        last = {static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a))};
        lastT = t;
    }
    return last;
}

// Gust strength 0 (lull) .. 1 (gust) at a world position. Long fronts travel with the wind (about 48000 units apart,
// roughly 23 cells), wobbled sideways so they are not ruler straight, plus a faster second wave.
inline float gustAt(float x, float z, double t) {
    const Lean d = windDirection(t);
    const float along = x * d.x + z * d.z, across = -x * d.z + z * d.x;
    const double w1 = along * 0.000131 - t * 1.15 + 0.35 * std::sin(across * 0.00004 + t * 0.07);
    const double w2 = along * 0.00031 - t * 1.9 + across * 0.00011;
    return smoothstep(0.0f, 1.0f, static_cast<float>(0.5 + 0.38 * std::sin(w1) + 0.17 * std::sin(w2)));
}

// Lean of an object of `kind` at world (x, z) with a per-object seed at time t. Neighbouring plants share most of their
// phase (a wave rolls through a meadow) with a little individual jitter. Trees lean little and slowly, plants a lot
// and quickly (a base lean with the gust, a sway and a fast flutter).
inline Lean windLean(int kind, float x, float z, uint32_t seed, double t, float strength) {
    const Lean d = windDirection(t);
    const float qx = -d.z, qz = d.x; // across the wind
    const float along = x * d.x + z * d.z;
    const float gust = gustAt(x, z, t);
    const float jitter = seedPhase(seed);
    const double phase = along * 0.00025 + 0.5 * jitter;
    float la, lc;
    if (kind == Tree) {
        const double sway = std::sin(kTwoPi * 0.38 * t + 0.5 * phase + 0.4 * jitter);
        const double shiver = std::sin(kTwoPi * 1.1 * t + phase + jitter);
        la = 0.010f + 0.022f * gust + 0.010f * (0.3f + gust) * static_cast<float>(sway) +
             0.005f * gust * static_cast<float>(shiver);
        lc = 0.004f * gust * static_cast<float>(std::sin(kTwoPi * 0.31 * t + phase));
    } else {
        const double freq = 1.4 + 0.5 * gust;
        const double sway = std::sin(kTwoPi * freq * t + phase);
        const double flutter = std::sin(kTwoPi * 3.3 * t + 2.3 * phase + jitter);
        la = 0.05f + 0.12f * gust + 0.035f * (0.4f + gust) * static_cast<float>(sway) +
             0.018f * (0.3f + gust) * static_cast<float>(flutter);
        lc = 0.025f * (0.3f + gust) * static_cast<float>(std::sin(kTwoPi * 0.9 * t + 1.7 * phase));
    }
    const float s = std::clamp(strength, kMinStrength, kMaxStrength);
    return {(d.x * la + qx * lc) * s, (d.z * la + qz * lc) * s};
}

// Extra lean of a plant at offset (dx, dz) from an actor: pushed away, zero beyond kPushRadius, smooth at both ends.
inline Lean pushLean(float dx, float dz, float strength) {
    const float d = std::sqrt(dx * dx + dz * dz);
    if (!(d < kPushRadius) || d < 1.0f)
        return {};
    const float f = smoothstep(0.0f, 1.0f, 1.0f - d / kPushRadius);
    const float amount = 0.55f * f * std::clamp(strength, kMinStrength, kMaxStrength);
    return {dx / d * amount, dz / d * amount};
}

// Caps the lean so a blade never folds flat.
inline Lean clampLean(Lean l) {
    const float m = std::sqrt(l.x * l.x + l.z * l.z);
    if (m > kMaxLean) {
        l.x *= kMaxLean / m;
        l.z *= kMaxLean / m;
    }
    return l;
}

// The shear to add to the world image `up` of the model's up axis (|up| = len): the top moves by lean * len along x / z
// and drops by lean^2 / 2 * len so the blade keeps its length when it bends over (world y is up).
inline void shearAdd(Lean l, float len, float out[3]) {
    out[0] = l.x * len;
    out[1] = -0.5f * (l.x * l.x + l.z * l.z) * len;
    out[2] = l.z * len;
}

} // namespace world::foliage
