// Terrain detail: backend-neutral policy and maths (unit tested, tests/test_terrain_detail.cpp).
//
// The game draws its height-field terrain as splat layers: every chunk is drawn once per ground texture with the
// blend weight in the vertex alpha. The textures are mapped from above, one tile per 2048 unit cell, so they are
// stretched on steep faces and repeat every cell. This header holds what both backends share:
//
//   * which draws are terrain layers (classify), from the GS state alone;
//   * the per-pixel world position of such a draw from the pixel position and Q (Unproject), relative to the camera;
//   * the constants of the shader fragment (ShaderConsts, gs_terrain.inc on Direct3D 11);
//   * the procedural detail volume (buildNoiseVolume), generated at start-up: no art asset is involved;
//   * a CPU reference of the shader's detail factor and cliff weights (detailFactor, cliffWeights).
//
// Everything here is a pure function of its arguments. See docs/journal/2026-11-12/gfx-terrain.md.
#pragma once
#include <gsr/scene_guide_camera.h>

#include <algorithm>
#include <cmath>
#include <climits>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace gsr::terrain {

// ---- settings ---------------------------------------------------------------------------------------------------
struct Settings {
    bool detail = false; // world-space detail and large-scale breakup on terrain layers
    // Steep faces sample the layer texture from the side instead of from above. This includes the engine's run-time
    // composite of a chunk's layers (the 32 bit texture that covers 2 or 4 cells and is all that mid-range terrain is
    // drawn from): on a steep face the side projection shows another part of that composite, so patches of grass and
    // rock sit elsewhere than in the original picture. That is a change of look, hence off by default.
    bool cliffs = false;
    float strength = 1.0f; // 0..2, scales the detail amplitudes
    uint32_t debug = 0;    // 1 = tint terrain layers (green: mapped from above only, red: side projection allowed)
    bool any() const { return detail || cliffs || debug != 0; }
    bool operator==(const Settings&) const = default;
};

// ---- classification ---------------------------------------------------------------------------------------------
struct DrawInfo {
    bool triangles = false; // not a sprite, line or point
    bool textured = false;  // TME with a resolved texture
    bool textureIsTarget = false;
    bool fst = false;
    bool blend = false; // PRIM.ABE
    uint64_t tex0 = 0, clamp = 0, test = 0, alpha = 0, zbuf = 0;
};

// A terrain splat layer, as every level of both campaigns draws it (measured, see the journal):
// perspective textured triangles, blended with source alpha over the frame (ALPHA 0x44), alpha test "greater than 0"
// (the engine skips pixels with no weight), depth test GEQUAL with depth writes, texture function MODULATE without
// texture alpha in the colour path, both axes repeating.
inline bool classify(const DrawInfo& d) {
    if (!d.triangles || !d.textured || d.textureIsTarget || d.fst || !d.blend)
        return false;
    if ((d.alpha & 0xff) != 0x44)
        return false;
    const uint32_t test = uint32_t(d.test);
    const bool ate = test & 1;
    const uint32_t atst = (test >> 1) & 7, aref = (test >> 4) & 0xff;
    const bool zte = (test >> 16) & 1;
    const uint32_t ztst = (test >> 17) & 3;
    if (!ate || atst != 6 || aref != 0 || !zte || ztst != 2)
        return false;
    if ((d.zbuf >> 32) & 1) // depth writes masked: decals, water
        return false;
    if (((d.tex0 >> 35) & 3) != 0) // MODULATE
        return false;
    if ((d.clamp & 0xf) != 0) // REPEAT on both axes
        return false;
    return true;
}

// ---- world position of a pixel ------------------------------------------------------------------------------------
// rel = w * (d0 + px * dx + py * dy) + base, with w = 1 / Q and (px, py) the pixel position the shader sees
// (SV_Position: render pixels, jitter included). rel is relative to `anchor` (the camera position), so single
// precision holds everywhere in a level.
struct Unproject {
    bool valid = false;
    double anchor[3]{};
    float base[3]{}, d0[3]{}, dx[3]{}, dy[3]{};
};

