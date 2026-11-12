// Ground texture distance rules (world/TerrainDistance.h): pure functions, synthetic numbers only.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "world/TerrainDistance.h"

namespace {

int g_failed = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                          \
            ++g_failed;                                                                                                \
        }                                                                                                              \
    } while (0)

using namespace world::terrain_distance;

bool sameBits(float a, float b) {
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

} // namespace

int main() {
    // factor: clamped to 1 .. 4, anything that is not a number is "off"
    CHECK(clampFactor(1.0f) == 1.0f && clampFactor(0.2f) == 1.0f && clampFactor(-3.0f) == 1.0f);
    CHECK(clampFactor(2.5f) == 2.5f && clampFactor(9.0f) == kMaxFactor);
    CHECK(clampFactor(std::numeric_limits<float>::quiet_NaN()) == kOff);
    CHECK(clampFactor(std::numeric_limits<float>::infinity()) == kOff);

    // off returns the stored value itself, bit for bit
    for (float stored : {1.0f, 0.75f, 3.0f, 0.0f, -1.0f})
        CHECK(sameBits(viewScale(stored, 1.0f), stored));
    CHECK(sameBits(viewScale(1.0f, 0.5f), 1.0f));
    // on: the scale the guest multiplies the chunk distance with shrinks by the factor
    CHECK(viewScale(1.0f, 2.0f) == 0.5f && viewScale(1.0f, 4.0f) == 0.25f && viewScale(0.8f, 2.0f) == 0.4f);
    // a stored value that is not a plain scale is left alone (the option then does nothing rather than harm)
    CHECK(sameBits(viewScale(0.0f, 2.0f), 0.0f));
    CHECK(sameBits(viewScale(-1.0f, 2.0f), -1.0f));
    CHECK(sameBits(viewScale(5000.0f, 2.0f), 5000.0f));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(std::isnan(viewScale(nan, 2.0f)));

    // what the option buys: a ground texture 20000 units away keeps 67 % of its weight originally (fade distance of
    // 30 cells), 84 % at factor 2 and 92 % at factor 4; at 61440 units it was gone and is half there at factor 2
    const float fade = 30.0f * 2048.0f;
    CHECK(std::abs(splatWeight(20000.0f, fade, 1.0f) - 0.6745f) < 1e-3f);
    CHECK(std::abs(splatWeight(20000.0f, fade, 2.0f) - 0.8372f) < 1e-3f);
    CHECK(std::abs(splatWeight(20000.0f, fade, 4.0f) - 0.9186f) < 1e-3f);
    CHECK(splatWeight(61440.0f, fade, 1.0f) == 0.0f && std::abs(splatWeight(61440.0f, fade, 2.0f) - 0.5f) < 1e-6f);
    CHECK(splatWeight(0.0f, fade, 1.0f) == 1.0f && splatWeight(1.0e9f, fade, 4.0f) == 0.0f);
    CHECK(splatWeight(100.0f, 0.0f, 2.0f) == 0.0f);

    if (g_failed)
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
    else
        std::printf("terrain distance rules: all checks passed\n");
    return g_failed ? 1 : 0;
}
