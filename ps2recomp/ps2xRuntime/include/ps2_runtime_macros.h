#ifndef PS2_RUNTIME_MACROS_H
#define PS2_RUNTIME_MACROS_H
#include <cstdint>
#include <cmath>
#include <cstring>
#include <bit>
#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(USE_SSE2NEON)
#include "sse2neon.h"
#else
#include <immintrin.h> // For SSE/AVX intrinsics
#endif

#include "ps2_runtime.h"

static inline int32_t Ps2ExtractEpi32(__m128i v, int index) {
    switch (index & 3) {
        case 0:
            return _mm_extract_epi32(v, 0);
        case 1:
            return _mm_extract_epi32(v, 1);
        case 2:
            return _mm_extract_epi32(v, 2);
        default:
            return _mm_extract_epi32(v, 3);
    }
}

static inline int64_t Ps2ExtractEpi64(__m128i v, int index) {
    if ((index & 1) == 0) {
        return _mm_cvtsi128_si64(v);
    } else {
        return _mm_extract_epi64(v, 1);
    }
}

static inline uint32_t ps2_clz32(uint32_t x) {
    return static_cast<uint32_t>(std::countl_zero(x));
}

static inline uint64_t Ps2HiLoToU64(uint64_t hi, uint64_t lo) {
    return ((hi & 0xFFFFFFFFull) << 32) | (lo & 0xFFFFFFFFull);
}

static inline uint64_t Ps2SignExt32ToU64(uint32_t v) {
    return (uint64_t)(int64_t)(int32_t)v;
}

// PLZCW: Count leading bits that match the sign bit, minus 1.
// For positive values: count leading zeros minus 1 (excludes sign bit).
// For negative values: count leading ones minus 1 (excludes sign bit).
// Special cases: 0x00000000 -> 31, 0xFFFFFFFF -> 31.
static inline uint32_t ps2_plzcw32(uint32_t x) {
    if (x == 0 || x == 0xFFFFFFFF)
        return 31;
    if (x & 0x80000000u)
        x = ~x; // If sign bit set, invert to count leading ones as zeros
    return static_cast<uint32_t>(std::countl_zero(x)) - 1;
}

#define PS2_BLENDV_PS(a, b, mask) _mm_blendv_ps((a), (b), (mask))
#define PS2_MIN_EPI32(a, b) _mm_min_epi32((a), (b))
#define PS2_MAX_EPI32(a, b) _mm_max_epi32((a), (b))
#define PS2_SHUFFLE_EPI8(v, mask) _mm_shuffle_epi8((v), (mask))

#define PS2_EXTRACT_EPI32(v, i) Ps2ExtractEpi32((v), (i))
#define PS2_EXTRACT_EPI64(v, i) Ps2ExtractEpi64((v), (i))

#define PS2_EXTRACT_EPI32_0(v) Ps2ExtractEpi32((v), 0)
#define PS2_EXTRACT_EPI32_1(v) Ps2ExtractEpi32((v), 1)
#define PS2_EXTRACT_EPI32_2(v) Ps2ExtractEpi32((v), 2)
#define PS2_EXTRACT_EPI32_3(v) Ps2ExtractEpi32((v), 3)

#define PS2_EXTRACT_EPI64_0(v) Ps2ExtractEpi64((v), 0)
#define PS2_EXTRACT_EPI64_1(v) Ps2ExtractEpi64((v), 1)

// Basic MIPS arithmetic operations
#define ADD32(a, b) ((uint32_t)((a) + (b)))
#define ADD32_OV(rs, rt, result32, overflow)                                                                           \
    do {                                                                                                               \
        int32_t _a = (int32_t)(rs);                                                                                    \
        int32_t _b = (int32_t)(rt);                                                                                    \
        int32_t _r = _a + _b;                                                                                          \
        overflow = (((_a ^ _b) >= 0) && ((_a ^ _r) < 0));                                                              \
        result32 = (uint32_t)_r;                                                                                       \
    } while (0);
#define SUB32(a, b) ((uint32_t)((a) - (b)))
#define SUB32_OV(rs, rt, result32, overflow)                                                                           \
    do {                                                                                                               \
        int32_t _a = (int32_t)(rs);                                                                                    \
        int32_t _b = (int32_t)(rt);                                                                                    \
        int32_t _r = _a - _b;                                                                                          \
        overflow = (((_a ^ _b) < 0) && ((_a ^ _r) < 0));                                                               \
        result32 = (uint32_t)_r;                                                                                       \
    } while (0);
#define MUL32(a, b) ((uint32_t)((a) * (b)))
#define DIV32(a, b) ((uint32_t)((a) / (b)))
#define AND32(a, b) ((uint32_t)((a) & (b)))
#define OR32(a, b) ((uint32_t)((a) | (b)))
#define XOR32(a, b) ((uint32_t)((a) ^ (b)))
#define NOR32(a, b) ((uint32_t)(~((a) | (b))))
#define SLL32(a, b) ((uint32_t)((a) << (b)))
#define SRL32(a, b) ((uint32_t)((a) >> (b)))
#define SRA32(a, b) ((uint32_t)((int32_t)(a) >> (b)))
#define SLT32(a, b) ((uint32_t)((int32_t)(a) < (int32_t)(b) ? 1 : 0))
#define SLTU32(a, b) ((uint32_t)((a) < (b) ? 1 : 0))

// PS2-specific 128-bit MMI operations
#define PS2_PEXTLW(a, b) _mm_unpacklo_epi32((__m128i)(b), (__m128i)(a))
#define PS2_PEXTUW(a, b) _mm_unpackhi_epi32((__m128i)(b), (__m128i)(a))
#define PS2_PEXTLH(a, b) _mm_unpacklo_epi16((__m128i)(b), (__m128i)(a))
#define PS2_PEXTUH(a, b) _mm_unpackhi_epi16((__m128i)(b), (__m128i)(a))
#define PS2_PEXTLB(a, b) _mm_unpacklo_epi8((__m128i)(b), (__m128i)(a))
#define PS2_PEXTUB(a, b) _mm_unpackhi_epi8((__m128i)(b), (__m128i)(a))
#define PS2_PADDW(a, b) _mm_add_epi32((__m128i)(a), (__m128i)(b))
#define PS2_PSUBW(a, b) _mm_sub_epi32((__m128i)(a), (__m128i)(b))
#define PS2_PMAXW(a, b) PS2_MAX_EPI32((__m128i)(a), (__m128i)(b))
#define PS2_PMINW(a, b) PS2_MIN_EPI32((__m128i)(a), (__m128i)(b))
#define PS2_PADDH(a, b) _mm_add_epi16((__m128i)(a), (__m128i)(b))
#define PS2_PSUBH(a, b) _mm_sub_epi16((__m128i)(a), (__m128i)(b))
#define PS2_PMAXH(a, b) _mm_max_epi16((__m128i)(a), (__m128i)(b))
#define PS2_PMINH(a, b) _mm_min_epi16((__m128i)(a), (__m128i)(b))
#define PS2_PADDB(a, b) _mm_add_epi8((__m128i)(a), (__m128i)(b))
#define PS2_PSUBB(a, b) _mm_sub_epi8((__m128i)(a), (__m128i)(b))
#define PS2_PAND(a, b) _mm_and_si128((__m128i)(a), (__m128i)(b))
#define PS2_POR(a, b) _mm_or_si128((__m128i)(a), (__m128i)(b))
#define PS2_PXOR(a, b) _mm_xor_si128((__m128i)(a), (__m128i)(b))
#define PS2_PNOR(a, b) _mm_xor_si128(_mm_or_si128((__m128i)(a), (__m128i)(b)), _mm_set1_epi32(0xFFFFFFFF))

// PS2 float semantics (EE FPU and VU0 macro mode). The PS2 format has no Inf, NaN or denormals:
//   - exponent 255 is an ordinary exponent, so 0x7F800000 is 2^128 and the largest value is 0x7FFFFFFF;
//   - exponent 0 is zero whatever the mantissa holds;
//   - a result of 2^129 or more saturates to +-0x7FFFFFFF, one below 2^-126 becomes +-0;
//   - x/0 gives +-0x7FFFFFFF.
// A host float cannot hold the exponent-255 values, so anything that touches them is computed in double.
// That path is rare and kept out of line; the inline part is the host operation and a range check.
// One case is left to the host: ADD.S/SUB.S/MUL.S and the vector operations do not look for an operand with
// exponent 0 and a non-zero mantissa, so it takes part with its tiny host value instead of as zero.
// Checked against the hardware results in ps2autotests (tests/cpu/ee_fpu).
#if defined(_MSC_VER)
#define PS2_FLOAT_NOINLINE __declspec(noinline)
#else
#define PS2_FLOAT_NOINLINE __attribute__((noinline))
#endif

constexpr uint32_t PS2_FLOAT_MAX_BITS = 0x7FFFFFFFu;
constexpr uint32_t PS2_FLOAT_EXP_MASK = 0x7F800000u;
constexpr uint32_t PS2_FLOAT_SIGN_MASK = 0x80000000u;

