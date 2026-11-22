// Exercise the actual LibC.cpp stub, extracted by CMake, with a bounded guest window and write receipts.
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace ps2_stubs {
struct R5900Context {
    std::array<uint32_t, 32> r{};
};
struct PS2Runtime {};
constexpr uint32_t kRamSize = 256;
static uint32_t traceAddress, traceSize;
static uint32_t getRegU32(R5900Context* ctx, int reg) {
    return ctx->r[reg];
}
static uint32_t guestContiguousBytes(uint32_t address) {
    return address > 0 && address < kRamSize ? kRamSize - address : 0;
}
static uint8_t* getMemPtr(uint8_t* ram, uint32_t address) {
    return guestContiguousBytes(address) ? ram + address : nullptr;
}
static const uint8_t* getConstMemPtr(uint8_t* ram, uint32_t address) {
    return getMemPtr(ram, address);
}
static void ps2TraceGuestRangeWrite(uint8_t*, uint32_t address, uint32_t size, const char*, R5900Context*) {
    traceAddress = address;
    traceSize = size;
}
#include "guest_strcpy_impl.h"
} // namespace ps2_stubs

int main() {
    using namespace ps2_stubs;
    std::array<uint8_t, kRamSize + 16> ram;
    R5900Context ctx;
    auto reset = [&]() {
        ram.fill(0xcc);
        traceAddress = traceSize = 0;
    };
    auto copy = [&](uint32_t dest, uint32_t src) {
        ctx.r[4] = dest;
        ctx.r[5] = src;
        ctx.r[2] = 0xffffffff;
        ps2_stubs::strcpy(ram.data(), &ctx, nullptr);
        assert(ctx.r[2] == dest);
    };

    // Save loading strips a path prefix in-place. Sweep alignments and prefix lengths so a host CRT's
    // short-string/vector-copy choices cannot hide the overlap (issue #21, especially on Linux).
    constexpr char level[] = "Level 1E - Inquisitor's Lair";
    for (uint32_t dest = 1; dest <= 32; ++dest) {
        for (uint32_t prefix = 0; prefix <= 32; ++prefix) {
            reset();
            std::memcpy(ram.data() + dest + prefix, level, sizeof(level));
            const auto before = ram;
            copy(dest, dest + prefix);
            assert(std::memcmp(ram.data() + dest, level, sizeof(level)) == 0);
            assert(traceAddress == dest && traceSize == sizeof(level));
            for (uint32_t i = 0; i < ram.size(); ++i) {
                if (i < dest || i >= dest + sizeof(level))
                    assert(ram[i] == before[i]);
            }
        }
    }
    reset();
    std::memcpy(ram.data() + 1, level, sizeof(level));
    copy(128, 1); // separate buffers
    assert(std::memcmp(ram.data() + 128, level, sizeof(level)) == 0);
    reset();
    ram[kRamSize - 1] = 0;
    copy(1, kRamSize - 1); // NUL at the final readable source byte
    assert(ram[1] == 0 && traceSize == 1);
    copy(kRamSize - 1, 1); // empty string at the final writable destination byte
    assert(ram[kRamSize - 1] == 0 && ram[kRamSize] == 0xcc && traceSize == 1);
    reset();
    std::memcpy(ram.data() + 1, "abc", 4);
    copy(kRamSize - 4, 1); // exact fit, including NUL
    assert(std::memcmp(ram.data() + kRamSize - 4, "abc", 4) == 0 && ram[kRamSize] == 0xcc);

    // Invalid/unterminated/oversized strings must leave both guest RAM and host canaries alone.
    auto unchanged = [&](uint32_t dest, uint32_t src) {
        const auto before = ram;
        traceSize = 0;
        copy(dest, src);
        assert(ram == before && traceSize == 0);
    };
    unchanged(kRamSize - 3, 1);
    unchanged(0, 1);
    unchanged(1, 0);
    unchanged(kRamSize, 1);
    unchanged(1, kRamSize);
    reset();
    unchanged(1, 32);
    unchanged(128, kRamSize - 1);
    std::cout << "PASS: overlapping level names, return/write receipts, guest boundaries and canaries\n";
}