// camera: the engine's world camera (row vectors: view = world * worldToView, clip = view * viewToClipVU; screen =
// offset + scale * clip / w, in GS coordinates, i.e. with XYOFFSET). renderScale: render pixels per native pixel of the
// draw; jitter: the temporal upscaler's sub-pixel offset in render pixels.
inline bool buildUnproject(const SceneGuideCamera& c, uint64_t xyoffset, float renderScaleX, float renderScaleY,
                           float jitterX, float jitterY, Unproject& out) {
    out = {};
    const uint32_t need = SceneGuideCamera::Valid | SceneGuideCamera::World | SceneGuideCamera::HaveViewport;
    if ((c.flags & need) != need || !(renderScaleX > 0) || !(renderScaleY > 0))
        return false;
    for (int i = 0; i < 2; ++i)
        if (!std::isfinite(c.viewportScale[i]) || std::abs(c.viewportScale[i]) < 1e-10f)
            return false;
    // View space from clip space. The projection the engine uses has w and clip z depending on view z alone and no
    // cross terms between x and y; anything else is refused (the shader's formula would not hold).
    // Going through view space keeps the result exact to single precision: inverting the whole world-to-clip matrix
    // instead loses three digits, because the homogeneous coordinate then hangs on the near plane term.
    const float* v = c.viewToClipVU;
    auto V = [&](int row, int col) { return double(v[row * 4 + col]); };
    const double size = std::abs(V(0, 0)) + std::abs(V(1, 1)) + std::abs(V(2, 3));
    if (!(std::abs(V(0, 0)) > 1e-12) || !(std::abs(V(1, 1)) > 1e-12) || !(std::abs(V(2, 3)) > 1e-12))
        return false;
    for (const auto& [row, col] : {std::pair{0, 1}, {1, 0}, {0, 2}, {1, 2}, {0, 3}, {1, 3}})
        if (std::abs(V(row, col)) > 1e-5 * size)
            return false;
    // zv = w / V23 - V33 / V23;  xv = (nx * w - zv * V20 - V30) / V00;  yv likewise
    const double zw = 1.0 / V(2, 3), z0 = -V(3, 3) / V(2, 3);
    const double xyX = double(xyoffset & 65535) / 16, xyY = double((xyoffset >> 32) & 65535) / 16;
    // nx = (px - jitter) / renderScale / scale + (xy - offset) / scale = ax * px + bx
    const double ax = 1.0 / (double(renderScaleX) * c.viewportScale[0]);
    const double ay = 1.0 / (double(renderScaleY) * c.viewportScale[1]);
    const double bx = (xyX - c.viewportOffset[0] - double(jitterX) / renderScaleX) / c.viewportScale[0];
    const double by = (xyY - c.viewportOffset[1] - double(jitterY) / renderScaleY) / c.viewportScale[1];
    // view = w * (viewD0 + px * viewDx + py * viewDy) + viewBase
    const double viewDx[3] = {ax / V(0, 0), 0, 0}, viewDy[3] = {0, ay / V(1, 1), 0};
    const double viewD0[3] = {(bx - zw * V(2, 0)) / V(0, 0), (by - zw * V(2, 1)) / V(1, 1), zw};
    const double viewBase[3] = {(-z0 * V(2, 0) - V(3, 0)) / V(0, 0), (-z0 * V(2, 1) - V(3, 1)) / V(1, 1), z0};
    // world = (view - T) * inverse(R) for view = world * R + T (row vectors)
    const float* m = c.worldToView;
    double r[9], inv[9];
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k)
            r[i * 3 + k] = m[i * 4 + k];
    const double det =
        r[0] * (r[4] * r[8] - r[5] * r[7]) - r[1] * (r[3] * r[8] - r[5] * r[6]) + r[2] * (r[3] * r[7] - r[4] * r[6]);
    if (!std::isfinite(det) || std::abs(det) < 1e-9)
        return false;
    inv[0] = (r[4] * r[8] - r[5] * r[7]) / det;
    inv[1] = (r[2] * r[7] - r[1] * r[8]) / det;
    inv[2] = (r[1] * r[5] - r[2] * r[4]) / det;
    inv[3] = (r[5] * r[6] - r[3] * r[8]) / det;
    inv[4] = (r[0] * r[8] - r[2] * r[6]) / det;
    inv[5] = (r[2] * r[3] - r[0] * r[5]) / det;
    inv[6] = (r[3] * r[7] - r[4] * r[6]) / det;
    inv[7] = (r[1] * r[6] - r[0] * r[7]) / det;
    inv[8] = (r[0] * r[4] - r[1] * r[3]) / det;
    auto toWorld = [&](const double view[3], int k) {
        return view[0] * inv[0 * 3 + k] + view[1] * inv[1 * 3 + k] + view[2] * inv[2 * 3 + k];
    };
    const double translation[3] = {m[12], m[13], m[14]};
    for (int k = 0; k < 3; ++k) {
        out.anchor[k] = -toWorld(translation, k); // the eye: view 0
        out.dx[k] = float(toWorld(viewDx, k));
        out.dy[k] = float(toWorld(viewDy, k));
        out.d0[k] = float(toWorld(viewD0, k));
        out.base[k] = float(toWorld(viewBase, k));
        if (!std::isfinite(out.anchor[k]) || !std::isfinite(out.dx[k]) || !std::isfinite(out.dy[k]) ||
            !std::isfinite(out.d0[k]) || !std::isfinite(out.base[k]))
            return false;
    }
    out.valid = true;
    return true;
}

