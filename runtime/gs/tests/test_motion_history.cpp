#include "motion_history.h"
#include <cmath>
#include <cstdio>
#include <limits>
using namespace gsr::motion;
int main() {
    unsigned checks = 0, failures = 0;
    auto check = [&](bool value, const char* name) {
        ++checks;
        if (!value) {
            ++failures;
            std::printf("FAIL %s\n", name);
        }
    };
    History h;
    Key key{1, 7, 3, 0, 0};
    std::vector<Previous> result;
    Vertex old[] = {{10, 20, 100, 0, 0}, {30, 20, 100, 1, 0}, {10, 40, 100, 0, 1}};
    h.beginFrame(1);
    check(!h.track(key, 5, old, 3, 2, 2, true, result).matched, "new object has no history");
    Vertex moved[] = {{13, 18, 200, 0, 0}, {33, 18, 100, 1, 0}, {13, 38, 50, 0, 1}};
    h.beginFrame(2);
    check(h.track(key, 5, moved, 3, 2, 2, true, result).matched, "moving and deforming vertices match");
    check(result[0].x / result[0].ratio == 20 && result[0].y / result[0].ratio == 40,
          "previous projected position recovered");
    check(result[0].ratio == 2 && result[2].ratio == 0.5f, "perspective depth ratios retained");
    const float x = (result[0].x + result[1].x + result[2].x) / (result[0].ratio + result[1].ratio + result[2].ratio);
    check(std::abs(x - (40 + 60 + 10) / 3.5f) < 1e-5f, "perspective-correct interpolated previous position");
    check(!h.track(key, 5, moved, 3, 2, 2, true, result).matched, "duplicate identity rejected");
    h.beginFrame(3);
    check(!h.track(key, 5, moved, 3, 2, 2, true, result).matched, "ambiguous previous frame discarded");
    h.beginFrame(4);
    auto clipped = moved[0];
    moved[0].u = .1f;
    check(!h.track(key, 5, moved, 3, 2, 2, true, result).matched, "clipped or reordered UV rejected");
    moved[0] = clipped;
    h.beginFrame(5);
    check(!h.track(key, 6, moved, 3, 2, 2, true, result).matched, "topology generation mismatch rejected");
    h.beginFrame(6);
    Key respawn = key;
    ++respawn.generation;
    check(!h.track(respawn, 6, moved, 3, 2, 2, true, result).matched, "reused address generation rejected");
    h.beginFrame(8);
    check(!h.track(respawn, 6, moved, 3, 2, 2, true, result).matched, "missing engine frame resets history");
    h.beginFrame(9, true);
    check(!h.track(respawn, 6, moved, 3, 2, 2, true, result).matched, "explicit camera cut resets history");
    h.beginFrame(10);
    moved[0].x = std::numeric_limits<float>::infinity();
    check(!h.track(respawn, 6, moved, 3, 2, 2, true, result).matched, "nonfinite source rejected");
    moved[0] = clipped;
    check(!h.track(respawn, 6, moved, 3, 2, 2, false, result).matched, "unproven correspondences rejected");
    History limited(1);
    limited.beginFrame(1);
    limited.track(key, 6, moved, 3, 2, 2, true, result);
    check(limited.bytes() == 0, "memory budget enforced");
    History sampled(1024 * 1024, true);
    sampled.beginFrame(1);
    sampled.track(key, 5, old, 3, 2, 2, true, result);
    sampled.commitFrame();
    sampled.beginFrame(2);
    sampled.track(key, 5, moved, 3, 2, 2, true, result); // submitted but not consumed
    sampled.beginFrame(4);
    check(sampled.track(key, 5, moved, 3, 2, 2, true, result).matched,
          "manual history keeps last accepted frame across skipped submissions");
    check(result[0].x / result[0].ratio == 20, "manual reference uses accepted position, not skipped frame");
    std::printf("motion history %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
