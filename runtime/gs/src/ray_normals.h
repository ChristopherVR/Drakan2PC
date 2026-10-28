#pragma once
#include "surface_geometry.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gsr::surface {
// Float4 material attributes per ray triangle: three vertex colours, UVs, UV and face normal, flags, and the three
// packed vertex normals. ray_water.hlsl indexes them by instance base + primitive with the same stride.
inline constexpr size_t kTriangleAttributes = 7;

// Octahedral unit vector, 12 bits per axis, stored as an integer below 2^24 so the float attribute holds it exactly
// (no NaN bit patterns). Decoded by unpackNormal in ray_water.hlsl.
inline float packNormal(Point n) {
    const float l1 = std::abs(n.x) + std::abs(n.y) + std::abs(n.z);
    if (!(l1 > 0) || !std::isfinite(l1))
        n = {0, 0, 1};
    else
        n = {n.x / l1, n.y / l1, n.z / l1};
    float u = n.x, v = n.y;
    if (n.z < 0) {
        u = (1 - std::abs(n.y)) * (n.x >= 0 ? 1.f : -1.f);
        v = (1 - std::abs(n.x)) * (n.y >= 0 ? 1.f : -1.f);
    }
    const auto quantize = [](float x) { return uint32_t(std::lround((std::clamp(x, -1.f, 1.f) * .5f + .5f) * 4095)); };
    return float(quantize(u) | quantize(v) << 12);
}

inline Point faceNormal(const Point (&p)[3]) {
    auto n = cross(sub(p[1], p[0]), sub(p[2], p[0]));
    const float len = length(n);
    return len > 0 ? Point{n.x / len, n.y / len, n.z / len} : n;
}

// Corners of a triangle soup that share an exact position: `corners` lists corner indices (triangle * 3 + corner)
// grouped by position, each group ending before the matching `ends` entry. Welding sorts every corner, so callers
// keep it per topology: an animated mesh's poses move welded corners together.
struct CornerWelds {
    std::vector<uint32_t> corners, ends;
};
// `corners(i, p)` fills triangle i's three positions.
template <class Corners>
void weldCorners(size_t triangles, Corners corners, CornerWelds& out) {
    struct Corner {
        float x, y, z;
        uint32_t index;
    };
    std::vector<Corner> sorted(triangles * 3);
    for (size_t i = 0; i < triangles; ++i) {
        Point p[3];
        corners(i, p);
        for (unsigned j = 0; j < 3; ++j)
            sorted[i * 3 + j] = {p[j].x, p[j].y, p[j].z, uint32_t(i * 3 + j)};
    }
    std::sort(sorted.begin(), sorted.end(), [](const Corner& a, const Corner& b) {
        return a.x != b.x ? a.x < b.x : a.y != b.y ? a.y < b.y : a.z < b.z;
    });
    out.corners.resize(sorted.size());
    out.ends.clear();
    for (size_t i = 0; i < sorted.size(); ++i) {
        if (i && (sorted[i].x != sorted[i - 1].x || sorted[i].y != sorted[i - 1].y || sorted[i].z != sorted[i - 1].z))
            out.ends.push_back(uint32_t(i));
        out.corners[i] = sorted[i].index;
    }
    if (!sorted.empty())
        out.ends.push_back(uint32_t(sorted.size()));
}

// Per-corner vertex normals, like the GS's per-vertex (Gouraud) lighting: the area-weighted face normals of every
// face welded to the corner, limited to faces within 60 degrees of the corner's own face so hard edges (walls, roof
// lines, cliffs meeting ground) stay sharp. `out` receives 3 normals per triangle.
template <class Corners>
void smoothNormals(size_t triangles, Corners corners, const CornerWelds& welds, std::vector<Point>& out) {
    constexpr float kSmoothCosine = .5f;
    std::vector<Point> area(triangles), unit(triangles);
    for (size_t i = 0; i < triangles; ++i) {
        Point p[3];
        corners(i, p);
        area[i] = cross(sub(p[1], p[0]), sub(p[2], p[0]));
        const float len = length(area[i]);
        if (!(len > 0) || !std::isfinite(len))
            area[i] = {};
        unit[i] = len > 0 && std::isfinite(len) ? Point{area[i].x / len, area[i].y / len, area[i].z / len} : Point{};
    }
    out.assign(triangles * 3, {});
    if (welds.corners.size() != triangles * 3) { // stale welds: face normals
        for (size_t i = 0; i < triangles * 3; ++i)
            out[i] = unit[i / 3];
        return;
    }
    uint32_t first = 0;
    for (const uint32_t end : welds.ends) {
        for (uint32_t a = first; a < end; ++a) {
            const Point own = unit[welds.corners[a] / 3];
            Point sum{};
            for (uint32_t b = first; b < end; ++b) {
                const auto face = welds.corners[b] / 3;
                if (own.x * unit[face].x + own.y * unit[face].y + own.z * unit[face].z < kSmoothCosine)
                    continue;
                sum = {sum.x + area[face].x, sum.y + area[face].y, sum.z + area[face].z};
            }
            const float len = length(sum);
            out[welds.corners[a]] = len > 0 && std::isfinite(len) ? Point{sum.x / len, sum.y / len, sum.z / len} : own;
        }
        first = end;
    }
}
template <class Corners>
void smoothNormals(size_t triangles, Corners corners, std::vector<Point>& out) {
    CornerWelds welds;
    weldCorners(triangles, corners, welds);
    smoothNormals(triangles, corners, welds, out);
}
} // namespace gsr::surface