// True when a host float operation cannot be trusted with x: exponent 255, or exponent 0 with a mantissa.
inline bool ps2_fspecial(float x) {
    const uint32_t t = std::bit_cast<uint32_t>(x) & 0x7FFFFFFFu;
    return t != 0u && (t - 0x00800000u) >= 0x7F000000u;
}

inline double ps2_to_double(float x) {
    uint32_t b = std::bit_cast<uint32_t>(x);
    const uint32_t exponent = b & PS2_FLOAT_EXP_MASK;
    if (exponent == 0u)
        return static_cast<double>(std::bit_cast<float>(b & PS2_FLOAT_SIGN_MASK));
    if (exponent == PS2_FLOAT_EXP_MASK)
        return static_cast<double>(std::bit_cast<float>(b - 0x00800000u)) * 2.0;
    return static_cast<double>(x);
}

inline float ps2_from_double(double d) {
    const double magnitude = std::fabs(d);
    const uint32_t sign = std::signbit(d) ? PS2_FLOAT_SIGN_MASK : 0u;
    if (magnitude >= 0x1p129)
        return std::bit_cast<float>(sign | PS2_FLOAT_MAX_BITS);
    if (magnitude < 0x1p-126)
        return std::bit_cast<float>(sign);
    if (magnitude >= 0x1p128) {
        // Convert one exponent lower, then put the exponent back as 255.
        const uint32_t b = std::bit_cast<uint32_t>(static_cast<float>(d * 0.5));
        if ((b & PS2_FLOAT_EXP_MASK) == PS2_FLOAT_EXP_MASK)
            return std::bit_cast<float>(sign | PS2_FLOAT_MAX_BITS);
        return std::bit_cast<float>(b + 0x00800000u);
    }
    // A value that rounds up to 2^128 comes out as the host's Inf pattern, which is 2^128 on the PS2.
    return static_cast<float>(d);
}

PS2_FLOAT_NOINLINE inline float ps2_fadd_slow(float a, float b) {
    // Zero operands and results end up here too; the host result is right for them.
    const float r = a + b;
    if (!(ps2_fspecial(a) | ps2_fspecial(b) | ps2_fspecial(r)))
        return r;
    return ps2_from_double(ps2_to_double(a) + ps2_to_double(b));
}

PS2_FLOAT_NOINLINE inline float ps2_fsub_slow(float a, float b) {
    // Zero operands and results end up here too; the host result is right for them.
    const float r = a - b;
    if (!(ps2_fspecial(a) | ps2_fspecial(b) | ps2_fspecial(r)))
        return r;
    return ps2_from_double(ps2_to_double(a) - ps2_to_double(b));
}

PS2_FLOAT_NOINLINE inline float ps2_fmul_slow(float a, float b) {
    // Zero operands and results end up here too; the host result is right for them.
    const float r = a * b;
    if (!(ps2_fspecial(a) | ps2_fspecial(b) | ps2_fspecial(r)))
        return r;
    return ps2_from_double(ps2_to_double(a) * ps2_to_double(b));
}

// A zero divisor gives +-max with the sign of fs xor ft.
PS2_FLOAT_NOINLINE inline float ps2_fdiv_slow(float a, float b) {
    const uint32_t ua = std::bit_cast<uint32_t>(a);
    const uint32_t ub = std::bit_cast<uint32_t>(b);
    if ((ub & PS2_FLOAT_EXP_MASK) == 0u)
        return std::bit_cast<float>(((ua ^ ub) & PS2_FLOAT_SIGN_MASK) | PS2_FLOAT_MAX_BITS);
    const float r = a / b;
    if (!(ps2_fspecial(a) | ps2_fspecial(b) | ps2_fspecial(r)))
        return r;
    return ps2_from_double(ps2_to_double(a) / ps2_to_double(b));
}

// sqrt(|ft|); the result is never negative, sqrt(-0) is +0.
PS2_FLOAT_NOINLINE inline float ps2_fsqrt_slow(float a) {
    if ((std::bit_cast<uint32_t>(a) & PS2_FLOAT_EXP_MASK) == 0u)
        return 0.0f;
    return ps2_from_double(std::sqrt(std::fabs(ps2_to_double(a))));
}

// fs / sqrt(|ft|). The sign of ft is ignored everywhere, so a zero ft gives +-max with the sign of fs alone.
PS2_FLOAT_NOINLINE inline float ps2_frsqrt_slow(float a, float b) {
    if ((std::bit_cast<uint32_t>(b) & PS2_FLOAT_EXP_MASK) == 0u)
        return std::bit_cast<float>((std::bit_cast<uint32_t>(a) & PS2_FLOAT_SIGN_MASK) | PS2_FLOAT_MAX_BITS);
    // The root is rounded to a float before the division, as on the hardware.
    const float root = ps2_from_double(std::sqrt(std::fabs(ps2_to_double(b))));
    return ps2_from_double(ps2_to_double(a) / static_cast<double>(root));
}

// True when a, b and the host result r are all normal numbers, so r is also the PS2 result.
inline bool ps2_fall_normal(float a, float b, float r) {
    // Doubling drops the sign; adding one exponent step wraps exponent 255 round to 0 next to exponent 0.
    const uint32_t sa = std::bit_cast<uint32_t>(a) * 2u + 0x01000000u;
    const uint32_t sb = std::bit_cast<uint32_t>(b) * 2u + 0x01000000u;
    const uint32_t sr = std::bit_cast<uint32_t>(r) * 2u + 0x01000000u;
    uint32_t lowest = sa < sb ? sa : sb;
    lowest = lowest < sr ? lowest : sr;
    return lowest >= 0x02000000u;
}

// The same test on the result alone. An exponent-255 operand always leaves exponent 255 in the host result
// of an addition, subtraction or multiplication (Inf or NaN), so this finds those operands as well.
inline bool ps2_fnormal(float r) {
    return (std::bit_cast<uint32_t>(r) * 2u + 0x01000000u) >= 0x02000000u;
}

inline float ps2_fadd(float a, float b) {
    const float r = a + b;
    return ps2_fnormal(r) ? r : ps2_fadd_slow(a, b);
}

inline float ps2_fsub(float a, float b) {
    const float r = a - b;
    return ps2_fnormal(r) ? r : ps2_fsub_slow(a, b);
}

inline float ps2_fmul(float a, float b) {
    const float r = a * b;
    return ps2_fnormal(r) ? r : ps2_fmul_slow(a, b);
}

// CVT.W.S: the R5900 FPU has a single rounding mode, round toward zero, and no NaN/Inf: an operand with |x| >= 2^31
// (exponent field > 0x9d, which includes the 0xff "Inf/NaN" encodings) saturates to 0x7fffffff / 0x80000000. The
// emitter macro used nearbyintf() (round to nearest even), so 63.6f became 64 instead of 63. A plain (int32_t) cast is
// not enough for the saturation: on x86 an out of range cast yields 0x80000000 for large positive values.
inline int32_t ps2_cvt_w_s(float f) {
    const uint32_t u = std::bit_cast<uint32_t>(f);
    if ((u & 0x7F800000u) <= 0x4E800000u)
        return static_cast<int32_t>(f);
    return (u & 0x80000000u) ? INT32_MIN : INT32_MAX;
}

inline float ps2_fdiv(float a, float b) {
    const float r = a / b;
    return ps2_fall_normal(a, b, r) ? r : ps2_fdiv_slow(a, b);
}

inline float ps2_fsqrt(float a) {
    const float r = std::sqrt(a);
    return ps2_fall_normal(a, a, r) ? r : ps2_fsqrt_slow(a);
}

inline float ps2_frsqrt(float a, float b) {
    const float r = a / std::sqrt(b);
    return ps2_fall_normal(a, b, r) ? r : ps2_frsqrt_slow(a, b);
}

// VU divide unit (DIV / SQRT / RSQRT -> Q). The VU has no exponent-255 numbers: overflow and division by zero give the
// largest finite value 0x7F7FFFFF with the sign of the result (the EE FPU gives 0x7FFFFFFF for x/0).
inline float ps2_vu_clamp(float r) {
    uint32_t b = std::bit_cast<uint32_t>(r);
    if ((b & PS2_FLOAT_EXP_MASK) == PS2_FLOAT_EXP_MASK)
        b = (b & PS2_FLOAT_SIGN_MASK) | 0x7F7FFFFFu;
    return std::bit_cast<float>(b);
}
inline float ps2_vu_div(float a, float b) {
    const uint32_t ua = std::bit_cast<uint32_t>(a), ub = std::bit_cast<uint32_t>(b);
    if ((ub & PS2_FLOAT_EXP_MASK) == 0u)
        return std::bit_cast<float>(((ua ^ ub) & PS2_FLOAT_SIGN_MASK) | 0x7F7FFFFFu);
    return ps2_vu_clamp(ps2_fdiv(a, b));
}
inline float ps2_vu_sqrt(float b) {
    return ps2_vu_clamp(ps2_fsqrt(std::fabs(b)));
}
inline float ps2_vu_rsqrt(float a, float b) {
    const uint32_t ua = std::bit_cast<uint32_t>(a), ub = std::bit_cast<uint32_t>(b);
    if ((ub & PS2_FLOAT_EXP_MASK) == 0u)
        return std::bit_cast<float>((ua & PS2_FLOAT_SIGN_MASK) | 0x7F7FFFFFu);
    return ps2_vu_clamp(ps2_frsqrt(a, std::fabs(b)));
}

