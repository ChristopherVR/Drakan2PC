// PC Settings as modern pages: the F1 overlay's settings (one SettingsSchema, see SettingsSchema.h) drawn in the
// modern pause menu's visual language, for the modern pause menu (and later the modern title menu).
//   * a landing page (Display, Audio, Controls, Gameplay, Key Bindings, Back) in the pause menu's column;
//   * a category page: a wide panel with page tabs, section headings and rows (toggles, "<  value  >" choices with a
//     list card, sliders that drag), scrolling to the focus; the help panel on the right (where the pause menu shows
//     its status strip) explains the focused row, its restart note or why it is unavailable;
//   * key binding pages (gameplay / menu keys): slot chips, press-a-key capture with a conflict question, clear,
//     restore defaults;
//   * cards: "keep these display settings?" with a timed revert, "restart required", a problem report, restore the
//     page's defaults.
// Every edit is a SettingsAction the glue applies at once (dsettings::ModernSettings: the overlay's apply path) and
// answers with onApplied(). Contract: runtime/gameui/README.md, "PC Settings pages".
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "gameui/Draw.h"
#include "gameui/Input.h"
#include "gameui/Layout.h"
#include "gameui/Screen.h"
#include "gameui/SettingsSchema.h"

namespace gameui {

enum class SettingsActionKind : uint8_t {
    Change,        // key = SettingRow::key, value = the new value (choice value, onValue / offValue, slider number)
    Bind,          // layer, action, slot (index into the action's slots; >= size = add), source; replace = remove the
                   // source from the other actions of the layer first ("Replace"), else keep them ("Use for both")
    ClearBinding,  // layer, action, slot
    ResetBindings, // layer: back to the built-in defaults
    ResetPage,     // page = SettingPage::id: the page's defaults (the overlay's "Defaults for this page")
    Restart,       // the user chose "Restart Now" on the restart card: value = the reasons, '\n' separated
};
const char* settingsActionName(SettingsActionKind k);

struct SettingsAction {
    SettingsActionKind kind = SettingsActionKind::Change;
    std::string key, value;
    bool revert = false; // Change: undoing a risky change (the keep card timed out / Revert)
    bool final = true;   // Change from a slider drag: false while the button is held, true on release / a key step
    std::string layer, action, source, page;
    int slot = -1;
    bool replace = false;
};

// What the glue reports back after applying an action.
struct SettingsFeedback {
    bool ok = true;
    std::vector<std::string> restartReasons; // settings of this change that need a restart (shows the restart card)
    std::string error;                       // a problem (shows the problem card)
    std::string message;                     // optional status line for the help panel ("Saved")
};

struct SettingsPagesConfig {
    ScreenConfig screen;
    float revertSeconds = 12;          // the keep card's countdown (the overlay's 12 s)
    float captureSeconds = 12;         // a key capture gives up after this long
    std::vector<std::string> trail;    // breadcrumb before "PC Settings" ({"Paused", "Options"})
    bool drawBackdrop = true;          // false: the host already dimmed the world
    std::string title = "PC Settings"; // the landing page's title and the breadcrumb ("Options" when unified)
};

// Time, key capture and the input of one frame.
struct SettingsInput {
    UiInput ui;
    double now = 0;          // seconds, monotonic (countdowns)
    std::string capturedKey; // while capturing(): the ini name of the key pressed this frame (glue), else empty
};

struct SettingsFrameOutput {
    DrawList draw;
    UiSound sound = UiSound::None;
    std::vector<SettingsAction> actions;
    bool wantsClose = false; // Back on the landing page: return to the menu that opened PC Settings
};

enum class SettingsView : uint8_t { Landing, Page, Bindings };
enum class SettingsCard : uint8_t { None, Choices, KeepDisplay, Restart, Problem, Defaults, Capture, Conflict };

class SettingsPages {
public:
    explicit SettingsPages(SettingsPagesConfig cfg = {});

    // Landing page (focus on the first category), or straight onto the category / binding layer with id `pageId`
    // ("display", "game" ...) at the first frame; Back from that page then leaves the pages (wantsClose). The schema
    // is passed to frame(); open() only resets the state.
    void open(const std::string& pageId = {});
    void close();
    bool isOpen() const { return open_; }
    void setTrail(std::vector<std::string> t) { cfg_.trail = std::move(t); }
    void setTitle(std::string t) { cfg_.title = std::move(t); }
    const SettingsPagesConfig& config() const { return cfg_; }

    SettingsFrameOutput frame(const SettingsSchema& schema, const SettingsInput& in, const Viewport& vp);
    // The glue's answer to the actions of the last frame. Restart reasons are collected and shown on one restart card
    // (after the keep card when both are due); an error shows the problem card.
    void onApplied(const SettingsFeedback& f);

    // While true the glue sends the next pressed key as SettingsInput::capturedKey (Esc and the pad's B as UiKey::Back:
    // cancel) and does not feed keyboard keys as navigation.
    bool capturing() const { return card_ == SettingsCard::Capture; }

    SettingsView view() const { return view_; }
    SettingsCard card() const { return card_; }
    // Current category page index (into schema.pages) or binding layer index (into schema.bindings).
    int pageIndex() const;
    TargetId focus() const { return focus_; }
    TargetId hover() const { return hover_; }
    const std::vector<HitTarget>& targets() const { return targets_; }
    const std::vector<Hint>& hints() const { return hints_; }
    // The focused setting row's key (empty when the focus is not on a setting row).
    std::string focusedKey() const;
    float scroll() const { return scroll_; }
    // Pixel rect of a row's value control (slider track, choice area, toggle area) on the current page; empty when
    // the row is not on this page. Tests and pointer mapping.
    Rect valueRect(const std::string& key) const;
    bool dragging() const { return drag_.has_value(); }
    double keepDeadline() const { return keepDeadline_; }
    // Pixel rect of the capture card's "click here" box (empty when not capturing).
    Rect captureRect() const { return capturing() ? lf_.map(captureBox()) : Rect{}; }
    // Settings of this visit that wait for a restart (the help panel's footer).
    const std::vector<std::string>& restartNeeded() const { return restartNeeded_; }

