// The modern Tab screen: pages, focus, navigation and drawing as a pure state machine.
//   frame(snapshot, input, viewport) -> draw commands + UI actions
// Layout is rebuilt every frame from the snapshot and the viewport, input is applied against that layout (hover /
// click hit tests, directional focus moves), then the page is drawn. No platform or ImGui calls.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "gameui/Actions.h"
#include "gameui/Draw.h"
#include "gameui/Input.h"
#include "gameui/Layout.h"
#include "gameui/Model.h"

namespace gameui {

enum class Page : uint8_t { Shop, Inventory, Character, Spells, Skills, Journal, Map };
inline constexpr int kPageCount = 7;
const char* pageLabel(Page p);
// Native section shown underneath a page (screen+0x154), -1 = leave it.
int nativeSectionFor(Page p);

// Button glyphs the renderer resolves per device.
enum class Prompt : uint8_t {
    Confirm,
    Back,
    Secondary,
    Tertiary,
    PagePrev,
    PageNext,
    FilterPrev,
    FilterNext,
    Navigate, // up / down (move): drawn as arrow shapes, not font glyphs (Gelasio has no arrows)
    Adjust    // left / right (change a value): arrow shapes
};

enum class TargetKind : uint8_t {
    None,
    PageTab,    // index = Page
    FilterTab,  // index = CategoryFilter
    GridCell,   // index = cell (origin cell for items)
    EquipSlot,  // index = ItemCategory (Character page)
    Spell,      // index = spell
    Skill,      // index = skill row
    SkillPlus,  // index = skill row (mouse only)
    SkillMinus, // index = skill row (mouse only)
    Quest,      // index = quest
    Marker,     // index = quest record with a map marker
    ShopStock,  // the merchant's current item
    ShopPrev,   // mouse only
    ShopNext,   // mouse only
    Hint,       // index = Prompt: clicking a footer hint presses it
    MenuRow,    // index = PauseRow (PauseScreen)
    SaveSlot,   // index = save slot number (SaveLoadScreen)
    PageStep,   // index = -1 / +1: the slot page arrows (mouse only)
    CardButton, // index = the confirm card's button (SaveLoadScreen: SaveCardButton, TitleScreen: TitleRow)
    TitleRow,   // index = TitleRow (TitleScreen)
};
struct TargetId {
    TargetKind kind = TargetKind::None;
    int index = -1;
    bool operator==(const TargetId&) const = default;
};
struct HitTarget {
    TargetId id;
    Rect rect;
    bool navigable = true; // reachable with directional input (mouse-only buttons are not)
    bool enabled = true;
};

struct Hint {
    Prompt prompt;
    std::string label;
    bool enabled = true;
};

struct ScreenConfig {
    Theme theme;
    TextMeasure measure; // empty = estimateTextWidth
    bool showComparison = true;
    bool includeShopPage = true; // a Shop page appears first while a shop is open
    float mapImageW = 0;         // native map image size, when the glue knows it (marker placement)
    float mapImageH = 0;
    float mapAspect = 0;        // on-screen width / height of the drawn map (the native page's pixels are not square)
    uint32_t toastFrames = 150; // how long a refusal message stays
};

// Feedback sounds the host plays through the game's own UI sound path (native actions make their own sounds; a host
// action that taps a native widget leaves the sound to it). Move: the focus tick; Page: a page / section / tab flip
// (the tick); Confirm: a row, button or card activated (the native click); Back: a page or card left (the native back
// sound); Slider / Toggle: a value stepped / switched (the native slider and check box sounds).
enum class UiSound : uint8_t { None, Move, Page, Confirm, Back, Slider, Toggle };

struct FrameOutput {
    DrawList draw;
    UiSound sound = UiSound::None;
    std::vector<UiAction> actions; // to push into the ActionQueue (ids are assigned there)
    bool wantsClose = false;       // the screen asked to close (a Close action is in `actions` too)
};

class InventoryScreen {
public:
    explicit InventoryScreen(ScreenConfig cfg = {});

    void open(const Snapshot& s, Page initial = Page::Inventory);
    void close();
    bool isOpen() const { return open_; }

    // One UI frame. `s` is the newest published snapshot.
    FrameOutput frame(const Snapshot& s, const UiInput& in, const Viewport& vp);
    // Feed results from the ResultQueue: refusals show a toast. A Close the game did not carry out (refused, aborted)
    // after the screen closed itself on Back reopens it where it was (page, filter and focus kept).
    void onResult(const ActionResult& r);
    // The screen asked to close and waits for the game's result. Meanwhile it takes no input; page() and previewRect()
    // stay as they were, so the glue keeps the native screen arranged under the last picture.
    bool closePending() const { return closePending_; }

