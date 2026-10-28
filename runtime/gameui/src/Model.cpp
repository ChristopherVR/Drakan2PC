#include "gameui/Model.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "gameui/Addresses.h"

namespace gameui {

ItemCategory categoryFor(uint32_t classId, uint32_t objectType) {
    namespace c = addr::cls;
    switch (classId) {
        case c::kMelee:
            return ItemCategory::Melee;
        case c::kBow:
            return ItemCategory::Bow;
        case c::kArmourA:
        case c::kArmourB:
        case c::kArmourC:
            return ItemCategory::Armour;
        case c::kHealthElixir:
        case c::kUsableB:
        case c::kUsableC:
            return ItemCategory::Consumable;
        case c::kSpellScroll:
            return ItemCategory::SpellScroll;
        case c::kAmmoA:
        case c::kAmmoB:
            return ItemCategory::Ammo;
        default:
            break;
    }
    // Unknown class: weapons (type 10) are most likely melee variants; usables (11) consumables.
    if (objectType == addr::actor::kTypeWeapon)
        return ItemCategory::Melee;
    if (objectType == addr::actor::kTypeUsable)
        return ItemCategory::Consumable;
    return ItemCategory::Other;
}

bool matchesFilter(ItemCategory c, CategoryFilter f) {
    switch (f) {
        case CategoryFilter::All:
            return true;
        case CategoryFilter::Weapons:
            return c == ItemCategory::Melee || c == ItemCategory::Bow || c == ItemCategory::Ammo;
        case CategoryFilter::Armour:
            return c == ItemCategory::Armour;
        case CategoryFilter::Consumables:
            return c == ItemCategory::Consumable || c == ItemCategory::SpellScroll;
        case CategoryFilter::Other:
            return c == ItemCategory::Other;
    }
    return true;
}

const char* categoryLabel(ItemCategory c) {
    switch (c) {
        case ItemCategory::Melee:
            return "Weapon";
        case ItemCategory::Bow:
            return "Bow";
        case ItemCategory::Ammo:
            return "Ammunition";
        case ItemCategory::Armour:
            return "Armour";
        case ItemCategory::Consumable:
            return "Consumable";
        case ItemCategory::SpellScroll:
            return "Spell Scroll";
        case ItemCategory::Other:
            break;
    }
    return "Item";
}

const char* filterLabel(CategoryFilter f) {
    switch (f) {
        case CategoryFilter::All:
            return "All";
        case CategoryFilter::Weapons:
            return "Weapons";
        case CategoryFilter::Armour:
            return "Armour";
        case CategoryFilter::Consumables:
            return "Consumables";
        case CategoryFilter::Other:
            return "Other";
    }
    return "";
}

bool isEquippable(ItemCategory c) {
    return c == ItemCategory::Melee || c == ItemCategory::Bow || c == ItemCategory::Armour;
}

QuantityKind quantityKindFor(ItemCategory c) {
    switch (c) {
        case ItemCategory::Melee:
        case ItemCategory::Armour:
            return QuantityKind::Durability;
        case ItemCategory::Bow:
        case ItemCategory::Ammo:
            return QuantityKind::Ammo;
        case ItemCategory::Consumable:
        case ItemCategory::SpellScroll:
            return QuantityKind::Charges;
        case ItemCategory::Other:
            break;
    }
    return QuantityKind::None;
}

// ---- Snapshot ----

const ItemEntry* Snapshot::itemAtCell(int cell) const {
    if (cell < 0 || cell >= static_cast<int>(cellItem.size()))
        return nullptr;
    const int i = cellItem[static_cast<size_t>(cell)];
    return i >= 0 && i < static_cast<int>(items.size()) ? &items[static_cast<size_t>(i)] : nullptr;
}

const ItemEntry* Snapshot::itemByHandle(uint32_t handle) const {
    if (handle == 0)
        return nullptr;
    for (const ItemEntry& e : items)
        if (e.handle == handle)
            return &e;
    return nullptr;
}

const ItemEntry* Snapshot::equippedIn(ItemCategory c) const {
    for (const ItemEntry& e : items)
        if (e.equipped && e.category == c && e.hand != ItemEntry::Hand::Left)
            return &e;
    return nullptr;
}

const ItemEntry* Snapshot::inHand(ItemEntry::Hand h) const {
    for (const ItemEntry& e : items)
        if (e.equipped && e.hand == h)
            return &e;
    return nullptr;
}

bool Snapshot::handsShown() const {
    return dualWield || inHand(ItemEntry::Hand::Left) != nullptr;
}

const ItemEntry* Snapshot::equippedFor(const ItemEntry& candidate) const {
    if (!isEquippable(candidate.category))
        return nullptr;
    // A weapon headed for the left hand is compared with the left-hand weapon (none: nothing to compare).
    if (handUse(*this, candidate) == dualwield::Use::EquipLeft)
        return inHand(ItemEntry::Hand::Left);
    for (const ItemEntry& e : items)
        if (e.equipped && e.category == candidate.category && e.handle != candidate.handle &&
            e.hand != ItemEntry::Hand::Left)
            return &e;
    return nullptr;
}

namespace {
dualwield::Item ruleItem(const ItemEntry* e) {
    if (!e)
        return {};
    return {e->handle, e->objectType, e->classId, e->stance};
}
} // namespace

dualwield::Use handUse(const Snapshot& s, const ItemEntry& it) {
    dualwield::SlotQuery q;
    q.enabled = s.dualWield;
    q.nativeSlot = it.category == ItemCategory::Melee ? dualwield::kSlotRight : -1;
    q.item = ruleItem(&it);
    q.right = ruleItem(s.inHand(ItemEntry::Hand::Right));
    q.offhand = ruleItem(s.inHand(ItemEntry::Hand::Left));
    if (!q.offhand.handle)
        q.offhand.handle = s.shieldSlot; // a shield: not a weapon, but the slot is taken
    q.bowDrawn = s.bowDrawn;
    return dualwield::useOf(q);
}

const char* handUseLabel(dualwield::Use u) {
    switch (u) {
        case dualwield::Use::EquipLeft:
            return "Equip Left Hand";
        case dualwield::Use::EquipRight:
            return "Equip Right Hand";
        case dualwield::Use::UnequipLeft:
            return "Unequip Left Hand";
        default:
            return nullptr;
    }
}

const char* handName(ItemEntry::Hand h) {
    switch (h) {
        case ItemEntry::Hand::Right:
            return "Right hand";
        case ItemEntry::Hand::Left:
            return "Left hand";
        default:
            return "";
    }
}

// ---- quick slots ----

std::vector<const ItemEntry*> quickSlotItems(const Snapshot& s) {
    std::vector<const ItemEntry*> out;
    for (const ItemEntry& e : s.items)
        if (e.hotSlot && e.originCell >= 0 && e.originCell < addr::ctl::kSlotCount)
            out.push_back(&e);
    // The native list runs column by column, top to bottom (33fa20 / 434288; input/QuickSlots.h).
    std::stable_sort(out.begin(), out.end(), [](const ItemEntry* a, const ItemEntry* b) {
        const int ca = a->originCell % addr::ctl::kGridColumns, cb = b->originCell % addr::ctl::kGridColumns;
        return ca != cb ? ca < cb : a->originCell < b->originCell;
    });
    return out;
}

int quickSlotNumber(const Snapshot& s, const ItemEntry& it) {
    if (!it.hotSlot)
        return 0;
    const auto items = quickSlotItems(s);
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i] == &it)
            return int(i) + 1;
    return 0;
}

