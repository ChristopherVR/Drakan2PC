#pragma once
#include "gs_mem.h"
#include <array>

namespace gsr::pixel_pages {
// A GS page always contains 8192 bytes. Base block and buffer stride only
// translate this fixed intra-page permutation, including unaligned bases.
template <uint32_t Format>
struct Page {
    static constexpr bool indexed = Format == PSM_T8 || Format == PSM_T4;
    static constexpr uint32_t width = indexed ? 128 : 64;
    static constexpr uint32_t height =
        Format == PSM_T4
            ? 128
            : (Format == PSM_T8 || Format == PSM_CT16 || Format == PSM_CT16S || Format == PSM_Z16 || Format == PSM_Z16S
                   ? 64
                   : 32);
    static const auto& offsets() {
        static const auto table = [] {
            std::array<uint16_t, width * height> result{};
            for (uint32_t y = 0; y < height; ++y)
                for (uint32_t x = 0; x < width; ++x) {
                    uint32_t nibble = 0;
                    result[y * width + x] = uint16_t(pixelAddr(Format, 0, 1, x, y, &nibble) | (nibble << 15));
                }
            return result;
        }();
        return table;
    }
    static uint32_t stride(uint32_t bw) { return indexed ? ((bw >> 1) ? bw >> 1 : 1u) : (bw ? bw : 1u); }
};
} // namespace gsr::pixel_pages
