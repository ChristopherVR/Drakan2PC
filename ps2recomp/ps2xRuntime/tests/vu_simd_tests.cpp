#include "vu/ps2_vu1_simd.h"
#include "runtime/ps2_vu1.h"
#include "vu/ps2_vu1_step.h"
#include <cfenv>
#include <cstring>
#include <limits>
#include <cstdio>
uint32_t seed = 0x718fd35;
uint32_t nextBits() {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}
int main() {
    uint64_t checks = 0;
    for (int mode : {FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD}) {
        std::fesetround(mode);
        for (unsigned n = 0; n < 1000000; ++n) {
            alignas(16) uint32_t raw[4];
            for (auto& bits : raw)
                bits = nextBits();
            // Dense coverage at exponent boundaries as well as random mantissas.
            if (n < 2048)
                for (unsigned lane = 0; lane < 4; ++lane)
                    raw[lane] = ((n & 256u) << 23) | ((n & 255u) << 23) | ((n & 512u) ? 0x7fffffu : lane);
            const __m128 value = vuNormalizeVec(_mm_castsi128_ps(_mm_load_si128(reinterpret_cast<__m128i*>(raw))));
            alignas(16) float normalized[4];
            _mm_store_ps(normalized, value);
            for (float scale : {1.0f, 16.0f, 4096.0f, 32768.0f}) {
                alignas(16) int32_t actual[4];
                _mm_store_si128(reinterpret_cast<__m128i*>(actual), Vu1Simd::toFixed(value, scale));
                for (unsigned lane = 0; lane < 4; ++lane) {
                    const double scaled = static_cast<double>(normalized[lane]) * static_cast<double>(scale);
                    const auto max = std::numeric_limits<int32_t>::max(), min = std::numeric_limits<int32_t>::min();
                    const int32_t expected = scaled >= double(max)   ? max
                                             : scaled <= double(min) ? min
                                                                     : int32_t(scaled);
                    if (expected != actual[lane]) {
                        std::printf("FTOI mismatch %08x scale %.0f mode %d\n", raw[lane], scale, mode);
                        return 1;
                    }
                    ++checks;
                }
                const __m128 integers = _mm_cvtepi32_ps(_mm_load_si128(reinterpret_cast<__m128i*>(raw)));
                alignas(16) uint32_t divided[4], multiplied[4];
                _mm_store_si128(reinterpret_cast<__m128i*>(divided),
                                _mm_castps_si128(_mm_div_ps(integers, _mm_set1_ps(scale))));
                _mm_store_si128(reinterpret_cast<__m128i*>(multiplied),
                                _mm_castps_si128(_mm_mul_ps(integers, _mm_set1_ps(1.0f / scale))));
                if (std::memcmp(divided, multiplied, 16))
                    return 2;
                checks += 4;
            }
            const int ordinary =
                Vu1Simd::ordinaryLanes(_mm_castsi128_ps(_mm_load_si128(reinterpret_cast<__m128i*>(raw))));
            for (unsigned lane = 0; lane < 4; ++lane) {
                if (bool(ordinary & (1 << lane)) != ((((raw[lane] >> 23) & 255u) - 8u) < 239u))
                    return 3;
                ++checks;
            }
            uint32_t source[4], expected[4], actual[4];
            for (unsigned lane = 0; lane < 4; ++lane) {
                source[lane] = nextBits();
                expected[lane] = actual[lane] = nextBits();
            }
            const uint8_t lanes = uint8_t(n & 15);
            for (unsigned lane = 0; lane < 4; ++lane)
                if (lanes & (8u >> lane))
                    expected[lane] = source[lane];
            Vu1Simd::writeLanes(reinterpret_cast<float*>(actual), reinterpret_cast<const float*>(source), lanes);
            if (std::memcmp(expected, actual, 16))
                return 4;
            ++checks;
        }
    }
    VU1State state{};
    state.vf[0][3] = 1.0f;
    for (unsigned trial = 0; trial < 100000; ++trial) {
        const uint8_t reg = trial & 31u;
        if (reg != 0)
            for (unsigned lane = 0; lane < 4; ++lane) {
                const uint32_t bits = nextBits();
                std::memcpy(&state.vf[reg][lane], &bits, 4);
            }
        const __m128 expected = vuNormalizeVec(_mm_loadu_ps(state.vf[reg]));
        const __m128 actual = Vu1Step::readOperand(state, reg);
        if (_mm_movemask_epi8(_mm_cmpeq_epi32(_mm_castps_si128(expected), _mm_castps_si128(actual))) != 65535)
            return 7;
        checks += 4;
        for (uint8_t component = 0; component < 4; ++component) {
            const __m128 expectedBroadcast = vuNormalizeVec(_mm_set1_ps(state.vf[reg][component]));
            const __m128 actualBroadcast = Vu1Step::readBroadcast(state, reg, component);
            if (_mm_movemask_epi8(
                    _mm_cmpeq_epi32(_mm_castps_si128(expectedBroadcast), _mm_castps_si128(actualBroadcast))) != 65535)
                return 8;
            checks += 4;
        }
    }
    std::fesetround(FE_TONEAREST);
    std::printf("%llu exact conversion, classification and lane-write comparisons\n", (unsigned long long)checks);
}
