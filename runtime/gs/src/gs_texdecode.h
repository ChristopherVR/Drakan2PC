// Texture decode from GS local memory to RGBA8 (R in the low byte, alpha in GS units: 0x80 = opaque for CT32). Pure CPU
// code, no D3D11: part of the portable gsr core (see docs/reference/portability.md); the D3D11 backend only uploads the
// result.
#pragma once
#include "gs_internal.h"
#include "gs_pixel_pages.h"

#include <cstdint>
#include <array>
#include <algorithm>
#include <vector>

namespace gsr {

inline uint32_t expand16(uint32_t v, uint64_t texa) {
    const uint32_t ta0 = (uint32_t)(texa & 0xFF), ta1 = (uint32_t)((texa >> 32) & 0xFF);
    const bool aem = (texa >> 15) & 1;
    const uint32_t r = (v & 31) << 3, g = ((v >> 5) & 31) << 3, b = ((v >> 10) & 31) << 3;
    uint32_t a = (v & 0x8000) ? ta1 : ta0;
    // AEM suppresses the all-zero 16-bit texel, not opaque black (0x8000).
    if (aem && (v & 0xFFFF) == 0)
        a = 0;
    return r | (g << 8) | (b << 16) | (a << 24);
}

inline uint32_t expand24(uint32_t v, uint64_t texa) {
    const uint32_t ta0 = (uint32_t)(texa & 0xFF);
    const bool aem = (texa >> 15) & 1;
    uint32_t a = ta0;
    if (aem && (v & 0xFFFFFF) == 0)
        a = 0;
    return (v & 0xFFFFFF) | (a << 24);
}

inline uint32_t clutColor(const ClutBuf& c, uint32_t idx, uint64_t texa) {
    const uint32_t raw = c.raw[idx & 511];
    if (c.is16)
        return expand16(raw & 0xFFFF, texa);
    if (c.cpsm == PSM_CT24)
        return expand24(raw, texa);
    return raw;
}

namespace detail {
// Format is constant through address/read and expansion, so the compiler can
// remove the format switches from the pixel loop. Unknown formats retain the
// LocalMemory::read fallback semantics.
template <uint32_t Format, bool PreExpanded = true>
inline void decodeTexturePixels(const LocalMemory& mem, uint32_t runtimeFormat, uint32_t tbp, uint32_t tbw, uint32_t w,
                                uint32_t h, uint64_t texa, const uint32_t* palette, std::vector<uint32_t>& out,
                                const ClutBuf* clut = nullptr, uint32_t paletteBase = 0) {
    auto color = [&](uint32_t v) -> uint32_t {
        if constexpr (Format == PSM_CT32)
            return v;
        else if constexpr (Format == PSM_CT24)
            return expand24(v, texa);
        else if constexpr (Format == PSM_CT16 || Format == PSM_CT16S)
            return expand16(v, texa);
        else if constexpr (!PreExpanded)
            return clutColor(*clut, paletteBase + v, texa);
        else if constexpr (Format == PSM_T8 || Format == PSM_T8H || Format == PSM_T4 || Format == PSM_T4HL ||
                           Format == PSM_T4HH)
            return palette[v];
        else
            return palette[v & 511u];
    };
    if constexpr (Format != 0xffffffffu) {
        if (size_t(w) * h > 64) {
            using Page = pixel_pages::Page<Format>;
            constexpr uint32_t pageW = Page::width, pageH = Page::height;
            // The intra-page layout is independent of base address and stride.
            // Keep nibble selection in bit 15 for T4; all byte offsets fit 13 bits.
            const auto& offsets = Page::offsets();
            const uint32_t stride = Page::stride(tbw);
            const auto* data = mem.data();
            for (uint32_t y = 0; y < h; ++y) {
                const auto* row = offsets.data() + (y % pageH) * pageW;
                const uint32_t base = tbp * 256u + (y / pageH) * stride * 8192u;
                auto* dest = out.data() + size_t(y) * w;
                for (uint32_t pageX = 0; pageX < w; pageX += pageW) {
                    const uint32_t pageBase = base + (pageX / pageW) * 8192u;
                    const uint32_t count = std::min(pageW, w - pageX);
                    for (uint32_t x = 0; x < count; ++x) {
                        const uint32_t offset = row[x];
                        const auto* p = data + ((pageBase + (offset & 0x7fffu)) & (kVramSize - 1));
                        uint32_t v = 0;
                        if constexpr (Format == PSM_CT32 || Format == PSM_CT24 || Format == PSM_Z32 ||
                                      Format == PSM_Z24) {
                            std::memcpy(&v, p, 4);
                            if constexpr (Format == PSM_CT24 || Format == PSM_Z24)
                                v &= 0xffffffu;
                        } else if constexpr (Format == PSM_CT16 || Format == PSM_CT16S || Format == PSM_Z16 ||
                                             Format == PSM_Z16S) {
                            uint16_t half;
                            std::memcpy(&half, p, 2);
                            v = half;
                        } else if constexpr (Format == PSM_T8)
                            v = p[0];
                        else if constexpr (Format == PSM_T8H)
                            v = p[3];
                        else if constexpr (Format == PSM_T4)
                            v = (p[0] >> ((offset >> 15) * 4)) & 15u;
                        else if constexpr (Format == PSM_T4HL)
                            v = p[3] & 15u;
                        else if constexpr (Format == PSM_T4HH)
                            v = p[3] >> 4;
                        dest[pageX + x] = color(v);
                    }
                }
            }
            return;
        }
    }
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const uint32_t v = mem.read(Format == 0xffffffffu ? runtimeFormat : Format, tbp, tbw, x, y);
            out[size_t(y) * w + x] = color(v);
        }
}

template <uint32_t Format>
inline void decodeTextureFormat(const LocalMemory& mem, uint32_t runtimeFormat, uint32_t tbp, uint32_t tbw, uint32_t w,
                                uint32_t h, uint64_t texa, const ClutBuf& clut, uint32_t csa,
                                std::vector<uint32_t>& out) {
    if constexpr (Format == PSM_CT32 || Format == PSM_CT24 || Format == PSM_CT16 || Format == PSM_CT16S) {
        decodeTexturePixels<Format>(mem, runtimeFormat, tbp, tbw, w, h, texa, nullptr, out);
    } else {
        constexpr bool fourBit = Format == PSM_T4 || Format == PSM_T4HL || Format == PSM_T4HH;
        constexpr uint32_t count = fourBit ? 16u : (Format == PSM_T8 || Format == PSM_T8H ? 256u : 512u);
        const uint32_t base = fourBit ? csa * 16u : 0u;
        // Tiny mips need fewer color expansions than a complete palette.
        if (size_t(w) * h <= count) {
            decodeTexturePixels<Format, false>(mem, runtimeFormat, tbp, tbw, w, h, texa, nullptr, out, &clut, base);
            return;
        }
        uint32_t palette[count];
        for (uint32_t i = 0; i < count; ++i)
            palette[i] = clutColor(clut, base + i, texa);
        decodeTexturePixels<Format>(mem, runtimeFormat, tbp, tbw, w, h, texa, palette, out);
    }
}
} // namespace detail

