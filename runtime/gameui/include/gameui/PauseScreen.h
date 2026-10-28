// The modern Escape (pause) menu of docs/journal/2026-10-25/modern-pause-menu.md.
// Phase 1: the root page (Resume, Save Game, Load Game, Options, Quit, with disabled states) and a status strip
// (location, up to three open objectives, health / mana / level / gold), in the Tab screen's style. Rows emit
// PauseActions; the native sub-screens (Save, Load, the Options tree, the Quit box) stay native.
// `PauseConfig::hostOptions`: Options opens a host Options page whose entries still open the native screens.
// Phase 2, `PauseConfig::hostPages`: the Options, Audio and Controller pages are host drawn (sliders, toggles, the
// scheme selector) over native screens 9 / 13 / 10 the glue keeps open and hidden; values come from guest memory
// (PauseOptionsState) and changes are Set* actions the glue applies through the native widgets. Default Settings asks
// on a host confirm card. `PauseConfig::quitCard`: Quit asks "Quit to title" / "Quit to desktop" on a host card.
// `PauseConfig::pcSettingsPages`: PC Settings (and Key Bindings) open the modern SettingsPages inside the pause menu
// instead of emitting OpenPcSettings / OpenKeyBindings (the F1 overlay), when the glue passes a schema.
// What the glue executes for each action: runtime/gameui/README.md, "Pause menu actions".
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
#include "gameui/Model.h"
#include "gameui/PauseOptions.h"
#include "gameui/Screen.h"
#include "gameui/SettingsPages.h"

