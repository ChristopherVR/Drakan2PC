// Texture upscaling / replacement core. See gs_texup.h.
#include "gs_texup.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <unordered_set>

namespace gsr::texup {

// =====================================================================================================================
// hash
template <bool RgbOnly>
static uint64_t hashPixels(const uint32_t* px, uint32_t w, uint32_t h) {
    uint64_t hh = 0x9E3779B97F4A7C15ull ^ (((uint64_t)w << 32) | h);
    const size_t n = (size_t)w * h;
    size_t i = 0;
    for (; i + 2 <= n; i += 2) {
        uint64_t v;
        std::memcpy(&v, px + i, 8);
        if constexpr (RgbOnly)
            v &= 0x00FFFFFF00FFFFFFull;
        hh = (hh ^ v) * 0xFF51AFD7ED558CCDull;
        hh ^= hh >> 32;
    }
    if (i < n) {
        const uint32_t v = RgbOnly ? px[i] & 0x00FFFFFFu : px[i];
        hh = (hh ^ v) * 0xFF51AFD7ED558CCDull;
        hh ^= hh >> 32;
    }
    hh ^= hh >> 33;
    hh *= 0xC4CEB9FE1A85EC53ull;
    hh ^= hh >> 33;
    return hh;
}

uint64_t hashContent(const uint32_t* px, uint32_t w, uint32_t h) {
    return hashPixels<false>(px, w, h);
}

uint64_t hashContentRgb(const uint32_t* px, uint32_t w, uint32_t h) {
    return hashPixels<true>(px, w, h);
}

std::string hashName(uint64_t h) {
    char b[24];
    std::snprintf(b, sizeof(b), "%016llx", (unsigned long long)h);
    return b;
}

// =====================================================================================================================
// analysis / classification
Stats analyse(const uint32_t* px, uint32_t w, uint32_t h) {
    Stats s;
    std::unordered_set<uint32_t> colors;
    uint64_t pairs = 0, equal = 0, partial = 0;
    uint32_t maxA = 0;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const uint32_t c = px[(size_t)y * w + x];
            if (colors.size() < 4096)
                colors.insert(c);
            const uint32_t a = c >> 24;
            maxA = std::max(maxA, a);
            if (a > 0 && a < 0x80)
                ++partial;
            if (x + 1 < w) {
                ++pairs;
                equal += (px[(size_t)y * w + x + 1] == c);
            }
            if (y + 1 < h) {
                ++pairs;
                equal += (px[(size_t)(y + 1) * w + x] == c);
            }
        }
    s.uniqueColors = (uint32_t)colors.size();
    s.equalNeighbourFrac = pairs ? (float)equal / (float)pairs : 1.0f;
    s.alphaPartialFrac = (w && h) ? (float)partial / (float)((size_t)w * h) : 0.0f;
    s.maxAlpha = maxA;
    return s;
}

bool looksUninitialized(const uint32_t* px, uint32_t w, uint32_t h) {
    if (!px || w < 8 || h < 8)
        return false;
    const uint32_t need = std::max(4u, h / 4);
    uint32_t noisy = 0;
    for (uint32_t y = 0; y < h; ++y) {
        const uint32_t* row = px + (size_t)y * w;
        uint64_t diff = 0;
        for (uint32_t x = 0; x + 1 < w; ++x)
            for (int c = 0; c < 24; c += 8)
                diff += (uint32_t)std::abs((int)((row[x] >> c) & 255) - (int)((row[x + 1] >> c) & 255));
        if (diff > (uint64_t)60 * 3 * (w - 1) && ++noisy >= need)
            return true;
        if (noisy + (h - 1 - y) < need)
            return false;
    }
    return false;
}

Algo classify(const Stats& s) {
    // pixel art / UI / cartoon: big flat areas or very few colours. Photographic / noisy terrain: almost no identical
    // neighbours.
    if (s.uniqueColors <= 24 || s.equalNeighbourFrac >= 0.50f)
        return Algo::XBrz;
    return Algo::Smooth;
}

// =====================================================================================================================
// xBRZ
namespace {
inline uint32_t cA(uint32_t c) {
    return c >> 24;
}
inline uint32_t cR(uint32_t c) {
    return (c >> 16) & 255;
}
inline uint32_t cG(uint32_t c) {
    return (c >> 8) & 255;
}
inline uint32_t cB(uint32_t c) {
    return c & 255;
}

constexpr double kEqualTol = 30.0;
constexpr double kLumaW = 1.0;
constexpr double kDominantThr = 3.6;
constexpr double kSteepThr = 2.2;
constexpr double kCenterBias = 4.0;

inline double distYCbCr(uint32_t p1, uint32_t p2) {
    const double rd = (double)cR(p1) - (double)cR(p2), gd = (double)cG(p1) - (double)cG(p2),
                 bd = (double)cB(p1) - (double)cB(p2);
    constexpr double kb = 0.0593, kr = 0.2627, kg = 1.0 - kb - kr, sb = 0.5 / (1.0 - kb), sr = 0.5 / (1.0 - kr);
    const double y = kr * rd + kg * gd + kb * bd;
    const double cb = sb * (bd - y), cr = sr * (rd - y);
    return std::sqrt((kLumaW * y) * (kLumaW * y) + cb * cb + cr * cr);
}
inline double colorDist(uint32_t p1, uint32_t p2) {
    if (p1 == p2)
        return 0.0;
    const double a1 = cA(p1) / 255.0, a2 = cA(p2) / 255.0;
    const double d = distYCbCr(p1, p2);
    return a1 < a2 ? a1 * d + 255.0 * (a2 - a1) : a2 * d + 255.0 * (a1 - a2);
}

// back = back*(N-M)/N + front*M/N, alpha aware
inline uint32_t gradient(uint32_t front, uint32_t back, uint32_t M, uint32_t N) {
    const uint32_t wf = cA(front) * M, wb = cA(back) * (N - M), ws = wf + wb;
    if (ws == 0)
        return 0;
    auto ch = [&](uint32_t f, uint32_t b) { return (f * wf + b * wb) / ws; };
    const uint32_t a = ws / N;
    return (a << 24) | (ch(cR(front), cR(back)) << 16) | (ch(cG(front), cG(back)) << 8) | ch(cB(front), cB(back));
}

enum Blend : uint8_t { BL_NONE = 0, BL_NORMAL = 1, BL_DOMINANT = 2 };
// blend info per source pixel: 2 bits each for corners TL(0) TR(2) BR(4) BL(6)
inline uint8_t getCorner(uint8_t info, int corner) {
    return (info >> (2 * corner)) & 3;
}
inline uint8_t setCorner(uint8_t info, int corner, uint8_t v) {
    return (uint8_t)(info | (v << (2 * corner)));
}
enum Corner { C_TL = 0, C_TR = 1, C_BR = 2, C_BL = 3 };

struct Padded {
    uint32_t pw, ph;
    std::vector<uint32_t> p;
    uint32_t at(int x, int y) const { return p[(size_t)y * pw + x]; }
};

Padded makePadded(const uint32_t* src, uint32_t w, uint32_t h, bool wrapX, bool wrapY) {
    Padded P;
    P.pw = w + 4;
    P.ph = h + 4;
    P.p.resize((size_t)P.pw * P.ph);
    for (uint32_t y = 0; y < P.ph; ++y) {
        int sy = (int)y - 2;
        sy = wrapY ? (((sy % (int)h) + (int)h) % (int)h) : std::clamp(sy, 0, (int)h - 1);
        for (uint32_t x = 0; x < P.pw; ++x) {
            int sx = (int)x - 2;
            sx = wrapX ? (((sx % (int)w) + (int)w) % (int)w) : std::clamp(sx, 0, (int)w - 1);
            P.p[(size_t)y * P.pw + x] = src[(size_t)sy * w + sx];
        }
    }
    return P;
}

// rotation tables for the 3x3 kernel (a b c / d e f / g h i)
const int kRot[4][9] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8}, {6, 3, 0, 7, 4, 1, 8, 5, 2}, {8, 7, 6, 5, 4, 3, 2, 1, 0}, {2, 5, 8, 1, 4, 7, 0, 3, 6}};

