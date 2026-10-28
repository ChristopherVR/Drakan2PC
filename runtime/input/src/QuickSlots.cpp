#include "input/QuickSlots.h"
#include <cstdio>

namespace drakan_input {
namespace {
namespace q = quickslot;

bool ram(uint32_t p) {
    return p >= 0x100000u && p < 0x2000000u;
}
void say(std::string* log, const char* fmt, int a = 0, int b = 0, unsigned c = 0) {
    if (!log)
        return;
    char buf[200];
    std::snprintf(buf, sizeof buf, fmt, a, b, c);
    *log += "[quick-slot] ";
    *log += buf;
    *log += "\n";
}
} // namespace

std::vector<int> quickSlotOrder(const bool (&hot)[q::kGridCells]) {
    std::vector<int> out;
    for (int col = 0; col < q::kGridColumns; ++col)
        for (int row = 0; row < q::kGridRows; ++row)
            if (hot[col + row * q::kGridColumns])
                out.push_back(col + row * q::kGridColumns);
    return out;
}

std::vector<int> nativeQuickList(IGuest& g, uint32_t ctl) {
    std::vector<int> out;
    if (!ram(ctl))
        return out;
    const uint32_t list = g.read32(ctl + q::kList);
    const uint32_t count = g.read32(ctl + q::kListCount), capacity = g.read32(ctl + q::kListCapacity);
    if (!ram(list) || count > uint32_t(q::kGridCells) || count > capacity)
        return out;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t cell = g.read32(list + i * q::kListStride);
        out.push_back(cell < uint32_t(q::kGridCells) ? int(cell) : -1);
    }
    return out;
}

uint32_t quickSlotPlayer(IGuest& g) {
    const uint32_t ctx = g.read32(q::kContextPtr);
    if (!ram(ctx))
        return 0;
    const uint32_t handle = g.read32(ctx + 0x4b0);
    if (!ram(handle))
        return 0;
    const uint32_t inner = g.read32(handle + 0xfc);
    const uint32_t player = ram(inner) ? g.read32(inner + 0x10) : 0;
    return ram(player) ? player : 0;
}

uint16_t nativePadButton(IGuest& g, uint32_t padMgr, uint32_t command) {
    for (uint32_t n = 0; n < q::kPadTableCount; ++n) {
        const uint32_t rec = padMgr + q::kPadTable + n * q::kPadTableStride;
        const uint32_t action = g.read32(rec), mask = g.read32(rec + 4);
        if (action >= 512 || !mask || mask > 0xffffu || (mask & (mask - 1)) || (mask & q::kSystemButtons))
            continue;
        if (g.read16(padMgr + q::kKeymap + action * 2) == command)
            return static_cast<uint16_t>(mask);
    }
    return 0;
}

void QuickSlotDriver::reset() {
    phase_ = Phase::Idle;
    overlay_ = {};
    frames_ = 0;
    slot_ = 0;
    cell_ = -1;
    ctl_ = item_ = 0;
    retries_ = 0;
}

bool QuickSlotDriver::press(uint16_t mask) {
    ++frames_;
    if (frames_ <= kReleaseFrames) {
        overlay_ = {mask, 0};
        return false;
    }
    if (frames_ <= kReleaseFrames + kPressFrames) {
        overlay_ = {0, mask};
        return false;
    }
    overlay_ = {};
    frames_ = 0;
    return true;
}

