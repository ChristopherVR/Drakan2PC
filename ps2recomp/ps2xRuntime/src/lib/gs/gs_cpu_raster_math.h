#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#if defined(__SSE4_1__) || defined(_M_X64) || defined(__EMSCRIPTEN__)
#include <smmintrin.h>
#endif

namespace gs_cpu {
inline uint32_t bilinearScalar(uint32_t c00, uint32_t c10, uint32_t c01, uint32_t c11, float fx, float fy) {
    uint32_t result = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        const float a = float((c00 >> shift) & 255), b = float((c10 >> shift) & 255);
        const float c = float((c01 >> shift) & 255), d = float((c11 >> shift) & 255);
        const float top = a + (b - a) * fx;
        const float bottom = c + (d - c) * fx;
        const auto value = std::clamp(std::lround(top + (bottom - top) * fy), 0l, 255l);
        result |= static_cast<uint32_t>(value) << shift;
    }
    return result;
}

inline uint32_t bilinear(uint32_t c00, uint32_t c10, uint32_t c01, uint32_t c11, float fx, float fy) {
#if defined(__SSE4_1__) || defined(_M_X64) || defined(__EMSCRIPTEN__)
    const auto unpack = [](uint32_t color) {
        return _mm_cvtepi32_ps(_mm_cvtepu8_epi32(_mm_cvtsi32_si128(static_cast<int>(color))));
    };
    const __m128 a = unpack(c00), b = unpack(c10), c = unpack(c01), d = unpack(c11);
    const __m128 x = _mm_set1_ps(fx), y = _mm_set1_ps(fy);
    const __m128 top = _mm_add_ps(a, _mm_mul_ps(_mm_sub_ps(b, a), x));
    const __m128 bottom = _mm_add_ps(c, _mm_mul_ps(_mm_sub_ps(d, c), x));
    const __m128 value = _mm_add_ps(top, _mm_mul_ps(_mm_sub_ps(bottom, top), y));
    // All interpolated channels are nonnegative. Match lround's half-away
    // rule using the fractional part: adding 0.5 first can misround values
    // just below a half when that addition itself rounds upward.
    const __m128i whole = _mm_cvttps_epi32(value);
    const __m128 fraction = _mm_sub_ps(value, _mm_cvtepi32_ps(whole));
    const __m128i up = _mm_castps_si128(_mm_cmpge_ps(fraction, _mm_set1_ps(0.5f)));
    const __m128i rounded = _mm_sub_epi32(whole, up);
    const __m128i words = _mm_packs_epi32(rounded, _mm_setzero_si128());
    return static_cast<uint32_t>(_mm_cvtsi128_si32(_mm_packus_epi16(words, _mm_setzero_si128())));
#else
    return bilinearScalar(c00, c10, c01, c11, fx, fy);
#endif
}
// Exact identities of (A - B) * C / 128 + D for byte-valued RGB.
inline bool blendIsSource(uint64_t alpha, uint8_t sourceAlpha, bool fixedDestinationAlpha) {
    const unsigned a = unsigned(alpha & 3), b = unsigned((alpha >> 2) & 3), d = unsigned((alpha >> 6) & 3);
    if (a == b && d == 0)
        return true;
    if (a != 0 || b != 1 || d != 1)
        return false;
    const unsigned c = unsigned((alpha >> 4) & 3);
    return c == 0 ? sourceAlpha == 128 : c == 1 ? fixedDestinationAlpha : ((alpha >> 32) & 255) == 128;
}
inline uint32_t modulateFog(uint32_t shade, uint32_t texture, bool textureAlpha, bool fogEnabled, uint8_t fog,
                            uint32_t fogColor) {
#if defined(__SSE4_1__) || defined(_M_X64) || defined(__EMSCRIPTEN__)
    const auto unpack = [](uint32_t v) { return _mm_cvtepu8_epi32(_mm_cvtsi32_si128(int(v))); };
    __m128i value =
        _mm_min_epi32(_mm_srli_epi32(_mm_mullo_epi32(unpack(shade), unpack(texture)), 7), _mm_set1_epi32(255));
    const uint32_t alpha = textureAlpha ? uint32_t(_mm_extract_epi32(value, 3)) << 24 : shade & 0xff000000u;
    if (fogEnabled) {
        value = _mm_add_epi32(_mm_srli_epi32(_mm_mullo_epi32(value, _mm_set1_epi32(fog)), 8),
                              _mm_srli_epi32(_mm_mullo_epi32(unpack(fogColor), _mm_set1_epi32(255 - fog)), 8));
    }
    const auto packed = _mm_packus_epi16(_mm_packs_epi32(value, _mm_setzero_si128()), _mm_setzero_si128());
    return (uint32_t(_mm_cvtsi128_si32(packed)) & 0x00ffffffu) | alpha;
#else
    uint32_t result = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        unsigned value = std::min(255u, (((shade >> shift) & 255) * ((texture >> shift) & 255)) >> 7);
        if (shift == 24 && !textureAlpha)
            value = shade >> 24;
        if (shift != 24 && fogEnabled)
            value = ((value * fog) >> 8) + ((((fogColor >> shift) & 255) * (255 - fog)) >> 8);
        result |= value << shift;
    }
    return result;