namespace gameui {

enum class PauseActionKind : uint8_t {
    Resume,          // START tap / Cross on 0x2314 (432708)
    OpenSave,        // focus 0x2311 + Cross -> screen 5
    OpenLoad,        // focus 0x2312 + Cross -> screen 4
    OpenOptions,     // focus 0x2313 + Cross -> screen 9
    Quit,            // focus 0x2315 + Cross -> the native Yes / No box
    OpenController,  // host Options page without hostPages: native screen 10, shown
    OpenAudio,       // host Options page without hostPages: native screen 13, shown
    OpenPcSettings,  // host Options page: the F1 overlay
    DefaultSettings, // host Options page without hostPages: 0x2344, the native Yes / No box, shown
    // ---- phase 2 (hostPages) ----
    EnterPage,           // value = PausePage entered (Options, Audio, Controller): open its native screen, hidden
    LeavePage,           // value = PausePage left (Options, Audio, Controller): close its native screen
    SetMasterVolume,     // value 0..100: slider 0x2351 on screen 13
    SetMusicVolume,      // value 0..100: slider 0x2352 on screen 13
    SetSubtitles,        // value 0 / 1: check box 0x2353 on screen 13
    SetStereo,           // value 0 / 1: check box 0x2354 on screen 13
    SetControllerScheme, // value 0..2: button 0x2367 on screen 10 (cycles)
    SetVibration,        // value 0 / 1: 0x2368 on screen 10
    SetReversePitch,     // value 0 / 1: 0x2369 on screen 10
    RestoreDefaults,     // confirmed on the host card: 0x2344 on screen 9, answer the native box Yes
    OpenKeyBindings,     // Controller page: the F1 overlay's Controls page
    // ---- phase 3 (quitCard) ----
    QuitToTitle,   // 0x2315 + the native box Yes, without the quit_exits conversion
    QuitToDesktop, // close the window (as F1 > Quit to Desktop)
};
const char* pauseActionName(PauseActionKind k);
// The option a Set* action changes (nullopt for the others), and the Set* action of an option.
std::optional<PauseOption> pauseActionOption(PauseActionKind k);
PauseActionKind pauseSetAction(PauseOption o);

struct PauseAction {
    uint32_t id = 0; // assigned by PauseActionQueue::push
    PauseActionKind kind = PauseActionKind::Resume;
    int32_t value = 0; // Set*: the new value; EnterPage / LeavePage: the PausePage
};

// Thread-safe FIFO (UI thread pushes, EE glue pops), like ActionQueue. A Set* action pushed while the newest queued
// action is the same Set* kind replaces that one's value (a slider drag queues one value, not one per frame); the
// returned id is then the queued action's.
class PauseActionQueue {
public:
    uint32_t push(PauseAction a);
    std::optional<PauseAction> pop();
    size_t size() const;
    void clear();

private:
    mutable std::mutex m_;
    std::deque<PauseAction> q_;
    uint32_t nextId_ = 1;
};

// Native state the glue reads each frame (432518's enable states, the option fields).
struct PauseState {
    bool canSave = true; // Save Game enabled (not when 37e810(0) == 2 or an inventory is open)
    bool canLoad = true; // Load Game enabled (37e810(0) == 1: a save exists)
    bool canOptions = true;
    bool canQuit = true;
    bool canQuitToTitle = true; // the Quit card's "Quit to title"
    std::string location;       // optional: the level name when the glue knows it (else the snapshot's region name)
    PauseOptionsState options;  // readPauseOptions(); invalid = the Audio / Controller values are greyed out
    // pcSettingsPages: the PC settings (dsettings::ModernSettings::schema()); null = the rows emit OpenPcSettings /
    // OpenKeyBindings as before. `now` (seconds) and `capturedKey` are passed on to the SettingsPages.
    const SettingsSchema* pcSettings = nullptr;
    double now = 0;
    std::string capturedKey;
};

enum class PausePage : uint8_t { Root, Options, Audio, Controller, QuitConfirm, DefaultsConfirm };
const char* pausePageName(PausePage p);

enum class PauseRow : uint8_t {
    Resume,
    Save,
    Load,
    Options,
    Quit,
    Controller,
    Audio,
    PcSettings,
    Defaults,
    Back,
    // phase 2 / 3
    MasterVolume,
    MusicVolume,
    Subtitles,
    Stereo,
    Scheme,
    Vibration,
    ReversePitch,
    KeyBindings,
    QuitToTitle,
    QuitToDesktop,
    RestoreDefaults,
    Cancel,
};
const char* pauseRowLabel(PauseRow r);

enum class PauseRowKind : uint8_t { Button, Slider, Toggle, Choice };
PauseRowKind pauseRowKind(PauseRow r);
std::optional<PauseOption> pauseRowOption(PauseRow r);
// "A" / "B" / "C" for the scheme values.
const char* pauseSchemeLabel(int32_t scheme);

struct PauseConfig {
    ScreenConfig screen;
    bool hostOptions = false; // Options opens the host Options page (its entries open the native screens)
    bool hostPages = false;   // phase 2: host Audio / Controller pages and the Defaults card (implies hostOptions)
    bool quitCard = false;    // phase 3: Quit opens the host Quit card
    int32_t sliderStep = 5;   // Left / Right on a volume slider
    uint32_t optionHoldFrames = 120; // a changed value is shown until the guest reports it, or this many frames
    uint32_t suspendFrames = 120;    // input ignored after opening a native child, until reactivate() or this passes
    bool pcSettingsPages =
        false; // PC Settings / Key Bindings open the modern SettingsPages (needs PauseState::pcSettings)
    // Embedding (TitleScreen's Options): the page the screen opens on and Back returns from. With a rootPage other
    // than Root, Back on that page sets PauseFrameOutput::wantsBack (no action: the host emits Enter / LeavePage of
    // its root itself) and the screen closes.
    PausePage rootPage = PausePage::Root;
    bool statusPanel = true; // the right-hand status strip (location, objectives, Rynn)
    bool centred = false;    // the menu column centred on the canvas (for use without the status strip)
    std::string rootTitle;   // breadcrumb before the page stack ("Main Menu"); empty = none
    // One Options screen (with pcSettingsPages and a schema): Options opens the PC settings pages with the game's own
    // options folded into their Audio and Controls pages (UnifiedOptions.h); there is no separate PC Settings row and
    // no host Options / Audio / Controller page. The native screens 9 / 13 / 10 are still entered (EnterPage /
    // LeavePage) so the game's options apply through the same Set* actions.
    bool unifiedOptions = false;
};

struct PauseFrameOutput {
    DrawList draw;
    UiSound sound = UiSound::None;
    std::vector<PauseAction> actions;     // to push into the PauseActionQueue
    bool wantsResume = false;             // Resume was chosen (a Resume action is in `actions`); the screen closed
    std::vector<SettingsAction> settings; // pcSettingsPages: edits to apply (dsettings::ModernSettings::apply)
    bool wantsBack = false;               // rootPage != Root: Back on the root page; the screen closed
};

class PauseScreen {
public:
    explicit PauseScreen(PauseConfig cfg = {});

    void open();       // root page (PauseConfig::rootPage), focus on its first row
    void reactivate(); // a native child closed and the pause is active again: input resumes, page and focus kept
    void close();
    bool isOpen() const { return open_; }
    // An action that opens a native child was emitted; input is ignored until reactivate() (or the time-out).
    bool suspended() const { return suspendLeft_ > 0; }

    PauseFrameOutput frame(const Snapshot& s, const PauseState& st, const UiInput& in, const Viewport& vp);

