#pragma once
#include "gs_mem.h"
#include <array>
#include <unordered_map>
#include <vector>

namespace gsr {
// Exact written bits of a small, possibly fragmented GS image upload. Masks use
// physical VRAM words so every upload format can patch a CT32 target without
// copying stale CPU bytes over GPU-owned channels or neighbouring texels.
class PartialUpload {
    struct Write {
        uint32_t address, mask;
    };
    std::vector<Write> writes;
    size_t written = 0;

public:
    std::unordered_map<uint32_t, std::array<uint32_t, 64>> blocks;
    void clear() {
        writes.clear();
        blocks.clear();
        written = 0;
    }
    bool prepare(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
        clear();
        if (!w || !h || uint64_t(w) * h > 4096)
            return false;
        writes.reserve(size_t(w) * h);
        for (uint32_t row = 0; row < h; ++row)
            for (uint32_t col = 0; col < w; ++col) {
                uint32_t nibble = 0;
                uint32_t a = pixelAddr(psm, bp, bw, (x + col) & 2047, (y + row) & 2047, &nibble) & (kVramSize - 1);
                uint32_t mask;
                switch (psm) {
                    case PSM_CT32:
                    case PSM_Z32:
                        mask = ~0u;
                        break;
                    case PSM_CT24:
                    case PSM_Z24:
                        mask = 0xffffffu;
                        break;
                    case PSM_CT16:
                    case PSM_CT16S:
                    case PSM_Z16:
                    case PSM_Z16S:
                        mask = 0xffffu << ((a & 3) * 8);
                        break;
                    case PSM_T8:
                        mask = 0xffu << ((a & 3) * 8);
                        break;
                    case PSM_T4:
                        mask = 0xfu << ((a & 3) * 8 + nibble * 4);
                        break;
                    case PSM_T8H:
                        mask = 0xff000000u;
                        break;
                    case PSM_T4HL:
                        mask = 0x0f000000u;
                        break;
                    case PSM_T4HH:
                        mask = 0xf0000000u;
                        break;
                    default:
                        clear();
                        return false;
                }
                writes.push_back({a & ~3u, mask});
                blocks.try_emplace(a >> 8, std::array<uint32_t, 64>{});
            }
        return true;
    }
    void advance(uint64_t pixels) {
        const size_t end = size_t(std::min<uint64_t>(pixels, writes.size()));
        while (written < end) {
            const auto& w = writes[written++];
            blocks[w.address >> 8][(w.address & 255) / 4] |= w.mask;
        }
    }
    uint32_t mask(uint32_t address) const {
        const auto found = blocks.find((address & (kVramSize - 1)) >> 8);
        return found == blocks.end() ? 0 : found->second[(address & 255) / 4];
    }
};
} // namespace gsr
