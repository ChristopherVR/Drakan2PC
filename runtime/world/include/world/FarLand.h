// Far land of converted levels: pure rules (no runtime dependencies; world_far_land_test).
//
// The terrain draw (PAL 0x152330) draws only the layers of the view layer's `visible` list. The lists of the converted
// Order of the Flame levels are the PC's, made for the PC fog (12 to 30 cells): in Ruined Village no list names a floor
// or ceiling more than 40 cells away, and nearer ones only when the PC thought them visible from the ground, while the
// converted level keeps the PS2 view distance (fog from 26 to 75 cells) and a camera above Rynn. Hills and river banks
// were missing and the sky showed through, cut along their layer edges (docs/journal/2026-11-16/ootf-distance-view.md).
// These rules pick, for one view layer, the unlisted floors and ceilings within kReachCells that a line of sight from
// the camera's cell reaches over the level's ground, so the draw grows by what can be seen. Layers
// behind hills stay out (the closure of the lists that far scenery uses at a larger view distance draws them too and
// halved the frame rate at the Ruined Village spot).
//
// Longer lists written into the level itself (a converter try) broke the game: the lists are also read outside the
// terrain draw (cell lookup, object loops), and three of three runs crashed or hung the GPU. The runtime swaps the
// result in for the draw only (drakan_view_distance.cpp).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace world::far_land {

inline constexpr int kFloor = 0, kCeiling = 1, kWater = 2;
// Layers at least this far (cells, box to box) from the view layer are candidates. 0: at the owner's Ruined Village
// spot the river bank that filled the gap is 12 and 22 cells away and not listed (24 left it out).
inline constexpr float kNearCells = 0.0f;
// Up to this far beyond the level's view distance (cells): the margin the engine's own cell rectangle adds.
inline constexpr float kReachMargin = 2.0f;
// Default reach (cells, box to box): the PC fog's end (30 cells) plus that margin. The layers that filled the owner's
// gap are 12 to 31 cells away; the whole view distance (77) cost +131 % frame time on Arokh over the Islands (the
// camera sees most of the level from up there) and +13 % at the owner's spot, 30 cells +5 % over the Islands. Past
// 32 cells the haze (from 26 to 75 cells) hides most of what is left out.
inline constexpr float kReachCells = 32.0f;
// Eye heights (world units) above the view layer's vertices for observers spread over a layer (tests and calibration).
inline constexpr float kEyes[] = {1200.0f, 6000.0f};
// A ray passes a ground sample this far below it (world units) before it counts as blocked.
inline constexpr float kClearance = 64.0f;
// Cells at each end of a ray that are not tested: the observer's end (a camera is in the air; an eye over a vertex
// clears its own slope) and the target's own rim. A ray shorter than both together passes (the layers touch).
inline constexpr float kObserverSkip = 0.5f;
inline constexpr float kTargetSkip = 1.0f;

// One terrain layer: origin and size in cells, vertex heights in world units (row-major, (w + 1) * (h + 1)), and per
// cell whether it draws anything (false = a hole or an invisible cell, both triangles).
struct Layer {
    int type = kFloor;
    int x0 = 0, z0 = 0;
    int w = 0, h = 0;
    std::vector<float> heights;
    std::vector<uint8_t> drawn;
    bool usable() const {
        return w > 0 && h > 0 && heights.size() == size_t(w + 1) * size_t(h + 1) && drawn.size() == size_t(w) * h;
    }
    bool vertexDrawn(int x, int z) const {
        for (int dz = -1; dz <= 0; ++dz)
            for (int dx = -1; dx <= 0; ++dx) {
                const int cx = x + dx, cz = z + dz;
                if (cx >= 0 && cz >= 0 && cx < w && cz < h && drawn[size_t(cz) * w + cx])
                    return true;
            }
        return false;
    }
    float height(int x, int z) const { return heights[size_t(z) * (w + 1) + x]; }
};

// Distance in cells between two layer boxes on the ground plane, per axis (0 when they touch or overlap).
inline int boxDistance(const Layer& a, const Layer& b) {
    const int dx = std::max({b.x0 - (a.x0 + a.w), a.x0 - (b.x0 + b.w), 0});
    const int dz = std::max({b.z0 - (a.z0 + a.h), a.z0 - (b.z0 + b.h), 0});
    return std::max(dx, dz);
}