struct OutMatrix {
    uint32_t* base;
    size_t stride;
    int N, rot;
    uint32_t& ref(int I, int J) const {
        int x = I, y = J;
        switch (rot) {
            case 1:
                x = J;
                y = N - 1 - I;
                break;
            case 2:
                x = N - 1 - I;
                y = N - 1 - J;
                break;
            case 3:
                x = N - 1 - J;
                y = I;
                break;
            default:
                break;
        }
        return base[(size_t)y * stride + x];
    }
};

inline void grad(uint32_t& back, uint32_t col, uint32_t M, uint32_t N) {
    back = gradient(col, back, M, N);
}

void blendLineShallow(int N, uint32_t col, const OutMatrix& o) {
    if (N == 2) {
        grad(o.ref(1, 0), col, 1, 4);
        grad(o.ref(1, 1), col, 3, 4);
    } else {
        grad(o.ref(3, 0), col, 1, 4);
        grad(o.ref(2, 2), col, 1, 4);
        grad(o.ref(3, 1), col, 3, 4);
        grad(o.ref(2, 3), col, 3, 4);
        o.ref(3, 2) = col;
        o.ref(3, 3) = col;
    }
}
void blendLineSteep(int N, uint32_t col, const OutMatrix& o) {
    if (N == 2) {
        grad(o.ref(0, 1), col, 1, 4);
        grad(o.ref(1, 1), col, 3, 4);
    } else {
        grad(o.ref(0, 3), col, 1, 4);
        grad(o.ref(2, 2), col, 1, 4);
        grad(o.ref(1, 3), col, 3, 4);
        grad(o.ref(3, 2), col, 3, 4);
        o.ref(2, 3) = col;
        o.ref(3, 3) = col;
    }
}
void blendLineSteepAndShallow(int N, uint32_t col, const OutMatrix& o) {
    if (N == 2) {
        grad(o.ref(1, 0), col, 1, 4);
        grad(o.ref(0, 1), col, 1, 4);
        grad(o.ref(1, 1), col, 5, 6);
    } else {
        grad(o.ref(3, 1), col, 3, 4);
        grad(o.ref(1, 3), col, 3, 4);
        grad(o.ref(3, 0), col, 1, 4);
        grad(o.ref(0, 3), col, 1, 4);
        grad(o.ref(2, 2), col, 1, 3);
        o.ref(3, 3) = col;
        o.ref(3, 2) = col;
        o.ref(2, 3) = col;
    }
}
void blendLineDiagonal(int N, uint32_t col, const OutMatrix& o) {
    if (N == 2)
        grad(o.ref(1, 1), col, 1, 2);
    else {
        grad(o.ref(3, 2), col, 1, 2);
        grad(o.ref(2, 3), col, 1, 2);
        o.ref(3, 3) = col;
    }
}
void blendCorner(int N, uint32_t col, const OutMatrix& o) {
    if (N == 2)
        grad(o.ref(1, 1), col, 21, 100);
    else {
        grad(o.ref(3, 3), col, 68, 100);
        grad(o.ref(3, 2), col, 9, 100);
        grad(o.ref(2, 3), col, 9, 100);
    }
}

void xbrzPass(const uint32_t* src, uint32_t w, uint32_t h, int N, bool wrapX, bool wrapY, std::vector<uint32_t>& out) {
    const Padded P = makePadded(src, w, h, wrapX, wrapY);
    const uint32_t pw = P.pw;
    std::vector<uint8_t> blend((size_t)P.pw * P.ph, 0);

    // corner pre-processing: corner (cx,cy) between F=(cx,cy) G=(cx+1,cy) J=(cx,cy+1) K=(cx+1,cy+1)
    for (int cy = 1; cy <= (int)h + 1; ++cy)
        for (int cx = 1; cx <= (int)w + 1; ++cx) {
            const uint32_t b = P.at(cx, cy - 1), c = P.at(cx + 1, cy - 1), e = P.at(cx - 1, cy), f = P.at(cx, cy),
                           g = P.at(cx + 1, cy), hh = P.at(cx + 2, cy), i = P.at(cx - 1, cy + 1), j = P.at(cx, cy + 1),
                           k = P.at(cx + 1, cy + 1), l = P.at(cx + 2, cy + 1), n = P.at(cx, cy + 2),
                           o = P.at(cx + 1, cy + 2);
            if ((f == g && j == k) || (f == j && g == k))
                continue;
            const double jg =
                colorDist(i, f) + colorDist(f, c) + colorDist(n, k) + colorDist(k, hh) + kCenterBias * colorDist(j, g);
            const double fk =
                colorDist(e, j) + colorDist(j, o) + colorDist(b, g) + colorDist(g, l) + kCenterBias * colorDist(f, k);
            uint8_t& bF = blend[(size_t)cy * pw + cx];
            uint8_t& bG = blend[(size_t)cy * pw + cx + 1];
            uint8_t& bJ = blend[(size_t)(cy + 1) * pw + cx];
            uint8_t& bK = blend[(size_t)(cy + 1) * pw + cx + 1];
            if (jg < fk) {
                const uint8_t t = (kDominantThr * jg < fk) ? BL_DOMINANT : BL_NORMAL;
                if (f != g && f != j)
                    bF = setCorner(bF, C_BR, t);
                if (k != j && k != g)
                    bK = setCorner(bK, C_TL, t);
            } else if (fk < jg) {
                const uint8_t t = (kDominantThr * fk < jg) ? BL_DOMINANT : BL_NORMAL;
                if (j != f && j != k)
                    bJ = setCorner(bJ, C_TR, t);
                if (g != f && g != k)
                    bG = setCorner(bG, C_BL, t);
            }
        }

    const size_t ow = (size_t)w * N;
    out.assign(ow * h * N, 0);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const int px = (int)x + 2, py = (int)y + 2;
            uint32_t* blk = &out[(size_t)y * N * ow + (size_t)x * N];
            const uint32_t e = P.at(px, py);
            for (int j = 0; j < N; ++j)
                for (int i = 0; i < N; ++i)
                    blk[(size_t)j * ow + i] = e;
            const uint8_t info = blend[(size_t)py * pw + px];
            if (!info)
                continue;
            const uint32_t ker[9] = {
                P.at(px - 1, py - 1), P.at(px, py - 1),     P.at(px + 1, py - 1), P.at(px - 1, py),    e,
                P.at(px + 1, py),     P.at(px - 1, py + 1), P.at(px, py + 1),     P.at(px + 1, py + 1)};
            for (int rot = 0; rot < 4; ++rot) {
                const int* r = kRot[rot];
                const uint32_t a = ker[r[0]], b = ker[r[1]], c = ker[r[2]], d = ker[r[3]], ee = ker[r[4]],
                               f = ker[r[5]], g = ker[r[6]], hh = ker[r[7]], ii = ker[r[8]];
                (void)a;
                uint8_t bl[4];
                for (int cc = 0; cc < 4; ++cc)
                    bl[cc] = getCorner(info, (cc - rot) & 3);
                if (bl[C_BR] < BL_NORMAL)
                    continue;
                auto eq = [&](uint32_t c1, uint32_t c2) { return colorDist(c1, c2) < kEqualTol; };
                bool doLine;
                if (bl[C_BR] >= BL_DOMINANT)
                    doLine = true;
                else if (bl[C_TR] != BL_NONE && !eq(ee, g))
                    doLine = false;
                else if (bl[C_BL] != BL_NONE && !eq(ee, c))
                    doLine = false;
                else if (!eq(ee, ii) && eq(g, hh) && eq(hh, ii) && eq(ii, f) && eq(f, c))
                    doLine = false;
                else
                    doLine = true;
                const uint32_t pxc = colorDist(ee, f) <= colorDist(ee, hh) ? f : hh;
                const OutMatrix om{blk, ow, N, rot};
                if (doLine) {
                    const double fg = colorDist(f, g), hc = colorDist(hh, c);
                    const bool shallow = kSteepThr * fg <= hc && ee != g && d != g;
                    const bool steep = kSteepThr * hc <= fg && ee != c && b != c;
                    if (shallow) {
                        if (steep)
                            blendLineSteepAndShallow(N, pxc, om);
                        else
                            blendLineShallow(N, pxc, om);
                    } else {
                        if (steep)
                            blendLineSteep(N, pxc, om);
                        else
                            blendLineDiagonal(N, pxc, om);
                    }
                } else
                    blendCorner(N, pxc, om);
            }
        }
}

