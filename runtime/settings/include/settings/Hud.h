// Host HUD layers drawn over the game image by every presentation backend (the Windows D3D11 overlay and the portable
// raylib renderer): FPS counter, narration subtitles, the game-menu cursor and the bow crosshair. The layers are
// registered once by game-side code; a backend only supplies the output size and where the game image sits.
// ImGui API only: draw functions are valid inside HudLayer::draw (an ImGui frame is current).
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace dsettings {

// Output (back buffer / canvas) size and the game image inside it (letterbox / pillarbox / widescreen).
struct HudView {
    float w = 0, h = 0;
    float viewX = 0, viewY = 0, viewW = 0, viewH = 0; // 0 x 0 = the whole output
    float uiScale = 1;                                // settings_ui_scale-like factor (1 at 1080p)
    float imageX() const { return viewW > 0 ? viewX : 0; }
    float imageY() const { return viewH > 0 ? viewY : 0; }
    float imageW() const { return viewW > 0 ? viewW : w; }
    float imageH() const { return viewH > 0 ? viewH : h; }
};

struct HudLayer {
    std::function<bool()> active; // cheap; false = nothing to draw this frame
    std::function<void(const HudView&)> draw;
};

// Draw order, bottom to top. Stats and Subtitles are also drawn while a settings menu is open; Overlay layers
// (crosshair, cursor) are not.
enum class HudSlot { Stats, Subtitles, Crosshair, Cursor, Count };

class HudLayers {
public:
    void set(HudSlot slot, HudLayer layer);
    // Any layer wants to draw this frame (backends skip their ImGui frame when nothing is up).
    bool anyActive(bool menuOpen) const;
    void draw(const HudView& view, bool menuOpen) const;

private:
    HudLayer layers_[static_cast<int>(HudSlot::Count)];
};

// Process-wide set: registration happens on start-up before the first present.
HudLayers& hudLayers();

float hudScaleFor(float outputHeight);

// Small translucent text panel in a corner of the output (0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right).
void drawStatsPanel(const std::vector<std::string>& lines, int corner, float w, float h, float uiScale);
// Menu cursor (arrow) with its tip at (x, y).
void drawMenuCursor(float x, float y, float scale);
// Aiming crosshair centred on (x, y).
void drawCrosshair(float x, float y, float scale);
// Subtitle band: full-width translucent bar near the bottom, centred wrapped text.
void drawSubtitleBand(const std::string& utf8, float alpha, float w, float h, float sizeFactor);

} // namespace dsettings
