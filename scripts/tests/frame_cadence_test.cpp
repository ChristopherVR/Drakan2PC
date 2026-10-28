#include "drakan_frame_policy.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
int main() {
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
    std::puts("41615 frame cadence checks passed");
}
