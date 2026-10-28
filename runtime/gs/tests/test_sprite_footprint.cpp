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
        assert(b.valid && b.u0 == 28 + .5f / n && b.u1 == 88 - .5f / n);
        assert(b.v0 == 3 + .5f / n && b.v1 == 61 - .5f / n);
        checks += 2;
        // The extended draw intentionally includes the white outline cell.
        quad(v, 13, 74, 20, 78, 28.5f, 89.5f, 60.5f, 2.5f);
        b = bounds(v, 6, true, 128, 128, n);
        assert(b.valid && b.u1 == 89 - .5f / n);
        ++checks;
        for (auto& p : v) {
            p.s *= .5f / 128;
            p.t *= .5f / 128;
            p.q = .5f;
        }
        b = bounds(v, 6, false, 128, 128, n);
        assert(b.valid && b.u1 == 89 - .5f / n);
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
