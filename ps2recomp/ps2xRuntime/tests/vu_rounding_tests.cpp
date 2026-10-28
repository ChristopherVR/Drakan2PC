#include "vu/ps2_vu1_simd.h"
#include <bit>
#include <cstdio>

namespace {
uint32_t seed = 0x728d31u;
uint32_t next() {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}
float operand() {
    const uint32_t bits = next();
    return std::bit_cast<float>((bits & 0x807fffffu) | ((1 + ((bits >> 23) % 254)) << 23));
}
} // namespace

int main() {
    uint64_t digest = 14695981039346656037ull;
    for (unsigned n = 0; n < 500000; ++n) {
        // Include cancellation, tiny opposing terms and overflow boundaries.
        const float a = n < 256 ? 1.0f : operand();
        const float b = n < 256 ? -std::bit_cast<float>((n + 1) << 23) : operand();
        float software[4];
#if !defined(__EMSCRIPTEN__)
        _mm_setcsr(0x1f80u); // software emulates rounding on a nearest-only host
#endif
        software[0] = Vu1Simd::Software::add(a, b);
        software[1] = Vu1Simd::Software::multiply(a, b);
        software[2] = Vu1Simd::Software::divide(a, b);
        software[3] = Vu1Simd::Software::squareRoot(std::fabs(a));
#if !defined(__EMSCRIPTEN__)
        _mm_setcsr(0xffc0u); // toward zero, flush-to-zero, denormals-are-zero
        const __m128 x = _mm_set_ss(a), y = _mm_set_ss(b);
        const float hardware[] = {_mm_cvtss_f32(_mm_add_ss(x, y)), _mm_cvtss_f32(_mm_mul_ss(x, y)),
                                  _mm_cvtss_f32(_mm_div_ss(x, y)),
                                  _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(std::fabs(a))))};
        for (unsigned operation = 0; operation < 4; ++operation) {
            if (std::bit_cast<uint32_t>(hardware[operation]) != std::bit_cast<uint32_t>(software[operation])) {
                std::printf("Rounding mismatch: op=%u a=%08x b=%08x hardware=%08x software=%08x\n", operation,
                            std::bit_cast<uint32_t>(a), std::bit_cast<uint32_t>(b),
                            std::bit_cast<uint32_t>(hardware[operation]), std::bit_cast<uint32_t>(software[operation]));
                return 1;
            }
        }
        _mm_setcsr(0x1f80u);
#endif
        for (float value : software) {
            digest ^= std::bit_cast<uint32_t>(value);
            digest *= 1099511628211ull;
        }
    }
    std::printf("VU rounding: 2,000,000 comparisons, digest=%016llx\n", static_cast<unsigned long long>(digest));
    return digest == 0xa0aa195d6d236fd4ull ? 0 : 2;
}
