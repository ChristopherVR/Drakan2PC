#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace gsr {
template <uint32_t Scale>
inline void expandRenderTargetRowsFixed(const uint32_t* src, uint32_t* dst, uint32_t width, uint32_t rows) {
    static_assert(Scale && !(Scale & (Scale - 1)));
    const size_t pitch = size_t(width) * Scale;
    for (uint32_t y = 0; y < rows; ++y) {
        uint32_t* row = dst + size_t(y) * Scale * pitch;
        const uint32_t* input = src + size_t(y) * width;
        for (uint32_t x = 0; x < width; ++x)
            std::fill_n(row + size_t(x) * Scale, Scale, input[x]);
        // Copy the completed rows in doubling runs, reducing memcpy calls
        // without overlapping either source and destination range.
        for (uint32_t copied = 1; copied < Scale; copied *= 2)
            std::memcpy(row + size_t(copied) * pitch, row, size_t(copied) * pitch * sizeof(uint32_t));
    }
}
// Replicate native framebuffer pixels exactly. Build each expanded row once,
// then copy it for the remaining scanlines instead of dividing at every pixel.
inline void expandRenderTargetRows(const uint32_t* src, uint32_t* dst, uint32_t width, uint32_t rows, uint32_t scale,
                                   bool specialize = true) {
    if (!width || !rows || !scale)
        return;
    if (specialize) {
        if (scale == 1) {
            std::memcpy(dst, src, size_t(width) * rows * sizeof(uint32_t));
            return;
        }
        if (scale == 2) {
            expandRenderTargetRowsFixed<2>(src, dst, width, rows);
            return;
        }
        if (scale == 4) {
            expandRenderTargetRowsFixed<4>(src, dst, width, rows);
            return;
        }
        if (scale == 8) {
            expandRenderTargetRowsFixed<8>(src, dst, width, rows);
            return;
        }
    }
    const size_t pitch = size_t(width) * scale;
    for (uint32_t y = 0; y < rows; ++y) {
        uint32_t* row = dst + size_t(y) * scale * pitch;
        const uint32_t* input = src + size_t(y) * width;
        if (scale == 1)
            std::memcpy(row, input, size_t(width) * sizeof(uint32_t));
        else
            for (uint32_t x = 0; x < width; ++x)
                std::fill_n(row + size_t(x) * scale, scale, input[x]);
        for (uint32_t repeat = 1; repeat < scale; ++repeat)
            std::memcpy(row + size_t(repeat) * pitch, row, pitch * sizeof(uint32_t));
    }
}
} // namespace gsr
