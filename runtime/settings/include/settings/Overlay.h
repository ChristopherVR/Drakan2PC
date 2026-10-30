// SettingsOverlay: SettingsMenu, the host screen layer and the HUD layers over the game, on a window platform (Win32
// with imgui_impl_win32, or GLFW with imgui_impl_glfw) and an OverlayRenderer (Direct3D 11 or WebGPU,
// settings/OverlayRenderer.h).
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "settings/Host.h"
#include "settings/Hud.h"
#include "settings/Menu.h"
#include "settings/OverlayRenderer.h"
#include "settings/Store.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct HWND__;
typedef HWND__* HWND;

namespace dsettings {

// Window platforms without native events (the browser): the host hands the overlay an input stream instead.
struct OverlayEvent {
    enum Type : uint8_t { Key, Button, Cursor, Wheel, Char, Focus } type = Key;
    int a = 0, b = 0; // Key: GLFW key code, down. Button: 0 left / 1 right / 2 middle, down. Cursor: x, y (surface
                      // pixels). Wheel: notches. Char: code point. Focus: focused.
};
struct OverlayEventSource {
    std::function<bool(OverlayEvent&)> next;                     // the next queued event (drained every frame)
    std::function<bool(int& x, int& y, int& w, int& h)> pointer; // cursor and the size of the surface it is over
    std::function<uint32_t()> gamepadButtons;                    // Gamepad API standard-mapping bits, 0 = none
    std::function<void(bool hidden)> setCursorHidden;            // the system cursor over the surface
};

struct OverlayInit {
    // Window that receives input (the top-level game window): an HWND on Windows, a GLFWwindow* elsewhere.
    void* hwnd = nullptr;
    // The GPU side: `renderer` when set, otherwise Direct3D 11 on device / context.
    std::shared_ptr<OverlayRenderer> renderer;
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
    OverlayEventSource events;      // event-fed platforms only (OverlayEvents.cpp, the browser)
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
    // Draw the overlay into `target` (the renderer's target type, e.g. an ID3D11RenderTargetView*; size w x h) if it
    // is open. Call once per presented frame, from the thread that owns the GPU context (gsr: from the
    // OverlayCallback). `image` = where the game image sits in the back buffer (its view rectangle; size and scale
    // are filled in).
    void render(void* target, uint32_t w, uint32_t h, const HudView& image = {});

    // ---- state ----
    void open();
    void close();
    void toggle();
    void hotkey(); // Same allow gate and pause interceptor as a physical F1 / controller combo.
    bool isOpen() const;
    // Optional: notified (on the UI thread) whenever the menu opens or closes, in addition to
    // ISettingsHost::onOverlayOpened/Closed.
    void setOpenChangedCallback(std::function<void(bool open)> cb);
    // Thread-safe flag for the game thread: true while the menu is open (use it to pause the EE and to feed neutral pad
    // data).
    bool isOpenAtomic() const;

    // ---- HUD: the shared layers of settings/Hud.h (hudLayers()) are drawn on top of the game every presented frame,
    // also while the menu is closed ----
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
    OverlayRenderer& renderer(); // textures for the host's own screen art, readback
    void* imguiContext() const;  // ImGuiContext* of the overlay (tests inject input events through it)
    SettingsStore& store();

    struct Impl; // the window platforms (src/OverlayImpl.h) work on it

private:
    SettingsOverlay();
    std::unique_ptr<Impl> m_;
};

} // namespace dsettings