// GS RGBA (R low, alpha 0..0x80) <-> ARGB with alpha 0..255
inline uint32_t toArgb(uint32_t c, bool scaleAlpha) {
    uint32_t a = c >> 24;
    if (scaleAlpha)
        a = std::min(255u, (a * 255u + 64u) / 128u);
    return (a << 24) | ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF);
}
inline uint32_t fromArgb(uint32_t c, bool scaleAlpha) {
    uint32_t a = c >> 24;
    if (scaleAlpha)
        a = std::min(0x80u, (a * 128u + 127u) / 255u);
    return (a << 24) | ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF);
}

// ---------------------------------------------------------------------------------------------------------------------
// smooth: Catmull-Rom bicubic on alpha-premultiplied colour + mild unsharp mask
inline float catmull(float t) {
    t = std::fabs(t);
    if (t < 1.0f)
        return 1.5f * t * t * t - 2.5f * t * t + 1.0f;
    if (t < 2.0f)
        return -0.5f * t * t * t + 2.5f * t * t - 4.0f * t + 2.0f;
    return 0.0f;
}

void smoothUpscale(const uint32_t* src, uint32_t w, uint32_t h, uint32_t N, bool wrapX, bool wrapY,
                   std::vector<uint32_t>& out) {
    const uint32_t ow = w * N, oh = h * N;
    // premultiplied float planes: r g b a (a in 0..1 of opaque 0x80 -> keep raw 0..255 scale for colour)
    uint32_t maxA = 0;
    for (size_t i = 0; i < (size_t)w * h; ++i)
        maxA = std::max(maxA, src[i] >> 24);
    const float aMax = (float)std::max(1u, maxA);
    std::vector<float> pm((size_t)w * h * 4);
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        const uint32_t c = src[i];
        // GS textures can carry useful RGB with every alpha byte zero (CT24,
        // TEXA=0, or TCC disabled). Premultiplying those by zero destroys their
        // colour even though the pixel shader may ignore texture alpha.
        const float a = maxA ? (float)(c >> 24) / aMax : 1.0f;
        pm[i * 4 + 0] = (float)(c & 255) * a;
        pm[i * 4 + 1] = (float)((c >> 8) & 255) * a;
        pm[i * 4 + 2] = (float)((c >> 16) & 255) * a;
        pm[i * 4 + 3] = a;
    }
    auto wrapIdx = [](int i, int n, bool wrap) { return wrap ? (((i % n) + n) % n) : std::clamp(i, 0, n - 1); };
    // horizontal pass: h rows x ow
    std::vector<float> tmp((size_t)ow * h * 4);
    for (uint32_t ox = 0; ox < ow; ++ox) {
        const float sx = ((float)ox + 0.5f) / (float)N - 0.5f;
        const int i0 = (int)std::floor(sx);
        const float f = sx - (float)i0;
        float wt[4];
        float ws = 0;
        for (int k = 0; k < 4; ++k) {
            wt[k] = catmull(f - (float)(k - 1));
            ws += wt[k];
        }
        for (int k = 0; k < 4; ++k)
            wt[k] /= ws;
        int xi[4];
        for (int k = 0; k < 4; ++k)
            xi[k] = wrapIdx(i0 + k - 1, (int)w, wrapX);
        for (uint32_t y = 0; y < h; ++y)
            for (int c = 0; c < 4; ++c) {
                float acc = 0;
                for (int k = 0; k < 4; ++k)
                    acc += wt[k] * pm[((size_t)y * w + xi[k]) * 4 + c];
                tmp[((size_t)y * ow + ox) * 4 + c] = acc;
            }
    }
    std::vector<float> res((size_t)ow * oh * 4);
    for (uint32_t oy = 0; oy < oh; ++oy) {
        const float sy = ((float)oy + 0.5f) / (float)N - 0.5f;
        const int j0 = (int)std::floor(sy);
        const float f = sy - (float)j0;
        float wt[4];
        float ws = 0;
        for (int k = 0; k < 4; ++k) {
            wt[k] = catmull(f - (float)(k - 1));
            ws += wt[k];
        }
        for (int k = 0; k < 4; ++k)
            wt[k] /= ws;
        int yi[4];
        for (int k = 0; k < 4; ++k)
            yi[k] = wrapIdx(j0 + k - 1, (int)h, wrapY);
        for (uint32_t ox = 0; ox < ow; ++ox)
            for (int c = 0; c < 4; ++c) {
                float acc = 0;
                for (int k = 0; k < 4; ++k)
                    acc += wt[k] * tmp[((size_t)yi[k] * ow + ox) * 4 + c];
                res[((size_t)oy * ow + ox) * 4 + c] = acc;
            }
    }
    // un-premultiply, unsharp mask on the colour (3x3 binomial blur at the output resolution)
    std::vector<float> col((size_t)ow * oh * 3), al((size_t)ow * oh);
    for (size_t i = 0; i < (size_t)ow * oh; ++i) {
        const float a = std::clamp(res[i * 4 + 3], 0.0f, 1.0f);
        al[i] = a;
        const float inv = a > 1e-4f ? 1.0f / a : 0.0f;
        for (int c = 0; c < 3; ++c)
            col[i * 3 + c] = std::clamp(res[i * 4 + c] * inv, 0.0f, 255.0f);
    }
    const float amount = 0.45f;
    out.resize((size_t)ow * oh);
    auto cidx = [&](int x, int y) {
        x = wrapIdx(x, (int)ow, wrapX);
        y = wrapIdx(y, (int)oh, wrapY);
        return (size_t)y * ow + x;
    };
    for (int y = 0; y < (int)oh; ++y)
        for (int x = 0; x < (int)ow; ++x) {
            const size_t i = (size_t)y * ow + x;
            uint32_t o = 0;
            for (int c = 0; c < 3; ++c) {
                float blur = 0;
                static const float k3[3] = {0.25f, 0.5f, 0.25f};
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        blur += k3[dy + 1] * k3[dx + 1] * col[cidx(x + dx, y + dy) * 3 + c];
                float v = col[i * 3 + c] + amount * (col[i * 3 + c] - blur);
                v = std::clamp(v, 0.0f, 255.0f);
                o |= (uint32_t)(v + 0.5f) << (8 * c);
            }
            const uint32_t a = (uint32_t)std::clamp(al[i] * aMax + 0.5f, 0.0f, (float)maxA);
            out[i] = o | (a << 24);
        }
}
} // namespace

