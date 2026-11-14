// motion_coverage.h ([video] velocity_skip): the grid's rules, and random worlds of draws through a model of the
// object-motion target with every velocity pass drawn against the same worlds with the invalid-only passes left out
// where the grid allows. What the guide pass reads (valid entries and their values) must be the same. GPU free.
#include "motion_coverage.h"

#include <cstdio>
#include <random>
#include <vector>

namespace {
int g_failed = 0, g_checks = 0;
void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failed;
        std::fprintf(stderr, "FAILED: %s\n", what);
    }
}

struct V {
    float x, y;
};

void rules() {
    motion_coverage::Grid g;
    g.reset(4096, 4096);
    check(!g.any(), "a reset grid is empty");
    check(!g.touches({0, 0, 4096, 4096}), "nothing valid after a reset");
    g.mark({100, 100, 110, 110});
    check(g.any(), "marked");
    check(g.touches({105, 105, 106, 106}), "inside the mark");
    check(g.touches({0, 0, 65, 65}), "same tile (64 px tiles) counts as may be valid");
    check(!g.touches({200, 200, 300, 300}), "other tiles stay clean");
    check(!g.touches({0, 0, 64, 64}), "the tile before is clean");
    check(!g.touches({105, 105, 105, 200}), "an empty rect touches nothing");
    g.reset(4096, 4096);
    check(!g.touches({105, 105, 106, 106}), "reset clears the marks");
    g.markAll();
    check(g.touches({4095, 4095, 4096, 4096}), "markAll reaches the last pixel");
    // Odd sizes: the last tile row and column hold the remainder.
    g.reset(1000, 600);
    g.mark({999, 599, 1000, 600});
    check(g.touches({990, 590, 1000, 600}), "corner of an odd target");
    check(!g.touches({0, 0, 900, 500}), "rest of an odd target clean");
    g.mark({-50, -50, 5000, 5000});
    check(g.touches({0, 0, 1, 1}), "a rect beyond the target is clipped, not lost");

    const V tri[3] = {{10.25f, 20.5f}, {30.0f, 22.0f}, {15.0f, 40.75f}};
    const auto r = motion_coverage::bounds(tri, 3, 8.0f, 8.0f, 2, 4096, 4096);
    check(r.x0 == 80 && r.y0 == 162 && r.x1 == 243 && r.y1 == 329, "bounds: scaled, grown, half open");
    const V far[2] = {{-100, -100}, {1e9f, 1e9f}};
    const auto c = motion_coverage::bounds(far, 2, 8.0f, 8.0f, 2, 4096, 4096);
    check(c.x0 == 0 && c.y0 == 0 && c.x1 == 4096 && c.y1 == 4096, "bounds clipped to the target");
    const V bad[1] = {{std::nanf(""), 0}};
    const auto w = motion_coverage::bounds(bad, 1, 8.0f, 8.0f, 2, 4096, 4096);
    check(w.x0 == 0 && w.x1 == 4096 && w.y1 == 4096, "a non-finite vertex covers the whole target");
}

// A model of the motion target: per pixel (valid, value). A draw writes some pixels inside its vertices' box (as a
// rasteriser with a depth test would: an arbitrary subset), valid with its value or invalid.
struct Entry {
    bool valid = false;
    uint32_t value = 0;
    bool operator==(const Entry& o) const { return valid == o.valid && (!valid || value == o.value); }
};

void worlds() {
    std::mt19937 rng(1234);
    const uint32_t W = 256, H = 160;
    uint64_t skipped = 0, drawn = 0, differ = 0, written = 0;
    for (int world = 0; world < 400; ++world) {
        std::vector<Entry> all(W * H), skip(W * H);
        motion_coverage::Grid grid;
        grid.reset(W, H);
        const int draws = 20 + (int)(rng() % 200);
        // Some worlds draw their tracked objects first, some last, most mixed.
        const int order = (int)(rng() % 3);
        for (int d = 0; d < draws; ++d) {
            bool mayValid;
            if (order == 0)
                mayValid = d < draws / 5;
            else if (order == 1)
                mayValid = d > draws * 4 / 5;
            else
                mayValid = rng() % 6 == 0;
            V v[3];
            const float cx = (float)(rng() % (W + 40)) - 20, cy = (float)(rng() % (H + 40)) - 20;
            for (auto& p : v) {
                p.x = (cx + (float)(rng() % 40) - 20) / 2.0f; // GS pixels: scale 2 below
                p.y = (cy + (float)(rng() % 40) - 20) / 2.0f;
            }
            const auto box = motion_coverage::bounds(v, 3, 2.0f, 2.0f, 0, W, H);
            const bool leaveOut = !mayValid && !grid.touches(box);
            if (mayValid)
                grid.mark(box);
            skipped += leaveOut;
            drawn += !leaveOut;
            const uint32_t value = (uint32_t)rng();
            const uint32_t density = 1 + rng() % 4;
            for (int32_t y = box.y0; y < box.y1; ++y)
                for (int32_t x = box.x0; x < box.x1; ++x) {
                    if (rng() % density)
                        continue; // depth test failed, outside the triangle, discarded
                    Entry e;
                    e.valid = mayValid && rng() % 4 != 0; // a tracked draw writes invalid entries too
                    e.value = value ^ (uint32_t)(x * 7 + y);
                    all[y * W + x] = e;
                    if (!leaveOut)
                        skip[y * W + x] = e;
                    ++written;
                }
        }
        for (uint32_t i = 0; i < W * H; ++i)
            differ += !(all[i] == skip[i]);
    }
    std::printf("worlds: 400, velocity passes drawn %llu, left out %llu, pixel writes %llu, pixels that differ %llu\n",
                (unsigned long long)drawn, (unsigned long long)skipped, (unsigned long long)written,
                (unsigned long long)differ);
    check(differ == 0, "the guide reads the same entries with the invalid-only passes left out");
    check(skipped > drawn / 10, "the model leaves passes out");
}
} // namespace

int main() {
    rules();
    worlds();
    std::printf("motion_coverage: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
