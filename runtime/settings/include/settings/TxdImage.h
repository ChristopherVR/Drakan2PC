// Decoder for the game's .TXD texture blobs (SRSC type 0x40, see docs/reference/ui-prompts.md section 2) into a
// GameImage. Header only, std only.
//   +0 u32 w, +4 u32 h, +8 u32 stride, +0xc u32 bpp (8/16/24/32), 0x40 byte header, rows stored bottom-up
//   bpp 8 : {u32 kind, u32 count, 0, 0}, 256 x B,G,R,A palette (kind 1; alpha 0..0x80), then w*h indices
//   bpp 16: ARGB1555 (R in the low bits)   bpp 24: R,G,B   bpp 32: R,G,B,A (alpha 0..0x80)
// Used by the host (ISettingsHost::gameTexture) and the demo; the library itself never reads files.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>

#include "settings/Host.h"

namespace dsettings {

inline bool decodeTxdBlob(const uint8_t* b, size_t n, GameImage& out) {
    if (n < 0x40)
        return false;
    uint32_t w, h, bpp;
    std::memcpy(&w, b, 4);
    std::memcpy(&h, b + 4, 4);
    std::memcpy(&bpp, b + 12, 4);
    if (!w || !h || w > 4096 || h > 4096)
        return false;
    const size_t px = static_cast<size_t>(w) * h;
    auto alpha = [](unsigned a) { return std::min(255u, a * 2u); }; // PS2 alpha 0..0x80
    out.w = static_cast<int>(w);
    out.h = static_cast<int>(h);
    out.rgba.assign(px, 0);
    auto put = [&](uint32_t x, uint32_t sy, uint32_t col) {
        out.rgba[static_cast<size_t>(h - 1 - sy) * w + x] = col;
    }; // flip: stored rows are bottom-up
    if (bpp == 8) {
        if (n < 0x40 + 16 + 1024 + px)
            return false;
        uint32_t kind;
        std::memcpy(&kind, b + 0x40, 4);
        if (kind != 1)
            return false;
        const uint8_t* pal = b + 0x50;
        const uint8_t* idx = b + 0x50 + 1024;
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                const uint8_t* e = pal + 4 * static_cast<size_t>(idx[static_cast<size_t>(y) * w + x]); // B,G,R,A
                put(x, y, e[2] | (e[1] << 8) | (e[0] << 16) | (alpha(e[3]) << 24));
            }
    } else if (bpp == 16) {
        if (n < 0x40 + px * 2)
            return false;
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                uint16_t v;
                std::memcpy(&v, b + 0x40 + (static_cast<size_t>(y) * w + x) * 2, 2);
                const unsigned r = (v & 31) * 255 / 31, g = ((v >> 5) & 31) * 255 / 31,
                               bl = ((v >> 10) & 31) * 255 / 31;
                put(x, y, r | (g << 8) | (bl << 16) | (255u << 24)); // the game treats these as opaque
            }
    } else if (bpp == 24) {
        if (n < 0x40 + px * 3)
            return false;
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                const uint8_t* e = b + 0x40 + (static_cast<size_t>(y) * w + x) * 3;
                put(x, y, e[0] | (e[1] << 8) | (e[2] << 16) | (255u << 24));
            }
    } else if (bpp == 32) {
        if (n < 0x40 + px * 4)
            return false;
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                const uint8_t* e = b + 0x40 + (static_cast<size_t>(y) * w + x) * 4;
                put(x, y, e[0] | (e[1] << 8) | (e[2] << 16) | (alpha(e[3]) << 24));
            }
    } else {
        return false;
    }
    return true;
}

} // namespace dsettings
