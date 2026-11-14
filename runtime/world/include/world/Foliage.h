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
inline constexpr float kPushRadius =
    4800.0f; // world units (a cell is 2048): about two and a half cells around the actor
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

// ---- Leaves (foliage_wind = all) ----
// Trunks and branches stay where they are; only the vertices of leaf materials move. A material is a leaf material
// when its texture is cut out (a share of fully transparent texels, sampled by the adapter); a leaf vertex that a
// trunk or branch face shares is an anchor and does not move, so no crack opens where a leaf card meets its branch.
// The offset of a leaf vertex is a function of its model position only (two copies of one vertex in two draw
// arrays move together) and grows with its height above the model's base and its distance from the trunk.

inline constexpr float kLeafCut = 0.1f;   // share of transparent texels from which a material is a leaf material
inline constexpr float kOpaqueCut = 0.02f; // at most this share: a solid (trunk / branch) material

inline bool leafMaterial(float cutShare) {
    return cutShare >= kLeafCut;
}

// What the adapter knows of one material of a model: the share of transparent texels in its texture, how many faces
// use it and the height range of their vertices (model units, y up).
struct MaterialStats {
    float cutShare = 0;
    uint32_t faces = 0;
    float yMin = 0, yMax = 0;
};

// A model that is not in the vegetation table still gets leaf wind when it looks like a tree: tall, a solid material
// that stands on the model's base (a trunk), cut-out materials that reach the upper half (a crown) and carry at least
// 15 % of the faces. Houses with a few cut-out windows, fences and banners fail one of the tests.
inline bool looksLikeTree(const MaterialStats* m, size_t count, float yMin, float yMax) {
    const float h = yMax - yMin;
    if (!(h >= 1500.0f) || count == 0)
        return false;
    bool trunk = false, crown = false;
    uint32_t leafFaces = 0, faces = 0;
    for (size_t i = 0; i < count; ++i) {
        faces += m[i].faces;
        if (!m[i].faces)
            continue;
        if (leafMaterial(m[i].cutShare)) {
            leafFaces += m[i].faces;
            crown |= m[i].yMax >= yMin + 0.5f * h;
        } else if (m[i].cutShare <= kOpaqueCut) {
            trunk |= m[i].yMin <= yMin + 0.05f * h;
        }
    }
    return trunk && crown && faces && leafFaces * 100 >= faces * 15;
}

// Weight 0 .. 1 of a leaf vertex at height y and distance r from the trunk axis (model units): 0 for an anchor, grows
// with the height above the base (the base stays put, the top moves most), and leaves far out move more than those
// near the trunk.
inline float leafWeight(float y, float r, float yMin, float yMax, float rMax, bool anchored) {
    if (anchored || !(yMax > yMin))
        return 0.0f;
    const float h = std::clamp((y - yMin) / (yMax - yMin), 0.0f, 1.0f);
    const float rf = rMax > 0 ? 0.6f + 0.4f * std::clamp(r / rMax, 0.0f, 1.0f) : 1.0f;
    return h * rf;
}

// Per vertex data, computed once per model: the weight and the two flutter phases (cos, sin) from the position, so
// that neighbouring leaves (a clump) flutter together and clumps further apart do not.
struct LeafVertex {
    float w = 0, c1 = 1, s1 = 0, c2 = 1, s2 = 0;
};
inline LeafVertex leafVertex(float x, float y, float z, float weight, float modelHeight) {
    LeafVertex v;
    v.w = weight;
    // About three flutter cycles across a crown, whatever the model's scale.
    const float k = modelHeight > 1.0f ? 18.0f / modelHeight : 0.0f;
    const float p1 = k * (0.83f * x + 0.61f * y + 0.47f * z), p2 = k * (-0.52f * x + 0.77f * y + 0.91f * z) + 1.3f;
    v.c1 = std::cos(p1);
    v.s1 = std::sin(p1);
    v.c2 = std::cos(p2);
    v.s2 = std::sin(p2);
    return v;
}

// What moves the leaves of one object this frame, in model units: the crown's sway (at weight 1) and two flutter
// vectors with the sine / cosine of their oscillators.
struct LeafFrame {
    float sway[3]{}, flutterA[3]{}, flutterB[3]{};
    float s1 = 0, c1 = 1, s2 = 0, c2 = 1;
};

