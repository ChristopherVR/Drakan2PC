#include "drakan_frame_policy.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
int main() {
    using drakan_frame_rate::useForwardWalkJump;
    assert(useForwardWalkJump(0x2f94b0, 2, 19, 0x101, 0, 1168));
    assert(!useForwardWalkJump(0x2f94b8, 2, 19, 0x101, 0, 1168));
    assert(!useForwardWalkJump(0x2f94b0, 0, 19, 0x101, 0, 1168));
    assert(!useForwardWalkJump(0x2f94b0, 2, 17, 0x101, 0, 1168));
    assert(!useForwardWalkJump(0x2f94b0, 2, 19, 0, 0, 1168));
    assert(!useForwardWalkJump(0x2f94b0, 2, 19, 2, 0, 1168));
    assert(!useForwardWalkJump(0x2f94b0, 2, 19, 3, 0, 1168));
    assert(!useForwardWalkJump(0x2f94b0, 2, 19, 0x101, 1, 1168));
    assert(!useForwardWalkJump(0x2f94b0, 2, 19, 0x101, 0, 1000));
    assert(!useForwardWalkJump(0x2f94b0, 2, 19, 0x101, 0, 0));
    using drakan_frame_rate::restoreGroundSnapMotion;
    const float snap[3] = {0, -35, 0}, walk[3] = {-7.03125f, -0.015625f, -0.0625f};
    float velocity[3] = {0, -1050, 0};
    restoreGroundSnapMotion(velocity, snap, walk, false);
    assert(velocity[0] == walk[0] * 30 && velocity[1] == walk[1] * 30 && velocity[2] == walk[2] * 30);
    // Recorded 6 ms walking tick: the corrected forward speed clears the game's 1000-unit jump threshold.
    assert(-velocity[0] / (30 * 0.006f) > 1000);
    for (const auto& motion : {std::initializer_list<float>{0, 0, 0}, {0, -2, 0}}) {
        float still[3] = {0, -1050, 0};
        restoreGroundSnapMotion(still, snap, motion.begin(), false);
        assert(still[0] == 0 && still[2] == 0); // stationary or blocked by a wall: no invented forward speed
        // No change to its native vertical result either.
        assert(still[1] == -1050);
    }
    const float normal[3] = {-7, 0, 0}, noSnap[3] = {0, 0, 0};
    for (const float* displacement : {normal, noSnap}) {
        float native[3] = {0, -1050, 0};
        restoreGroundSnapMotion(native, displacement, walk, false);
        assert(native[0] == 0 && native[2] == 0);
    }
    float slower[3] = {0, -1050, 0}, moving[3] = {-3, -1050, 1};
    restoreGroundSnapMotion(slower, snap, walk, true);
    restoreGroundSnapMotion(moving, snap, walk, false);
    assert(slower[0] == 0 && slower[2] == 0);
    assert(moving[0] == -3 && moving[2] == 1);
    using drakan_frame_rate::cadenceCount;
    constexpr unsigned pc = 0x4bc080, ra = 0x1d7a24;
    for (unsigned n = 0; n < 100; ++n) {
        assert(cadenceCount(n, false, pc, ra) == n);
        assert(cadenceCount(n, true, pc + 8, ra) == n);
        assert(cadenceCount(n, true, pc, ra + 4) == n);
        assert(cadenceCount(n, true, pc, ra) == (n == 2 || n == 3 ? 4 : n));
    }
    assert(cadenceCount(0xffffffffu, true, pc, ra) == 0xffffffffu);
    for (bool higher : {false, true}) {
        unsigned count = 0, wakes = 0;
        // Mirrors native increment -> SleepThread -> test, including the case
        // where a queued wake makes SleepThread return directly.
        while (count < 4) {
            count = cadenceCount(count + 1, higher, pc, ra);
            ++wakes;
        }
        assert(wakes == (higher ? 2u : 4u));
    }
    // The far-object pass keeps colours only no draw could rebuild (inventory page grid).
    using drakan_frame_rate::keepUnrebuildableLighting;
    using drakan_frame_rate::ObjectLighting;
    constexpr unsigned pass = drakan_frame_rate::kFarLightingRelease, vt = drakan_frame_rate::kSceneObjectVtable;
    const ObjectLighting grid{vt, 0xffffffffu, 0, 0x17eb090u};
    assert(keepUnrebuildableLighting(pass, grid));
    assert(!keepUnrebuildableLighting(pass + 4, grid));                                  // other callers of +11c
    assert(!keepUnrebuildableLighting(pass, {vt, 0x131u, 0, 0x17eb090u}));               // layer lights rebuild it
    assert(!keepUnrebuildableLighting(pass, {vt, 0xffffffffu, 0x165272cu, 0x17eb090u})); // light link rebuilds it
    assert(!keepUnrebuildableLighting(pass, {vt, 0xffffffffu, 0, 0}));                   // nothing to keep
    assert(!keepUnrebuildableLighting(pass, {vt + 8, 0xffffffffu, 0, 0x17eb090u}));      // not a scene object
    // Tick time snaps to whole refreshes while keeping the clock's total.
    using drakan_frame_rate::snapTickToRefresh;
    constexpr double T = 1.0 / 59.9956;
    auto near = [](double a, double b) { return std::fabs(a - b) < 1e-9; };
    double carry = 0, measuredSum = 0, givenSum = 0;
    for (int i = 0; i < 600; ++i) { // +-2.5 ms release jitter on a 60 Hz display: every tick one refresh
        const double measured = T + ((i & 1) ? 0.0025 : -0.0025);
        const double given = snapTickToRefresh(measured, T, carry);
        assert(near(given, T));
        assert(std::fabs(carry) <= 0.0025 + 1e-12);
        measuredSum += measured;
        givenSum += given;
    }
    assert(std::fabs(measuredSum - givenSum - carry) < 1e-9); // nothing lost
    carry = measuredSum = givenSum = 0;
    for (int i = 0; i < 20000; ++i) { // the clock runs 0.1 % fast: the carry moves until one tick takes two refreshes
        const double measured = T * 1.001;
        const double given = snapTickToRefresh(measured, T, carry);
        assert(near(given, T) || near(given, 2 * T));
        assert(std::fabs(carry) <= T / 2 + 1e-12);
        measuredSum += measured;
        givenSum += given;
    }
    assert(std::fabs(measuredSum - givenSum - carry) < 1e-9);
    carry = 0;
    assert(near(snapTickToRefresh(0.0334, T, carry), 2 * T)); // a frame shown for two refreshes
    carry = 0.003;
    assert(near(snapTickToRefresh(0.004, T, carry), 0.004) && carry == 0); // catch-up tick passes through
    carry = 0.002;
    assert(near(snapTickToRefresh(0.016, 0.0, carry), 0.016) && carry == 0); // not vsync-locked
    carry = 0.002;
    assert(snapTickToRefresh(-1.0, T, carry) == -1.0 && carry == 0);
    // Cut scene loop: one ~31 ms tick and one ~2 ms tick per 33.3 ms. The short tick repays what the long one was
    // rounded up by, so the world keeps the clock's pace (it ran 6 % fast when the debt was dropped).
    carry = measuredSum = givenSum = 0;
    for (int i = 0; i < 3000; ++i) {
        for (const double measured : {0.0313, 0.002}) {
            const double given = snapTickToRefresh(measured, T, carry);
            assert(given > 0.0);
            assert(std::fabs(carry) <= T / 2 + 1e-12);
            measuredSum += measured;
            givenSum += given;
        }
    }
    assert(std::fabs(givenSum / measuredSum - 1.0) < 0.005); // within 0.5 % over 100 s (was +7 %)
    carry = -0.0023;
    assert(near(snapTickToRefresh(0.002, T, carry), 0.0005) && near(carry, -0.0008)); // never zero
    std::puts("Frame cadence and forward-jump checks passed");
}
