// Near-plane clip of projected polygons (world/PolyClip.h): pure math, no game bytes.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "world/PolyClip.h"

namespace {
int g_failed = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                          \
            ++g_failed;                                                                                                \
        }                                                                                                              \
    } while (0)

using namespace world::polyclip;

// Projects a camera space point the way the engine does: x = cx + X / depth, z proportional to q.
Vertex project(float X, float Y, float depth, float s, float t, uint32_t rgba) {
    Vertex v;
    const Params p;
    v.q = 1.0f / depth;
    v.x = p.centreX + X * v.q;
    v.y = p.centreY + Y * v.q;
    v.z = 8.6e9f * v.q;
    v.w = 255.0f;
    v.s = s;
    v.t = t;
    v.rgba = rgba;
    return v;
}

void testFrontPolygonsUntouched() {
    const Vertex quad[4] = {project(-50, -50, 600, 0, 0, 0x40ffffff), project(50, -50, 600, 0, 1, 0x40ffffff),
                            project(50, 50, 900, 1, 1, 0x40ffffff), project(-50, 50, 900, 1, 0, 0x40ffffff)};
    CHECK(!straddlesCamera(quad, 4));
    Vertex out[kMaxClipped];
    const int n = clipPolygon(quad, 4, Params{}, out);
    CHECK(n == 4);
    for (int i = 0; i < n; ++i)
        CHECK(std::memcmp(&out[i], &quad[i], sizeof(Vertex)) == 0); // bit identical
}

void testStraddleIsClippedToFront() {
    // ribbon: near edge 1500 in front, far edge 400 behind the camera
    const Vertex quad[4] = {project(-100, 0, 1500, 0, 0, 0x44000000), project(100, 0, 1500, 0, 1, 0x44000000),
                            project(100, 0, -400, 1, 1, 0xff000000), project(-100, 0, -400, 1, 0, 0xff000000)};
    CHECK(straddlesCamera(quad, 4));
    Params p;
    Vertex out[kMaxClipped];
    const int n = clipPolygon(quad, 4, p, out);
    CHECK(n >= 3);
    for (int i = 0; i < n; ++i) {
        CHECK(out[i].q > 0.0f);
        CHECK(1.0f / out[i].q >= p.nearW * 0.999f);
        CHECK(std::fabs(out[i].x - p.centreX) <= p.guard * 1.001f);
        CHECK(std::fabs(out[i].y - p.centreY) <= p.guard * 1.001f);
        CHECK(std::isfinite(out[i].z) && std::isfinite(out[i].x));
    }
    // the two front vertices survive bit for bit
    int exact = 0;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < 2; ++j)
            if (std::memcmp(&out[i], &quad[j], sizeof(Vertex)) == 0)
                ++exact;
    CHECK(exact == 2);
    // new vertices lie on the edge near -> far at the crossing of depth nearW: s goes 0 -> 1 linearly in depth
    // (depth 1500 -> -400): u = (1500 - 8) / 1900
    const float uExpected = (1500.0f - p.nearW) / 1900.0f;
    bool found = false;
    for (int i = 0; i < n; ++i)
        if (std::memcmp(&out[i], &quad[0], sizeof(Vertex)) != 0 &&
            std::memcmp(&out[i], &quad[1], sizeof(Vertex)) != 0 && std::fabs(out[i].t - 0.0f) < 1e-3f) {
            CHECK(std::fabs(out[i].s - uExpected) < 1e-3f);
            // colour alpha lerps 0x44 -> 0xff by u
            const float a = static_cast<float>(out[i].rgba >> 24);
            CHECK(std::fabs(a - (0x44 + (0xff - 0x44) * uExpected)) < 1.5f);
            found = true;
        }
    CHECK(found);
}

void testPerspectiveCorrectCrossing() {
    // The crossing of a point halfway (in camera depth) must project where the straight line in camera space does.
    const Vertex a = project(100, 0, 1000, 0, 0, 0), b = project(100, 0, -1000, 1, 0, 0);
    const Vertex tri[3] = {a, b, project(-100, 0, 1000, 0, 1, 0)};
    Params p;
    p.nearW = 500.0f;
    Vertex out[kMaxClipped];
    const int n = clipPolygon(tri, 3, p, out);
    CHECK(n >= 3);
    bool found = false;
    for (int i = 0; i < n; ++i)
        if (std::fabs(1.0f / out[i].q - 500.0f) < 0.5f && out[i].t == 0.0f) {
            // camera x stays 100: screen x = 2048 + 100 / 500 * ... (project uses x = cx + X*q with X=100)
            CHECK(std::fabs(out[i].x - (p.centreX + 100.0f / 500.0f)) < 1e-2f);
            found = true;
        }
    CHECK(found);
}