struct Point {
    float x, z, y; // cells, cells, world units
};

// Up to `per` x `per` evenly spaced drawn vertices of L, plus its `highest` highest drawn vertices.
inline std::vector<Point> samplePoints(const Layer& L, int per, int highest) {
    std::vector<Point> pts;
    std::vector<uint8_t> seen(size_t(L.w + 1) * (L.h + 1), 0);
    auto add = [&](int x, int z) {
        const size_t v = size_t(z) * (L.w + 1) + x;
        if (seen[v] || !L.vertexDrawn(x, z))
            return;
        seen[v] = 1;
        pts.push_back({float(L.x0 + x), float(L.z0 + z), L.heights[v]});
    };
    auto axis = [&](int n) {
        std::vector<int> out;
        const int k = std::min(per, n + 1);
        for (int i = 0; i < k; ++i)
            out.push_back(k == 1 ? 0 : int(std::lround(double(i) * n / (k - 1))));
        return out;
    };
    for (int z : axis(L.h))
        for (int x : axis(L.w))
            add(x, z);
    if (highest > 0) {
        std::vector<std::pair<float, int>> order;
        for (int z = 0; z <= L.h; ++z)
            for (int x = 0; x <= L.w; ++x)
                if (L.vertexDrawn(x, z))
                    order.push_back({-L.height(x, z), z * (L.w + 1) + x});
        const size_t n = std::min(order.size(), size_t(highest));
        std::partial_sort(order.begin(), order.begin() + n, order.end());
        for (size_t i = 0; i < n; ++i)
            add(order[i].second % (L.w + 1), order[i].second / (L.w + 1));
    }
    return pts;
}

// The highest drawn floor at every world vertex: what a line of sight has to clear.
class Ground {
public:
    explicit Ground(const std::vector<Layer>& layers) {
        int x1 = std::numeric_limits<int>::min(), z1 = x1;
        x0_ = z0_ = std::numeric_limits<int>::max();
        for (const Layer& L : layers)
            if (L.type == kFloor && L.usable()) {
                x0_ = std::min(x0_, L.x0);
                z0_ = std::min(z0_, L.z0);
                x1 = std::max(x1, L.x0 + L.w);
                z1 = std::max(z1, L.z0 + L.h);
            }
        if (x1 < x0_) {
            x0_ = z0_ = 0;
            return;
        }
        w_ = x1 - x0_ + 1;
        h_ = z1 - z0_ + 1;
        top_.assign(size_t(w_) * h_, -std::numeric_limits<float>::infinity());
        for (const Layer& L : layers) {
            if (L.type != kFloor || !L.usable())
                continue;
            for (int z = 0; z <= L.h; ++z)
                for (int x = 0; x <= L.w; ++x) {
                    if (!L.vertexDrawn(x, z))
                        continue;
                    float& t = top_[size_t(L.z0 + z - z0_) * w_ + (L.x0 + x - x0_)];
                    t = std::max(t, L.height(x, z));
                }
        }
    }

    // Ground height at (x, z) in cells (bilinear over the drawn corners), or -infinity over nothing.
    float at(float x, float z) const {
        const float gx = x - float(x0_), gz = z - float(z0_);
        if (w_ < 2 || h_ < 2 || gx < 0 || gz < 0 || gx > float(w_ - 1) || gz > float(h_ - 1))
            return -std::numeric_limits<float>::infinity();
        // The far edge row and column belong to the last cell.
        const int ix = std::min(int(gx), w_ - 2), iz = std::min(int(gz), h_ - 2);
        const float fx = gx - float(ix), fz = gz - float(iz);
        const size_t i = size_t(iz) * w_ + ix;
        const float a = top_[i], b = top_[i + 1], c = top_[i + w_], d = top_[i + w_ + 1];
        const float lo = -std::numeric_limits<float>::infinity();
        if (a == lo || b == lo || c == lo || d == lo)
            return std::max({a, b, c, d});
        return (a * (1 - fx) + b * fx) * (1 - fz) + (c * (1 - fx) + d * fx) * fz;
    }

