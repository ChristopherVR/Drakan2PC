// Snapshot of everything the modern Tab screen shows. Plain data, std only: the game thread fills one per frame
// (Reader.h), publishes it through ModelExchange, and the UI thread reads an immutable copy.
#pragma once
#include "gameui/DualWield.h"
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace gameui {

// ---- categories ----
// There is no category field in the game: derived from object type (h+0xc) and resource class (rec+8).
enum class ItemCategory : uint8_t { Melee, Bow, Ammo, Armour, Consumable, SpellScroll, Other };
// Filter tabs of the inventory grid.
enum class CategoryFilter : uint8_t { All, Weapons, Armour, Consumables, Other };
inline constexpr int kCategoryFilterCount = 5;

ItemCategory categoryFor(uint32_t classId, uint32_t objectType);
bool matchesFilter(ItemCategory c, CategoryFilter f);
const char* categoryLabel(ItemCategory c); // "Weapon", "Bow", ...
const char* filterLabel(CategoryFilter f); // "All", "Weapons", ...
bool isEquippable(ItemCategory c);         // Melee, Bow, Armour

// What the item's quantity pair means (drawn as a bar with this label). [inf] from the fixtures.
enum class QuantityKind : uint8_t { None, Durability, Ammo, Charges };
QuantityKind quantityKindFor(ItemCategory c);

// ---- inventory ----
struct ItemEntry {
    int slot = -1;       // record index: 0..23 for the grid, 0x1b for the shop stock item
    int originCell = -1; // top-left cell of the footprint (== slot for grid items)
    int width = 1;       // footprint in cells (columns)
    int height = 1;      // footprint in cells (rows)
    uint32_t handle = 0; // guest actor handle: identity for verification; never dereferenced by the UI
    uint32_t objectType = 0;
    uint32_t classId = 0;
    ItemCategory category = ItemCategory::Other;
    uint32_t nameId = 0;
    std::string name;          // UTF-8; from the string cache, else the cached details line 0, else ""
    bool nameResident = false; // name came from the string cache
    // Details lines exactly as the native details panel formats them ("Damage: 4", "Speed: Fast", ...). Only the
    // native selection has fresh lines; others carry the lines cached from when they were last selected.
    std::vector<std::string> details;
    bool detailsCurrent = false; // details were built for this item this frame
    bool requirementMet = true;  // from ctl+0x49c, only meaningful while detailsCurrent
    int32_t quantity = -1;       // instance +0x14 / +0x18 (dec); -1 / -1 = indestructible or n/a
    int32_t quantityMax = -1;
    bool equipped = false;
    // Which hand holds an equipped melee weapon: Left = the off-hand weapon in the shield slot (dual wielding,
    // gameui/DualWield.h), Right = equipment slot 0. None for everything else.
    enum class Hand : uint8_t { None, Right, Left };
    Hand hand = Hand::None;
    uint32_t stance = ~0u; // melee property 0x1e (0 = one-handed), else ~0
    bool hotSlot = false;
    // raw properties (0 when absent)
    int32_t price = 0;       // prop 0x11 [inf]
    int32_t skillNeeded = 0; // prop 0x13
    int32_t meleeDamage = 0; // prop 0x1a, melee only [inf]
    bool dropAllowed = false;
};

// ---- spells ----
struct SpellEntry {
    int index = 0;
    int knownLevel = 0; // 0 = not learned
    std::string name;   // spell type name ("Fireball"); empty when the string is not resident
    bool nameResident = false;
    uint32_t levelResource = 0;
    uint32_t state = 0;
    std::vector<std::string> details; // native details lines, cached from when the spell was last selected
};
struct SpellBook {
    std::vector<SpellEntry> entries;
    int selected = -1;        // ctl+0x4b4
    uint32_t summonState = 0; // player+0x1040: 1..4 = summon running (selection refused)
    bool summonBusy() const { return summonState >= 1 && summonState <= 4; }
};

// ---- skills ----
struct SkillEntry {
    int id = 0; // 1 Melee, 2 Archery, 3 Magic
    int level = 0;
    int max = 0;
    int committed = 0; // the level points cannot be returned below (= level outside the Skills page)
    std::string name;
};
struct SkillSheet {
    std::vector<SkillEntry> entries;
    int unspentPoints = 0;
    bool pending = false; // values are the Skills page's working copies (applied when the page is left)
};

// ---- character ----
struct CharacterStats {
    int32_t hp = 0, hpMax = 0;
    int32_t mana = 0, manaMax = 0;
    int32_t gold = 0;
    int32_t xp = 0;
    int32_t level = 0;
};

// ---- journal and map ----
struct QuestEntry {
    int index = 0;
    int status = 0;        // quest prop 0; 1 / 2 = listed (2 = done [inf])
    bool optional = false; // quest prop 1
    uint32_t textId = 0;
    std::string text; // UTF-8; empty when not resident
    bool textResident = false;
    uint32_t captionId = 0;
    std::string caption; // map marker caption ("The Palace")
    bool captionResident = false;
    int region = 0;             // record +0x14: the region (map) the record belongs to
    int32_t mapX = 0, mapY = 0; // marker position in map image pixels
    uint32_t icon = 0;
    bool enabled = false;
    bool listed() const { return status == 1 || status == 2; }
    bool done() const { return status == 2; } // [inf] 2 = completed
    bool marker() const { return captionId != 0; }
};
struct MapRegion {
    int index = 0;
    std::string name;
};

// ---- shop ----
struct ShopState {
    bool active = false; // ctl+0xc
    uint32_t shopkeeper = 0;
    int selection = 0;     // ctl+0x1c
    bool hasStock = false; // record 0x1b holds the current stock item
    ItemEntry stock;       // the stock item the native shop list shows
};

// ---- native screen state the UI and the action verification need ----
struct NativeState {
    bool valid = false;         // context and player found
    bool onDragon = false;      // inventory not available
    bool inventoryOpen = false; // S+0x34
    uint32_t topScreen = 0;     // *(0x4fb60c): changes when a Yes / No box opens over 0x15
    uint32_t topScreenId = 0;
    int section = -1;            // screen+0x154 while 0x15 is the top screen
    float animation = 0;         // screen+0x15c while 0x15 is the top screen: > 0 opening, < 0 closing, 0 idle
    bool controllerBusy = false; // ctl+0: selection change in progress
    int selection = -1;          // ctl+0x20
    int pane = 0;                // ctl+0x24
    bool detailsValid = false;   // ctl+0x4ac
    int skillRow = -1;           // Skills section +0x58 (the native row selection), -1 = not found
    int mapSelection = -1;       // Map section +0x6c: the selected marker's quest record (43a588 cycles it), -1 = n/a
};

struct Snapshot {
    uint64_t sequence = 0; // incremented by every read; actions measure time-outs in sequences (frames)
    NativeState native;
    CharacterStats stats;
    std::vector<ItemEntry> items;    // one entry per item (origin cell), sorted by slot
    std::array<int8_t, 24> cellItem; // grid cell -> index into items, -1 = empty
    SpellBook spells;
    SkillSheet skills;
    std::vector<QuestEntry> quests; // read whenever the context exists (also on the dragon, outside screen 0x15)
    std::vector<MapRegion> regions;
    int mapRegion = -1; // region of the native map section (+0x70) while screen 0x15 is up, else -1
    ShopState shop;
    bool dualWield = false;           // [general] dual_wield
    bool bowDrawn = false;            // the bow holds the left hand (no off-hand equip)
    uint32_t shieldSlot = 0;          // handle in equipment slot 2 (a shield or the left-hand weapon), 0 = empty
    std::vector<std::string> details; // the native details buffers of the selection (blank lines dropped)

    Snapshot() { cellItem.fill(-1); }
    const ItemEntry* itemAtCell(int cell) const; // any cell of the footprint
    const ItemEntry* itemByHandle(uint32_t handle) const;
    // The equipped item that `candidate` would replace (same category, not itself), or null.
    const ItemEntry* equippedFor(const ItemEntry& candidate) const;
    const ItemEntry* equippedIn(ItemCategory c) const; // Melee: the right hand
    const ItemEntry* inHand(ItemEntry::Hand h) const;
    // A left-hand weapon is held or the option is on: the screens name hands.
    bool handsShown() const;
};

// ---- dual wielding ----
// What using `it` does with hands (gameui/DualWield.h): the labels "Equip to left hand", "Unequip left hand", ...
dualwield::Use handUse(const Snapshot& s, const ItemEntry& it);
const char* handName(ItemEntry::Hand h); // "Right hand", "Left hand", ""
// The Confirm label for a hand-specific use ("Equip Left Hand"), null for Use::Other.
const char* handUseLabel(dualwield::Use u);

// ---- quick slots ----
// Hot-slotted grid items in the order of the native Quick Inventory bar: column by column, top to bottom (cells 0, 8,
// 16, 1, 9, ...). Entry i is quick slot i + 1, the item key i + 1 (QUICK1..QUICK9) uses / equips.
std::vector<const ItemEntry*> quickSlotItems(const Snapshot& s);
// 1-based quick-slot number of `it` (an element of s.items), 0 when it is not a quick slot.
int quickSlotNumber(const Snapshot& s, const ItemEntry& it);

// ---- journal / map helpers (map, pause and strip screens) ----
// The region shown on the map: Snapshot::mapRegion, else the region of the first enabled marker, else of the first
// listed quest, else the first named region; -1 when nothing is known.
int currentRegion(const Snapshot& s);
// Name of region `index` ("Surdana"); empty when the table has no name for it.
std::string regionName(const Snapshot& s, int index);
// `override` when given (the glue may know the level name), else the current region's name, else "".
std::string locationName(const Snapshot& s, const std::string& override = {});
// Listed quests in display order: primary before optional; within each, open before completed, then table order.
std::vector<const QuestEntry*> orderedObjectives(const Snapshot& s, bool includeDone = true);
// What to show for an objective. Strings are only read when resident, so: the text, else the marker caption, else
// the name of the quest's region, else `fallback` (the location name).
std::string objectiveLabel(const Snapshot& s, const QuestEntry& q, const std::string& fallback);

// ---- comparison ----
struct StatDelta {
    std::string label;     // "Damage"
    std::string candidate; // value text of the inspected item ("8")
    std::string equipped;  // value text of the equipped item ("4"), empty when it has no such line
    int better = 0;        // +1 candidate better, -1 worse, 0 equal / not comparable
};
// "Damage: 4" -> {"Damage", "4"}; false for lines without "label: value".
bool splitStatLine(const std::string& line, std::string& label, std::string& value);
// Compares the details lines (by label; numbers and the Speed / Range words are ordered), falling back to the item
// properties when lines are missing. Line 0 (the name) is skipped.
std::vector<StatDelta> compareItems(const ItemEntry& candidate, const ItemEntry& equipped);

// ---- publish: game thread writes, UI thread reads ----
// Double buffered: the writer fills a back buffer it owns (reused when no reader still holds it) and publish()
// swaps it in. Readers get an immutable shared copy that stays valid while they hold it. No allocation in steady
// state, one short mutex section per publish / acquire.
class ModelExchange {
public:
    ModelExchange();
    Snapshot& back(); // writer only
    void publish();   // writer only: back() becomes the front; a fresh or recycled buffer becomes back()
    std::shared_ptr<const Snapshot> acquire() const; // any thread; null before the first publish
    uint64_t published() const;                      // number of publishes so far

private:
    mutable std::mutex m_;
    std::shared_ptr<Snapshot> front_, back_;
    uint64_t count_ = 0;
};

} // namespace gameui