enum class Ps2VecOp { Add, Sub, Mul };

// Lane by lane through the scalar helpers; only reached when a result lane has exponent 255.
PS2_FLOAT_NOINLINE inline __m128 ps2_vslow(__m128 a, __m128 b, Ps2VecOp op) {
    alignas(16) float la[4], lb[4], lr[4];
    _mm_store_ps(la, a);
    _mm_store_ps(lb, b);
    for (int i = 0; i < 4; ++i) {
        switch (op) {
            case Ps2VecOp::Add:
                lr[i] = ps2_fadd_slow(la[i], lb[i]);
                break;
            case Ps2VecOp::Sub:
                lr[i] = ps2_fsub_slow(la[i], lb[i]);
                break;
            default:
                lr[i] = ps2_fmul_slow(la[i], lb[i]);
                break;
        }
    }
    return _mm_load_ps(lr);
}

// r is the host result of a op b. As in ps2_fnormal, testing r alone finds every lane that needs the double
// path. Results with exponent 0 are flushed to +-0.
inline __m128 ps2_vfinish(__m128 a, __m128 b, __m128 r, Ps2VecOp op) {
    const __m128i expMask = _mm_set1_epi32(0x7F800000);
    const __m128i rexp = _mm_and_si128(_mm_castps_si128(r), expMask);
    if (_mm_movemask_epi8(_mm_cmpeq_epi32(rexp, expMask)) != 0)
        return ps2_vslow(a, b, op);
    const __m128i flush = _mm_and_si128(_mm_cmpeq_epi32(rexp, _mm_setzero_si128()), _mm_set1_epi32(0x7FFFFFFF));
    return _mm_castsi128_ps(_mm_andnot_si128(flush, _mm_castps_si128(r)));
}

inline __m128 ps2_vadd(__m128 a, __m128 b) {
    return ps2_vfinish(a, b, _mm_add_ps(a, b), Ps2VecOp::Add);
}

inline __m128 ps2_vsub(__m128 a, __m128 b) {
    return ps2_vfinish(a, b, _mm_sub_ps(a, b), Ps2VecOp::Sub);
}

inline __m128 ps2_vmul(__m128 a, __m128 b) {
    return ps2_vfinish(a, b, _mm_mul_ps(a, b), Ps2VecOp::Mul);
}

// PS2 VU (Vector Unit) operations
#define PS2_VADD(a, b) ps2_vadd((__m128)(a), (__m128)(b))
#define PS2_VSUB(a, b) ps2_vsub((__m128)(a), (__m128)(b))
#define PS2_VMUL(a, b) ps2_vmul((__m128)(a), (__m128)(b))
#define PS2_VDIV(a, b) _mm_div_ps((__m128)(a), (__m128)(b))
#define PS2_VMULQ(a, q) _mm_mul_ps((__m128)(a), _mm_set1_ps(q))
#define PS2_VBLEND(a, b, mask) PS2_BLENDV_PS((__m128)(a), (__m128)(b), (__m128)(mask))

// Memory access helpers - Hybrid Fast/Slow Path
// Fast path: Direct RDRAM access (masked).
// Slow path: Full runtime->Load/Store

static inline bool Ps2FastRangeIsContiguous(uint32_t offset, uint32_t bytes) {
    return offset <= (PS2_RAM_SIZE - bytes);
}

static inline uint8_t Ps2FastRead8(const uint8_t* rdram, uint32_t addr) {
    return rdram[addr & PS2_RAM_MASK];
}

static inline uint16_t Ps2FastRead16(const uint8_t* rdram, uint32_t addr) {
    const uint32_t offset = addr & PS2_RAM_MASK;
    if (!Ps2FastRangeIsContiguous(offset, sizeof(uint16_t))) {
        uint8_t wrapped[sizeof(uint16_t)];
        for (uint32_t i = 0; i < sizeof(uint16_t); ++i) {
            wrapped[i] = rdram[(offset + i) & PS2_RAM_MASK];
        }
        uint16_t value;
        std::memcpy(&value, wrapped, sizeof(value));
        return value;
    }

    uint16_t value;
    std::memcpy(&value, rdram + offset, sizeof(value));
    return value;
}

static inline uint32_t Ps2FastRead32(const uint8_t* rdram, uint32_t addr) {
    const uint32_t offset = addr & PS2_RAM_MASK;
    if (!Ps2FastRangeIsContiguous(offset, sizeof(uint32_t))) {
        uint8_t wrapped[sizeof(uint32_t)];
        for (uint32_t i = 0; i < sizeof(uint32_t); ++i) {
            wrapped[i] = rdram[(offset + i) & PS2_RAM_MASK];
        }
        uint32_t value;
        std::memcpy(&value, wrapped, sizeof(value));
        return value;
    }

    uint32_t value;
    std::memcpy(&value, rdram + offset, sizeof(value));
    return value;
}

static inline uint64_t Ps2FastRead64(const uint8_t* rdram, uint32_t addr) {
    const uint32_t offset = addr & PS2_RAM_MASK;
    if (!Ps2FastRangeIsContiguous(offset, sizeof(uint64_t))) {
        uint8_t wrapped[sizeof(uint64_t)];
        for (uint32_t i = 0; i < sizeof(uint64_t); ++i) {
            wrapped[i] = rdram[(offset + i) & PS2_RAM_MASK];
        }
        uint64_t value;
        std::memcpy(&value, wrapped, sizeof(value));
        return value;
    }

    uint64_t value;
    std::memcpy(&value, rdram + offset, sizeof(value));
    return value;
}

static inline __m128i Ps2FastRead128(const uint8_t* rdram, uint32_t addr) {
    const uint32_t offset = addr & PS2_RAM_MASK;
    if (!Ps2FastRangeIsContiguous(offset, sizeof(__m128i))) {
        alignas(16) uint8_t wrapped[sizeof(__m128i)];
        for (uint32_t i = 0; i < sizeof(__m128i); ++i) {
            wrapped[i] = rdram[(offset + i) & PS2_RAM_MASK];
        }
        __m128i value;
        std::memcpy(&value, wrapped, sizeof(value));
        return value;
    }

    __m128i value;
    std::memcpy(&value, rdram + offset, sizeof(value));
    return value;
}

static inline void Ps2FastWrite8(uint8_t* rdram, uint32_t addr, uint8_t value) {
    rdram[addr & PS2_RAM_MASK] = value;
}

static inline void Ps2FastWrite16(uint8_t* rdram, uint32_t addr, uint16_t value) {
    const uint32_t offset = addr & PS2_RAM_MASK;
    if (!Ps2FastRangeIsContiguous(offset, sizeof(uint16_t))) {
        uint8_t wrapped[sizeof(uint16_t)];
        std::memcpy(wrapped, &value, sizeof(value));
        for (uint32_t i = 0; i < sizeof(uint16_t); ++i) {
            rdram[(offset + i) & PS2_RAM_MASK] = wrapped[i];
        }
        return;
    }
    std::memcpy(rdram + offset, &value, sizeof(value));
}

static inline void Ps2FastWrite32(uint8_t* rdram, uint32_t addr, uint32_t value) {
    const uint32_t offset = addr & PS2_RAM_MASK;
    if (!Ps2FastRangeIsContiguous(offset, sizeof(uint32_t))) {
        uint8_t wrapped[sizeof(uint32_t)];
        std::memcpy(wrapped, &value, sizeof(value));
        for (uint32_t i = 0; i < sizeof(uint32_t); ++i) {
            rdram[(offset + i) & PS2_RAM_MASK] = wrapped[i];
        }
        return;
    }
    std::memcpy(rdram + offset, &value, sizeof(value));
}

static inline void Ps2FastWrite64(uint8_t* rdram, uint32_t addr, uint64_t value) {
    const uint32_t offset = addr & PS2_RAM_MASK;
    if (!Ps2FastRangeIsContiguous(offset, sizeof(uint64_t))) {
        uint8_t wrapped[sizeof(uint64_t)];
        std::memcpy(wrapped, &value, sizeof(value));
        for (uint32_t i = 0; i < sizeof(uint64_t); ++i) {
            rdram[(offset + i) & PS2_RAM_MASK] = wrapped[i];
        }
        return;
    }
    std::memcpy(rdram + offset, &value, sizeof(value));
}

static inline void Ps2FastWrite128(uint8_t* rdram, uint32_t addr, __m128i value) {
    const uint32_t offset = addr & PS2_RAM_MASK;
    if (!Ps2FastRangeIsContiguous(offset, sizeof(__m128i))) {
        alignas(16) uint8_t wrapped[sizeof(__m128i)];
        std::memcpy(wrapped, &value, sizeof(value));
        for (uint32_t i = 0; i < sizeof(__m128i); ++i) {
            rdram[(offset + i) & PS2_RAM_MASK] = wrapped[i];
        }
        return;
    }
    std::memcpy(rdram + offset, &value, sizeof(value));
}