// Camera-relative position of the pixel (px, py) with texture coordinate Q.
inline void relativeAt(const Unproject& u, float px, float py, float q, float rel[3]) {
    const float w = 1.0f / q;
    for (int k = 0; k < 3; ++k)
        rel[k] = w * (u.d0[k] + px * u.dx[k] + py * u.dy[k]) + u.base[k];
}

// ---- side projection -------------------------------------------------------------------------------------------
// Logical texels per world unit of a layer draw, from one of its triangles (camera-relative positions p, texture
// coordinates in logical texels t). The mapping from above is affine in world x/z; 0 when the triangle is degenerate
// or the mapping is not close to a rotation / mirror with one scale (then the side projection cannot continue it).
inline float texelDensity(const float p[3][3], const float t[3][2]) {
    const double ax = double(p[1][0]) - p[0][0], az = double(p[1][2]) - p[0][2];
    const double bx = double(p[2][0]) - p[0][0], bz = double(p[2][2]) - p[0][2];
    const double det = ax * bz - az * bx;
    if (std::abs(det) < 1.0)
        return 0;
    const double ua = double(t[1][0]) - t[0][0], ub = double(t[2][0]) - t[0][0];
    const double va = double(t[1][1]) - t[0][1], vb = double(t[2][1]) - t[0][1];
    // texel = J * (x, z)
    const double j00 = (ua * bz - ub * az) / det, j01 = (ax * ub - bx * ua) / det;
    const double j10 = (va * bz - vb * az) / det, j11 = (ax * vb - bx * va) / det;
    const double s0 = std::sqrt(j00 * j00 + j10 * j10), s1 = std::sqrt(j01 * j01 + j11 * j11);
    if (!(s0 > 0) || !(s1 > 0) || std::abs(s0 - s1) > 0.05 * std::max(s0, s1))
        return 0;
    if (std::abs(j00 * j01 + j10 * j11) > 0.05 * s0 * s1) // not orthogonal
        return 0;
    return float(0.5 * (s0 + s1));
}

// The side projection wraps the texture in world units, and the shader works relative to the camera, so the anchor
// is passed modulo kSideModulo. That is only seamless when the texture's world period divides it: periods of
// 2048 * 2^n units qualify, and n = -3 .. 3 are taken (a larger period is a far colour map, not a ground
// texture). Returns the snapped density (texels per unit), 0 when it does not qualify.
inline constexpr double kSideModulo = 131072.0;
inline float sideDensity(float density, uint32_t textureWidth, uint32_t textureHeight) {
    if (!(density > 0) || textureWidth != textureHeight || !textureWidth)
        return 0;
    const double period = double(textureWidth) / density; // world units per tile
    const double n = std::log2(period / 2048.0);
    const double r = std::round(n);
    if (std::abs(n - r) > 0.03 || r < -3 || r > 3)
        return 0;
    return float(double(textureWidth) / (2048.0 * std::exp2(r)));
}

// Weight of the side projection for a face with unit normal n (y up), and the share of the plane facing z in it.
// dither: a value in -0.5 .. 0.5 that breaks the straight edge between faces of different slope.
struct CliffWeights {
    float side = 0, planeZ = 0;
};
inline constexpr float kCliffStart = 0.64f; // n.y below which the side projection starts (50 degrees, stretch 1.56)
inline constexpr float kCliffRange = 0.24f; // fully from the side at n.y 0.40 (66 degrees, stretch 2.5)
inline CliffWeights cliffWeights(const float n[3], float dither) {
    CliffWeights w;
    const float ny = std::abs(n[1]);
    w.side = std::clamp((kCliffStart - ny) / kCliffRange + dither * 0.6f, 0.0f, 1.0f);
    const float ax = n[0] * n[0], az = n[2] * n[2];
    w.planeZ = std::clamp((az / (ax + az + 1e-6f) - 0.5f) * 4.0f + 0.5f + dither * 0.5f, 0.0f, 1.0f);
    return w;
}

