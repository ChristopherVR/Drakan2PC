// Where a world's object-motion target may hold a valid entry ([video] velocity_skip, env GSR_VELOCITY_SKIP).
//
// With DLSS on every world draw is followed by a velocity pass into the object-motion target (gs_draw.inl,
// drawVelocity). Its pixel program writes (motion, valid, depth); the only reader, the guide pass (nr_guides.cpp),
// takes an entry only where valid > 0.5 and uses the camera motion everywhere else. A draw none of whose vertices
// carries a previous position (valid 0 in every motion::Previous: untracked terrain and scenery, rejected or
// unmatched objects) can only write invalid entries. Writing an invalid entry over a cleared or invalid one changes
// nothing the guide reads; only an invalid entry over a valid one does. So such a draw's velocity pass (and the upload
// of its all-zero previous stream) can be left out exactly when no valid entry lies under it.
//
// Grid keeps, per world, a conservative map of where valid entries may be: kTiles x kTiles tiles over the target, a
// tile marked when a draw that may write a valid entry covers part of it. A draw is tested and marked with the
// bounding box of its vertices in target pixels plus a margin (jitter and rasterisation rules stay inside it). Marked
// tiles never clear before the target does, so the test only ever says "may be valid" too often. GPU free.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace motion_coverage {

// Pixels [x0, x1) x [y0, y1) of the target; empty when x0 >= x1 or y0 >= y1.
struct Rect {
    int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool empty() const { return x0 >= x1 || y0 >= y1; }
};

// The bounding box of the vertices' positions (x, y in GS pixels of the target, scaled by sx, sy into target pixels)
// grown by margin pixels on every side and clipped to a width x height target. A vertex that is not finite makes the
// box the whole target.
template <class Vertex>
Rect bounds(const Vertex* v, uint32_t count, float sx, float sy, int32_t margin, uint32_t width, uint32_t height) {
    Rect whole{0, 0, (int32_t)width, (int32_t)height};
    if (!count)
        return {};
    float minX = v[0].x, maxX = v[0].x, minY = v[0].y, maxY = v[0].y;
    for (uint32_t i = 1; i < count; ++i) {
        minX = std::min(minX, v[i].x);
        maxX = std::max(maxX, v[i].x);
        minY = std::min(minY, v[i].y);
        maxY = std::max(maxY, v[i].y);
    }
    const double x0 = std::floor((double)minX * sx) - margin, x1 = std::ceil((double)maxX * sx) + margin + 1;
    const double y0 = std::floor((double)minY * sy) - margin, y1 = std::ceil((double)maxY * sy) + margin + 1;
    if (!std::isfinite(x0) || !std::isfinite(x1) || !std::isfinite(y0) || !std::isfinite(y1))
        return whole;
    Rect r;
    r.x0 = (int32_t)std::clamp(x0, 0.0, (double)width);
    r.x1 = (int32_t)std::clamp(x1, 0.0, (double)width);
    r.y0 = (int32_t)std::clamp(y0, 0.0, (double)height);
    r.y1 = (int32_t)std::clamp(y1, 0.0, (double)height);
    return r;
}

class Grid {
public:
    static constexpr uint32_t kTiles = 64; // per axis: one bit per tile in a 64-bit row

    // A cleared target of width x height pixels: no valid entry anywhere.
    void reset(uint32_t width, uint32_t height) {
        width_ = width;
        height_ = height;
        tileW_ = std::max(1u, (width + kTiles - 1) / kTiles);
        tileH_ = std::max(1u, (height + kTiles - 1) / kTiles);
        std::fill(std::begin(rows_), std::end(rows_), 0ull);
        any_ = false;
    }
    // Valid entries may lie anywhere in r.
    void mark(const Rect& r) {
        if (r.empty())
            return;
        uint64_t bits;
        uint32_t ty0, ty1;
        if (!span(r, bits, ty0, ty1))
            return;
        for (uint32_t ty = ty0; ty <= ty1; ++ty)
            rows_[ty] |= bits;
        any_ = true;
    }
    void markAll() { mark({0, 0, (int32_t)width_, (int32_t)height_}); }
    // May a valid entry lie in r?
    bool touches(const Rect& r) const {
        if (!any_ || r.empty())
            return false;
        uint64_t bits;
        uint32_t ty0, ty1;
        if (!span(r, bits, ty0, ty1))
            return false;
        for (uint32_t ty = ty0; ty <= ty1; ++ty)
            if (rows_[ty] & bits)
                return true;
        return false;
    }
    bool any() const { return any_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }

private:
    bool span(const Rect& r, uint64_t& bits, uint32_t& ty0, uint32_t& ty1) const {
        const int32_t x0 = std::max(r.x0, 0), y0 = std::max(r.y0, 0);
        const int32_t x1 = std::min<int32_t>(r.x1, (int32_t)width_), y1 = std::min<int32_t>(r.y1, (int32_t)height_);
        if (x0 >= x1 || y0 >= y1)
            return false;
        const uint32_t tx0 = (uint32_t)x0 / tileW_, tx1 = std::min(kTiles - 1, (uint32_t)(x1 - 1) / tileW_);
        ty0 = (uint32_t)y0 / tileH_;
        ty1 = std::min(kTiles - 1, (uint32_t)(y1 - 1) / tileH_);
        const uint32_t n = tx1 - tx0 + 1;
        bits = (n >= 64 ? ~0ull : ((1ull << n) - 1)) << tx0;
        return true;
    }
    uint64_t rows_[kTiles]{};
    uint32_t width_ = 0, height_ = 0, tileW_ = 1, tileH_ = 1;
    bool any_ = false;
};

} // namespace motion_coverage