    Page page() const { return page_; }
    CategoryFilter filter() const { return filter_; }
    TargetId focus() const { return focus_; }
    TargetId hover() const { return hover_; }
    const std::vector<HitTarget>& targets() const { return targets_; }
    const std::vector<Hint>& hints() const { return hints_; }
    // Character page: where Rynn's live preview goes (empty on other pages). The glue frames the world camera on it.
    Rect previewRect() const { return preview_; }
    const std::string& toast() const { return toast_; }
    std::vector<Page> visiblePages(const Snapshot& s) const;

    // Native map image size (pixels), when the glue knows it: the focused marker is ringed on the map.
    void setMapImageSize(float w, float h, float aspect) {
        cfg_.mapImageW = w;
        cfg_.mapImageH = h;
        cfg_.mapAspect = aspect;
    }

    // Directional move used by the gamepad / keyboard: best navigable target from `from` in direction (dx, dy).
    static int pickDirectional(const std::vector<HitTarget>& t, const Rect& from, int dx, int dy);

private:
    ScreenConfig cfg_;
    bool open_ = false;
    bool closePending_ = false;
    Page page_ = Page::Inventory;
    CategoryFilter filter_ = CategoryFilter::All;
    TargetId focus_, hover_;
    TargetId pageFocus_[kPageCount]; // focus remembered per page
    uint32_t lastSteer_ = 0;         // handle of the last SelectItem sent, to avoid re-sending
    uint32_t steerWait_ = 0;         // frames since then (re-sent when the details did not arrive)
    int lastSpellSteer_ = -1;        // spell of the last SelectSpell sent by focus
    int lastMarkerSteer_ = -1;       // quest record of the last SelectMarker sent by focus (Map page)
    bool pendingSelect_ = false;     // a SelectItem / SelectSpell is queued or running (the queue coalesces them)
    uint32_t pendingAge_ = 0;
    std::vector<uint32_t> prefetched_; // handles whose details were fetched once (equipped gear, alternatives)
    int questScroll_ = 0;              // first visible journal row
    std::vector<HitTarget> targets_;
    std::vector<Hint> hints_;
    Rect preview_;
    std::string toast_;
    uint32_t toastLeft_ = 0;
    LayoutFrame lf_;
    std::vector<int> journalOrder_; // quest indices in list order

    float measure(std::string_view t, FontRole f, float px) const;
    void setPage(Page p, const Snapshot& s, FrameOutput& out);
    void buildTargets(const Snapshot& s);
    void buildHints(const Snapshot& s);
    void ensureFocus(const Snapshot& s);
    void applyInput(const Snapshot& s, const UiInput& in, FrameOutput& out);
    void activate(const TargetId& t, const Snapshot& s, FrameOutput& out, bool fromMouse);
    void press(Prompt p, const Snapshot& s, FrameOutput& out);
    void steerSelection(const Snapshot& s, FrameOutput& out);
    const HitTarget* findTarget(const TargetId& id) const;
    const ItemEntry* focusedItem(const Snapshot& s) const;

    void draw(const Snapshot& s, DrawList& d) const;
    void drawHeader(const Snapshot& s, DrawList& d) const;
    void drawFooter(DrawList& d) const;
    void drawInventory(const Snapshot& s, DrawList& d) const;
    void drawGrid(const Snapshot& s, DrawList& d, bool dimFiltered) const;
    void drawItemDetail(const Snapshot& s, const ItemEntry* it, const Rect& panel, DrawList& d, bool shopPrices) const;
    void drawCharacter(const Snapshot& s, DrawList& d) const;
    void drawNameplate(const Snapshot& s, const Rect& preview, DrawList& d) const;
    std::vector<const ItemEntry*> alternatives(const Snapshot& s, ItemCategory slot) const;
    void drawSpells(const Snapshot& s, DrawList& d) const;
    void drawSkills(const Snapshot& s, DrawList& d) const;
    void drawJournal(const Snapshot& s, DrawList& d) const;
    void drawMap(const Snapshot& s, DrawList& d) const;
    void drawShop(const Snapshot& s, DrawList& d) const;
    void drawFocus(const Rect& r, DrawList& d) const;
    void drawBar(const Rect& r, int32_t cur, int32_t max, Color fill, const std::string& label, DrawList& d) const;
    void textD(DrawList& d, const Rect& design, const std::string& s, FontRole f, Color c,
               TextAlign a = TextAlign::Left) const;
    float fontPx(FontRole f) const;
};

// Design-space rectangles shared by layout, hit testing and drawing (exposed for tests and the glue's icon crops).
namespace design {
Rect gridCell(int cell); // one cell of the 8 x 3 inventory grid
Rect gridItem(int originCell, int w, int h);
Rect pageTab(int slot, int count);
Rect filterTab(int f);
Rect detailPanel();
Rect previewArea();      // Character page: Rynn in the centre
Rect inventoryPreview(); // Inventory and Character pages: Rynn left of the centre column
Rect centreColumn();     // the centre column (inside the native 4:3 window, over the native grid)
Rect mapImageArea();     // Map page: where the native map image is placed (inside the native window)
Rect equipCard(int i);   // Character page: weapon, bow, armour
} // namespace design

} // namespace gameui
