#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gsr::sprite_footprint {
struct Bounds {
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    bool valid = false;
};

// Preserve the texel cells covered by the original integer-centred GS raster.
// Enlarging a sprite must not expose an adjacent atlas cell that the original
// draw never sampled. Keep every subtexel of an HD replacement inside those
// cells. This is deliberately limited to one rectangular, affine sprite.
template <class V>
Bounds bounds(const V* v, uint32_t count, bool fst, float width, float height, unsigned textureFactor) {
    Bounds out;
    if (count != 6 || textureFactor == 0)
        return out;
    float x0 = v[0].x, x1 = x0, y0 = v[0].y, y1 = y0;
    for (unsigned i = 0; i < 6; ++i) {
        if (!std::isfinite(v[i].x) || !std::isfinite(v[i].y) || !std::isfinite(v[i].s) || !std::isfinite(v[i].t) ||
            !std::isfinite(v[i].q) || (!fst && (v[i].q <= 0 || v[i].q != v[0].q)))
            return out;
        x0 = std::min(x0, v[i].x);
        x1 = std::max(x1, v[i].x);
        y0 = std::min(y0, v[i].y);
        y1 = std::max(y1, v[i].y);
    }
    if (x1 - x0 < 1 || y1 - y0 < 1)
        return out;
    float us[4]{}, vs[4]{};
    unsigned corners = 0;
    for (unsigned i = 0; i < 6; ++i) {
        if ((v[i].x != x0 && v[i].x != x1) || (v[i].y != y0 && v[i].y != y1))
            return out;
        unsigned c = (v[i].x == x1 ? 1u : 0u) + (v[i].y == y1 ? 2u : 0u);
        float u = fst ? v[i].s : v[i].s / v[i].q * width;
        float t = fst ? v[i].t : v[i].t / v[i].q * height;
        if (!std::isfinite(u) || !std::isfinite(t))
            return out;
        if ((corners & (1u << c)) && (std::fabs(us[c] - u) > .001f || std::fabs(vs[c] - t) > .001f))
            return out;
        us[c] = u;
        vs[c] = t;
        corners |= 1u << c;
    }
    if (corners != 15 || std::fabs(us[0] - us[2]) > .001f || std::fabs(us[1] - us[3]) > .001f ||
        std::fabs(vs[0] - vs[1]) > .001f || std::fabs(vs[2] - vs[3]) > .001f)
        return out;
    // Only native one-texel-per-pixel artwork is established by the captures.
    if (std::fabs(std::fabs(us[1] - us[0]) / (x1 - x0) - 1) > .001f ||
        std::fabs(std::fabs(vs[2] - vs[0]) / (y1 - y0) - 1) > .001f)
        return out;
    auto axis = [&](float a, float b, float ta, float tb, float& lo, float& hi) {
        const float first = std::ceil(a), last = std::ceil(b) - 1;
        if (first > last)
            return false;
        float p = ta + (tb - ta) * (first - a) / (b - a), q = ta + (tb - ta) * (last - a) / (b - a);
        lo = std::floor(std::min(p, q)) + .5f / textureFactor;
        hi = std::floor(std::max(p, q)) + 1 - .5f / textureFactor;
        return true;
    };
    out.valid = axis(x0, x1, us[0], us[1], out.u0, out.u1) && axis(y0, y1, vs[0], vs[2], out.v0, out.v1);
    return out;
}
} // namespace gsr::sprite_footprint