    // Whether the segment from o to t clears the ground (ends excepted).
    bool clear(const Point& o, const Point& t) const {
        const float dx = t.x - o.x, dz = t.z - o.z;
        const float len = std::sqrt(dx * dx + dz * dz);
        const float span = len - kObserverSkip - kTargetSkip;
        if (span <= 0)
            return true;
        const int steps = std::max(1, int(std::ceil(span)));
        for (int s = 0; s <= steps; ++s) {
            const float f = (kObserverSkip + span * float(s) / float(steps)) / len;
            const float y = o.y + (t.y - o.y) * f;
            if (y < at(o.x + dx * f, o.z + dz * f) - kClearance)
                return false;
        }
        return true;
    }

private:
    int x0_ = 0, z0_ = 0, w_ = 0, h_ = 0;
    std::vector<float> top_;
};

inline std::vector<Point> observers(const Layer& L) {
    std::vector<Point> out;
    const std::vector<Point> base = samplePoints(L, 3, 0);
    for (float e : kEyes)
        for (Point p : base) {
            p.y += e;
            out.push_back(p);
        }
    return out;
}

inline std::vector<Point> targets(const Layer& L) {
    return samplePoints(L, 3, 12);
}

// The camera cells a list is kept for: 2048 units across (one terrain cell) and kCellHeight high.
inline constexpr float kCellHeight = 1024.0f;
// Observers for every camera position in one camera cell (x, z in cells, a height bin of kCellHeight): the cell's four
// corners and its centre at the top of the bin. So a list depends only on its cell and holds for any camera in it (a
// list from the first camera point alone could miss a layer seen from the far side of the cell). A cushion of 1.5
// cells and 1,500 units more height added 60 layers at the owner's Ruined Village spot against 21 to 26 for one
// camera point, for the same picture, and the frame time grows with the layers drawn, so the cell is kept small.
inline std::vector<Point> cellObservers(int cellX, int cellZ, int heightBin) {
    const float x = float(cellX), z = float(cellZ), y = float(heightBin + 1) * kCellHeight;
    return {{x, z, y}, {x + 1, z, y}, {x, z + 1, y}, {x + 1, z + 1, y}, {x + 0.5f, z + 0.5f, y}};
}

// Layers to add to view layer `view`'s list for the draw: floors and ceilings not in `listed`, between `near` and
// `reach` cells from the view layer's box, that one of `obs` sees over `ground`; nearest first. Only floor and water
// view layers get any (a camera in a ceiling layer is in a cave). `targetCache` (one slot per layer, empty = not yet
// sampled) keeps the target samples between calls.
inline std::vector<uint16_t> farLayers(size_t view, const std::vector<Layer>& layers, const Ground& ground,
                                       const std::vector<uint16_t>& listed, float reach, const std::vector<Point>& obs,
                                       std::vector<std::vector<Point>>& targetCache, float near = kNearCells) {
    std::vector<uint16_t> out;
    if (view >= layers.size() || obs.empty())
        return out;
    const Layer& L = layers[view];
    if ((L.type != kFloor && L.type != kWater) || !L.usable())
        return out;
    std::vector<uint8_t> have(layers.size(), 0);
    for (uint16_t i : listed)
        if (i < have.size())
            have[i] = 1;
    have[view] = 1;
    if (targetCache.size() != layers.size())
        targetCache.assign(layers.size(), {});
    std::vector<std::pair<int, uint16_t>> found;
    for (size_t j = 0; j < layers.size() && j <= 0xffff; ++j) {
        const Layer& B = layers[j];
        if (have[j] || (B.type != kFloor && B.type != kCeiling) || !B.usable())
            continue;
        const int d = boxDistance(L, B);
        if (float(d) < near || float(d) > reach)
            continue;
        std::vector<Point>& tgt = targetCache[j];
        if (tgt.empty())
            tgt = targets(B);
        bool seen = false;
        for (const Point& o : obs) {
            for (const Point& t : tgt)
                if (ground.clear(o, t)) {
                    seen = true;
                    break;
                }
            if (seen)
                break;
        }
        if (seen)
            found.push_back({d, uint16_t(j)});
    }
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& f : found)
        out.push_back(f.second);
    return out;
}

// The same with observers spread over the view layer (kEyes above up to 3 x 3 of its vertices).
inline std::vector<uint16_t> farLayers(size_t view, const std::vector<Layer>& layers, const Ground& ground,
                                       const std::vector<uint16_t>& listed, float reach,
                                       std::vector<std::vector<Point>>& targetCache) {
    if (view >= layers.size())
        return {};
    return farLayers(view, layers, ground, listed, reach, observers(layers[view]), targetCache);
}

} // namespace world::far_land
