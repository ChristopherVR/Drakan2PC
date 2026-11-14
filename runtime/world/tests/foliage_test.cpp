// Wind model of the living-foliage option (world/Foliage.h): pure functions, no game bytes.
#include <cmath>
#include <chrono>
#include <cstdio>

#include "world/Foliage.h"

namespace {

int g_failed = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                          \
            ++g_failed;                                                                                                \
        }                                                                                                              \
    } while (0)

using namespace world::foliage;

float mag(Lean l) {
    return std::sqrt(l.x * l.x + l.z * l.z);
}

void testDeterministicAndBounded() {
    const Lean a = windLean(Plant, 123456.f, 654321.f, 77, 12.5, 1.0f);
    const Lean b = windLean(Plant, 123456.f, 654321.f, 77, 12.5, 1.0f);
    CHECK(a.x == b.x && a.z == b.z);
    float maxPlant = 0, maxTree = 0, minPlant = 1e9f;
    for (int i = 0; i < 4000; ++i) {
        const double t = i * 0.173;
        const float x = 100000.f + 37.f * i, z = 300000.f - 91.f * i;
        const Lean p = windLean(Plant, x, z, 1000 + i, t, 1.0f), r = windLean(Tree, x, z, 1000 + i, t, 1.0f);
        CHECK(std::isfinite(p.x) && std::isfinite(p.z) && std::isfinite(r.x) && std::isfinite(r.z));
        maxPlant = std::max(maxPlant, mag(p));
        minPlant = std::min(minPlant, mag(p));
        maxTree = std::max(maxTree, mag(r));
    }
    // Plants lean visibly (but never fold flat in the wind alone); trees move far less than plants.
    CHECK(maxPlant > 0.1f && maxPlant < kMaxLean);
    CHECK(maxTree > 0.01f && maxTree < 0.1f);
    CHECK(maxTree < maxPlant * 0.5f);
    CHECK(minPlant >= 0.0f);
}

void testStrengthScalesAndClamps() {
    const Lean a = windLean(Plant, 5e5f, 2e5f, 9, 3.3, 1.0f), b = windLean(Plant, 5e5f, 2e5f, 9, 3.3, 2.0f);
    CHECK(std::fabs(mag(b) - 2.0f * mag(a)) < 1e-4f);
    // Out of range strengths are clamped to 0.25 .. 2.
    const Lean lo = windLean(Plant, 5e5f, 2e5f, 9, 3.3, 0.0f), hi = windLean(Plant, 5e5f, 2e5f, 9, 3.3, 9.0f);
    CHECK(std::fabs(mag(lo) - 0.25f * mag(a)) < 1e-4f);
    CHECK(std::fabs(mag(hi) - 2.0f * mag(a)) < 1e-4f);
}

void testGustsTravelWithTheWind() {
    // The gust field at (x, t) equals the field one wind-travel later further along the wind direction: the pattern
    // rolls downwind. Gust fronts move at about 1.15 / 0.000131 = 8800 units per second.
    const double t = 40.0;
    const Lean d = windDirection(t);
    const float x0 = 400000.f, z0 = 250000.f;
    const float g0 = gustAt(x0, z0, t);
    float bestShift = 0, best = 1e9f;
    for (int s = -20000; s <= 20000; s += 100) {
        const float v = gustAt(x0 + d.x * s, z0 + d.z * s, t + 1.0);
        const float e = std::fabs(v - g0);
        if (e < best) {
            best = e;
            bestShift = static_cast<float>(s);
        }
    }
    // Moving the sample along the wind by the travel distance reproduces the old value: the shift is positive
    // (against the propagation sign), of the order of the front speed, not zero.
    CHECK(std::fabs(bestShift) > 2000.f && std::fabs(bestShift) < 20000.f);
    for (int i = 0; i < 500; ++i) {
        const float g = gustAt(1000.f * i, 777.f * i, 1.7 * i);
        CHECK(g >= 0.0f && g <= 1.0f);
    }
}

void testNeighboursMoveTogether() {
    // Two plants half a cell apart sway nearly in step (a wave rolls through the meadow), not in random directions.
    int agree = 0, n = 0;
    for (int i = 0; i < 300; ++i) {
        const double t = 0.37 * i;
        const Lean a = windLean(Plant, 300000.f, 300000.f, 1, t, 1.0f);
        const Lean b = windLean(Plant, 301000.f, 300000.f, 2, t, 1.0f);
        ++n;
        if (a.x * b.x + a.z * b.z > 0)
            ++agree;
    }
    CHECK(agree > n * 9 / 10);
}

