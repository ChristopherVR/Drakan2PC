#pragma once
#include "gs_pixel_pages.h"
#include <algorithm>
#if defined(_M_X64) || defined(__SSSE3__)
#include <tmmintrin.h>
#define GSR_UPLOAD_SSSE3 1
#endif

namespace gsr {
namespace image_upload {
#if GSR_UPLOAD_SSSE3
// PSMT8: four rows of 16 pixels (x and y aligned to 16 and 4) fill one 64-byte column of a block exactly. For the
// column index c inside the block, output vector k gathers its bytes from the four source rows with one PSHUFB each.
struct T8Columns {
    alignas(16) uint8_t mask[4][4][4][16]; // [column][output qword-pair k][source row j][byte]
    T8Columns() : mask() {
        std::memset(mask, 0x80, sizeof mask);
        for (uint32_t c = 0; c < 4; ++c)
            for (uint32_t j = 0; j < 4; ++j)
                for (uint32_t i = 0; i < 16; ++i) {
                    const uint32_t o = tables::col8[c * 4 + j][i] - c * 64u;
                    mask[c][o / 16][j][o % 16] = uint8_t(i);
                }
    }
};
inline const T8Columns& t8Columns() {
    static const T8Columns table;
    return table;
}

// Writes rows y..y+3 (y % 4 == 0) of x0..x0+width (x0 and width multiples of 16) from row-major input.
inline void writeT8Rows(uint8_t* memory, uint32_t bp, uint32_t bw, uint32_t x0, uint32_t y, uint32_t width,
                        const uint8_t* input) {
    using Page = pixel_pages::Page<PSM_T8>;
    const auto& offsets = Page::offsets();
    const auto& table = t8Columns();
    const uint32_t column = (y >> 2) & 3u;
    const uint32_t pageRow = (y / Page::height) * Page::stride(bw);
    const uint16_t* rowOffsets = offsets.data() + (y % Page::height) * Page::width;
    for (uint32_t g = 0; g < width; g += 16) {
        const uint32_t x = (x0 + g) & 2047u;
        const uint32_t base = bp * 256u + (pageRow + x / Page::width) * 8192u;
        const uint32_t columnOffset = rowOffsets[x % Page::width] & 0x1fc0u; // block + column, 64-byte aligned
        uint8_t* dest = memory + ((base + columnOffset) & (kVramSize - 1));
        const __m128i r0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(input + g));
        const __m128i r1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(input + width + g));
        const __m128i r2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(input + 2 * width + g));
        const __m128i r3 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(input + 3 * width + g));
        for (uint32_t k = 0; k < 4; ++k) {
            const auto* m = table.mask[column][k];
            __m128i v = _mm_shuffle_epi8(r0, _mm_load_si128(reinterpret_cast<const __m128i*>(m[0])));
            v = _mm_or_si128(v, _mm_shuffle_epi8(r1, _mm_load_si128(reinterpret_cast<const __m128i*>(m[1]))));
            v = _mm_or_si128(v, _mm_shuffle_epi8(r2, _mm_load_si128(reinterpret_cast<const __m128i*>(m[2]))));
            v = _mm_or_si128(v, _mm_shuffle_epi8(r3, _mm_load_si128(reinterpret_cast<const __m128i*>(m[3]))));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dest + k * 16u), v);
        }
    }
}
#endif

