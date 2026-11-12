#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace drakan_frame_rate {
// Native keyboard Walk also forces the vertical dodge (state 19) in jump-start, before it tests forward speed.
// Keep that action for stationary/backward/sideways input; a moving forward PC walk uses the normal jump path.
inline bool useForwardWalkJump(uint32_t ra, uint32_t flags, uint32_t state, uint32_t forward, uint32_t sideways,
                               float speed) {
    return ra == 0x2f94b0u && (flags & 2u) && state == 19 && (forward & 1u) && !(forward & 2u) && !(sideways & 3u) &&
           speed > 1000.0f;
}

// A ground snap can leave only vertical displacement in the position history, while the collision-resolved
// motion still contains this tick's forward travel. At high rates the fixed snap overwhelms the smaller motion
// and the native velocity chooser returns a vertical-only vector. Restore resolved motion for the
// forward-speed measurement, not for airborne physics or the native "prefer slower" mode.
inline void restoreGroundSnapMotion(float* velocity, const float* displacement, const float* motion,
                                    bool preferSlower) {
    if (!preferSlower && displacement[0] == 0.0f && displacement[2] == 0.0f && displacement[1] != 0.0f &&
        velocity[0] == 0.0f && velocity[2] == 0.0f && (motion[0] != 0.0f || motion[2] != 0.0f)) {
        velocity[0] = motion[0] * 30.0f;
        velocity[1] = motion[1] * 30.0f;
        velocity[2] = motion[2] * 30.0f;
    }
}

// Only the graphics worker's SleepThread call participates. Other threads,
// resumed syscalls, counter overflow and the original cadence are unchanged.
constexpr uint32_t cadenceCount(uint32_t count, bool higher, uint32_t pc, uint32_t ra) {
    return higher && pc == 0x4bc080u && ra == 0x1d7a24u && count >= 2 && count < 4 ? 4 : count;
}

// Scene objects (vtable 51b8b0) keep their per-vertex colours at +78. The pass 189288 frees the colours of objects
// far from the camera (18ba28) through vtable +11c (17e478, called at 18938c); a later draw (189b38) rebuilds them
// from the lights it gathers. An object with no layer (+7c = -1) and no light link (*(+a0) or its slot 1 empty) gathers
// none, so the draw clears its lighting flags +74 for good and it is drawn with the default colour 0x7f. The inventory
// page grid is such an object: 13af38 lights it once when the page is built. At the faster cadence the pass can run
// between that and the grid's first draw, so the grid lost its lines on some opens.
inline constexpr uint32_t kFarLightingRelease = 0x18938cu; // return address of the vtable +11c call in 189288
inline constexpr uint32_t kSceneObjectVtable = 0x51b8b0u;

struct ObjectLighting {
    uint32_t vtable = 0, layer = 0, lightSource = 0, colours = 0; // +0, +7c, (*(+a0))[1] or 0, +78
};

// True when the far-object pass is about to free colours that no draw can rebuild.
constexpr bool keepUnrebuildableLighting(uint32_t ra, const ObjectLighting& object) {
    return ra == kFarLightingRelease && object.vtable == kSceneObjectVtable && object.layer == 0xffffffffu &&
           object.lightSource == 0 && object.colours != 0;
}

// Engine tick time while presentation is vsync-locked. Every shown frame lasts whole refreshes, but 1c3028 reads the
// 1 ms game clock wherever the EE happens to be after the previous present, so release jitter moved the world 14 then
// 19 ms per 16.7 ms frame. Give the tick whole refreshes and carry the difference into the next one (total game time
// stays the clock's within half a refresh). A tick shorter than half a refresh (a catch-up after a stall) is off the
// grid: it passes through and drops a positive carry. A negative carry (earlier ticks were rounded up) is repaid by
// such a tick instead: the cut scene loop runs a full tick and a 1 - 3 ms tick per 33 ms, and dropping the debt there
// ran every scene 6 % fast (2.3 ms rounded up + the 2 ms tick, per cycle). `carry`: seconds measured but not yet
// given, may be negative.
inline double snapTickToRefresh(double measured, double refresh, double& carry) {
    if (!(measured >= 0.0) || !(refresh > 0.0)) {
        carry = 0.0;
        return measured;
    }
    const double due = measured + carry;
    if (due < refresh * 0.5) {
        if (carry < 0.0) {
            constexpr double kMinTick = 0.0005; // never hand the engine a zero time step
            const double given = due > kMinTick ? due : kMinTick;
            carry = due - given;
            return given;
        }
        carry = 0.0;
        return measured;
    }
    const double given = std::round(due / refresh) * refresh;
    carry = due - given;
    return given;
}
inline constexpr float kRopeTickLimit = 1.0f / 30.0f;
inline constexpr int kRopeMaxSteps = 8;
// 3e3610 (rope / cable swing, engine class 0x10049) adds velocity * tick to the rope's angle with a small-angle step
// and only binds the objects the rope carries (3ab1f8 -> 3aafd8) after its first step. Both assume the console's steady
// tick: a carried object stays behind the rope by the rope's first step for good, and one long tick (a load hitch, a
// slow frame) leaves the swing off-centre for good. So the rope update runs as `steps` updates of `seconds` each: one
// update of 0 s until its carried objects are bound (it binds them where the level placed them), otherwise the tick
// split into equal parts of at most the console's 1/30 s (a tick longer than kRopeMaxSteps of those is cut short: the
// rope then lags a frame slower than 4 per second instead of jumping).
struct RopeTicks {
    int steps;
    float seconds;
};
inline RopeTicks ropeTicks(float tick, bool swinging, bool carries, bool carriedBound) {
    if (!swinging || !(tick > 0.0f))
        return {1, tick};
    if (carries && !carriedBound)
        return {1, 0.0f};
    if (tick <= kRopeTickLimit)
        return {1, tick};
    int steps = static_cast<int>(tick / kRopeTickLimit);
    if (static_cast<float>(steps) * kRopeTickLimit < tick)
        ++steps;
    if (steps > kRopeMaxSteps)
        return {kRopeMaxSteps, kRopeTickLimit};
    return {steps, tick / static_cast<float>(steps)};
}
} // namespace drakan_frame_rate
