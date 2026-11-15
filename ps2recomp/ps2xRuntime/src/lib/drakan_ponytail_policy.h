#pragma once
// Rynn's ponytail is the engine's vertex cloth (0x11d368, docs/journal/2026-11-16/ponytail-lift.md). Each step drags
// every particle with an air drag of `k2c` (5 per second for Rynn) against the body's velocity, measured as
// (position - last position) x 1/dt. With gravity 4263 units/s^2 the hair's terminal speed is about 850 units/s, so at
// walking speed (1200) the drag already outweighs gravity and the tail is held out level; running (2900) lifts it
// past horizontal. Three flaws make it worse:
// * above 33 frames per second the passes of the step that do not scale with dt run more often per second and hold
//   the tail out stiffly with no drag at all (cadenceStep, below);
// * the step caps dt at 0.03 s and then uses 1/0.03 for the velocity, although the position moved for the whole
//   frame: at 30 Hz (dt 0.033 to 0.034) the speed reads 13 % high, after a 0.1 s hitch 3.3 times;
// * a root-motion snap at an animation change moves the body up to 100 units in one step, a one-step "speed" of
//   up to 10,000.
// dragScale scales `k2c` for one step so the drag sees an eased speed: the true speed (over the real dt) through
// v / (1 + v / softSpeed). Slow sway keeps most of its drag, and the drag force saturates towards k2c x softSpeed
// (1,500 for Rynn, a lift of about 20 degrees from drag alone), so walking swings the tail without holding it out
// and a one-step snap is a small kick. Running still lifts it more (the speed is monotonic and the bounce of the run
// and the body collision act unchanged). At rest the scale is 1: the level's wind and the standing hang are unchanged.
// k2c also scales the level wind's push while moving, and with k2c below k28 the step's (k28 - k2c) x g x dt term
// adds up to 15 % gravity while moving; both only make the tail hang lower.
#include <cmath>

namespace drakan_ponytail {

constexpr float kEngineDtCap = 0.03f;       // the step's dt cap (and 1/dt = 33.33 above it)
constexpr float kDefaultSoftSpeed = 300.0f; // units/s where the eased speed starts to fall behind the true speed

// The speed the engine's step computes from a displacement of `distance` units: distance x 1/dt, with its cap.
inline float engineSpeed(float distance, float worldDt, float worldInvDt) {
    return distance * (worldDt > kEngineDtCap ? 1.0f / kEngineDtCap : worldInvDt);
}

// The speed the drag should see: the true speed over the real frame time, eased above `softSpeed`.
inline float easedSpeed(float distance, float worldDt, float softSpeed) {
    if (!(worldDt > 0.0f) || !(distance > 0.0f))
        return 0.0f;
    const float v = distance / worldDt;
    if (!(softSpeed > 0.0f))
        return v;
    return v / (1.0f + v / softSpeed);
}

// Factor for the cloth's drag coefficient (resource + 0x2c) for this step: eased speed / engine speed, in (0, 1].
inline float dragScale(float distance, float worldDt, float worldInvDt, float softSpeed) {
    const float engine = engineSpeed(distance, worldDt, worldInvDt);
    if (!(engine > 1e-3f) || !std::isfinite(engine))
        return 1.0f;
    const float eased = easedSpeed(distance, worldDt, softSpeed);
    const float s = eased / engine;
    return s > 1.0f ? 1.0f : (s > 0.0f ? s : 0.0f);
}

// Step cadence. The PS2 ran the step at 25 to 30 Hz (dt 0.03 after the cap). At the PC's frame rates its passes that
// do not scale with dt (re-basing the particles into the moving body's frame, the length constraints, the body
// collisions) run two to seven times as often per second and hold the tail out stiffly even with no drag at all. Above
// 33 frames per second the step therefore runs on the frame nearest each 30 Hz step, with the time gathered since the
// last step as its dt (the engine caps it at 0.03, as at 30 Hz); on the frames in between the particles are held in the
// body's frame. The world dt is in whole milliseconds and jitters, so the rule is half a frame either side of 1/30 s,
// and a step never gathers more than kCadenceMaxStep (no slow motion at 40 to 55 fps, which then step every frame).
// Returns the dt to step with, or 0 to hold this frame. `gathered` is per cloth.
constexpr float kCadenceStep = 1.0f / 30.0f;
constexpr float kCadenceMaxStep = 0.0365f;

inline float cadenceStep(float& gathered, float frameDt) {
    if (!(frameDt > 0.0f) || frameDt >= kEngineDtCap) {
        gathered = 0.0f; // 33 Hz or slower: the engine's own step, as on the PS2
        return frameDt;
    }
    gathered += frameDt;
    if (gathered < kCadenceStep - 0.5f * frameDt && gathered + frameDt <= kCadenceMaxStep)
        return 0.0f;
    const float dt = gathered;
    gathered = 0.0f;
    return dt;
}

} // namespace drakan_ponytail