void upscale(const uint32_t* src, uint32_t w, uint32_t h, uint32_t factor, Algo algo, bool wrapX, bool wrapY,
             std::vector<uint32_t>& out) {
    uint32_t N = 1;
    while (N * 2 <= factor && N < 8)
        N *= 2;
    if (N == 1 || w == 0 || h == 0) {
        out.assign(src, src + (size_t)w * h);
        return;
    }
    if (algo == Algo::Auto)
        algo = classify(analyse(src, w, h));
    if (algo == Algo::Smooth) {
        smoothUpscale(src, w, h, N, wrapX, wrapY, out);
        return;
    }
    // xBRZ on ARGB (alpha rescaled to 0..255 when the texture uses the GS 0..0x80 range)
    uint32_t maxA = 0;
    for (size_t i = 0; i < (size_t)w * h; ++i)
        maxA = std::max(maxA, src[i] >> 24);
    const bool scaleA = maxA <= 0x80;
    std::vector<uint32_t> a((size_t)w * h), b;
    for (size_t i = 0; i < a.size(); ++i)
        a[i] = toArgb(src[i], scaleA);
    uint32_t cw = w, ch = h, cur = 1;
    while (cur < N) {
        const int step = (N / cur >= 4) ? 4 : 2;
        xbrzPass(a.data(), cw, ch, step, wrapX, wrapY, b);
        a.swap(b);
        cw *= step;
        ch *= step;
        cur *= step;
    }
    out.resize(a.size());
    for (size_t i = 0; i < a.size(); ++i)
        out[i] = fromArgb(a[i], scaleA);
}

void downsample2x(const uint32_t* src, uint32_t w, uint32_t h, std::vector<uint32_t>& out) {
    const uint32_t nw = std::max(1u, w / 2), nh = std::max(1u, h / 2);
    out.resize((size_t)nw * nh);
    for (uint32_t y = 0; y < nh; ++y)
        for (uint32_t x = 0; x < nw; ++x) {
            uint32_t sa = 0, sr = 0, sg = 0, sb = 0, n = 0, wsum = 0;
            for (uint32_t dy = 0; dy < 2; ++dy)
                for (uint32_t dx = 0; dx < 2; ++dx) {
                    const uint32_t sx = std::min(w - 1, x * 2 + dx), sy = std::min(h - 1, y * 2 + dy);
                    const uint32_t c = src[(size_t)sy * w + sx];
                    const uint32_t a = c >> 24;
                    sa += a;
                    sr += (c & 255) * a;
                    sg += ((c >> 8) & 255) * a;
                    sb += ((c >> 16) & 255) * a;
                    wsum += a;
                    ++n;
                }
            uint32_t r, g, b;
            if (wsum) {
                r = (sr + wsum / 2) / wsum;
                g = (sg + wsum / 2) / wsum;
                b = (sb + wsum / 2) / wsum;
            } else {
                // fully transparent: plain average
                uint32_t pr = 0, pg = 0, pb = 0;
                for (uint32_t dy = 0; dy < 2; ++dy)
                    for (uint32_t dx = 0; dx < 2; ++dx) {
                        const uint32_t c = src[(size_t)std::min(h - 1, y * 2 + dy) * w + std::min(w - 1, x * 2 + dx)];
                        pr += c & 255;
                        pg += (c >> 8) & 255;
                        pb += (c >> 16) & 255;
                    }
                r = pr / 4;
                g = pg / 4;
                b = pb / 4;
            }
            out[(size_t)y * nw + x] = r | (g << 8) | (b << 16) | (((sa + n / 2) / n) << 24);
        }
}

// =====================================================================================================================
// PNG
namespace {
uint32_t g_crc[256];
bool g_crcInit = false;
uint32_t crc32u(uint32_t crc, const uint8_t* p, size_t n) {
    if (!g_crcInit) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            g_crc[i] = c;
        }
        g_crcInit = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i)
        crc = g_crc[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}
void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((uint8_t)(x >> 24));
    v.push_back((uint8_t)(x >> 16));
    v.push_back((uint8_t)(x >> 8));
    v.push_back((uint8_t)x);
}
void putChunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    put32(out, (uint32_t)data.size());
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    put32(out, crc32u(0, td.data(), td.size()));
}

// ---- inflate (RFC 1951), after Mark Adler's puff ----
struct BitIn {
    const uint8_t* d;
    size_t n, pos = 0;
    uint32_t bitbuf = 0;
    int bitcnt = 0;
    bool err = false;
    int bits(int need) {
        uint32_t v = bitbuf;
        while (bitcnt < need) {
            if (pos >= n) {
                err = true;
                return 0;
            }
            v |= (uint32_t)d[pos++] << bitcnt;
            bitcnt += 8;
        }
        bitbuf = v >> need;
        bitcnt -= need;
        return (int)(v & ((1u << need) - 1));
    }
};
struct Huff {
    short count[16];
    short symbol[288];
};
int decodeSym(BitIn& s, const Huff& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; ++len) {
        code |= s.bits(1);
        if (s.err)
            return -1;
        const int count = h.count[len];
        if (code - count < first)
            return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}
int construct(Huff& h, const short* length, int n) {
    for (int l = 0; l <= 15; ++l)
        h.count[l] = 0;
    for (int s = 0; s < n; ++s)
        h.count[length[s]]++;
    if (h.count[0] == n)
        return 0;
    int left = 1;
    for (int l = 1; l <= 15; ++l) {
        left <<= 1;
        left -= h.count[l];
        if (left < 0)
            return left;
    }
    short offs[16];
    offs[1] = 0;
    for (int l = 1; l < 15; ++l)
        offs[l + 1] = offs[l] + h.count[l];
    for (int s = 0; s < n; ++s)
        if (length[s] != 0)
            h.symbol[offs[length[s]]++] = (short)s;
    return left;
}
bool inflateCodes(BitIn& s, std::vector<uint8_t>& out, const Huff& lc, const Huff& dc) {
    static const short lens[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                   31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                   2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const short dists[30] = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                    33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                   6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;) {
        int sym = decodeSym(s, lc);
        if (sym < 0)
            return false;
        if (sym < 256)
            out.push_back((uint8_t)sym);
        else if (sym == 256)
            return true;
        else {
            sym -= 257;
            if (sym >= 29)
                return false;
            const int len = lens[sym] + s.bits(lext[sym]);
            const int ds = decodeSym(s, dc);
            if (ds < 0 || ds >= 30)
                return false;
            const size_t dist = (size_t)dists[ds] + (size_t)s.bits(dext[ds]);
            if (s.err || dist > out.size())
                return false;
            size_t from = out.size() - dist;
            for (int i = 0; i < len; ++i)
                out.push_back(out[from + i]);
        }
    }
}
bool inflateRaw(const uint8_t* d, size_t n, std::vector<uint8_t>& out) {
    BitIn s{d, n};
    int last;
    static Huff fixedL, fixedD;
    static bool fixedInit = false;
    if (!fixedInit) {
        short l[288];
        int i = 0;
        for (; i < 144; ++i)
            l[i] = 8;
        for (; i < 256; ++i)
            l[i] = 9;
        for (; i < 280; ++i)
            l[i] = 7;
        for (; i < 288; ++i)
            l[i] = 8;
        construct(fixedL, l, 288);
        for (i = 0; i < 30; ++i)
            l[i] = 5;
        construct(fixedD, l, 30);
        fixedInit = true;
    }
    do {
        last = s.bits(1);
        const int type = s.bits(2);
        if (s.err)
            return false;
        if (type == 0) {
            s.bitbuf = 0;
            s.bitcnt = 0;
            if (s.pos + 4 > n)
                return false;
            const uint32_t len = d[s.pos] | (d[s.pos + 1] << 8);
            s.pos += 4;
            if (s.pos + len > n)
                return false;
            out.insert(out.end(), d + s.pos, d + s.pos + len);
            s.pos += len;
        } else if (type == 1) {
            if (!inflateCodes(s, out, fixedL, fixedD))
                return false;
        } else if (type == 2) {
            static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            const int nlen = s.bits(5) + 257, ndist = s.bits(5) + 1, ncode = s.bits(4) + 4;
            if (s.err || nlen > 286 || ndist > 30)
                return false;
            short lengths[320] = {};
            for (int i = 0; i < ncode; ++i)
                lengths[order[i]] = (short)s.bits(3);
            Huff lencode;
            if (construct(lencode, lengths, 19) != 0)
                return false;
            int idx = 0;
            while (idx < nlen + ndist) {
                int sym = decodeSym(s, lencode);
                if (sym < 0)
                    return false;
                if (sym < 16)
                    lengths[idx++] = (short)sym;
                else {
                    int len = 0, rep;
                    if (sym == 16) {
                        if (idx == 0)
                            return false;
                        len = lengths[idx - 1];
                        rep = 3 + s.bits(2);
                    } else if (sym == 17)
                        rep = 3 + s.bits(3);
                    else
                        rep = 11 + s.bits(7);
                    if (idx + rep > nlen + ndist)
                        return false;
                    while (rep--)
                        lengths[idx++] = (short)len;
                }
            }
            Huff lc, dc;
            construct(lc, lengths, nlen);
            construct(dc, lengths + nlen, ndist);
            if (!inflateCodes(s, out, lc, dc))
                return false;
        } else
            return false;
    } while (!last);
    return true;
}

