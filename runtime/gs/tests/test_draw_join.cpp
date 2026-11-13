// Joining draws that differ only in their texture (draw_join.h), without a GPU: the join rule, the target block
// range, and the segment bookkeeping against a model of the core. A random stream of passes (texture changes, palette
// loads, other state changes) is cut into draws the way the core does without joining (a new draw at every state
// change, the palette read at the draw) and the way it does with joining (segments, palette copies); every segment
// handed to the backend must be one of the old draws: the same vertices, the same state and the same palette.
#include "draw_join.h"

#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

using namespace gsr;

namespace {
int g_failed = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                          \
            ++g_failed;                                                                                                \
        }                                                                                                              \
    } while (0)

GsDrawState terrainPass(uint32_t tbp, uint32_t cbp, uint32_t serial) {
    GsDrawState s{};
    s.frame = 0x80080;
    s.zbuf = 0x100;
    s.scissor = (uint64_t(511) << 48) | (uint64_t(639) << 16);
    s.test = 0x5000d;
    s.alpha = 0x44;
    // PSMT8 128 x 128, CLD 1
    s.tex0 = uint64_t(tbp) | (uint64_t(2) << 14) | (uint64_t(0x13) << 20) | (uint64_t(7) << 26) | (uint64_t(7) << 30) |
             (uint64_t(cbp) << 37) | (uint64_t(1) << 61);
    s.tex1 = 0xffffff3000000130ull;
    s.miptbp1 = tbp * 3ull;
    s.flags = DF_TME | DF_FGE | DF_ABE | DF_IIP;
    s.clutSerial = serial;
    return s;
}

void testRule() {
    const GsDrawState a = terrainPass(0x3b2c, 0x3b82, 1), b = terrainPass(0x396a, 0x39c0, 2);
    CHECK(draw_join::onlyTextureDiffers(a, b));
    CHECK(draw_join::onlyTextureDiffers(a, a));
    GsDrawState c = b;
    c.tex0 ^= uint64_t(1) << 26; // another width
    CHECK(!draw_join::onlyTextureDiffers(a, c));
    c = b;
    c.tex0 ^= uint64_t(1) << 35; // another texture function
    CHECK(!draw_join::onlyTextureDiffers(a, c));
    c = b;
    c.tex0 ^= uint64_t(1) << 20; // another format
    CHECK(!draw_join::onlyTextureDiffers(a, c));
    c = b;
    c.tex1 ^= 4; // another mip count
    CHECK(!draw_join::onlyTextureDiffers(a, c));
    c = b;
    c.alpha = 0x48;
    CHECK(!draw_join::onlyTextureDiffers(a, c));
    c = b;
    c.test ^= 1;
    CHECK(!draw_join::onlyTextureDiffers(a, c));
    c = b;
    c.frame ^= 1;
    CHECK(!draw_join::onlyTextureDiffers(a, c));
    c = b;
    c.flags |= DF_FST;
    CHECK(!draw_join::onlyTextureDiffers(a, c));
    GsDrawState sprite = a, sprite2 = b;
    sprite.flags |= DF_SPRITE;
    sprite2.flags |= DF_SPRITE;
    CHECK(!draw_join::onlyTextureDiffers(sprite, sprite2));
    GsDrawState flat = a, flat2 = b;
    flat.flags &= ~DF_TME;
    flat2.flags &= ~DF_TME;
    CHECK(!draw_join::onlyTextureDiffers(flat, flat2));

    uint32_t first = 0, last = 0;
    draw_join::targetBlocks(a, first, last); // frame at page 0x80, 8 pages wide, 512 rows; depth at page 0x100
    CHECK(first == 0x80 * 32);
    CHECK(last == 0x100 * 32 + 8 * 16 * 32 - 1);
    CHECK(draw_join::overlaps(first, last, 0x1000, 0x1000));
    CHECK(!draw_join::overlaps(first, last, 0x3b82 * 1, 0x3b82)); // the palette of a ground texture
    CHECK(!draw_join::overlaps(first, last, last + 1, last + 9));
}

struct OldDraw {
    uint32_t first, count;
    GsDrawState state;
    uint32_t palette; // the palette's first word: which load it was
};

