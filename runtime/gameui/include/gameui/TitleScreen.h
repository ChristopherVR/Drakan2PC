// The modern title (main) menu over the game's title screen 8, which the glue keeps open with its page hidden: the
// title art (widget 0x2301) and the music stay native, or `TitleState::backdrop` names the art for the renderer. A
// centred menu column (Continue, Load Game, New Game, Options, Credits, PC Settings, Quit) in the modern screens'
// style, a one-line description, footer hints, and the Quit card. Options opens the pause menu's Options / Audio /
// Controller pages (a PauseScreen with rootPage = Options, centred, without the status strip), so the option pages
// and their actions are the same component as in the pause menu. Glue contract: runtime/gameui/README.md, "Title
// menu actions"; research: docs/journal/2026-10-25/modern-save-load-title.md section 2.
#pragma once
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "gameui/Draw.h"
#include "gameui/Input.h"
#include "gameui/Layout.h"
#include "gameui/PauseScreen.h"
#include "gameui/SaveSlots.h"
#include "gameui/Screen.h"
#include "gameui/SettingsPages.h"

namespace gameui {

enum class TitleRow : uint8_t {
    Continue,   // load the newest save (not a native row: the host's shortcut through the Load screen)
    LoadGame,   // 0x2302 -> screen 4
    NewGame,    // 0x2304 -> screen 0xe (the first level, then the intro movie)
    Options,    // 0x2305 -> screen 9: the host Options pages
    Credits,    // 0x2306 -> screen 0x13
    PcSettings, // the modern PC Settings pages (TitleConfig::pcSettingsPages + a schema), else the F1 overlay
    Quit,       // the host Quit card (natively 0x2303 and its Yes / No box)
    // Quit card
    QuitToDesktop,
    Cancel,
};
const char* titleRowLabel(TitleRow r);

enum class TitleActionKind : uint8_t {
    Continue,       // value = slot number of the save to load
    LoadGame,       // tap 0x2302: the Load screen (modern or native, per [general] save_menu)
    NewGame,        // tap 0x2304
    Credits,        // tap 0x2306
    OpenPcSettings, // drakan_settings_host::requestOpen() (only without the modern PC Settings pages)
    QuitToDesktop,  // drakan_win::requestClose()
};
const char* titleActionName(TitleActionKind k);

struct TitleAction {
    uint32_t id = 0; // assigned by TitleActionQueue::push
    TitleActionKind kind = TitleActionKind::LoadGame;
    int32_t value = 0;
};

class TitleActionQueue {
public:
    uint32_t push(TitleAction a);
    std::optional<TitleAction> pop();
    size_t size() const;
    void clear();

private:
    mutable std::mutex m_;
    std::deque<TitleAction> q_;
    uint32_t nextId_ = 1;
};

struct TitleState {
    bool canLoad = true;          // Load Game enabled (native 0x2302 enabled: 37e810(0) == 1, a save exists)
    int continueSlot = 0;         // the newest save's slot number; 0 = no Continue row
    std::string continueLocation; // its location ("Surdana")
    SaveTimestamp continueTime;   // and when it was saved
    bool canQuit = true;          // [general] title_quit
    PauseOptionsState options;    // the Audio / Controller values (readPauseOptions), for the Options pages
    ImageRef backdrop;            // the title art to draw behind the menu; None = transparent (the native art shows)
    DateOrder dateOrder = DateOrder::DayMonthYear;
    bool clock24 = true;
    std::string footer; // optional small text bottom-left (a version line)
    // TitleConfig::pcSettingsPages: the PC settings (dsettings::ModernSettings::schema()); null = PC Settings (and the
    // Options pages' PC Settings / Key Bindings) emit OpenPcSettings / OpenKeyBindings. `now` (seconds) and
    // `capturedKey` go to the SettingsPages as in PauseState.
    const SettingsSchema* pcSettings = nullptr;
    double now = 0;
    std::string capturedKey;
};

enum class TitlePage : uint8_t { Main, QuitConfirm, Options, PcSettings };

struct TitleConfig {
    ScreenConfig screen;
    bool showContinue = true;     // a Continue row when TitleState::continueSlot is set
    bool showPcSettings = true;   // a PC Settings row on the main page
    uint32_t suspendFrames = 240; // input ignored after an action that leaves the menu, until reactivate()
    int32_t sliderStep = 5;       // Options pages (PauseConfig)
    uint32_t optionHoldFrames = 120;
    // PC Settings (the main row, and Options > PC Settings / Controller > Key Bindings) open the modern SettingsPages
    // inside the title menu when TitleState::pcSettings is set.
    bool pcSettingsPages = false;
    // One Options screen (PauseConfig::unifiedOptions): Options opens the PC settings pages with the game's options
    // folded in, and there is no PC Settings row.
    bool unifiedOptions = false;
};

struct TitleFrameOutput {
    DrawList draw;
    UiSound sound = UiSound::None;
    std::vector<TitleAction> actions;       // to push into the TitleActionQueue
    std::vector<PauseAction> optionActions; // Options pages: to push into a PauseActionQueue (README mapping)
    std::vector<SettingsAction> settings;   // pcSettingsPages: edits to apply (dsettings::ModernSettings::apply)
};

class TitleScreen {
public:
    explicit TitleScreen(TitleConfig cfg = {});