// ---- smooth terrain normals ------------------------------------------------------------------------------------
// The drawn terrain triangles are 2048 units wide, so a normal taken per triangle makes every weight derived from it
// jump at triangle edges. The height field itself gives a smooth one: a grid over the level with, per grid vertex,
// the area-weighted normal of the resident triangles around it. Layers stack (floors, ceilings, caves), so a grid
// vertex holds up to kGridSlots entries at different heights and a pixel takes the one nearest in height.
// A texel is {normal x, normal z, world height, normal y}; an empty slot has height kGridEmpty. Normals point up
// (y >= 0): only the slope matters to its users, and ceilings then accumulate like floors.
inline constexpr uint32_t kGridSlots = 4, kGridMaxSize = 1024;
inline constexpr float kGridEmpty = -1.0e30f, kGridCell = 2048.0f;
inline constexpr float kGridMergeHeight = 8.0f; // vertices of adjoining layers within this height are one vertex
struct NormalGrid {
    int32_t originX = 0, originZ = 0; // grid vertex (0, 0) is at world x = originX * 2048, z = originZ * 2048
    uint32_t width = 0, height = 0;   // grid vertices
    uint32_t dropped = 0;             // vertices that found no free slot
    std::vector<float> texels;        // slot-major: [slot][z][x][4]
    bool empty() const { return !width || !height; }
    const float* at(uint32_t slot, uint32_t x, uint32_t z) const {
        return &texels[((size_t(slot) * height + z) * width + x) * 4];
    }
};
// count triangles; position(i, corner) returns a pointer to that corner's world x, y, z. Triangles with a vertex off
// the grid (never the case for the resident height field) are skipped.
template <class Position>
NormalGrid buildNormalGrid(size_t count, Position&& position) {
    NormalGrid grid;
    int64_t minX = INT64_MAX, minZ = INT64_MAX, maxX = INT64_MIN, maxZ = INT64_MIN;
    auto cellOf = [](float v, int64_t& out) {
        const double c = double(v) / kGridCell;
        const double r = std::round(c);
        out = int64_t(r);
        return std::isfinite(c) && std::abs(c - r) < 0.01 && std::abs(r) < 1e7;
    };
    for (size_t i = 0; i < count; ++i)
        for (int corner = 0; corner < 3; ++corner) {
            const float* p = position(i, corner);
            int64_t x, z;
            if (!cellOf(p[0], x) || !cellOf(p[2], z))
                continue;
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minZ = std::min(minZ, z);
            maxZ = std::max(maxZ, z);
        }
    if (minX > maxX || maxX - minX >= kGridMaxSize || maxZ - minZ >= kGridMaxSize)
        return grid;
    grid.originX = int32_t(minX);
    grid.originZ = int32_t(minZ);
    grid.width = uint32_t(maxX - minX + 1);
    grid.height = uint32_t(maxZ - minZ + 1);
    grid.texels.assign(size_t(grid.width) * grid.height * kGridSlots * 4, 0.0f);
    for (size_t t = 0; t < grid.texels.size(); t += 4)
        grid.texels[t + 2] = kGridEmpty;
    for (size_t i = 0; i < count; ++i) {
        const float* p[3] = {position(i, 0), position(i, 1), position(i, 2)};
        int64_t x[3], z[3];
        bool onGrid = true;
        for (int c = 0; c < 3; ++c)
            onGrid = onGrid && cellOf(p[c][0], x[c]) && cellOf(p[c][2], z[c]) && std::isfinite(p[c][1]);
        if (!onGrid)
            continue;
        const double ax = double(p[1][0]) - p[0][0], ay = double(p[1][1]) - p[0][1], az = double(p[1][2]) - p[0][2];
        const double bx = double(p[2][0]) - p[0][0], by = double(p[2][1]) - p[0][1], bz = double(p[2][2]) - p[0][2];
        double n[3] = {ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx}; // length = twice the area
        if (n[1] < 0)
            for (double& v : n)
                v = -v;
        if (!(n[0] * n[0] + n[1] * n[1] + n[2] * n[2] > 1.0))
            continue;
        for (int c = 0; c < 3; ++c) {
            const uint32_t gx = uint32_t(x[c] - minX), gz = uint32_t(z[c] - minZ);
            float* found = nullptr;
            for (uint32_t slot = 0; slot < kGridSlots && !found; ++slot) {
                float* texel = &grid.texels[((size_t(slot) * grid.height + gz) * grid.width + gx) * 4];
                if (texel[2] == kGridEmpty) {
                    texel[2] = p[c][1];
                    found = texel;
                } else if (std::abs(texel[2] - p[c][1]) <= kGridMergeHeight)
                    found = texel;
            }
            if (!found) {
                ++grid.dropped;
                continue;
            }
            // accumulate in units of 2048^2 so single precision holds a whole fan of cells
            found[0] += float(n[0] / (double(kGridCell) * kGridCell));
            found[1] += float(n[2] / (double(kGridCell) * kGridCell));
            found[3] += float(n[1] / (double(kGridCell) * kGridCell));
        }
    }
    for (size_t t = 0; t < grid.texels.size(); t += 4) {
        float* texel = &grid.texels[t];
        if (texel[2] == kGridEmpty)
            continue;
        const float len = std::sqrt(texel[0] * texel[0] + texel[1] * texel[1] + texel[3] * texel[3]);
        if (len > 1e-12f) {
            texel[0] /= len;
            texel[1] /= len;
            texel[3] /= len;
        } else {
            texel[0] = texel[1] = 0;
            texel[3] = 1;
        }
    }
    return grid;
}
// How far a grid vertex's stored height may be from the height the pixel's own triangle predicts for it before the
// pixel falls back to its face normal there (terrain drawn at a coarser level of detail sits up to about 250 units
// off the full-detail height field, and the fourth corner of a cell is not on the pixel's triangle).
inline constexpr float kGridHeightTolerance = 1400.0f;

