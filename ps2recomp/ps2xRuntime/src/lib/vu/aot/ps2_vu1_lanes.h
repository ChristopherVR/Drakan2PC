#pragma once
// SIMD packs for the functional VU1 engine's loop path (ps2_vu1_func.cpp): every 128-bit lane holds the four
// components (x, y, z, w) of one loop iteration's value, so one instruction of the pack executes the same VU operation
// for W consecutive iterations. The operations reproduce the SSE code of the scalar engine lane by lane (same
// instructions, same MXCSR rounding), so results are bit-identical.
//
// MSVC accepts AVX2 / AVX-512 intrinsics without /arch, and the callers only reach these functions after a CPUID check
// (Vu1Lanes::detect). Other compilers build the packs only when the translation unit targets the instruction set.
#include <cstdint>
#include <cstring>

#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#define VU_LANES_AVX2 1
#define VU_LANES_AVX512 1
#else
#include <cpuid.h>
#if defined(__AVX2__)
#define VU_LANES_AVX2 1
#endif
#if defined(__AVX512F__)
#define VU_LANES_AVX512 1
#endif
#endif
#endif

#if defined(_MSC_VER)
#define VU_LANES_INLINE __forceinline
#define VU_LANES_NOINLINE __declspec(noinline)
#else
#define VU_LANES_INLINE inline __attribute__((always_inline))
#define VU_LANES_NOINLINE __attribute__((noinline))
#endif

