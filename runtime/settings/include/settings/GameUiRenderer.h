// GameUiRenderer: draws a gameui::DrawList (the modern Tab screen, runtime/gameui) with Dear ImGui in the game's
// look: dark stone panels in the game's brass frame, red-brown cells, the gem-and-crescent crest, the native section
// icons and parchment from INTERFCE.TXD (GameSkin), the game's flame on focused rows, and screen fonts baked at the
// output size (UiFonts::screen) so text is crisp at 4K. ImGui API only (backend agnostic): the overlay glue
// supplies its copy of the game frame and two blend callbacks as opaque ImGui handles. The frame copy gives the
// backdrop its blur (sharp inside the WorldView holes, where Rynn stands) and the item icons (crops of the native 3D
// icons the game drew under the host grid).
#pragma once
#include <functional>

#include "imgui.h"

#include "gameui/Draw.h"
#include "gameui/Layout.h"
#include "settings/Menu.h"
#include "settings/Skin.h"

namespace dsettings {

struct GameUiCanvas {
    ImTextureID frame = 0; // the back buffer before the overlay drew (0 = none: plain dim, no icons)
    float frameW = 0, frameH = 0;
    ImDrawCallback additive = nullptr; // switches the blend to additive (dst + src); reset with
                                       // ImDrawCallback_ResetRenderState
    ImDrawCallback opaque = nullptr;   // switches the blend to a plain copy (src)
};

// A rectangle of the frame copy for an ItemIcon / SpellIcon (black background: drawn additively) or a MapBackground
// (`opaque`: copied): UVs and its size in pixels (for aspect-correct fitting).
struct IconCrop {
    ImVec2 uv0, uv1;
    float w = 0, h = 0;
    bool opaque = false;
    bool stretch = false;    // fill the rectangle instead of fitting the crop's aspect
    ImTextureID texture = 0; // the crop's texture when it is not the frame copy (the renderer's grid capture)
};
using IconSource = std::function<bool(const gameui::ImageRef& ref, IconCrop& out)>;

class GameUiRenderer {
public:
    void setFonts(const UiFonts& f) { fonts_ = f; }
    const UiFonts& fonts() const { return fonts_; }
    void setSkin(const GameSkin* s) { skin_ = s; }
    // Button glyphs: gamepad (Xbox layout) or keyboard caps.
    void setGamepadPrompts(bool gamepad) { gamepad_ = gamepad; }
    // Text width with the screen fonts (for gameui::ScreenConfig::measure). Valid while the fonts are.
    gameui::TextMeasure measure() const;
    ImFont* font(gameui::FontRole role) const;

    // Draws the list into `dl`. `fade` 0..1 multiplies every alpha (open / close); `time` animates the flame.
    void draw(ImDrawList* dl, const gameui::DrawList& list, const GameUiCanvas& canvas, const IconSource& icons,
              double time, float fade) const;

    // The prompt glyph alone (also used by the host for its own hints).
    void drawPrompt(ImDrawList* dl, const gameui::Rect& r, uint32_t prompt, ImU32 tint, float fade) const;

private:
    void backdrop(ImDrawList* dl, const gameui::DrawCmd& c, const gameui::DrawList& list, const GameUiCanvas& canvas,
                  float fade) const;
    void panel(ImDrawList* dl, const gameui::DrawCmd& c, double time, float fade) const;
    // False when the image could not be drawn (no crop this frame, unknown kind): the command's fallback is drawn.
    bool image(ImDrawList* dl, const gameui::DrawCmd& c, const GameUiCanvas& canvas, const IconSource& icons,
               float fade) const;
    void text(ImDrawList* dl, const gameui::DrawCmd& c, float fade) const;
    void glyph(ImDrawList* dl, const gameui::Rect& r, uint32_t category, ImU32 col) const;
    void spellGlyph(ImDrawList* dl, const gameui::Rect& r, ImU32 col) const;
    void skillsGlyph(ImDrawList* dl, const gameui::Rect& r, ImU32 col) const;
    void trackedText(ImDrawList* dl, const gameui::DrawCmd& c, ImFont* f, float size, float fade) const;
    void crest(ImDrawList* dl, const gameui::Rect& r, float fade) const;

    UiFonts fonts_;
    const GameSkin* skin_ = nullptr;
    bool gamepad_ = false;
};

} // namespace dsettings
