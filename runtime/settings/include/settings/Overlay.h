// SettingsOverlay: Win32 + Direct3D 11 glue around SettingsMenu (Dear ImGui backends imgui_impl_win32 /
// imgui_impl_dx11). This is the only part of the library that knows about the platform; a WebGPU/GLFW or Emscripten
// glue can replace it and reuse SettingsMenu.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "settings/Host.h"
#include "settings/Menu.h"
#include "settings/Store.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct HWND__;
typedef HWND__* HWND;

namespace dsettings {

struct OverlayInit {
    HWND hwnd = nullptr; // window that receives input (top-level game window); also used for DPI / cursor position
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ISettingsHost* host = nullptr; // must outlive the overlay
    Paths paths;                   // drakan.ini / drakan_input.ini
    std::string headingFontPath;   // optional serif TTF for the title, e.g. C:\Windows\Fonts\georgiab.ttf (empty =
                                   // auto-detect Georgia Bold)
    bool installSubclass =
        true; // subclass `hwnd` so keyboard/mouse messages reach the overlay without any WndProc code
    bool enableGamepadCombo = true; // Back + LB + RB (hold 0.5 s) toggles the menu
    int hotkeyVk = 0x70;            // VK_F1
};

class SettingsOverlay {
public:
    static std::unique_ptr<SettingsOverlay> create(const OverlayInit& init);
    ~SettingsOverlay();

    // ---- window messages ----
    // Call from the window procedure BEFORE the game sees the message when installSubclass == false. Returns true when
    // the overlay consumed the message (then return *result). While the menu is open all keyboard and mouse messages
    // are consumed.
    bool handleMessage(HWND hwnd, unsigned msg, uintptr_t wp, intptr_t lp, intptr_t* result);

    // ---- frame ----
    // Draw the overlay into `rtv` (size w x h) if it is open. Call once per presented frame, from the thread that owns
    // the D3D11 context (gsr: from the OverlayCallback). All D3D11 pipeline state touched is saved and restored.
    void render(ID3D11RenderTargetView* rtv, uint32_t w, uint32_t h);

    // ---- state ----
    void open();
    void close();
    void toggle();
    bool isOpen() const;
    // Optional: notified (on the UI thread) whenever the menu opens or closes, in addition to
    // ISettingsHost::onOverlayOpened/Closed.
    void setOpenChangedCallback(std::function<void(bool open)> cb);
    // Thread-safe flag for the game thread: true while the menu is open (use it to pause the EE and to feed neutral pad
    // data).
    bool isOpenAtomic() const;

    // ---- HUD layer: drawn on top of the game every presented frame, also while the menu is closed (narration
    // subtitles) ----
    struct HudLayer {
        std::function<bool()> active; // cheap; false = nothing to draw this frame (no ImGui frame is run)
        std::function<void(float w, float h, float uiScale)>
            draw; // w x h = back buffer size; uiScale = settings_ui_scale-like factor (1 at 1080p)
    };
    void setHudLayer(HudLayer layer);
    // Second layer, drawn after the HUD layer (the game-menu cursor). Same contract.
    void setCursorLayer(HudLayer layer);
    // Third layer, drawn before the others and also while the menu is open: the FPS / frame time counter. Same
    // contract.
    void setStatsLayer(HudLayer layer);
    // Draws a small translucent text panel (one entry of `lines` per row) in a corner of the back buffer
    // (0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right). Only valid inside a HudLayer::draw.
    static void drawStatsPanel(const std::vector<std::string>& lines, int corner, float w, float h, float uiScale);
    // Menu cursor (arrow) at back buffer position (x, y), `scale` = HudLayer uiScale. Only valid inside a
    // HudLayer::draw.
    static void drawMenuCursor(float x, float y, float scale);
    // Aiming crosshair centred on (x, y), `scale` = HudLayer uiScale. Only valid inside a HudLayer::draw.
    static void drawCrosshair(float x, float y, float scale);
    // Persistent native-menu labels, drawn after the game on every present.
    void drawNativeMenuText(const std::string& text, float x, float y, float height, bool highlighted);
    // Pointer position (OS cursor) mapped into a back buffer of w x h pixels; false when the cursor is outside the
    // window.
    bool pointerInBackbuffer(uint32_t w, uint32_t h, float* x, float* y) const;
    // Opening through the hotkey / gamepad combo (toggle) asks this first; false = "not now" (the host opens later with
    // open()). Used to put the game into its own pause menu before the overlay opens over it.
    void setOpenInterceptor(std::function<bool()> gate);
    // Opening through the hotkey / gamepad combo is only possible while `allow` returns true (false = the key press is
    // ignored). Closing is never gated. Used to keep F1 out of movies, cut scenes, the start-up logos and loading
    // screens.
    void setAllowGate(std::function<bool()> allow);
    // Draws a subtitle band: full-width translucent bar near the bottom, centred wrapped text. Only valid inside
    // HudLayer::draw.
    static void drawSubtitleBand(const std::string& utf8, float alpha, float w, float h, float sizeFactor);

    // ---- screen layer: a full-screen host UI over the running game (the modern Tab screen) ----
    // Drawn under the settings menu (background draw list) and above the game. While it wants input and the menu is
    // closed, keyboard and mouse messages go to it (through ImGui's IO; the gamepad through the Win32 backend's
    // XInput poll), F1 still opens the menu over it.
    struct ScreenFrame {
        float w = 0, h = 0;                // back buffer
        ImTextureID frame = 0;             // copy of the back buffer before the overlay (0 = unavailable)
        float frameW = 0, frameH = 0;      // its size
        ImDrawCallback additive = nullptr; // draw-list callbacks switching the blend (reset with
        ImDrawCallback opaque = nullptr;   // ImDrawCallback_ResetRenderState)
        const UiFonts* fonts = nullptr;    // includes the screen fonts baked for this output size
        const GameSkin* skin = nullptr;    // the game's art (when loaded)
        bool input = false;                // ImGui's input this frame belongs to the layer
    };
    struct ScreenLayer {
        std::function<bool()> active;     // draw this frame
        std::function<bool()> wantsInput; // take keyboard / mouse / pad (ignored while the menu is open)
        // Pixel sizes of the screen fonts for a back buffer of w x h (UiFonts::screen order); re-baked on change.
        std::function<ScreenFontSizes(float w, float h)> fontSizes;
        std::function<void(const ScreenFrame&)> draw;
        // Keep the last copy of the back buffer instead of taking a new one (the screen fades out over a still picture
        // of what it showed, e.g. while the game closes the page under it).
        std::function<bool()> freezeFrame;
    };
    void setScreenLayer(ScreenLayer layer);
    // The screen layer takes the keyboard / mouse right now (thread-safe).
    bool screenInputActive() const;

    SettingsMenu& menu();
    void* imguiContext() const; // ImGuiContext* of the overlay (tests inject input events through it)
    SettingsStore& store();

private:
    SettingsOverlay();
    struct Impl;
    std::unique_ptr<Impl> m_;
};

} // namespace dsettings