#endif
}
inline uint32_t blendSourceAlpha128(uint32_t source, uint32_t destination) {
    const unsigned alpha = source >> 24;
#if defined(__SSE4_1__) || defined(_M_X64) || defined(__EMSCRIPTEN__)
    const auto unpack = [](uint32_t v) { return _mm_cvtepu8_epi32(_mm_cvtsi32_si128(int(v))); };
    const auto src = unpack(source), dst = unpack(destination);
    const auto value =
        _mm_add_epi32(_mm_srai_epi32(_mm_mullo_epi32(_mm_sub_epi32(src, dst), _mm_set1_epi32(alpha)), 7), dst);
    const auto packed = _mm_packus_epi16(_mm_packs_epi32(value, _mm_setzero_si128()), _mm_setzero_si128());
    return (uint32_t(_mm_cvtsi128_si32(packed)) & 0x00ffffffu) | (source & 0xff000000u);
#else
    uint32_t result = source & 0xff000000u;
    for (unsigned shift = 0; shift < 24; shift += 8) {
        const int src = (source >> shift) & 255, dst = (destination >> shift) & 255;
        result |= uint32_t(std::clamp(((src - dst) * int(alpha) >> 7) + dst, 0, 255)) << shift;
    }
    return result;
#endif
}
// CRT composition uses truncation toward zero for division by 255 and
// normalizes output alpha. The factor is a byte, so |product| <= 65025.
inline uint32_t blendDisplay32(uint32_t source, uint32_t destination, uint8_t factor) {
#if defined(__SSE4_1__) || defined(_M_X64) || defined(__EMSCRIPTEN__)
    const auto unpack = [](uint32_t v) { return _mm_cvtepu8_epi32(_mm_cvtsi32_si128(int(v))); };
    const auto src = unpack(source), dst = unpack(destination);
    const auto product = _mm_mullo_epi32(_mm_sub_epi32(src, dst), _mm_set1_epi32(factor));
    const auto magnitude = _mm_add_epi32(_mm_abs_epi32(product), _mm_set1_epi32(1));
    const auto quotient = _mm_srli_epi32(_mm_add_epi32(magnitude, _mm_srli_epi32(magnitude, 8)), 8);
    const auto value = _mm_add_epi32(dst, _mm_sign_epi32(quotient, product));
    const auto packed = _mm_packus_epi16(_mm_packs_epi32(value, _mm_setzero_si128()), _mm_setzero_si128());
    return (uint32_t(_mm_cvtsi128_si32(packed)) & 0x00ffffffu) | 0xff000000u;
#else
    uint32_t result = 0xff000000u;
    for (unsigned shift = 0; shift < 24; shift += 8) {
        const int src = (source >> shift) & 255, dst = (destination >> shift) & 255;
        result |= uint32_t(dst + ((src - dst) * int(factor)) / 255) << shift;
    }
    return result;
#endif
}

