// Near-plane clipping of the engine's projected polygons (header only, no guest access).
//
// The polygon pipeline (FUN_00146378 -> FUN_00146880 -> GIF emitter FUN_001d9de8) receives vertices that are already
// projected: screen x / y (GS primitive units, centre 2048), z proportional to q, w (fog / alpha float), s / t (plain
// texture coordinates), q = 1 / camera depth and a packed RGBA word. The engine only checks that one vertex is in front
// of the camera and then draws the whole polygon, so a ribbon whose far edge is behind the camera is sent to the GS
// with q < 0 on two vertices: the perspective division flips, the texture coordinates pass through a singularity and
// the screen fills with starburst fans. This header clips such polygons against the plane camera depth = nearW (and
// against a guard band that keeps the result inside the GS 16 bit coordinate window) with Sutherland-Hodgman in
// homogeneous space, interpolating position, depth, s / t, fog and colour exactly where they are linear.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

namespace world::polyclip {

struct Vertex { // 32 bytes, the layout the engine hands to the emitter
    float x, y, z, w;
    float s, t, q;
    uint32_t rgba;
};
static_assert(sizeof(Vertex) == 32, "engine vertex layout");

struct Params {
    float centreX = 2048.0f, centreY = 2048.0f; // GS primitive coordinate of the screen centre
    float nearW = 8.0f;                         // camera depth (1 / q) of the clip plane
    float guard = 1800.0f;                      // max |x - centre|, |y - centre| after clipping (GS window is +-2048)
    float trigger = 2040.0f;                    // polygons with a vertex beyond this reach the 16 bit XY wrap
};

constexpr int kMaxClipped = 16; // a triangle clipped by 5 planes has at most 8 vertices

// True when the polygon has a vertex in front of (q > 0) and a vertex behind (q < 0) the camera, i.e. the only case
// the clipper changes; every other polygon (and any with a q of exactly 0 or a non-finite value) is left alone.
inline bool straddlesCamera(const Vertex* v, int n) {
    bool front = false, back = false;
    for (int i = 0; i < n; ++i) {
        const float q = v[i].q;
        if (!std::isfinite(q) || !std::isfinite(v[i].x) || !std::isfinite(v[i].y) || !std::isfinite(v[i].z))
            return false;
        if (q > 0.0f)
            front = true;
        else if (q < 0.0f)
            back = true;
        else
            return false;
    }
    return front && back;
}

// True when the polygon is outside the GS coordinate window (|x - centre| or |y - centre| beyond Params::trigger on
// some vertex): the GIF XYZ2 fields are 16 bit, so the hardware wraps such vertices and draws garbage, which is the
// other way a ribbon with its tip close to the camera becomes a starburst.
inline bool outsideWindow(const Vertex* v, int n, const Params& p) {
    for (int i = 0; i < n; ++i) {
        if (!std::isfinite(v[i].x) || !std::isfinite(v[i].y) || !std::isfinite(v[i].q) || !std::isfinite(v[i].z) ||
            v[i].q <= 0.0f)
            return false; // non finite / behind / q == 0: not handled here (straddlesCamera covers the behind case)
        if (std::fabs(v[i].x - p.centreX) > p.trigger || std::fabs(v[i].y - p.centreY) > p.trigger)
            return true;
    }
    return false;
}

// The polygons clipPolygon should be applied to.
inline bool needsClip(const Vertex* v, int n, const Params& p) {
    return straddlesCamera(v, n) || outsideWindow(v, n, p);
}

namespace detail {
struct HV { // homogeneous vertex: the attributes that are linear in clip space
    float W, X, Y, Z, fog, s, t, c[4];
    int src; // index of the input vertex when unchanged, else -1
};
inline HV lift(const Vertex& v, int idx, const Params& p) {
    HV h;
    h.W = 1.0f / v.q;
    h.X = (v.x - p.centreX) * h.W;
    h.Y = (v.y - p.centreY) * h.W;
    h.Z = v.z * h.W;
    h.fog = v.w;
    h.s = v.s;
    h.t = v.t;
    for (int k = 0; k < 4; ++k)
        h.c[k] = static_cast<float>((v.rgba >> (8 * k)) & 0xff);
    h.src = idx;
    return h;
}
inline HV lerp(const HV& a, const HV& b, float u) {
    HV r;
    r.W = a.W + (b.W - a.W) * u;
    r.X = a.X + (b.X - a.X) * u;
    r.Y = a.Y + (b.Y - a.Y) * u;
    r.Z = a.Z + (b.Z - a.Z) * u;
    r.fog = a.fog + (b.fog - a.fog) * u;
    r.s = a.s + (b.s - a.s) * u;
    r.t = a.t + (b.t - a.t) * u;
    for (int k = 0; k < 4; ++k)
        r.c[k] = a.c[k] + (b.c[k] - a.c[k]) * u;
    r.src = -1;
    return r;
}
inline float dist(const HV& h, int plane, const Params& p) {
    switch (plane) {
        case 0:
            return h.W - p.nearW;
        case 1:
            return p.guard * h.W - h.X;
        case 2:
            return p.guard * h.W + h.X;
        case 3:
            return p.guard * h.W - h.Y;
        default:
            return p.guard * h.W + h.Y;
    }
}
inline Vertex lower(const HV& h, const Vertex* in, const Params& p) {
    if (h.src >= 0)
        return in[h.src];
    Vertex o;
    const float q = 1.0f / h.W;
    o.q = q;
    o.x = p.centreX + h.X * q;
    o.y = p.centreY + h.Y * q;
    o.z = h.Z * q;
    o.w = h.fog;
    o.s = h.s;
    o.t = h.t;
    uint32_t col = 0;
    for (int k = 0; k < 4; ++k) {
        float c = h.c[k] + 0.5f;
        c = c < 0.0f ? 0.0f : (c > 255.0f ? 255.0f : c);
        col |= static_cast<uint32_t>(c) << (8 * k);
    }
    o.rgba = col;
    return o;
}
} // namespace detail

// Clips the convex polygon in[0..n) (n <= 8) against the near plane and the guard band. Returns the number of output
// vertices (0 when nothing remains in front of the camera); out must hold kMaxClipped vertices. Vertices that survive
// unchanged are copied bit for bit.
inline int clipPolygon(const Vertex* in, int n, const Params& p, Vertex* out) {
    using namespace detail;
    HV buf[2][kMaxClipped + 8];
    int cnt = n;
    for (int i = 0; i < n; ++i)
        buf[0][i] = lift(in[i], i, p);
    int cur = 0;
    for (int plane = 0; plane < 5; ++plane) {
        const HV* src = buf[cur];
        HV* dst = buf[cur ^ 1];
        int m = 0;
        for (int i = 0; i < cnt && m < kMaxClipped + 6; ++i) {
            const HV& a = src[i];
            const HV& b = src[(i + 1) % cnt];
            const float da = dist(a, plane, p), db = dist(b, plane, p);
            if (da >= 0.0f)
                dst[m++] = a;
            if ((da >= 0.0f) != (db >= 0.0f)) {
                const float u = da / (da - db);
                dst[m++] = lerp(a, b, u);
            }
        }
        cur ^= 1;
        cnt = m;
        if (cnt < 3)
            return 0;
    }
    if (cnt > kMaxClipped)
        cnt = kMaxClipped;
    for (int i = 0; i < cnt; ++i)
        out[i] = lower(buf[cur][i], in, p);
    return cnt;
}

} // namespace world::polyclip