// CPU reference of the shader's lookup (terrainSmoothNormal in gs_terrain.inc): world position, unit face normal.
inline void smoothNormal(const NormalGrid& grid, const float world[3], const float face[3], float out[3]) {
    float up[3] = {face[0], face[1], face[2]};
    if (up[1] < 0)
        for (float& v : up)
            v = -v;
    out[0] = up[0];
    out[1] = up[1];
    out[2] = up[2];
    if (grid.empty())
        return;
    const float cx = world[0] / kGridCell - float(grid.originX), cz = world[2] / kGridCell - float(grid.originZ);
    const float fx = cx - std::floor(cx), fz = cz - std::floor(cz);
    const int ix = int(std::floor(cx)), iz = int(std::floor(cz));
    float acc[3] = {0, 0, 0};
    for (int corner = 0; corner < 4; ++corner) {
        const int ox = corner & 1, oz = corner >> 1;
        const float weight = (ox ? fx : 1 - fx) * (oz ? fz : 1 - fz);
        const int gx = ix + ox, gz = iz + oz;
        float n[3] = {up[0], up[1], up[2]};
        if (gx >= 0 && gz >= 0 && gx < int(grid.width) && gz < int(grid.height)) {
            // the height this pixel's plane has at the corner
            const float dx = (float(ox) - fx) * kGridCell, dz = (float(oz) - fz) * kGridCell;
            const float expected = up[1] > 0.02f ? world[1] - (up[0] * dx + up[2] * dz) / up[1] : world[1];
            float best = kGridHeightTolerance;
            for (uint32_t slot = 0; slot < kGridSlots; ++slot) {
                const float* texel = grid.at(slot, uint32_t(gx), uint32_t(gz));
                const float d = std::abs(texel[2] - expected);
                if (d < best) {
                    best = d;
                    n[0] = texel[0];
                    n[1] = texel[3];
                    n[2] = texel[1];
                }
            }
        }
        for (int k = 0; k < 3; ++k)
            acc[k] += weight * n[k];
    }
    const float len = std::sqrt(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]);
    if (len > 1e-6f)
        for (int k = 0; k < 3; ++k)
            out[k] = acc[k] / len;
}