// ---- journal / map helpers ----

int currentRegion(const Snapshot& s) {
    if (s.mapRegion >= 0)
        return s.mapRegion;
    for (const QuestEntry& q : s.quests)
        if (q.marker() && q.enabled)
            return q.region;
    for (const QuestEntry& q : s.quests)
        if (q.listed())
            return q.region;
    return s.regions.empty() ? -1 : s.regions.front().index;
}

std::string regionName(const Snapshot& s, int index) {
    for (const MapRegion& r : s.regions)
        if (r.index == index)
            return r.name;
    return {};
}

std::string locationName(const Snapshot& s, const std::string& override) {
    if (!override.empty())
        return override;
    return regionName(s, currentRegion(s));
}

std::vector<const QuestEntry*> orderedObjectives(const Snapshot& s, bool includeDone) {
    std::vector<const QuestEntry*> v;
    for (int optional = 0; optional < 2; ++optional)
        for (int done = 0; done < (includeDone ? 2 : 1); ++done)
            for (const QuestEntry& q : s.quests)
                if (q.listed() && q.optional == (optional == 1) && q.done() == (done == 1))
                    v.push_back(&q);
    return v;
}

std::string objectiveLabel(const Snapshot& s, const QuestEntry& q, const std::string& fallback) {
    if (!q.text.empty())
        return q.text;
    if (!q.caption.empty())
        return q.caption;
    std::string r = regionName(s, q.region);
    return r.empty() ? fallback : r;
}

