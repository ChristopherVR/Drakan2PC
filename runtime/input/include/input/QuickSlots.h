// Quick-slot hotkeys (QUICK1..QUICK9): key N uses / equips the Nth item of the game's own quick inventory.
//
// The native Quick Inventory (SCES_500.06, docs/journal/2026-10-25/quick-slot-hotkeys.md):
//   * ctl = player + 0x584 keeps the bar's item list at ctl+0x214 ({cell, 0} records of 8 bytes, capacity +0x218,
//     count +0x21c), the bar's current index at +0x220, its scroll animation at +0x228 and "bar shown" at +0x10.
//     The list holds the hot-slotted grid cells (record +4) in column order: cells 0, 8, 16, 1, 9, 17, ... (built by
//     33fa20 on the first open after a load, +0x238 set, and on every inventory close by 434288).
//   * R1 / Q (command 0x403a, 23cae0): with one item a weapon is used at once, otherwise the bar opens at +0x220;
//     another R1 steps. Attack (0x4006) while the bar is shown selects list[+0x220] (33eaf8), uses / equips it
//     (33f630, message 0x1045: weapons are drawn, potions drunk) and closes the bar (33fc88(ctl, 0)).
//
// The driver sets +0x220 to N - 1 while the bar is closed and overlays the scheme's own Quick Inventory and Attack
// buttons on the next pad samples, so everything runs through the native PadMgr dispatch on the game's thread (the
// same safe route as drakan_quick_inventory.h). Native refusals (swimming, climbing, a summon, scripted scenes) stay
// native: the bar simply does not open and nothing else is pressed.
#pragma once
#include "input/GuestInterface.h"
#include <cstdint>
#include <string>
#include <vector>

namespace drakan_input {

namespace quickslot {
// globals
inline constexpr uint32_t kContextPtr = 0x5565d4; // ctx; ctx+0x4b0 = player actor handle
inline constexpr uint32_t kStatsBlock = 0x4fbac8; // S; S+0x34 = 1 while the inventory screen is open
inline constexpr uint32_t kScriptedFlag = 0x4fbafc;
inline constexpr uint32_t kPadTable = 0x740; // PadMgr + 0x740 (0x4e9800): 17 records {action, button mask, ...}
inline constexpr uint32_t kPadTableCount = 17;
inline constexpr uint32_t kPadTableStride = 12;
inline constexpr uint32_t kKeymap = 0x8; // PadMgr + 8: u16 command per action id (512)
// Start (action 0x120) is bound to Attack 0x4006 too, but opens the pause menu: Start / Select are never overlaid.
inline constexpr uint32_t kSystemButtons = 0x800 | 0x100;
// player
inline constexpr uint32_t kController = 0x584;
inline constexpr uint32_t kSummonState = 0x1040; // 0 or 5 = free (1..4 summon: the bar is refused)
inline constexpr uint32_t kMoveFlags = 0xab0;    // & 0x8000: the bar is refused and Attack attacks
inline constexpr uint32_t kMoveBlocked = 0x8000;
// controller ctl
inline constexpr uint32_t kBusy = 0x0;
inline constexpr uint32_t kBarShown = 0x10;
inline constexpr uint32_t kSlots = 0x30;
inline constexpr uint32_t kSlotStride = 0x10;
inline constexpr uint32_t kList = 0x214;
inline constexpr uint32_t kListCapacity = 0x218;
inline constexpr uint32_t kListCount = 0x21c;
inline constexpr uint32_t kListIndex = 0x220;
inline constexpr uint32_t kListScroll = 0x228;
inline constexpr uint32_t kListDirty = 0x238; // set after a load: the next open rebuilds the list (341388 -> 33fa20)
inline constexpr uint32_t kListStride = 8;
// slot record
inline constexpr uint32_t kEquipped = 0x0;
inline constexpr uint32_t kHot = 0x4;
inline constexpr uint32_t kItem = 0x8;
inline constexpr uint32_t kMaxContinuation = 0x53; // item values <= this are continuation markers of a larger item
inline constexpr uint32_t kItemType = 0xc;         // 10 weapon, 11 usable
inline constexpr uint32_t kTypeWeapon = 10;
// commands
inline constexpr uint32_t kCmdQuickInventory = 0x403a;
inline constexpr uint32_t kCmdAttack = 0x4006;
inline constexpr int kGridColumns = 8, kGridRows = 3, kGridCells = 24;
} // namespace quickslot

// Native quick inventory order of a grid: column by column, top to bottom (33fa20 / 434288). `hot[c]` is true when
// cell c holds the top-left cell of an item whose hot-slot flag is set. Returns the cells in bar order.
std::vector<int> quickSlotOrder(const bool (&hot)[quickslot::kGridCells]);

// The bar's live list (cells in bar order); empty when the controller is not valid.
std::vector<int> nativeQuickList(IGuest& g, uint32_t ctl);
// The player data object (ctx+0x4b0 -> +0xfc -> +0x10), 0 when there is none.
uint32_t quickSlotPlayer(IGuest& g);
// Pad button (scePad mask, active high) that the live controller scheme binds to `command`, 0 when none.
uint16_t nativePadButton(IGuest& g, uint32_t padMgr, uint32_t command);

class QuickSlotDriver {
public:
    enum class Phase { Idle, Open, WaitOpen, Retry, Confirm, WaitClose };
    // Pad overlay for the next samples: bits forced up (released) and forced down (pressed), scePad masks.
    struct Overlay {
        uint16_t up = 0, down = 0;
    };

    // EE thread, once per PadMgr update before the game reads the pad. `slot` = 1..kQuickSlotMax pressed this frame
    // (0 = none); `allowed` = free gameplay (no menu, cut scene, Arokh, focus loss or suspension). Log lines are
    // appended to *log.
    void tick(IGuest& g, int slot, bool allowed, uint32_t padMgr, std::string* log);
    Overlay overlay() const { return overlay_; }
    Phase phase() const { return phase_; }
    void reset();

    // Frames a button is first held released (to make an edge even while it is physically held), then pressed.
    static constexpr int kReleaseFrames = 2, kPressFrames = 2;
    static constexpr int kOpenWait = 8, kCloseWait = 30;
    // A refused open (a weapon swap or a drink still playing) is tried again this often, kRetryGap frames apart.
    static constexpr int kRetries = 6, kRetryGap = 15;

private:
    bool press(uint16_t mask); // drives overlay_; true once the press is complete
    Phase phase_ = Phase::Idle;
    Overlay overlay_;
    int frames_ = 0, slot_ = 0, cell_ = -1, retries_ = 0;
    uint32_t ctl_ = 0, item_ = 0;
    uint16_t openMask_ = 0, confirmMask_ = 0;
};

} // namespace drakan_input