    void open();
    void close();
    bool isOpen() const { return open_; }
    // A native child (Load, Credits, the F1 overlay) closed and the title is the active screen again.
    void reactivate();
    bool suspended() const { return suspendLeft_ > 0 || options_.suspended(); }

    TitleFrameOutput frame(const TitleState& st, const UiInput& in, const Viewport& vp);

    TitlePage page() const { return page_; }
    // Continue was chosen: the menu shows the loading card in place of its column until reactivate() / open().
    bool loading() const { return loading_; }
    // pcSettingsPages: the PC Settings pages are shown (from the main row or from Options), and those pages
    // (onApplied(), capturing()).
    bool pcSettingsOpen() const { return page_ == TitlePage::PcSettings || options_.pcSettingsOpen(); }
    SettingsPages& pcSettings() { return page_ == TitlePage::PcSettings ? pcPages_ : options_.pcSettings(); }
    // The Options pages (the embedded PauseScreen): its page(), pageStack(), targets() ...
    const PauseScreen& options() const { return options_; }
    TargetId focus() const { return focus_; }
    TargetId hover() const { return hover_; }
    const std::vector<HitTarget>& targets() const {
        return page_ == TitlePage::Options      ? options_.targets()
               : page_ == TitlePage::PcSettings ? pcPages_.targets()
                                                : targets_;
    }
    const std::vector<Hint>& hints() const {
        return page_ == TitlePage::Options      ? options_.hints()
               : page_ == TitlePage::PcSettings ? pcPages_.hints()
                                                : hints_;
    }
    std::vector<TitleRow> rows(const TitleState& st) const; // rows of the current page (Main / QuitConfirm)
    bool rowEnabled(TitleRow r, const TitleState& st) const;

private:
    TitleConfig cfg_;
    PauseScreen options_;
    SettingsPages pcPages_;
    bool open_ = false;
    TitlePage page_ = TitlePage::Main;
    TargetId focus_, hover_, mainFocus_;
    uint32_t suspendLeft_ = 0;
    bool loading_ = false;
    std::vector<HitTarget> targets_;
    std::vector<Hint> hints_;
    LayoutFrame lf_;

    std::vector<TitleRow> rowsOf(TitlePage page, const TitleState& st) const;
    const HitTarget* findTarget(const TargetId& id) const;
    Rect rowRect(int i, int n) const; // design units, main column
    void buildTargets(const TitleState& st);
    void ensureFocus(const TitleState& st);
    void buildHints(const TitleState& st);
    void activate(TitleRow r, const TitleState& st, TitleFrameOutput& out);
    void back(TitleFrameOutput& out);
    void applyInput(const TitleState& st, const UiInput& in, TitleFrameOutput& out);
    void draw(const TitleState& st, DrawList& d) const;
};

namespace design::title {
Rect menuPanel(int rows);
Rect row(int i, int rows);
Rect cardPanel();
Rect cardRow(int i);
} // namespace design::title

} // namespace gameui
