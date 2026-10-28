// Draw commands as data. The screen emits them in back-buffer pixels; a renderer (ImGui in the settings overlay,
// later WebGPU) turns them into quads. Images are opaque references the renderer resolves (skin art from
// INTERFCE.TXD, item icon crops from the hidden native page, the map background...). No ImGui / D3D here.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gameui {

struct Vec2 {
    float x = 0, y = 0;
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }
    bool empty() const { return w <= 0 || h <= 0; }
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    Rect inset(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    bool operator==(const Rect&) const = default;
};

// Colours are 0xRRGGBBAA, straight alpha.
using Color = uint32_t;
constexpr Color rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return (uint32_t(r) << 24) | (uint32_t(g) << 16) | (uint32_t(b) << 8) | a;
}
constexpr Color withAlpha(Color c, uint8_t a) {
    return (c & 0xffffff00u) | a;
}

// What an image reference points at; the renderer owns the mapping to textures.
enum class ImageKind : uint8_t {
    None,
    ItemIcon,      // index = grid cell (or 0x1b for the shop stock item): crop of the native page canvas
    CategoryIcon,  // index = CategoryFilter
    PageIcon,      // index = Page
    SpellIcon,     // index = spell index
    Prompt,        // index = Prompt: button glyph for the current device
    MapBackground, // index = region
    SkinFrame,     // brass 9-slice frame (INTERFCE.TXD 118)
    SkinStone,     // stone tile (88)
    SkinOrb,       // orb (128)
    SkinFire,      // fire highlight (28)
    SkinCrest,     // the shared ornament: the pale blue-grey gem (INTERFCE.TXD 129) in a gilt setting between two
                   // bone-ivory crescents (Order of the Flame's inventory frame); index unused
    UiIcon,        // index = UiIconId: the game's inventory icon atlas (INTERFCE.TXD 94)
    SavePreview,   // index = save slot number (1-based, BESCES-50006Slot<n>): the HD companion picture
                   // (drakan_save_previews) or the save's native 128 x 128 thumbnail; nothing for an empty slot
    TitleArt,      // index unused: the title menu's background art (title screen widget 0x2301)
};
// Pictures of the game's inventory icon atlas (INTERFCE.TXD 94, the native section tabs and the gold bag).
enum class UiIconId : uint8_t { Chest, Stars, Face, Map, Book, Trade, GoldBag, Count };
struct ImageRef {
    ImageKind kind = ImageKind::None;
    uint32_t index = 0;
    uint64_t handle() const { return (uint64_t(kind) << 32) | index; }
    static ImageRef fromHandle(uint64_t h) { return {ImageKind(uint8_t(h >> 32)), uint32_t(h)}; }
    bool operator==(const ImageRef&) const = default;
};

// Semantic panel styles: a skinned renderer draws stone + brass for them, a plain one fills with the colours.
enum class PanelStyle : uint8_t {
    Backdrop,  // full-screen dim / vignette over the world
    Main,      // large stone panel with brass frame
    Inset,     // recessed slot (grid cells, bars)
    Card,      // raised card (equipped slots, list rows)
    Parchment, // journal text
    Focus,     // ember focus ring
    Header,    // top / bottom bars
};

enum class FontRole : uint8_t { Body, Heading, Title, Numeric, Small };
enum class TextAlign : uint8_t { Left, Center, Right };

enum class DrawKind : uint8_t {
    Rect,     // filled, `color`, `rounding`
    Outline,  // `color`, `thickness`, `rounding`
    Gradient, // vertical, `color` top -> `color2` bottom
    Panel,    // `panel` style, `color` = tint (0 = style default)
    Image,    // `image`, `uv`, `color` = tint
    Text,     // `text` in `rect` (single line, clipped to rect.w), `font`, `fontSize`, `align`, vertically centred
    Line,     // from (rect.x, rect.y) to (rect.right(), rect.bottom()), `color`, `thickness`
    ClipPush, // intersect the clip rect with `rect`
    ClipPop,
    WorldView, // `rect` should show the live world (Rynn's preview): renderers draw nothing, the glue frames it
};

struct DrawCmd {
    DrawKind kind = DrawKind::Rect;
    Rect rect;
    Color color = 0;
    Color color2 = 0;
    float thickness = 1;
    float rounding = 0;
    PanelStyle panel = PanelStyle::Main;
    ImageRef image;
    ImageRef fallback; // Image: drawn instead (tinted `color2`) when the renderer cannot resolve `image`
    Rect uv{0, 0, 1, 1};
    FontRole font = FontRole::Body;
    float fontSize = 0;
    TextAlign align = TextAlign::Left;
    float tracking = 0; // Text: extra pixels between glyphs (letter-spaced capitals)
    std::string text;   // UTF-8
};

class DrawList {
public:
    void clear() { cmds_.clear(); }
    const std::vector<DrawCmd>& commands() const { return cmds_; }
    size_t size() const { return cmds_.size(); }

    void rect(const Rect& r, Color c, float rounding = 0);
    void outline(const Rect& r, Color c, float thickness, float rounding = 0);
    void gradient(const Rect& r, Color top, Color bottom);
    void panel(const Rect& r, PanelStyle s, Color tint = 0);
    void image(const Rect& r, ImageRef img, Color tint = 0xffffffffu, Rect uv = {0, 0, 1, 1});
    // `img`, or `fallback` (tinted `fallbackTint`) when the renderer has no picture for `img` this frame.
    void imageOr(const Rect& r, ImageRef img, ImageRef fallback, Color tint, Color fallbackTint);
    void text(const Rect& r, std::string s, FontRole f, float size, Color c, TextAlign a = TextAlign::Left,
              float tracking = 0);
    void line(Vec2 a, Vec2 b, Color c, float thickness);
    void pushClip(const Rect& r);
    void popClip();
    void worldView(const Rect& r);

private:
    std::vector<DrawCmd> cmds_;
};

} // namespace gameui