inline uint8_t paeth(int a, int b, int c) {
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return (uint8_t)((pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c));
}

inline uint32_t gsAlphaFromFile(uint32_t a8) {
    return (a8 * 128u + 127u) / 255u;
}

bool readFileBytes(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n < 0) {
        std::fclose(f);
        return false;
    }
    out.resize((size_t)n);
    const bool ok = n == 0 || std::fread(out.data(), 1, (size_t)n, f) == (size_t)n;
    std::fclose(f);
    return ok;
}

bool readPng(const std::vector<uint8_t>& f, std::vector<uint32_t>& px, uint32_t& W, uint32_t& H) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (f.size() < 33 || std::memcmp(f.data(), sig, 8) != 0)
        return false;
    size_t p = 8;
    uint32_t bd = 0, ct = 0, interlace = 0;
    std::vector<uint8_t> idat, plte, trns;
    while (p + 12 <= f.size()) {
        const uint32_t len = (f[p] << 24) | (f[p + 1] << 16) | (f[p + 2] << 8) | f[p + 3];
        const char* type = (const char*)&f[p + 4];
        if (p + 12 + len > f.size())
            return false;
        const uint8_t* d = &f[p + 8];
        if (!std::memcmp(type, "IHDR", 4) && len >= 13) {
            W = (d[0] << 24) | (d[1] << 16) | (d[2] << 8) | d[3];
            H = (d[4] << 24) | (d[5] << 16) | (d[6] << 8) | d[7];
            bd = d[8];
            ct = d[9];
            interlace = d[12];
        } else if (!std::memcmp(type, "PLTE", 4))
            plte.assign(d, d + len);
        else if (!std::memcmp(type, "tRNS", 4))
            trns.assign(d, d + len);
        else if (!std::memcmp(type, "IDAT", 4))
            idat.insert(idat.end(), d, d + len);
        else if (!std::memcmp(type, "IEND", 4))
            break;
        p += 12 + len;
    }
    if (!W || !H || W > 16384 || H > 16384 || interlace != 0 || idat.size() < 6)
        return false;
    int chans;
    switch (ct) {
        case 0:
            chans = 1;
            break;
        case 2:
            chans = 3;
            break;
        case 3:
            chans = 1;
            break;
        case 4:
            chans = 2;
            break;
        case 6:
            chans = 4;
            break;
        default:
            return false;
    }
    if (bd != 1 && bd != 2 && bd != 4 && bd != 8 && bd != 16)
        return false;
    std::vector<uint8_t> raw;
    raw.reserve((size_t)W * H * 4 + H);
    if (!inflateRaw(idat.data() + 2, idat.size() - 2, raw))
        return false;
    const size_t bpp = std::max<size_t>(1, (chans * bd) / 8);
    const size_t stride = ((size_t)W * chans * bd + 7) / 8;
    if (raw.size() < (stride + 1) * H)
        return false;
    std::vector<uint8_t> img(stride * H);
    for (uint32_t y = 0; y < H; ++y) {
        const uint8_t ft = raw[y * (stride + 1)];
        const uint8_t* src = &raw[y * (stride + 1) + 1];
        uint8_t* dst = &img[y * stride];
        const uint8_t* prev = y ? &img[(y - 1) * stride] : nullptr;
        for (size_t i = 0; i < stride; ++i) {
            const int a = i >= bpp ? dst[i - bpp] : 0, b = prev ? prev[i] : 0,
                      c = (prev && i >= bpp) ? prev[i - bpp] : 0;
            int v = src[i];
            switch (ft) {
                case 1:
                    v += a;
                    break;
                case 2:
                    v += b;
                    break;
                case 3:
                    v += (a + b) / 2;
                    break;
                case 4:
                    v += paeth(a, b, c);
                    break;
                default:
                    break;
            }
            dst[i] = (uint8_t)v;
        }
    }
    px.resize((size_t)W * H);
    auto sample = [&](const uint8_t* row, uint32_t x, int ch) -> uint32_t // 0..255 scaled
    {
        if (bd == 8)
            return row[x * chans + ch];
        if (bd == 16)
            return row[(x * chans + ch) * 2];
        const uint32_t bitpos = (x * chans + ch) * bd;
        const uint32_t v = (row[bitpos / 8] >> (8 - bd - (bitpos % 8))) & ((1u << bd) - 1);
        return ct == 3 ? v : v * 255u / ((1u << bd) - 1);
    };
    for (uint32_t y = 0; y < H; ++y) {
        const uint8_t* row = &img[y * stride];
        for (uint32_t x = 0; x < W; ++x) {
            uint32_t r, g, b, a = 255;
            if (ct == 3) {
                const uint32_t i = sample(row, x, 0);
                r = i * 3 + 0 < plte.size() ? plte[i * 3] : 0;
                g = i * 3 + 1 < plte.size() ? plte[i * 3 + 1] : 0;
                b = i * 3 + 2 < plte.size() ? plte[i * 3 + 2] : 0;
                a = i < trns.size() ? trns[i] : 255;
            } else if (ct == 0) {
                r = g = b = sample(row, x, 0);
            } else if (ct == 4) {
                r = g = b = sample(row, x, 0);
                a = sample(row, x, 1);
            } else {
                r = sample(row, x, 0);
                g = sample(row, x, 1);
                b = sample(row, x, 2);
                if (ct == 6)
                    a = sample(row, x, 3);
            }
            px[(size_t)y * W + x] = r | (g << 8) | (b << 16) | (gsAlphaFromFile(a) << 24);
        }
    }
    return true;
}

