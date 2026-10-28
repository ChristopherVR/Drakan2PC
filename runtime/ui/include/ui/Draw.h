// Tiny software canvas + 5x7 bitmap font + key cap / mouse icon renderer for the prompt glyphs. No anti-aliasing on
// purpose: the target textures are 8-bit palettised and the PS2 original icons are crisp pixel art; a handful of flat
// colours keeps palette use minimal.
#pragma once
#include "ui/Bindings.h"
#include "ui/Txd.h"
#include <string>
#include <vector>

namespace drakan_ui {

struct Canvas {
    int w = 0, h = 0;
    std::vector<Rgba> px;
    Canvas() = default;
    Canvas(int w_, int h_, Rgba fill = Rgba{}) : w(w_), h(h_), px((size_t)w_ * h_, fill) {}
    void set(int x, int y, Rgba c) {
        if (x >= 0 && y >= 0 && x < w && y < h)
            px[(size_t)y * w + x] = c;
    }
    Rgba get(int x, int y) const { return (x >= 0 && y >= 0 && x < w && y < h) ? px[(size_t)y * w + x] : Rgba{}; }
    void rect(int x, int y, int rw, int rh, Rgba c) {
        for (int j = 0; j < rh; ++j)
            for (int i = 0; i < rw; ++i)
                set(x + i, y + j, c);
    }
    void frame(int x, int y, int rw, int rh, Rgba c) {
        rect(x, y, rw, 1, c);
        rect(x, y + rh - 1, rw, 1, c);
        rect(x, y, 1, rh, c);
        rect(x + rw - 1, y, 1, rh, c);
    }
};

struct KeyStyle {
    Rgba bg{}; // colour used where the cap leaves the cell transparent (alpha 0)
    Rgba fill{}, border{}, shade{}, text{}, accent{};
};
// Default flat style; `alpha` = opaque alpha of the target texture (PS2 units: 0x7f / 0x78 / 0x80).
KeyStyle defaultStyle(Rgba background, uint8_t alpha);

constexpr int kGlyphW = 5, kGlyphH = 7;
int textWidth(const std::string& s, int scale = 1, int gap = 1); // pixels (5 px glyphs + `gap` px between them)
void drawText(Canvas& c, int x, int y, const std::string& s, int scale, Rgba col, int gap = 1);

// A key cap of exactly w x h pixels at (x,y) showing `label` (text cap, arrow key, mouse icon or wheel icon).
void drawLabelCap(Canvas& c, int x, int y, int w, int h, const Label& label, const KeyStyle& st);
// Mouse silhouette (w x h) with the given button highlighted: 1 left, 2 right, 3 middle (wheel), 0 none, 4/5 side
// buttons.
void drawMouse(Canvas& c, int x, int y, int w, int h, int button, int wheelDir, const KeyStyle& st);
// Plain triangle arrow (0 up, 1 down, 2 left, 3 right) centred in the box.
void drawArrow(Canvas& c, int x, int y, int w, int h, int dir, Rgba col);
// Preferred width of a text cap for diagrams (padding included, >= minW).
int capWidth(const Label& l, int minW = 14);

} // namespace drakan_ui
