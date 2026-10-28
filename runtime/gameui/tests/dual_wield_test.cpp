#include "gameui/DualWield.h"
#include "gameui/Model.h"
#include <string>
#include <cstdio>
#include <cstdlib>

using namespace gameui::dualwield;

static unsigned checks;
static void check(bool b, const char* message) {
    ++checks;
    if (!b) {
        std::fprintf(stderr, "FAIL %s\n", message);
        std::exit(1);
    }
}

// The magic5 fixture: Wood Club (cell 1) and Rynn's Dagger (cell 16), both class 0x1000e, stance 0; the bow 0x1004f.
static Item weapon(uint32_t h, uint32_t stance = 0) {
    return {h, kTypeWeapon, kClassMelee, stance};
}
static const Item kClub = weapon(0xa6b5b0), kDagger = weapon(0xa7d4b0), kTwoHanded = weapon(0xa70000, 1);
static const Item kBow{0xa6f930, kTypeWeapon, 0x1004f, ~0u};
static const Item kShield{0xa71000, kTypeWeapon, 0x10090, ~0u};

static SlotQuery query(const Item& item, const Item& right, const Item& offhand = {}, bool enabled = true) {
    SlotQuery q;
    q.enabled = enabled;
    q.nativeSlot = item.cls == kClassMelee ? 0 : item.cls == 0x1004f ? 1 : item.cls == 0x10090 ? 2 : -1;
    q.item = item;
    q.right = right;
    q.offhand = offhand;
    return q;
}

