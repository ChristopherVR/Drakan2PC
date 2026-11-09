#include "sprite_footprint.h"
#include <cassert>
#include <cstdio>
#include <limits>
struct V {
    float x, y, s, t, q = 1;
};
static void quad(V* v, float x0, float x1, float y0, float y1, float u0, float u1, float v0, float v1) {
    v[0] = {x0, y0, u0, v0};
    v[1] = {x1, y0, u1, v0};
    v[2] = {x0, y1, u0, v1};
    v[3] = v[2];
    v[4] = v[1];
    v[5] = {x1, y1, u1, v1};
}
int main() {
    using gsr::sprite_footprint::bounds;
    V v[6];
    unsigned checks = 0;
    for (unsigned n : {1u, 2u, 4u, 8u}) {
        quad(v, 13, 73, 20, 78, 28.5f, 88.5f, 60.5f, 2.5f);
        auto b = bounds(v, 6, true, 128, 128, n);
        // Every factor clamps to the centres of the first and last sampled texels (28.5 .. 87.5, 3.5 .. 60.5).
        assert(b.valid && b.u0 == 28.5f && b.u1 == 87.5f);
        assert(b.v0 == 3.5f && b.v1 == 60.5f);
        checks += 2;
        // The extended draw intentionally includes the white outline cell.
        quad(v, 13, 74, 20, 78, 28.5f, 89.5f, 60.5f, 2.5f);
        b = bounds(v, 6, true, 128, 128, n);
        assert(b.valid && b.u1 == 88.5f);
        ++checks;
        for (auto& p : v) {
            p.s *= .5f / 128;
            p.t *= .5f / 128;
            p.q = .5f;
        }
        b = bounds(v, 6, false, 128, 128, n);
        assert(b.valid && b.u1 == 88.5f);
        ++checks;
    }
    // The Order of the Flame health/compass orbs: a 60 x 58 native sprite sampling a 128 x 128 render-target sheet.
    // Sampled at render scale 2 its last render pixel lands past texel 87; the neighbouring texel 88 / row 2 holds
    // the white outline of another piece, which showed as a thin line on the right and bottom edge.
    quad(v, 421, 481, 434, 492, 28.5f, 88.5f, 60.5f, 2.5f);
    auto rt = bounds(v, 6, true, 128, 128, 2);
    assert(rt.valid && rt.u0 == 28.5f && rt.u1 == 87.5f);
    assert(rt.v0 == 3.5f && rt.v1 == 60.5f);
    assert(std::floor(rt.u1) == 87 && std::floor(rt.v0) == 3);
    ++checks;
    // The same orb with an HD / xBRZ replacement (4 physical texels per texel) or at render scale 5: the outer
    // sub-texels of texel 87 blend towards the white outline of texel 88 and drew a one-texel line right of the orb
    // (margin of half a physical texel, .5 / 4 = 0.125, was not enough). The clamp stays on the texel centre.
    for (unsigned n : {3u, 4u, 5u, 8u}) {
        auto hd = bounds(v, 6, true, 128, 128, n);
        assert(hd.valid && hd.u1 == 87.5f && hd.u0 == 28.5f && hd.v0 == 3.5f && hd.v1 == 60.5f);
        ++checks;
    }
    quad(v, 13.25f, 73.25f, 20.25f, 78.25f, 88.5f, 28.5f, 2.5f, 60.5f);
    auto b = bounds(v, 6, true, 128, 128, 1);
    assert(b.valid && b.u0 == 28.5f && b.u1 == 87.5f && b.v0 == 3.5f && b.v1 == 60.5f);
    ++checks;
    assert(!bounds(v, 3, true, 128, 128, 1).valid);
    ++checks;
    assert(!bounds(v, 6, true, 128, 128, 0).valid);
    ++checks;
    v[0].x += 1;
    assert(!bounds(v, 6, true, 128, 128, 1).valid);
    ++checks;
    quad(v, 0, 60, 0, 60, 0, 120, 0, 60);
    assert(!bounds(v, 6, true, 128, 128, 1).valid);
    ++checks;
    quad(v, 0, 60, 0, 60, 0, 60, 0, 60);
    v[0].q = 0;
    assert(!bounds(v, 6, false, 128, 128, 1).valid);
    ++checks;
    v[0].x = std::numeric_limits<float>::quiet_NaN();
    assert(!bounds(v, 6, true, 128, 128, 1).valid);
    ++checks;
    quad(v, 0, 60, 0, 60, 0, 60, 0, 60);
    v[1].q = 2;
    assert(!bounds(v, 6, false, 128, 128, 1).valid);
    ++checks;
    for (int sign : {-1, 1})
        for (int sub = 0; sub < 16; ++sub)
            for (unsigned n : {1u, 8u}) {
                float start = -3 + sub / 16.0f, end = start + 60, u = 28.5f;
                quad(v, start, end, 0, 60, u, u + sign * 60, 0, 60);
                b = bounds(v, 6, true, 256, 256, n);
                assert(b.valid);
                ++checks;
                for (float x = std::ceil(start); x < end; ++x) {
                    float original = u + sign * (x - start);
                    assert(std::floor(original) == std::floor(std::clamp(original, b.u0, b.u1)));
                    ++checks;
                }
            }
    std::printf("sprite footprint: %u checks passed\n", checks);
}
