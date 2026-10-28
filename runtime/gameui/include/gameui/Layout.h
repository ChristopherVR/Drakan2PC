// Resolution / aspect independent layout: everything is designed on a 1920 x 1080 canvas and mapped into a centred
// 16:9 safe area of the viewport (pillarboxed on 21:9 / 32:9, letterboxed on 4:3). Bars and the backdrop span the
// whole viewport. Plus the theme (dark stone, brass, ember highlight, parchment) and text measuring / wrapping.
#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "gameui/Draw.h"

namespace gameui {

inline constexpr float kDesignW = 1920.0f;
inline constexpr float kDesignH = 1080.0f;

// Output area in back-buffer pixels (OverlayContext::viewX/Y/W/H).
struct Viewport {
    float x = 0, y = 0, w = 1920, h = 1080;
};

struct LayoutFrame {
    Viewport view;
    float scale = 1;      // design unit -> pixel
    float ox = 0, oy = 0; // pixel origin of the design canvas (safe area top-left)
    Rect safe;            // safe area in pixels
    // Design rect -> pixels.
    Rect map(const Rect& d) const { return {ox + d.x * scale, oy + d.y * scale, d.w * scale, d.h * scale}; }
    float px(float design) const { return design * scale; }
    // Full-width band at design y / height (spans the viewport, not just the safe area).
    Rect band(float y, float h) const { return {view.x, oy + y * scale, view.w, h * scale}; }
};
LayoutFrame computeFrame(const Viewport& v);

// Text width in pixels for a UTF-8 string at `sizePx`. The renderer supplies the real font metrics; the default
// estimate (0.5 em per glyph) keeps the library usable headless.
using TextMeasure = std::function<float(std::string_view text, FontRole font, float sizePx)>;
float estimateTextWidth(std::string_view text, FontRole font, float sizePx);
// Greedy word wrap to `width` pixels; explicit '\n' breaks are kept. Never returns an empty vector for non-empty text.
std::vector<std::string> wrapText(std::string_view text, float width, FontRole font, float sizePx,
                                  const TextMeasure& measure);
// Cuts `text` to fit `width`, ending with "..." when cut.
std::string ellipsize(std::string_view text, float width, FontRole font, float sizePx, const TextMeasure& measure);

// The palette follows Drakan: Order of the Flame's inventory (docs/journal/2026-10-25/ootf-inventory-map-reference.md):
// gilded bronze edges, dark red-brown cells on bronze grid lines, a thin red selection trace, bone-ivory and a pale
// blue-grey gem for ornaments; text in parchment ivory with gilt headings and bronze letter-spaced labels.
struct Theme {
    Color backdrop = rgba(9, 6, 4, 205);
    Color backdropEdge = rgba(0, 0, 0, 235); // vignette edge
    Color panel = rgba(21, 14, 10, 232);
    Color panelRaised = rgba(38, 25, 17, 238);
    Color inset = rgba(31, 11, 5, 240); // cell background, OotF #1b0800..#220500
    Color brass = rgba(201, 155, 82);   // labels, gilt text
    Color brassDim = rgba(118, 84, 42);
    Color text = rgba(229, 217, 191); // parchment ivory
    Color textMuted = rgba(163, 143, 114);
    Color textDisabled = rgba(99, 86, 70);
    Color heading = rgba(239, 211, 152); // gilt ivory
    Color ember = rgba(224, 123, 46);
    Color emberGlow = rgba(214, 48, 26, 58); // soft red glow under the selection
    Color select = rgba(214, 34, 22);        // the thin red selection trace (OotF #d40000)
    Color gridLine = rgba(100, 60, 12);      // bronze lattice between cells (#643c0c)
    Color gridLight = rgba(156, 103, 21);    // its lit edge (#9c6715)
    Color gilt = rgba(226, 182, 92);         // gilded edges and rules
    Color ivory = rgba(223, 214, 188);       // bone-ivory accents
    Color good = rgba(143, 203, 107);
    Color bad = rgba(216, 88, 74);
    Color hp = rgba(184, 50, 46);
    Color mana = rgba(74, 127, 216);
    Color xp = rgba(201, 164, 76);
    Color parchment = rgba(217, 201, 163, 245);
    Color ink = rgba(58, 42, 26);
    // design-unit font sizes
    float sizeTitle = 44, sizeHeading = 32, sizeBody = 24, sizeSmall = 19, sizeNumeric = 26;
};

} // namespace gameui