// ---- DDS ----
void decodeBC1Block(const uint8_t* b, uint32_t out[16], bool alpha1) {
    const uint32_t c0 = b[0] | (b[1] << 8), c1 = b[2] | (b[3] << 8);
    auto expand = [](uint32_t c, uint32_t* rgb) {
        rgb[0] = ((c >> 11) & 31) * 255 / 31;
        rgb[1] = ((c >> 5) & 63) * 255 / 63;
        rgb[2] = (c & 31) * 255 / 31;
    };
    uint32_t p[4][4];
    expand(c0, p[0]);
    expand(c1, p[1]);
    p[0][3] = p[1][3] = 255;
    if (c0 > c1 || !alpha1) {
        for (int i = 0; i < 3; ++i) {
            p[2][i] = (2 * p[0][i] + p[1][i]) / 3;
            p[3][i] = (p[0][i] + 2 * p[1][i]) / 3;
        }
        p[2][3] = p[3][3] = 255;
    } else {
        for (int i = 0; i < 3; ++i) {
            p[2][i] = (p[0][i] + p[1][i]) / 2;
            p[3][i] = 0;
        }
        p[2][3] = 255;
        p[3][3] = 0;
    }
    const uint32_t bits = b[4] | (b[5] << 8) | (b[6] << 16) | ((uint32_t)b[7] << 24);
    for (int i = 0; i < 16; ++i) {
        const uint32_t* c = p[(bits >> (2 * i)) & 3];
        out[i] = c[0] | (c[1] << 8) | (c[2] << 16) | (c[3] << 24);
    }
}
bool readDds(const std::vector<uint8_t>& f, std::vector<uint32_t>& px, uint32_t& W, uint32_t& H) {
    if (f.size() < 128 || std::memcmp(f.data(), "DDS ", 4) != 0)
        return false;
    auto rd = [&](size_t o) {
        return (uint32_t)f[o] | ((uint32_t)f[o + 1] << 8) | ((uint32_t)f[o + 2] << 16) | ((uint32_t)f[o + 3] << 24);
    };
    H = rd(12);
    W = rd(16);
    if (!W || !H || W > 16384 || H > 16384)
        return false;
    const uint32_t pfFlags = rd(80), fourcc = rd(84), bitcount = rd(88), rm = rd(92), gm = rd(96), bm = rd(100),
                   am = rd(104);
    size_t off = 128;
    enum { RAW, BC1, BC2, BC3 } kind = RAW;
    bool bgra = false;
    uint32_t bpp = 32;
    if (pfFlags & 4) {
        if (fourcc == 0x31545844)
            kind = BC1; // DXT1
        else if (fourcc == 0x33545844)
            kind = BC2; // DXT3
        else if (fourcc == 0x35545844)
            kind = BC3;                // DXT5
        else if (fourcc == 0x30315844) // DX10
        {
            if (f.size() < 148)
                return false;
            const uint32_t fmt = rd(128);
            off = 148;
            if (fmt == 28) {
                kind = RAW;
                bgra = false;
            } else if (fmt == 87 || fmt == 91) {
                kind = RAW;
                bgra = true;
            } else if (fmt == 71 || fmt == 72)
                kind = BC1;
            else if (fmt == 74 || fmt == 75)
                kind = BC2;
            else if (fmt == 77 || fmt == 78)
                kind = BC3;
            else
                return false;
        } else
            return false;
    } else {
        bpp = bitcount;
        if (bpp != 32 && bpp != 24)
            return false;
        bgra = (rm == 0x00FF0000u && bm == 0x000000FFu);
        if (!bgra && !(rm == 0x000000FFu && bm == 0x00FF0000u))
            return false;
        (void)gm;
        (void)am;
    }
    px.assign((size_t)W * H, 0);
    if (kind == RAW) {
        const size_t bytes = bpp / 8;
        if (f.size() < off + (size_t)W * H * bytes)
            return false;
        for (size_t i = 0; i < (size_t)W * H; ++i) {
            const uint8_t* s = &f[off + i * bytes];
            const uint32_t r = bgra ? s[2] : s[0], g = s[1], b = bgra ? s[0] : s[2], a = bytes == 4 ? s[3] : 255;
            px[i] = r | (g << 8) | (b << 16) | (gsAlphaFromFile(a) << 24);
        }
        return true;
    }
    const uint32_t bw = (W + 3) / 4, bh = (H + 3) / 4, bsz = kind == BC1 ? 8 : 16;
    if (f.size() < off + (size_t)bw * bh * bsz)
        return false;
    for (uint32_t by = 0; by < bh; ++by)
        for (uint32_t bx = 0; bx < bw; ++bx) {
            const uint8_t* b = &f[off + ((size_t)by * bw + bx) * bsz];
            uint32_t blk[16];
            decodeBC1Block(kind == BC1 ? b : b + 8, blk, kind == BC1);
            if (kind == BC2)
                for (int i = 0; i < 16; ++i) {
                    const uint32_t a4 = (b[i / 2] >> (4 * (i & 1))) & 15;
                    blk[i] = (blk[i] & 0x00FFFFFFu) | ((a4 * 17u) << 24);
                }
            else if (kind == BC3) {
                uint32_t al[8];
                al[0] = b[0];
                al[1] = b[1];
                if (al[0] > al[1])
                    for (int i = 1; i < 7; ++i)
                        al[1 + i] = ((7 - i) * al[0] + i * al[1]) / 7;
                else {
                    for (int i = 1; i < 5; ++i)
                        al[1 + i] = ((5 - i) * al[0] + i * al[1]) / 5;
                    al[6] = 0;
                    al[7] = 255;
                }
                uint64_t bits = 0;
                for (int i = 0; i < 6; ++i)
                    bits |= (uint64_t)b[2 + i] << (8 * i);
                for (int i = 0; i < 16; ++i)
                    blk[i] = (blk[i] & 0x00FFFFFFu) | (al[(bits >> (3 * i)) & 7] << 24);
            }
            for (int i = 0; i < 16; ++i) {
                const uint32_t x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if (x < W && y < H) {
                    const uint32_t c = blk[i];
                    px[(size_t)y * W + x] = (c & 0x00FFFFFFu) | (gsAlphaFromFile(c >> 24) << 24);
                }
            }
        }
    return true;
}
} // namespace

