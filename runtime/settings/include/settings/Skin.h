// GameSkin: the game's own UI art (INTERFCE.TXD), decoded at runtime from the user's disc by the host
// (ISettingsHost::gameTexture) and used to draw the settings menu in the game's look: brass frame, stone backdrop,
// brass plates, orb knobs and the game's menu font, with a PC flame highlight. Nothing is ever stored on disk. ImGui
// API only (backend agnostic): the glue provides `Upload` (RGBA -> ImTextureID).
#pragma once
#include <functional>
#include <vector>

#include "imgui.h"
#include "settings/Host.h"

namespace dsettings {

class GameSkin {
public:
    // INTERFCE.TXD texture ids (docs/reference/ui-prompts.md)
    enum Id : int {
        kFrame = 118,
        kStone = 88,
        kOrb = 128,
        kArrows = 197,
        kArrowsUD = 198,
        kCheck = 103,
        kFire = 28,
        kCursor = 61,
        kFont = 96,
        // Optional (the modern screens, runtime/gameui): drawn when present, never required.
        kIcons = 94,     // inventory icon atlas: the five section icons, trade, gold bag
        kGem = 129,      // pale blue-grey orb (the gem of the shared ornament)
        kParchment = 86, // cream cross-hatched parchment tile
    };
    struct Tex {
        ImTextureID id = 0;
        int w = 0, h = 0;
    };

    // Reads the images from the host. True when all the pieces the skin needs are available (frame, stone, font ...).
    // Cheap to call again until it succeeds (the textures only exist once the game has loaded INTERFCE.TXD).
    bool fetch(const ISettingsHost& host);
    bool fetched() const { return fetched_; }
    const GameImage* fontImage() const { return fetched_ ? &imgs_[kFontIdx] : nullptr; }

    using Upload = std::function<ImTextureID(const GameImage&)>;
    // Creates the textures (call on the thread that owns the device). Afterwards ready() is true.
    bool upload(const Upload& up);
    bool ready() const { return ready_; }
    void reset(); // forget everything (device lost / host changed)

    // The game's decorative menu font (built into the ImGui atlas by buildFonts); null -> text falls back to the TTF
    // heading font.
    ImFont* font = nullptr;
    float fontBaked = 64.0f; // size the glyphs were baked at (pixels)

    // ---- drawing helpers (valid when ready()) ----
    // Dark stone fill (tiled), tinted.
    void stone(ImDrawList* dl, ImVec2 a, ImVec2 b, float tilePx, ImU32 tint) const;
    // The brass frame (9-slice of the popup frame): outer rectangle a..b, `corner` = size of the corner studs in
    // pixels.
    void frame(ImDrawList* dl, ImVec2 a, ImVec2 b, float corner, ImU32 col = IM_COL32_WHITE) const;
    // The brass title / button plate (3-slice of the bar with the spear tip caps).
    void plate(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col = IM_COL32_WHITE) const;
    // The inner recessed slot of a plate (where its label goes), as a rectangle inside a..b.
    static void plateSlot(ImVec2 a, ImVec2 b, ImVec2& sa, ImVec2& sb);
    // Animated PC flame highlight, clipped to a..b; generated once as an RGBA sheet.
    void fire(ImDrawList* dl, ImVec2 a, ImVec2 b, float alpha, double time) const;
    // The gem / orb (slider knob, checkbox gem): centre, radius, tint.
    void orb(ImDrawList* dl, ImVec2 c, float r, ImU32 tint) const;
    // Check box outline (square), centre-less: a..b.
    void box(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col) const;
    // Triangle arrows of the game's selector widgets: dir 0 left, 1 right, 2 up, 3 down; centred at c with height h.
    void arrow(ImDrawList* dl, ImVec2 c, float h, int dir, ImU32 col) const;
    // The game's cursor (tip at p), `size` = pixel height.
    void cursor(ImDrawList* dl, ImVec2 p, float size) const;

    // ---- optional pieces (false / nothing drawn when the disc's texture was not available) ----
    // One picture of the inventory icon atlas (gameui::UiIconId order: chest, stars, face, map, book, trade, gold bag),
    // fitted into a..b keeping its aspect.
    bool icon(ImDrawList* dl, ImVec2 a, ImVec2 b, int which, ImU32 tint) const;
    // The gem: the orb texture as a disc at c, radius r.
    bool gem(ImDrawList* dl, ImVec2 c, float r, ImU32 tint) const;
    // The parchment tile repeated over a..b.
    bool parchment(ImDrawList* dl, ImVec2 a, ImVec2 b, float tilePx, ImU32 tint) const;

private:
    static constexpr int kN = 9;
    static constexpr int kFontIdx = 8;
    static constexpr int kOpt = 3; // optional textures: icons, gem, parchment
    GameImage imgs_[kN];
    Tex tex_[kN];
    GameImage optImgs_[kOpt];
    Tex optTex_[kOpt];
    bool fetched_ = false, ready_ = false;
    static int slot(int id);
};

// Bakes the game's font (alpha mask glyphs, 3x smoothed) into the atlas as codepoints 0x20..0x7e of `font` (a font that
// already exists in io.Fonts and keeps its own glyphs for everything else). Call before the atlas is built;
// finishGameGlyphs() after io.Fonts->Build().
struct GameFontJob {
    std::vector<int> rects;
    std::vector<int> codes;
    std::vector<std::vector<unsigned char>> alpha;
    std::vector<int> w, h;
};
bool prepareGameGlyphs(ImGuiIO& io, ImFont* font, const GameImage& img, float bakedPx, GameFontJob& job);
void finishGameGlyphs(ImGuiIO& io, const GameFontJob& job);

} // namespace dsettings
