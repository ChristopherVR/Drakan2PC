#pragma once
#include <cstdint>
#include <bit>
#include <cmath>
#include <smmintrin.h>

#if defined(_MSC_VER)
#define VU_SIMD_INLINE __forceinline
#else
#define VU_SIMD_INLINE inline __attribute__((always_inline))
#endif
namespace Vu1Simd {
// WebAssembly has a fixed nearest rounding mode. VU instructions instead use
// toward-zero rounding, including the low bits lost when unlike exponents add.
// Keep this emulation local to VU arithmetic; EE/host arithmetic is unaffected.
namespace Software {
VU_SIMD_INLINE float roundZero(double value, double residual = 0.0) {
    float result = static_cast<float>(value);
    if (std::isnan(value) || value == 0.0)
        return result;
    uint32_t bits = std::bit_cast<uint32_t>(result);
    const double rounded = static_cast<double>(result);
    if ((value > 0.0 && (rounded > value || (rounded == value && residual < 0.0))) ||
        (value < 0.0 && (rounded < value || (rounded == value && residual > 0.0))))
        --bits;
    // EE worker threads enable FTZ/DAZ on x86. Reproduce FTZ explicitly here.
    if ((bits & 0x7f800000u) == 0)
        bits &= 0x80000000u;
    return std::bit_cast<float>(bits);
}
VU_SIMD_INLINE float add(float a, float b) {
    const double x = a, y = b, sum = x + y;
    const double back = sum - x;
    const double residual = (x - (sum - back)) + (y - back);
    return roundZero(sum, residual);
}
VU_SIMD_INLINE float multiply(float a, float b) {
    return roundZero(double(a) * double(b));
}
VU_SIMD_INLINE float divide(float a, float b) {
    return roundZero(double(a) / double(b));
}
VU_SIMD_INLINE float squareRoot(float value) {
    float result = std::sqrt(value);
    if (std::isfinite(result) && double(result) * double(result) > double(value))
        result = std::bit_cast<float>(std::bit_cast<uint32_t>(result) - 1);
    return result;
}
} // namespace Software

#if defined(__EMSCRIPTEN__)
VU_SIMD_INLINE float add(float a, float b) {
    return Software::add(a, b);
}
VU_SIMD_INLINE float subtract(float a, float b) {
    return Software::add(a, -b);
}
VU_SIMD_INLINE float multiply(float a, float b) {
    return Software::multiply(a, b);
}
VU_SIMD_INLINE float divide(float a, float b) {
    return Software::divide(a, b);
}
VU_SIMD_INLINE float fromIntScalar(int32_t value) {
    return Software::roundZero(double(value));
}
VU_SIMD_INLINE float squareRoot(float value) {
    return Software::squareRoot(value);
}
#else
VU_SIMD_INLINE float add(float a, float b) {
    return a + b;
}
VU_SIMD_INLINE float subtract(float a, float b) {
    return a - b;
}
VU_SIMD_INLINE float multiply(float a, float b) {
    return a * b;
}
VU_SIMD_INLINE float divide(float a, float b) {
    return a / b;
}
VU_SIMD_INLINE float fromIntScalar(int32_t value) {
    return static_cast<float>(value);
}
VU_SIMD_INLINE float squareRoot(float value) {
    return std::sqrt(value);
}
#endif

class ScopedRounding {
#if !defined(__EMSCRIPTEN__)
    unsigned prior_ = _mm_getcsr();
#endif
public:
    ScopedRounding() {
#if !defined(__EMSCRIPTEN__)
        _mm_setcsr((prior_ & ~0x6000u) | 0x6000u);
#endif
    }
    ~ScopedRounding() {
#if !defined(__EMSCRIPTEN__)
        _mm_setcsr(prior_);
#endif
    }
};

#if defined(__EMSCRIPTEN__)
template <typename Operation>
VU_SIMD_INLINE __m128 mapBinary(__m128 a, __m128 b, Operation operation) {
    alignas(16) float x[4], y[4];
    _mm_store_ps(x, a);
    _mm_store_ps(y, b);
    for (unsigned i = 0; i < 4; ++i)
        x[i] = operation(x[i], y[i]);
    return _mm_load_ps(x);
}
VU_SIMD_INLINE __m128 add(__m128 a, __m128 b) {
    return mapBinary(a, b, [](float x, float y) { return Software::add(x, y); });
}
VU_SIMD_INLINE __m128 subtract(__m128 a, __m128 b) {
    return mapBinary(a, b, [](float x, float y) { return Software::add(x, -y); });
}
VU_SIMD_INLINE __m128 multiply(__m128 a, __m128 b) {
    return mapBinary(a, b, Software::multiply);
}
VU_SIMD_INLINE __m128 divide(__m128 a, __m128 b) {
    return mapBinary(a, b, Software::divide);
}
VU_SIMD_INLINE __m128 fromInt(__m128i value) {
    alignas(16) int32_t integers[4];
    alignas(16) float floats[4];
    _mm_store_si128(reinterpret_cast<__m128i*>(integers), value);
    for (unsigned i = 0; i < 4; ++i)
        floats[i] = Software::roundZero(double(integers[i]));
    return _mm_load_ps(floats);
}
#else
VU_SIMD_INLINE __m128 add(__m128 a, __m128 b) {
    return _mm_add_ps(a, b);
}
VU_SIMD_INLINE __m128 subtract(__m128 a, __m128 b) {
    return _mm_sub_ps(a, b);
}
VU_SIMD_INLINE __m128 multiply(__m128 a, __m128 b) {
    return _mm_mul_ps(a, b);
}
VU_SIMD_INLINE __m128 divide(__m128 a, __m128 b) {
    return _mm_div_ps(a, b);
}
VU_SIMD_INLINE __m128 fromInt(__m128i value) {
    return _mm_cvtepi32_ps(value);
}
#endif

// Exact operand normalization: keep finite normals, flush denormals to signed
// zero, and clamp exponent-255 values to signed FLT_MAX. Magnitudes are positive
// integers, so MIN and one threshold compare replace exponent extraction and
// the separate zero/nonfinite selection masks.
VU_SIMD_INLINE __m128 normalize(__m128 value) {
    const __m128i bits = _mm_castps_si128(value);
    const __m128i sign = _mm_and_si128(bits, _mm_set1_epi32(static_cast<int>(0x80000000u)));
    const __m128i magnitude = _mm_and_si128(bits, _mm_set1_epi32(0x7fffffff));
    const __m128i clamped = _mm_min_epu32(magnitude, _mm_set1_epi32(0x7f7fffff));
    const __m128i normal = _mm_cmpgt_epi32(magnitude, _mm_set1_epi32(0x007fffff));
    return _mm_castsi128_ps(_mm_or_si128(sign, _mm_and_si128(clamped, normal)));
}

// Inputs have already passed vuNormalizeVec (finite, with denormals flushed).
// Scaling by a power of two is exact in the integer conversion range. Positive
// overflow saturates to INT_MAX; CVTTPS already returns INT_MIN for negative
// overflow. The float immediately below 2^31 is below INT_MAX as well.
VU_SIMD_INLINE __m128i toFixed(__m128 value, float scale) {
    const __m128 scaled = Vu1Simd::multiply(value, _mm_set1_ps(scale));
    const __m128i converted = _mm_cvttps_epi32(scaled);
    const __m128i positiveOverflow = _mm_castps_si128(_mm_cmpge_ps(scaled, _mm_set1_ps(2147483648.0f)));
    // CVTTPS returns INT_MIN in every positive-overflow lane. Flipping all
    // bits produces INT_MAX there and leaves every other converted lane intact.
    return _mm_xor_si128(converted, positiveOverflow);
}

// Select result components using immediate masks. Compiled microcode folds
// this switch to one instruction (or no merge for a full-register write),
// avoiding BLENDV's implicit XMM0 mask and its register pressure.
VU_SIMD_INLINE __m128 mergeLanes(__m128 prior, __m128 value, unsigned mask) {
    switch (mask & 15u) {
        case 0:
            return prior;
        case 1:
            return _mm_move_ss(prior, value);
        case 2:
            return _mm_blend_ps(prior, value, 2);
        case 3:
            return _mm_blend_ps(prior, value, 3);
        case 4:
            return _mm_blend_ps(prior, value, 4);
        case 5:
            return _mm_blend_ps(prior, value, 5);
        case 6:
            return _mm_blend_ps(prior, value, 6);
        case 7:
            return _mm_blend_ps(prior, value, 7);
        case 8:
            return _mm_blend_ps(prior, value, 8);
        case 9:
            return _mm_blend_ps(prior, value, 9);
        case 10:
            return _mm_blend_ps(prior, value, 10);
        case 11:
            return _mm_blend_ps(prior, value, 11);
        case 12:
            return _mm_blend_ps(prior, value, 12);
        case 13:
            return _mm_blend_ps(prior, value, 13);
        case 14:
            return _mm_blend_ps(prior, value, 14);
        default:
            return value;
    }
}

// The lane mask is a compile-time constant in the compiled microprogram.
// Keeping the operation here lets it disappear into its instruction pair.
VU_SIMD_INLINE void writeLanes(float* dest, const float* source, uint8_t lanes) {
    if ((lanes & 15u) == 15u) {
        _mm_storeu_ps(dest, _mm_loadu_ps(source));
        return;
    }
    if (lanes & 8u)
        dest[0] = source[0];
    if (lanes & 4u)
        dest[1] = source[1];
    if (lanes & 2u)
        dest[2] = source[2];
    if (lanes & 1u)
        dest[3] = source[3];
}

VU_SIMD_INLINE int ordinaryLanes(__m128 value) {
    const __m128i magnitude = _mm_and_si128(_mm_castps_si128(value), _mm_set1_epi32(0x7fffffff));
    // Integer wrap maps [0x04000000, 0x7b800000) onto the lowest signed
    // values. One signed comparison excludes both the small and large classes.
    const __m128i biased = _mm_add_epi32(magnitude, _mm_set1_epi32(0x7c000000));
    const __m128i valid = _mm_cmpgt_epi32(_mm_set1_epi32(-0x08800000), biased);
    return _mm_movemask_ps(_mm_castsi128_ps(valid));
}
} // namespace Vu1Simd
#undef VU_SIMD_INLINE
