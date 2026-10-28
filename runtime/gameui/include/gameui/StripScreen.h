// The compact "I" inventory: Drakan: Order of the Flame's placement (an 8 x 3 bar flush with the bottom centre, the
// world visible above it) drawn in the modern Tab screen's style (the same cells, focus, quantity / equipped /
// quick-slot marks and ItemRef icon crops), on one opaque plate with a slim prompt row above the grid and a compact
// item card right of it. Same UiActions as the Tab screen's Inventory page; same navigation model (HitTargets +
// pickDirectional).
//
// Layout: a 1920 x 1080 design canvas scaled by the viewport height (narrowed only when a 1360-unit-wide canvas would
// not fit, i.e. narrower than 4:3), centred horizontally, its bottom on the viewport's bottom. A cell is 60 units
// (0.056 H) on every aspect; nothing spans the viewport except a subtle gradient behind the bar.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "gameui/Actions.h"
#include "gameui/Draw.h"
#include "gameui/Input.h"
#include "gameui/Layout.h"
#include "gameui/Model.h"
#include "gameui/Screen.h"

namespace gameui {

class StripScreen {
public:
    explicit StripScreen(ScreenConfig cfg = {});

    void open(const Snapshot& s);
    void close();
    bool isOpen() const { return open_; }
    bool closePending() const { return closePending_; }

    // One UI frame (see InventoryScreen::frame). Actions: SelectItem (focus steers the native selection so the card
    // gets the native details), UseOrEquip, Drop, ToggleHotSlot, Close.
    FrameOutput frame(const Snapshot& s, const UiInput& in, const Viewport& vp);
    // Results from the ResultQueue: refusals show a toast; a Close the game did not carry out reopens the strip.
    void onResult(const ActionResult& r);

    TargetId focus() const { return focus_; }
    TargetId hover() const { return hover_; }
    const std::vector<HitTarget>& targets() const { return targets_; }
    const std::vector<Hint>& hints() const { return hints_; }
    const std::string& toast() const { return toast_; }

    // Pixel rectangles of the last frame's layout (also valid before the first frame for `vp` via layoutFor), for the
    // glue: where to place the native 3D icons so the ItemIcon crops find them, and what to keep clear.
    struct Layout {
        LayoutFrame frame;
        Rect plate;      // the opaque panel holding the prompts, the grid and the card
        Rect bar;        // the grid area (inside the plate)
        Rect card;       // the item card (inside the plate, right)
        Rect hints;      // the prompt row (inside the plate, above the grid)
        Rect nativeGrid; // where the glue places the native 8 x 3 grid (under the opaque plate, Tab screen scale)
        Rect cell(int cell) const;
        Rect item(int originCell, int w, int h) const;
        Rect bounds() const; // everything the strip draws (excluding the gradient)
    };
    static Layout layoutFor(const Viewport& vp);
    const Layout& layout() const { return layout_; }

private:
    ScreenConfig cfg_;
    bool open_ = false;
    bool closePending_ = false;
    TargetId focus_, hover_;
    uint32_t lastSteer_ = 0, steerWait_ = 0;
    bool pendingSelect_ = false;
    uint32_t pendingAge_ = 0;
    std::vector<uint32_t> prefetched_;
    std::vector<HitTarget> targets_;
    std::vector<Hint> hints_;
    std::vector<Rect> hintRects_; // design units
    std::string toast_;
    uint32_t toastLeft_ = 0;
    Layout layout_;

    const HitTarget* findTarget(const TargetId& id) const;
    const ItemEntry* focusedItem(const Snapshot& s) const;
    void buildTargets(const Snapshot& s);
    void ensureFocus(const Snapshot& s);
    void buildHints(const Snapshot& s);
    void applyInput(const Snapshot& s, const UiInput& in, FrameOutput& out);
    void press(Prompt p, const Snapshot& s, FrameOutput& out);
    void steerSelection(const Snapshot& s, FrameOutput& out);
    void draw(const Snapshot& s, DrawList& d) const;
    void drawCard(const Snapshot& s, DrawList& d) const;
};

// Design rectangles of the strip (1920 x 1080 canvas of StripScreen::Layout::frame).
namespace design::strip {
Rect cell(int cell);
Rect item(int originCell, int w, int h);
Rect bar();
Rect card();
Rect hintRow();
Rect plate();
Rect nativeGrid();
} // namespace design::strip

} // namespace gameui
