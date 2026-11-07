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