// ---- comparison ----

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Orderable value of a stat: leading number, or the rank of a Speed / Range word. false = not comparable.
bool rankOf(const std::string& value, double& out) {
    const std::string v = lower(trim(value));
    if (v.empty())
        return false;
    if (std::isdigit(static_cast<unsigned char>(v[0])) || ((v[0] == '-' || v[0] == '+') && v.size() > 1)) {
        char* e = nullptr;
        out = std::strtod(v.c_str(), &e);
        return e != v.c_str();
    }
    static const struct {
        const char* word;
        double rank;
    } kWords[] = {{"very slow", 0}, {"slow", 1},      {"medium", 2}, {"normal", 2}, {"average", 2},
                  {"fast", 3},      {"very fast", 4}, {"short", 0},  {"long", 4}};
    for (const auto& w : kWords)
        if (v == w.word) {
            out = w.rank;
            return true;
        }
    return false;
}

// +1: a higher value is better, -1: lower is better, 0: neutral (prices).
int directionOf(const std::string& label) {
    const std::string l = lower(label);
    if (l.find("price") != std::string::npos || l.find("sell") != std::string::npos ||
        l.find("repair") != std::string::npos)
        return 0;
    if (l.find("needed") != std::string::npos || l.find("cost") != std::string::npos)
        return -1;
    return 1;
}

struct Stat {
    std::string label, value;
};

std::vector<Stat> statsOf(const ItemEntry& it) {
    std::vector<Stat> out;
    for (size_t i = 1; i < it.details.size(); ++i) {
        Stat s;
        if (splitStatLine(it.details[i], s.label, s.value))
            out.push_back(std::move(s));
    }
    auto has = [&](const char* l) {
        return std::any_of(out.begin(), out.end(), [&](const Stat& s) { return lower(s.label) == lower(l); });
    };
    if (it.category == ItemCategory::Melee && it.meleeDamage > 0 && !has("Damage"))
        out.push_back({"Damage", std::to_string(it.meleeDamage)});
    if (it.skillNeeded > 0 && !has("Skill Needed") && it.details.empty())
        out.push_back({"Skill Needed", std::to_string(it.skillNeeded)});
    return out;
}

} // namespace

bool splitStatLine(const std::string& line, std::string& label, std::string& value) {
    const size_t c = line.find(':');
    if (c == std::string::npos || c == 0)
        return false;
    label = trim(line.substr(0, c));
    value = trim(line.substr(c + 1));
    return !label.empty() && !value.empty();
}

std::vector<StatDelta> compareItems(const ItemEntry& candidate, const ItemEntry& equipped) {
    const std::vector<Stat> a = statsOf(candidate), b = statsOf(equipped);
    std::vector<StatDelta> out;
    auto find = [](const std::vector<Stat>& v, const std::string& label) -> const Stat* {
        for (const Stat& s : v)
            if (lower(s.label) == lower(label))
                return &s;
        return nullptr;
    };
    for (const Stat& s : a) {
        StatDelta d{s.label, s.value, {}, 0};
        if (const Stat* o = find(b, s.label)) {
            d.equipped = o->value;
            double x = 0, y = 0;
            if (rankOf(s.value, x) && rankOf(o->value, y) && x != y)
                d.better = (x > y ? 1 : -1) * directionOf(s.label);
        }
        out.push_back(std::move(d));
    }
    for (const Stat& s : b)
        if (!find(a, s.label))
            out.push_back({s.label, {}, s.value, 0});
    return out;
}

// ---- ModelExchange ----

ModelExchange::ModelExchange() : back_(std::make_shared<Snapshot>()) {}

Snapshot& ModelExchange::back() {
    return *back_;
}

void ModelExchange::publish() {
    std::lock_guard<std::mutex> lock(m_);
    std::swap(front_, back_);
    ++count_;
    // The old front becomes the new back buffer unless a reader still holds it.
    if (!back_ || back_.use_count() > 1)
        back_ = std::make_shared<Snapshot>();
}

std::shared_ptr<const Snapshot> ModelExchange::acquire() const {
    std::lock_guard<std::mutex> lock(m_);
    return front_;
}

uint64_t ModelExchange::published() const {
    std::lock_guard<std::mutex> lock(m_);
    return count_;
}

} // namespace gameui