void testPush() {
    const Lean none = pushLean(kPushRadius + 1, 0, 1.0f);
    CHECK(none.x == 0 && none.z == 0);
    const Lean exact = pushLean(0, 0, 1.0f); // standing on it: no direction, no push
    CHECK(exact.x == 0 && exact.z == 0);
    const Lean near = pushLean(300.f, 0, 1.0f);
    CHECK(near.x > 0.4f && near.z == 0.0f); // away from the actor (+x)
    const Lean west = pushLean(-300.f, 0, 1.0f);
    CHECK(west.x < -0.4f);
    // Continuous and monotonic from the edge to the actor.
    float prev = 0;
    for (float d = kPushRadius; d > 10.f; d -= 10.f) {
        const float m = mag(pushLean(d - 0.01f, 0, 1.0f));
        CHECK(m + 1e-5f >= prev);
        prev = m;
    }
    CHECK(mag(pushLean(kPushRadius - 1.f, 0, 1.0f)) < 0.01f);
}

void testShearKeepsLength() {
    Lean l = clampLean({3.0f, 4.0f});
    CHECK(std::fabs(mag(l) - kMaxLean) < 1e-5f);
    const Lean small = clampLean({0.1f, 0.0f});
    CHECK(small.x == 0.1f);
    float add[3];
    const float len = 10000.f;
    shearAdd({0.3f, 0.2f}, len, add);
    // up = (0, len, 0) + add: length stays within 0.2 % for a 0.36 lean (second order cancels).
    const float ux = add[0], uy = len + add[1], uz = add[2];
    const float newLen = std::sqrt(ux * ux + uy * uy + uz * uz);
    CHECK(std::fabs(newLen - len) / len < 0.01f);
    CHECK(add[0] == 0.3f * len && add[2] == 0.2f * len && add[1] < 0);
}

void testLeafClassification() {
    CHECK(!leafMaterial(0.0f) && !leafMaterial(0.05f) && leafMaterial(0.5f));
    // A tree: solid trunk from the base, a cut-out crown with most faces.
    const MaterialStats tree[] = {{0.0f, 212, 0, 5052}, {0.6f, 504, 188, 4946}};
    CHECK(looksLikeTree(tree, 2, 0, 5052));
    // A palm: trunk texture and fronds from a third of the height up.
    const MaterialStats palm[] = {{0.0f, 198, -279, 8825}, {0.77f, 60, 2749, 7776}};
    CHECK(looksLikeTree(palm, 2, -279, 8825));
    // A house with a few cut-out windows, a small cut-out prop, a solid hut, all cut out (no trunk).
    const MaterialStats house[] = {{0.0f, 900, 0, 4000}, {0.5f, 20, 1500, 2500}};
    CHECK(!looksLikeTree(house, 2, 0, 4000));
    const MaterialStats prop[] = {{0.0f, 20, 0, 118}, {0.5f, 12, 32, 118}};
    CHECK(!looksLikeTree(prop, 2, 0, 118));
    const MaterialStats hut[] = {{0.0f, 359, 0, 3174}, {0.0f, 582, 256, 2458}};
    CHECK(!looksLikeTree(hut, 2, 0, 3174));
    const MaterialStats cut[] = {{0.5f, 295, 0, 8402}};
    CHECK(!looksLikeTree(cut, 1, 0, 8402));
}

void testLeafWeights() {
    // Anchors and the base do not move; the weight grows with height and with the distance from the trunk.
    CHECK(leafWeight(4000, 500, 0, 5000, 2000, true) == 0.0f);
    CHECK(leafWeight(0, 500, 0, 5000, 2000, false) == 0.0f);
    float prev = -1;
    for (float y = 0; y <= 5000; y += 250) {
        const float w = leafWeight(y, 1000, 0, 5000, 2000, false);
        CHECK(w >= prev && w <= 1.0f);
        prev = w;
    }
    CHECK(leafWeight(4000, 2000, 0, 5000, 2000, false) > leafWeight(4000, 0, 0, 5000, 2000, false));
}

