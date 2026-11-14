// View distance rules (world/ViewDistance.h): pure functions, synthetic numbers only.
#include <cmath>
#include <cstdio>
#include <limits>

#include "world/ViewDistance.h"

namespace {

int g_failed = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                          \
            ++g_failed;                                                                                                \
        }                                                                                                              \
    } while (0)

using namespace world::view_distance;

bool near(float a, float b) {
    return std::fabs(a - b) <= 1e-6f * std::max(std::fabs(a), std::fabs(b));
}

} // namespace

int main() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    // factor: 1 .. 3, anything that is not a number is "off"
    CHECK(clampFactor(1.0f) == kOff && clampFactor(0.5f) == kOff && clampFactor(-2.0f) == kOff);
    CHECK(clampFactor(2.0f) == 2.0f && clampFactor(7.0f) == kMaxFactor);
    CHECK(clampFactor(nan) == kOff && clampFactor(std::numeric_limits<float>::infinity()) == kOff);

    // arguments the setter can take without a division by zero
    CHECK(valid({75.0f, 48.75f}) && valid({16.0f, 6.0f}) && valid({10.0f, 10.0f}));
    CHECK(!valid({0.0f, 1.0f}) && !valid({10.0f, 0.0f}) && !valid({10.0f, 11.0f}) && !valid({nan, 1.0f}));

    // the fields as the guest writes them for the numbers found in every level: far 75 cells, range 48.75
    const FogFields f = fields({75.0f, 48.75f});
    CHECK(f.farCells == 75.0f);
    CHECK(f.record[0] == 153600.0f && f.record[4] == 153600.0f); // far
    CHECK(f.record[5] == 99840.0f && f.record[7] == 53760.0f);   // range, fog start
    CHECK(near(f.record[1], 53760.0f * 53760.0f) && near(f.record[2], 153600.0f * 153600.0f));
    CHECK(near(f.farSquared, 153600.0f * 153600.0f));
    CHECK(near(f.record[6], 1.0f / 99840.0f));
    CHECK(near(f.record[3], 1.0f / (153600.0f * 153600.0f - 53760.0f * 53760.0f)));

    // factor 1 leaves the arguments as they are; 2 moves the fog start and end out together
    const FogArgs a{75.0f, 48.75f};
    CHECK(scaled(a, 1.0f).farCells == 75.0f && scaled(a, 1.0f).rangeCells == 48.75f);
    const FogFields g = fields(scaled(a, 2.0f));
    CHECK(g.farCells == 150.0f && g.record[0] == 307200.0f && g.record[7] == 107520.0f);
    // the fog ramp keeps its shape: start / far is the same at every factor
    CHECK(near(g.record[7] / g.record[0], f.record[7] / f.record[0]));
    // the clamp holds for the terrain grid: at most 3 times
    CHECK(scaled(a, 5.0f).farCells == 225.0f);

    if (g_failed)
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
    else
        std::printf("view_distance_test: all checks passed\n");
    return g_failed ? 1 : 0;
}
