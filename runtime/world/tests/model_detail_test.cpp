// Model detail rules (world/ModelDetail.h): pure functions, synthetic numbers only.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "world/ModelDetail.h"

namespace {

int g_failed = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                          \
            ++g_failed;                                                                                                \
        }                                                                                                              \
    } while (0)

using namespace world::model_detail;

bool sameBits(float a, float b) {
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

// The engine's selector, as read from guest 0x102c90: one step per call.
unsigned selectorStep(unsigned slot, float d2, const float* table, unsigned count) {
    if (slot > 0 && d2 < table[slot] - kLodHysteresisSquared)
        return slot - 1;
    if (slot + 1 < count && table[slot + 1] + kLodHysteresisSquared <= d2)
        return slot + 1;
    return slot;
}

void testOffIsIdentity() {
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    for (const float v : {0.0f, -1.0f, 1.0f, 36.0e6f, 1.44e8f, 3.0e19f, nan, inf, -inf, 1.0e-30f}) {
        CHECK(sameBits(lodDistanceSquared(v, 1.0f), v));
        CHECK(sameBits(drawDistance(v, 1.0f), v));
        CHECK(sameBits(lodDistanceSquared(v, 0.5f), v)); // below 1 is off too
        CHECK(sameBits(drawDistance(v, 0.0f), v));
    }
    CHECK(clampLodFactor(nan) == 1.0f && clampDrawFactor(inf) == 1.0f);
    CHECK(clampLodFactor(0.2f) == 1.0f && clampLodFactor(100.0f) == kMaxLodFactor);
    CHECK(clampDrawFactor(-3.0f) == 1.0f && clampDrawFactor(9.0f) == kMaxDrawFactor);
    CHECK(clampLodFactor(kDefaultLodFactor) == kDefaultLodFactor);
    CHECK(clampDrawFactor(kDefaultDrawFactor) == kDefaultDrawFactor);
}

void testLodFactorMovesSwitches() {
    // A villager's table: 0 / 6000 / 12000, stored squared.
    const float table[3] = {0.0f, 6000.0f * 6000.0f, 12000.0f * 12000.0f};
    auto settle = [&](float distance, float factor, unsigned slot) {
        for (int i = 0; i < 8; ++i)
            slot = selectorStep(slot, lodDistanceSquared(distance * distance, factor), table, 3);
        return slot;
    };
    CHECK(settle(7000.0f, 1.0f, 0) == 1 && settle(13000.0f, 1.0f, 0) == 2);
    CHECK(settle(7000.0f, 4.0f, 2) == 0 && settle(23000.0f, 4.0f, 2) == 0);
    CHECK(settle(25000.0f, 4.0f, 0) == 1 && settle(49000.0f, 4.0f, 0) == 2);
    CHECK(settle(13000.0f, 2.0f, 0) == 1 && settle(25000.0f, 2.0f, 0) == 2);
    // The switch points scale exactly with the factor (hysteresis aside).
    CHECK(settle(4.0f * 6000.0f + 400.0f, 4.0f, 0) == 1);
    CHECK(settle(4.0f * 6000.0f - 400.0f, 4.0f, 1) == 0);
}

void testBelowLod() {
    const float table[3] = {0.0f, 6000.0f * 6000.0f, 12000.0f * 12000.0f};
    // LOD 1 is forbidden: from any distance and any starting slot the model ends on LOD 0 and stays there.
    for (const float distance : {100.0f, 5900.0f, 6100.0f, 20000.0f, 90000.0f})
        for (unsigned start = 0; start < 3; ++start) {
            unsigned slot = start;
            for (int i = 0; i < 8; ++i)
                slot = selectorStep(slot, belowLod(distance * distance, table[1]), table, 3);
            CHECK(slot == 0);
        }
    // LOD 2 is forbidden: LOD 1 is still reached, LOD 2 never.
    for (unsigned start = 0; start < 3; ++start) {
        unsigned slot = start;
        for (int i = 0; i < 8; ++i)
            slot = selectorStep(slot, belowLod(90000.0f * 90000.0f, table[2]), table, 3);
        CHECK(slot == 1);
    }
    CHECK(belowLod(100.0f, table[1]) == 100.0f); // near distances pass through
    // A switch distance inside the hysteresis band (Rynn's 2300 is outside it; 1000 is inside) clamps to zero.
    CHECK(belowLod(5.0e6f, 1000.0f * 1000.0f) == 0.0f);
    CHECK(belowLod(std::numeric_limits<float>::quiet_NaN(), table[1]) < table[1]);
    CHECK(belowLod(7.0f, std::numeric_limits<float>::infinity()) == 7.0f);
}

void testLookChangingLod() {
    using Lists = std::vector<std::vector<uint16_t>>;
    CHECK(firstLookChangingLod(Lists{}) == 0);
    CHECK(firstLookChangingLod(Lists{{0, 1, 2}}) == 0);
    // Lower LODs on a subset of the full model's sheets: fine.
    CHECK(firstLookChangingLod(Lists{{1, 0, 0, 2, 2, 2}, {1, 0, 2, 2}, {2}}) == 0);
    // Extra low-detail sheets at the end of the list (indices 3 and 4) from LOD 1 on.
    CHECK(firstLookChangingLod(Lists{{0, 1, 2}, {3, 4, 2}, {3, 4, 2}}) == 1);
    // Only the last LOD differs.
    CHECK(firstLookChangingLod(Lists{{0, 1}, {1, 0, 1}, {5}}) == 2);
    CHECK(firstLookChangingLod(Lists{{0, 1}, {}, {}}) == 0);
}

void testDrawDistance() {
    CHECK(drawDistance(35000.0f, 1.5f) == 52500.0f);
    CHECK(drawDistance(7071.0f, 4.0f) == 28284.0f);
    // The engine's "no limit" value and everything that is not a positive finite distance stay as they are.
    float unlimited;
    const uint32_t bits = 0x5f0ac723u;
    std::memcpy(&unlimited, &bits, 4);
    CHECK(sameBits(drawDistance(unlimited, 4.0f), unlimited));
    CHECK(drawDistance(0.0f, 4.0f) == 0.0f && drawDistance(-5.0f, 4.0f) == -5.0f);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(std::isnan(drawDistance(nan, 2.0f)));
    CHECK(drawDistance(140000.0f, kMaxDrawFactor) < kUnlimitedDrawDistance);
}

} // namespace

int main() {
    testOffIsIdentity();
    testLodFactorMovesSwitches();
    testBelowLod();
    testLookChangingLod();
    testDrawDistance();
    if (g_failed)
        std::fprintf(stderr, "world_model_detail_test: %d check(s) failed\n", g_failed);
    else
        std::printf("world_model_detail_test: all checks passed\n");
    return g_failed ? 1 : 0;
}