#define FAST_READ8(addr) Ps2FastRead8(rdram, (uint32_t)(addr))
#define FAST_READ16(addr) Ps2FastRead16(rdram, (uint32_t)(addr))
#define FAST_READ32(addr) Ps2FastRead32(rdram, (uint32_t)(addr))
#define FAST_READ64(addr) Ps2FastRead64(rdram, (uint32_t)(addr))
#define FAST_READ128(addr) Ps2FastRead128(rdram, (uint32_t)(addr))

#define FAST_WRITE8(addr, val) Ps2FastWrite8(rdram, (uint32_t)(addr), (uint8_t)(val))
#define FAST_WRITE16(addr, val) Ps2FastWrite16(rdram, (uint32_t)(addr), (uint16_t)(val))
#define FAST_WRITE32(addr, val) Ps2FastWrite32(rdram, (uint32_t)(addr), (uint32_t)(val))
#define FAST_WRITE64(addr, val) Ps2FastWrite64(rdram, (uint32_t)(addr), (uint64_t)(val))
#define FAST_WRITE128(addr, val) Ps2FastWrite128(rdram, (uint32_t)(addr), (val))

// ---- Guest memory access (Drakan perf: no lambdas, one inline RDRAM test, everything else out of line) ----
// Fast path: the address maps (KSEG0/1/uncached mirror/physical) into the first 32 MB - 16 bytes of RDRAM, so a plain
// masked load/store is exact (no wrap-around possible). Everything else (scratchpad, I/O registers, GS, VU memory,
// BIOS, KSEG2/3, the last 16 bytes of RDRAM) takes the former path through PS2Runtime::LoadN/StoreN.
#if defined(_MSC_VER)
#define PS2_FORCEINLINE __forceinline
#define PS2_NOINLINE __declspec(noinline)
#else
#define PS2_FORCEINLINE inline __attribute__((always_inline))
#define PS2_NOINLINE __attribute__((noinline))
#endif

constexpr uint32_t PS2_FAST_RAM_LIMIT = PS2_RAM_SIZE - 16u;

static PS2_FORCEINLINE bool Ps2InFastRam(uint32_t a) {
    return ((a & 0x1FFFFFFFu) < PS2_FAST_RAM_LIMIT) & (a < 0xC0000000u);
}

PS2_NOINLINE inline uint8_t Ps2Rd8Slow(uint8_t* rdram, PS2Runtime* rt, R5900Context* ctx, uint32_t a) {
    return PS2Runtime::isSpecialAddress(a) ? rt->Load8(rdram, ctx, a) : Ps2FastRead8(rdram, a);
}
PS2_NOINLINE inline uint16_t Ps2Rd16Slow(uint8_t* rdram, PS2Runtime* rt, R5900Context* ctx, uint32_t a) {
    return PS2Runtime::isSpecialAddress(a) ? rt->Load16(rdram, ctx, a) : Ps2FastRead16(rdram, a);
}
PS2_NOINLINE inline uint32_t Ps2Rd32Slow(uint8_t* rdram, PS2Runtime* rt, R5900Context* ctx, uint32_t a) {
    return PS2Runtime::isSpecialAddress(a) ? rt->Load32(rdram, ctx, a) : Ps2FastRead32(rdram, a);
}
PS2_NOINLINE inline uint64_t Ps2Rd64Slow(uint8_t* rdram, PS2Runtime* rt, R5900Context* ctx, uint32_t a) {
    return PS2Runtime::isSpecialAddress(a) ? rt->Load64(rdram, ctx, a) : Ps2FastRead64(rdram, a);
}
PS2_NOINLINE inline __m128i Ps2Rd128Slow(uint8_t* rdram, PS2Runtime* rt, R5900Context* ctx, uint32_t a) {
    return PS2Runtime::isSpecialAddress(a) ? rt->Load128(rdram, ctx, a) : Ps2FastRead128(rdram, a);
}
PS2_NOINLINE inline void Ps2Wr8Slow(uint8_t* rdram, PS2Runtime* rt, R5900Context* ctx, uint32_t a, uint8_t v) {
    if (PS2Runtime::isSpecialAddress(a))
        rt->Store8(rdram, ctx, a, v);
    else
        Ps2FastWrite8(rdram, a, v);
}
PS2_NOINLINE inline void Ps2Wr16Slow(uint8_t* rdram, PS2Runtime* rt, R5900Context* ctx, uint32_t a, uint16_t v) {
    if (PS2Runtime::isSpecialAddress(a))
        rt->Store16(rdram, ctx, a, v);
    else
        Ps2FastWrite16(rdram, a, v);
}
PS2_NOINLINE inline void Ps2Wr32Slow(uint8_t* rdram, PS2Runtime* rt, R5900Context* ctx, uint32_t a, uint32_t v) {
    if (PS2Runtime::isSpecialAddress(a))
        rt->Store32(rdram, ctx, a, v);
    else
        Ps2FastWrite32(rdram, a, v);
}
PS2_NOINLINE inline void Ps2Wr64Slow(uint8_t* rdram, PS2Runtime* rt, R5900Context* ctx, uint32_t a, uint64_t v) {
    if (PS2Runtime::isSpecialAddress(a))
        rt->Store64(rdram, ctx, a, v);
    else
        Ps2FastWrite64(rdram, a, v);
}
PS2_NOINLINE inline void Ps2Wr128Slow(uint8_t* rdram, PS2Runtime* rt, R5900Context* ctx, uint32_t a, __m128i v) {
    if (PS2Runtime::isSpecialAddress(a))
        rt->Store128(rdram, ctx, a, v);
    else
        Ps2FastWrite128(rdram, a, v);
}

#define PS2_RD_FN(NAME, T, SLOW)                                                                                       \
    static PS2_FORCEINLINE T NAME(uint8_t* rdram, PS2Runtime* rt, R5900Context* ctx, uint32_t a) {                     \
        if (Ps2InFastRam(a)) {                                                                                         \
            T v;                                                                                                       \
            std::memcpy(&v, rdram + (a & PS2_RAM_MASK), sizeof(T));                                                    \
            return v;                                                                                                  \
        }                                                                                                              \
        return SLOW(rdram, rt, ctx, a);                                                                                \
    }
PS2_RD_FN(Ps2Rd8, uint8_t, Ps2Rd8Slow)
PS2_RD_FN(Ps2Rd16, uint16_t, Ps2Rd16Slow)
PS2_RD_FN(Ps2Rd32, uint32_t, Ps2Rd32Slow)
PS2_RD_FN(Ps2Rd64, uint64_t, Ps2Rd64Slow)
PS2_RD_FN(Ps2Rd128, __m128i, Ps2Rd128Slow)
#undef PS2_RD_FN

#define PS2_WR_FN(NAME, T, SLOW)                                                                                       \
    static PS2_FORCEINLINE void NAME(uint8_t* rdram, PS2Runtime* rt, R5900Context* ctx, uint32_t a, T v) {             \
        if (Ps2InFastRam(a)) {                                                                                         \
            std::memcpy(rdram + (a & PS2_RAM_MASK), &v, sizeof(T));                                                    \
            return;                                                                                                    \
        }                                                                                                              \
        SLOW(rdram, rt, ctx, a, v);                                                                                    \
    }
PS2_WR_FN(Ps2Wr8, uint8_t, Ps2Wr8Slow)
PS2_WR_FN(Ps2Wr16, uint16_t, Ps2Wr16Slow)
PS2_WR_FN(Ps2Wr32, uint32_t, Ps2Wr32Slow)
PS2_WR_FN(Ps2Wr64, uint64_t, Ps2Wr64Slow)
PS2_WR_FN(Ps2Wr128, __m128i, Ps2Wr128Slow)
#undef PS2_WR_FN

#define READ8(addr) Ps2Rd8(rdram, runtime, ctx, (uint32_t)(addr))
#define READ16(addr) Ps2Rd16(rdram, runtime, ctx, (uint32_t)(addr))
#define READ32(addr) Ps2Rd32(rdram, runtime, ctx, (uint32_t)(addr))
#define READ64(addr) Ps2Rd64(rdram, runtime, ctx, (uint32_t)(addr))
#define READ128(addr) Ps2Rd128(rdram, runtime, ctx, (uint32_t)(addr))

#define WRITE8(addr, val) Ps2Wr8(rdram, runtime, ctx, (uint32_t)(addr), (uint8_t)(val))
#define WRITE16(addr, val) Ps2Wr16(rdram, runtime, ctx, (uint32_t)(addr), (uint16_t)(val))
#define WRITE32(addr, val) Ps2Wr32(rdram, runtime, ctx, (uint32_t)(addr), (uint32_t)(val))
#define WRITE64(addr, val) Ps2Wr64(rdram, runtime, ctx, (uint32_t)(addr), (uint64_t)(val))
#define WRITE128(addr, val) Ps2Wr128(rdram, runtime, ctx, (uint32_t)(addr), (val))

// Packed Compare Greater Than (PCGT)
#define PS2_PCGTW(a, b) _mm_cmpgt_epi32((__m128i)(a), (__m128i)(b))
#define PS2_PCGTH(a, b) _mm_cmpgt_epi16((__m128i)(a), (__m128i)(b))
#define PS2_PCGTB(a, b) _mm_cmpgt_epi8((__m128i)(a), (__m128i)(b))

