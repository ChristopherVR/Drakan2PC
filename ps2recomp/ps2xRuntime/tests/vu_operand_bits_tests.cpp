#include "vu/ps2_vu1_simd.h"
#include <array>
#include <bit>
#include <cfenv>
#include <cstdio>
#include <cstring>
#include <random>

#if defined(_MSC_VER)
#define BIT_TEST_INLINE __forceinline
#else
#define BIT_TEST_INLINE inline __attribute__((always_inline))
#endif
// Frozen pre-change operand mapping, independent of the compact implementation.
static BIT_TEST_INLINE __m128 priorNormalize(__m128 value) {
    const __m128i bits = _mm_castps_si128(value);
    const __m128i sign = _mm_and_si128(bits, _mm_set1_epi32(static_cast<int>(0x80000000u)));
    const __m128i exponent = _mm_and_si128(_mm_srli_epi32(bits, 23), _mm_set1_epi32(255));
    const __m128i zero = _mm_cmpeq_epi32(exponent, _mm_setzero_si128());
    const __m128i maximum = _mm_cmpeq_epi32(exponent, _mm_set1_epi32(255));
    __m128i out = _mm_andnot_si128(_mm_or_si128(zero, maximum), bits);
    out = _mm_or_si128(out, _mm_and_si128(zero, sign));
    out = _mm_or_si128(out, _mm_and_si128(maximum, _mm_or_si128(sign, _mm_set1_epi32(0x7f7fffff))));
    return _mm_castsi128_ps(out);
}
static BIT_TEST_INLINE int priorOrdinary(__m128 value) {
    const __m128i mag = _mm_and_si128(_mm_castps_si128(value), _mm_set1_epi32(0x7fffffff));
    return _mm_movemask_ps(_mm_castsi128_ps(_mm_and_si128(_mm_cmpgt_epi32(mag, _mm_set1_epi32(0x03ffffff)),
                                                          _mm_cmplt_epi32(mag, _mm_set1_epi32(0x7b800000)))));
}
static BIT_TEST_INLINE __m128i priorFixed(__m128 value) {
    const __m128i converted = _mm_cvttps_epi32(value);
    const __m128i over = _mm_castps_si128(_mm_cmpge_ps(value, _mm_set1_ps(2147483648.0f)));
    return _mm_or_si128(_mm_and_si128(over, _mm_set1_epi32(0x7fffffff)), _mm_andnot_si128(over, converted));
}

int main() {
    for (uint64_t base = 0; base < 0x100000000ull; base += 4) {
        const __m128 v =
            _mm_castsi128_ps(_mm_set_epi32(int32_t(base + 3), int32_t(base + 2), int32_t(base + 1), int32_t(base)));
        const __m128 oldNormalized = priorNormalize(v), normalized = Vu1Simd::normalize(v);
        if (_mm_movemask_epi8(_mm_cmpeq_epi8(_mm_castps_si128(oldNormalized), _mm_castps_si128(normalized))) !=
            0xffff) {
            std::printf("FAIL normalize at %llx\n", (unsigned long long)base);
            return 4;
        }
        if (priorOrdinary(v) != Vu1Simd::ordinaryLanes(v)) {
            std::printf("FAIL class at %llx\n", (unsigned long long)base);
            return 1;
        }
        const __m128i fixed = Vu1Simd::toFixed(v, 1.0f);
        const __m128i prior = priorFixed(v);
        if (_mm_movemask_epi8(_mm_cmpeq_epi8(fixed, prior)) != 0xffff) {
            std::printf("FAIL conversion at %llx\n", (unsigned long long)base);
            return 3;
        }
    }
    std::mt19937 rng(0x1441a9e);
    uint64_t merges = 0;
    for (int mode : {FE_TONEAREST, FE_TOWARDZERO, FE_DOWNWARD, FE_UPWARD}) {
        std::fesetround(mode);
        for (unsigned trial = 0; trial < 16384; ++trial) {
            alignas(16) uint32_t a[4], b[4];
            for (auto& x : a)
                x = rng();
            for (auto& x : b)
                x = rng();
            for (unsigned mask = 0; mask < 16; ++mask) {
                alignas(16) uint32_t result[4];
                _mm_store_si128(reinterpret_cast<__m128i*>(result),
                                _mm_castps_si128(Vu1Simd::mergeLanes(
                                    _mm_castsi128_ps(_mm_load_si128(reinterpret_cast<const __m128i*>(a))),
                                    _mm_castsi128_ps(_mm_load_si128(reinterpret_cast<const __m128i*>(b))), mask)));
                for (unsigned lane = 0; lane < 4; ++lane)
                    if (result[lane] != ((mask >> lane) & 1u ? b[lane] : a[lane]))
                        return 2;
                ++merges;
            }
        }
    }
    std::fesetround(FE_TONEAREST);
    std::printf("PASS: all 4294967296 normalization, classifier and saturated-conversion bit patterns; %llu exact lane "
                "merges across all masks and four rounding modes\n",
                (unsigned long long)merges);
}
