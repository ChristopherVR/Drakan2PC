#include "input/DragonSteer.h"
#include <algorithm>
#include <cmath>

namespace drakan_input {

DragonSteerStep DragonSteer::step(const DragonSteerConfig& c, float dt) {
    double share = 1.0;
    if (c.smoothingMs > 0.0f && dt > 0.0f)
        share = 1.0 - std::exp(-static_cast<double>(dt) * 1000.0 / static_cast<double>(c.smoothingMs));
    else if (c.smoothingMs > 0.0f)
        share = 0.0; // a tick without time hands nothing out
    double x = pendX_ * share, y = pendY_ * share;
    // Less than a hundredth of a count left: hand it out now so a stop is a stop.
    if (std::fabs(pendX_ - x) < 0.01)
        x = pendX_;
    if (std::fabs(pendY_ - y) < 0.01)
        y = pendY_;
    pendX_ -= x;
    pendY_ -= y;
    DragonSteerStep s;
    s.yaw = static_cast<float>(-x * c.sensX * c.scale * (c.invertX ? -1.0 : 1.0));
    s.pitch = static_cast<float>(-y * c.sensY * c.scale * (c.invertY ? -1.0 : 1.0));
    return s;
}

float dragonYaw(const float m[9]) {
    return std::atan2(-m[6], m[0]);
}

float dragonPitch(const float m[9]) {
    return std::atan2(-m[5], std::hypot(m[2], m[8]));
}

void rotateWorldYaw(float m[9], float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    for (int col = 0; col < 3; ++col) {
        const float x = m[col], z = m[6 + col];
        m[col] = x * c + z * s;
        m[6 + col] = -x * s + z * c;
    }
}

void rotateLocalPitch(float m[9], float angle) {
    float k[3] = {m[0], m[3], m[6]};
    const float len = std::sqrt(k[0] * k[0] + k[1] * k[1] + k[2] * k[2]);
    if (!(len > 1e-6f))
        return;
    for (float& v : k)
        v /= len;
    const float c = std::cos(angle), s = std::sin(angle);
    for (int col = 0; col < 3; ++col) {
        const float v[3] = {m[col], m[3 + col], m[6 + col]};
        const float dot = k[0] * v[0] + k[1] * v[1] + k[2] * v[2];
        const float cross[3] = {k[1] * v[2] - k[2] * v[1], k[2] * v[0] - k[0] * v[2], k[0] * v[1] - k[1] * v[0]};
        for (int r = 0; r < 3; ++r)
            m[r * 3 + col] = v[r] * c + cross[r] * s + k[r] * dot * (1.0f - c);
    }
}

float clampPitchDelta(float pitch, float delta, float lo, float hi) {
    if (delta > 0.0f)
        return std::max(0.0f, std::min(delta, hi - pitch));
    if (delta < 0.0f)
        return std::min(0.0f, std::max(delta, lo - pitch));
    return 0.0f;
}

} // namespace drakan_input
