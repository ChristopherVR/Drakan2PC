#include "gs_image_upload.h"
#include <cassert>
#include <cstdio>
#include <iterator>
#include <vector>
using namespace gsr;
uint32_t seed = 0x1256f0ab;
uint32_t randomValue() {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}
void reference(LocalMemory& mem, uint32_t format, uint32_t bp, uint32_t bw, uint32_t x0, uint32_t y0, uint32_t width,
               uint32_t& cx, uint32_t& cy, uint64_t& remaining, const uint8_t* data, uint32_t bytes) {
    const auto bits = transferBits(format);
    for (uint32_t i = 0; i < uint64_t(bytes) * 8 / bits && remaining; ++i) {
        uint32_t v = 0;
        if (bits == 32)
            std::memcpy(&v, data + size_t(i) * 4, 4);
        else if (bits == 16) {
            uint16_t half;
            std::memcpy(&half, data + size_t(i) * 2, 2);
            v = half;
        } else if (bits == 8)
            v = data[i];
        else
            v = (data[i / 2] >> ((i & 1) * 4)) & 15u;
        mem.write(format, bp, bw, (x0 + cx) & 2047, (y0 + cy) & 2047, v);
        --remaining;
        if (++cx == width) {
            cx = 0;
            ++cy;
        }
    }
}
int main() {
    LocalMemory expected, actual;
    for (size_t i = 0; i < kVramSize; ++i)
        expected.data()[i] = uint8_t(randomValue());
    const uint32_t formats[] = {PSM_CT32, PSM_Z32, PSM_CT16, PSM_CT16S, PSM_Z16, PSM_Z16S,
                                PSM_T8,   PSM_T8H, PSM_T4,   PSM_T4HL,  PSM_T4HH};
    // Aligned PSMT8 rectangles also take the four-row column path.
    const std::pair<uint32_t, uint32_t> sizes[] = {{1u, 1u},  {3u, 5u},   {33u, 17u}, {129u, 133u}, {257u, 65u},
                                                   {16u, 4u}, {16u, 13u}, {64u, 20u}, {128u, 64u},  {48u, 9u}};
    const std::pair<uint32_t, uint32_t> origins[] = {{0u, 0u}, {63u, 31u}, {2040u, 2047u}, {32u, 8u}, {2032u, 2044u}};
    size_t cases = 0, packets = 0;
    for (auto format : formats)
        for (auto bp : {0u, 31u, 16383u})
            for (auto bw : {0u, 1u, 3u, 8u})
                for (size_t si = 0; si < std::size(sizes); ++si)
                    for (size_t oi = 0; oi < std::size(origins); ++oi) {
                        // The aligned shapes only matter for PSMT8; keep the other formats on the original grid.
                        if (format != PSM_T8 && (si >= 5 || oi >= 3))
                            continue;
                        const auto wh = sizes[si];
                        const auto xy = origins[oi];
                        std::memcpy(actual.data(), expected.data(), kVramSize);
                        const size_t bytes = (uint64_t(wh.first) * wh.second * transferBits(format) + 7) / 8;
                        std::vector<uint8_t> stream((bytes + 15) & ~size_t(15));
                        for (auto& b : stream)
                            b = uint8_t(randomValue());
                        uint32_t ex = 0, ey = 0, ax = 0, ay = 0;
                        uint64_t er = uint64_t(wh.first) * wh.second, ar = er;
                        for (size_t at = 0; at < stream.size();) {
                            const uint32_t take =
                                uint32_t(std::min<size_t>((1 + randomValue() % 20) * 16, stream.size() - at));
                            reference(expected, format, bp, bw, xy.first, xy.second, wh.first, ex, ey, er,
                                      stream.data() + at, take);
                            assert(writeImageChunk(actual, format, bp, bw, xy.first, xy.second, wh.first, ax, ay, ar,
                                                   stream.data() + at, take));
                            assert(ex == ax && ey == ay && er == ar);
                            ++packets;
                            at += take;
                        }
                        assert(ar == 0 && actual.clock() == 1);
                        if (std::memcmp(actual.data(), expected.data(), kVramSize)) {
                            std::fprintf(stderr, "Upload mismatch psm=%u bp=%u bw=%u xy=%u,%u size=%u,%u\n", format, bp,
                                         bw, xy.first, xy.second, wh.first, wh.second);
                            return 1;
                        }
                        ++cases;
                    }
    uint32_t x = 0, y = 0;
    uint64_t remain = 1;
    uint8_t bytes[16]{};
    assert(!writeImageChunk(actual, PSM_CT24, 0, 1, 0, 0, 1, x, y, remain, bytes, 16));
    assert(!writeImageChunk(actual, PSM_Z24, 0, 1, 0, 0, 1, x, y, remain, bytes, 16));
    std::printf("Image upload: %zu exact full-VRAM cases, %zu split packets; cursor, nibble, preserved bits and wrap "
                "checks passed\n",
                cases, packets);
}
