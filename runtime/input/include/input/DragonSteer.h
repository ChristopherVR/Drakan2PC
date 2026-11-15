// Mouse steering for Arokh ([mouse] dragon_mode = mouse): raw mouse counts turn the controlled dragon's heading about
// the world up axis and pitch it about its own right axis, by a fixed angle per count. There is no stick in between:
// no dead zone, no clamp at full deflection, no squared response and no rate lag. The game's flight model (rates,
// banking, speed) still runs for the keyboard and a pad. Pure logic: the integration reads and writes the 3 x 3
// rotation of the dragon's world transform (docs/reference/input.md, "Arokh flight on mouse and keyboard").
#pragma once

namespace drakan_input {

struct DragonSteerConfig {
    float sensX = 0.0020f; // radians per count ([mouse] sensitivity_x)
    float sensY = 0.0020f; // [mouse] sensitivity_y
    float scale = 1.0f;    // [mouse] dragon_sensitivity: multiplies both while flying
    bool invertX = false;
    bool invertY = false;
    float smoothingMs = 0.0f; // [mouse] dragon_smoothing: time constant of an optional exponential ease (0 = raw)
    float pitchMin = -1.0f;   // nose-down limit of the mouse pitch (radians, pitch() below)
    float pitchMax = 0.75f;   // nose-up limit
};

// Angles for one world tick, in the terms of yaw() / pitch() below: yaw > 0 turns left (mouse right gives yaw < 0),
// pitch > 0 raises the nose.
struct DragonSteerStep {
    float yaw = 0.0f;
    float pitch = 0.0f;
};

// Pending mouse displacement between world ticks. Counts are never dropped or scaled by time: with smoothing on, the
// rest is handed out on later ticks.
class DragonSteer {
public:
    void reset() { pendX_ = pendY_ = 0.0; }
    void add(int dx, int dy) {
        pendX_ += dx;
        pendY_ += dy;
    }
    bool idle() const { return pendX_ == 0.0 && pendY_ == 0.0; }
    // dt = seconds of this world tick (only used by the smoothing).
    DragonSteerStep step(const DragonSteerConfig& c, float dt);

private:
    double pendX_ = 0.0, pendY_ = 0.0;
};

// Rotation part of a guest world transform: m[r * 3 + c], rows of the matrix at transform + 0x20 (column 0 = right,
// 1 = up, 2 = back, each in world coordinates).
float dragonYaw(
    const float m[9]); // atan2(-right.z, right.x): the heading (the harnesses' atan2(back.x, right.x) while level)
float dragonPitch(const float m[9]); // atan2(-back.y, |back.xz|), > 0 nose up
// Turns every column about the world up axis; dragonYaw grows by `angle`.
void rotateWorldYaw(float m[9], float angle);
// Turns every column about the transform's own right axis (column 0); dragonPitch grows by `angle` while the right
// axis is level.
void rotateLocalPitch(float m[9], float angle);
// The part of `delta` that keeps `pitch` inside [lo, hi]; a pitch already outside may only move back towards it.
float clampPitchDelta(float pitch, float delta, float lo, float hi);

} // namespace drakan_input
