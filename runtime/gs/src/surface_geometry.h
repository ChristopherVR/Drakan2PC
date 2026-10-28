#pragma once
#include <gsr/scene_guide_camera.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace gsr::surface {
struct Point {
    float x = 0, y = 0, z = 0;
};
inline Point sub(Point a, Point b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Point cross(Point a, Point b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Point a) {
    return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
}
struct RasterToWorld {
    double inverse[16]{};
    SceneGuideCamera camera;
    float xy[2]{};
    bool valid = false;
    bool configure(const SceneGuideCamera& c, uint64_t xyoffset) {
        valid = false;
        camera = c;
        xy[0] = float(xyoffset & 65535) / 16;
        xy[1] = float((xyoffset >> 32) & 65535) / 16;
        if (!(c.flags & SceneGuideCamera::Valid))
            return false;
        for (int i = 0; i < 3; ++i)
            if (!std::isfinite(c.viewportScale[i]) || std::abs(c.viewportScale[i]) < 1e-10)
                return false;
        double a[4][8]{};
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                a[i][j] = c.worldToClipVU[i * 4 + j];
                a[i][j + 4] = i == j;
            }
        for (int col = 0; col < 4; ++col) {
            int p = col;
            for (int i = col + 1; i < 4; ++i)
                if (std::abs(a[i][col]) > std::abs(a[p][col]))
                    p = i;
            if (!std::isfinite(a[p][col]) || std::abs(a[p][col]) < 1e-18)
                return false;
            for (int j = 0; j < 8; ++j)
                std::swap(a[col][j], a[p][j]);
            const double d = a[col][col];
            for (int j = 0; j < 8; ++j)
                a[col][j] /= d;
            for (int i = 0; i < 4; ++i)
                if (i != col) {
                    const double f = a[i][col];
                    for (int j = 0; j < 8; ++j)
                        a[i][j] -= f * a[col][j];
                }
        }
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                inverse[i * 4 + j] = a[i][j + 4];
        valid = true;
        return true;
    }
    bool point(float x, float y, float rawZ, Point& out) const {
        if (!valid)
            return false;
        const double p[] = {(x + xy[0] - camera.viewportOffset[0]) / camera.viewportScale[0],
                            (y + xy[1] - camera.viewportOffset[1]) / camera.viewportScale[1],
                            (double(rawZ) / 16 - camera.viewportOffset[2]) / camera.viewportScale[2], 1};
        double q[4]{};
        for (int j = 0; j < 4; ++j)
            for (int i = 0; i < 4; ++i)
                q[j] += p[i] * inverse[i * 4 + j];
        if (!std::isfinite(q[3]) || std::abs(q[3]) < 1e-12)
            return false;
        out = {float(q[0] / q[3]), float(q[1] / q[3]), float(q[2] / q[3])};
        return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
    }
};
// RasterToWorld with a memo of the last few converted vertices. Strip and fan batches repeat two of the previous
// triangle's three vertices; a repeated (x, y, z) returns the stored result of the same pure computation.
class CachedRasterToWorld {
    struct Slot {
        uint32_t x = 0, y = 0, z = 0;
        bool used = false, ok = false;
        Point p;
    };
    Slot slots[4];
    unsigned next = 0;
    static uint32_t bits(float f) {
        uint32_t u;
        std::memcpy(&u, &f, sizeof u);
        return u;
    }

public:
    RasterToWorld transform;
    bool configure(const SceneGuideCamera& c, uint64_t xyoffset) {
        for (auto& slot : slots)
            slot.used = false;
        return transform.configure(c, xyoffset);
    }
    bool point(float x, float y, float rawZ, Point& out) {
        const uint32_t bx = bits(x), by = bits(y), bz = bits(rawZ);
        for (const auto& slot : slots)
            if (slot.used && slot.x == bx && slot.y == by && slot.z == bz) {
                if (slot.ok)
                    out = slot.p;
                return slot.ok;
            }
        auto& slot = slots[next++ & 3];
        slot.x = bx;
        slot.y = by;
        slot.z = bz;
        slot.used = true;
        slot.ok = transform.point(x, y, rawZ, slot.p);
        if (slot.ok)
            out = slot.p;
        return slot.ok;
    }
};
inline bool horizontal(Point a, Point b, Point c) {
    const auto normal = cross(sub(b, a), sub(c, a));
    const float size = length(normal);
    return size > 4096 && std::abs(normal.y) > size * 0.995f;
}
} // namespace gsr::surface