// ---- detail octaves ---------------------------------------------------------------------------------------------
// Three octaves of one tiling noise volume, each in its own rotated frame so their lattices never line up with each
// other or with the 2048 unit cells: fine grain, medium breakup, and a slow one against the visible tile repeat.
inline constexpr int kOctaves = 3;
inline constexpr float kOctavePeriod[kOctaves] = {224.0f, 1408.0f, 9728.0f}; // world units per noise tile
inline constexpr float kOctaveAmplitude[kOctaves] = {0.12f, 0.025f, 0.04f};  // albedo factor swing at strength 1
inline constexpr float kReliefAmplitude = 0.30f;                 // light-facing slope of the fine and medium octaves
inline constexpr float kReliefStep = 0.045f;                     // in noise tiles
inline constexpr float kReliefFine = 0.6f, kReliefMedium = 0.1f; // octave weights inside the relief term
// The fine and medium octaves and the relief read as surface roughness up close; on a mountain across the valley
// they would read as grain. They fade from full at kReliefNear to kReliefFar times their strength at kReliefNear +
// kReliefRange (camera distance, units). The slow octave keeps its strength.
inline constexpr float kReliefNear = 3000.0f, kReliefRange = 14000.0f, kReliefFar = 0.3f;
inline float reliefFade(float distance) {
    const float t = std::clamp((distance - kReliefNear) / kReliefRange, 0.0f, 1.0f);
    return 1.0f + (kReliefFar - 1.0f) * t;
}
// The engine draws a character's projected shadow over the terrain as a pass of its own that darkens the ground by
// as little as 21 / 128 (16 percent). Detail with features of the size of that shadow (the medium octave, 0.3 to
// 1.5 m) and a swing of the same size hides it: with the first amplitudes (0.06 and a relief weight of 0.3 to 0.6)
// Rynn's shadow on a Tundra slope could no longer be made out, although it was drawn. The fine octave has features of
// a few centimetres and the slow one of many metres; neither competes with a shadow. So the rule: the medium octave's
// peak to peak swing stays under half the weakest shadow at strength 1 and under the shadow at the top strength.
inline constexpr float kWeakestShadow = 21.0f / 128.0f;
inline constexpr float kTypicalReliefStep = 0.14f; // mean |noise - lit noise| of the volume (35.5 of 255)
inline constexpr float mediumScaleSwing(float strength) {
    return strength * 2.0f * (kOctaveAmplitude[1] + kReliefAmplitude * kReliefMedium * kTypicalReliefStep);
}
inline constexpr float kOctaveRotation[kOctaves][9] = {
    {1, 0, 0, 0, 1, 0, 0, 0, 1},
    {0.8657570f, -0.2803366f, 0.4145796f, 0.4145796f, 0.8657570f, -0.2803366f, -0.2803366f, 0.4145796f, 0.8657570f},
    {0.8288755f, -0.4966948f, -0.2574096f, -0.0066126f, 0.4513950f, -0.8922997f, 0.5593940f, 0.7413075f, 0.3708658f},
};
inline void rotate(const float m[9], const float v[3], float out[3]) {
    for (int r = 0; r < 3; ++r)
        out[r] = m[r * 3] * v[0] + m[r * 3 + 1] * v[1] + m[r * 3 + 2] * v[2];
}

// cbuffer of the shader fragment (register b3 on Direct3D 11); 16 float4.
struct ShaderConsts {
    float base[4];             // xyz camera-relative base, w = strength
    float d0[4];               // w = 1 when detail is on
    float dx[4];               // w = 1 when the side projection is on
    float dy[4];               // w = debug mode
    float octave[kOctaves][4]; // xyz = offset of the anchor in noise tiles (fraction), w = 1 / period
    float amplitude[4];        // fine, medium, slow, relief
    float sampling[4];         // w = gradient scale (2^lod bias); xyz unused
    float side[4];             // xyz = anchor modulo kSideModulo
    float relief[4];           // x = fine weight, y = medium weight, z = fade start, w = 1 / fade range
    float reliefFar[4];        // x = relief strength at the far end of the fade
    float grid[4];             // xy = anchor in grid cells relative to the grid origin, zw = grid size (0 = no grid)
    float gridHeight[4]; // x = anchor height (world y), y = height tolerance, z = kCliffStart, w = 1 / kCliffRange
    float pad[2][4];
};
static_assert(sizeof(ShaderConsts) == 16 * 16, "cbuffer layout");

