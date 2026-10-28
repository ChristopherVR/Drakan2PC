#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace gsr {
// Replicate native framebuffer pixels exactly. Build each expanded row once,
// then copy it for the remaining scanlines instead of dividing at every pixel.
inline void expandRenderTargetRows(const uint32_t* src, uint32_t* dst, uint32_t width, uint32_t rows, uint32_t scale) {
    if (!width || !rows || !scale)
        return;
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