// ---- deflate (RFC 1951) encoder: LZ77 with hash chains + one dynamic Huffman block; used for the PNG dumps (no zlib
// dependency) ----
namespace {
struct BitOut {
    std::vector<uint8_t>& out;
    uint64_t acc = 0;
    int n = 0;
    explicit BitOut(std::vector<uint8_t>& o) : out(o) {}
    void put(uint32_t v, int bits) // LSB first
    {
        acc |= (uint64_t)v << n;
        n += bits;
        while (n >= 8) {
            out.push_back((uint8_t)acc);
            acc >>= 8;
            n -= 8;
        }
    }
    void flush() {
        if (n > 0) {
            out.push_back((uint8_t)acc);
            acc = 0;
            n = 0;
        }
    }
};

uint32_t revBits(uint32_t v, int n) {
    uint32_t r = 0;
    for (int i = 0; i < n; ++i) {
        r = (r << 1) | (v & 1);
        v >>= 1;
    }
    return r;
}

// Huffman code lengths (limit bits) for `freq`; symbols with freq 0 get length 0.
void huffLengths(const std::vector<uint32_t>& freq, int limit, std::vector<uint8_t>& len) {
    const size_t n = freq.size();
    len.assign(n, 0);
    std::vector<uint64_t> f(freq.begin(), freq.end());
    for (int attempt = 0; attempt < 32; ++attempt) {
        struct Node {
            uint64_t w;
            int l, r;
        };
        std::vector<Node> nodes;
        std::vector<int> sym;                       // node index -> symbol (leaves)
        std::vector<std::pair<uint64_t, int>> heap; // (weight, node)
        for (size_t i = 0; i < n; ++i)
            if (f[i]) {
                nodes.push_back({f[i], -1, -1});
                sym.push_back((int)i);
                heap.push_back({f[i], (int)nodes.size() - 1});
            }
        if (nodes.empty())
            return;
        if (nodes.size() == 1) {
            len.assign(n, 0);
            len[sym[0]] = 1;
            return;
        }
        auto cmp = [](const std::pair<uint64_t, int>& a, const std::pair<uint64_t, int>& b) {
            return a.first > b.first || (a.first == b.first && a.second > b.second);
        };
        std::make_heap(heap.begin(), heap.end(), cmp);
        while (heap.size() > 1) {
            std::pop_heap(heap.begin(), heap.end(), cmp);
            auto a = heap.back();
            heap.pop_back();
            std::pop_heap(heap.begin(), heap.end(), cmp);
            auto b = heap.back();
            heap.pop_back();
            nodes.push_back({a.first + b.first, a.second, b.second});
            sym.push_back(-1);
            heap.push_back({a.first + b.first, (int)nodes.size() - 1});
            std::push_heap(heap.begin(), heap.end(), cmp);
        }
        // depths by walking down from the root
        std::vector<int> depth(nodes.size(), 0);
        int maxd = 0;
        for (int i = (int)nodes.size() - 1; i >= 0; --i) {
            if (nodes[i].l >= 0) {
                depth[nodes[i].l] = depth[i] + 1;
                depth[nodes[i].r] = depth[i] + 1;
            } else
                maxd = std::max(maxd, depth[i]);
        }
        if (maxd <= limit) {
            len.assign(n, 0);
            for (size_t i = 0; i < nodes.size(); ++i)
                if (sym[i] >= 0)
                    len[sym[i]] = (uint8_t)depth[i];
            return;
        }
        for (size_t i = 0; i < n; ++i)
            if (f[i])
                f[i] = (f[i] + 1) / 2; // flatten the distribution and retry
    }
}

void huffCodes(const std::vector<uint8_t>& len,
               std::vector<uint32_t>& code) // canonical, already bit reversed for LSB-first output
{
    std::vector<int> cnt(16, 0), next(16, 0);
    for (uint8_t l : len)
        if (l)
            cnt[l]++;
    int c = 0;
    for (int b = 1; b <= 15; ++b) {
        c = (c + cnt[b - 1]) << 1;
        next[b] = c;
    }
    code.assign(len.size(), 0);
    for (size_t i = 0; i < len.size(); ++i)
        if (len[i])
            code[i] = revBits((uint32_t)next[len[i]]++, len[i]);
}

const uint16_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                               31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

struct Tok {
    uint16_t lenOrLit;
    uint16_t dist;
}; // dist 0: literal

int lenSym(int l) {
    int s = 28;
    while (kLenBase[s] > l)
        --s;
    return s;
}
int distSym(int d) {
    int s = 29;
    while (kDistBase[s] > d)
        --s;
    return s;
}

// returns a complete zlib stream (header + one dynamic block + adler32)
void zlibCompress(const uint8_t* data, size_t n, std::vector<uint8_t>& z) {
    std::vector<Tok> toks;
    toks.reserve(n / 2 + 16);
    {
        constexpr int kHashBits = 15, kWin = 32768, kMaxChain = 48;
        std::vector<int32_t> head(1u << kHashBits, -1), prev(n ? n : 1, -1);
        auto hash3 = [&](size_t i) {
            return (uint32_t)(((data[i] << 10) ^ (data[i + 1] << 5) ^ data[i + 2]) * 2654435761u) >> (32 - kHashBits);
        };
        auto insert = [&](size_t i) {
            if (i + 2 < n) {
                const uint32_t h = hash3(i);
                prev[i] = head[h];
                head[h] = (int32_t)i;
            }
        };
        auto findMatch = [&](size_t i, int& bestLen, int& bestDist) {
            bestLen = 0;
            bestDist = 0;
            if (i + 2 >= n)
                return;
            int chain = kMaxChain;
            const int maxLen = (int)std::min<size_t>(258, n - i);
            for (int32_t j = head[hash3(i)]; j >= 0 && chain-- > 0; j = prev[j]) {
                const size_t d = i - (size_t)j;
                if (d > (size_t)kWin)
                    break;
                if (data[j + bestLen] != data[i + bestLen] && bestLen)
                    continue;
                int l = 0;
                while (l < maxLen && data[j + l] == data[i + l])
                    ++l;
                if (l > bestLen) {
                    bestLen = l;
                    bestDist = (int)d;
                    if (l == maxLen)
                        break;
                }
            }
        };
        size_t i = 0;
        while (i < n) {
            int l, d;
            findMatch(i, l, d);
            if (l >= 3) {
                // lazy: a longer match one byte later wins
                int l2 = 0, d2 = 0;
                insert(i);
                if (i + 1 < n && l < 32)
                    findMatch(i + 1, l2, d2);
                if (l2 > l + 1) {
                    toks.push_back({data[i], 0});
                    ++i;
                    continue;
                }
                toks.push_back({(uint16_t)l, (uint16_t)d});
                for (int k = 1; k < l; ++k)
                    insert(i + k);
                i += l;
            } else {
                insert(i);
                toks.push_back({data[i], 0});
                ++i;
            }
        }
    }
    std::vector<uint32_t> lf(286, 0), df(30, 0);
    for (const Tok& t : toks) {
        if (!t.dist)
            lf[t.lenOrLit]++;
        else {
            lf[257 + lenSym(t.lenOrLit)]++;
            df[distSym(t.dist)]++;
        }
    }
    lf[256] = 1;
    // a deflate block needs at least one distance code; two keep every decoder happy
    {
        int used = 0;
        for (uint32_t v : df)
            used += v != 0;
        for (size_t i = 0; i < df.size() && used < 2; ++i)
            if (!df[i]) {
                df[i] = 1;
                ++used;
            }
    }
    std::vector<uint8_t> ll, dl;
    huffLengths(lf, 15, ll);
    huffLengths(df, 15, dl);
    std::vector<uint32_t> lc, dc;
    huffCodes(ll, lc);
    huffCodes(dl, dc);
    int hlit = 286, hdist = 30;
    while (hlit > 257 && ll[hlit - 1] == 0)
        --hlit;
    while (hdist > 1 && dl[hdist - 1] == 0)
        --hdist;
    // code length alphabet: RLE of the concatenated lengths
    std::vector<uint8_t> all(ll.begin(), ll.begin() + hlit);
    all.insert(all.end(), dl.begin(), dl.begin() + hdist);
    struct Cl {
        uint8_t sym, extra;
    };
    std::vector<Cl> cl;
    for (size_t i = 0; i < all.size();) {
        size_t run = 1;
        while (i + run < all.size() && all[i + run] == all[i])
            ++run;
        if (all[i] == 0 && run >= 3) {
            size_t r = std::min<size_t>(run, 138);
            if (r >= 11)
                cl.push_back({18, (uint8_t)(r - 11)});
            else
                cl.push_back({17, (uint8_t)(r - 3)});
            i += r;
        } else if (all[i] != 0 && run >= 4) {
            cl.push_back({all[i], 0});
            size_t r = std::min<size_t>(run - 1, 6);
            cl.push_back({16, (uint8_t)(r - 3)});
            i += 1 + r;
        } else {
            cl.push_back({all[i], 0});
            ++i;
        }
    }
    std::vector<uint32_t> cf(19, 0);
    for (const Cl& c : cl)
        cf[c.sym]++;
    {
        int used = 0;
        for (uint32_t v : cf)
            used += v != 0;
        for (size_t i = 0; i < cf.size() && used < 2; ++i)
            if (!cf[i]) {
                cf[i] = 1;
                ++used;
            }
    } // zlib rejects an incomplete one-symbol code length code
    std::vector<uint8_t> cll;
    huffLengths(cf, 7, cll);
    std::vector<uint32_t> cc;
    huffCodes(cll, cc);
    static const int order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    int hclen = 19;
    while (hclen > 4 && cll[order[hclen - 1]] == 0)
        --hclen;

    z.clear();
    z.push_back(0x78);
    z.push_back(0xDA);
    BitOut bo(z);
    bo.put(1, 1); // BFINAL
    bo.put(2, 2); // dynamic Huffman
    bo.put((uint32_t)(hlit - 257), 5);
    bo.put((uint32_t)(hdist - 1), 5);
    bo.put((uint32_t)(hclen - 4), 4);
    for (int i = 0; i < hclen; ++i)
        bo.put(cll[order[i]], 3);
    for (const Cl& c : cl) {
        bo.put(cc[c.sym], cll[c.sym]);
        if (c.sym == 16)
            bo.put(c.extra, 2);
        else if (c.sym == 17)
            bo.put(c.extra, 3);
        else if (c.sym == 18)
            bo.put(c.extra, 7);
    }
    for (const Tok& t : toks) {
        if (!t.dist) {
            bo.put(lc[t.lenOrLit], ll[t.lenOrLit]);
            continue;
        }
        const int ls = lenSym(t.lenOrLit), ds = distSym(t.dist);
        bo.put(lc[257 + ls], ll[257 + ls]);
        if (kLenExtra[ls])
            bo.put((uint32_t)(t.lenOrLit - kLenBase[ls]), kLenExtra[ls]);
        bo.put(dc[ds], dl[ds]);
        if (kDistExtra[ds])
            bo.put((uint32_t)(t.dist - kDistBase[ds]), kDistExtra[ds]);
    }
    bo.put(lc[256], ll[256]);
    bo.flush();
    uint32_t s1 = 1, s2 = 0;
    for (size_t i = 0; i < n; ++i) {
        s1 = (s1 + data[i]) % 65521;
        s2 = (s2 + s1) % 65521;
    }
    const uint32_t ad = (s2 << 16) | s1;
    z.push_back((uint8_t)(ad >> 24));
    z.push_back((uint8_t)(ad >> 16));
    z.push_back((uint8_t)(ad >> 8));
    z.push_back((uint8_t)ad);
}
} // namespace

