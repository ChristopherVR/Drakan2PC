// Drawing helpers shared by the strip, map and pause screens (internal). They draw in the modern Tab screen's visual
// language: the same panels, focus treatment, bars, text sizes and footer hints as InventoryScreen. Rectangles are in
// the screen's design units and mapped through its LayoutFrame (whose origin each screen anchors differently).
#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "gameui/Draw.h"
#include "gameui/Layout.h"
#include "gameui/Screen.h"

namespace gameui::detail {

float themeSize(const Theme& th, FontRole f);

// A LayoutFrame whose design canvas is 1080 units high, scaled by the viewport height (and narrowed so a canvas
// `minDesignW` wide still fits), with design x = `anchorX` placed at the viewport's left / centre / right and design
// y = 1080 at the viewport's bottom (`bottomAnchored`) or the canvas centred vertically.
enum class AnchorX : uint8_t { Left, Centre, Right };
LayoutFrame anchoredFrame(const Viewport& vp, float minDesignW, AnchorX ax, bool bottomAnchored);

class Paint {
public:
    Paint(const LayoutFrame& lf, const ScreenConfig& cfg) : lf_(lf), cfg_(cfg) {}

    const LayoutFrame& lf() const { return lf_; }
    const Theme& th() const { return cfg_.theme; }
    Rect map(const Rect& design) const { return lf_.map(design); }
    float px(float design) const { return lf_.px(design); }
    // Pixel size of a font role (`size` design units, 0 = the theme's size for the role).
    float fontPx(FontRole f, float size = 0) const { return lf_.px(size > 0 ? size : themeSize(cfg_.theme, f)); }
    float measure(std::string_view t, FontRole f, float sizePx) const;
    // Width of `t` in design units.
    float textWidth(std::string_view t, FontRole f, float size = 0) const;
    std::vector<std::string> wrap(std::string_view t, float designW, FontRole f, float size = 0) const;

    // Single-line text in a design rect, ellipsized to fit.
    void text(DrawList& d, const Rect& design, const std::string& s, FontRole f, Color c, TextAlign a = TextAlign::Left,
              float size = 0) const;
    // A line struck through text drawn with the same arguments (completed objectives).
    void strike(DrawList& d, const Rect& design, const std::string& s, FontRole f, Color c,
                TextAlign a = TextAlign::Left, float size = 0) const;
    // A section label: letter-spaced capitals in gilt bronze (design rect; `size` design units, 0 = 16).
    void label(DrawList& d, const Rect& design, std::string_view s, Color c, TextAlign a = TextAlign::Left,
               float size = 0) const;
    // Focus of a row or card: soft red glow, the warm wash with the game's flame, the thin red trace (pixel rect).
    void focus(DrawList& d, const Rect& px) const;
    // Focus of a grid cell / item: the red trace alone over a red-brown wash (Order of the Flame), no flame.
    void selectCell(DrawList& d, const Rect& px) const;
    // The bronze lattice behind a grid of cells and its gilt outer edge (pixel rect of the cells' union, `gap` the
    // gap between cells in pixels: the lattice shows through it).
    void lattice(DrawList& d, const Rect& px, float gap) const;
    // The shared ornament (gem between ivory crescents), centred at a pixel point, `h` design units high.
    void crest(DrawList& d, float cx, float cy, float h) const;
    // A gilded rule from x0 to x1 at y (pixels), with the crest at its centre when `withCrest`.
    void ornateRule(DrawList& d, float x0, float x1, float y, bool withCrest) const;
    // Value bar with a label left and "cur / max" right (InventoryScreen::drawBar).
    void bar(DrawList& d, const Rect& design, int32_t cur, int32_t max, Color fill, const std::string& label,
             float labelSize = 0) const;
    // A thin brass rule across a design rect's top edge.
    void rule(DrawList& d, float x, float y, float w) const;
    // A menu button as the pause rows draw it: card panel, the focus look, a Heading label (left, or centred), and
    // "Unavailable" at the right when disabled (design rect).
    void button(DrawList& d, const Rect& design, const std::string& label, bool focused, bool hovered, bool enabled,
                TextAlign align = TextAlign::Left) const;
    // A confirm card's frame over a dimmed screen: the panel (design rect), its title and up to two body lines, as
    // the pause menu's Quit / Default Settings cards. The buttons are drawn by the caller (design::pause::cardRow).
    void confirmCard(DrawList& d, const Rect& design, const std::string& title, const std::string& body) const;

    // Footer hints: a prompt glyph and its label per hint. Layout in design units, `y` / `h` the row, right aligned
    // at `right` or centred on `centreX` (when `centred`).
    std::vector<Rect> layoutHints(const std::vector<Hint>& hints, float anchorX, bool centred, float y, float h,
                                  float glyph, float gap, FontRole f, float size = 0) const;
    void drawHint(DrawList& d, const Hint& h, const Rect& px, bool hovered, float glyph, FontRole f,
                  float size = 0) const;

private:
    LayoutFrame lf_;
    const ScreenConfig& cfg_;
};

// The focus / selection look without a Paint (InventoryScreen uses its own LayoutFrame): see Paint::focus,
// Paint::selectCell, Paint::lattice, Paint::ornateRule.
void drawFocus(DrawList& d, const Theme& th, const LayoutFrame& lf, const Rect& px);
void drawCellSelect(DrawList& d, const Theme& th, const LayoutFrame& lf, const Rect& px);
void drawLattice(DrawList& d, const Theme& th, const LayoutFrame& lf, const Rect& px, float gap);
void drawOrnateRule(DrawList& d, const Theme& th, const LayoutFrame& lf, float x0, float x1, float y, bool withCrest);
void drawLabel(DrawList& d, const Theme& th, const LayoutFrame& lf, const Rect& design, std::string_view s, Color c,
               TextAlign a, float size);

const char* quantityLabelFor(QuantityKind k); // "Durability", "Arrows", "Charges"
std::string fmtInt(int32_t v);
std::string fmtPair(int32_t a, int32_t b);

} // namespace gameui::detail