class Interpolator4 {
public:
    Interpolator4(const std::array<float, 4>& a, const std::array<float, 4>& b, const std::array<float, 4>& c)
#if defined(__SSE4_1__) || defined(_M_X64) || defined(__EMSCRIPTEN__)
        : a_(_mm_loadu_ps(a.data())), b_(_mm_loadu_ps(b.data())), c_(_mm_loadu_ps(c.data())){}
#else
        : a_(a), b_(b), c_(c) {
    }
#endif
          std::array<float, 4> values(float w0, float w1, float w2) const {
        std::array<float, 4> result;
#if defined(__SSE4_1__) || defined(_M_X64) || defined(__EMSCRIPTEN__)
        _mm_storeu_ps(result.data(), interpolate(w0, w1, w2));
#else
        for (unsigned i = 0; i < 4; ++i)
            result[i] = a_[i] * w0 + b_[i] * w1 + c_[i] * w2;
#endif
        return result;
    }
    uint32_t color(float w0, float w1, float w2) const {
#if defined(__SSE4_1__) || defined(_M_X64) || defined(__EMSCRIPTEN__)
        const __m128i whole = _mm_cvttps_epi32(interpolate(w0, w1, w2));
        const __m128i words = _mm_packs_epi32(whole, _mm_setzero_si128());
        return static_cast<uint32_t>(_mm_cvtsi128_si32(_mm_packus_epi16(words, _mm_setzero_si128())));
#else
        const auto channels = values(w0, w1, w2);
        uint32_t result = 0;
        for (unsigned i = 0; i < 4; ++i)
            result |= uint32_t(std::clamp(int(channels[i]), 0, 255)) << (8 * i);
        return result;
#endif
    }

private:
#if defined(__SSE4_1__) || defined(_M_X64) || defined(__EMSCRIPTEN__)
    __m128 interpolate(float w0, float w1, float w2) const {
        return _mm_add_ps(_mm_add_ps(_mm_mul_ps(a_, _mm_set1_ps(w0)), _mm_mul_ps(b_, _mm_set1_ps(w1))),
                          _mm_mul_ps(c_, _mm_set1_ps(w2)));
    }
    __m128 a_, b_, c_;
#else
    std::array<float, 4> a_, b_, c_;
#endif
};
struct RowSpan {
    int first, last;
};
// Conservative affine bounds discard empty portions of a row. The exact
// float coverage test still decides every retained pixel. Error allowances
// cover all native rounding directions, including cancellation in the sums.
class TriangleRowClip {
public:
    TriangleRowClip(int minX, int maxX, float fx2, float fy2, float dy0, float dx0, float dy1, float dx1, float winding,
                    float inverse)
        : minX_(minX), maxX_(maxX), fx2_(fx2), fy2_(fy2), dy0_(dy0), dx0_(dx0), dy1_(dy1), dx1_(dx1),
          scale_(double(winding) * inverse), inverse_(inverse) {
        maxRx_ = std::max(std::fabs(double(minX) + 0.5 - fx2), std::fabs(double(maxX) + 0.5 - fx2));
        a0_ = double(dy0) * scale_;
        a1_ = double(dy1) * scale_;
    }
    RowSpan row(float py) const {
        if (maxX_ - minX_ < 32)
            return {minX_, maxX_};
        const float ry = py - fy2_;
        const double m0 = (std::fabs(dy0_) * maxRx_ + std::fabs(dx0_ * double(ry))) * inverse_;
        const double m1 = (std::fabs(dy1_) * maxRx_ + std::fabs(dx1_ * double(ry))) * inverse_;
        constexpr double kRoundError = 8.0 / 8388608.0;
        const double e0 = kRoundError * m0 + 1.0e-7;
        const double e1 = kRoundError * m1 + 1.0e-7;
        const double e2 = e0 + e1 + kRoundError * (1.0 + m0 + m1);
        const double b0 = (dx0_ * double(ry) - dy0_ * fx2_) * scale_;
        const double b1 = (dx1_ * double(ry) - dy1_ * fx2_) * scale_;
        double lo = double(minX_) + 0.5, hi = double(maxX_) + 0.5;
        const auto intersect = [&](double a, double b, double error) {
            const double threshold = -1.0e-4 - error;
            if (a > 0)
                lo = std::max(lo, (threshold - b) / a);
            else if (a < 0)
                hi = std::min(hi, (threshold - b) / a);
            else if (b < threshold)
                lo = hi + 1;
        };
        intersect(a0_, b0, e0);
        intersect(a1_, b1, e1);
        intersect(-a0_ - a1_, 1.0 - b0 - b1, e2);
        if (!std::isfinite(lo) || !std::isfinite(hi))
            return {minX_, maxX_};
        if (lo > hi)
            return {1, 0};
        // Keep one boundary pixel on either side as an additional allowance
        // for the double-precision affine-bound construction itself.
        return {std::max(minX_, int(std::floor(lo - 0.5))), std::min(maxX_, int(std::ceil(hi - 0.5)))};
    }

private:
    int minX_, maxX_;
    double fx2_;
    float fy2_;
    double dy0_, dx0_, dy1_, dx1_, scale_, inverse_, maxRx_, a0_, a1_;
};
struct TriangleWeights {
    alignas(16) float w0[4], w1[4], w2[4];
    unsigned mask = 0;
};
inline TriangleWeights triangleWeights(int x, float py, float fx2, float fy2, float dy0, float dx0, float dy1,
                                       float dx1, float winding, float invAbsDenom) {
    TriangleWeights result;
#if defined(__SSE4_1__) || defined(_M_X64) || defined(__EMSCRIPTEN__)
    const __m128 px = _mm_setr_ps(float(x) + 0.5f, float(x + 1) + 0.5f, float(x + 2) + 0.5f, float(x + 3) + 0.5f);
    const __m128 rx = _mm_sub_ps(px, _mm_set1_ps(fx2));
    const __m128 ry = _mm_sub_ps(_mm_set1_ps(py), _mm_set1_ps(fy2));
    const __m128 sign = _mm_set1_ps(winding), reciprocal = _mm_set1_ps(invAbsDenom);
    const __m128 w0 = _mm_mul_ps(
        _mm_mul_ps(_mm_add_ps(_mm_mul_ps(_mm_set1_ps(dy0), rx), _mm_mul_ps(_mm_set1_ps(dx0), ry)), sign), reciprocal);
    const __m128 w1 = _mm_mul_ps(
        _mm_mul_ps(_mm_add_ps(_mm_mul_ps(_mm_set1_ps(dy1), rx), _mm_mul_ps(_mm_set1_ps(dx1), ry)), sign), reciprocal);
    const __m128 w2 = _mm_sub_ps(_mm_sub_ps(_mm_set1_ps(1.0f), w0), w1);
    const __m128 edge = _mm_set1_ps(-1.0e-4f);
    const __m128 rejected =
        _mm_or_ps(_mm_or_ps(_mm_cmplt_ps(w0, edge), _mm_cmplt_ps(w1, edge)), _mm_cmplt_ps(w2, edge));
    result.mask = unsigned(~_mm_movemask_ps(rejected)) & 15u;
    _mm_store_ps(result.w0, w0);
    _mm_store_ps(result.w1, w1);
    _mm_store_ps(result.w2, w2);
#else
    for (unsigned i = 0; i < 4; ++i) {
        const float px = float(x + int(i)) + 0.5f;
        result.w0[i] = (((dy0 * (px - fx2) + dx0 * (py - fy2)) * winding) * invAbsDenom);
        result.w1[i] = (((dy1 * (px - fx2) + dx1 * (py - fy2)) * winding) * invAbsDenom);
        result.w2[i] = 1.0f - result.w0[i] - result.w1[i];
        if (!(result.w0[i] < -1.0e-4f || result.w1[i] < -1.0e-4f || result.w2[i] < -1.0e-4f))
            result.mask |= 1u << i;
    }
#endif
    return result;
}
} // namespace gs_cpu
