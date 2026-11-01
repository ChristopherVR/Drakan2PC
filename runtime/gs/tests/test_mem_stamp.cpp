// LocalMemory::maxStamp (page-accelerated) must equal a block-by-block maximum for every range, aligned or not,
// including ranges that wrap past the end of memory, after any mix of touchBlocks / touchRect.
#include "gs_mem.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace gsr;

static uint32_t seedValue = 0x2545f491;
static uint32_t randomValue() {
    seedValue ^= seedValue << 13;
    seedValue ^= seedValue >> 17;
    seedValue ^= seedValue << 5;
    return seedValue;
}

static uint64_t bruteForce(const LocalMemory& mem, uint32_t first, uint32_t count) {
    uint64_t m = 0;
    for (uint32_t i = 0; i < count; ++i)
        m = std::max(m, mem.blockStamp(first + i));
    return m;
}

// readRectCT32 must return exactly the words read() returns pixel by pixel, for any rectangle (block aligned or not),
// page base and buffer width.
static int rectTest() {
    LocalMemory mem;
    uint8_t* data = mem.data();
    for (size_t i = 0; i < kVramSize; i += 4) {
        const uint32_t v = randomValue();
        std::memcpy(data + i, &v, 4);
    }
    std::vector<uint32_t> out;
    uint64_t pixels = 0;
    for (int i = 0; i < 3000; ++i) {
        const uint32_t bp = (randomValue() % 200) * 32 + (randomValue() % 5 == 0 ? randomValue() % 32 : 0);
        const uint32_t bw = 1 + randomValue() % 8;
        const uint32_t x0 = randomValue() % 3 ? (randomValue() % 8) * 8 : randomValue() % 100;
        const uint32_t y0 = randomValue() % 3 ? (randomValue() % 8) * 8 : randomValue() % 100;
        const uint32_t w = 1 + randomValue() % 160, h = 1 + randomValue() % 100;
        out.assign((size_t)w * h, 0xdeadbeef);
        mem.readRectCT32(bp, bw, x0, y0, w, h, out.data());
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                const uint32_t want = mem.read(PSM_CT32, bp, bw, x0 + x, y0 + y);
                if (out[(size_t)y * w + x] != want) {
                    std::fprintf(stderr, "readRectCT32 bp=%u bw=%u (%u,%u) %ux%u: pixel (%u,%u) = %08x, want %08x\n",
                                 bp, bw, x0, y0, w, h, x, y, out[(size_t)y * w + x], want);
                    return 1;
                }
                ++pixels;
            }
    }
    std::printf("PASS readRectCT32 %llu pixels\n", (unsigned long long)pixels);
    return 0;
}

int main() {
    if (rectTest())
        return 1;
    LocalMemory mem;
    uint64_t checked = 0;
    for (int round = 0; round < 400; ++round) {
        const unsigned writes = 1 + randomValue() % 6;
        for (unsigned w = 0; w < writes; ++w) {
            if (randomValue() & 1)
                mem.touchBlocks(randomValue() % kVramBlocks, 1 + randomValue() % 300);
            else
                mem.touchRect(randomValue() % 3 ? PSM_CT32 : PSM_T8, (randomValue() % 400) * 32, 1 + randomValue() % 8,
                              randomValue() % 64, randomValue() % 64, 1 + randomValue() % 200, 1 + randomValue() % 200);
        }
        for (int q = 0; q < 200; ++q) {
            uint32_t first, count;
            switch (randomValue() % 4) {
                case 0: // whole page rows, the render-target case
                    first = (randomValue() % (kVramBlocks / 32)) * 32;
                    count = (1 + randomValue() % 8) * 32;
                    break;
                case 1: // page aligned start, ragged length
                    first = (randomValue() % (kVramBlocks / 32)) * 32;
                    count = 1 + randomValue() % 300;
                    break;
                case 2: // wraps past the end
                    first = kVramBlocks - 1 - randomValue() % 70;
                    count = 1 + randomValue() % 200;
                    break;
                default:
                    first = randomValue() % kVramBlocks;
                    count = randomValue() % 400;
                    break;
            }
            const uint64_t expected = bruteForce(mem, first, count);
            const uint64_t got = mem.maxStamp(first, count);
            if (expected != got) {
                std::fprintf(stderr, "maxStamp(%u, %u) = %llu, expected %llu\n", first, count, (unsigned long long)got,
                             (unsigned long long)expected);
                return 1;
            }
            ++checked;
        }
    }
    std::printf("PASS %llu ranges\n", (unsigned long long)checked);
    return 0;
}