// Packed Add with Signed Saturation Word (PADDSW)
inline __m128i ps2_paddsw(__m128i a, __m128i b) {

    __m128i sum = _mm_add_epi32(a, b);
    // Check for over/underflow. Clamp to either INT32_MIN/INT32_MAX.
    __m128i overflow = _mm_and_si128(_mm_xor_si128(a, sum), _mm_xor_si128(b, sum));
    // Extract input sign.
    overflow = _mm_srai_epi32(overflow, 31);
    __m128i input_sign = _mm_srai_epi32(a, 31);
// Select saturation value based on overflow sign.
#if defined(__SSE4_1__)
    __m128i sat = _mm_blendv_epi8(_mm_set1_epi32(INT32_MAX), _mm_set1_epi32(INT32_MIN), input_sign);
    return _mm_blendv_epi8(sum, sat, overflow);
#else
    __m128i sat = _mm_or_si128(_mm_and_si128(input_sign, _mm_set1_epi32(INT32_MIN)),
                               _mm_andnot_si128(input_sign, _mm_set1_epi32(INT32_MAX)));
    return _mm_or_si128(_mm_and_si128(overflow, sat), _mm_andnot_si128(overflow, sum));
#endif
}
#define PS2_PADDSW(a, b) ps2_paddsw((__m128i)(a), (__m128i)(b))

// Packed Subtract with Signed Saturation Word (PSUBSW)
inline __m128i ps2_psubsw(__m128i a, __m128i b) {
    __m128i diff = _mm_sub_epi32(a, b);
    // Check for over/underflow. Clamp to either INT32_MIN/INT32_MAX.
    __m128i overflow = _mm_and_si128(_mm_xor_si128(a, b), _mm_xor_si128(a, diff));
    // Extract input sign.
    overflow = _mm_srai_epi32(overflow, 31);
    __m128i input_sign = _mm_srai_epi32(a, 31);
// Select saturation value based on overflow sign.
#if defined(__SSE4_1__)
    __m128i sat = _mm_blendv_epi8(_mm_set1_epi32(INT32_MAX), _mm_set1_epi32(INT32_MIN), input_sign);
    return _mm_blendv_epi8(diff, sat, overflow);
#else
    __m128i sat = _mm_or_si128(_mm_and_si128(input_sign, _mm_set1_epi32(INT32_MIN)),
                               _mm_andnot_si128(input_sign, _mm_set1_epi32(INT32_MAX)));
    return _mm_or_si128(_mm_and_si128(overflow, sat), _mm_andnot_si128(overflow, diff));
#endif
}
#define PS2_PSUBSW(a, b) ps2_psubsw((__m128i)(a), (__m128i)(b))

// Packed Compare Equal (PCEQ)
#define PS2_PCEQW(a, b) _mm_cmpeq_epi32((__m128i)(a), (__m128i)(b))
#define PS2_PCEQH(a, b) _mm_cmpeq_epi16((__m128i)(a), (__m128i)(b))
#define PS2_PCEQB(a, b) _mm_cmpeq_epi8((__m128i)(a), (__m128i)(b))

// Packed Absolute (PABS)
#define PS2_PABSW(a) _mm_abs_epi32((__m128i)(a))
#define PS2_PABSH(a) _mm_abs_epi16((__m128i)(a))
#define PS2_PABSB(a) _mm_abs_epi8((__m128i)(a))

// Packed Pack (PPAC) - Packs larger elements into smaller ones
inline __m128i ps2_paddu32(__m128i a, __m128i b) {
    __m128i sum = _mm_add_epi32(a, b);
    __m128i overflow =
        _mm_cmpgt_epi32(_mm_xor_si128(a, _mm_set1_epi32(INT32_MIN)), _mm_xor_si128(sum, _mm_set1_epi32(INT32_MIN)));
    return _mm_or_si128(sum, overflow); // overflow lanes become all-1s
}
inline __m128i ps2_psubu32(__m128i a, __m128i b) {
    __m128i diff = _mm_sub_epi32(a, b);
    // Underflow if a < b (unsigned). Clamp to 0.
    __m128i underflow =
        _mm_cmpgt_epi32(_mm_xor_si128(b, _mm_set1_epi32(INT32_MIN)), _mm_xor_si128(a, _mm_set1_epi32(INT32_MIN)));
    return _mm_andnot_si128(underflow, diff); // underflow lanes become 0
}

inline __m128i ps2_ppacw(__m128i rs, __m128i rt) {
    // rs = [rs3 rs2 rs1 rs0], rt = [rt3 rt2 rt1 rt0]
    return _mm_castps_si128(_mm_shuffle_ps(_mm_castsi128_ps(rt), _mm_castsi128_ps(rs), _MM_SHUFFLE(2, 0, 2, 0)));
}
#define PS2_PPACW(a, b) ps2_ppacw((__m128i)(a), (__m128i)(b))

inline __m128i ps2_ppach(__m128i rs, __m128i rt) {
    const __m128i mask = _mm_setr_epi8(0, 1, 4, 5, 8, 9, 12, 13,  // from rt: halfwords 0,2,4,6
                                       0, 1, 4, 5, 8, 9, 12, 13); // from rs: halfwords 0,2,4,6
    __m128i lo = _mm_shuffle_epi8(rt, mask);
    __m128i hi = _mm_shuffle_epi8(rs, mask);
    return _mm_unpacklo_epi64(lo, hi);
}
#define PS2_PPACH(a, b) ps2_ppach((__m128i)(a), (__m128i)(b))

inline __m128i ps2_ppacb(__m128i rs, __m128i rt) {
    const __m128i mask = _mm_setr_epi8(0, 2, 4, 6, 8, 10, 12, 14,  // from rt: bytes 0,2,4,6,8,10,12,14
                                       0, 2, 4, 6, 8, 10, 12, 14); // from rs
    __m128i lo = _mm_shuffle_epi8(rt, mask);
    __m128i hi = _mm_shuffle_epi8(rs, mask);
    return _mm_unpacklo_epi64(lo, hi);
}
#define PS2_PPACB(a, b) ps2_ppacb((__m128i)(a), (__m128i)(b))

// Packed Interleave (PINT)
#define PS2_PINTH(a, b)                                                                                                \
    _mm_unpacklo_epi16(_mm_shuffle_epi32((__m128i)(b), _MM_SHUFFLE(3, 2, 1, 0)),                                       \
                       _mm_shuffle_epi32((__m128i)(a), _MM_SHUFFLE(3, 2, 1, 0)))
#define PS2_PINTEH(a, b)                                                                                               \
    _mm_unpackhi_epi16(_mm_shuffle_epi32((__m128i)(b), _MM_SHUFFLE(3, 2, 1, 0)),                                       \
                       _mm_shuffle_epi32((__m128i)(a), _MM_SHUFFLE(3, 2, 1, 0)))

// Packed Multiply-Add (PMADD)
#define PS2_PMADDW(a, b)                                                                                               \
    _mm_add_epi32(_mm_mullo_epi32(_mm_shuffle_epi32((__m128i)(a), _MM_SHUFFLE(1, 0, 3, 2)),                            \
                                  _mm_shuffle_epi32((__m128i)(b), _MM_SHUFFLE(1, 0, 3, 2))),                           \
                  _mm_mullo_epi32(_mm_shuffle_epi32((__m128i)(a), _MM_SHUFFLE(3, 2, 1, 0)),                            \
                                  _mm_shuffle_epi32((__m128i)(b), _MM_SHUFFLE(3, 2, 1, 0))))

// Packed Variable Shifts
#define PS2_PSLLVW(a, b) _mm_custom_sllv_epi32((__m128i)(a), (__m128i)(b))
#define PS2_PSRLVW(a, b) _mm_custom_srlv_epi32((__m128i)(a), (__m128i)(b))
#define PS2_PSRAVW(a, b) _mm_custom_srav_epi32((__m128i)(a), (__m128i)(b))

inline __m128i _mm_custom_sllv_epi32(__m128i a, __m128i count) {
    alignas(16) int32_t a_arr[4];
    alignas(16) int32_t count_arr[4];
    alignas(16) int32_t result[4];

    std::memcpy(a_arr, &a, sizeof(a));
    std::memcpy(count_arr, &count, sizeof(count));

    for (int i = 0; i < 4; i++) {
        result[i] = a_arr[i] << (count_arr[i] & 0x1F);
    }

    __m128i out;
    std::memcpy(&out, result, sizeof(out));
    return out;
}

inline __m128i _mm_custom_srlv_epi32(__m128i a, __m128i count) {
    int32_t a_arr[4], count_arr[4], result[4];
    _mm_storeu_si128((__m128i*)a_arr, a);
    _mm_storeu_si128((__m128i*)count_arr, count);
    for (int i = 0; i < 4; i++) {
        result[i] = (uint32_t)a_arr[i] >> (count_arr[i] & 0x1F);
    }
    return _mm_loadu_si128((__m128i*)result);
}

