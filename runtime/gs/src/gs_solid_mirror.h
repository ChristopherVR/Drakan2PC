#pragma once
#include "gs_internal.h"
#include "gs_mem.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
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

// ---- The mirror as both backends apply it (D3D11Backend::Impl::draw, WgpuBackend::Impl::draw) -----------------------
// The backends keep the same bookkeeping per render target (RT: dirty rows, row stamps, world / temporal phase). The
// decision and the memory writes below read those fields by name, so neither backend holds a copy of the rule.

// GSR_RB_SOLID=0 turns the mirror off (every such draw then needs a GPU readback before the game reads it).
inline bool solidMirrorEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("GSR_RB_SOLID");
        return !value || value[0] != '0';
    }();
    return enabled;
}

// The rectangles of this draw that memory can hold instead of a later readback, or none.
//   plainDraw   the backend drew it at the target's plain scale with no jitter and the full write mask
//   rts         every target of the backend (smart pointers to RT); rowSpan(target, y0, y1, lo, hi) gives the VRAM
//               blocks of its rows [y0, y1)
// Only a clean scratch target qualifies: existing GPU-only pixels, world resolves, transformed HUD geometry and
// in-flight uploads keep the readback. syncRT does not upload wrapped allocation rows, so the mirrored page rows must
// lie inside memory; the allocation itself may run past the end (a target on the last page is 64 rows tall and only
// its first page row exists, which is where the game's 4 x 4 probe at page 0x1ff is drawn and read every frame).
template <class RT, class Targets, class RowSpan>
std::vector<SolidMirror> planSolidMirror(const GsDrawState& s, const DrawVertex* v, uint32_t count, bool haveTexture,
                                         const RT& rt, bool uploadActive, bool plainDraw, const Targets& rts,
                                         RowSpan&& rowSpan) {
    if (!solidMirrorEnabled() || rt.dirty || uploadActive || rt.w > 128 || rt.wstate || rt.neuralState ||
        rt.temporalEpoch || rt.surfaceSubmittedEpoch || rt.neuralOutputEpoch || !plainDraw)
        return {};
    std::vector<SolidMirror> mirrored = solidMirrors(s, v, count, haveTexture, rt.w, rt.h);
    uint32_t first = ~0u, last = 0;
    for (const auto& m : mirrored) {
        const uint64_t endRow = (uint64_t(m.y) + m.h + rt.pageH() - 1) / rt.pageH();
        if (uint64_t(rt.base()) + endRow * rt.fbw * 32 > kVramBlocks)
            return {};
        uint32_t lo, hi;
        spanBlocks(PSM_CT32, rt.base(), rt.fbw, m.x, m.y, m.w, m.h, lo, hi);
        first = std::min(first, lo);
        last = std::max(last, hi);
    }
    if (mirrored.empty())
        return mirrored;
    for (const auto& other : rts) {
        if (&*other == &rt || !other->dirty)
            continue;
        uint32_t lo, hi;
        rowSpan(*other, other->dy0, std::min(other->dy1, other->h), lo, hi);
        if (hi >= kVramBlocks || (lo <= last && hi >= first))
            return {}; // another GPU owner must retain its normal coherence ordering
    }
    return mirrored;
}

// Write the planned rectangles to memory and mark the target current: its GPU pixels and memory now agree. Call
// after the draw's own bookkeeping (writes, dirty rows).
template <class RT>
void applySolidMirror(LocalMemory& mem, const std::vector<SolidMirror>& mirrored, RT& rt) {
    if (mirrored.empty())
        return;
    for (const auto& m : mirrored) {
        for (uint32_t y = m.y; y < m.y + m.h; ++y)
            for (uint32_t x = m.x; x < m.x + m.w; ++x)
                mem.write(PSM_CT32, rt.base(), rt.fbw, x, y, m.rgba);
        mem.touchRect(PSM_CT32, rt.base(), rt.fbw, m.x, m.y, m.w, m.h);
        for (uint32_t row = m.y / rt.pageH();
             row < (m.y + m.h + rt.pageH() - 1) / rt.pageH() && row < rt.rowStamp.size(); ++row)
            rt.rowStamp[row] = mem.clock();
    }
    rt.seen = mem.clock();
    rt.dirty = false;
}
} // namespace gsr