void QuickSlotDriver::tick(IGuest& g, int slot, bool allowed, uint32_t padMgr, std::string* log) {
    if (!allowed) {
        if (phase_ != Phase::Idle)
            say(log, "slot %d: cancelled (gameplay no longer free)", slot_);
        reset();
        return;
    }
    const uint32_t player = quickSlotPlayer(g);
    const uint32_t ctl = player ? player + q::kController : 0;
    if (phase_ != Phase::Idle && ctl != ctl_) {
        say(log, "slot %d: cancelled (the player changed)", slot_);
        reset();
        return;
    }
    switch (phase_) {
        case Phase::Idle: {
            if (slot <= 0)
                return;
            if (!ctl) {
                say(log, "slot %d: no player", slot);
                return;
            }
            const uint32_t summon = g.read32(player + q::kSummonState);
            if (g.read32(q::kScriptedFlag) || g.read32(q::kStatsBlock + 0x34) || g.read32(ctl + q::kBusy) ||
                (summon != 0 && summon != 5)) {
                say(log, "slot %d: refused (scripted %d / busy or summon %d)", slot,
                    int(g.read32(q::kScriptedFlag) | g.read32(q::kStatsBlock + 0x34)), int(summon));
                return;
            }
            std::vector<int> list = nativeQuickList(g, ctl);
            if (list.empty() && g.read32(ctl + q::kListDirty)) {
                // After a load the list is empty and marked for a rebuild: the bar's first open builds it (33fa20)
                // from the hot-slot flags in the native order.
                bool hot[q::kGridCells] = {};
                for (int c = 0; c < q::kGridCells; ++c) {
                    const uint32_t rec = ctl + q::kSlots + uint32_t(c) * q::kSlotStride;
                    const uint32_t item = g.read32(rec + q::kItem);
                    hot[c] = g.read32(rec + q::kHot) && item > q::kMaxContinuation;
                }
                list = quickSlotOrder(hot);
            }
            if (slot > int(list.size()) || list[size_t(slot - 1)] < 0) {
                say(log, "slot %d: empty (%d quick items)", slot, int(list.size()));
                return;
            }
            const int cell = list[size_t(slot - 1)];
            const uint32_t rec = ctl + q::kSlots + uint32_t(cell) * q::kSlotStride;
            const uint32_t item = g.read32(rec + q::kItem);
            if (item <= q::kMaxContinuation || !ram(item)) {
                say(log, "slot %d: cell %d holds no item", slot, cell);
                return;
            }
            // Use on an equipped weapon puts it away (33f630 posts 0x1045 with -1): a hotkey only draws.
            if (g.read32(rec + q::kEquipped) && g.read32(item + q::kItemType) == q::kTypeWeapon) {
                say(log, "slot %d: cell %d already equipped", slot, cell);
                return;
            }
            openMask_ = nativePadButton(g, padMgr, q::kCmdQuickInventory);
            confirmMask_ = nativePadButton(g, padMgr, q::kCmdAttack);
            if (!openMask_ || !confirmMask_) {
                say(log, "slot %d: no native pad button for Quick Inventory / Attack (%x / %x)", slot, openMask_,
                    confirmMask_);
                return;
            }
            slot_ = slot;
            cell_ = cell;
            ctl_ = ctl;
            item_ = item;
            frames_ = 0;
            const bool shown = g.read32(ctl + q::kBarShown) != 0;
            if (!shown || !g.read32(ctl + q::kListScroll))
                g.write32(ctl + q::kListIndex, uint32_t(slot - 1));
            phase_ = shown ? Phase::WaitOpen : Phase::Open;
            say(log, "slot %d -> cell %d, item %x", slot, cell, item);
            break;
        }
        case Phase::Open:
            if (press(openMask_))
                phase_ = Phase::WaitOpen;
            break;
        case Phase::WaitOpen: {
            overlay_ = {};
            if (g.read32(ctl + q::kBarShown)) {
                if (g.read32(ctl + q::kListScroll)) // a step the player started: let it settle
                    break;
                const std::vector<int> list = nativeQuickList(g, ctl);
                if (slot_ > int(list.size()) || list[size_t(slot_ - 1)] != cell_) {
                    say(log, "slot %d: the bar's list changed, not confirmed", slot_);
                    reset();
                    break;
                }
                g.write32(ctl + q::kListIndex, uint32_t(slot_ - 1));
                phase_ = Phase::Confirm;
                frames_ = 0;
                break;
            }
            if (++frames_ > kOpenWait) {
                // One quick weapon is used at once without the bar (23cae0). Otherwise the game refused the bar,
                // e.g. while a weapon swap or a drink still plays: try again a few times (a buffered key press).
                const bool direct =
                    g.read32(ctl + q::kListCount) == 1 && g.read32(item_ + q::kItemType) == q::kTypeWeapon;
                if (direct) {
                    say(log, "slot %d: used directly (the only quick item)", slot_);
                    reset();
                } else if (++retries_ > kRetries) {
                    say(log, "slot %d: the game refused the Quick Inventory bar", slot_);
                    reset();
                } else {
                    phase_ = Phase::Retry;
                    frames_ = 0;
                }
            }
            break;
        }
        case Phase::Retry:
            overlay_ = {};
            if (++frames_ >= kRetryGap) {
                frames_ = 0;
                const uint32_t rec = ctl + q::kSlots + uint32_t(cell_) * q::kSlotStride;
                if (g.read32(rec + q::kItem) != item_ ||
                    (g.read32(rec + q::kEquipped) && g.read32(item_ + q::kItemType) == q::kTypeWeapon)) {
                    say(log, "slot %d: cell %d changed or is equipped now, not retried", slot_, cell_);
                    reset();
                } else if (g.read32(ctl + q::kBarShown)) {
                    phase_ = Phase::WaitOpen; // the player opened it meanwhile
                } else {
                    g.write32(ctl + q::kListIndex, uint32_t(slot_ - 1));
                    phase_ = Phase::Open;
                }
            }
            break;
        case Phase::Confirm:
            // Checked until the press starts (the press itself closes the bar).
            if (frames_ < kReleaseFrames &&
                (!g.read32(ctl + q::kBarShown) || (g.read32(player + q::kMoveFlags) & q::kMoveBlocked))) {
                say(log, "slot %d: the bar closed before the confirm", slot_);
                reset();
                break;
            }
            if (press(confirmMask_))
                phase_ = Phase::WaitClose;
            break;
        case Phase::WaitClose:
            overlay_ = {};
            if (!g.read32(ctl + q::kBarShown)) {
                say(log, "slot %d: confirmed, cell %d equipped=%d", slot_, cell_,
                    int(g.read32(ctl + q::kSlots + uint32_t(cell_) * q::kSlotStride + q::kEquipped)));
                reset();
            } else if (++frames_ > kCloseWait) {
                say(log, "slot %d: the bar is still shown after the confirm", slot_);
                reset();
            }
            break;
    }
}

} // namespace drakan_input
