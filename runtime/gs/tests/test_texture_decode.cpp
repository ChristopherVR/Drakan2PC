#include "gs_texdecode.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif
using namespace gsr;
NOINLINE void referenceDecode(const LocalMemory& mem, uint32_t psm, uint32_t tbp, uint32_t tbw, uint32_t w, uint32_t h,
                              uint64_t texa, const ClutBuf& clut, uint32_t csa, std::vector<uint32_t>& out) {
    out.resize((size_t)w * h);
    tbw = tbw > 1u ? tbw : 1u;
    const uint32_t cbase = (psm == PSM_T4 || psm == PSM_T4HL || psm == PSM_T4HH) ? csa * 16u : 0u;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const uint32_t v = mem.read(psm, tbp, tbw, x, y);
            uint32_t c;
            switch (psm) {
                case PSM_CT32:
                    c = v;
                    break;
                case PSM_CT24:
                    c = expand24(v, texa);
                    break;
                case PSM_CT16:
                case PSM_CT16S:
                    c = expand16(v, texa);
                    break;
                default:
                    c = clutColor(clut, cbase + v, texa);
                    break;
            }
            out[(size_t)y * w + x] = c;
        }
}

NOINLINE void candidateDecode(const LocalMemory& mem, uint32_t p, uint32_t bp, uint32_t bw, uint32_t w, uint32_t h,
                              uint64_t a, const ClutBuf& c, uint32_t s, std::vector<uint32_t>& out) {
    decodeTextureLevel(mem, p, bp, bw, w, h, a, c, s, out);
}
uint32_t randomState = 0x31415926;
uint32_t rng() {
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return randomState;
}
using Decoder = decltype(&referenceDecode);
volatile uint64_t benchmarkSink = 0;
double measure(Decoder fn, const LocalMemory& mem, uint32_t format, uint32_t side, const ClutBuf& clut,
               unsigned iterations) {
    std::vector<uint32_t> out;
    out.reserve(size_t(side) * side);
    fn(mem, format, 16380, 4, side, side, 0x8000000080ull, clut, 31, out);
    const auto start = std::chrono::steady_clock::now();
    uint64_t check = 0;
    for (unsigned n = 0; n < iterations; ++n) {
        fn(mem, format, 16380, 4, side, side, 0x8000000080ull, clut, 31, out);
        check += out[(n * 17u) % out.size()];
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    benchmarkSink = check;
    return ms;
}
int main() {
    LocalMemory mem;
    for (size_t i = 0; i < kVramSize; ++i)
        mem.data()[i] = uint8_t(rng());
    const uint32_t formats[] = {PSM_CT32, PSM_CT24, PSM_CT16, PSM_CT16S, PSM_T8,   PSM_T4, PSM_T8H,    PSM_T4HL,
                                PSM_T4HH, PSM_Z32,  PSM_Z24,  PSM_Z16,   PSM_Z16S, 0x3fu,  0xffffffffu};
    const uint64_t texas[] = {0ull, 0x80ull, 0x8000000080ull, 0x80008000ull, 0xff00008001ull, 0x123456789abcdef0ull};
    const uint32_t csas[] = {0, 1, 15, 31, 32, 511, 0xffffffffu};
    const uint32_t bases[] = {0, 16380, 16383, 16387};
    const uint32_t widths[] = {0, 1, 2, 8, 63};
    const std::pair<uint32_t, uint32_t> dimensions[] = {{0, 17}, {17, 0}, {1, 1},   {3, 5},
                                                        {8, 8},  {17, 9}, {65, 33}, {129, 65}};
    size_t checks = 0, pixels = 0;
    std::vector<uint32_t> expected, actual;
    for (unsigned paletteMode = 0; paletteMode < 4; ++paletteMode) {
        ClutBuf clut;
        clut.is16 = paletteMode == 2 || paletteMode == 3;
        clut.cpsm = paletteMode == 1 ? PSM_CT24 : paletteMode == 3 ? PSM_CT16S : PSM_CT32;
        for (auto& x : clut.raw)
            x = rng();
        clut.raw[0] = 0;
        clut.raw[1] = 0x8000;
        clut.raw[511] = 0xff000000;
        for (auto f : formats)
            for (auto a : texas)
                for (auto csa : csas)
                    for (auto bp : bases)
                        for (auto bw : widths)
                            for (auto wh : dimensions) {
                                referenceDecode(mem, f, bp, bw, wh.first, wh.second, a, clut, csa, expected);
                                candidateDecode(mem, f, bp, bw, wh.first, wh.second, a, clut, csa, actual);
                                if (expected != actual) {
                                    std::fprintf(
                                        stderr, "Mismatch psm=%x texa=%llx csa=%u bp=%u bw=%u size=%ux%u palette=%u\n",
                                        f, (unsigned long long)a, csa, bp, bw, wh.first, wh.second, paletteMode);
                                    return 1;
                                }
                                ++checks;
                                pixels += expected.size();
                            }
    }
    // Large dimensions, zeroed memory, mutations, and CLUT/TEXA updates between calls.
    for (unsigned pattern = 0; pattern < 3; ++pattern) {
        if (pattern == 1)
            std::memset(mem.data(), 0, kVramSize);
        if (pattern == 2)
            std::memset(mem.data(), 0xff, kVramSize);
        ClutBuf clut;
        clut.is16 = true;
        clut.cpsm = PSM_CT16;
        for (auto& x : clut.raw)
            x = rng();
        clut.raw[0] = 0;
        clut.raw[511] = 0x8000;
        for (auto f : formats)
            for (auto wh : {std::pair<uint32_t, uint32_t>{257, 129}, {1024, 17}, {33, 1025}}) {
                referenceDecode(mem, f, 16383, 0, wh.first, wh.second, 0xff00008080ull, clut, 0xffffffffu, expected);
                candidateDecode(mem, f, 16383, 0, wh.first, wh.second, 0xff00008080ull, clut, 0xffffffffu, actual);
                if (expected != actual)
                    return 2;
                ++checks;
                pixels += expected.size();
            }
    }
    std::printf("{\"equivalence_cases\":%zu,\"exact_pixels\":%zu,\"benchmarks\":[\n", checks, pixels);
    ClutBuf clut;
    clut.is16 = true;
    clut.cpsm = PSM_CT16;
    for (auto& x : clut.raw)
        x = rng();
    for (size_t i = 0; i < kVramSize; ++i)
        mem.data()[i] = uint8_t(rng());
    bool first = true;
    for (auto f : {PSM_CT32, PSM_CT24, PSM_CT16, PSM_CT16S, PSM_T8, PSM_T4, PSM_T8H, PSM_T4HL, PSM_T4HH})
        for (auto side : {1u, 4u, 16u, 64u, 128u, 256u}) {
            unsigned iterations = std::max(4u, 524288u / (side * side));
            std::vector<double> ratios, oldTimes, newTimes;
            for (unsigned repeat = 0; repeat < 5; ++repeat) {
                double oldMs, newMs;
                if (repeat & 1) {
                    newMs = measure(candidateDecode, mem, f, side, clut, iterations);
                    oldMs = measure(referenceDecode, mem, f, side, clut, iterations);
                } else {
                    oldMs = measure(referenceDecode, mem, f, side, clut, iterations);
                    newMs = measure(candidateDecode, mem, f, side, clut, iterations);
                }
                oldTimes.push_back(oldMs);
                newTimes.push_back(newMs);
                ratios.push_back(oldMs / newMs);
            }
            std::sort(ratios.begin(), ratios.end());
            std::sort(oldTimes.begin(), oldTimes.end());
            std::sort(newTimes.begin(), newTimes.end());
            std::printf("%s{\"psm\":%u,\"side\":%u,\"iterations\":%u,\"old_median_ms\":%.6f,\"new_median_ms\":%.6f,"
                        "\"paired_median_speedup\":%.4f}",
                        first ? "" : ",\n", unsigned(f), side, iterations, oldTimes[2], newTimes[2], ratios[2]);
            first = false;
        }
    std::printf("\n]}\n");
    return 0;
}