// Decode a mip level. CLUT expansion is local to this call: palette/TEXA changes
// are observed every time. This does not cache memory or alter coherence/stamps.
inline void decodeTextureLevel(const LocalMemory& mem, uint32_t psm, uint32_t tbp, uint32_t tbw, uint32_t w, uint32_t h,
                               uint64_t texa, const ClutBuf& clut, uint32_t csa, std::vector<uint32_t>& out) {
    out.resize(size_t(w) * h);
    if (!w || !h)
        return;
    tbw = tbw > 1u ? tbw : 1u;
    // Avoid template-dispatch/palette setup overhead on the final 1x1 mip.
    if (w == 1 && h == 1) {
        const uint32_t v = mem.read(psm, tbp, tbw, 0, 0);
        switch (psm) {
            case PSM_CT32:
                out[0] = v;
                return;
            case PSM_CT24:
                out[0] = expand24(v, texa);
                return;
            case PSM_CT16:
            case PSM_CT16S:
                out[0] = expand16(v, texa);
                return;
            default: {
                const uint32_t base = (psm == PSM_T4 || psm == PSM_T4HL || psm == PSM_T4HH) ? csa * 16u : 0u;
                out[0] = clutColor(clut, base + v, texa);
                return;
            }
        }
    }
#define GSR_DECODE_FORMAT(format)                                                                                      \
    case format:                                                                                                       \
        detail::decodeTextureFormat<format>(mem, psm, tbp, tbw, w, h, texa, clut, csa, out);                           \
        return
    switch (psm) {
        GSR_DECODE_FORMAT(PSM_CT32);
        GSR_DECODE_FORMAT(PSM_CT24);
        GSR_DECODE_FORMAT(PSM_CT16);
        GSR_DECODE_FORMAT(PSM_CT16S);
        GSR_DECODE_FORMAT(PSM_T8);
        GSR_DECODE_FORMAT(PSM_T4);
        GSR_DECODE_FORMAT(PSM_T8H);
        GSR_DECODE_FORMAT(PSM_T4HL);
        GSR_DECODE_FORMAT(PSM_T4HH);
        GSR_DECODE_FORMAT(PSM_Z32);
        GSR_DECODE_FORMAT(PSM_Z24);
        GSR_DECODE_FORMAT(PSM_Z16);
        GSR_DECODE_FORMAT(PSM_Z16S);
        default:
            detail::decodeTextureFormat<0xffffffffu>(mem, psm, tbp, tbw, w, h, texa, clut, csa, out);
            return;
    }
#undef GSR_DECODE_FORMAT
}

} // namespace gsr