inline __m128i _mm_custom_srav_epi32(__m128i a, __m128i count) {
    int32_t a_arr[4], count_arr[4], result[4];
    _mm_storeu_si128((__m128i*)a_arr, a);
    _mm_storeu_si128((__m128i*)count_arr, count);
    for (int i = 0; i < 4; i++) {
        result[i] = a_arr[i] >> (count_arr[i] & 0x1F);
    }
    return _mm_loadu_si128((__m128i*)result);
}

// PMFHL function implementations
inline __m128i ps2_u64_to_epi64_pair(uint64_t value) {
    return _mm_set1_epi64x(static_cast<long long>(value));
}

#define PS2_PMFHL_LW(hi, lo) _mm_unpacklo_epi64(ps2_u64_to_epi64_pair(lo), ps2_u64_to_epi64_pair(hi))
#define PS2_PMFHL_UW(hi, lo) _mm_unpackhi_epi64(ps2_u64_to_epi64_pair(lo), ps2_u64_to_epi64_pair(hi))
#define PS2_PMFHL_SLW(hi, lo) _mm_packs_epi32(ps2_u64_to_epi64_pair(lo), ps2_u64_to_epi64_pair(hi))
#define PS2_PMFHL_LH(hi, lo)                                                                                           \
    _mm_shuffle_epi32(_mm_packs_epi32(ps2_u64_to_epi64_pair(lo), ps2_u64_to_epi64_pair(hi)), _MM_SHUFFLE(3, 1, 2, 0))
#define PS2_PMFHL_SH(hi, lo)                                                                                           \
    _mm_shufflehi_epi16(_mm_shufflelo_epi16(_mm_packs_epi32(ps2_u64_to_epi64_pair(lo), ps2_u64_to_epi64_pair(hi)),     \
                                            _MM_SHUFFLE(3, 1, 2, 0)),                                                  \
                        _MM_SHUFFLE(3, 1, 2, 0))

// FPU (COP1) operations
#define FPU_SET_ACC(ctx, res) (ctx->f_acc = res)
#define FPU_ADD_S(a, b) ps2_fadd((float)(a), (float)(b))
#define FPU_SUB_S(a, b) ps2_fsub((float)(a), (float)(b))
#define FPU_MUL_S(a, b) ps2_fmul((float)(a), (float)(b))
#define FPU_DIV_S(a, b) ps2_fdiv((float)(a), (float)(b))
#define FPU_RSQRT_S(a, b) ps2_frsqrt((float)(a), (float)(b))
#define FPU_SQRT_S(a) ps2_fsqrt((float)(a))
#define FPU_ABS_S(a) fabsf((float)(a))
#define FPU_MOV_S(a) ((float)(a))
#define FPU_NEG_S(a) (-(float)(a))
#define FPU_ROUND_L_S(a) ((int64_t)roundf((float)(a)))
#define FPU_TRUNC_L_S(a) ((int64_t)(float)(a))
#define FPU_CEIL_L_S(a) ((int64_t)ceilf((float)(a)))
#define FPU_FLOOR_L_S(a) ((int64_t)floorf((float)(a)))
#define FPU_ROUND_W_S(a) ((int32_t)nearbyintf((float)(a)))
#define FPU_TRUNC_W_S(a) ps2_cvt_w_s((float)(a))
#define FPU_CEIL_W_S(a) ((int32_t)ceilf((float)(a)))
#define FPU_FLOOR_W_S(a) ((int32_t)floorf((float)(a)))
#define FPU_CVT_S_W(a) ((float)(int32_t)(a))
#define FPU_CVT_S_L(a) ((float)(int64_t)(a))
#define FPU_CVT_W_S(a) ps2_cvt_w_s((float)(a))
#define FPU_CVT_L_S(a) ((int64_t)(float)(a))
// Compares / MIN / MAX. The R5900 has no NaN: exponent 255 is an ordinary exponent (0x7FFFFFFF = the largest value,
// which is what x/0 gives), exponent 0 is zero whatever the mantissa. A host float compare calls those patterns NaN
// (unordered: C.LT false, std::min/max keep the first operand), so Drakan code that compares against the result of a
// division by zero took the wrong branch. The compare is done on a sign/magnitude integer key instead.
inline int32_t ps2_fkey(float f) {
    const uint32_t b = std::bit_cast<uint32_t>(f);
    if ((b & 0x7F800000u) == 0u)
        return 0;
    const int32_t m = static_cast<int32_t>(b & 0x7FFFFFFFu);
    return (b >> 31) ? -m : m;
}
inline float ps2_fmax(float a, float b) {
    return ps2_fkey(a) < ps2_fkey(b) ? b : a;
}
inline float ps2_fmin(float a, float b) {
    return ps2_fkey(b) < ps2_fkey(a) ? b : a;
}
#define FPU_C_F_S(a, b) (0)
#define FPU_C_UN_S(a, b) (0)
#define FPU_C_EQ_S(a, b) (ps2_fkey((float)(a)) == ps2_fkey((float)(b)))
#define FPU_C_UEQ_S(a, b) (ps2_fkey((float)(a)) == ps2_fkey((float)(b)))
#define FPU_C_OLT_S(a, b) (ps2_fkey((float)(a)) < ps2_fkey((float)(b)))
#define FPU_C_ULT_S(a, b) (ps2_fkey((float)(a)) < ps2_fkey((float)(b)))
#define FPU_C_OLE_S(a, b) (ps2_fkey((float)(a)) <= ps2_fkey((float)(b)))
#define FPU_C_ULE_S(a, b) (ps2_fkey((float)(a)) <= ps2_fkey((float)(b)))
#define FPU_C_SF_S(a, b) (0)
#define FPU_C_NGLE_S(a, b) (0)
#define FPU_C_SEQ_S(a, b) (ps2_fkey((float)(a)) == ps2_fkey((float)(b)))
#define FPU_C_NGL_S(a, b) (ps2_fkey((float)(a)) == ps2_fkey((float)(b)))
#define FPU_C_LT_S(a, b) (ps2_fkey((float)(a)) < ps2_fkey((float)(b)))
#define FPU_C_NGE_S(a, b) (ps2_fkey((float)(a)) < ps2_fkey((float)(b)))
#define FPU_C_LE_S(a, b) (ps2_fkey((float)(a)) <= ps2_fkey((float)(b)))
#define FPU_C_NGT_S(a, b) (ps2_fkey((float)(a)) <= ps2_fkey((float)(b)))

// QFSRV: Quadword Funnel Shift Right Variable
// Concatenates rs || rt (256 bits) and right-shifts by SA bits, taking lower 128 bits.
inline __m128i ps2_qfsrv(__m128i rs, __m128i rt, uint32_t sa) {
    if (sa == 0)
        return rt;
    if (sa >= 128) {
        if (sa >= 256)
            return _mm_setzero_si128();
        uint32_t shift = sa - 128;
        if (shift == 0)
            return rs;
        // Shift rs right by (sa-128) bits
        uint32_t byteShift = shift / 8;
        uint32_t bitShift = shift % 8;
        // Byte shift rs right
        alignas(16) uint8_t buf[16] = {};
        alignas(16) uint8_t src[16];
        _mm_store_si128((__m128i*)src, rs);
        for (uint32_t i = 0; i + byteShift < 16; i++)
            buf[i] = src[i + byteShift];
        __m128i result = _mm_load_si128((__m128i*)buf);
        if (bitShift > 0)
            result = _mm_or_si128(_mm_srli_epi64(result, bitShift),
                                  _mm_slli_epi64(_mm_bsrli_si128(result, 8), 64 - bitShift));
        return result;
    }
    // sa is 1..127: result = (rs || rt) >> sa, lower 128 bits
    uint32_t byteShift = sa / 8;
    uint32_t bitShift = sa % 8;
    alignas(16) uint8_t combined[32];
    _mm_store_si128((__m128i*)(combined), rt);      // low 128 bits
    _mm_store_si128((__m128i*)(combined + 16), rs); // high 128 bits
    // Shift right by byteShift bytes
    alignas(16) uint8_t shifted[16];
    for (uint32_t i = 0; i < 16; i++)
        shifted[i] = (i + byteShift < 32) ? combined[i + byteShift] : 0;
    __m128i result = _mm_load_si128((__m128i*)shifted);
    if (bitShift > 0) {
        uint8_t extra = (byteShift + 16 < 32) ? combined[byteShift + 16] : 0;
        __m128i hi_byte = _mm_insert_epi8(_mm_setzero_si128(), extra, 15);
        alignas(16) uint8_t src32[32];
        for (uint32_t i = 0; i < 32; i++)
            src32[i] = combined[i];
        uint64_t lo0, lo1, hi0, hi1;
        std::memcpy(&lo0, src32, 8);
        std::memcpy(&lo1, src32 + 8, 8);
        std::memcpy(&hi0, src32 + 16, 8);
        std::memcpy(&hi1, src32 + 24, 8);
        // 256-bit right shift by sa bits
        uint64_t r0, r1;
        if (sa < 64) {
            r0 = (lo0 >> sa) | (lo1 << (64 - sa));
            r1 = (lo1 >> sa) | (hi0 << (64 - sa));
        } else if (sa < 128) {
            uint32_t s = sa - 64;
            if (s == 0) {
                r0 = lo1;
                r1 = hi0;
            } else {
                r0 = (lo1 >> s) | (hi0 << (64 - s));
                r1 = (hi0 >> s) | (hi1 << (64 - s));
            }
        } else {
            r0 = 0;
            r1 = 0; // handled above
        }
        result = _mm_set_epi64x((long long)r1, (long long)r0);
    }
    return result;
}
#define PS2_QFSRV(rs, rt, sa) ps2_qfsrv((__m128i)(rs), (__m128i)(rt), (uint32_t)(sa))
#define PS2_PCPYLD(rs, rt) _mm_unpacklo_epi64(rt, rs)
#define PS2_PEXEH(rs) _mm_shufflelo_epi16(_mm_shufflehi_epi16(rs, _MM_SHUFFLE(2, 3, 0, 1)), _MM_SHUFFLE(2, 3, 0, 1))
#define PS2_PEXEW(rs) _mm_shuffle_epi32(rs, _MM_SHUFFLE(2, 3, 0, 1))
#define PS2_PROT3W(rs) _mm_shuffle_epi32(rs, _MM_SHUFFLE(0, 3, 2, 1))

