// Modern Tab screen: keeping Rynn's live preview centred in the host's preview column (docs/reference/mouse-menus.md
// section 13, "World"). Pure math, no guest memory: tests in ps2xRuntime/tests/tab_preview_policy_tests.cpp.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace drakan_tab::detail {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

// The world camera object (ctx+0x4b4 -> +0xfc -> +0x10 -> +0x148): position at +0x14, 3x3 at +0x20 (row-major). Its
// columns are the camera's right, up and back axes; the world view matrix (0x4dad58) is its transpose.
struct CameraFrame {
    Vec3 pos;
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
};

// Mean of view x / depth over the points in front of the camera (Rynn's animated joints: player+0x480, count +0x488).
// 0 = straight ahead; positive = right of the projection centre. False when no point is usable.
inline bool skeletonViewX(const Vec3* pts, size_t n, const CameraFrame& cam, float& out) {
    constexpr float kMinDepth = 1.0f;
    double sum = 0;
    size_t used = 0;
    for (size_t i = 0; i < n; ++i) {
        const float dx = pts[i].x - cam.pos.x, dy = pts[i].y - cam.pos.y, dz = pts[i].z - cam.pos.z;
        const float vx = cam.m[0] * dx + cam.m[3] * dy + cam.m[6] * dz;
        const float depth = -(cam.m[2] * dx + cam.m[5] * dy + cam.m[8] * dz); // -back
        if (!std::isfinite(vx) || !std::isfinite(depth) || depth < kMinDepth)
            continue;
        sum += vx / depth;
        ++used;
    }
    if (!used)
        return false;
    out = float(sum / double(used));
    return std::isfinite(out);
}

// Critically damped follow (frame-rate independent): x eases to the target with no overshoot; smoothTime is roughly
// the time to cover most of a step. The first sample (or a reset) snaps.
struct Follow {
    bool valid = false;
    float x = 0, v = 0;
};
inline float followStep(Follow& f, float target, float dt, float smoothTime) {
    if (!std::isfinite(target))
        return f.valid ? f.x : 0.0f;
    if (!f.valid || !(dt >= 0) || !std::isfinite(dt) || !(smoothTime > 0)) {
        f = {true, target, 0};
        return f.x;
    }
    dt = std::min(dt, 0.25f);
    const float w = 2.0f / smoothTime;
    const float e = w * dt;
    const float k = 1.0f / (1.0f + e + 0.48f * e * e + 0.235f * e * e * e); // exp(-w dt)
    const float d = f.x - target;
    const float t = (f.v + w * d) * dt;
    f.v = (f.v - w * t) * k;
    f.x = target + (d + t) * k;
    return f.x;
}

// The world crop 132f60 (from 2c4444) gets: projection centre = W/2 + offset (frame pixels), crop scale s. Projected x
// = W/2 + offset + projX * viewX, where projX = sc+0x40 * sc+0x48 of the world draw (frame pixels per unit of x /
// depth; s changes the extent by s and projectionX by 1/s, so it cancels). The offset puts a point at viewX on the
// host's centre (centreX of the output, 0.5 = middle); its shift is limited to maxShift * W. The crop covers the
// screen: s >= 1 + 2 |offset| / W (+0.01).
// Rynn's preview centre as the fraction of the game picture [vx, vx + vw] (back-buffer pixels) that the EE crop takes.
// The host column sits at the viewport's left; with a pillarboxed picture (4:3 on a wide output) its centre falls near
// or outside the picture's left edge and the pillarbox cut her in half. The centre is kept halfWidth inside the
// picture (her on-screen half width: it follows the picture height); a picture narrower than 2 halfWidth centres her.
inline float previewCentreInPicture(float centrePx, float vx, float vw, float halfWidth) {
    if (!(vw > 0) || !std::isfinite(centrePx))
        return 0.5f;
    const float h = std::clamp(halfWidth, 0.0f, 0.5f * vw);
    return (std::clamp(centrePx, vx + h, vx + vw - h) - vx) / vw;
}

struct Crop {
    float offset = 0; // frame pixels (fractional)
    float scale = 1;
};
inline Crop previewCrop(float centreX, float viewX, float projX, float width, float maxShift) {
    Crop c;
    if (!(width > 1) || !std::isfinite(width))
        return c;
    const float cx = std::isfinite(centreX) ? std::clamp(centreX, 0.1f, 0.9f) : 0.5f;
    float shift = 0;
    if (std::isfinite(viewX) && std::isfinite(projX) && projX > 0 && maxShift > 0)
        shift = std::clamp(projX * viewX, -maxShift * width, maxShift * width);
    c.offset = (cx - 0.5f) * width - shift;
    c.scale = 1.0f + 2.0f * std::fabs(c.offset) / width + 0.01f;
    return c;
}

} // namespace drakan_tab::detail
