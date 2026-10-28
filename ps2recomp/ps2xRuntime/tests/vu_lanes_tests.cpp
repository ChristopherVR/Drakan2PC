// The multi-iteration packs of the functional VU1 engine (vu/aot/ps2_vu1_lanes.h) against the SSE code of the scalar
// engine, lane by lane: operand normalization, FTOI/ITOF, ABS, DIV, the lane moves, and the test that decides whether
// Vu1Step::calculateFmac would return the plain SSE result (fast path or flagsKnownClasses).
#include "vu/aot/ps2_vu1_lanes.h"
#include "runtime/ps2_vu1.h"
#include "vu/ps2_vu1_simd.h"
#include "vu/ps2_vu1_step.h"
#include <cfenv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
uint32_t seed = 0x2545f491u;
uint32_t nextBits() {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}
// random floats biased towards the classes the checks distinguish: zeros, denormals, tiny and huge exponents
uint32_t randomFloatBits() {
    static constexpr uint32_t kExponents[] = {0, 1, 2, 7, 8, 9, 60, 120, 127, 128, 200, 245, 246, 247, 253, 254, 255};
    const uint32_t r = nextBits();
    const uint32_t sign = r & 0x80000000u;
    switch (r & 7u) {
        case 0:
            return sign; // +-0
        case 1:
        case 2: {
            const uint32_t e = kExponents[nextBits() % (sizeof(kExponents) / sizeof(kExponents[0]))];
            return sign | (e << 23) | (nextBits() & 0x7fffffu);
        }
        default:
            return sign | ((100u + nextBits() % 56u) << 23) | (nextBits() & 0x7fffffu);
    }
}
float asFloat(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
uint32_t asBits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
float clampVu(float value) {
    uint32_t bits = asBits(value);
    const uint32_t exponent = (bits >> 23) & 0xFFu;
    if (exponent == 0u)
        bits &= 0x80000000u;
    else if (exponent == 0xFFu)
        bits = (bits & 0x80000000u) | 0x7F7FFFFFu;
    return asFloat(bits);
}

template <class B>
struct Tester {
    static constexpr uint32_t W = B::W;
    using V = typename B::V;
    using M = typename B::M;
    uint64_t checks = 0;

    static V fromLanes(const __m128 (&x)[W]) {
        alignas(64) float f[4 * W];
        for (uint32_t j = 0; j < W; ++j)
            _mm_store_ps(f + 4 * j, x[j]);
        return B::load(reinterpret_cast<const uint8_t*>(f));
    }
    static void toFloats(V v, float (&f)[4 * W]) {
        alignas(64) float t[4 * W];
        std::memcpy(t, &v, sizeof(t));
        std::memcpy(f, t, sizeof(t));
    }
    // bit 4 * j + c of a mask = component c of lane j
    static uint32_t maskBits(M m) {
        if constexpr (sizeof(M) == 2)
            return static_cast<uint32_t>(m);
        else {
            alignas(32) int32_t t[8];
            std::memcpy(t, &m, 32);
            uint32_t bits = 0;
            for (uint32_t k = 0; k < 8; ++k)
                if (t[k])
                    bits |= 1u << k;
            return bits;
        }
    }
    bool same(V v, const float* expected, const char* what) {
        float actual[4 * W];
        toFloats(v, actual);
        for (uint32_t k = 0; k < 4 * W; ++k)
            if (asBits(actual[k]) != asBits(expected[k])) {
                std::printf("W=%u %s: element %u got %08x expected %08x\n", W, what, k, asBits(actual[k]),
                            asBits(expected[k]));
                return false;
            }
        checks += 4 * W;
        return true;
    }

    template <int Kind, int ConstB>
    bool fmacCase(const __m128 (&vs)[W], const __m128 (&b)[W], const __m128 (&acc)[W]) {
        __m128 product[W], result[W];
        uint32_t expectedBad = 0;
        for (uint32_t j = 0; j < W; ++j) {
            // Vu1Step::calculateFmac
            product[j] = _mm_setzero_ps();
            if constexpr (Kind == 1)
                result[j] = _mm_add_ps(vs[j], b[j]);
            else if constexpr (Kind == 2)
                result[j] = _mm_sub_ps(vs[j], b[j]);
            else if constexpr (Kind == 3)
                result[j] = product[j] = _mm_mul_ps(vs[j], b[j]);
            else {
                product[j] = _mm_mul_ps(vs[j], b[j]);
                result[j] = Kind == 4 ? _mm_add_ps(acc[j], product[j]) : _mm_sub_ps(acc[j], product[j]);
            }
            const int ordR = Vu1FmacDetail::ordinaryLanes(result[j]);
            const int ordP = Kind >= 4 ? Vu1FmacDetail::ordinaryLanes(product[j]) : 0xF;
            for (int c = 0; c < 4; ++c) {
                const int comps = 1 << c;
                uint32_t mac, status, sticky;
                const bool plain =
                    ((ordR & ordP) & comps) == comps ||
                    Vu1FmacDetail::flagsKnownClasses(Kind, vs[j], b[j], Kind >= 4 ? product[j] : result[j], result[j],
                                                     comps, ordR, ordP, mac, status, sticky);
                if (!plain)
                    expectedBad |= 1u << (4 * j + c);
            }
        }
        const M bad = Vu1Lanes::fmacBad<B, Kind, ConstB>(B::template comps<15>(), fromLanes(vs), fromLanes(b),
                                                         fromLanes(product), fromLanes(result));
        if (maskBits(bad) != expectedBad) {
            std::printf("W=%u FMAC kind %d constB %d: bad %x expected %x\n", W, Kind, ConstB, maskBits(bad),
                        expectedBad);
            for (uint32_t j = 0; j < W; ++j) {
                alignas(16) uint32_t a[4], x[4], y[4];
                _mm_store_ps(reinterpret_cast<float*>(a), vs[j]);
                _mm_store_ps(reinterpret_cast<float*>(x), b[j]);
                _mm_store_ps(reinterpret_cast<float*>(y), acc[j]);
                std::printf("  lane %u vs %08x %08x %08x %08x b %08x %08x %08x %08x acc %08x %08x %08x %08x\n", j, a[0],
                            a[1], a[2], a[3], x[0], x[1], x[2], x[3], y[0], y[1], y[2], y[3]);
            }
            return false;
        }
        checks += 4 * W;
        return true;
    }

    bool run() {
        for (int mode : {FE_TOWARDZERO, FE_TONEAREST}) {
            const unsigned savedCsr = _mm_getcsr();
            _mm_setcsr((savedCsr & ~0x6000u) | (mode == FE_TOWARDZERO ? 0x6000u : 0u));
            for (uint32_t n = 0; n < 200000; ++n) {
                __m128 raw[W], vs[W], b[W], acc[W], one[W], zero[W];
                float rawF[4 * W];
                for (uint32_t j = 0; j < W; ++j) {
                    alignas(16) uint32_t r[4], x[4], y[4], z[4];
                    for (uint32_t c = 0; c < 4; ++c) {
                        r[c] = randomFloatBits();
                        x[c] = randomFloatBits();
                        y[c] = randomFloatBits();
                        z[c] = randomFloatBits();
                    }
                    if (n & 1u) // a broadcast second operand, like the bc FMAC forms
                        x[1] = x[2] = x[3] = x[0];
                    raw[j] = _mm_load_ps(reinterpret_cast<const float*>(r));
                    vs[j] = Vu1Simd::normalize(_mm_load_ps(reinterpret_cast<const float*>(x)));
                    b[j] = Vu1Simd::normalize(_mm_load_ps(reinterpret_cast<const float*>(y)));
                    acc[j] = Vu1Simd::normalize(_mm_load_ps(reinterpret_cast<const float*>(z)));
                    one[j] = _mm_set1_ps(1.0f);
                    zero[j] = _mm_setzero_ps();
                    _mm_storeu_ps(rawF + 4 * j, raw[j]);
                }
                const V pack = fromLanes(raw);
                float expected[4 * W];
                // normalize
                for (uint32_t j = 0; j < W; ++j)
                    _mm_storeu_ps(expected + 4 * j, Vu1Simd::normalize(raw[j]));
                if (!same(B::normalize(pack), expected, "normalize"))
                    return false;
                // FTOI (of normalized values) / ITOF
                for (float scale : {1.0f, 16.0f, 4096.0f, 32768.0f}) {
                    for (uint32_t j = 0; j < W; ++j)
                        _mm_storeu_ps(expected + 4 * j,
                                      _mm_castsi128_ps(Vu1Simd::toFixed(Vu1Simd::normalize(raw[j]), scale)));
                    if (!same(B::toFixed(B::normalize(pack), scale), expected, "toFixed"))
                        return false;
                }
                for (uint32_t j = 0; j < W; ++j)
                    _mm_storeu_ps(expected + 4 * j, _mm_cvtepi32_ps(_mm_castps_si128(raw[j])));
                if (!same(B::itof(pack), expected, "itof"))
                    return false;
                for (uint32_t j = 0; j < W; ++j)
                    _mm_storeu_ps(expected + 4 * j,
                                  _mm_andnot_ps(_mm_castsi128_ps(_mm_set1_epi32(static_cast<int>(0x80000000u))),
                                                Vu1Simd::normalize(raw[j])));
                if (!same(B::abs(B::normalize(pack)), expected, "abs"))
                    return false;
                // DIV (ps2_vu1_func.cpp: clampVu(num) / clampVu(den), den == 0 -> +-FLT_MAX, clampVu(result))
                {
                    const V num = B::normalize(B::template permute<0x00>(pack));
                    const V den = B::normalize(B::template permute<0xFF>(fromLanes(vs)));
                    for (uint32_t j = 0; j < W; ++j) {
                        const float nf = clampVu(rawF[4 * j]);
                        alignas(16) float vsf[4];
                        _mm_store_ps(vsf, vs[j]);
                        const float df = clampVu(vsf[3]);
                        float q;
                        if (df == 0.0f)
                            q = std::signbit(nf) != std::signbit(df) ? -std::numeric_limits<float>::max()
                                                                     : std::numeric_limits<float>::max();
                        else
                            q = nf / df;
                        q = clampVu(q);
                        for (uint32_t c = 0; c < 4; ++c)
                            expected[4 * j + c] = q;
                    }
                    if (!same(B::normalize(B::divide(num, den)), expected, "divide"))
                        return false;
                }
                // lane moves
                {
                    const V other = fromLanes(vs);
                    float vsF[4 * W];
                    for (uint32_t j = 0; j < W; ++j)
                        _mm_storeu_ps(vsF + 4 * j, vs[j]);
                    for (uint32_t j = 0; j < W; ++j)
                        for (uint32_t c = 0; c < 4; ++c)
                            expected[4 * j + c] = j == 0 ? vsF[4 * (W - 1) + c] : rawF[4 * (j - 1) + c];
                    if (!same(B::shiftIn(pack, B::last(other)), expected, "shiftIn/last"))
                        return false;
                    for (uint32_t j = 0; j < W; ++j)
                        for (uint32_t c = 0; c < 4; ++c)
                            expected[4 * j + c] = (0x5u >> c) & 1u ? vsF[4 * j + c] : rawF[4 * j + c];
                    if (!same(B::template blend<0x5u>(pack, other), expected, "blend"))
                        return false;
                    for (uint32_t j = 0; j < W; ++j)
                        for (uint32_t c = 0; c < 4; ++c)
                            expected[4 * j + c] = rawF[4 * j + ((c + 1) & 3u)];
                    if (!same(B::template permute<_MM_SHUFFLE(0, 3, 2, 1)>(pack), expected, "permute"))
                        return false;
                }
                // FMAC acceptance
                if (!fmacCase<1, -1>(vs, b, acc) || !fmacCase<2, -1>(vs, b, acc) || !fmacCase<3, -1>(vs, b, acc) ||
                    !fmacCase<4, -1>(vs, b, acc) || !fmacCase<5, -1>(vs, b, acc) || !fmacCase<3, 1>(vs, one, acc) ||
                    !fmacCase<4, 1>(vs, one, acc) || !fmacCase<5, 1>(vs, one, acc) || !fmacCase<4, 0>(vs, zero, acc) ||
                    !fmacCase<5, 0>(vs, zero, acc))
                    return false;
            }
            _mm_setcsr(savedCsr);
        }
        return true;
    }
};
} // namespace

int main() {
    const uint32_t width = Vu1Lanes::detect();
    std::printf("lane width supported: %u\n", width);
    uint64_t checks = 0;
#if defined(VU_LANES_AVX2)
    if (width >= 2) {
        Tester<Vu1Lanes::Avx2> t;
        if (!t.run())
            return 1;
        checks += t.checks;
    }
#endif
#if defined(VU_LANES_AVX512)
    if (width >= 4) {
        Tester<Vu1Lanes::Avx512> t;
        if (!t.run())
            return 2;
        checks += t.checks;
    }
#endif
    std::printf("vu_lanes: %llu checks passed\n", static_cast<unsigned long long>(checks));
    return 0;
}
