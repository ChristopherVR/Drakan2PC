// SettingsOverlay internals shared by the platform-free core (Overlay.cpp) and the window platforms
// (OverlayWin32.cpp, OverlayGlfw.cpp). Private to the settings library.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "imgui.h"
#include "settings/Overlay.h"

namespace dsettings {

// ImGui is not thread safe and the overlay can be driven from two threads (Win32: window messages arrive on the window
// thread, render() runs on the present thread). Every access to the overlay context (the backend's event queue,
// NewFrame, the frame itself) is serialised by one recursive mutex; the scope also selects the context
// (SetCurrentContext is process global). Without it a press / release pair arriving while a frame was being built could
// be dropped or reordered, which showed up as "a click needs several tries".
extern std::recursive_mutex g_overlayImguiMutex;
struct ImGuiCtxScope {
    std::lock_guard<std::recursive_mutex> lock;
    ImGuiContext* prev;
    explicit ImGuiCtxScope(ImGuiContext* c) : lock(g_overlayImguiMutex), prev(ImGui::GetCurrentContext()) {
        ImGui::SetCurrentContext(c);
    }
    ~ImGuiCtxScope() { ImGui::SetCurrentContext(prev); }
};

// The window half of the overlay: ImGui's platform backend, window hooks, the hotkey and gamepad combo.
class OverlayPlatform {
public:
    virtual ~OverlayPlatform() = default;
    // ImGui context current. Installs the ImGui platform backend and the window hooks.
    virtual bool init(SettingsOverlay& overlay, SettingsOverlay::Impl& impl) = 0;
    virtual void shutdown() = 0; // ImGui context current
    // Every presented frame, frame lock held, also while nothing is drawn: the gamepad combo.
    virtual void poll() = 0;
    // Frame lock held, before ImGui::NewFrame: queued input, the gamepad combo, the backend's NewFrame and the pointer
    // mapped into a w x h back buffer.
    virtual void newFrame(uint32_t w, uint32_t h) = 0;
    // Win32 window messages (see SettingsOverlay::handleMessage); other platforms hook their window themselves.
    virtual bool handleMessage(void*, unsigned, uintptr_t, intptr_t, intptr_t*) { return false; }
    virtual bool pointerInBackbuffer(uint32_t w, uint32_t h, float* x, float* y) const = 0;
    // Milliseconds on the clock the platform stamps input with (screen-layer input ownership changes).
    virtual uint32_t nowMs() const = 0;
};
std::unique_ptr<OverlayPlatform> createOverlayPlatform(); // the build's window platform

struct SettingsOverlay::Impl {
    OverlayInit init;
    ImGuiContext* ctx = nullptr;
    std::unique_ptr<OverlayPlatform> platform;
    std::unique_ptr<SettingsStore> store;
    std::unique_ptr<SettingsMenu> menu;
    UiFonts fonts;
    std::atomic<bool> open{false};
    std::function<void(bool)> openCb;
    ScreenLayer screenLayer;
    std::atomic<bool> screenInput{false}; // the layer takes window input (render thread writes)
    // platform->nowMs() when the layer took the input: key / button presses posted before it belong to whoever had the
    // input then (the key that opened the screen reaches the game through raw input on another thread first, and its
    // window message can be dispatched only after the layer took over).
    std::atomic<uint32_t> screenInputSince{0};
    ScreenFontSizes screenSizes; // sizes the screen fonts are baked at
    // Copy of the back buffer before the overlay draws (the screen layer's blur and item icon crops).
    ImTextureID frameTex = 0;
    uint32_t frameW = 0, frameH = 0;
    std::function<bool()> openGate;
    std::function<bool()> allowGate; // false = F1 / the gamepad combo are ignored (cut scenes, movies, loading)
    // the game's art (loaded when the menu first opens with the art available)
    std::string headingTtf, bodyTtf;
    bool artDone = false;
    uint64_t artRetryAtMs = 0;
    std::vector<ImTextureID> images; // the game art's textures
    bool backendsUp = false;
    // size of the last rendered back buffer (window -> back buffer mapping of input positions)
    std::atomic<uint32_t> lastW{0}, lastH{0};

    // Rebuilds the font atlas (menu fonts, the game font when the art is loaded, the screen fonts at `sizes`).
    void rebuildFonts(const ScreenFontSizes& sizes);
    ImTextureID uploadTexture(const GameImage& im);
    // Fetch the game's UI textures, rebuild the font atlas with the game's font and upload the textures (frame lock
    // held, before NewFrame).
    void tryLoadArt();
    // F1 / the gamepad combo: toggles the menu unless opening is gated (closing never is).
    void hotkey(SettingsOverlay& overlay);
};

} // namespace dsettings