void testFullyBehindAndDegenerate() {
    const Vertex back[3] = {project(0, 0, -100, 0, 0, 0), project(10, 0, -100, 1, 0, 0), project(0, 10, -100, 0, 1, 0)};
    CHECK(!straddlesCamera(back, 3)); // the game culls these; the clipper leaves them alone
    Vertex out[kMaxClipped];
    CHECK(clipPolygon(back, 3, Params{}, out) == 0);
    Vertex z[3] = {project(0, 0, 100, 0, 0, 0), project(10, 0, -100, 1, 0, 0), project(0, 10, 100, 0, 1, 0)};
    z[2].q = 0.0f; // untextured polygons may carry q == 0
    CHECK(!straddlesCamera(z, 3));
    z[2].q = 1.0f / 100;
    z[0].x = NAN;
    CHECK(!straddlesCamera(z, 3));
}

void testOutsideWindowIsClipped() {
    // all in front, but the tip is 12 units from the camera: x = 2048 + 90000 / 12 is far outside the GS window
    const Vertex tri[4] = {project(0, 0, 1000, 0, 0, 0), project(50, 0, 1000, 0, 1, 0), project(90000, 0, 12, 1, 1, 0),
                           project(90000, 90000, 12, 1, 0, 0)};
    Params p;
    CHECK(!straddlesCamera(tri, 4));
    CHECK(outsideWindow(tri, 4, p) && needsClip(tri, 4, p));
    Vertex out[kMaxClipped];
    const int n = clipPolygon(tri, 4, p, out);
    CHECK(n >= 3);
    for (int i = 0; i < n; ++i)
        CHECK(std::fabs(out[i].x - p.centreX) <= p.guard * 1.001f &&
              std::fabs(out[i].y - p.centreY) <= p.guard * 1.001f);
    // in range polygons never trigger
    const Vertex ok[3] = {project(0, 0, 1000, 0, 0, 0), project(500, 0, 1000, 0, 1, 0), project(0, 500, 800, 1, 1, 0)};
    CHECK(!needsClip(ok, 3, p));
}

void testGuardBand() {
    // front vertices far off screen: stay within the 16 bit window after clipping
    const Vertex tri[3] = {project(900000, 0, 100, 0, 0, 0), project(-900000, 0, 100, 1, 0, 0),
                           project(0, 0, -50, 0, 1, 0)};
    Params p;
    Vertex out[kMaxClipped];
    const int n = clipPolygon(tri, 3, p, out);
    CHECK(n >= 3);
    for (int i = 0; i < n; ++i)
        CHECK(std::fabs(out[i].x - p.centreX) <= p.guard * 1.001f);
}

void testObservedStratosFan() {
    // numbers from the Stratos capture (docs/journal/2026-11-06/stratos-garbled-effects.md)
    Vertex q[4] = {{2020.6f, 2097.19f, 5.63562e6f, 255, 0, 0, 0.000656083f, 0x44000000u},
                   {2154.31f, 1967.3f, 5.63562e6f, 255, 0, 1, 0.000656083f, 0x44000000u},
                   {1874.55f, 1821.55f, -4.97093e6f, 255, 1, 1, -0.000578701f, 0x44000000u},
                   {1992.5f, 1706.99f, -4.97093e6f, 255, 1, 0, -0.000578701f, 0x44000000u}};
    CHECK(straddlesCamera(q, 4));
    Vertex out[kMaxClipped];
    const int n = clipPolygon(q, 4, Params{}, out);
    CHECK(n >= 3);
    for (int i = 0; i < n; ++i)
        CHECK(out[i].q > 0.0f && std::fabs(out[i].x - 2048.0f) <= 1801.0f && std::fabs(out[i].y - 2048.0f) <= 1801.0f);
}
} // namespace

int main() {
    testFrontPolygonsUntouched();
    testStraddleIsClippedToFront();
    testPerspectiveCorrectCrossing();
    testFullyBehindAndDegenerate();
    testGuardBand();
    testOutsideWindowIsClipped();
    testObservedStratosFan();
    if (g_failed) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
        return 1;
    }
    std::puts("world_polyclip_test: ok");
    return 0;
}