namespace Vu1Lanes {

// Widest pack the CPU and the build support: 4 (AVX-512F), 2 (AVX2) or 0 (none).
inline uint32_t detect() {
#if defined(VU_LANES_AVX2)
    int r[4] = {};
    const auto cpuid = [&r](int leaf, int sub) {
#if defined(_MSC_VER) && !defined(__clang__)
        __cpuidex(r, leaf, sub);
#else
        unsigned a, b, c, d;
        __cpuid_count(leaf, sub, a, b, c, d);
        r[0] = static_cast<int>(a);
        r[1] = static_cast<int>(b);
        r[2] = static_cast<int>(c);
        r[3] = static_cast<int>(d);
#endif
    };
    cpuid(0, 0);
    if (r[0] < 7)
        return 0;
    cpuid(1, 0);
    const bool osxsave = (r[2] & (1 << 27)) != 0, avx = (r[2] & (1 << 28)) != 0;
    if (!osxsave || !avx)
        return 0;
#if defined(_MSC_VER) && !defined(__clang__)
    const uint64_t xcr0 = _xgetbv(0);
#else
    uint32_t lo, hi;
    __asm__("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    const uint64_t xcr0 = (static_cast<uint64_t>(hi) << 32) | lo;
#endif
    if ((xcr0 & 0x6u) != 0x6u) // XMM and YMM state
        return 0;
    cpuid(7, 0);
    const bool avx2 = (r[1] & (1 << 5)) != 0, avx512f = (r[1] & (1 << 16)) != 0;
    if (!avx2)
        return 0;
#if defined(VU_LANES_AVX512)
    if (avx512f && (xcr0 & 0xE6u) == 0xE6u) // + opmask, ZMM_Hi256, Hi16_ZMM state
        return 4;
#else
    (void)avx512f;
#endif
    return 2;
#else
    return 0;
#endif
}

#if defined(VU_LANES_AVX2)
// Two iterations per __m256.
struct Avx2 {
    static constexpr uint32_t W = 2;
    using V = __m256;
    using M = __m256i; // all ones in a lane component that failed a check

    static VU_LANES_INLINE V bcast(__m128 x) { return _mm256_set_m128(x, x); }
    static VU_LANES_INLINE V set1(float f) { return _mm256_set1_ps(f); }
    static VU_LANES_INLINE V constant(float x, float y, float z, float w) {
        return _mm256_setr_ps(x, y, z, w, x, y, z, w);
    }
    // W consecutive quadwords
    static VU_LANES_INLINE V load(const uint8_t* p) { return _mm256_loadu_ps(reinterpret_cast<const float*>(p)); }
    static VU_LANES_INLINE V loadLanes(const uint8_t* base, const uint32_t (&off)[W]) {
        return _mm256_loadu2_m128(reinterpret_cast<const float*>(base + off[1]),
                                  reinterpret_cast<const float*>(base + off[0]));
    }
    template <uint32_t J>
    static VU_LANES_INLINE __m128 lane(V x) {
        if constexpr (J == 0)
            return _mm256_castps256_ps128(x);
        else
            return _mm256_extractf128_ps(x, 1);
    }
    // components of `b` where bit c of C4 is set (bit 0 = x)
    template <uint32_t C4>
    static VU_LANES_INLINE V blend(V a, V b) {
        if constexpr ((C4 & 15u) == 0)
            return a;
        else if constexpr ((C4 & 15u) == 15u)
            return b;
        else
            return _mm256_blend_ps(a, b, static_cast<int>((C4 & 15u) | ((C4 & 15u) << 4)));
    }
    template <int Imm>
    static VU_LANES_INLINE V permute(V x) {
        return _mm256_permute_ps(x, Imm);
    }
    // lane j <- x lane j - 1; lane 0 <- start (any lane of it)
    static VU_LANES_INLINE V shiftIn(V x, V start) { return _mm256_permute2f128_ps(x, start, 0x02); }
    // every lane <- the last lane of x
    static VU_LANES_INLINE V last(V x) { return _mm256_permute2f128_ps(x, x, 0x11); }
    // every lane <- lane j of x
    static VU_LANES_INLINE V laneOf(V x, uint32_t j) {
        return j == 0 ? _mm256_permute2f128_ps(x, x, 0x00) : _mm256_permute2f128_ps(x, x, 0x11);
    }
    // all components of the first n lanes
    static VU_LANES_INLINE M firstLanes(uint32_t n) {
        return n >= 2 ? _mm256_set1_epi32(-1) : _mm256_setr_epi32(-1, -1, -1, -1, 0, 0, 0, 0);
    }

    static VU_LANES_INLINE V add(V a, V b) { return _mm256_add_ps(a, b); }
    static VU_LANES_INLINE V sub(V a, V b) { return _mm256_sub_ps(a, b); }
    static VU_LANES_INLINE V mul(V a, V b) { return _mm256_mul_ps(a, b); }
    static VU_LANES_INLINE V div(V a, V b) { return _mm256_div_ps(a, b); }
    static VU_LANES_INLINE V min(V a, V b) { return _mm256_min_ps(a, b); }
    static VU_LANES_INLINE V max(V a, V b) { return _mm256_max_ps(a, b); }

    // Vu1Simd::normalize
    static VU_LANES_INLINE V normalize(V value) {
        const __m256i bits = _mm256_castps_si256(value);
        const __m256i sign = _mm256_and_si256(bits, _mm256_set1_epi32(static_cast<int>(0x80000000u)));
        const __m256i magnitude = _mm256_and_si256(bits, _mm256_set1_epi32(0x7fffffff));
        const __m256i clamped = _mm256_min_epu32(magnitude, _mm256_set1_epi32(0x7f7fffff));
        const __m256i normal = _mm256_cmpgt_epi32(magnitude, _mm256_set1_epi32(0x007fffff));
        return _mm256_castsi256_ps(_mm256_or_si256(sign, _mm256_and_si256(clamped, normal)));
    }
    // Vu1Simd::toFixed
    static VU_LANES_INLINE V toFixed(V value, float scale) {
        const __m256 scaled = _mm256_mul_ps(value, _mm256_set1_ps(scale));
        const __m256i converted = _mm256_cvttps_epi32(scaled);
        const __m256i overflow = _mm256_castps_si256(_mm256_cmp_ps(scaled, _mm256_set1_ps(2147483648.0f), _CMP_GE_OS));
        return _mm256_castsi256_ps(_mm256_xor_si256(converted, overflow));
    }
    static VU_LANES_INLINE V itof(V bits) { return _mm256_cvtepi32_ps(_mm256_castps_si256(bits)); }
    static VU_LANES_INLINE V abs(V x) {
        return _mm256_andnot_ps(_mm256_castsi256_ps(_mm256_set1_epi32(static_cast<int>(0x80000000u))), x);
    }
    // DIV: den == 0 gives +-FLT_MAX by the sign of num ^ den; the caller normalizes the result
    static VU_LANES_INLINE V divide(V num, V den) {
        const __m256 q = _mm256_div_ps(num, den);
        const __m256 zero = _mm256_cmp_ps(den, _mm256_setzero_ps(), _CMP_EQ_OQ);
        const __m256i sign = _mm256_and_si256(_mm256_castps_si256(_mm256_xor_ps(num, den)),
                                              _mm256_set1_epi32(static_cast<int>(0x80000000u)));
        const __m256 special = _mm256_castsi256_ps(_mm256_or_si256(sign, _mm256_set1_epi32(0x7f7fffff)));
        return _mm256_blendv_ps(q, special, zero);
    }

    // ---- checks (component masks)
    static VU_LANES_INLINE M none() { return _mm256_setzero_si256(); }
    template <uint32_t C4>
    static VU_LANES_INLINE M comps() {
        constexpr int x = (C4 & 1u) ? -1 : 0, y = (C4 & 2u) ? -1 : 0, z = (C4 & 4u) ? -1 : 0, w = (C4 & 8u) ? -1 : 0;
        return _mm256_setr_epi32(x, y, z, w, x, y, z, w);
    }
    // Vu1Simd::ordinaryLanes: exponent field 8..246
    static VU_LANES_INLINE M ordinary(V v) {
        const __m256i magnitude = _mm256_and_si256(_mm256_castps_si256(v), _mm256_set1_epi32(0x7fffffff));
        const __m256i biased = _mm256_add_epi32(magnitude, _mm256_set1_epi32(0x7c000000));
        return _mm256_cmpgt_epi32(_mm256_set1_epi32(-0x08800000), biased);
    }
    static VU_LANES_INLINE M zero(V v) {
        const __m256i magnitude = _mm256_and_si256(_mm256_castps_si256(v), _mm256_set1_epi32(0x7fffffff));
        return _mm256_cmpeq_epi32(magnitude, _mm256_setzero_si256());
    }
    static VU_LANES_INLINE M mor(M a, M b) { return _mm256_or_si256(a, b); }
    static VU_LANES_INLINE M mand(M a, M b) { return _mm256_and_si256(a, b); }
    // components of m whose value is not ordinary / not zero
    static VU_LANES_INLINE M notOrdinary(M m, V v) { return _mm256_andnot_si256(ordinary(v), m); }
    static VU_LANES_INLINE M nonzero(M m, V v) { return _mm256_andnot_si256(zero(v), m); }
    static VU_LANES_INLINE bool any(M m) { return !_mm256_testz_si256(m, m); }
    // any component of lane j
    static VU_LANES_INLINE bool laneAny(M m, uint32_t j) {
        return ((_mm256_movemask_ps(_mm256_castsi256_ps(m)) >> (4 * j)) & 0xF) != 0;
    }
    static VU_LANES_INLINE V insertLane(V x, uint32_t j, __m128 v) {
        return j == 0 ? _mm256_insertf128_ps(x, v, 0) : _mm256_insertf128_ps(x, v, 1);
    }

    // ---- stores
    template <uint32_t C4>
    static VU_LANES_INLINE void storeLane(uint8_t* p, __m128 x) {
        float* f = reinterpret_cast<float*>(p);
        if constexpr ((C4 & 15u) == 15u)
            _mm_storeu_ps(f, x);
        else
            _mm_storeu_ps(f, _mm_blend_ps(_mm_loadu_ps(f), x, static_cast<int>(C4 & 15u)));
    }
};
#endif

#if defined(VU_LANES_AVX512)
// Four iterations per __m512.
struct Avx512 {
    static constexpr uint32_t W = 4;
    using V = __m512;
    using M = __mmask16;

    static VU_LANES_INLINE V bcast(__m128 x) { return _mm512_broadcast_f32x4(x); }
    static VU_LANES_INLINE V set1(float f) { return _mm512_set1_ps(f); }
    static VU_LANES_INLINE V constant(float x, float y, float z, float w) {
        return _mm512_broadcast_f32x4(_mm_setr_ps(x, y, z, w));
    }
    static VU_LANES_INLINE V load(const uint8_t* p) { return _mm512_loadu_ps(p); }
    static VU_LANES_INLINE V loadLanes(const uint8_t* base, const uint32_t (&off)[W]) {
        const auto at = [base](uint32_t o) { return _mm_loadu_ps(reinterpret_cast<const float*>(base + o)); };
        const __m256 lo = _mm256_set_m128(at(off[1]), at(off[0]));
        const __m256 hi = _mm256_set_m128(at(off[3]), at(off[2]));
        return _mm512_castpd_ps(
            _mm512_insertf64x4(_mm512_castpd256_pd512(_mm256_castps_pd(lo)), _mm256_castps_pd(hi), 1)); // AVX-512F
    }
    template <uint32_t J>
    static VU_LANES_INLINE __m128 lane(V x) {
        if constexpr (J == 0)
            return _mm512_castps512_ps128(x);
        else
            return _mm512_extractf32x4_ps(x, J);
    }
    template <uint32_t C4>
    static VU_LANES_INLINE V blend(V a, V b) {
        if constexpr ((C4 & 15u) == 0)
            return a;
        else if constexpr ((C4 & 15u) == 15u)
            return b;
        else
            return _mm512_mask_blend_ps(static_cast<__mmask16>((C4 & 15u) * 0x1111u), a, b);
    }
    template <int Imm>
    static VU_LANES_INLINE V permute(V x) {
        return _mm512_permute_ps(x, Imm);
    }
    static VU_LANES_INLINE V shiftIn(V x, V start) {
        return _mm512_castsi512_ps(_mm512_alignr_epi32(_mm512_castps_si512(x), _mm512_castps_si512(start), 12));
    }
    static VU_LANES_INLINE V last(V x) { return _mm512_shuffle_f32x4(x, x, 0xFF); }
    static VU_LANES_INLINE V laneOf(V x, uint32_t j) {
        switch (j) {
            case 0:
                return _mm512_shuffle_f32x4(x, x, 0x00);
            case 1:
                return _mm512_shuffle_f32x4(x, x, 0x55);
            case 2:
                return _mm512_shuffle_f32x4(x, x, 0xAA);
            default:
                return _mm512_shuffle_f32x4(x, x, 0xFF);
        }
    }
    static VU_LANES_INLINE M firstLanes(uint32_t n) { return static_cast<M>(n >= 4 ? 0xFFFFu : (1u << (4 * n)) - 1u); }

    static VU_LANES_INLINE V add(V a, V b) { return _mm512_add_ps(a, b); }
    static VU_LANES_INLINE V sub(V a, V b) { return _mm512_sub_ps(a, b); }
    static VU_LANES_INLINE V mul(V a, V b) { return _mm512_mul_ps(a, b); }
    static VU_LANES_INLINE V div(V a, V b) { return _mm512_div_ps(a, b); }
    static VU_LANES_INLINE V min(V a, V b) { return _mm512_min_ps(a, b); }
    static VU_LANES_INLINE V max(V a, V b) { return _mm512_max_ps(a, b); }

    static VU_LANES_INLINE V normalize(V value) {
        const __m512i bits = _mm512_castps_si512(value);
        const __m512i sign = _mm512_and_si512(bits, _mm512_set1_epi32(static_cast<int>(0x80000000u)));
        const __m512i magnitude = _mm512_and_si512(bits, _mm512_set1_epi32(0x7fffffff));
        const __m512i clamped = _mm512_min_epu32(magnitude, _mm512_set1_epi32(0x7f7fffff));
        const __mmask16 normal = _mm512_cmpgt_epi32_mask(magnitude, _mm512_set1_epi32(0x007fffff));
        return _mm512_castsi512_ps(_mm512_mask_or_epi32(sign, normal, sign, clamped));
    }
    static VU_LANES_INLINE V toFixed(V value, float scale) {
        const __m512 scaled = _mm512_mul_ps(value, _mm512_set1_ps(scale));
        const __m512i converted = _mm512_cvttps_epi32(scaled);
        const __mmask16 overflow = _mm512_cmp_ps_mask(scaled, _mm512_set1_ps(2147483648.0f), _CMP_GE_OS);
        return _mm512_castsi512_ps(_mm512_mask_xor_epi32(converted, overflow, converted, _mm512_set1_epi32(-1)));
    }
    static VU_LANES_INLINE V itof(V bits) { return _mm512_cvtepi32_ps(_mm512_castps_si512(bits)); }
    static VU_LANES_INLINE V abs(V x) {
        return _mm512_castsi512_ps(_mm512_and_si512(_mm512_castps_si512(x), _mm512_set1_epi32(0x7fffffff)));
    }
    static VU_LANES_INLINE V divide(V num, V den) {
        const __m512 q = _mm512_div_ps(num, den);
        const __mmask16 zero = _mm512_cmp_ps_mask(den, _mm512_setzero_ps(), _CMP_EQ_OQ);
        const __m512i sign = _mm512_and_si512(_mm512_xor_si512(_mm512_castps_si512(num), _mm512_castps_si512(den)),
                                              _mm512_set1_epi32(static_cast<int>(0x80000000u)));
        const __m512 special = _mm512_castsi512_ps(_mm512_or_si512(sign, _mm512_set1_epi32(0x7f7fffff)));
        return _mm512_mask_blend_ps(zero, q, special);
    }

    static VU_LANES_INLINE M none() { return 0; }
    template <uint32_t C4>
    static VU_LANES_INLINE M comps() {
        return static_cast<M>((C4 & 15u) * 0x1111u);
    }
    static VU_LANES_INLINE M ordinary(V v) {
        const __m512i magnitude = _mm512_and_si512(_mm512_castps_si512(v), _mm512_set1_epi32(0x7fffffff));
        const __m512i biased = _mm512_add_epi32(magnitude, _mm512_set1_epi32(0x7c000000));
        return _mm512_cmplt_epi32_mask(biased, _mm512_set1_epi32(-0x08800000));
    }
    static VU_LANES_INLINE M zero(V v) {
        return _mm512_testn_epi32_mask(_mm512_castps_si512(v), _mm512_set1_epi32(0x7fffffff));
    }
    static VU_LANES_INLINE M mor(M a, M b) { return static_cast<M>(a | b); }
    static VU_LANES_INLINE M mand(M a, M b) { return static_cast<M>(a & b); }
    static VU_LANES_INLINE M notOrdinary(M m, V v) {
        const __m512i magnitude = _mm512_and_si512(_mm512_castps_si512(v), _mm512_set1_epi32(0x7fffffff));
        const __m512i biased = _mm512_add_epi32(magnitude, _mm512_set1_epi32(0x7c000000));
        return _mm512_mask_cmpge_epi32_mask(m, biased, _mm512_set1_epi32(-0x08800000));
    }
    static VU_LANES_INLINE M nonzero(M m, V v) {
        return _mm512_mask_test_epi32_mask(m, _mm512_castps_si512(v), _mm512_set1_epi32(0x7fffffff));
    }
    static VU_LANES_INLINE bool any(M m) { return m != 0; }
    static VU_LANES_INLINE bool laneAny(M m, uint32_t j) { return ((m >> (4 * j)) & 0xFu) != 0; }
    static VU_LANES_INLINE V insertLane(V x, uint32_t j, __m128 v) {
        switch (j) {
            case 0:
                return _mm512_insertf32x4(x, v, 0);
            case 1:
                return _mm512_insertf32x4(x, v, 1);
            case 2:
                return _mm512_insertf32x4(x, v, 2);
            default:
                return _mm512_insertf32x4(x, v, 3);
        }
    }

    template <uint32_t C4>
    static VU_LANES_INLINE void storeLane(uint8_t* p, __m128 x) {
        float* f = reinterpret_cast<float*>(p);
        if constexpr ((C4 & 15u) == 15u)
            _mm_storeu_ps(f, x);
        else
            _mm_storeu_ps(f, _mm_blend_ps(_mm_loadu_ps(f), x, static_cast<int>(C4 & 15u)));
    }
};
#endif

// Components of `comps` whose FMAC result Vu1Step::calculateFmac would not return as the plain SSE result, i.e. where
// neither its fast path (ordinary result, and ordinary product for MADD/MSUB) nor flagsKnownClasses (a zero sum for
// ADD/SUB; a zero result with a zero operand for MUL/MADD/MSUB; for MADD/MSUB a product that is ordinary or has a zero
// operand) applies. Kind: 1 ADD, 2 SUB, 3 MUL, 4 MADD, 5 MSUB. ConstB: the second operand is the constant 0 (0), the
// constant 1 (1) or a value (-1). vs and b are the (normalized) operands.
template <class B, int Kind, int ConstB>
VU_LANES_INLINE typename B::M fmacBad(typename B::M comps, typename B::V vs, typename B::V b, typename B::V product,
                                      typename B::V result) {
    if constexpr (Kind <= 2)
        return B::nonzero(B::notOrdinary(comps, result), result);
    else if constexpr (Kind == 3 && ConstB == 0)
        return B::none(); // zero times a normalized value is an exact zero
    else {
        typename B::M nz; // components without a zero operand
        if constexpr (ConstB == 0)
            nz = B::none();
        else if constexpr (ConstB == 1)
            nz = B::nonzero(comps, vs);
        else
            nz = B::nonzero(B::nonzero(comps, vs), b);
        if constexpr (Kind == 3)
            return B::notOrdinary(nz, result);
        else {
            const typename B::M t = B::notOrdinary(comps, result);
            return B::mor(B::mor(B::mand(t, nz), B::nonzero(t, result)), B::notOrdinary(nz, product));
        }
    }
}

} // namespace Vu1Lanes
#undef VU_LANES_INLINE