void testLeafMotion() {
    const float identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    const Lean dir{1.0f, 0.0f};
    const float height = 5000.0f;
    const LeafVertex top = leafVertex(800, 4800, 300, leafWeight(4800, 900, 0, 5000, 2000, false), height);
    const LeafVertex anchor = leafVertex(100, 3000, 50, 0.0f, height);
    double meanAlong = 0;
    float maxOffset = 0;
    int differ = 0;
    for (int i = 0; i < 2000; ++i) {
        const double t = 0.05 * i;
        const LeafFrame f = leafFrame(dir, 300000.f, 200000.f, 1234, t, 1.0f, height, identity);
        float o[3], a[3];
        leafOffset(f, top, o);
        leafOffset(f, anchor, a);
        CHECK(a[0] == 0 && a[1] == 0 && a[2] == 0);
        CHECK(std::isfinite(o[0]) && std::isfinite(o[1]) && std::isfinite(o[2]));
        meanAlong += o[0];
        maxOffset = std::max(maxOffset, std::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]));
        // Two trees side by side do not move in step.
        const LeafFrame g = leafFrame(dir, 300400.f, 200000.f, 98765, t, 1.0f, height, identity);
        float p[3];
        leafOffset(g, top, p);
        differ += std::fabs(p[0] - o[0]) > 1.0f;
    }
    meanAlong /= 2000;
    // On average the crown leans downwind; at most a few percent of the height, never a jump.
    CHECK(meanAlong > 0.005 * height);
    CHECK(maxOffset > 0.01f * height && maxOffset < 0.08f * height);
    CHECK(differ > 1500);
    // Strength scales the motion, and the model's own scale is undone (a tree drawn twice as large moves the same
    // world distance: half the model units).
    const float scaled[9] = {2, 0, 0, 0, 2, 0, 0, 0, 2};
    float inv[9];
    CHECK(invert3(scaled, inv) && std::fabs(inv[0] - 0.5f) < 1e-6f);
    const LeafFrame f1 = leafFrame(dir, 1, 1, 7, 3.0, 1.0f, height, identity);
    const LeafFrame f2 = leafFrame(dir, 1, 1, 7, 3.0, 1.0f, height, inv);
    CHECK(std::fabs(f2.sway[0] - 0.5f * f1.sway[0]) < 1e-3f);
    const LeafFrame f3 = leafFrame(dir, 1, 1, 7, 3.0, 2.0f, height, identity);
    CHECK(std::fabs(f3.sway[0] - 2.0f * f1.sway[0]) < 1e-3f);
    // A rotated tree (yaw 90 degrees) gets the same world wind in its own axes.
    const float yaw[9] = {0, 0, 1, 0, 1, 0, -1, 0, 0};
    float yawInv[9];
    CHECK(invert3(yaw, yawInv));
    const LeafFrame fr = leafFrame(dir, 1, 1, 7, 3.0, 1.0f, height, yawInv);
    float world[3];
    for (int r = 0; r < 3; ++r)
        world[r] = yaw[r * 3] * fr.sway[0] + yaw[r * 3 + 1] * fr.sway[1] + yaw[r * 3 + 2] * fr.sway[2];
    CHECK(std::fabs(world[0] - f1.sway[0]) < 1e-2f && std::fabs(world[2]) < 1e-2f);
    // Level wind steers the direction; none falls back to the global wind.
    const Lean steered = steerWind(0.0f, -3.0f, 10.0);
    CHECK(std::fabs(steered.x) < 1e-6f && std::fabs(steered.z + 1.0f) < 1e-6f);
    const Lean global = steerWind(0.0f, 0.0f, 10.0), expect = windDirection(10.0);
    CHECK(global.x == expect.x && global.z == expect.z);
}

// Cost of the per-object model: a busy forest frame draws a few thousand trees and plants.
void benchmark() {
    const int n = 200000;
    float sink = 0;
    const auto a = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i) {
        const double t = 100.0 + (i / 32) * 0.0005; // time stamps shared by 32 objects, like the hook
        const Lean l =
            windLean(i & 1, 100000.f + 13.f * (i % 997), 200000.f + 7.f * (i % 991), static_cast<uint32_t>(i), t, 1.0f);
        sink += l.x + l.z;
    }
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - a).count();
    std::printf("windLean: %.0f ns per object (%.3f ms for 2000 objects) %g\n", us * 1000.0 / n, us / n * 2.0, sink);
}

} // namespace

int main() {
    benchmark();
    testDeterministicAndBounded();
    testStrengthScalesAndClamps();
    testGustsTravelWithTheWind();
    testNeighboursMoveTogether();
    testPush();
    testShearKeepsLength();
    testLeafClassification();
    testLeafWeights();
    testLeafMotion();
    if (g_failed) {
        std::fprintf(stderr, "%d checks failed\n", g_failed);
        return 1;
    }
    std::printf("foliage model: all checks passed\n");
    return 0;
}
