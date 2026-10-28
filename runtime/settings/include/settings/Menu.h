// SettingsMenu: the Dear ImGui user interface of the settings overlay. Backend agnostic: it only calls the ImGui API,
// so the same code runs on the D3D11/Win32 glue (Overlay.h) today and on a WebGPU/GLFW/Emscripten backend later.
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "gameui/SettingsSchema.h"
#include "settings/Host.h"
#include "settings/Schema.h"
#include "settings/Skin.h"
#include "settings/Store.h"
#include "settings/Types.h"

struct ImFont;
struct ImGuiStyle;
struct ImGuiIO;

namespace dsettings {

enum class Page { Display, Audio, Controls, General };

// The drakan_input.ini source name of a keyboard key (an ImGuiKey value: "A", "LCONTROL", "F5" ...); null for keys
// the bindings cannot name. The overlay's capture uses it, the modern PC Settings pages' glue too.
const char* iniNameOfImGuiKey(int imguiKey);

// ---- style / fonts (ImGui only) ----
// Dark parchment + gold palette. `scale` multiplies all paddings (font size is scaled through
// ImGuiIO::FontGlobalScale).
void applyDrakanStyle(float scale);
struct UiFonts {
    ImFont* body = nullptr;
    ImFont* heading = nullptr;
    ImFont* game = nullptr;
    float bakedPx = 28.0f;
    float gameBakedPx = 64.0f;
    // Screen fonts of the full-screen host UI (the modern Tab screen), baked at the exact pixel sizes it draws at so
    // text stays crisp at any output size (0 / null = not built). Order: body, heading, title, numeric, small.
    static constexpr int kScreenFonts = 5;
    ImFont* screen[kScreenFonts] = {};
    float screenPx[kScreenFonts] = {};
};
// Pixel sizes for UiFonts::screen (same order); all zero = no screen fonts.
struct ScreenFontSizes {
    float px[UiFonts::kScreenFonts] = {};
    bool any() const {
        for (float v : px)
            if (v > 0)
                return true;
        return false;
    }
};
// Bakes the fonts into io.Fonts (call before the backend creates its device objects / builds the atlas).
// headingTtfPath: optional serif font file (e.g. C:\Windows\Fonts\georgiab.ttf); empty or missing -> the body font is
// used. bodyTtfPath: optional serif font for the small text (e.g. palatino); empty or missing -> the embedded Roboto
// Medium. gameFont: the game's own menu font (INTERFCE.TXD 96) -> UiFonts::game (headings, tabs, buttons); null -> not
// built.
UiFonts buildFonts(ImGuiIO& io, const std::string& headingTtfPath = {}, const std::string& bodyTtfPath = {},
                   const GameImage* gameFont = nullptr, const ScreenFontSizes& screen = {});

class SettingsMenu {
public:
    SettingsMenu(ISettingsHost& host, SettingsStore& store);

    void open();  // reloads the store, shows the menu, calls host.onOverlayOpened()
    void close(); // discards unsaved changes, calls host.onOverlayClosed()
    bool isOpen() const { return open_; }
    void setFonts(const UiFonts& f) {
        fonts_ = f;
        skin_.font = f.game;
        skin_.fontBaked = f.gameBakedPx;
    }
    // The game's own art (frame, stone, plates, font). The overlay glue fetches / uploads it when the menu opens;
    // without it the plain style is used.
    GameSkin& skin() { return skin_; }
    bool skinned() const { return skin_.ready() && skin_.font != nullptr; }
    // Called whenever the menu opens or closes (before/after the host callbacks), from the UI thread.
    void setStateCallback(std::function<void(bool open)> cb) { stateCb_ = std::move(cb); }
    void setPage(Page p) { page_ = p; }
    Page page() const { return page_; }

    // Draw the menu. Call between ImGui::NewFrame() and ImGui::Render(). Returns true while the menu is open.
    bool frame();

    // Current effective UI scale (1.0 = designed for 1080p).
    float scale() const { return scale_; }

    // ---- test / screenshot hooks ----
    enum class DemoState { None, Capture, CaptureConflict, RestartPrompt, RevertPrompt, ErrorPrompt };
    void demoState(DemoState s);
    void applyPending(bool closeAfter) { apply(closeAfter); } // what the Apply / OK buttons do (tests)
    void demoScrollTo(float y) { demoScroll_ = y; }
    void demoControlsTab(int t) { forceTab_ = t; }
    const Settings& pending() const { return p_; }
    Settings& pendingMut() { return p_; }

private:
    ISettingsHost& host_;
    SettingsStore& store_;
    UiFonts fonts_;
    GameSkin skin_;
    unsigned lastNav_ = 0, lastHover_ = 0;
    HostCaps caps_;
    double capsRefreshAt_ = 0;
    MonitorInfo mon_;
    std::vector<std::string> audioDevices_;
    std::function<void(bool)> stateCb_;
    bool open_ = false;
    Page page_ = Page::Display;
    int controlsTab_ = 0;
    int forceTab_ = -1;
    float demoScroll_ = -1.0f;
    float scale_ = 1.0f, lastStyleScale_ = -1.0f;
    Settings p_, a_; // pending (edited) and applied (active in the game)
    bool linkSens_ = true;
    int resPreset_ = -1;
    bool wantPageFocus_ = false;
    bool navFlagsSaved_ = false;
    int savedNavFlags_ = 0;

    // key capture
    struct Capture {
        bool active = false;
        bool game = true;
        std::string action;
        int slot = 0;
        double startTime = 0;
        bool mouseArmed = false;
        bool conflictAsk = false;
        std::string pendingSource;
    };
    Capture cap_;

    // popups
    bool openRestart_ = false, openRevert_ = false, openError_ = false, openResetAll_ = false, openQuit_ = false;
    std::vector<std::string> restartReasons_;
    std::string errorText_;
    Settings revertTo_;
    double revertDeadline_ = 0;

    void drawHeader(float w);
    void drawSidebar(float h);
    void drawFooter();
    // The Display / Audio / Controls (mouse and gamepad) / Gameplay rows come from buildSettingsSchema (Schema.h),
    // the same rows the modern pages show.
    SchemaEnv schemaEnv() const;
    void drawSchemaPage(const char* pageId, bool controllerView = false);
    void drawSchemaRow(const gameui::SettingRow& r, bool controllerView);
    void drawDisplay();
    void drawAudio();
    void drawControls();
    void drawGeneral();
    void drawBindingTable(bool gameLayer);
    void drawMouseGamepad();
    void drawControllerView();
    void drawPopups();
    void updateCapture();
    void commitCapture(const std::string& source, bool replaceOthers);
    void apply(bool closeAfter);
    bool pageDirty(Page p) const;
    void resetPage();
    void resetBindings(bool gameLayer);
    void refreshEnvironment();
    float S(float v) const { return v * scale_; }
};

} // namespace dsettings