inline void buildConsts(const Unproject& u, const Settings& s, float gradientScale, ShaderConsts& k,
                        const NormalGrid* grid = nullptr) {
    std::memset(&k, 0, sizeof(k));
    if (grid && !grid->empty()) {
        k.grid[0] = float(u.anchor[0] / double(kGridCell) - double(grid->originX));
        k.grid[1] = float(u.anchor[2] / double(kGridCell) - double(grid->originZ));
        k.grid[2] = float(grid->width);
        k.grid[3] = float(grid->height);
    }
    k.gridHeight[0] = float(u.anchor[1]);
    k.gridHeight[1] = kGridHeightTolerance;
    k.gridHeight[2] = kCliffStart;
    k.gridHeight[3] = 1.0f / kCliffRange;
    for (int i = 0; i < 3; ++i) {
        k.base[i] = u.base[i];
        k.d0[i] = u.d0[i];
        k.dx[i] = u.dx[i];
        k.dy[i] = u.dy[i];
        k.side[i] = float(std::fmod(u.anchor[i], kSideModulo));
    }
    const float strength = std::clamp(s.strength, 0.0f, 2.0f);
    k.base[3] = strength;
    k.d0[3] = s.detail ? 1.0f : 0.0f;
    k.dx[3] = s.cliffs ? 1.0f : 0.0f;
    k.dy[3] = float(s.debug);
    for (int o = 0; o < kOctaves; ++o) {
        // rotate the anchor in double, keep the fraction: the shader adds the rotated relative position.
        const float* m = kOctaveRotation[o];
        for (int r = 0; r < 3; ++r) {
            const double v = (double(m[r * 3]) * u.anchor[0] + double(m[r * 3 + 1]) * u.anchor[1] +
                              double(m[r * 3 + 2]) * u.anchor[2]) /
                             double(kOctavePeriod[o]);
            k.octave[o][r] = float(v - std::floor(v));
        }
        k.octave[o][3] = 1.0f / kOctavePeriod[o];
        k.amplitude[o] = kOctaveAmplitude[o];
    }
    k.amplitude[3] = kReliefAmplitude;
    k.sampling[3] = gradientScale;
    k.relief[0] = kReliefFine;
    k.relief[1] = kReliefMedium;
    k.relief[2] = kReliefNear;
    k.relief[3] = 1.0f / kReliefRange;
    k.reliefFar[0] = kReliefFar;
}

// ---- noise volume -----------------------------------------------------------------------------------------------
// size^3 bytes (x fastest), tiling in all three axes, mean 127.5. Deterministic: the same bytes on every machine.
namespace detail {
inline uint32_t hash3(uint32_t x, uint32_t y, uint32_t z, uint32_t seed) {
    uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u ^ z * 0xcb1ab31fu ^ seed * 0x165667b1u;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    h ^= h >> 15;
    return h;
}
inline float fade(float t) {
    return t * t * t * (t * (t * 6 - 15) + 10);
}
// Tiling gradient noise on a lattice of `cells` per axis, p in 0..1 (one tile).
inline float gradientNoise(float px, float py, float pz, uint32_t cells, uint32_t seed) {
    const float x = px * cells, y = py * cells, z = pz * cells;
    const int xi = int(std::floor(x)), yi = int(std::floor(y)), zi = int(std::floor(z));
    const float fx = x - xi, fy = y - yi, fz = z - zi;
    const float ux = fade(fx), uy = fade(fy), uz = fade(fz);
    float acc[2][2][2];
    for (int dz = 0; dz < 2; ++dz)
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx) {
                const uint32_t h =
                    hash3(uint32_t(xi + dx) % cells, uint32_t(yi + dy) % cells, uint32_t(zi + dz) % cells, seed);
                // 12 edge directions of a cube
                static const signed char g[12][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0},
                                                     {1, 0, 1}, {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
                                                     {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}};
                const signed char* d = g[h % 12];
                acc[dz][dy][dx] = d[0] * (fx - dx) + d[1] * (fy - dy) + d[2] * (fz - dz);
            }
    auto mix = [](float a, float b, float t) { return a + (b - a) * t; };
    return mix(mix(mix(acc[0][0][0], acc[0][0][1], ux), mix(acc[0][1][0], acc[0][1][1], ux), uy),
               mix(mix(acc[1][0][0], acc[1][0][1], ux), mix(acc[1][1][0], acc[1][1][1], ux), uy), uz);
}
} // namespace detail

