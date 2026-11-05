#pragma once
#include "gs_internal.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace gsr {
struct SolidMirror {
    uint32_t x, y, w, h, rgba;
};

// Prove a bounded sprite has constant, destination-independent CT32 output.
// haveTexture is the effective shader state, including unsupported-texture fallback.
inline std::optional<SolidMirror> solidMirror(const GsDrawState& s, const DrawVertex* v, uint32_t count,
                                              bool haveTexture, uint32_t width, uint32_t height) {
    if (haveTexture || count != 6 || !(s.flags & DF_SPRITE) ||
        (s.flags & (DF_FGE | DF_DTHE | DF_AA1 | DF_LINE | DF_POINT)) || bitsOf(s.frame, 24, 6) != PSM_CT32 ||
        (s.frame >> 32) || !(s.zbuf & (1ull << 32)) || (s.test & (1ull << 14)) ||
        ((s.test & 1) && bitsOf(s.test, 1, 3) != 1) || ((s.test & (1ull << 16)) && bitsOf(s.test, 17, 2) != 1))
        return {};
    // (A-B)*C+D is exactly source RGB only when A==B and D==source.
    if ((s.flags & DF_ABE) && (bitsOf(s.alpha, 0, 2) != bitsOf(s.alpha, 2, 2) || bitsOf(s.alpha, 6, 2) != 0))
        return {};
    const float left = v[0].x, top = v[0].y, right = v[1].x, bottom = v[2].y;
    for (float n : {left, top, right, bottom})
        if (!std::isfinite(n) || n < -2048 || n > 2048 || std::floor(n) != n)
            return {};
    if (!(right > left && bottom > top) || v[1].y != top || v[2].x != left || v[3].x != right || v[3].y != top ||
        v[4].x != right || v[4].y != bottom || v[5].x != left || v[5].y != bottom)
        return {};
    for (uint32_t i = 0; i < count; ++i)
        if (v[i].z != 0 || v[i].r != v[0].r || v[i].g != v[0].g || v[i].b != v[0].b || v[i].a != v[0].a)
            return {};
    uint32_t pixel = 0, shift = 0;
    for (float c : {v[0].r, v[0].g, v[0].b, v[0].a}) {
        if (!std::isfinite(c) || c < 0 || c > 255 || std::floor(c) != c)
            return {};
        pixel |= uint32_t(c) << shift;
        shift += 8;
    }
    if (s.fba)
        pixel |= 0x80000000u;
    const int x0 = std::max({int(left), 0, int(bitsOf(s.scissor, 0, 11))});
    const int y0 = std::max({int(top), 0, int(bitsOf(s.scissor, 32, 11))});
    const int x1 = std::min({int(right), int(width), int(bitsOf(s.scissor, 16, 11)) + 1});
    const int y1 = std::min({int(bottom), int(height), int(bitsOf(s.scissor, 48, 11)) + 1});
    if (x1 <= x0 || y1 <= y0 || uint64_t(x1 - x0) * (y1 - y0) > 4096)
        return {};
    return SolidMirror{uint32_t(x0), uint32_t(y0), uint32_t(x1 - x0), uint32_t(y1 - y0), pixel};
}

inline std::vector<SolidMirror> solidMirrors(const GsDrawState& s, const DrawVertex* v, uint32_t count,
                                             bool haveTexture, uint32_t width, uint32_t height) {
    if (!count || count % 6 || count > 6 * 64)
        return {};
    std::vector<SolidMirror> result;
    uint32_t pixels = 0;
    for (uint32_t i = 0; i < count; i += 6) {
        auto rect = solidMirror(s, v + i, 6, haveTexture, width, height);
        if (!rect || (pixels += rect->w * rect->h) > 4096)
            return {};
        result.push_back(*rect);
    }
    return result;
}
} // namespace gsr
