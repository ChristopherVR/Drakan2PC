// Focused runtime checks; the driver extracts the current strncat body from LibC.cpp.
#include "ps2_runtime_macros.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace ps2_stubs {
static uint32_t getRegU32(R5900Context* c, int n) {
    return GPR_U32(c, n);
}
static uint32_t guestContiguousBytes(uint32_t a) {
    return a > 0 && a < 64 ? 64 - a : 0;
}
static uint8_t* getMemPtr(uint8_t* ram, uint32_t a) {
    return guestContiguousBytes(a) ? ram + a : nullptr;
}
static const uint8_t* getConstMemPtr(uint8_t* ram, uint32_t a) {
    return getMemPtr(ram, a);
}
#include "stab_string_impl.h"
} // namespace ps2_stubs

static void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        std::exit(1);
    }
}

int main() {
    R5900Context ctx{};
    alignas(16) float vf[4];
    _mm_store_ps(vf, ctx.vu0_vf[0]);
    check(vf[0] == 0 && vf[1] == 0 && vf[2] == 0 && vf[3] == 1, "new context vf0");

    ctx.hi = 0x7fffffffu;
    ctx.lo = 0xffffffffu;
    const __m128i ones = _mm_set_epi32(0, 1, 0, 1);
    const __m128i sum = ps2_mmi_pmulw(&ctx, ones, ones, 1);
    check((uint64_t)_mm_cvtsi128_si64(sum) == 0x8000000000000000ull && ctx.hi == 0xffffffff80000000ull && ctx.lo == 0,
          "PMADDW wraps a signed accumulator overflow");
    const __m128i mins = _mm_set1_epi16(-32768);
    const __m128i hsum = ps2_mmi_phmadh(&ctx, mins, mins, 1);
    check((uint32_t)_mm_cvtsi128_si32(hsum) == 0x80000000u, "PHMADH wraps a word sum overflow (arithmetic only)");

    std::array<uint8_t, 80> ram;
    auto prepare = [&]() {
        ram.fill(0xcc);
        std::memcpy(ram.data() + 1, "abcdef", 7);
        SET_GPR_U32(&ctx, 4, 60);
        SET_GPR_U32(&ctx, 5, 1);
        SET_GPR_U32(&ctx, 6, 0xffffffffu);
    };
    prepare();
    std::memcpy(ram.data() + 60, "x", 2);
    ps2_stubs::strncat(ram.data(), &ctx, nullptr);
    check(std::memcmp(ram.data() + 60, "xab", 4) == 0 && ram[64] == 0xcc && GPR_U32(&ctx, 2) == 60,
          "strncat destination boundary and return");
    prepare();
    const auto before = ram;
    ps2_stubs::strncat(ram.data(), &ctx, nullptr);
    check(ram == before, "strncat unterminated destination is bounded");
    prepare();
    ram[60] = 0;
    ram[63] = 'z';
    SET_GPR_U32(&ctx, 5, 63);
    ps2_stubs::strncat(ram.data(), &ctx, nullptr);
    check(ram[60] == 'z' && ram[61] == 0 && ram[64] == 0xcc, "strncat unterminated source boundary");
    prepare();
    std::memcpy(ram.data() + 60, "x", 2);
    SET_GPR_U32(&ctx, 6, 0);
    ps2_stubs::strncat(ram.data(), &ctx, nullptr);
    check(ram[60] == 'x' && ram[61] == 0 && ram[62] == 0xcc, "strncat zero count");
    std::puts("PASS: vf0 initialization, two MMI overflow boundaries, four strncat boundaries");
}