// Channel 0 is the noise ("height"), channel 1 the same noise one relief step towards a fixed light direction in the
// volume's own frame, so the shader gets the relief term (height minus lit height) from the same fetch as the albedo
// term. Each octave samples the volume in its own rotated frame, so the three octaves are lit from three directions:
// that reads as roughness, not as a lit bump map, and needs no light from the level.
inline constexpr uint32_t kNoiseChannels = 2;
inline constexpr float kReliefLight[3] = {0.4504f, 0.7707f, 0.4504f}; // unit vector, volume frame
inline std::vector<uint8_t> buildNoiseVolume(uint32_t size) {
    const size_t count = size_t(size) * size * size;
    std::vector<float> f(count * kNoiseChannels);
    double sum = 0, sum2 = 0;
    auto noise = [](float px, float py, float pz) {
        px -= std::floor(px);
        py -= std::floor(py);
        pz -= std::floor(pz);
        // four lattice octaves; the coarse ones carry most of the energy, the fine ones the grain
        return detail::gradientNoise(px, py, pz, 4, 1) * 1.0f + detail::gradientNoise(px, py, pz, 8, 2) * 0.7f +
               detail::gradientNoise(px, py, pz, 16, 3) * 0.5f + detail::gradientNoise(px, py, pz, 32, 4) * 0.35f;
    };
    for (uint32_t z = 0; z < size; ++z)
        for (uint32_t y = 0; y < size; ++y)
            for (uint32_t x = 0; x < size; ++x) {
                const float px = (x + 0.5f) / size, py = (y + 0.5f) / size, pz = (z + 0.5f) / size;
                const float v = noise(px, py, pz);
                const size_t i = ((size_t(z) * size + y) * size + x) * kNoiseChannels;
                f[i] = v;
                f[i + 1] = noise(px + kReliefStep * kReliefLight[0], py + kReliefStep * kReliefLight[1],
                                 pz + kReliefStep * kReliefLight[2]);
                sum += v;
                sum2 += double(v) * v;
            }
    const double mean = sum / double(count), deviation = std::sqrt(std::max(sum2 / double(count) - mean * mean, 1e-12));
    std::vector<uint8_t> out(f.size());
    // 2.4 standard deviations span the byte range; the rest clips (under 2 % of the texels)
    const double gain = 127.5 / (2.4 * deviation);
    for (size_t i = 0; i < f.size(); ++i)
        out[i] = uint8_t(std::clamp(std::lround(127.5 + (f[i] - mean) * gain), 0l, 255l));
    return out;
}
// Next mip level of a cubic volume (box filter; keeps the mean).
inline std::vector<uint8_t> halveVolume(const std::vector<uint8_t>& src, uint32_t size) {
    const uint32_t h = size / 2, n = kNoiseChannels;
    std::vector<uint8_t> out(size_t(h) * h * h * n);
    for (uint32_t z = 0; z < h; ++z)
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < h; ++x)
                for (uint32_t c = 0; c < n; ++c) {
                    uint32_t acc = 0;
                    for (uint32_t k = 0; k < 8; ++k)
                        acc += src[((size_t(z * 2 + (k >> 2)) * size + (y * 2 + ((k >> 1) & 1))) * size + x * 2 +
                                    (k & 1)) *
                                       n +
                                   c];
                    out[((size_t(z) * h + y) * h + x) * n + c] = uint8_t((acc + 4) / 8);
                }
    return out;
}
inline constexpr uint32_t kNoiseSize = 64;

// ---- CPU reference of the shader's detail factor -----------------------------------------------------------------
// noise[o] / noiseLit[o]: the two channels of the volume sampled for octave o at the pixel, 0..1.
// The colour of the layer is multiplied by the result before fog.
inline float detailFactor(const float noise[kOctaves], const float noiseLit[2], float strength, float distance = 0) {
    float m = kOctaveAmplitude[0] * (noise[0] - 0.5f) * 2.0f + kOctaveAmplitude[1] * (noise[1] - 0.5f) * 2.0f;
    // noise is height: a surface that falls away towards the light faces it and is brighter
    m += kReliefAmplitude * ((noise[0] - noiseLit[0]) * kReliefFine + (noise[1] - noiseLit[1]) * kReliefMedium);
    m = m * reliefFade(distance) + kOctaveAmplitude[2] * (noise[2] - 0.5f) * 2.0f;
    return std::max(0.0f, 1.0f + strength * m);
}

} // namespace gsr::terrain
