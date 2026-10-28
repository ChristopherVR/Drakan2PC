// HD movie frame replacement: shared definitions for the host (which knows the decoded movie frames) and the renderer
// (which sees them arrive as 16x16 image uploads). See docs/reference/fmv.md section 9. Standard C++, no platform API.
#pragma once
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

namespace gsr {

// An HD movie frame, RGBA8 (R in the low byte), row-major, width * height entries. Alpha is ignored.
struct MovieImage {
    uint32_t width = 0, height = 0;
    std::vector<uint32_t> rgba;
};

// Called on the GS thread when a frame buffer holds a complete registered movie frame; may block briefly while the
// host decodes the HD frame. nullptr (or an empty image) keeps the native frame.
using MovieFrameSource = std::function<std::shared_ptr<const MovieImage>(uint32_t movie, uint32_t frame)>;

namespace movie {

constexpr uint32_t kBlock = 16;                        // the game stamps the IPU output one macroblock at a time
constexpr uint32_t kBlockBytes = kBlock * kBlock * 4u; // RGBA32 as the IPU writes it (alpha 0x80)

// Hash of one macroblock's 1024 bytes (rows of 16 RGBA32 pixels, as uploaded to the GS).
inline uint64_t blockHash(const uint8_t* px) {
    uint64_t h = 0x9e3779b97f4a7c15ull;
    for (uint32_t i = 0; i < kBlockBytes; i += 8) {
        uint64_t w;
        std::memcpy(&w, px + i, 8);
        h = (h ^ w) * 0xff51afd7ed558ccdull;
        h ^= h >> 29;
    }
    return h;
}

// Contribution of one macroblock to a frame signature. The signature is the 64-bit sum of the terms of all macroblocks
// of the frame: independent of upload order, so the renderer can keep it current incrementally, and a sum (not xor), so
// repeated identical macroblocks (black borders) do not cancel out.
inline uint64_t blockTerm(uint64_t blockHash) {
    uint64_t z = blockHash + 0x9e3779b97f4a7c15ull;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

} // namespace movie
} // namespace gsr