int main() {
    // Slot choice.
    check(chooseSlot(query(kDagger, kClub)) == 2, "second one-handed weapon goes to the left hand");
    check(chooseSlot(query(kDagger, kClub, {}, false)) == 0, "option off: native, replaces the right hand");
    check(chooseSlot(query(kDagger, {})) == 0, "empty right hand: the weapon goes to the right hand");
    check(chooseSlot(query(kClub, kClub)) == 0, "the right-hand weapon itself stays native");
    check(chooseSlot(query(kTwoHanded, kClub)) == 0, "a stance-1 weapon is not taken as one-handed");
    check(chooseSlot(query(kDagger, kTwoHanded)) == 0, "no off-hand next to a stance-1 right hand");
    check(chooseSlot(query(kBow, kClub)) == 1, "the bow keeps its own slot");
    check(chooseSlot(query(kShield, kClub)) == 2, "a shield keeps its own slot");
    check(chooseSlot(query(weapon(0xa72000), kClub, kDagger)) == 0, "both hands full: native, replaces the right hand");
    check(chooseSlot(query(weapon(0xa72000), kClub, kShield)) == 0, "a shield in slot 2 is not displaced");
    check(chooseSlot(query(kDagger, kClub, kDagger)) == 2, "the off-hand weapon maps back to slot 2");
    check(chooseSlot(query(kDagger, kClub, kDagger, false)) == 2, "... also after the option was switched off");
    {
        SlotQuery q = query(kDagger, kClub);
        q.bowDrawn = true;
        check(chooseSlot(q) == 0, "the drawn bow holds the left hand");
        q = query(kDagger, kClub);
        q.item.type = 11;
        check(chooseSlot(q) == 0, "not a weapon object");
        q = query({}, kClub);
        check(chooseSlot(q) == -1, "no item: native result");
    }
    // Left hand bone.
    check(boneAction(kDagger, {}, 0) == BoneAction::Attach, "off-hand weapon, empty bone: attach");
    check(boneAction(kDagger, kDagger, 0) == BoneAction::None, "already attached");
    check(boneAction(kDagger, kClub, 0) == BoneAction::Attach, "another weapon on the bone: replace");
    check(boneAction({}, kDagger, 0) == BoneAction::Detach, "slot emptied: detach the stale weapon");
    check(boneAction({}, {}, 0) == BoneAction::None, "nothing anywhere");
    check(boneAction({}, kBow, 0) == BoneAction::None, "a native item on the bone stays");
    check(boneAction(kShield, {}, 0) == BoneAction::None, "a shield is native (its own bone)");
    check(boneAction(kDagger, kBow, kBow.handle) == BoneAction::None, "the drawn bow owns the bone");
    check(boneAction({}, kDagger, kBow.handle) == BoneAction::None, "never detach while a holder owns the bone");
    // Blade.
    check(bladePlan(true, false).right && !bladePlan(true, false).left, "right strike: the right blade only");
    check(!bladePlan(true, true).right && bladePlan(true, true).left, "left strike: the left blade only");
    check(!bladePlan(false, true).right && !bladePlan(false, true).left, "window closed: both dead (left strike)");
    check(!bladePlan(false, false).right && !bladePlan(false, false).left, "window closed: both dead");

    // Labels: what using the weapon does.
    check(useOf(query(kDagger, kClub)) == Use::EquipLeft, "second one-handed weapon: equip to the left hand");
    check(useOf(query(kDagger, kClub, kDagger)) == Use::UnequipLeft, "the left-hand weapon: unequip");
    check(useOf(query(kDagger, {})) == Use::EquipRight, "empty right hand, option on: equip to the right hand");
    check(useOf(query(kDagger, {}, {}, false)) == Use::Other, "option off, no left weapon: plain Equip");
    check(useOf(query(weapon(0xa72000), kClub, kDagger)) == Use::EquipRight, "both hands full: replaces the right");
    check(useOf(query(weapon(0xa72000), kClub, kDagger, false)) == Use::EquipRight,
          "option off but a left weapon is held: still name the hand");
    check(useOf(query(kClub, kClub)) == Use::Other, "the right-hand weapon itself: native label");
    check(useOf(query(kTwoHanded, kClub)) == Use::Other, "two-handed: no hand label");
    check(useOf(query(kBow, kClub)) == Use::Other, "bow: no hand label");

    // The native details line and its recognition.
    check(std::string(handLine(query(kDagger, kClub))) == "Equips to left hand", "line: equips to left");
    check(std::string(handLine(query(kDagger, kClub, kDagger))) == "In left hand", "line: in left hand");
    check(std::string(handLine(query(kClub, kClub, kDagger))) == "In right hand", "line: in right hand");
    check(std::string(handLine(query(kClub, kClub, {}, false))) == "", "line: option off, one weapon: none");
    check(std::string(handLine(query(kBow, kClub))) == "", "line: bow: none");
    check(isHandLine("In left hand") && !isHandLine("Speed: Fast") && !isHandLine(""), "hand lines recognised");

    // The modern screens' model: hands from the snapshot.
    {
        gameui::Snapshot s;
        auto entry = [](const Item& i, bool equipped, gameui::ItemEntry::Hand hand) {
            gameui::ItemEntry e;
            e.handle = i.handle;
            e.objectType = i.type;
            e.classId = i.cls;
            e.stance = i.stance;
            e.category = gameui::ItemCategory::Melee;
            e.equipped = equipped;
            e.hand = hand;
            return e;
        };
        s.dualWield = true;
        s.items = {entry(kClub, true, gameui::ItemEntry::Hand::Right),
                   entry(kDagger, false, gameui::ItemEntry::Hand::None)};
        check(gameui::handUse(s, s.items[1]) == Use::EquipLeft, "model: dagger goes left");
        check(std::string(gameui::handUseLabel(gameui::handUse(s, s.items[1]))) == "Equip Left Hand", "model label");
        check(s.equippedIn(gameui::ItemCategory::Melee)->handle == kClub.handle,
              "model: the weapon slot is the right hand");
        check(s.equippedFor(s.items[1]) == nullptr, "model: a left-hand candidate compares with an empty left hand");
        s.items[1].equipped = true;
        s.items[1].hand = gameui::ItemEntry::Hand::Left;
        s.shieldSlot = kDagger.handle;
        check(gameui::handUse(s, s.items[1]) == Use::UnequipLeft, "model: unequip left");
        check(s.equippedIn(gameui::ItemCategory::Melee)->handle == kClub.handle, "model: left weapon not the slot");
        check(s.inHand(gameui::ItemEntry::Hand::Left)->handle == kDagger.handle && s.handsShown(), "model: hands");
        s.dualWield = false;
        s.items[1] = entry(kDagger, false, gameui::ItemEntry::Hand::None);
        s.shieldSlot = kShield.handle; // a shield in slot 2
        check(gameui::handUse(s, s.items[1]) == Use::Other && !s.handsShown(), "model: option off with a shield");
    }

    std::printf("dual_wield: %u checks passed\n", checks);
    return 0;
}
