#include "ps2_vif_unpack.h"
#include <array>
#include <cstdio>
#include <random>
#include <vector>

int main() {
#if defined(_M_X64) || defined(__SSE2__)
    std::mt19937 random(0x1851f);
    std::array<uint8_t, 16384 + 32> actual{}, expected{};
    uint64_t cases = 0, vectors = 0;
    for (unsigned vn = 0; vn < 4; ++vn)
        for (unsigned vl = 0; vl < 3; ++vl)
            for (unsigned unsignedData = 0; unsignedData < 2; ++unsignedData)
                for (unsigned trial = 0; trial < 4000; ++trial) {
                    const uint32_t address = trial < 1024 ? trial : random() % 1024;
                    const uint32_t count = trial % 257 ? 1 + random() % 256 : 256;
                    const uint32_t wl = trial % 3 ? 1 + random() % 255 : 1;
                    const uint32_t cl = wl + random() % (256 - wl);
                    const uint32_t components = vn + 1, bits = 32u >> vl;
                    std::vector<uint8_t> source(count * components * bits / 8u);
                    for (auto& byte : source)
                        byte = static_cast<uint8_t>(random());
                    for (auto& byte : expected)
                        byte = static_cast<uint8_t>(random());
                    actual = expected;
                    uint32_t first = 7, end = 0, refFirst = 7, refEnd = 0;
                    if (!vif_unpack::fast(actual.data() + 16, source.data(), address, count, cl, wl, vn, vl,
                                          unsignedData != 0, false, trial % 2 ? 0 : 3, first, end))
                        return 2;
                    for (uint32_t i = 0; i < count; ++i) {
                        const uint32_t dest = (address + (i / wl) * cl + i % wl) & 1023u;
                        if (dest < 7) {
                            refFirst = std::min(refFirst, dest);
                            refEnd = std::max(refEnd, dest + 1);
                        }
                        uint32_t lanes[4];
                        std::memcpy(lanes, expected.data() + 16 + dest * 16, 16);
                        for (uint32_t field = 0; field < (components == 1 ? 4u : components); ++field) {
                            const uint32_t src = (i * components + (components == 1 ? 0 : field)) * bits / 8;
                            uint32_t raw = 0;
                            std::memcpy(&raw, source.data() + src, bits / 8);
                            if (!unsignedData && bits == 8)
                                raw = static_cast<int8_t>(raw);
                            if (!unsignedData && bits == 16)
                                raw = static_cast<int16_t>(raw);
                            lanes[field] = raw;
                        }
                        std::memcpy(expected.data() + 16 + dest * 16, lanes, 16);
                    }
                    if (actual != expected || first != refFirst || end != refEnd) {
                        std::printf("Mismatch vn=%u vl=%u unsigned=%u address=%u count=%u cl=%u wl=%u\n", vn, vl,
                                    unsignedData, address, count, cl, wl);
                        return 1;
                    }
                    ++cases;
                    vectors += count;
                }
    // These require the full ROW/COL/mask/fill interpreter; no memory or tap changes.
    for (uint32_t mode = 0; mode < 8; ++mode)
        for (uint32_t vl = 0; vl < 4; ++vl)
            for (uint32_t masked = 0; masked < 2; ++masked)
                for (uint32_t fill = 0; fill < 2; ++fill) {
                    if (!masked && !fill && vl < 3 && (mode % 4 == 0 || mode % 4 == 3))
                        continue;
                    actual = expected;
                    uint32_t first = 7, end = 0;
                    if (vif_unpack::fast(actual.data() + 16, expected.data(), 0, 1, 1, fill ? 2 : 1, 3, vl, false,
                                         masked != 0, mode, first, end) ||
                        actual != expected || first != 7 || end != 0)
                        return 3;
                }
    // STMASK / STMOD uploads against the scalar rules of PS2Memory::processVIF1Data.
    uint64_t maskedCases = 0;
    for (unsigned vn = 0; vn < 4; ++vn)
        for (unsigned vl = 0; vl < 3; ++vl)
            for (unsigned trial = 0; trial < 3000; ++trial) {
                const bool unsignedData = trial & 1u;
                const bool maskEnable = (trial >> 1) % 3 != 0;
                const uint32_t mode = (trial >> 2) & 3u;
                if (!maskEnable && (mode == 0 || mode == 3))
                    continue;
                const uint32_t address = random() % 1024;
                const uint32_t count = 1 + random() % 256;
                const uint32_t wl = trial % 5 ? 1 + random() % 8 : 1;
                const uint32_t cl = wl + random() % 6;
                const uint32_t mask = static_cast<uint32_t>(random());
                const uint32_t components = vn + 1, bits = 32u >> vl;
                std::vector<uint8_t> source(count * components * bits / 8u);
                for (auto& byte : source)
                    byte = static_cast<uint8_t>(random());
                for (auto& byte : expected)
                    byte = static_cast<uint8_t>(random());
                actual = expected;
                uint32_t row[4], col[4], refRow[4];
                for (uint32_t f = 0; f < 4; ++f) {
                    row[f] = refRow[f] = static_cast<uint32_t>(random());
                    col[f] = static_cast<uint32_t>(random());
                }
                uint32_t first = 7, end = 0, refFirst = 7, refEnd = 0;
                if (!vif_unpack::fast(actual.data() + 16, source.data(), address, count, cl, wl, vn, vl, unsignedData,
                                      maskEnable, mode, first, end, mask, row, col))
                    return 4;
                for (uint32_t i = 0; i < count; ++i) {
                    const uint32_t cycle = i % wl;
                    const uint32_t dest = (address + (i / wl) * cl + cycle) & 1023u;
                    if (dest < 7) {
                        refFirst = std::min(refFirst, dest);
                        refEnd = std::max(refEnd, dest + 1);
                    }
                    uint32_t lanes[4], data[4];
                    std::memcpy(lanes, expected.data() + 16 + dest * 16, 16);
                    std::memcpy(data, lanes, 16);
                    for (uint32_t field = 0; field < (components == 1 ? 4u : components); ++field) {
                        const uint32_t src = (i * components + (components == 1 ? 0 : field)) * bits / 8;
                        uint32_t raw = 0;
                        std::memcpy(&raw, source.data() + src, bits / 8);
                        if (!unsignedData && bits == 8)
                            raw = static_cast<int8_t>(raw);
                        if (!unsignedData && bits == 16)
                            raw = static_cast<int16_t>(raw);
                        data[field] = raw;
                    }
                    const uint32_t c = cycle > 3 ? 3 : cycle;
                    for (uint32_t field = 0; field < 4; ++field) {
                        const uint32_t spec = maskEnable ? (mask >> ((c * 4 + field) * 2)) & 3u : 0u;
                        if (spec == 0) {
                            uint32_t v = data[field];
                            if (mode == 1 || mode == 2) {
                                v += refRow[field];
                                if (mode == 2)
                                    refRow[field] = v;
                            }
                            lanes[field] = v;
                        } else if (spec == 1)
                            lanes[field] = refRow[field];
                        else if (spec == 2)
                            lanes[field] = col[c];
                    }
                    std::memcpy(expected.data() + 16 + dest * 16, lanes, 16);
                }
                if (actual != expected || first != refFirst || end != refEnd ||
                    std::memcmp(row, refRow, sizeof row) != 0) {
                    std::printf("Masked mismatch vn=%u vl=%u mode=%u mask=%u address=%u count=%u cl=%u wl=%u\n", vn, vl,
                                mode, maskEnable ? 1u : 0u, address, count, cl, wl);
                    return 5;
                }
                ++maskedCases;
            }
    std::printf("PASS: %llu exact uploads, %llu vectors, %llu masked/offset uploads; VU wrap, cycle gaps, signed data, "
                "preserved lanes, ROW/COL/mask, camera taps and fallback guards\n",
                static_cast<unsigned long long>(cases), static_cast<unsigned long long>(vectors),
                static_cast<unsigned long long>(maskedCases));
#else
    std::puts("Non-SSE2 target retains the reference path");
#endif
}