// Additional VU0 operations
#define PS2_VSQRT(x) sqrtf(x)
#define PS2_VRSQRT(x) (1.0f / sqrtf(x))

// The GPR file is __m128i[32]; the low 64 bits are accessed directly (MSVC has no type-based alias analysis), so a
// scalar write no longer read-modify-writes all 128 bits.
template <typename T>
static PS2_FORCEINLINE T* Ps2GprPtr(R5900Context* ctx, int reg) {
    return reinterpret_cast<T*>(&ctx->r[reg]);
}
#define GPR_U32(ctx_ptr, reg_idx) ((reg_idx) == 0 ? 0U : *Ps2GprPtr<const uint32_t>(ctx_ptr, (reg_idx)))
#define GPR_S32(ctx_ptr, reg_idx) ((reg_idx) == 0 ? 0 : *Ps2GprPtr<const int32_t>(ctx_ptr, (reg_idx)))
#define GPR_U64(ctx_ptr, reg_idx) ((reg_idx) == 0 ? 0ULL : *Ps2GprPtr<const uint64_t>(ctx_ptr, (reg_idx)))
#define GPR_S64(ctx_ptr, reg_idx) ((reg_idx) == 0 ? 0LL : *Ps2GprPtr<const int64_t>(ctx_ptr, (reg_idx)))
#define GPR_VEC(ctx_ptr, reg_idx) ((reg_idx) == 0 ? _mm_setzero_si128() : ctx_ptr->r[reg_idx])

static inline void Ps2SetGprLow64(R5900Context* ctx, int reg, __m128i new_low) {
    if (reg != 0) {
        ctx->r[reg] = _mm_castpd_si128(_mm_move_sd(_mm_castsi128_pd(ctx->r[reg]), _mm_castsi128_pd(new_low)));
    }
}

#define SET_GPR_U32(ctx_ptr, reg_idx, val)                                                                             \
    do {                                                                                                               \
        if ((reg_idx) != 0)                                                                                            \
            *Ps2GprPtr<int64_t>(ctx_ptr, (reg_idx)) = (int64_t)(int32_t)(val);                                         \
    } while (0)

#define SET_GPR_ZE32(ctx_ptr, reg_idx, val)                                                                            \
    do {                                                                                                               \
        if ((reg_idx) != 0)                                                                                            \
            *Ps2GprPtr<int64_t>(ctx_ptr, (reg_idx)) = (int64_t)(uint32_t)(val);                                        \
    } while (0)

#define SET_GPR_S32(ctx_ptr, reg_idx, val)                                                                             \
    do {                                                                                                               \
        if ((reg_idx) != 0)                                                                                            \
            *Ps2GprPtr<int64_t>(ctx_ptr, (reg_idx)) = (int64_t)(int32_t)(val);                                         \
    } while (0)

#define SET_GPR_U64(ctx_ptr, reg_idx, val)                                                                             \
    do {                                                                                                               \
        if ((reg_idx) != 0)                                                                                            \
            *Ps2GprPtr<int64_t>(ctx_ptr, (reg_idx)) = (int64_t)(val);                                                  \
    } while (0)

#define SET_GPR_S64(ctx_ptr, reg_idx, val) SET_GPR_U64(ctx_ptr, reg_idx, val)

#define SET_GPR_VEC(ctx_ptr, reg_idx, val)                                                                             \
    do {                                                                                                               \
        if (reg_idx != 0)                                                                                              \
            ctx_ptr->r[reg_idx] = (val);                                                                               \
    } while (0)

// ---- MMI helpers with exact R5900 semantics (stability audit, docs/reference/stability.md)
// ------------------------------------------------------------- HI/LO of the MMI instructions are 128 bits wide: LO =
// (lo, lo1), HI = (hi, hi1); word i of LO is the half (i & 1) of lo (i < 2) or lo1.
struct Ps2Mmi {
    uint32_t lo[4], hi[4];
    static Ps2Mmi get(const R5900Context* c) {
        Ps2Mmi m;
        m.lo[0] = (uint32_t)c->lo;
        m.lo[1] = (uint32_t)(c->lo >> 32);
        m.lo[2] = (uint32_t)c->lo1;
        m.lo[3] = (uint32_t)(c->lo1 >> 32);
        m.hi[0] = (uint32_t)c->hi;
        m.hi[1] = (uint32_t)(c->hi >> 32);
        m.hi[2] = (uint32_t)c->hi1;
        m.hi[3] = (uint32_t)(c->hi1 >> 32);
        return m;
    }
    void put(R5900Context* c) const {
        c->lo = ((uint64_t)lo[1] << 32) | lo[0];
        c->lo1 = ((uint64_t)lo[3] << 32) | lo[2];
        c->hi = ((uint64_t)hi[1] << 32) | hi[0];
        c->hi1 = ((uint64_t)hi[3] << 32) | hi[2];
    }
};

union Ps2Q {
    uint64_t d[2];
    uint32_t w[4];
    int32_t sw[4];
    uint16_t h[8];
    int16_t sh[8];
    uint8_t b[16];
    __m128i v;
};

