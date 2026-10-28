// The modern Save Game / Load Game screen: a slot browser in the modern screens' style over the game's own slot
// screens (Load = 4, Save = 5), which the glue keeps open and hidden. Left: the shown page's four slot cards
// (thumbnail, location, date and time; empty, no-space and damaged slots marked), a page bar; right: the focused
// slot's large preview and details. Confirm cards: Overwrite (a used slot in Save), Load (while a level runs; the
// native screen asks then too), optionally Save into an empty slot. States: reading, no memory card, unformatted, full.
// The screen emits SaveLoadActions; the glue drives the native list with them (runtime/gameui/README.md, "Save / Load
// actions"; research: docs/journal/2026-10-25/modern-save-load-title.md).
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
#include "gameui/SaveSlots.h"
#include "gameui/Screen.h"

namespace gameui {

enum class SaveLoadMode : uint8_t { Save, Load };
// Where the screen was opened from: the title's Load Game, the pause menu's Save / Load, or the save offered at a
// level change (Save screen mode 1, screen +0x1e4: Back skips the save).
enum class SaveLoadContext : uint8_t { Title, Pause, LevelChange };

enum class SaveLoadActionKind : uint8_t {
    SelectSlot,       // value = slot number: put the native selection (+0xa4) on it (the page must be shown)
    ChangePage,       // value = page: rewrite the slot folder cells and rescan (the pager's PgDn / PgUp)
    ConfirmSave,      // value = slot number (an empty slot): save there
    ConfirmOverwrite, // value = slot number (a used slot): replace that save (the host card already asked)
    ConfirmLoad,      // value = slot number (a used slot): load it (the host card asked when a level runs)
    Back,             // leave the screen (START = the GUI's back key 1); LevelChange: skip saving
};
const char* saveLoadActionName(SaveLoadActionKind k);

struct SaveLoadAction {
    uint32_t id = 0; // assigned by SaveLoadActionQueue::push
    SaveLoadActionKind kind = SaveLoadActionKind::SelectSlot;
    int32_t value = 0;
};

// Thread-safe FIFO (UI thread pushes, EE glue pops). A SelectSlot pushed behind a queued SelectSlot replaces it (focus
// moving over several cards selects natively once).
class SaveLoadActionQueue {
public:
    uint32_t push(SaveLoadAction a);
    std::optional<SaveLoadAction> pop();
    size_t size() const;
    void clear();

private:
    mutable std::mutex m_;
    std::deque<SaveLoadAction> q_;
    uint32_t nextId_ = 1;
};

// The confirm card on top of the list.
enum class SaveCard : uint8_t { None, Save, Overwrite, Load };
// What a Confirm* started: the screen shows a "Loading..." / "Saving..." card from that frame on (the native load runs
// synchronously and keeps the old picture up for a moment) until reactivate(), open() or close().
enum class SaveProgress : uint8_t { None, Loading, Saving };
enum class SaveCardButton : uint8_t { Primary, Cancel };

struct SaveLoadConfig {
    ScreenConfig screen;
    bool confirmSave = false;        // ask before saving into an empty slot (the native screen does not)
    bool confirmLoadOnTitle = false; // ask before loading from the title (the native screen does not)
    uint32_t suspendFrames = 600;    // input ignored after a Confirm* until reactivate(), close() or this passes
    uint32_t pageWaitFrames = 180;   // a ChangePage not reflected by the state after this is given up
};

struct SaveLoadFrameOutput {
    DrawList draw;
    UiSound sound = UiSound::None;
    std::vector<SaveLoadAction> actions; // to push into the SaveLoadActionQueue
    bool wantsBack = false;              // Back was chosen (a Back action is in `actions`); the screen closed
};

class SaveLoadScreen {
public:
    explicit SaveLoadScreen(SaveLoadConfig cfg = {});