    PausePage page() const { return page_; }
    // Pages under the current one (Back returns to the last), root first.
    std::vector<PausePage> pageStack() const;
    TargetId focus() const { return focus_; }
    TargetId hover() const { return hover_; }
    const std::vector<HitTarget>& targets() const { return targets_; }
    const std::vector<Hint>& hints() const { return hints_; }
    std::vector<PauseRow> rows() const; // rows of the current page, in order
    static std::vector<PauseRow> rowsOf(PausePage p);
    bool rowEnabled(PauseRow r, const PauseState& st) const;
    // The value shown for an option: the last one set from this screen until the guest reports it (or the hold time
    // passes), else the guest's.
    int32_t optionValue(PauseOption o, const PauseState& st) const;
    bool dragging() const { return dragRow_.has_value(); }
    // Pixel rect of a slider row's track on the current page (empty when the row is not shown): mouse positioning.
    Rect sliderTrack(PauseRow r) const;
    // pcSettingsPages: the PC Settings pages are shown (frame() draws them instead of the pause page), and the pages
    // themselves (onApplied(), capturing()).
    bool pcSettingsOpen() const { return pcOpen_; }
    SettingsPages& pcSettings() { return pcPages_; }

private:
    struct StackEntry {
        PausePage page;
        TargetId focus;
    };
    struct Override {
        bool active = false;
        int32_t value = 0;
        uint32_t age = 0;
    };

    PauseConfig cfg_;
    bool open_ = false;
    PausePage page_ = PausePage::Root;
    TargetId focus_, hover_;
    std::vector<StackEntry> stack_;
    Override overrides_[kPauseOptionCount];
    std::optional<PauseRow> dragRow_;
    uint32_t suspendLeft_ = 0;
    std::vector<HitTarget> targets_;
    std::vector<Hint> hints_;
    LayoutFrame lf_;
    SettingsPages pcPages_;
    bool pcOpen_ = false;
    // unifiedOptions: the pages are the Options screen (game options folded in); the native screen entered under them
    // (Options 9 alone, Audio 13 or Controller 10), and an embedded screen that opens straight onto them.
    bool unified_ = false, unifiedPending_ = false;
    PausePage nativeSub_ = PausePage::Options;
    SettingsSchema merged_;

    bool hostOptions() const { return cfg_.hostOptions || cfg_.hostPages; }
    float dx() const; // design x offset of the menu column (PauseConfig::centred)
    Rect shifted(Rect r) const { return {r.x + dx(), r.y, r.w, r.h}; }
    bool isCard(PausePage p) const { return p == PausePage::QuitConfirm || p == PausePage::DefaultsConfirm; }
    const HitTarget* findTarget(const TargetId& id) const;
    Rect rowRect(PausePage p, int i) const; // design units
    void buildTargets(const PauseState& st);
    void ensureFocus();
    void buildHints(const PauseState& st);
    void pushPage(PausePage p, PauseRow focusRow, PauseFrameOutput& out);
    void openUnified(PauseFrameOutput* out);
    // The native screen for the page the unified pages show (Audio 13, Controller 10, else only Options 9).
    void syncNativePage(PauseFrameOutput& out, bool closing);
    // Game option edits -> Set* actions; ResetPage also restores the page's game options.
    void splitUnified(std::vector<SettingsAction>& in, PauseFrameOutput& out);
    void activate(PauseRow r, const PauseState& st, PauseFrameOutput& out);
    void adjust(PauseRow r, int dir, const PauseState& st, PauseFrameOutput& out);
    bool setOption(PauseOption o, int32_t v, const PauseState& st, PauseFrameOutput& out, bool sound);
    void sliderFromPointer(PauseRow r, float x, const PauseState& st, PauseFrameOutput& out);
    void back(PauseFrameOutput& out);
    void ageOverrides(const PauseState& st);
    void applyInput(const PauseState& st, const UiInput& in, PauseFrameOutput& out);
    void draw(const Snapshot& s, const PauseState& st, DrawList& d) const;
    void drawMenu(PausePage p, bool live, const PauseState& st, DrawList& d) const;
    void drawRow(PauseRow r, const Rect& rd, bool live, const PauseState& st, DrawList& d) const;
    void drawCard(const PauseState& st, DrawList& d) const;
    void drawStatus(const Snapshot& s, const PauseState& st, DrawList& d) const;
};

namespace design::pause {
Rect menuPanel();
Rect row(int i);
Rect statusPanel();
Rect sliderTrack(int i);       // the track inside row(i)
Rect choicePrev(int i);        // the "<" of a Choice row (clicking it steps back)
Rect cardPanel(int rows);      // the confirm card, centred
Rect cardRow(int i, int rows); // a row of the confirm card
} // namespace design::pause

} // namespace gameui