bool writePngRGBA(const std::string& path, const uint32_t* px, uint32_t w, uint32_t h) {
    // Pixels are written exactly as decoded: RGB under alpha 0 is preserved (CT24 / CT16 textures carry real colour
    // with TEXA alpha 0), the alpha channel maps GS units 0..0x80 to 0..255 (0x80 -> 255; values above 0x80, which a
    // few palettes use, clamp to 255).
    const size_t stride = (size_t)w * 4;
    std::vector<uint8_t> img(stride * h);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const uint32_t c = px[(size_t)y * w + x];
            uint32_t a = c >> 24;
            a = a >= 0x80 ? 255u : (a * 255u + 64u) / 128u;
            uint8_t* d = &img[y * stride + (size_t)x * 4];
            d[0] = (uint8_t)(c & 255);
            d[1] = (uint8_t)((c >> 8) & 255);
            d[2] = (uint8_t)((c >> 16) & 255);
            d[3] = (uint8_t)a;
        }
    // per row filter choice (None / Sub / Up / Average / Paeth) by the minimum sum of absolute filtered values
    std::vector<uint8_t> raw((stride + 1) * h);
    std::vector<uint8_t> cand[5];
    for (auto& c : cand)
        c.resize(stride);
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* cur = &img[y * stride];
        const uint8_t* up = y ? &img[(y - 1) * stride] : nullptr;
        for (size_t i = 0; i < stride; ++i) {
            const int a = i >= 4 ? cur[i - 4] : 0, b = up ? up[i] : 0, c = (up && i >= 4) ? up[i - 4] : 0;
            cand[0][i] = cur[i];
            cand[1][i] = (uint8_t)(cur[i] - a);
            cand[2][i] = (uint8_t)(cur[i] - b);
            cand[3][i] = (uint8_t)(cur[i] - ((a + b) / 2));
            cand[4][i] = (uint8_t)(cur[i] - paeth(a, b, c));
        }
        int best = 0;
        uint64_t bestSum = ~0ull;
        for (int f = 0; f < 5; ++f) {
            uint64_t s = 0;
            for (size_t i = 0; i < stride; ++i) {
                const int v = (int8_t)cand[f][i];
                s += (uint64_t)(v < 0 ? -v : v);
            }
            if (s < bestSum) {
                bestSum = s;
                best = f;
            }
        }
        raw[y * (stride + 1)] = (uint8_t)best;
        std::memcpy(&raw[y * (stride + 1) + 1], cand[best].data(), stride);
    }
    std::vector<uint8_t> z;
    zlibCompress(raw.data(), raw.size(), z);
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    put32(ihdr, w);
    put32(ihdr, h);
    ihdr.push_back(8);
    ihdr.push_back(6);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    putChunk(out, "IHDR", ihdr);
    putChunk(out, "IDAT", z);
    putChunk(out, "IEND", {});
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

bool readImageBytes(const std::vector<uint8_t>& bytes, std::vector<uint32_t>& px, uint32_t& w, uint32_t& h) {
    if (readPng(bytes, px, w, h))
        return true;
    return readDds(bytes, px, w, h);
}

bool readImageFile(const std::string& path, std::vector<uint32_t>& px, uint32_t& w, uint32_t& h) {
    std::vector<uint8_t> f;
    if (!readFileBytes(path, f))
        return false;
    return readImageBytes(f, px, w, h);
}

bool fileExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}
bool makeDirs(const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return !ec;
}
std::vector<std::string> listDir(const std::string& dir) {
    std::vector<std::string> r;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec))
            r.push_back(it->path().filename().string());
    return r;
}

// =====================================================================================================================
Pool::Pool(unsigned threads) {
    for (unsigned i = 0; i < threads; ++i)
        m_threads.emplace_back([this] {
            for (;;) {
                std::function<void()> job;
                {
                    std::unique_lock<std::mutex> lk(m_mu);
                    m_cv.wait(lk, [&] { return m_stop || !m_q.empty() || !m_urgent.empty(); });
                    if (m_stop && m_q.empty() && m_urgent.empty())
                        return;
                    auto& lane = m_urgent.empty() ? m_q : m_urgent;
                    job = std::move(lane.front());
                    lane.pop_front();
                }
                job();
            }
        });
}
Pool::~Pool() {
    {
        std::lock_guard<std::mutex> lk(m_mu);
        m_stop = true;
    }
    m_cv.notify_all();
    for (auto& t : m_threads)
        t.join();
}
void Pool::submit(std::function<void()> job, bool urgent) {
    if (m_threads.empty()) {
        job();
        return;
    }
    {
        std::lock_guard<std::mutex> lk(m_mu);
        (urgent ? m_urgent : m_q).push_back(std::move(job));
    }
    m_cv.notify_one();
}

} // namespace gsr::texup
