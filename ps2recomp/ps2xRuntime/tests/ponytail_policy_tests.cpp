#include "../src/lib/drakan_ponytail_policy.h"
#include <cassert>
#include <cmath>
#include <iostream>

using drakan_ponytail::cadenceStep;
using drakan_ponytail::dragScale;
using drakan_ponytail::easedSpeed;
using drakan_ponytail::engineSpeed;
using drakan_ponytail::kCadenceStep;
using drakan_ponytail::kDefaultSoftSpeed;

namespace {
bool near(float a, float b, float tol) {
    return std::fabs(a - b) <= tol;
}
} // namespace

int main() {
    // The engine's speed: distance x 1/dt below the cap, distance / 0.03 above it (a 30 Hz frame of 1/30 s reads
    // 11 % high, a 0.1 s hitch 3.3 times).
    assert(near(engineSpeed(10.0f, 0.01f, 100.0f), 1000.0f, 1e-3f));
    assert(near(engineSpeed(40.0f, 1.0f / 30.0f, 30.0f), 1333.33f, 0.1f));
    assert(near(engineSpeed(100.0f, 0.1f, 10.0f), 3333.33f, 0.1f));

    // The eased speed uses the real frame time and saturates at the soft speed.
    assert(easedSpeed(0.0f, 0.03f, kDefaultSoftSpeed) == 0.0f);
    assert(easedSpeed(5.0f, 0.0f, kDefaultSoftSpeed) == 0.0f);
    const float walk = easedSpeed(1200.0f / 30.0f, 1.0f / 30.0f, kDefaultSoftSpeed);
    const float run = easedSpeed(2900.0f / 30.0f, 1.0f / 30.0f, kDefaultSoftSpeed);
    const float fall = easedSpeed(6000.0f / 30.0f, 1.0f / 30.0f, kDefaultSoftSpeed);
    assert(walk < run && run < fall && fall < kDefaultSoftSpeed); // monotonic: running lifts more than walking
    assert(walk > 0.5f * kDefaultSoftSpeed);
    // Slow sway (50 units/s) keeps most of its drag.
    assert(easedSpeed(0.5f, 0.01f, kDefaultSoftSpeed) > 0.8f * 50.0f);
    // softSpeed 0 = no easing (only the dt correction).
    assert(near(easedSpeed(30.0f, 0.03f, 0.0f), 1000.0f, 1e-2f));

    // The drag scale: 1 at rest, in (0, 1) when moving, the same eased drag at every frame rate.
    assert(dragScale(0.0f, 0.03f, 33.33f, kDefaultSoftSpeed) == 1.0f);
    const float s30 = dragScale(1200.0f / 30.0f, 1.0f / 30.0f, 30.0f, kDefaultSoftSpeed);
    const float s120 = dragScale(1200.0f / 120.0f, 1.0f / 120.0f, 120.0f, kDefaultSoftSpeed);
    assert(s30 > 0.0f && s30 < 1.0f && s120 > 0.0f && s120 < 1.0f);
    const float drag30 = s30 * engineSpeed(1200.0f / 30.0f, 1.0f / 30.0f, 30.0f);
    const float drag120 = s120 * engineSpeed(1200.0f / 120.0f, 1.0f / 120.0f, 120.0f);
    assert(near(drag30, drag120, 0.5f));
    // A 0.1 s hitch no longer triples the drag: the effective speed equals that of a steady frame at that speed.
    const float hitch = dragScale(120.0f, 0.1f, 10.0f, kDefaultSoftSpeed) * engineSpeed(120.0f, 0.1f, 10.0f);
    assert(near(hitch, drag30, 0.5f));
    // A one-step root-motion snap (100 units in 1/120 s, a "speed" of 12,000) stays below the soft speed.
    assert(dragScale(100.0f, 1.0f / 120.0f, 120.0f, kDefaultSoftSpeed) * 12000.0f < kDefaultSoftSpeed);
    // Garbage in (no frame time, a non-finite speed) leaves the engine's drag alone.
    assert(dragScale(10.0f, 0.0f, 0.0f, kDefaultSoftSpeed) == 1.0f);
    assert(dragScale(INFINITY, 0.01f, 100.0f, kDefaultSoftSpeed) == 1.0f);

    // Cadence: at 30 Hz or slower every frame steps with its own dt.
    float gathered = 0.0f;
    assert(near(cadenceStep(gathered, 1.0f / 30.0f), 1.0f / 30.0f, 1e-6f) && gathered == 0.0f);
    assert(near(cadenceStep(gathered, 0.1f), 0.1f, 1e-6f));
    // At 120 Hz three frames are held, the fourth steps with the gathered 1/30 s; the steps keep up with the clock.
    float stepped = 0.0f, held = 0.0f;
    for (int i = 0; i < 120; ++i) {
        const float s = cadenceStep(gathered, 1.0f / 120.0f);
        assert(s >= 0.0f && s <= kCadenceStep);
        if (s == 0.0f)
            held += 1.0f;
        else
            stepped += s;
    }
    assert(held >= 88.0f && held <= 92.0f);        // 30 steps per second
    assert(near(stepped + gathered, 1.0f, 1e-3f)); // no time lost
    // At 60 Hz every other frame is held (30 steps per second); at 144 and 240 Hz a step stays within a 30 Hz frame.
    gathered = 0.0f;
    assert(cadenceStep(gathered, 1.0f / 60.0f) == 0.0f);
    assert(near(cadenceStep(gathered, 1.0f / 60.0f), 1.0f / 30.0f, 1e-5f));
    gathered = 0.0f;
    for (int i = 0; i < 1000; ++i)
        assert(cadenceStep(gathered, (i % 2 ? 1.0f / 144.0f : 1.0f / 240.0f)) <= kCadenceStep);
    // A zero frame time is the engine's own case (no step time): passed through, the gathered time cleared.
    gathered = 0.01f;
    assert(cadenceStep(gathered, 0.0f) == 0.0f && gathered == 0.0f);
    std::cout << "ponytail policy: ok\n";
    return 0;
}
