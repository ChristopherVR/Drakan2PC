#pragma once
#include <gsr/scene_geometry.h>
#include "ray_scene.h"
#include <memory>
#include <span>
#include <unordered_map>
#include <cfenv>
#if (defined(_M_X64) || defined(_M_IX86) || defined(__SSE__)) && !defined(__EMSCRIPTEN__)
#include <xmmintrin.h>
#endif

namespace gsr::surface {
// Full native layer snapshot. Visibility in the current view never controls
// lifetime. A new snapshot atomically replaces geometry and material mappings.
class ResidentScene {
    struct Projection {
        float u[3]{}, v[3]{};
        uint32_t texture = ~0u;
        float area = 0;
    };
    std::vector<uint32_t> layers;
    // Triangle indices per 2048-unit cell (by centroid), in insertion order.
    std::unordered_map<int64_t, std::vector<uint32_t>> cells;
    std::unordered_map<uint32_t, Projection> materials;
    // Match candidates stored contiguously per cell and water flag, in the cell's insertion order, with the terms
    // match() needs. Degenerate triangles (|det| < 1 or normal length < 1) never match and are left out. Rebuilt by
    // prepareMatch() after replace() or when the floating-point mode changes.
    // rdet = 1/det feeds a conservative multiply-only prefilter; the exact test (with the division) decides.
    struct MatchEntry {
        float x0, y0, z0, ax, az, bx, bz, det, rdet, nx, ny, nz, length;
        uint32_t index;
    };
    struct CellRange {
        uint32_t first[2]{}, count[2]{};
    };
    mutable std::vector<MatchEntry> matchEntries;
    mutable std::unordered_map<int64_t, CellRange> matchCells;
    // Consecutive draw triangles mostly fall into one cell: remember the last lookup.
    mutable int64_t lastCell = 0;
    mutable const CellRange* lastRange = nullptr;
    mutable bool lastValid = false;
    mutable unsigned matchMode = ~0u, projectionMode = ~0u;
    mutable std::shared_ptr<std::vector<Triangle>> projected;
    mutable bool projectionDirty = true;
    mutable uint64_t projectedVersion = 0;
    static unsigned mathMode() {
#if (defined(_M_X64) || defined(_M_IX86) || defined(__SSE__)) && !defined(__EMSCRIPTEN__)
        return _mm_getcsr() & 0xe040u; // rounding, flush-to-zero, denormals-are-zero
#else
        return unsigned(std::fegetround());
#endif
    }
    void prepareMatch() const {
        const auto mode = mathMode();
        if (matchMode == mode)
            return;
        matchEntries.clear();
        matchEntries.reserve(triangles.size());
        matchCells.clear();
        matchCells.reserve(cells.size());
        lastValid = false;
        for (const auto& [key, indices] : cells) {
            CellRange range;
            for (unsigned water = 0; water < 2; ++water) {
                range.first[water] = uint32_t(matchEntries.size());
                for (auto index : indices) {
                    const auto& tri = triangles[index];
                    if (tri.water != (water != 0))
                        continue;
                    const float ax = tri.p[1].x - tri.p[0].x, az = tri.p[1].z - tri.p[0].z,
                                bx = tri.p[2].x - tri.p[0].x, bz = tri.p[2].z - tri.p[0].z;
                    const float det = ax * bz - az * bx;
                    const auto normal = cross(sub(tri.p[1], tri.p[0]), sub(tri.p[2], tri.p[0]));
                    const float len = length(normal);
                    if (std::abs(det) < 1 || len < 1)
                        continue;
                    matchEntries.push_back({tri.p[0].x, tri.p[0].y, tri.p[0].z, ax, az, bx, bz, det, 1.f / det,
                                            normal.x, normal.y, normal.z, len, index});
                }
                range.count[water] = uint32_t(matchEntries.size()) - range.first[water];
            }
            if (range.count[0] || range.count[1])
                matchCells.emplace(key, range);
        }
        matchMode = mode;
    }
    static int64_t cell(float x, float z) {
        return int64_t(uint64_t(uint32_t(int32_t(std::floor(x / 2048.f)))) << 32 |
                       uint32_t(int32_t(std::floor(z / 2048.f))));
    }

public:
    uint64_t identity = 0;
    std::vector<Triangle> triangles;
    uint32_t waterCount = 0, layerCount = 0;
    uint64_t geometryVersion = 0; // positions change only through replace()
    void replace(const SceneGeometry& input) {
        identity = input.identity;
        ++geometryVersion;
        layerCount = input.layers;
        waterCount = 0;
        triangles.clear();
        layers.clear();
        cells.clear();
        materials.clear();
        matchEntries.clear();
        matchCells.clear();
        lastValid = false;
        projected.reset(); // holders keep the previous level's triangles
        projectionDirty = true;
        matchMode = projectionMode = ~0u;
        triangles.reserve(input.triangles.size());
        layers.reserve(input.triangles.size());
        for (const auto& source : input.triangles) {
            Triangle tri;
            tri.water = source.water != 0;
            tri.vertexLit = true;
            bool valid = true;
            for (unsigned i = 0; i < 3; ++i)
                for (unsigned c = 0; c < 3; ++c) {
                    if (c == 0)
                        tri.p[i] = {source.position[i][0], source.position[i][1], source.position[i][2]};
                    tri.lighting[i][c] = source.color[i][c];
                    valid = valid && std::isfinite(source.position[i][c]) && std::isfinite(source.color[i][c]);
                }
            if (!valid)
                continue;
            const auto index = uint32_t(triangles.size());
            triangles.push_back(tri);
            layers.push_back(source.layer);
            waterCount += tri.water;
            const auto center =
                Point{(tri.p[0].x + tri.p[1].x + tri.p[2].x) / 3, 0, (tri.p[0].z + tri.p[1].z + tri.p[2].z) / 3};
            cells[cell(center.x, center.z)].push_back(index);
        }
    }
    // Index of the first resident triangle (insertion order) in the draw centroid's cell with this water flag whose
    // xz projection contains the centroid (1% barycentric tolerance) and whose plane holds all three draw vertices
    // within 32 units; -1 when there is none.
    int match(const Triangle& draw, bool water) const {
        prepareMatch();
        const auto p =
            Point{(draw.p[0].x + draw.p[1].x + draw.p[2].x) / 3, (draw.p[0].y + draw.p[1].y + draw.p[2].y) / 3,
                  (draw.p[0].z + draw.p[1].z + draw.p[2].z) / 3};
        const auto key = cell(p.x, p.z);
        if (!lastValid || lastCell != key) {
            const auto it = matchCells.find(key);
            lastRange = it == matchCells.end() ? nullptr : &it->second;
            lastCell = key;
            lastValid = true;
        }
        if (!lastRange)
            return -1;
        const MatchEntry* entry = matchEntries.data() + lastRange->first[water];
        const MatchEntry* const end = entry + lastRange->count[water];
        for (; entry != end; ++entry) {
            const auto& g = *entry;
            const float nu = (p.x - g.x0) * g.bz - (p.z - g.z0) * g.bx;
            const float nv = g.ax * (p.z - g.z0) - g.az * (p.x - g.x0);
            // n * (1/det) differs from n / det by a few ulps (< 1e-6 relative), so these margins only reject
            // entries the exact test below rejects too. NaN never compares true and falls through to the exact test.
            const float pu = nu * g.rdet, pv = nv * g.rdet;
            if (pu < -.0101f || pv < -.0101f || pu + pv - 1e-5f * (std::abs(pu) + std::abs(pv)) > 1.0101f)
                continue;
            const float u = nu / g.det;
            const float v = nv / g.det;
            if (u < -.01f || v < -.01f || u + v > 1.01f)
                continue;
            const float limit = g.length * 32.f;
            bool samePlane = true;
            for (const auto& q : draw.p)
                if (std::abs(g.nx * (q.x - g.x0) + g.ny * (q.y - g.y0) + g.nz * (q.z - g.z0)) > limit) {
                    samePlane = false;
                    break;
                }
            if (samePlane)
                return int(g.index);
        }
        return -1;
    }
    void observe(const Triangle& draw, int index) {
        if (index < 0 || draw.texture == ~0u)
            return;
        const float ax = draw.p[1].x - draw.p[0].x, az = draw.p[1].z - draw.p[0].z, bx = draw.p[2].x - draw.p[0].x,
                    bz = draw.p[2].z - draw.p[0].z;
        const float det = ax * bz - az * bx;
        if (std::abs(det) < 10000)
            return;
        const auto previous = materials.find(layers[index]);
        if (previous != materials.end() && previous->second.area >= std::abs(det))
            return;
        Projection m;
        m.texture = draw.texture;
        m.area = std::abs(det);
        for (unsigned component = 0; component < 2; ++component) {
            float* coefficients = component ? m.v : m.u;
            const float a = draw.uv[1][component] - draw.uv[0][component],
                        b = draw.uv[2][component] - draw.uv[0][component];
            coefficients[0] = (a * bz - b * az) / det;
            coefficients[1] = (ax * b - bx * a) / det;
            coefficients[2] = draw.uv[0][component] - coefficients[0] * draw.p[0].x - coefficients[1] * draw.p[0].z;
        }
        materials[layers[index]] = m;
        projectionDirty = true;
    }
    // Triangles with the observed material projections applied. Valid until the next replace/observe call.
    std::span<const Triangle> projectedTriangles() const { return *projectedShared(); }
    // The same triangles as an immutable vector that stays valid for its holders: a new projection is built in a
    // new vector unless nobody else holds the previous one.
    std::shared_ptr<const std::vector<Triangle>> projectedShared() const {
        const auto mode = mathMode();
        if (!projected || projectionDirty || projectionMode != mode) {
            if (!projected || projected.use_count() > 1)
                projected = std::make_shared<std::vector<Triangle>>();
            auto& out = *projected;
            out.clear();
            out.reserve(triangles.size());
            for (size_t i = 0; i < triangles.size(); ++i) {
                auto t = triangles[i];
                const auto found = materials.find(layers[i]);
                if (found != materials.end()) {
                    const auto& m = found->second;
                    t.texture = m.texture;
                    for (unsigned j = 0; j < 3; ++j) {
                        t.uv[j][0] = m.u[0] * t.p[j].x + m.u[1] * t.p[j].z + m.u[2];
                        t.uv[j][1] = m.v[0] * t.p[j].x + m.v[1] * t.p[j].z + m.v[2];
                    }
                }
                out.push_back(t);
            }
            projectionDirty = false;
            projectionMode = mode;
            ++projectedVersion;
        }
        return projected;
    }
    // Changes whenever projectedTriangles() would return different attributes.
    uint64_t attributeVersion() const {
        projectedTriangles();
        return projectedVersion;
    }
    void append(std::vector<Triangle>& out) const {
        const auto source = projectedTriangles();
        out.insert(out.end(), source.begin(), source.end());
    }
};
} // namespace gsr::surface
