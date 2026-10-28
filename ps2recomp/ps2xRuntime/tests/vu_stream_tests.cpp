#include "vu/ps2_vu1_flag_retire.h"
#include "vu/ps2_vu1_ring.h"
#include <array>
#include <vector>
#include <random>
#include <cstdio>

struct Entry {
    uint64_t readyCycle;
    uint32_t mac, status, clip, extraSticky;
    bool writesMac, writesStatus, writesSticky, writesClip;
};

int main() {
    std::mt19937 rng(0x123abc);
    uint64_t checks = 0;
    for (unsigned n = 0; n < 300000; ++n) {
        std::array<Entry, 8> entries;
        for (auto& entry : entries)
            entry = {rng() % 40,      rng(),           rng(),           rng(),          rng(),
                     bool(rng() & 1), bool(rng() & 1), bool(rng() & 1), bool(rng() & 1)};
        uint32_t head = rng() % 8, count = rng() % 9;
        uint32_t mac = rng(), status = rng(), clip = rng();
        auto actualHead = head, actualCount = count;
        auto actualMac = mac, actualStatus = status, actualClip = clip;
        // Deliberately include unordered readiness and arbitrary register bits.
        // The FIFO must stop at its first future entry even if later ones are due.
        for (uint64_t cycle = 0; cycle < 45; cycle += 3) {
            // Original per-entry retirement, independent of the optimized helper.
            while (count) {
                const auto& entry = entries[head];
                if (entry.readyCycle > cycle)
                    break;
                if (entry.writesMac)
                    mac = entry.mac;
                if (entry.writesStatus) {
                    const auto current = entry.status & 15u;
                    status = (status & 0xff0u) | current | ((current | entry.extraSticky) << 6);
                }
                if (entry.writesSticky)
                    status = (status & 0x3fu) | (entry.status & 0xfc0u);
                if (entry.writesClip)
                    clip = entry.clip;
                head = (head + 1) & 7;
                --count;
            }
            Vu1Flags::retire(entries.data(), 7, cycle, actualHead, actualCount, actualMac, actualStatus, actualClip);
            if (head != actualHead || count != actualCount || mac != actualMac || status != actualStatus ||
                clip != actualClip)
                return 1;
            ++checks;
        }
    }
    for (unsigned n = 0; n < 10000; ++n) {
        const unsigned size = 16u << (rng() % 11), offset = rng(), bytes = rng() % 65537;
        std::vector<uint8_t> source(size), actual(bytes + 32, 0xcd), expected = actual;
        for (auto& byte : source)
            byte = uint8_t(rng());
        for (unsigned j = 0; j < bytes; ++j)
            expected[16 + j] = source[(offset + j) & (size - 1)];
        Vu1Ring::copyPayload(actual.data() + 16, source.data(), size, offset, bytes);
        // Includes guards, empty payloads, unaligned offsets and multiple wraps.
        if (actual != expected)
            return 2;
        ++checks;
    }
    std::printf("%llu flag retirement and wrapped payload comparisons passed\n", (unsigned long long)checks);
}
