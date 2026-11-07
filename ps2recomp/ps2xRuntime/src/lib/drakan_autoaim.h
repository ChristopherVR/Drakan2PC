// Drakan play-test aid: steering helpers for DRAKAN_PLAYTEST_AUTOAIM (docs/reference/env-flags.md). Header only so the
// conventions are unit tested without the guest (scripts/tests/CMakeLists.txt, autoaim_policy).
#pragma once

#include <cmath>
#include <cstdlib>
#include <vector>

namespace drakan_autoaim {

// The `place` yaw convention (measured, drakan_playtest.h): 0 faces -z, -pi/2 faces +x, so facing the vector (dx, dz)
// is yaw = atan2(-dx, -dz).
inline float yawToward(float dx, float dz) {
    return std::atan2(-dx, -dz);
}

// The pure rotation about y the engine keeps at actor + 0x20 (row major 3x3), yaw = atan2(m[0][2], m[0][0]).
inline void yawMatrix(float yaw, float m[9]) {
    const float c = std::cos(yaw), s = std::sin(yaw);
    const float v[9] = {c, 0, s, 0, 1, 0, -s, 0, c};
    for (int i = 0; i < 9; ++i)
        m[i] = v[i];
}

inline float yawOfMatrix(const float m[9]) {
    return std::atan2(m[2], m[0]);
}

// Rotation that faces the vector (dx, dy, dz) including pitch, in the engine's layout (the axes are the matrix columns:
// forward = -column 2, yaw 0 faces -z, up = column 1). Writes 9 floats row major, like yawMatrix.
inline void aimMatrix(float dx, float dy, float dz, float m[9]) {
    const float horiz = std::sqrt(dx * dx + dz * dz);
    const float len = std::sqrt(horiz * horiz + dy * dy);
    if (len < 1e-3f) {
        yawMatrix(0.0f, m);
        return;
    }
    const float yaw = yawToward(dx, dz);
    const float pitch = std::atan2(dy, horiz);
    const float cp = std::cos(pitch), sp = std::sin(pitch), sy = std::sin(yaw), cy = std::cos(yaw);
    const float f[3] = {-sy * cp, sp, -cy * cp}; // forward
    const float c2[3] = {-f[0], -f[1], -f[2]};   // column 2
    const float c0[3] = {cy, 0.0f, -sy};         // column 0 (right), horizontal
    const float c1[3] = {c2[1] * c0[2] - c2[2] * c0[1], c2[2] * c0[0] - c2[0] * c0[2], c2[0] * c0[1] - c2[1] * c0[0]};
    for (int r = 0; r < 3; ++r) {
        m[r * 3 + 0] = c0[r];
        m[r * 3 + 1] = c1[r];
        m[r * 3 + 2] = c2[r];
    }
}

// Moves `cur` toward `target` by at most `maxStep`.
inline float stepToward(float cur, float target, float maxStep) {
    const float d = target - cur;
    return std::fabs(d) <= maxStep ? target : cur + (d > 0 ? maxStep : -maxStep);
}

struct Waypoint {
    float p[3];
};

// "x y z; x y z; ..." (commas or spaces inside a point, ';' between points); malformed points are skipped.
inline std::vector<Waypoint> parseWaypoints(const char* text) {
    std::vector<Waypoint> out;
    if (!text)
        return out;
    const char* c = text;
    while (*c) {
        Waypoint w{};
        int n = 0;
        while (*c && *c != ';') {
            char* end = nullptr;
            const float f = std::strtof(c, &end);
            if (end != c) {
                if (n < 3)
                    w.p[n] = f;
                ++n;
                c = end;
            } else {
                ++c;
            }
        }
        if (n == 3)
            out.push_back(w);
        if (*c == ';')
            ++c;
    }
    return out;
}

} // namespace drakan_autoaim