    // `focusSlot`: the slot number to focus first (0 = the native selection, else the first useful slot).
    void open(SaveLoadMode mode, SaveLoadContext ctx, int focusSlot = 0);
    void close();
    bool isOpen() const { return open_; }
    // The native confirm / save / load ended and the slot screen is the active screen again (e.g. a failed save's
    // OK box was answered): input resumes, the card closes.
    void reactivate();
    bool suspended() const { return suspendLeft_ > 0; }

    SaveLoadFrameOutput frame(const SaveSlotsState& st, const UiInput& in, const Viewport& vp);

    SaveLoadMode mode() const { return mode_; }
    SaveLoadContext context() const { return ctx_; }
    SaveCard card() const { return card_; }
    // The progress card shown (a Confirm* sent, or SaveSlotsState::loadingSlot).
    SaveProgress progress() const { return progress_; }
    int focusedSlot() const; // slot number, 0 = none (a card button or nothing focused)
    // The page the screen asked for (ChangePage) and waits for; -1 = none.
    int pendingPage() const { return pendingPage_; }
    TargetId focus() const { return focus_; }
    TargetId hover() const { return hover_; }
    const std::vector<HitTarget>& targets() const { return targets_; }
    const std::vector<Hint>& hints() const { return hints_; }

    // Whether Confirm on a slot of this state does anything in `mode` (Load: used; Save: empty, used, damaged).
    static bool slotActionable(SaveLoadMode mode, SlotState s);

private:
    SaveLoadConfig cfg_;
    bool open_ = false;
    SaveLoadMode mode_ = SaveLoadMode::Load;
    SaveLoadContext ctx_ = SaveLoadContext::Pause;
    SaveCard card_ = SaveCard::None;
    int cardSlot_ = 0;
    SaveProgress progress_ = SaveProgress::None;
    int progressSlot_ = 0;
    TargetId focus_, hover_;
    TargetId listFocus_; // the focus under a card
    int wantFocus_ = 0;  // slot number to focus once its page is shown (open, page change)
    int focusRow_ = 0;   // row kept across page changes
    int pendingPage_ = -1;
    uint32_t pendingAge_ = 0;
    int lastSelected_ = 0; // slot number of the last SelectSlot sent
    uint32_t suspendLeft_ = 0;
    std::vector<HitTarget> targets_;
    std::vector<Hint> hints_;
    LayoutFrame lf_;

    const HitTarget* findTarget(const TargetId& id) const;
    bool listLive(const SaveSlotsState& st) const; // the list takes input (no card, not reading, not waiting)
    void buildTargets(const SaveSlotsState& st);
    void ensureFocus(const SaveSlotsState& st);
    void buildHints(const SaveSlotsState& st);
    void steer(const SaveSlotsState& st, SaveLoadFrameOutput& out);
    void changePage(const SaveSlotsState& st, int delta, SaveLoadFrameOutput& out);
    void activateSlot(const SaveSlotsState& st, int number, SaveLoadFrameOutput& out);
    void confirm(SaveLoadActionKind k, int number, SaveLoadFrameOutput& out);
    void pressCard(SaveCardButton b, const SaveSlotsState& st, SaveLoadFrameOutput& out);
    void back(SaveLoadFrameOutput& out);
    void applyInput(const SaveSlotsState& st, const UiInput& in, SaveLoadFrameOutput& out);
    void draw(const SaveSlotsState& st, DrawList& d) const;
    void drawSlot(const SaveSlotsState& st, const SaveSlot& s, const Rect& rd, DrawList& d) const;
    void drawProgress(const SaveSlotsState& st, DrawList& d) const;
    void drawPreview(const SaveSlotsState& st, DrawList& d) const;
    void drawCard(const SaveSlotsState& st, DrawList& d) const;
};

namespace design::saveload {
Rect listPanel();
Rect slotCard(int row); // row 0..3
Rect slotThumb(int row);
Rect pageBar();
Rect pageArrow(int dir); // -1 / +1
Rect previewPanel();
Rect previewImage();
Rect cardPanel();
Rect cardButton(int i); // 0 = primary, 1 = cancel
} // namespace design::saveload

} // namespace gameui
