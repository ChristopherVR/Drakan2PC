// Drakan .TXD texture blob (SRSC type 0x40) reader/editor. Layout: docs/reference/ui-prompts.md section 3,
// analysis/scripts/txd_decode.py.
//   +0 u32 w, +4 u32 h, +8 u32 stride, +0xc u32 bpp, ... 0x40-byte header, then
//   bpp 8 : {u32 kind(1), u32 count, 0, 0}, 256 x BGRA palette (kind 1), w*h indices
//   bpp 24: RGB, bpp 32: RGBA        (all rows stored bottom-up; alpha in PS2 units 0..0x80)
// TxdEdit works in "upright" coordinates (y = 0 is the top of the picture as shown on screen) and keeps untouched
// pixels bit-exact: the game's font loader finds glyph boundaries by comparing palette *indices* of marker pixels, so
// an edit must never renumber the palette. New colours are written into palette slots that no remaining pixel uses.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace drakan_ui {

struct Rgba {
    uint8_t r = 0, g = 0, b = 0, a = 0; // a: PS2 range 0..0x80
    bool operator==(const Rgba& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
    bool operator!=(const Rgba& o) const { return !(*this == o); }
};

class TxdEdit {
public:
    // Supports bpp 8 (palette kind 1), 24 and 32. Returns false for anything else or a size mismatch.
    bool load(const std::vector<uint8_t>& blob);
    int width() const { return w_; }
    int height() const { return h_; }
    int bpp() const { return bpp_; }
    Rgba get(int x, int y) const; // upright coordinates; reflects pending set() calls
    void set(int x, int y, Rgba c);
    // Applies the edits. Colours that do not fit into free palette slots are mapped to the nearest used entry (counted
    // in overflow()).
    std::vector<uint8_t> finish();
    int overflow() const { return overflow_; }
    int allocated() const { return allocated_; } // palette slots (re)written

private:
    std::vector<uint8_t> b_;
    int w_ = 0, h_ = 0, bpp_ = 0;
    size_t palOff_ = 0, pixOff_ = 0;
    std::vector<int64_t> pending_; // per stored pixel: -1 none, else packed RGBA
    int overflow_ = 0, allocated_ = 0;
    size_t sidx(int x, int y) const { return static_cast<size_t>(h_ - 1 - y) * w_ + x; }
    Rgba pal(int i) const;
};

// Minimal PNG writer (stored deflate blocks) for tools/tests. alpha8 = true scales PS2 alpha 0..0x80 to 0..255.
bool writePng(const std::string& path, int w, int h, const std::vector<Rgba>& upright, bool alpha8 = true);
// Whole texture to RGBA (upright), palette applied.
std::vector<Rgba> toRgba(const TxdEdit& t);

} // namespace drakan_ui
