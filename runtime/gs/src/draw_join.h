// Joining consecutive draws that differ only in their texture ([video] terrain_batching, env GSR_TERRAIN_BATCHING).
//
// Converted Order of the Flame ground is drawn as one pass per texture per small chunk: thousands of short draws a
// frame (two to a few dozen triangles each) whose GS state is the same except for the texture (TEX0 addresses, the
// mip addresses and the palette, which every pass loads anew). Without joining, each of them ends the core's batch
// (gs_core.cpp, beginDraw and loadClut) and goes through the whole per-draw path of the backend (gs_draw.inl).
//
// With joining the core keeps such passes in one batch as segments: each segment keeps its own GS state and, when a
// palette load replaced it while the batch was open, a copy of its palette. The backend gets the segments together
// (BackendApi::drawSegments) and draws them with one classification, one set of bindings and one constants upload,
// switching only the texture between the segments, in their order. Primitives are blended in submission order, so
// the picture is the same as with one draw per segment; the shared draw code falls back to one draw per segment
// whenever a joined draw could differ (gs_draw.inl, drawSegments).
//
// GPU free: the rule which states may join and the segment bookkeeping (tests/test_draw_join.cpp).
#pragma once
#include "gs_internal.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace gsr::draw_join {

// At most this many segments in one batch (one texture switch each).
constexpr uint32_t kMaxSegments = 64;

// TEX0 fields that may differ between joined segments: TBP0, TBW (the image), CBP, CSA, CLD (the palette and its
// load). The format, size, colour and function fields (PSM, TW, TH, TCC, TFX, CPSM, CSM) must be equal.
constexpr uint64_t kTex0Free = 0x3FFFull | (0x3Full << 14) | (0x3FFFull << 37) | (0x1Full << 56) | (0x7ull << 61);

// A textured triangle draw (no sprites, lines or points): the only kind that joins.
inline bool texturedTriangles(const GsDrawState& s) {
    return (s.flags & DF_TME) && !(s.flags & (DF_SPRITE | DF_LINE | DF_POINT));
}

// Whether a draw of state `next` may join an open batch of state `open`: both textured triangle draws whose state is
// the same except for the free TEX0 fields, the mip addresses and the palette serial.
inline bool onlyTextureDiffers(const GsDrawState& open, const GsDrawState& next) {
    if (!texturedTriangles(open))
        return false;
    GsDrawState a = open, b = next;
    a.tex0 &= ~kTex0Free;
    b.tex0 &= ~kTex0Free;
    a.miptbp1 = b.miptbp1 = 0;
    a.miptbp2 = b.miptbp2 = 0;
    a.clutSerial = b.clutSerial = 0;
    a.pad = b.pad = 0;
    return std::memcmp(&a, &b, sizeof a) == 0;
}

// First and last block of memory a draw's frame and depth buffers can occupy (whole pages, from the buffer base to
// the scissor's last row at the smallest page height): a palette read from there may need what the open batch draws.
inline void targetBlocks(const GsDrawState& s, uint32_t& first, uint32_t& last) {
    const uint32_t fbp = uint32_t(s.frame & 0x1FF), fbw = uint32_t((s.frame >> 16) & 0x3F);
    const uint32_t zbp = uint32_t(s.zbuf & 0x1FF);
    const uint32_t rows = uint32_t((s.scissor >> 48) & 0x7FF) + 1;
    const uint32_t pages = (fbw ? fbw : 1) * ((rows + 31) / 32);
    const uint32_t fLo = fbp * 32, fHi = fLo + pages * 32 - 1;
    const uint32_t zLo = zbp * 32, zHi = zLo + pages * 32 - 1;
    first = fLo < zLo ? fLo : zLo;
    last = fHi > zHi ? fHi : zHi;
}

inline bool overlaps(uint32_t aFirst, uint32_t aLast, uint32_t bFirst, uint32_t bLast) {
    return aFirst <= bLast && bFirst <= aLast;
}

// The segments of the open batch. Empty while the batch is an ordinary one (batchState and the live palette).
class Segments {
public:
    struct Segment {
        uint32_t first = 0; // its first vertex in the batch
        GsDrawState state{};
        int32_t clut = -1; // index of its palette copy, -1 = the live palette
    };

    bool empty() const { return segments_.empty(); }
    uint32_t size() const { return uint32_t(segments_.size()); }
    bool full() const { return segments_.size() >= kMaxSegments; }
    const Segment& operator[](uint32_t i) const { return segments_[i]; }

    // The open batch (state `open`) becomes segment 0 if it is not a segment yet.
    void open(const GsDrawState& open) {
        if (segments_.empty())
            segments_.push_back({0, open, -1});
    }
    // The live palette is about to be replaced: the segments that still use it (those since the last copy, at the
    // end of the list) get a copy. A second load before the next draw replaces a palette no segment used: nothing.
    void keepPalette(const ClutBuf& live) {
        if (segments_.back().clut >= 0)
            return;
        if (palettes_.size() <= used_)
            palettes_.emplace_back();
        palettes_[used_] = live;
        for (size_t i = segments_.size(); i-- > 0 && segments_[i].clut < 0;)
            segments_[i].clut = int32_t(used_);
        ++used_;
    }
    // A new segment from vertex `first` on.
    void add(uint32_t first, const GsDrawState& state) { segments_.push_back({first, state, -1}); }

    // The segments as the backend gets them, `total` vertices in the batch, `live` the current palette.
    void views(const ClutBuf& live, uint32_t total, std::vector<DrawSegment>& out) const {
        out.clear();
        for (size_t i = 0; i < segments_.size(); ++i) {
            const Segment& s = segments_[i];
            const uint32_t end = i + 1 < segments_.size() ? segments_[i + 1].first : total;
            out.push_back({s.first, end - s.first, &s.state, s.clut >= 0 ? &palettes_[size_t(s.clut)] : &live});
        }
    }
    void clear() {
        segments_.clear();
        used_ = 0;
    }

private:
    std::vector<Segment> segments_;
    std::vector<ClutBuf> palettes_; // reused between batches
    size_t used_ = 0;
};

} // namespace gsr::draw_join