// Wind direction for the leaves: the level's own wind (the engine's eased gust vector, x / z) when it has one, else
// the slowly veering global wind.
inline Lean steerWind(float levelX, float levelZ, double t) {
    const float m = std::sqrt(levelX * levelX + levelZ * levelZ);
    if (m > 1e-3f && std::isfinite(m))
        return {levelX / m, levelZ / m};
    return windDirection(t);
}

// The leaf frame of an object at world (x, z) with seed `seed` (its handle) and world height `height`. `dir` is the
// wind direction (unit, x / z), `toModel` maps a world vector to model units (the inverse of the object's 3x3,
// row-major). The crown leans downwind 1 .. 3 % of the height with the gust plus a slow 0.45 Hz sway; leaves flutter
// at 1.7 and 2.6 Hz with up to about 1 % of the height. Each object has its own phase, so a forest does not move in
// step, while the gust field it samples is shared with its neighbours.
inline LeafFrame leafFrame(Lean dir, float x, float z, uint32_t seed, double t, float strength, float height,
                           const float toModel[9]) {
    const float s = std::clamp(strength, kMinStrength, kMaxStrength);
    const float gust = gustAt(x, z, t);
    const float jitter = seedPhase(seed);
    const float sway = static_cast<float>(std::sin(kTwoPi * 0.45 * t + jitter));
    const float lean = (0.008f + 0.016f * gust + 0.006f * (0.3f + gust) * sway) * height * s;
    const float fa = 0.006f * (0.4f + gust) * height * s, fb = 0.004f * (0.4f + gust) * height * s;
    const float world[3][3] = {{dir.x * lean, 0.0f, dir.z * lean},
                               {dir.x * fa, 0.3f * fa, dir.z * fa},
                               {-dir.z * fb, 0.2f * fb, dir.x * fb}};
    LeafFrame f;
    float* out[3] = {f.sway, f.flutterA, f.flutterB};
    for (int v = 0; v < 3; ++v)
        for (int r = 0; r < 3; ++r)
            out[v][r] = toModel[r * 3 + 0] * world[v][0] + toModel[r * 3 + 1] * world[v][1] +
                        toModel[r * 3 + 2] * world[v][2];
    const double a1 = kTwoPi * 1.7 * t + 2.1 * jitter, a2 = kTwoPi * 2.6 * t + 3.7 * jitter;
    f.s1 = static_cast<float>(std::sin(a1));
    f.c1 = static_cast<float>(std::cos(a1));
    f.s2 = static_cast<float>(std::sin(a2));
    f.c2 = static_cast<float>(std::cos(a2));
    return f;
}

// Offset (model units) of one leaf vertex: sin(a1 + p1) and sin(a2 + p2) by the angle sum, no trigonometry per vertex.
inline void leafOffset(const LeafFrame& f, const LeafVertex& v, float out[3]) {
    const float t1 = f.s1 * v.c1 + f.c1 * v.s1, t2 = f.s2 * v.c2 + f.c2 * v.s2;
    for (int i = 0; i < 3; ++i)
        out[i] = v.w * (f.sway[i] + f.flutterA[i] * t1 + f.flutterB[i] * t2);
}

// Inverse of a row-major 3x3 (the object's rotation and scale). False when it is singular or not finite.
inline bool invert3(const float m[9], float out[9]) {
    const float c0 = m[4] * m[8] - m[5] * m[7], c1 = m[5] * m[6] - m[3] * m[8], c2 = m[3] * m[7] - m[4] * m[6];
    const float det = m[0] * c0 + m[1] * c1 + m[2] * c2;
    if (!std::isfinite(det) || std::fabs(det) < 1e-12f)
        return false;
    const float k = 1.0f / det;
    out[0] = c0 * k;
    out[1] = (m[2] * m[7] - m[1] * m[8]) * k;
    out[2] = (m[1] * m[5] - m[2] * m[4]) * k;
    out[3] = c1 * k;
    out[4] = (m[0] * m[8] - m[2] * m[6]) * k;
    out[5] = (m[2] * m[3] - m[0] * m[5]) * k;
    out[6] = c2 * k;
    out[7] = (m[1] * m[6] - m[0] * m[7]) * k;
    out[8] = (m[0] * m[4] - m[1] * m[3]) * k;
    return true;
}

} // namespace world::foliage