    // Design-space layout (exposed for tests).
    static Rect landingRow(int i);
    static Rect pagePanel();
    static Rect helpPanel();
    static Rect tabRect(int i, int n);
    static Rect listArea();

private:
    // A focusable element of the current view, laid out in design units (list items before scrolling).
    struct Item {
        enum Kind : uint8_t { Category, BindLayer, Back, Row, Chip, Tab, CardRow, Choice } kind;
        int a = 0, b = 0, c = 0; // Category / Tab: page; Row: section, row; Chip: group, action, slot; Card: index
        Rect rect;
        bool enabled = true;
        bool navigable = true;
        bool scrolls = false; // inside the scrolling list
    };
    struct Heading {
        std::string title;
        float y; // list-relative design y
    };
    struct Drag {
        std::string key;
        float value;
    };
    // A value set from here is shown until the schema reports it (or ~1.5 s pass).
    struct Held {
        std::string key, value;
        int frames;
    };
    struct Capture {
        int group = 0, index = 0, slot = 0;
        std::string action;
        double started = 0;
        bool armed = false; // the mouse button that opened the capture was released
        std::string pendingSource;
    };

    SettingsPagesConfig cfg_;
    const SettingsSchema* schema_ = nullptr;
    bool open_ = false;
    SettingsView view_ = SettingsView::Landing;
    SettingsCard card_ = SettingsCard::None;
    int tab_ = 0; // pages first, then binding layers
    TargetId focus_, hover_, pageFocus_;
    std::vector<Item> items_, cardItems_;
    std::vector<Heading> headings_, notes_;
    std::vector<HitTarget> targets_;
    std::vector<Hint> hints_;
    LayoutFrame lf_;
    float scroll_ = 0, contentH_ = 0;
    bool scrollToFocus_ = true;
    std::optional<Drag> drag_;
    std::string pendingOpen_;
    bool returnOnBack_ = false; // opened straight onto a page: Back from it leaves (wantsClose)
    std::vector<Held> held_;
    double now_ = 0;

    std::string choiceKey_;
    int choiceScroll_ = 0;
    std::string keepKey_, keepPrevious_;
    double keepDeadline_ = 0;
    std::vector<std::string> restartReasons_, restartNeeded_;
    bool restartPending_ = false;
    std::string problem_;
    Capture cap_;
    std::string status_;
    double statusUntil_ = 0;

    const SettingPage* currentPage() const;
    const BindingLayerView* currentLayer() const;
    const SettingRow* rowAt(int section, int row) const;
    const BindingAction* actionAt(int group, int action) const;
    const Item* itemFor(const TargetId& id) const;
    const SettingRow* focusedRow() const;
    std::string shownValue(const SettingRow& r) const;
    bool shownOn(const SettingRow& r) const;
    int shownChoice(const SettingRow& r) const;
    float shownSlider(const SettingRow& r) const;
    int tabCount() const;
    std::string sourceTitle(const std::string& source) const;

    void build();
    float cardHeight(int bodyLines, int rows, bool box) const;
    std::vector<std::string> cardRows() const;
    std::vector<std::string> cardBody() const;
    Rect cardPanel() const;
    Rect captureBox() const;
    void buildCard();
    Rect itemPx(const Item& it) const;
    void buildTargets();
    bool pointerOver(const HitTarget& t, float x, float y) const;
    void ensureFocus();
    void keepFocusVisible();
    void buildHints();
    static Rect sliderTrack(const Rect& valueArea);

    void enterPage(int tab, SettingsFrameOutput& out);
    void switchTab(int dir, SettingsFrameOutput& out);
    void focusCurrentTab(); // focus the tab strip entry of the shown page
    void change(const SettingRow& r, const std::string& value, SettingsFrameOutput& out, bool final = true,
                bool revert = false);
    void adjust(const SettingRow& r, int dir, SettingsFrameOutput& out);
    void sliderFromPointer(const SettingRow& r, float x, SettingsFrameOutput& out, bool final);
    void openCard(SettingsCard c);
    void closeCard();
    void cardBack(SettingsFrameOutput& out);
    void revertKeep(SettingsFrameOutput& out);
    void back(SettingsFrameOutput& out);
    void startCapture(const Item& chip, bool fromPointer);
    void finishCapture(const std::string& source, SettingsFrameOutput& out);
    void emitBind(const std::string& source, bool replace, SettingsFrameOutput& out);
    void activateCard(const Item& it, SettingsFrameOutput& out);
    void activate(const Item& it, SettingsFrameOutput& out, bool fromPointer, float px);
    void press(Prompt p, SettingsFrameOutput& out);
    void moveFocus(int dx, int dy);
    void applyCardInput(const SettingsInput& in, SettingsFrameOutput& out);
    void applyInput(const SettingsInput& in, SettingsFrameOutput& out);
    void ageHeld();

    void draw(DrawList& d) const;
    void drawHeader(DrawList& d) const;
    void drawLanding(DrawList& d) const;
    void drawTabs(DrawList& d) const;
    void drawPage(DrawList& d) const;
    void drawRow(const Item& it, int index, DrawList& d) const;
    void drawChipRow(int first, DrawList& d) const;
    void drawHelp(DrawList& d) const;
    void drawCard(DrawList& d) const;
    void drawFooter(DrawList& d) const;
};

} // namespace gameui