template <uint32_t Format>
void write(LocalMemory& mem, uint32_t bp, uint32_t bw, uint32_t x0, uint32_t y0, uint32_t width, uint32_t& cx,
           uint32_t& cy, uint64_t& remaining, const uint8_t* input, uint32_t bytes) {
    using Page = pixel_pages::Page<Format>;
    constexpr uint32_t bits = Format == PSM_T4 || Format == PSM_T4HL || Format == PSM_T4HH ? 4
                              : Format == PSM_T8 || Format == PSM_T8H                      ? 8
                              : Format == PSM_CT16 || Format == PSM_CT16S || Format == PSM_Z16 || Format == PSM_Z16S
                                  ? 16
                                  : 32;
    const uint32_t pixels = uint32_t(std::min<uint64_t>(remaining, uint64_t(bytes) * 8 / bits));
    const auto& offsets = Page::offsets();
    auto* memory = mem.data();
    uint32_t done = 0;
    while (done < pixels) {
        const uint32_t x = (x0 + cx) & 2047u, y = (y0 + cy) & 2047u;
#if GSR_UPLOAD_SSSE3
        if constexpr (Format == PSM_T8) {
            if (cx == 0 && (y & 3u) == 0 && (x0 & 15u) == 0 && (width & 15u) == 0 && width != 0 &&
                pixels - done >= 4u * width) {
                writeT8Rows(memory, bp, bw, x, y, width, input + done);
                done += 4u * width;
                remaining -= 4u * width;
                cy += 4;
                continue;
            }
        }
#endif
        const uint32_t count = std::min({pixels - done, width - cx, Page::width - x % Page::width});
        const auto* row = offsets.data() + (y % Page::height) * Page::width + x % Page::width;
        const uint32_t base = bp * 256u + ((y / Page::height) * Page::stride(bw) + x / Page::width) * 8192u;
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t offset = row[i];
            auto* p = memory + ((base + (offset & 0x7fffu)) & (kVramSize - 1));
            if constexpr (bits == 32)
                std::memcpy(p, input + size_t(done + i) * 4, 4);
            else if constexpr (bits == 16)
                std::memcpy(p, input + size_t(done + i) * 2, 2);
            else if constexpr (Format == PSM_T8)
                p[0] = input[done + i];
            else if constexpr (Format == PSM_T8H)
                p[3] = input[done + i];
            else {
                const uint32_t pixel = done + i;
                const uint8_t value = (input[pixel / 2] >> ((pixel & 1) * 4)) & 15u;
                if constexpr (Format == PSM_T4) {
                    const uint32_t shift = (offset >> 15) * 4;
                    p[0] = uint8_t((p[0] & ~(15u << shift)) | (uint32_t(value) << shift));
                } else if constexpr (Format == PSM_T4HL)
                    p[3] = (p[3] & 0xf0u) | value;
                else
                    p[3] = (p[3] & 0x0fu) | (value << 4);
            }
        }
        done += count;
        remaining -= count;
        cx += count;
        if (cx == width) {
            cx = 0;
            ++cy;
        }
    }
}
} // namespace image_upload
// Callers retain the existing transfer boundaries, stamps and upload callbacks.
// CT24/Z24 keep their byte-carry path because packets can split a 24-bit pixel.
inline bool writeImageChunk(LocalMemory& mem, uint32_t format, uint32_t bp, uint32_t bw, uint32_t x0, uint32_t y0,
                            uint32_t width, uint32_t& cx, uint32_t& cy, uint64_t& remaining, const uint8_t* input,
                            uint32_t bytes) {
#define GSR_UPLOAD_FORMAT(f)                                                                                           \
    case f:                                                                                                            \
        image_upload::write<f>(mem, bp, bw, x0, y0, width, cx, cy, remaining, input, bytes);                           \
        return true
    switch (format) {
        GSR_UPLOAD_FORMAT(PSM_CT32);
        GSR_UPLOAD_FORMAT(PSM_Z32);
        GSR_UPLOAD_FORMAT(PSM_CT16);
        GSR_UPLOAD_FORMAT(PSM_CT16S);
        GSR_UPLOAD_FORMAT(PSM_Z16);
        GSR_UPLOAD_FORMAT(PSM_Z16S);
        GSR_UPLOAD_FORMAT(PSM_T8);
        GSR_UPLOAD_FORMAT(PSM_T8H);
        GSR_UPLOAD_FORMAT(PSM_T4);
        GSR_UPLOAD_FORMAT(PSM_T4HL);
        GSR_UPLOAD_FORMAT(PSM_T4HH);
        default:
            return false;
    }
#undef GSR_UPLOAD_FORMAT
}
} // namespace gsr