// PMULTW / PMADDW / PMSUBW: words 0 and 2, 64-bit product (+/- the 64-bit HI:LO / HI1:LO1 accumulator), results sign
// extended into LO/HI, full 64 bits to rd.
static inline __m128i ps2_mmi_pmulw(R5900Context* c, __m128i rs, __m128i rt, int mode) {
    Ps2Q a, b, r;
    a.v = rs;
    b.v = rt;
    for (int k = 0; k < 2; ++k) {
        uint64_t &lo = k ? c->lo1 : c->lo, &hi = k ? c->hi1 : c->hi;
        uint64_t t = (uint64_t)((int64_t)a.sw[2 * k] * (int64_t)b.sw[2 * k]);
        if (mode) {
            const uint64_t acc = ((uint64_t)(uint32_t)hi << 32) | (uint32_t)lo;
            t = (mode == 1) ? acc + t : acc - t;
        }
        lo = (uint64_t)(int64_t)(int32_t)(uint32_t)t;
        hi = (uint64_t)(int64_t)(int32_t)(uint32_t)((uint64_t)t >> 32);
        r.d[k] = (uint64_t)t;
    }
    return r.v;
}
static inline __m128i ps2_mmi_pmuluw(R5900Context* c, __m128i rs, __m128i rt, int mode) {
    Ps2Q a, b, r;
    a.v = rs;
    b.v = rt;
    for (int k = 0; k < 2; ++k) {
        uint64_t &lo = k ? c->lo1 : c->lo, &hi = k ? c->hi1 : c->hi;
        uint64_t t = (uint64_t)a.w[2 * k] * (uint64_t)b.w[2 * k];
        if (mode)
            t += ((uint64_t)(uint32_t)hi << 32) | (uint32_t)lo;
        lo = (uint64_t)(int64_t)(int32_t)(uint32_t)t;
        hi = (uint64_t)(int64_t)(int32_t)(uint32_t)(t >> 32);
        r.d[k] = t;
    }
    return r.v;
}
static inline void ps2_mmi_pdivw(R5900Context* c, __m128i rs, __m128i rt) {
    Ps2Q a, b;
    a.v = rs;
    b.v = rt;
    for (int k = 0; k < 2; ++k) {
        uint64_t &lo = k ? c->lo1 : c->lo, &hi = k ? c->hi1 : c->hi;
        const int32_t x = a.sw[2 * k], y = b.sw[2 * k];
        if (y == 0) {
            lo = x < 0 ? 1ull : ~0ull;
            hi = (uint64_t)(int64_t)x;
        } else if (x == INT32_MIN && y == -1) {
            lo = (uint64_t)(int64_t)INT32_MIN;
            hi = 0;
        } else {
            lo = (uint64_t)(int64_t)(x / y);
            hi = (uint64_t)(int64_t)(x % y);
        }
    }
}
static inline void ps2_mmi_pdivuw(R5900Context* c, __m128i rs, __m128i rt) {
    Ps2Q a, b;
    a.v = rs;
    b.v = rt;
    for (int k = 0; k < 2; ++k) {
        uint64_t &lo = k ? c->lo1 : c->lo, &hi = k ? c->hi1 : c->hi;
        const uint32_t x = a.w[2 * k], y = b.w[2 * k];
        if (y == 0) {
            lo = ~0ull;
            hi = (uint64_t)(int64_t)(int32_t)x;
        } else {
            lo = (uint64_t)(int64_t)(int32_t)(x / y);
            hi = (uint64_t)(int64_t)(int32_t)(x % y);
        }
    }
}
// PDIVBW: four words of rs divided by the signed halfword rt[0]. Not verified against hardware (unused by Drakan).
static inline void ps2_mmi_pdivbw(R5900Context* c, __m128i rs, __m128i rt) {
    Ps2Q a, b;
    a.v = rs;
    b.v = rt;
    Ps2Mmi m = Ps2Mmi::get(c);
    const int32_t y = b.sh[0];
    for (int i = 0; i < 4; ++i) {
        const int32_t x = a.sw[i];
        if (y == 0) {
            m.lo[i] = x < 0 ? 1u : ~0u;
            m.hi[i] = (uint32_t)x;
        } else if (x == INT32_MIN && y == -1) {
            m.lo[i] = 0x80000000u;
            m.hi[i] = 0;
        } else {
            m.lo[i] = (uint32_t)(x / y);
            m.hi[i] = (uint32_t)(x % y);
        }
    }
    m.put(c);
}
// PMULTH / PMADDH / PMSUBH (mode 0 / 1 / 2): eight halfword products into LO/HI words
// {LO0,LO1,HI0,HI1,LO2,LO3,HI2,HI3}; rd = (LO0, HI0, LO2, HI2).
static inline __m128i ps2_mmi_pmulh(R5900Context* c, __m128i rs, __m128i rt, int mode) {
    Ps2Q a, b, r;
    a.v = rs;
    b.v = rt;
    Ps2Mmi m = Ps2Mmi::get(c);
    uint32_t* slot[8] = {&m.lo[0], &m.lo[1], &m.hi[0], &m.hi[1], &m.lo[2], &m.lo[3], &m.hi[2], &m.hi[3]};
    for (int i = 0; i < 8; ++i) {
        const int32_t p = (int32_t)a.sh[i] * (int32_t)b.sh[i];
        *slot[i] = mode == 0 ? (uint32_t)p : (mode == 1 ? *slot[i] + (uint32_t)p : *slot[i] - (uint32_t)p);
    }
    m.put(c);
    r.w[0] = m.lo[0];
    r.w[1] = m.hi[0];
    r.w[2] = m.lo[2];
    r.w[3] = m.hi[2];
    return r.v;
}
// PHMADH / PHMSBH (mode 1 / 2): horizontal halfword multiply-add / -subtract. Not verified against hardware (unused by
// Drakan).
static inline __m128i ps2_mmi_phmadh(R5900Context* c, __m128i rs, __m128i rt, int mode) {
    Ps2Q a, b, r;
    a.v = rs;
    b.v = rt;
    Ps2Mmi m = Ps2Mmi::get(c);
    uint32_t* slot[4][2] = {{&m.lo[0], &m.lo[1]}, {&m.hi[0], &m.hi[1]}, {&m.lo[2], &m.lo[3]}, {&m.hi[2], &m.hi[3]}};
    for (int g = 0; g < 4; ++g) {
        const int32_t p0 = (int32_t)a.sh[2 * g] * (int32_t)b.sh[2 * g];
        const int32_t p1 = (int32_t)a.sh[2 * g + 1] * (int32_t)b.sh[2 * g + 1];
        *slot[g][0] = mode == 1 ? (uint32_t)p1 + (uint32_t)p0 : (uint32_t)p1 - (uint32_t)p0;
        *slot[g][1] = (uint32_t)p1;
    }
    m.put(c);
    r.w[0] = m.lo[0];
    r.w[1] = m.hi[0];
    r.w[2] = m.lo[2];
    r.w[3] = m.hi[2];
    return r.v;
}
// PMFHL (fmt = LW 0, UW 1, SLW 2, LH 3, SH 4)
static inline __m128i ps2_mmi_pmfhl(const R5900Context* c, int fmt) {
    const Ps2Mmi m = Ps2Mmi::get(c);
    Ps2Q r;
    switch (fmt) {
        case 0:
            r.w[0] = m.lo[0];
            r.w[1] = m.hi[0];
            r.w[2] = m.lo[2];
            r.w[3] = m.hi[2];
            break;
        case 1:
            r.w[0] = m.lo[1];
            r.w[1] = m.hi[1];
            r.w[2] = m.lo[3];
            r.w[3] = m.hi[3];
            break;
        case 2:
            for (int k = 0; k < 2; ++k) {
                const int64_t t = (int64_t)(((uint64_t)m.hi[2 * k] << 32) | m.lo[2 * k]);
                if (t >= 0x7FFFFFFFll)
                    r.d[k] = 0x7FFFFFFFull;
                else if (t <= -0x80000000ll)
                    r.d[k] = 0xFFFFFFFF80000000ull;
                else
                    r.d[k] = (uint64_t)(int64_t)(int32_t)m.lo[2 * k];
            }
            break;
        case 3:
            r.h[0] = (uint16_t)m.lo[0];
            r.h[1] = (uint16_t)m.lo[1];
            r.h[2] = (uint16_t)m.hi[0];
            r.h[3] = (uint16_t)m.hi[1];
            r.h[4] = (uint16_t)m.lo[2];
            r.h[5] = (uint16_t)m.lo[3];
            r.h[6] = (uint16_t)m.hi[2];
            r.h[7] = (uint16_t)m.hi[3];
            break;
        default: {
            auto sat = [](uint32_t x) -> uint16_t {
                const int32_t s = (int32_t)x;
                return s > 0x7FFF ? (uint16_t)0x7FFF : (s < -0x8000 ? (uint16_t)0x8000 : (uint16_t)s);
            };
            r.h[0] = sat(m.lo[0]);
            r.h[1] = sat(m.lo[1]);
            r.h[2] = sat(m.hi[0]);
            r.h[3] = sat(m.hi[1]);
            r.h[4] = sat(m.lo[2]);
            r.h[5] = sat(m.lo[3]);
            r.h[6] = sat(m.hi[2]);
            r.h[7] = sat(m.hi[3]);
            break;
        }
    }
    return r.v;
}
// PMTHL.LW: LO0 = rs.w0, HI0 = rs.w1, LO2 = rs.w2, HI2 = rs.w3 (the other words keep their value)
static inline void ps2_mmi_pmthl_lw(R5900Context* c, __m128i rs) {
    Ps2Q a;
    a.v = rs;
    Ps2Mmi m = Ps2Mmi::get(c);
    m.lo[0] = a.w[0];
    m.hi[0] = a.w[1];
    m.lo[2] = a.w[2];
    m.hi[2] = a.w[3];
    m.put(c);
}
// PSLLVW / PSRLVW / PSRAVW (op 0 / 1 / 2): only words 0 and 2 are shifted (by the low 5 bits of the same word of rs);
// each result is sign extended to 64 bits.
static inline __m128i ps2_mmi_pshiftvw(__m128i rs, __m128i rt, int op) {
    Ps2Q a, b, r;
    a.v = rs;
    b.v = rt;
    for (int k = 0; k < 2; ++k) {
        const uint32_t sh = a.w[2 * k] & 31u;
        uint32_t x;
        if (op == 0)
            x = b.w[2 * k] << sh;
        else if (op == 1)
            x = b.w[2 * k] >> sh;
        else
            x = (uint32_t)(b.sw[2 * k] >> sh);
        r.d[k] = (uint64_t)(int64_t)(int32_t)x;
    }
    return r.v;
}
// PINTH: rd = (rs.h7, rt.h3, rs.h6, rt.h2, rs.h5, rt.h1, rs.h4, rt.h0) from the high halfword down
static inline __m128i ps2_mmi_pinth(__m128i rs, __m128i rt) {
    return _mm_unpacklo_epi16(rt, _mm_srli_si128(rs, 8));
}
// PINTEH: rd = (rs.h6, rt.h6, rs.h4, rt.h4, rs.h2, rt.h2, rs.h0, rt.h0)
static inline __m128i ps2_mmi_pinteh(__m128i rs, __m128i rt) {
    const __m128i mask = _mm_set1_epi32(0x0000FFFF);
    return _mm_or_si128(_mm_and_si128(rt, mask), _mm_slli_epi32(_mm_and_si128(rs, mask), 16));
}
// PABSW / PABSH take their source from rt; the most negative value saturates to the most positive one.
static inline __m128i ps2_mmi_pabsw(__m128i v) {
    const __m128i a = _mm_abs_epi32(v);
    return _mm_xor_si128(a, _mm_and_si128(_mm_cmpeq_epi32(a, _mm_set1_epi32((int)0x80000000)), _mm_set1_epi32(-1)));
}
static inline __m128i ps2_mmi_pabsh(__m128i v) {
    const __m128i a = _mm_abs_epi16(v);
    return _mm_xor_si128(a, _mm_and_si128(_mm_cmpeq_epi16(a, _mm_set1_epi16((short)0x8000)), _mm_set1_epi16(-1)));
}

#endif // PS2_RUNTIME_MACROS_H
