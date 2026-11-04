#include "rt_upload.h"
#include "partial_upload.h"
#include <cstring>
#include <cstdio>
#include <vector>
#include <chrono>

#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
static double
benchmarkUpload(uint32_t width, uint32_t rows, uint32_t scale, bool specialize, uint64_t& checksum) {
    std::vector<uint32_t> input(size_t(width) * rows, 0x7f314589);
    std::vector<uint32_t> output(input.size() * scale * scale);
    constexpr unsigned kIterations = 20000;
    for (unsigned i = 0; i < 200; ++i)
        gsr::expandRenderTargetRows(input.data(), output.data(), width, rows, scale, specialize);
    const auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < kIterations; ++i) {
        input[0] = i;
        gsr::expandRenderTargetRows(input.data(), output.data(), width, rows, scale, specialize);
        checksum += output[0];
    }
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / kIterations;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--benchmark") == 0) {
        uint64_t checksum = 0;
        for (const uint32_t scale : {2u, 4u, 8u})
            for (const uint32_t width : {16u, 128u, 512u}) {
                double times[2]{};
                const bool reverse = argc > 2 && std::strcmp(argv[2], "--reverse") == 0;
                for (unsigned step = 0; step < 2; ++step) {
                    const unsigned algorithm = reverse ? 1 - step : step;
                    times[algorithm] = benchmarkUpload(width, 8, scale, algorithm != 0, checksum);
                }
                std::printf("width=%u rows=8 scale=%u generic_ns=%.1f specialized_ns=%.1f\n", width, scale, times[0],
                            times[1]);
            }
        std::printf("checksum=%llu\n", static_cast<unsigned long long>(checksum));
        return 0;
    }
    {
        using namespace gsr;
        LocalMemory cpu, reference;
        std::vector<uint8_t> merged(kVramSize);
        size_t checks = 0;
        for (uint32_t psm : {PSM_CT32, PSM_CT24, PSM_CT16, PSM_CT16S, PSM_Z32, PSM_Z24, PSM_Z16, PSM_Z16S, PSM_T8,
                             PSM_T4, PSM_T8H, PSM_T4HL, PSM_T4HH})
            for (uint32_t test = 0; test < 12; ++test) {
                const uint32_t w = 1 + test * 2, h = 1 + test % 7, x = test == 11 ? 2044 : 3 + test,
                               y = test == 11 ? 2046 : 5 + test, bp = test & 1 ? kVramBlocks - 1 : 0x125;
                PartialUpload upload;
                if (!upload.prepare(psm, bp, 3, x, y, w, h))
                    return 1;
                std::memset(cpu.data(), 0xa5, kVramSize); // stale CPU bytes deliberately differ from GPU bytes
                std::memset(reference.data(), 0x5a, kVramSize);
                uint32_t written = 0;
                for (uint32_t prefix : {0u, 1u, w * h / 2, w * h - 1, w * h}) {
                    while (written < prefix) {
                        const uint32_t xx = (x + written % w) & 2047, yy = (y + written / w) & 2047;
                        const uint32_t value = written * 2654435761u + 0x7f908321u;
                        cpu.write(psm, bp, 3, xx, yy, value);
                        reference.write(psm, bp, 3, xx, yy, value);
                        ++written;
                    }
                    upload.advance(prefix);
                    std::memset(merged.data(), 0x5a, kVramSize);
                    for (const auto& [block, masks] : upload.blocks)
                        for (uint32_t word = 0; word < 64; ++word) {
                            const uint32_t at = block * 256 + word * 4;
                            uint32_t input;
                            std::memcpy(&input, cpu.data() + at, 4);
                            const uint32_t output = (0x5a5a5a5au & ~masks[word]) | (input & masks[word]);
                            std::memcpy(merged.data() + at, &output, 4);
                        }
                    if (std::memcmp(merged.data(), reference.data(), kVramSize)) {
                        std::fprintf(stderr, "Partial mask mismatch: psm=%x test=%u prefix=%u\n", psm, test, prefix);
                        return 1;
                    }
                    ++checks;
                }
            }
        std::printf(
            "Partial uploads: %zu whole-VRAM comparisons, all formats, fragmentation, abort prefixes and wrap\n",
            checks);
    }
    size_t checks = 0;
    for (uint32_t width : {1u, 7u, 64u, 128u, 512u})
        for (uint32_t rows : {1u, 3u, 32u, 64u})
            for (uint32_t scale = 1; scale <= 8; ++scale) {
                std::vector<uint32_t> input(size_t(width) * rows);
                for (size_t i = 0; i < input.size(); ++i)
                    input[i] = uint32_t(i * 2654435761u); // all channels, including alpha
                const size_t pitch = size_t(width) * scale, count = pitch * rows * scale;
                std::vector<uint32_t> output(count + 2, 0xdeadbeefu);
                gsr::expandRenderTargetRows(input.data(), output.data() + 1, width, rows, scale);
                if (output.front() != 0xdeadbeefu || output.back() != 0xdeadbeefu)
                    return 1;
                for (uint32_t y = 0; y < rows * scale; ++y)
                    for (size_t x = 0; x < pitch; ++x) {
                        ++checks;
                        if (output[1 + size_t(y) * pitch + x] != input[size_t(y / scale) * width + x / scale]) {
                            std::fprintf(stderr, "Mismatch: width=%u rows=%u scale=%u x=%zu y=%u\n", width, rows, scale,
                                         x, y);
                            return 1;
                        }
                    }
            }
    // Empty dimensions must not dereference pointers.
    gsr::expandRenderTargetRows(nullptr, nullptr, 0, 1, 4);
    gsr::expandRenderTargetRows(nullptr, nullptr, 1, 0, 4);
    gsr::expandRenderTargetRows(nullptr, nullptr, 1, 1, 0);
    std::printf("%zu exact pixel checks passed\n", checks);
}
