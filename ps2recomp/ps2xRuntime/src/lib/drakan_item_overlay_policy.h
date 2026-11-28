// Depth clear under a native 3D item overlay (GitHub issue 22): the math of drakan_item_overlay.cpp, std only.
//
// The item overlay draw 0x3aff40 (Quick Inventory bar 0x3b1288, its pickup display and a GUI item list 0x2c1210)
// draws an item's 3D model over the finished world. Before the model it projects the eight corners of the item
// handle's own bounding box (handle+0xb8: origin and three edge vectors) with 0x1336a8 and clears the depth under
// that screen rectangle (render object vtable +0x44 = 0x1342e0 -> 0x146df8: ZTST ALWAYS, ZMSK 0, FBMSK all, 64 x 32
// sprites at Z 0). The model is then drawn with GEQUAL against the world depth, followed by the item's part objects
// (instance+0x108, drawn by 0x3b0868 with the same matrix). The parts are not in that box, so where a part (the
// club's head) leaves the rectangle, nearer world geometry hides it. Coverage() widens the native rectangle by the
// parts' own boxes, projected the same way.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace drakan_item_overlay {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

// A bounding box as the engine keeps it at handle+0xb8: a corner and the three edge vectors.
struct Box {
    Vec3 origin, a, b, c;
};

// The screen rectangle 0x1342e0 takes: whole pixels in the render object's frame, inclusive-exclusive like the
// guest's test (x0 < x1 and y0 < y1, else nothing is cleared).
struct Rect {
    int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool valid() const { return x0 < x1 && y0 < y1; }
};

// 0x1336a8: world point -> screen, with the camera of the world view struct (0x4dad58: x / y / w rows at +0x0c /
// +0x18 / +0x24, camera position at +0x6c) and the render object (offset = (+0x50 or +0x58) - (2048 - frame / 2) +
// viewport / 2, scale = +0x40 * +0x48 or +0x44 * +0x4c).
struct Projector {
    Vec3 camera, rowX, rowY, rowW;
    float offsetX = 0, offsetY = 0, scaleX = 0, scaleY = 0;

    // False for a point on or behind the camera plane, where the guest's division has no useful result.
    bool project(const Vec3& p, float& x, float& y) const {
        const float dx = p.x - camera.x, dy = p.y - camera.y, dz = p.z - camera.z;
        const float w = -(dx * rowW.x + dy * rowW.y + dz * rowW.z);
        if (!(w > 1e-6f) || !std::isfinite(w))
            return false;
        const float inv = 1.0f / w;
        x = offsetX + (dx * rowX.x + dy * rowX.y + dz * rowX.z) * inv * scaleX;
        y = offsetY - (dx * rowY.x + dy * rowY.y + dz * rowY.z) * inv * scaleY;
        return std::isfinite(x) && std::isfinite(y);
    }
};

// cvt.w.s as the guest rounds the projected corners: toward zero, saturated.
inline int32_t truncate(float v) {
    if (!(v > -2147483520.0f))
        return std::numeric_limits<int32_t>::min();
    if (!(v < 2147483520.0f))
        return std::numeric_limits<int32_t>::max();
    return static_cast<int32_t>(v);
}

// The rectangle 0x3aff40 builds for a box: min / max of its eight projected corners. False when a corner cannot be
// projected (the box is not usable for the clear).
inline bool boxRect(const Projector& projector, const Box& box, Rect& out) {
    const Vec3& o = box.origin;
    const Vec3 corners[8] = {
        o,
        {o.x + box.a.x, o.y + box.a.y, o.z + box.a.z},
        {o.x + box.b.x, o.y + box.b.y, o.z + box.b.z},
        {o.x + box.c.x, o.y + box.c.y, o.z + box.c.z},
        {o.x + box.a.x + box.b.x, o.y + box.a.y + box.b.y, o.z + box.a.z + box.b.z},
        {o.x + box.a.x + box.c.x, o.y + box.a.y + box.c.y, o.z + box.a.z + box.c.z},
        {o.x + box.b.x + box.c.x, o.y + box.b.y + box.c.y, o.z + box.b.z + box.c.z},
        {o.x + box.a.x + box.b.x + box.c.x, o.y + box.a.y + box.b.y + box.c.y, o.z + box.a.z + box.b.z + box.c.z},
    };
    Rect r{std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::max(),
           std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::min()};
    for (const Vec3& corner : corners) {
        float x = 0, y = 0;
        if (!projector.project(corner, x, y))
            return false;
        const int32_t ix = truncate(x), iy = truncate(y);
        r.x0 = std::min(r.x0, ix);
        r.y0 = std::min(r.y0, iy);
        r.x1 = std::max(r.x1, ix);
        r.y1 = std::max(r.y1, iy);
    }
    out = r;
    return true;
}

// Combines rectangles: one that has no area is ignored.
class Coverage {
public:
    // `item` is the native rectangle; `frameW` x `frameH` the render object's frame (+0xf8 / +0xfc).
    Coverage(const Rect& item, int32_t frameW, int32_t frameH) : item_(item), out_(item) {
        if (!item.valid() || frameW <= 0 || frameH <= 0)
            return;
        // A part box is accepted near the item only: a stale box (a part last drawn somewhere else, before the bar
        // opened) must not clear the depth of half the screen. A part touches the object it belongs to, so its box
        // reaches the item's rectangle grown by the item's own size; the result is held inside the rectangle grown by
        // twice that and inside the frame.
        const int64_t size = std::max<int64_t>(int64_t(item.x1) - item.x0, int64_t(item.y1) - item.y0);
        near_ = grow(item, size);
        limit_ = grow(item, 2 * size);
        limit_.x0 = std::max<int32_t>(limit_.x0, 0);
        limit_.y0 = std::max<int32_t>(limit_.y0, 0);
        limit_.x1 = std::min<int32_t>(limit_.x1, frameW);
        limit_.y1 = std::min<int32_t>(limit_.y1, frameH);
        usable_ = limit_.valid();
    }

    // Adds a part's rectangle. False when it was not used (no area, or too far from the item).
    bool add(const Rect& part) {
        if (!usable_ || !part.valid() || part.x1 < near_.x0 || part.x0 > near_.x1 || part.y1 < near_.y0 ||
            part.y0 > near_.y1)
            return false;
        // Clipped to the limit, but never smaller than the native rectangle.
        out_.x0 = std::min(out_.x0, std::max(part.x0, limit_.x0));
        out_.y0 = std::min(out_.y0, std::max(part.y0, limit_.y0));
        out_.x1 = std::max(out_.x1, std::min(part.x1, limit_.x1));
        out_.y1 = std::max(out_.y1, std::min(part.y1, limit_.y1));
        return true;
    }

    const Rect& rect() const { return out_; }
    bool widened() const {
        return out_.x0 != item_.x0 || out_.y0 != item_.y0 || out_.x1 != item_.x1 || out_.y1 != item_.y1;
    }

private:
    static Rect grow(const Rect& r, int64_t by) {
        const auto clamp = [](int64_t v) {
            return static_cast<int32_t>(
                std::clamp<int64_t>(v, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
        };
        return {clamp(r.x0 - by), clamp(r.y0 - by), clamp(r.x1 + by), clamp(r.y1 + by)};
    }

    Rect item_, out_, near_, limit_;
    bool usable_ = false;
};

} // namespace drakan_item_overlay
