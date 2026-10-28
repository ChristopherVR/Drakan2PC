// Dual wielding ([general] dual_wield): which hand a one-handed weapon goes to. Pure decisions shared by the runtime
// hooks (ps2xRuntime drakan_dual_wield.cpp, which also owns the switch below) and the inventory screens, so the labels
// always say what using the item will do.
//
// Native model (SCES_500.06, docs/reference/combat-controls.md "Dual wielding"): the equipment slot of an item comes
// from the inventory controller's 0x344cb8 (class 0x1000e melee -> 0 right hand, 0x1004f bow -> 1, 0x10090 shield ->
// 2, ...). Slot 2 (player+0xa68) is the shield slot; every native reader of it checks class 0x10090, so a melee weapon
// parked there is equipped, saved and dropped like a shield but otherwise ignored. The host attaches it to the left
// hand bone and makes its blade live while the right hand's blade is live.
#pragma once
#include <atomic>
#include <cstdint>
#include <optional>
#include <string_view>

namespace gameui::dualwield {

inline constexpr uint32_t kClassMelee = 0x1000e;
inline constexpr uint32_t kTypeWeapon = 10;
inline constexpr int32_t kSlotRight = 0, kSlotOffhand = 2;
// Guest offsets: equipment slots player+0xa48 + n*0x10, the left hand holder's mode (5 = the bow is drawn), the
// stance property of a weapon (copied by the hand holder into its mode).
inline constexpr uint32_t kPlayerSlots = 0xa48, kSlotStride = 0x10, kPlayerLeftHolder = 0xe90, kBowMode = 5;
inline constexpr uint32_t kPropStance = 0x1e;

// The option, set by the runtime (startup and the F1 settings), read by the hooks and the screens.
inline std::atomic<bool> g_enabled{false};
inline bool enabled() {
    return g_enabled.load(std::memory_order_relaxed);
}

struct Item {
    uint32_t handle = 0;
    uint32_t type = 0;     // h+0xc
    uint32_t cls = 0;      // resource record +8
    uint32_t stance = ~0u; // property 0x1e, the weapon type: 0 One Handed, 1 Two Handed, 2 Hammer/Axe
    bool melee() const { return handle && type == kTypeWeapon && cls == kClassMelee; }
    // The disc's weapon schema names the property "Weapon Type" = One Handed, Two Handed, Hammer/Axe, in the order of
    // the native per-type attack table (0x4fc108: one column of actions per type), so only type 0 is one-handed.
    bool oneHanded() const { return melee() && stance == 0; }
};

struct SlotQuery {
    bool enabled = false;    // [general] dual_wield
    int32_t nativeSlot = -1; // what 0x344cb8 returned
    Item item;               // the item being used
    Item right;              // player+0xa48, the right hand
    Item offhand;            // player+0xa68, the shield slot
    bool bowDrawn = false;   // left hand holder mode 5: the bow occupies the left hand
};

// The slot an item goes to. The off-hand item itself always maps back to slot 2 (using it again unequips it, also
// after the option was switched off). A second one-handed weapon goes to the left hand only while the right hand holds
// a different one-handed weapon and the shield slot is empty; with both hands full the native rule (replace the right
// hand) applies.
inline int32_t chooseSlot(const SlotQuery& q) {
    if (q.item.handle && q.item.handle == q.offhand.handle && q.offhand.melee())
        return kSlotOffhand;
    if (!q.enabled || q.nativeSlot != kSlotRight || q.bowDrawn || q.offhand.handle)
        return q.nativeSlot;
    if (!q.item.oneHanded() || !q.right.oneHanded() || q.right.handle == q.item.handle)
        return q.nativeSlot;
    return kSlotOffhand;
}

enum class BoneAction { None, Attach, Detach };

// After the native attach routine (0x31ca20) handled the shield slot: what the left hand bone needs. `onBone` is the
// item currently attached to that bone (one attachment per bone), `leftHolder` the weapon of the left hand holder
// (player+0xe90, the drawn bow), which owns the bone while it is set.
inline BoneAction boneAction(const Item& offhand, const Item& onBone, uint32_t leftHolder) {
    if (leftHolder)
        return BoneAction::None;
    if (offhand.melee())
        return onBone.handle == offhand.handle ? BoneAction::None : BoneAction::Attach;
    // Only a melee weapon that no holder owns can be a stale off-hand; anything else on the bone is native.
    return onBone.melee() ? BoneAction::Detach : BoneAction::None;
}

// Which blade the native attack window (0x49c590 on the right hand holder, `requested`) makes live: the weapon of
// the hand that strikes. A left-handed strike (the mirrored swing) keeps the right blade dead and gives the window
// to the left one; a right-handed strike leaves the left blade dead.
struct BladePlan {
    bool right = false, left = false;
};
inline BladePlan bladePlan(bool requested, bool leftStrike) {
    return leftStrike ? BladePlan{false, requested} : BladePlan{requested, false};
}

// What using a weapon does, for labels: nothing hand-specific, equip to / unequip from a hand.
enum class Use { Other, EquipRight, EquipLeft, UnequipLeft };
inline Use useOf(const SlotQuery& q) {
    if (!q.item.melee())
        return Use::Other;
    const int32_t slot = chooseSlot(q);
    if (slot == kSlotOffhand)
        return q.item.handle == q.offhand.handle ? Use::UnequipLeft : Use::EquipLeft;
    // Name the right hand only where a left hand is in play: with the option on for a one-handed weapon, or while
    // the left hand holds a weapon.
    if (slot == kSlotRight && q.item.handle != q.right.handle &&
        ((q.enabled && q.item.oneHanded()) || q.offhand.melee()))
        return Use::EquipRight;
    return Use::Other;
}

// The line the runtime adds to the native details panel of a melee weapon (the panel's range line, under Speed):
// what using it does, or which hand holds it. The modern screens drop these lines; they label hands themselves.
inline constexpr const char* kHandLines[] = {"Equips to left hand", "Equips to right hand", "In left hand",
                                             "In right hand"};
inline const char* handLine(const SlotQuery& q) {
    switch (useOf(q)) {
        case Use::EquipLeft:
            return kHandLines[0];
        case Use::EquipRight:
            return kHandLines[1];
        case Use::UnequipLeft:
            return kHandLines[2];
        default:
            break;
    }
    if (q.item.melee() && q.item.handle == q.right.handle && (q.enabled || q.offhand.melee()))
        return kHandLines[3];
    return "";
}
inline bool isHandLine(std::string_view line) {
    for (const char* l : kHandLines)
        if (line == l)
            return true;
    return false;
}

} // namespace gameui::dualwield