// One random stream through both models. Returns the number of segments the joined model handed over in batches of
// more than one.
uint64_t testStream(uint32_t seed) {
    std::mt19937 rng(seed);
    ClutBuf live{};
    uint32_t serial = 0, loads = 0;
    // old model: the open draw, cut at every state change, the live palette at its end
    std::vector<OldDraw> oldDraws;
    // joined model
    draw_join::Segments segments;
    std::vector<DrawSegment> views;
    std::vector<OldDraw> newDraws;
    uint64_t joined = 0;

    uint32_t vertices = 0; // the batch of both models (same vertex positions)
    bool open = false;
    GsDrawState openState{};
    uint32_t oldFirst = 0;
    bool joinedOpen = false;
    GsDrawState joinedState{};

    auto oldFlush = [&] {
        if (open && vertices > oldFirst)
            oldDraws.push_back({oldFirst, vertices - oldFirst, openState, live.raw[0]});
        open = false;
        oldFirst = vertices;
    };
    auto newFlush = [&](uint32_t batchStart) {
        if (!joinedOpen)
            return;
        if (segments.empty())
            newDraws.push_back({batchStart, vertices - batchStart, joinedState, live.raw[0]});
        else {
            segments.views(live, vertices - batchStart, views);
            for (const auto& v : views)
                newDraws.push_back({batchStart + v.first, v.count, *v.state, v.clut->raw[0]});
            if (views.size() > 1)
                joined += views.size();
            segments.clear();
        }
        joinedOpen = false;
    };
    uint32_t batchStart = 0;
    for (int pass = 0; pass < 4000; ++pass) {
        const uint32_t kind = rng() % 10;
        GsDrawState next = terrainPass(0x3000 + (rng() % 8) * 0x40, 0x3800 + (rng() % 8) * 0x40, serial);
        if (kind == 0)
            next.alpha = 0x48; // a state change that does not join
        if (kind == 1)
            next.flags |= DF_SPRITE;
        // a palette load before the pass (CLD 1): both models end or keep their batch as the core does
        if (rng() % 4 != 0) {
            // old: flush, then load
            oldFlush();
            // joined: keep the batch (a textured triangle batch whose palette lies outside the target) or flush
            if (joinedOpen && vertices > batchStart && !segments.full() && draw_join::texturedTriangles(joinedState)) {
                segments.open(joinedState);
                segments.keepPalette(live);
            } else {
                newFlush(batchStart);
                batchStart = vertices;
            }
            live.raw[0] = ++loads;
            next.clutSerial = ++serial;
        }
        // the pass's primitives
        const uint32_t prims = 1 + rng() % 6;
        for (uint32_t p = 0; p < prims; ++p) {
            // old model: beginDraw
            if (open && std::memcmp(&openState, &next, sizeof next) != 0)
                oldFlush();
            if (!open) {
                open = true;
                openState = next;
                oldFirst = vertices;
            }
            // joined model: beginDraw
            if (joinedOpen && std::memcmp(&joinedState, &next, sizeof next) != 0) {
                if (vertices > batchStart && !segments.full() && draw_join::onlyTextureDiffers(joinedState, next)) {
                    segments.open(joinedState);
                    segments.add(vertices - batchStart, next);
                    joinedState = next;
                } else {
                    newFlush(batchStart);
                    batchStart = vertices;
                }
            }
            if (!joinedOpen) {
                joinedOpen = true;
                joinedState = next;
                batchStart = vertices;
            }
            vertices += 3;
        }
        if (rng() % 50 == 0) { // something else that ends every batch (a transfer, a FINISH)
            oldFlush();
            newFlush(batchStart);
            batchStart = vertices;
        }
    }
    oldFlush();
    newFlush(batchStart);

    CHECK(oldDraws.size() == newDraws.size());
    const size_t n = std::min(oldDraws.size(), newDraws.size());
    uint32_t bad = 0;
    for (size_t i = 0; i < n; ++i) {
        const OldDraw &o = oldDraws[i], &w = newDraws[i];
        if (o.first != w.first || o.count != w.count || std::memcmp(&o.state, &w.state, sizeof o.state) != 0 ||
            o.palette != w.palette) {
            if (!bad)
                std::fprintf(stderr, "draw %zu: old %u+%u pal %u tex %llx, new %u+%u pal %u tex %llx\n", i, o.first,
                             o.count, o.palette, (unsigned long long)o.state.tex0, w.first, w.count, w.palette,
                             (unsigned long long)w.state.tex0);
            ++bad;
        }
    }
    if (bad)
        std::fprintf(stderr, "seed %u: %u of %zu draws differ\n", seed, bad, n);
    CHECK(bad == 0);
    return joined;
}
} // namespace

int main() {
    testRule();
    uint64_t joined = 0;
    for (uint32_t seed = 1; seed <= 20; ++seed)
        joined += testStream(seed);
    CHECK(joined > 1000); // the streams do join
    std::printf("draw_join: %llu segments in joined batches, %s\n", (unsigned long long)joined,
                g_failed ? "FAILED" : "ok");
    return g_failed ? 1 : 0;
}
