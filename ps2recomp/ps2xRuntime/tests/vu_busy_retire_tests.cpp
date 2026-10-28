#include "vu/ps2_vu1_fast.h"
#include <cstdio>
#include <random>

int main() {
    std::mt19937 random(0x1850bc);
    auto bits = [&] { return (uint64_t(random()) << 32u) | random(); };
    constexpr uint32_t cases = 100000;
    for (uint32_t trial = 0; trial < cases; ++trial) {
        Vu1Fast::Spare expected{};
        const uint64_t base = bits();
        const uint32_t cycles = trial % 64u;
        for (uint32_t ahead = 1; ahead <= 3u; ++ahead) {
            auto& entry = expected.clearAt[(base + ahead) & 15u];
            entry = {bits(), bits(), random()};
            expected.pending.lo |= entry.lo;
            expected.pending.hi |= entry.hi;
            expected.pending.va |= entry.va;
        }
        auto actual = expected;
        // Previous implementation: scan all elapsed slots, up to the whole ring.
        const uint32_t advance = cycles < 15u ? cycles : 15u;
        for (uint32_t i = 1; i <= advance; ++i) {
            auto& entry = expected.clearAt[(base + i) & 15u];
            expected.pending.lo &= ~entry.lo;
            expected.pending.hi &= ~entry.hi;
            expected.pending.va &= ~entry.va;
            entry = {};
        }
        Vu1Fast::retireBlockEntry(actual, base, cycles);
        auto equal = [](const auto& a, const auto& b) { return a.lo == b.lo && a.hi == b.hi && a.va == b.va; };
        if (!equal(actual.pending, expected.pending))
            return 1;
        for (unsigned i = 0; i < 16; ++i)
            if (!equal(actual.clearAt[i], expected.clearAt[i]))
                return 2;
    }
    for (uint32_t trial = 0; trial < cases; ++trial) {
        Vu1Fast::Spare actual{};
        const uint64_t base = random() % 0x10000000u;
        const uint32_t oldRemaining = 1u + random() % 3u;
        const uint32_t cycles = 1u + random() % 8u;
        const uint32_t issue = random() % cycles;
        const uint64_t ready = base + issue + 4u;
        const uint64_t mask = 1ull << (random() & 63u);
        const bool high = (random() & 1u) != 0u;
        const uint64_t lo = high ? 0 : mask, hi = high ? mask : 0;
        actual.pending = {lo, hi, 0};
        actual.clearAt[(base + oldRemaining) & 15u] = {lo, hi, 0};
        Vu1Fast::retireBlockEntry(actual, base, cycles);
        const uint64_t end = base + cycles;
        if (ready > end)
            Vu1Fast::installBlockWrite(actual, end, cycles, uint32_t(ready - end), lo, hi, 0);
        for (uint32_t ahead = 0; ahead <= 6; ++ahead) {
            if (ahead)
                Vu1Fast::advanceBusy(actual, end + ahead);
            const uint64_t expectedLo = end + ahead < ready ? lo : 0;
            const uint64_t expectedHi = end + ahead < ready ? hi : 0;
            if (actual.pending.lo != expectedLo || actual.pending.hi != expectedHi || actual.pending.va != 0)
                return 3;
        }
    }
    std::printf("PASS: %u overwritten entry deadlines retain the newest write's exact readiness\n", cases);
    std::printf("PASS: %u exact block retirements against the full scan, including short blocks and ring wrap\n",
                cases);
}
