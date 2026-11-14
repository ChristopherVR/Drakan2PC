// Engine budget rules (world/EngineLimits.h): pure functions, synthetic numbers only.
#include <cstdio>
#include <cstring>
#include <limits>

#include "world/EngineLimits.h"

namespace {

int g_failed = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                          \
            ++g_failed;                                                                                                \
        }                                                                                                              \
    } while (0)

bool sameBits(float a, float b) {
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

} // namespace

int main() {
    namespace sd = world::shadow_distance;
    // factor: clamped to 1 .. 4, anything that is not a number is "off"
    CHECK(sd::clampFactor(1.0f) == 1.0f && sd::clampFactor(0.3f) == 1.0f && sd::clampFactor(-2.0f) == 1.0f);
    CHECK(sd::clampFactor(2.5f) == 2.5f && sd::clampFactor(10.0f) == sd::kMaxFactor);
    CHECK(sd::clampFactor(std::numeric_limits<float>::quiet_NaN()) == sd::kOff);
    CHECK(sd::clampFactor(std::numeric_limits<float>::infinity()) == sd::kOff);

    // off returns the stored value bit for bit, also for values that are not distances
    for (float stored : {125829.125f, 0.0f, -5.0f, 1.0f})
        CHECK(sameBits(sd::scaledSquared(stored, 1.0f), stored));
    CHECK(sameBits(sd::scaledSquared(std::numeric_limits<float>::infinity(), 2.0f),
                   std::numeric_limits<float>::infinity()));
    CHECK(sameBits(sd::scaledSquared(-3.0f, 2.0f), -3.0f) && sameBits(sd::scaledSquared(0.0f, 2.0f), 0.0f));
    // on: squared distances grow with the square of the factor, so start / end / cutoff keep their ratios
    CHECK(sd::scaledSquared(100.0f, 2.0f) == 400.0f && sd::scaledSquared(100.0f, 3.0f) == 900.0f);
    const float start = 62914.5625f, end = 125829.125f, cut = 138412.046875f;
    for (float f : {1.5f, 2.0f, 4.0f}) {
        CHECK(sd::scaledSquared(end, f) / sd::scaledSquared(start, f) == end / start);
        CHECK(sd::scaledSquared(start, f) < sd::scaledSquared(end, f) &&
              sd::scaledSquared(end, f) < sd::scaledSquared(cut, f));
    }

    namespace rc = world::resource_cache;
    CHECK(rc::budgetBytes(0) == 0 && rc::budgetBytes(-4) == 0);
    CHECK(rc::budgetBytes(1) == 1024u * 1024u && rc::budgetBytes(12) == 12u * 1024u * 1024u);
    CHECK(rc::budgetBytes(1000) == static_cast<uint32_t>(rc::kMaxMb) * 1024u * 1024u);
    // only raises a positive budget
    CHECK(rc::raisedBudget(0x566666u, 12u << 20) == 12u << 20);
    CHECK(rc::raisedBudget(0x566666u, 0) == 0 && rc::raisedBudget(0x566666u, 0x400000u) == 0);
    CHECK(rc::raisedBudget(0, 12u << 20) == 0 && rc::raisedBudget(12u << 20, 12u << 20) == 0);

    if (g_failed)
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
    else
        std::printf("engine limits: all checks passed\n");
    return g_failed ? 1 : 0;
}
