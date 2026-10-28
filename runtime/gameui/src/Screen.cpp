#include "gameui/Screen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

#include "Paint.h"
#include "gameui/Addresses.h"

namespace gameui {

// ---- design rectangles (1920 x 1080 canvas) ----

namespace {

constexpr float kMargin = 96;
constexpr float kContentTop = 140;
constexpr float kContentBottom = 968;
// The Inventory grid sits in the centre column, inside the native 4:3 window (design x 240..1680 on 16:9): the glue
// draws the native 3D icons exactly under these cells (they are culled outside that window). Rynn stands left of it.
constexpr float kCell = 96, kCellGap = 6;
constexpr float kGridY = 214;
constexpr float kInvGridX = 600;
constexpr float kShopGridX = 776;
constexpr float kGridW = 8 * kCell + 7 * kCellGap; // 810
constexpr float kDetailX = 1440;
constexpr int kCols = addr::ctl::kGridColumns;
constexpr int kCells = addr::ctl::kSlotCount;

constexpr float kSpellRowH = 72, kSpellRowGap = 8, kSpellListY = 150;
constexpr float kSkillRowY = 250, kSkillRowH = 150, kSkillRowGap = 24;
constexpr float kJournalListY = 196, kJournalRowH = 58;
constexpr int kJournalVisibleRows = 13;
constexpr float kMarkerRowY = 196, kMarkerRowH = 56;

Rect gridCellAt(float ox, float oy, int cell) {
    const int col = cell % kCols, row = cell / kCols;
    return {ox + static_cast<float>(col) * (kCell + kCellGap), oy + static_cast<float>(row) * (kCell + kCellGap), kCell,
            kCell};
}

Rect gridItemAt(float ox, float oy, int origin, int w, int h) {
    const Rect c = gridCellAt(ox, oy, origin);
    return {c.x, c.y, static_cast<float>(w) * kCell + static_cast<float>(w - 1) * kCellGap,
            static_cast<float>(h) * kCell + static_cast<float>(h - 1) * kCellGap};
}

Rect equipSlotRect(int i) {
    return {kInvGridX, 196 + static_cast<float>(i) * 210, kGridW, 196};
}
// Character page: each card lists the unequipped gear of its kind at its right (Right from the card reaches them).
constexpr int kAltRows = 2;
constexpr float kAltX = 548, kAltW = 250;
Rect altRow(int card, int i) {
    const Rect c = equipSlotRect(card);
    return {c.x + kAltX, c.y + 40 + static_cast<float>(i) * 76, kAltW, 70};
}

Rect spellRow(int i) {
    return {kMargin, kSpellListY + static_cast<float>(i) * (kSpellRowH + kSpellRowGap), 680, kSpellRowH};
}

Rect skillRow(int i) {
    return {360, kSkillRowY + static_cast<float>(i) * (kSkillRowH + kSkillRowGap), 1200, kSkillRowH};
}
Rect skillMinus(int i) {
    const Rect r = skillRow(i);
    return {r.right() - 184, r.y + 44, 64, 64};
}
Rect skillPlus(int i) {
    const Rect r = skillRow(i);
    return {r.right() - 104, r.y + 44, 64, 64};
}

Rect journalRow(int visibleIndex) {
    return {kMargin, kJournalListY + static_cast<float>(visibleIndex) * kJournalRowH, 720, kJournalRowH - 6};
}

Rect markerRow(int i) {
    return {1384, kMarkerRowY + static_cast<float>(i) * kMarkerRowH, 440, kMarkerRowH - 6};
}

// Inside the native 4:3 window (design x 240..1680 on every aspect): the glue places the native map image there.
Rect mapPanel() {
    return {256, kContentTop, 1088, kContentBottom - kContentTop};
}

Rect shopStockCard() {
    return {kMargin, 200, 648, 300};
}
Rect shopPrevButton() {
    return {kMargin, 516, 96, 64};
}
Rect shopNextButton() {
    return {kMargin + 648 - 96, 516, 96, 64};
}

constexpr ItemCategory kEquipSlots[] = {ItemCategory::Melee, ItemCategory::Bow, ItemCategory::Armour};

const char* equipSlotLabel(ItemCategory c) {
    switch (c) {
        case ItemCategory::Melee:
            return "Weapon";
        case ItemCategory::Bow:
            return "Bow";
        case ItemCategory::Armour:
            return "Armour";
        default:
            return "";
    }
}

const char* quantityLabel(QuantityKind k) {
    switch (k) {
        case QuantityKind::Durability:
            return "Durability";
        case QuantityKind::Ammo:
            return "Arrows";
        case QuantityKind::Charges:
            return "Charges";
        case QuantityKind::None:
            break;
    }
    return "";
}

const char* refusalText(ActionKind k) {
    switch (k) {
        case ActionKind::UseOrEquip:
            return "That can't be used right now.";
        case ActionKind::Drop:
            return "That can't be dropped.";
        case ActionKind::ToggleHotSlot:
            return "That item is used up, or its requirement is not met.";
        case ActionKind::SelectSpell:
        case ActionKind::CastOrSummon:
            return "The spell can't be cast right now.";
        case ActionKind::SpendSkillPoint:
            return "No skill point could be spent.";
        case ActionKind::ReturnSkillPoint:
            return "No skill point could be returned.";
        case ActionKind::Buy:
            return "You can't buy that.";
        case ActionKind::Sell:
            return "The merchant won't buy that.";
        default:
            return "That is not possible right now.";
    }
}

std::string fmtInt(int32_t v) {
    return std::to_string(v);
}

std::string fmtPair(int32_t a, int32_t b) {
    return std::to_string(a) + " / " + std::to_string(b);
}

const char* primaryLabel(const Snapshot& s, const ItemEntry& it) {
    if (const char* hand = handUseLabel(handUse(s, it)))
        return hand;
    switch (it.category) {
        case ItemCategory::Melee:
        case ItemCategory::Bow:
        case ItemCategory::Armour:
            return it.equipped ? "Equipped" : "Equip";
        case ItemCategory::SpellScroll:
            return "Read";
        case ItemCategory::Consumable:
            return "Use";
        default:
            return "Use";
    }
}

bool primaryEnabled(const Snapshot& s, const ItemEntry& it) {
    // The left-hand weapon is equipped, yet using it again puts it away.
    return !(isEquippable(it.category) && it.equipped) || handUse(s, it) == dualwield::Use::UnequipLeft;
}

} // namespace

namespace design {
Rect gridCell(int cell) {
    return gridCellAt(kInvGridX, kGridY, cell);
}
Rect gridItem(int originCell, int w, int h) {
    return gridItemAt(kInvGridX, kGridY, originCell, w, h);
}
Rect pageTab(int slot, int count) {
    const float w = 176, gap = 10;
    const float total = static_cast<float>(count) * w + static_cast<float>(count - 1) * gap;
    return {(kDesignW - total) * 0.5f + static_cast<float>(slot) * (w + gap), 34, w, 54};
}
Rect filterTab(int f) {
    // Between the trigger glyphs at the grid's edges.
    return {kInvGridX + 40 + static_cast<float>(f) * 148, kContentTop, 142, 46};
}
Rect detailPanel() {
    return {kDetailX, kContentTop, kDesignW - kMargin - kDetailX, kContentBottom - kContentTop};
}
Rect previewArea() {
    return {576, kContentTop, 768, kContentBottom - kContentTop};
}
Rect inventoryPreview() {
    return {kMargin, kContentTop, kInvGridX - 40 - kMargin, kContentBottom - kContentTop};
}
Rect centreColumn() {
    return {kInvGridX - 20, kContentTop, kGridW + 40, kContentBottom - kContentTop};
}
Rect mapImageArea() {
    return mapPanel().inset(24);
}
Rect equipCard(int i) {
    return equipSlotRect(i);
}
} // namespace design

const char* pageLabel(Page p) {
    switch (p) {
        case Page::Shop:
            return "Merchant";
        case Page::Inventory:
            return "Inventory";
        case Page::Character:
            return "Character";
        case Page::Spells:
            return "Spells";
        case Page::Skills:
            return "Skills";
        case Page::Journal:
            return "Journal";
        case Page::Map:
            return "Map";
    }
    return "";
}

int nativeSectionFor(Page p) {
    switch (p) {
        case Page::Inventory:
        case Page::Character:
            return 0;
        case Page::Spells:
            return 1;
        case Page::Skills:
            return 2;
        case Page::Map:
            return 3;
        case Page::Journal:
            return 4;
        case Page::Shop:
            break; // shop sections differ (3 sections); the executor keeps the native one
    }
    return -1;
}

// ---- state machine ----

InventoryScreen::InventoryScreen(ScreenConfig cfg) : cfg_(std::move(cfg)) {}

float InventoryScreen::measure(std::string_view t, FontRole f, float px) const {
    return cfg_.measure ? cfg_.measure(t, f, px) : estimateTextWidth(t, f, px);
}

float InventoryScreen::fontPx(FontRole f) const {
    const Theme& th = cfg_.theme;
    float d = th.sizeBody;
    switch (f) {
        case FontRole::Title:
            d = th.sizeTitle;
            break;
        case FontRole::Heading:
            d = th.sizeHeading;
            break;
        case FontRole::Numeric:
            d = th.sizeNumeric;
            break;
        case FontRole::Small:
            d = th.sizeSmall;
            break;
        case FontRole::Body:
            break;
    }
    return lf_.px(d);
}

std::vector<Page> InventoryScreen::visiblePages(const Snapshot& s) const {
    std::vector<Page> v;
    if (cfg_.includeShopPage && s.shop.active)
        v.push_back(Page::Shop);
    for (Page p : {Page::Inventory, Page::Character, Page::Spells, Page::Skills, Page::Journal, Page::Map})
        v.push_back(p);
    return v;
}

void InventoryScreen::open(const Snapshot& s, Page initial) {
    open_ = true;
    closePending_ = false;
    page_ = (cfg_.includeShopPage && s.shop.active) ? Page::Shop : initial;
    filter_ = CategoryFilter::All;
    focus_ = hover_ = {};
    for (TargetId& t : pageFocus_)
        t = {};
    lastSteer_ = 0;
    steerWait_ = 0;
    lastSpellSteer_ = -1;
    lastMarkerSteer_ = -1;
    pendingSelect_ = false;
    pendingAge_ = 0;
    prefetched_.clear();
    questScroll_ = 0;
    toast_.clear();
    toastLeft_ = 0;
}

void InventoryScreen::close() {
    open_ = false;
    targets_.clear();
    hints_.clear();
    preview_ = {};
}

void InventoryScreen::onResult(const ActionResult& r) {
    if (r.kind == ActionKind::Close) {
        if (closePending_ && !open_ && r.outcome != Outcome::Confirmed)
            open_ = true; // the game kept the screen open: back where it was
        else if (closePending_ && !open_)
            close();
        closePending_ = false;
        return;
    }
    if (r.kind == ActionKind::SelectItem || r.kind == ActionKind::SelectSpell)
        pendingSelect_ = false;
    if (r.outcome != Outcome::Refused || r.kind == ActionKind::SelectItem || r.kind == ActionKind::SwitchSection ||
        r.kind == ActionKind::SelectMarker)
        return;
    toast_ = refusalText(r.kind);
    toastLeft_ = cfg_.toastFrames;
}

const HitTarget* InventoryScreen::findTarget(const TargetId& id) const {
    for (const HitTarget& t : targets_)
        if (t.id == id)
            return &t;
    return nullptr;
}

const ItemEntry* InventoryScreen::focusedItem(const Snapshot& s) const {
    switch (focus_.kind) {
        case TargetKind::GridCell:
            return s.itemAtCell(focus_.index);
        case TargetKind::EquipSlot:
            return s.equippedIn(static_cast<ItemCategory>(focus_.index));
        case TargetKind::ShopStock:
            return s.shop.hasStock ? &s.shop.stock : nullptr;
        default:
            return nullptr;
    }
}

int InventoryScreen::pickDirectional(const std::vector<HitTarget>& t, const Rect& from, int dx, int dy) {
    int best = -1;
    float bestScore = std::numeric_limits<float>::max();
    for (size_t i = 0; i < t.size(); ++i) {
        if (!t[i].navigable)
            continue;
        const Rect& r = t[i].rect;
        if (r == from)
            continue;
        float centerAlong, edgeGap, perpGap, perpCenter;
        if (dx != 0) {
            centerAlong = (r.cx() - from.cx()) * static_cast<float>(dx);
            edgeGap = dx > 0 ? r.x - from.right() : from.x - r.right();
            perpGap = std::max(0.0f, std::max(r.y - from.bottom(), from.y - r.bottom()));
            perpCenter = std::fabs(r.cy() - from.cy());
        } else {
            centerAlong = (r.cy() - from.cy()) * static_cast<float>(dy);
            edgeGap = dy > 0 ? r.y - from.bottom() : from.y - r.bottom();
            perpGap = std::max(0.0f, std::max(r.x - from.right(), from.x - r.right()));
            perpCenter = std::fabs(r.cx() - from.cx());
        }
        if (centerAlong <= 1.0f || edgeGap < -1.0f)
            continue;
        const float score = std::max(edgeGap, 0.0f) + perpGap * 4.0f + perpCenter * 0.25f;
        if (score < bestScore) {
            bestScore = score;
            best = static_cast<int>(i);
        }
    }
    return best;
}

void InventoryScreen::buildTargets(const Snapshot& s) {
    targets_.clear();
    preview_ = {};
    auto add = [&](TargetKind k, int idx, const Rect& design, bool nav = true, bool enabled = true) {
        targets_.push_back({{k, idx}, lf_.map(design), nav, enabled});
    };
    const std::vector<Page> pages = visiblePages(s);
    for (size_t i = 0; i < pages.size(); ++i)
        add(TargetKind::PageTab, static_cast<int>(pages[i]),
            design::pageTab(static_cast<int>(i), static_cast<int>(pages.size())), false);
    if (!s.native.valid)
        return;

    auto addGrid = [&](float ox, float oy) {
        for (int c = 0; c < kCells; ++c) {
            const ItemEntry* it = s.itemAtCell(c);
            if (!it) {
                add(TargetKind::GridCell, c, gridCellAt(ox, oy, c));
            } else if (it->originCell == c) {
                const bool vis = page_ != Page::Inventory || matchesFilter(it->category, filter_);
                add(TargetKind::GridCell, c, gridItemAt(ox, oy, c, it->width, it->height), vis, vis);
            }
        }
    };

    switch (page_) {
        case Page::Inventory:
            for (int f = 0; f < kCategoryFilterCount; ++f)
                add(TargetKind::FilterTab, f, design::filterTab(f), false);
            addGrid(kInvGridX, kGridY);
            preview_ = lf_.map(design::inventoryPreview());
            break;
        case Page::Character: {
            for (int i = 0; i < 3; ++i) { // the card without its pack column, so Right reaches the pack items
                const Rect r = equipSlotRect(i);
                add(TargetKind::EquipSlot, static_cast<int>(kEquipSlots[i]), {r.x, r.y, kAltX - 12, r.h});
            }
            for (int c = 0; c < 3; ++c) {
                const std::vector<const ItemEntry*> alts = alternatives(s, kEquipSlots[c]);
                for (size_t i = 0; i < alts.size() && i < kAltRows; ++i)
                    add(TargetKind::GridCell, alts[i]->originCell, altRow(c, static_cast<int>(i)));
            }
            preview_ = lf_.map(design::inventoryPreview());
            break;
        }
        case Page::Spells:
            for (const SpellEntry& sp : s.spells.entries)
                if (sp.index < 10)
                    add(TargetKind::Spell, sp.index, spellRow(sp.index), sp.knownLevel > 0, sp.knownLevel > 0);
            break;
        case Page::Skills:
            for (size_t i = 0; i < s.skills.entries.size() && i < 3; ++i) {
                const int row = static_cast<int>(i);
                const SkillEntry& sk = s.skills.entries[i];
                add(TargetKind::Skill, row, skillRow(row));
                add(TargetKind::SkillMinus, row, skillMinus(row), false, sk.level > sk.committed);
                add(TargetKind::SkillPlus, row, skillPlus(row), false, s.skills.unspentPoints > 0 && sk.level < sk.max);
            }
            break;
        case Page::Journal: {
            journalOrder_.clear();
            for (int pass = 0; pass < 2; ++pass) {
                bool heading = false;
                for (const QuestEntry& q : s.quests) {
                    if (!q.listed() || q.optional != (pass == 1))
                        continue;
                    if (!heading) {
                        journalOrder_.push_back(pass == 0 ? -1 : -2);
                        heading = true;
                    }
                    journalOrder_.push_back(q.index);
                }
            }
            // keep the focused row visible
            if (focus_.kind == TargetKind::Quest) {
                for (size_t r = 0; r < journalOrder_.size(); ++r)
                    if (journalOrder_[r] == focus_.index) {
                        const int row = static_cast<int>(r);
                        if (row < questScroll_ + 1)
                            questScroll_ = std::max(0, row - 1); // show the heading above the first entry
                        if (row >= questScroll_ + kJournalVisibleRows)
                            questScroll_ = row - kJournalVisibleRows + 1;
                    }
            }
            questScroll_ =
                std::clamp(questScroll_, 0, std::max(0, static_cast<int>(journalOrder_.size()) - kJournalVisibleRows));
            for (int v = 0; v < kJournalVisibleRows; ++v) {
                const size_t r = static_cast<size_t>(questScroll_ + v);
                if (r >= journalOrder_.size())
                    break;
                if (journalOrder_[r] >= 0)
                    add(TargetKind::Quest, journalOrder_[r], journalRow(v));
            }
            break;
        }
        case Page::Map: {
            int row = 0;
            for (const QuestEntry& q : s.quests)
                if (q.marker() && q.enabled && row < 13)
                    add(TargetKind::Marker, q.index, markerRow(row++));
            break;
        }
        case Page::Shop:
            add(TargetKind::ShopStock, 0, shopStockCard(), true, s.shop.hasStock);
            add(TargetKind::ShopPrev, 0, shopPrevButton(), false);
            add(TargetKind::ShopNext, 0, shopNextButton(), false);
            addGrid(kShopGridX, kGridY);
            break;
    }
}

void InventoryScreen::ensureFocus(const Snapshot& s) {
    if (focus_.kind != TargetKind::None) {
        const HitTarget* t = findTarget(focus_);
        if (t && t->navigable)
            return;
    }
    TargetId want;
    switch (page_) {
        case Page::Inventory:
        case Page::Shop:
            for (const ItemEntry& it : s.items)
                if (page_ == Page::Shop || matchesFilter(it.category, filter_)) {
                    want = {TargetKind::GridCell, it.originCell};
                    break;
                }
            if (page_ == Page::Shop && s.shop.hasStock)
                want = {TargetKind::ShopStock, 0};
            break;
        case Page::Spells:
            if (s.spells.selected >= 0)
                want = {TargetKind::Spell, s.spells.selected};
            break;
        default:
            break;
    }
    if (want.kind != TargetKind::None) {
        const HitTarget* t = findTarget(want);
        if (t && t->navigable) {
            focus_ = want;
            return;
        }
    }
    focus_ = {};
    for (const HitTarget& t : targets_)
        if (t.navigable) {
            focus_ = t.id;
            return;
        }
}

void InventoryScreen::buildHints(const Snapshot& s) {
    hints_.clear();
    if (!s.native.valid) {
        hints_.push_back({Prompt::Back, "Close"});
        return;
    }
    const ItemEntry* it = focusedItem(s);
    switch (page_) {
        case Page::Inventory:
            if (it) {
                hints_.push_back({Prompt::Confirm, primaryLabel(s, *it), primaryEnabled(s, *it)});
                hints_.push_back({Prompt::Secondary, "Drop", it->dropAllowed});
                hints_.push_back({Prompt::Tertiary, it->hotSlot ? "Remove Quick Slot" : "Quick Slot", true});
            }
            hints_.push_back({Prompt::FilterNext, "Filter"});
            break;
        case Page::Character:
            if (focus_.kind == TargetKind::EquipSlot)
                hints_.push_back({Prompt::Confirm, "Change"});
            else if (it)
                hints_.push_back({Prompt::Confirm, primaryLabel(s, *it), primaryEnabled(s, *it)});
            break;
        case Page::Spells:
            if (focus_.kind == TargetKind::Spell) {
                const bool known = focus_.index >= 0 && focus_.index < static_cast<int>(s.spells.entries.size()) &&
                                   s.spells.entries[static_cast<size_t>(focus_.index)].knownLevel > 0;
                const bool free = !s.spells.summonBusy();
                hints_.push_back({Prompt::Confirm, "Summon", known && free});
            }
            break;
        case Page::Skills:
            if (focus_.kind == TargetKind::Skill) {
                hints_.push_back({Prompt::Confirm, "Spend Point", s.skills.unspentPoints > 0});
                const bool back = focus_.index >= 0 && focus_.index < static_cast<int>(s.skills.entries.size()) &&
                                  s.skills.entries[static_cast<size_t>(focus_.index)].level >
                                      s.skills.entries[static_cast<size_t>(focus_.index)].committed;
                hints_.push_back({Prompt::Secondary, "Return Point", back});
            }
            break;
        case Page::Shop:
            if (focus_.kind == TargetKind::ShopStock)
                hints_.push_back({Prompt::Confirm, "Buy", s.shop.hasStock});
            else if (it)
                hints_.push_back({Prompt::Confirm, "Sell"});
            hints_.push_back({Prompt::FilterNext, "Browse"});
            break;
        case Page::Journal:
        case Page::Map:
            break;
    }
    hints_.push_back({Prompt::Back, "Close"});

    // Footer hint targets, right aligned.
    float x = kDesignW - kMargin;
    const float glyph = 40, gap = 36;
    for (size_t i = hints_.size(); i-- > 0;) {
        const float labelW = measure(hints_[i].label, FontRole::Body, fontPx(FontRole::Body)) / lf_.scale;
        const float w = glyph + 10 + labelW;
        x -= w;
        targets_.push_back({{TargetKind::Hint, static_cast<int>(hints_[i].prompt)},
                            lf_.map({x, 1012, w, 44}),
                            false,
                            hints_[i].enabled});
        x -= gap;
    }
}

void InventoryScreen::setPage(Page p, const Snapshot& s, FrameOutput& out) {
    if (p == page_)
        return;
    pageFocus_[static_cast<int>(page_)] = focus_;
    page_ = p;
    focus_ = pageFocus_[static_cast<int>(p)];
    hover_ = {};
    const int section = nativeSectionFor(p);
    if (section >= 0 && section != s.native.section)
        out.actions.push_back({0, ActionKind::SwitchSection, section, 0});
}

std::vector<const ItemEntry*> InventoryScreen::alternatives(const Snapshot& s, ItemCategory slot) const {
    std::vector<const ItemEntry*> v;
    for (const ItemEntry& it : s.items)
        if (it.category == slot && !it.equipped)
            v.push_back(&it);
    return v;
}

// Keeps the native selection on the focused item (its details are the native ones) and fetches, once each, the details
// of the gear the pages compare against. One SelectItem / SelectSpell at a time: the queue coalesces them.
void InventoryScreen::steerSelection(const Snapshot& s, FrameOutput& out) {
    if (page_ == Page::Map) {
        // The game's map selection follows the focused location, so its own caption (and the selection state the
        // native page keeps) name the same place as the list.
        if (focus_.kind != TargetKind::Marker || focus_.index == lastMarkerSteer_ ||
            s.native.section != nativeSectionFor(Page::Map) || s.native.mapSelection < 0)
            return;
        lastMarkerSteer_ = focus_.index;
        if (focus_.index != s.native.mapSelection)
            out.actions.push_back({0, ActionKind::SelectMarker, focus_.index, 0}); // the queue keeps the newest
        return;
    }
    lastMarkerSteer_ = -1;
    if (pendingSelect_ && ++pendingAge_ < 120)
        return;
    pendingSelect_ = false;
    auto send = [&](ActionKind k, int target, uint32_t handle) {
        out.actions.push_back({0, k, target, handle});
        pendingSelect_ = true;
        pendingAge_ = 0;
    };
    if (page_ == Page::Spells) {
        if (focus_.kind != TargetKind::Spell || focus_.index == s.spells.selected || s.spells.summonBusy() ||
            s.native.section != nativeSectionFor(Page::Spells) || focus_.index == lastSpellSteer_)
            return;
        lastSpellSteer_ = focus_.index;
        send(ActionKind::SelectSpell, focus_.index, 0);
        return;
    }
    lastSpellSteer_ = -1;
    if (page_ != Page::Inventory && page_ != Page::Character && page_ != Page::Shop)
        return;
    const ItemEntry* it = focusedItem(s);
    if (it && it->slot >= 0 && it->slot < kCells && !it->detailsCurrent) {
        // The native details builder can come up empty (its labels still loading while the screen opens): ask again
        // after a while if the item's details did not arrive.
        const bool again = it->handle == lastSteer_ && it->details.size() <= 1 && ++steerWait_ >= 30;
        if (it->handle != lastSteer_ || again) {
            lastSteer_ = it->handle;
            steerWait_ = 0;
            send(ActionKind::SelectItem, it->slot, it->handle);
            return;
        }
    }
    if (page_ == Page::Shop)
        return;
    for (const ItemEntry& e : s.items) {
        const bool wanted = e.equipped || (page_ == Page::Character && isEquippable(e.category));
        if (!wanted || e.details.size() > 1 || e.detailsCurrent || e.slot < 0 || e.slot >= kCells ||
            std::find(prefetched_.begin(), prefetched_.end(), e.handle) != prefetched_.end())
            continue;
        prefetched_.push_back(e.handle);
        lastSteer_ = e.handle;
        steerWait_ = 0;
        send(ActionKind::SelectItem, e.slot, e.handle);
        return;
    }
}

void InventoryScreen::activate(const TargetId& t, const Snapshot& s, FrameOutput& out, bool fromMouse) {
    auto push = [&](ActionKind k, int target, uint32_t handle = 0) { out.actions.push_back({0, k, target, handle}); };
    switch (t.kind) {
        case TargetKind::PageTab:
            setPage(static_cast<Page>(t.index), s, out);
            break;
        case TargetKind::FilterTab:
            if (filter_ != static_cast<CategoryFilter>(t.index)) {
                filter_ = static_cast<CategoryFilter>(t.index);
                focus_ = {};
            }
            break;
        case TargetKind::GridCell:
            if (const ItemEntry* it = s.itemAtCell(t.index)) {
                if (page_ == Page::Shop)
                    push(ActionKind::Sell, it->slot, it->handle);
                else if (primaryEnabled(s, *it))
                    push(ActionKind::UseOrEquip, it->slot, it->handle);
            }
            break;
        case TargetKind::EquipSlot: {
            // Jump to the inventory, filtered to this kind of gear, on the equipped item.
            const auto cat = static_cast<ItemCategory>(t.index);
            setPage(Page::Inventory, s, out);
            filter_ = cat == ItemCategory::Armour ? CategoryFilter::Armour : CategoryFilter::Weapons;
            if (const ItemEntry* eq = s.equippedIn(cat))
                focus_ = {TargetKind::GridCell, eq->originCell};
            break;
        }
        case TargetKind::Spell:
            // Focus already selects the spell natively (like the native arrows); Confirm summons it.
            if (t.index >= 0 && t.index < static_cast<int>(s.spells.entries.size()) &&
                s.spells.entries[static_cast<size_t>(t.index)].knownLevel > 0 && !s.spells.summonBusy())
                push(ActionKind::CastOrSummon, t.index);
            break;
        case TargetKind::Skill:
        case TargetKind::SkillPlus:
            if (s.skills.unspentPoints > 0)
                push(ActionKind::SpendSkillPoint, t.index);
            break;
        case TargetKind::SkillMinus:
            push(ActionKind::ReturnSkillPoint, t.index);
            break;
        case TargetKind::ShopStock:
            if (s.shop.hasStock)
                push(ActionKind::Buy, s.shop.selection, s.shop.stock.handle);
            break;
        case TargetKind::ShopPrev:
            push(ActionKind::ShopBrowse, -1);
            break;
        case TargetKind::ShopNext:
            push(ActionKind::ShopBrowse, 1);
            break;
        case TargetKind::Hint:
            press(static_cast<Prompt>(t.index), s, out);
            break;
        case TargetKind::Quest:
        case TargetKind::Marker:
        case TargetKind::MenuRow:
        case TargetKind::None:
            break;
    }
    (void)fromMouse;
}

void InventoryScreen::press(Prompt p, const Snapshot& s, FrameOutput& out) {
    const ItemEntry* it = focusedItem(s);
    auto push = [&](ActionKind k, int target, uint32_t handle = 0) { out.actions.push_back({0, k, target, handle}); };
    switch (p) {
        case Prompt::Confirm:
            activate(focus_, s, out, false);
            break;
        case Prompt::Back:
            out.actions.push_back({0, ActionKind::Close, -1, 0});
            out.wantsClose = true;
            break;
        case Prompt::Secondary:
            if (page_ == Page::Inventory && it && it->dropAllowed)
                push(ActionKind::Drop, it->slot, it->handle);
            else if (page_ == Page::Skills && focus_.kind == TargetKind::Skill)
                push(ActionKind::ReturnSkillPoint, focus_.index);
            break;
        case Prompt::Tertiary:
            if (page_ == Page::Inventory && it && !s.shop.active)
                push(ActionKind::ToggleHotSlot, it->slot, it->handle);
            break;
        case Prompt::PagePrev:
        case Prompt::PageNext: {
            const std::vector<Page> pages = visiblePages(s);
            auto cur = std::find(pages.begin(), pages.end(), page_);
            int i = cur == pages.end() ? 0 : static_cast<int>(cur - pages.begin());
            const int n = static_cast<int>(pages.size());
            i = (i + (p == Prompt::PageNext ? 1 : n - 1)) % n;
            setPage(pages[static_cast<size_t>(i)], s, out);
            break;
        }
        case Prompt::FilterPrev:
        case Prompt::FilterNext: {
            const int dir = p == Prompt::FilterNext ? 1 : -1;
            if (page_ == Page::Inventory) {
                filter_ = static_cast<CategoryFilter>((static_cast<int>(filter_) + dir + kCategoryFilterCount) %
                                                      kCategoryFilterCount);
                focus_ = {}; // re-focus the first matching item
            } else if (page_ == Page::Shop) {
                push(ActionKind::ShopBrowse, dir);
            }
            break;
        }
        case Prompt::Navigate:
        case Prompt::Adjust:
            break;
    }
}

void InventoryScreen::applyInput(const Snapshot& s, const UiInput& in, FrameOutput& out) {
    // Pointer: hover focuses navigable targets (modern PC behaviour); clicks act on what is under the pointer.
    hover_ = {};
    if (in.pointerValid) {
        for (size_t i = targets_.size(); i-- > 0;)
            if (targets_[i].rect.contains(in.pointerX, in.pointerY)) {
                hover_ = targets_[i].id;
                if (in.pointerMoved && targets_[i].navigable)
                    focus_ = targets_[i].id;
                break;
            }
    }
    if (in.rightClick) {
        press(Prompt::Back, s, out);
        return;
    }
    if (in.leftClick && hover_.kind != TargetKind::None) {
        const HitTarget* t = findTarget(hover_);
        if (t && t->enabled) {
            const TargetKind k = hover_.kind;
            const bool needsDouble = k == TargetKind::GridCell || k == TargetKind::ShopStock ||
                                     k == TargetKind::EquipSlot || k == TargetKind::Spell;
            if (t->navigable)
                focus_ = hover_;
            if (!needsDouble || in.doubleClick)
                activate(hover_, s, out, true);
        }
    }
    // The wheel walks the journal entries (the list scrolls to keep the focus visible).
    std::vector<UiKey> keys = in.keys;
    if (page_ == Page::Journal && in.wheel != 0)
        keys.push_back(in.wheel > 0 ? UiKey::Up : UiKey::Down);
    for (UiKey k : keys) {
        if (!open_)
            break;
        int dx = 0, dy = 0;
        switch (k) {
            case UiKey::Up:
                dy = -1;
                break;
            case UiKey::Down:
                dy = 1;
                break;
            case UiKey::Left:
                dx = -1;
                break;
            case UiKey::Right:
                dx = 1;
                break;
            case UiKey::Confirm:
                press(Prompt::Confirm, s, out);
                break;
            case UiKey::Back:
            case UiKey::Close:
                press(Prompt::Back, s, out);
                break;
            case UiKey::Secondary:
                press(Prompt::Secondary, s, out);
                break;
            case UiKey::Tertiary:
                press(Prompt::Tertiary, s, out);
                break;
            case UiKey::PagePrev:
                press(Prompt::PagePrev, s, out);
                break;
            case UiKey::PageNext:
                press(Prompt::PageNext, s, out);
                break;
            case UiKey::FilterPrev:
                press(Prompt::FilterPrev, s, out);
                break;
            case UiKey::FilterNext:
                press(Prompt::FilterNext, s, out);
                break;
        }
        if (out.wantsClose)
            break;
        if (dx == 0 && dy == 0) {
            // page / filter changes rebuild the targets before the next key
            buildTargets(s);
            ensureFocus(s);
            continue;
        }
        // Skills: Left / Right return / spend a point on the focused row, like the native page.
        if (page_ == Page::Skills && focus_.kind == TargetKind::Skill && dx != 0) {
            press(dx > 0 ? Prompt::Confirm : Prompt::Secondary, s, out);
            continue;
        }
        const HitTarget* cur = findTarget(focus_);
        if (!cur) {
            ensureFocus(s);
            continue;
        }
        const int next = pickDirectional(targets_, cur->rect, dx, dy);
        if (next >= 0)
            focus_ = targets_[static_cast<size_t>(next)].id;
    }
}

FrameOutput InventoryScreen::frame(const Snapshot& s, const UiInput& in, const Viewport& vp) {
    FrameOutput out;
    if (!open_)
        return out;
    lf_ = computeFrame(vp);
    if (page_ == Page::Shop && !s.shop.active)
        page_ = Page::Inventory;

    buildTargets(s);
    ensureFocus(s);
    buildHints(s);
    const Page pageBefore = page_;
    const CategoryFilter filterBefore = filter_;
    const TargetId focusBefore = focus_;
    applyInput(s, in, out);
    if (page_ != pageBefore || filter_ != filterBefore)
        out.sound = UiSound::Page;
    else if (!(focus_ == focusBefore) && focusBefore.kind != TargetKind::None)
        out.sound = UiSound::Move;
    if (out.wantsClose) {
        // Closed for input; the page, the preview rectangle and the map / grid placement stay until the game's result
        // (the glue keeps the native screen arranged under the last picture through the close animation).
        open_ = false;
        closePending_ = true;
        targets_.clear();
        hints_.clear();
        return out;
    }
    buildTargets(s);
    ensureFocus(s);
    buildHints(s);
    if (s.native.valid)
        steerSelection(s, out);
    if (toastLeft_ > 0 && --toastLeft_ == 0)
        toast_.clear();
    draw(s, out.draw);
    return out;
}

// ---- drawing ----

void InventoryScreen::textD(DrawList& d, const Rect& design, const std::string& s, FontRole f, Color c,
                            TextAlign a) const {
    if (s.empty())
        return;
    const Rect r = lf_.map(design);
    d.text(r, ellipsize(s, r.w, f, fontPx(f), cfg_.measure), f, fontPx(f), c, a);
}

void InventoryScreen::drawFocus(const Rect& r, DrawList& d) const {
    detail::drawFocus(d, cfg_.theme, lf_, r);
}

void InventoryScreen::drawBar(const Rect& r, int32_t cur, int32_t max, Color fill, const std::string& label,
                              DrawList& d) const {
    const Theme& th = cfg_.theme;
    const Rect bar = lf_.map(r);
    d.panel(bar, PanelStyle::Inset, th.inset);
    if (max > 0) {
        const float f = std::clamp(static_cast<float>(cur) / static_cast<float>(max), 0.0f, 1.0f);
        const Rect in = bar.inset(lf_.px(3));
        d.gradient({in.x, in.y, in.w * f, in.h}, fill, withAlpha(fill, 170));
    }
    if (!label.empty()) {
        const Rect t = {r.x + 12, r.y, r.w - 24, r.h};
        textD(d, t, label, FontRole::Small, th.text, TextAlign::Left);
        textD(d, t, max > 0 ? fmtPair(cur, max) : fmtInt(cur), FontRole::Small, th.text, TextAlign::Right);
    }
}

void InventoryScreen::draw(const Snapshot& s, DrawList& d) const {
    const Theme& th = cfg_.theme;
    const Rect full{lf_.view.x, lf_.view.y, lf_.view.w, lf_.view.h};
    d.panel(full, PanelStyle::Backdrop, th.backdrop);
    drawHeader(s, d);
    if (!s.native.valid) {
        const char* msg = s.native.onDragon ? "The inventory can't be opened while riding Arokh."
                                            : "The inventory is not available right now.";
        textD(d, {0, 500, kDesignW, 60}, msg, FontRole::Heading, th.textMuted, TextAlign::Center);
    } else {
        switch (page_) {
            case Page::Inventory:
                drawInventory(s, d);
                break;
            case Page::Character:
                drawCharacter(s, d);
                break;
            case Page::Spells:
                drawSpells(s, d);
                break;
            case Page::Skills:
                drawSkills(s, d);
                break;
            case Page::Journal:
                drawJournal(s, d);
                break;
            case Page::Map:
                drawMap(s, d);
                break;
            case Page::Shop:
                drawShop(s, d);
                break;
        }
    }
    drawFooter(d);
    if (!toast_.empty()) {
        const float w = std::min(1100.0f, measure(toast_, FontRole::Body, fontPx(FontRole::Body)) / lf_.scale + 80);
        const Rect r{(kDesignW - w) * 0.5f, 930, w, 56};
        d.panel(lf_.map(r), PanelStyle::Card, th.panelRaised);
        d.outline(lf_.map(r), th.bad, lf_.px(2), lf_.px(6));
        textD(d, r, toast_, FontRole::Body, th.text, TextAlign::Center);
    }
}

void InventoryScreen::drawHeader(const Snapshot& s, DrawList& d) const {
    const Theme& th = cfg_.theme;
    const Rect band = lf_.band(0, 120);
    d.gradient(band, withAlpha(th.panel, 240), withAlpha(th.panel, 0));
    detail::drawOrnateRule(d, th, lf_, band.x, band.right(), band.y + lf_.px(112), true);

    // Page tabs (the native section icon and the name) with the shoulder button glyphs either side.
    const std::vector<Page> pages = visiblePages(s);
    const int n = static_cast<int>(pages.size());
    for (int i = 0; i < n; ++i) {
        const Rect r = design::pageTab(i, n);
        const Page pg = pages[static_cast<size_t>(i)];
        const bool cur = pg == page_;
        const bool hov = hover_.kind == TargetKind::PageTab && hover_.index == static_cast<int>(pg);
        if (cur) {
            d.panel(lf_.map(r), PanelStyle::Card, th.panelRaised);
            d.rect(lf_.map({r.x + 16, r.bottom() - 3, r.w - 32, 2}), th.gilt);
        } else if (hov) {
            d.rect(lf_.map(r), withAlpha(th.panelRaised, 140), lf_.px(2));
        }
        const std::string label = pageLabel(pg);
        constexpr float kIcon = 34, kIconGap = 8;
        const float lw = measure(label, FontRole::Body, fontPx(FontRole::Body)) / std::max(lf_.scale, 1e-6f);
        const float x0 = r.cx() - (kIcon + kIconGap + lw) * 0.5f;
        d.image(lf_.map({x0, r.cy() - kIcon * 0.5f, kIcon, kIcon}), {ImageKind::PageIcon, uint32_t(pg)},
                cur ? 0xffffffffu : (hov ? withAlpha(rgba(235, 225, 210), 235) : rgba(180, 165, 150, 175)));
        textD(d, {x0 + kIcon + kIconGap, r.y, lw + 8, r.h}, label, FontRole::Body,
              cur ? th.heading : (hov ? th.text : th.textMuted), TextAlign::Left);
    }
    if (n > 0) {
        const Rect first = design::pageTab(0, n), last = design::pageTab(n - 1, n);
        d.image(lf_.map({first.x - 56, first.y + 7, 40, 40}), {ImageKind::Prompt, uint32_t(Prompt::PagePrev)});
        d.image(lf_.map({last.right() + 16, last.y + 7, 40, 40}), {ImageKind::Prompt, uint32_t(Prompt::PageNext)});
    }

    // Gold, top right.
    const Rect gold{kDesignW - kMargin - 300, 36, 300, 48};
    d.imageOr(lf_.map({gold.right() - 44, gold.y, 48, 48}), {ImageKind::UiIcon, uint32_t(UiIconId::GoldBag)},
              {ImageKind::SkinOrb, 0}, 0xffffffffu, th.xp);
    textD(d, {gold.x, gold.y, gold.w - 52, gold.h}, fmtInt(s.stats.gold), FontRole::Numeric, th.heading,
          TextAlign::Right);
    textD(d, {gold.x, gold.y + 40, gold.w - 52, 28}, "Gold", FontRole::Small, th.textMuted, TextAlign::Right);
}

void InventoryScreen::drawFooter(DrawList& d) const {
    const Theme& th = cfg_.theme;
    const Rect band = lf_.band(992, 88);
    d.gradient(band, withAlpha(th.panel, 0), withAlpha(th.panel, 235));
    for (size_t i = 0; i < hints_.size(); ++i) {
        const HitTarget* t = nullptr;
        for (const HitTarget& h : targets_)
            if (h.id.kind == TargetKind::Hint && h.id.index == static_cast<int>(hints_[i].prompt))
                t = &h;
        if (!t)
            continue;
        const Rect r = t->rect;
        const bool hov = hover_ == t->id && t->enabled;
        const Color c = !hints_[i].enabled ? th.textDisabled : (hov ? th.heading : th.text);
        d.image({r.x, r.y + lf_.px(2), lf_.px(40), lf_.px(40)}, {ImageKind::Prompt, uint32_t(hints_[i].prompt)},
                hints_[i].enabled ? 0xffffffffu : withAlpha(0xffffffffu, 90));
        d.text({r.x + lf_.px(50), r.y, r.w - lf_.px(50), r.h}, hints_[i].label, FontRole::Body, fontPx(FontRole::Body),
               c, TextAlign::Left);
    }
}

void InventoryScreen::drawGrid(const Snapshot& s, DrawList& d, bool dimFiltered) const {
    const Theme& th = cfg_.theme;
    const float ox = page_ == Page::Shop ? kShopGridX : kInvGridX;
    // Red-brown cells on a bronze lattice with a gilt edge (Order of the Flame's inventory grid).
    detail::drawLattice(d, th, lf_, lf_.map({ox, kGridY, kGridW, 3 * kCell + 2 * kCellGap}), lf_.px(kCellGap));
    for (int c = 0; c < kCells; ++c)
        d.panel(lf_.map(gridCellAt(ox, kGridY, c)), PanelStyle::Inset, th.inset);
    // The selection's glow and wash go under the item, its red trace over it.
    const HitTarget* focusCell = focus_.kind == TargetKind::GridCell ? findTarget(focus_) : nullptr;
    if (focusCell) {
        const Rect& r = focusCell->rect;
        const float g = lf_.px(4);
        d.rect({r.x - g, r.y - g, r.w + 2 * g, r.h + 2 * g}, th.emberGlow, lf_.px(6));
        d.panel(r, PanelStyle::Card, th.panelRaised);
        d.panel(r, PanelStyle::Focus, th.select);
    }
    for (const ItemEntry& it : s.items) {
        const Rect r = lf_.map(gridItemAt(ox, kGridY, it.originCell, it.width, it.height));
        const bool vis = !dimFiltered || matchesFilter(it.category, filter_);
        const uint8_t alpha = vis ? 255 : 60;
        const bool focused = focusCell && focusCell->id.index == it.originCell;
        if (!focused)
            d.panel(r, PanelStyle::Card, withAlpha(th.panelRaised, vis ? 236 : 120));
        d.imageOr(r.inset(lf_.px(4)), {ImageKind::ItemIcon, uint32_t(it.slot)},
                  {ImageKind::CategoryIcon, uint32_t(it.category)}, withAlpha(0xffffffffu, alpha),
                  withAlpha(0xffffffffu, vis ? 90 : 30));
        if (it.equipped) {
            d.outline(r.inset(lf_.px(2)), withAlpha(th.brass, alpha), lf_.px(2), lf_.px(4));
            // E, or the hand while hands are named (dual wielding).
            const char* mark = !s.handsShown() || it.hand == ItemEntry::Hand::None ? "E"
                               : it.hand == ItemEntry::Hand::Left                  ? "L"
                                                                                   : "R";
            d.text({r.x + lf_.px(8), r.y + lf_.px(4), r.w, lf_.px(24)}, mark, FontRole::Small, fontPx(FontRole::Small),
                   withAlpha(th.heading, alpha));
        }
        if (const int qn = quickSlotNumber(s, it)) { // the quick-slot key, as on the I strip
            const Rect pill{r.right() - lf_.px(30), r.y + lf_.px(5), lf_.px(25), lf_.px(25)};
            d.rect(pill, withAlpha(th.ember, uint8_t(220 * alpha / 255)), lf_.px(12.5f));
            d.text(pill, fmtInt(qn), FontRole::Small, fontPx(FontRole::Small), withAlpha(th.text, alpha),
                   TextAlign::Center);
        }
        const QuantityKind qk = quantityKindFor(it.category);
        if (it.quantityMax > 0 && qk == QuantityKind::Durability) {
            const float f = std::clamp(static_cast<float>(it.quantity) / static_cast<float>(it.quantityMax), 0.f, 1.f);
            const Rect bar{r.x + lf_.px(10), r.bottom() - lf_.px(12), r.w - lf_.px(20), lf_.px(5)};
            d.rect(bar, withAlpha(th.inset, alpha));
            d.rect({bar.x, bar.y, bar.w * f, bar.h}, withAlpha(f < 0.25f ? th.bad : th.good, alpha));
        } else if (it.quantityMax > 0 && qk != QuantityKind::None) {
            d.text({r.x, r.bottom() - lf_.px(30), r.w - lf_.px(10), lf_.px(26)}, fmtInt(it.quantity), FontRole::Small,
                   fontPx(FontRole::Small), withAlpha(th.text, alpha), TextAlign::Right);
        }
    }
    if (focusCell)
        d.outline(focusCell->rect, th.select, lf_.px(1.75f), lf_.px(2));
}

void InventoryScreen::drawItemDetail(const Snapshot& s, const ItemEntry* it, const Rect& panel, DrawList& d,
                                     bool shopPrices) const {
    const Theme& th = cfg_.theme;
    d.panel(lf_.map(panel), PanelStyle::Main, th.panel);
    const float x = panel.x + 32, w = panel.w - 64;
    float y = panel.y + 28;
    if (!it) {
        textD(d, {x, y, w, 40}, "Empty slot", FontRole::Body, th.textDisabled);
        return;
    }
    // The item, large, then its name. (Wide panels, the shop's, keep the icon left of the title.)
    const bool wide = panel.w > 640;
    const float iconH = wide ? 132.0f : 200.0f;
    const Rect icon = wide ? Rect{x, y, iconH, iconH} : Rect{x, y, w, iconH};
    d.panel(lf_.map(icon), PanelStyle::Inset, th.inset);
    const float side = std::min(icon.w, icon.h) - 12;
    d.imageOr(lf_.map({icon.cx() - side * 0.5f, icon.cy() - side * 0.5f, side, side}),
              {ImageKind::ItemIcon, uint32_t(it->slot)}, {ImageKind::CategoryIcon, uint32_t(it->category)}, 0xffffffffu,
              withAlpha(0xffffffffu, 90));
    const float tx = wide ? x + iconH + 20 : x, tw = wide ? w - iconH - 20 : w;
    float ty = wide ? y + 4 : y + iconH + 20;
    textD(d, {tx, ty, tw, 44}, it->name.empty() ? std::string("...") : it->name, FontRole::Heading, th.heading);
    ty += 48;
    std::string sub = categoryLabel(it->category);
    if (it->equipped) {
        sub += "  \xC2\xB7  Equipped"; // U+00B7 middle dot
        if (s.handsShown() && it->hand != ItemEntry::Hand::None)
            sub += std::string(", ") + handName(it->hand);
    }
    if (it->hotSlot)
        sub += "  \xC2\xB7  Quick Slot";
    textD(d, {tx, ty, tw, 30}, sub, FontRole::Small, it->equipped ? th.ember : th.textMuted);
    y = wide ? y + iconH + 20 : ty + 44;
    const Rect rule = lf_.map({x, y, w, 0});
    d.line({rule.x, rule.y}, {rule.right(), rule.y}, withAlpha(th.brassDim, 200), lf_.px(1.5f));
    y += 18;

    // Stat lines as label / value rows.
    if (it->details.size() <= 1 && !it->detailsCurrent) {
        textD(d, {x, y, w, 34}, "...", FontRole::Body, th.textMuted);
        y += 40;
    }
    for (size_t i = 1; i < it->details.size(); ++i) {
        std::string label, value;
        const std::string& line = it->details[i];
        const bool price = line.find("Price") != std::string::npos || line.find("Sell") != std::string::npos ||
                           line.find("Repair") != std::string::npos;
        if (price && !shopPrices)
            continue;
        const QuantityKind lineQk = quantityKindFor(it->category);
        if (it->quantityMax > 0 && lineQk != QuantityKind::None && splitStatLine(line, label, value) &&
            label == quantityLabel(lineQk))
            continue; // shown by the bar below
        if (splitStatLine(line, label, value)) {
            textD(d, {x, y, w, 36}, label, FontRole::Body, th.textMuted);
            textD(d, {x, y, w, 36}, value, FontRole::Body, th.text, TextAlign::Right);
        } else {
            textD(d, {x, y, w, 36}, line, FontRole::Body, th.brass);
        }
        y += 40;
    }
    const QuantityKind qk = quantityKindFor(it->category);
    if (it->quantityMax > 0 && qk != QuantityKind::None) {
        y += 8;
        drawBar({x, y, w, 34}, it->quantity, it->quantityMax,
                qk == QuantityKind::Durability ? th.good : (qk == QuantityKind::Ammo ? th.xp : th.mana),
                quantityLabel(qk), d);
        y += 50;
    }
    if (it->detailsCurrent && !it->requirementMet) {
        textD(d, {x, y, w, 34}, "Requirement not met", FontRole::Body, th.bad);
        y += 44;
    }

    // Comparison against the equipped item of the same kind.
    const ItemEntry* eq = cfg_.showComparison && !it->equipped ? s.equippedFor(*it) : nullptr;
    if (eq) {
        y += 16;
        const Rect box{x - 8, y, w + 16, panel.bottom() - y - 24};
        if (box.h > 120) {
            d.panel(lf_.map(box), PanelStyle::Inset, withAlpha(th.inset, 200));
            float cy = y + 14;
            textD(d, {x + 8, cy, w - 16, 30}, "Compared with equipped", FontRole::Small, th.textMuted);
            cy += 32;
            textD(d, {x + 8, cy, w - 16, 34}, eq->name.empty() ? std::string("...") : eq->name, FontRole::Body,
                  th.heading);
            cy += 44;
            for (const StatDelta& sd : compareItems(*it, *eq)) {
                if (cy + 34 > box.bottom() - 8)
                    break;
                const Color c = sd.better > 0 ? th.good : (sd.better < 0 ? th.bad : th.text);
                textD(d, {x + 8, cy, w * 0.5f, 34}, sd.label, FontRole::Body, th.textMuted);
                const std::string v = (sd.candidate.empty() ? std::string("-") : sd.candidate);
                textD(d, {x + w * 0.5f, cy, w * 0.25f, 34}, v, FontRole::Body, c, TextAlign::Right);
                textD(d, {x + w * 0.75f, cy, w * 0.25f - 16, 34}, sd.equipped.empty() ? std::string("-") : sd.equipped,
                      FontRole::Body, th.textMuted, TextAlign::Right);
                cy += 38;
            }
        }
    }
}

void InventoryScreen::drawInventory(const Snapshot& s, DrawList& d) const {
    const Theme& th = cfg_.theme;

    // Rynn, left: the live world shows through (the glue centres her in this rectangle); a floor shade grounds her.
    const Rect pv = design::inventoryPreview();
    d.worldView(lf_.map(pv));
    d.gradient(lf_.map({pv.x, pv.bottom() - 260, pv.w, 260}), withAlpha(th.backdrop, 0), withAlpha(th.backdrop, 150));
    drawNameplate(s, pv, d);

    // Filter tabs over the grid.
    for (int f = 0; f < kCategoryFilterCount; ++f) {
        const Rect r = design::filterTab(f);
        const bool cur = static_cast<int>(filter_) == f;
        const bool hov = hover_.kind == TargetKind::FilterTab && hover_.index == f;
        d.panel(lf_.map(r), PanelStyle::Card, cur ? th.panelRaised : withAlpha(th.panel, hov ? 210 : 140));
        if (cur)
            d.rect(lf_.map({r.x + 18, r.bottom() - 3, r.w - 36, 2}), th.gilt);
        textD(d, r, filterLabel(static_cast<CategoryFilter>(f)), FontRole::Small,
              cur ? th.heading : (hov ? th.text : th.textMuted), TextAlign::Center);
    }
    {
        const Rect first = design::filterTab(0), last = design::filterTab(kCategoryFilterCount - 1);
        d.image(lf_.map({first.x - 40, first.y + 7, 32, 32}), {ImageKind::Prompt, uint32_t(Prompt::FilterPrev)});
        d.image(lf_.map({last.right() + 6, last.y + 7, 32, 32}), {ImageKind::Prompt, uint32_t(Prompt::FilterNext)});
    }

    // The grid on a framed panel.
    const Rect gridPanel{kInvGridX - 20, kGridY - 20, kGridW + 40, 3 * kCell + 2 * kCellGap + 40};
    drawGrid(s, d, true);

    // Equipped gear under the grid.
    float y = gridPanel.bottom() + 26;
    detail::drawLabel(d, th, lf_, {kInvGridX, y, 400, 30}, "Equipped", th.brass, TextAlign::Left, 0);
    y += 36;
    // While hands are named (dual wielding) the weapon card splits into the right and the left hand.
    struct Card {
        ItemCategory cat;
        const ItemEntry* eq;
        const char* label;
    };
    std::vector<Card> cards;
    for (const ItemCategory c : kEquipSlots) {
        if (c == ItemCategory::Melee && s.handsShown()) {
            cards.push_back({c, s.inHand(ItemEntry::Hand::Right), handName(ItemEntry::Hand::Right)});
            cards.push_back({c, s.inHand(ItemEntry::Hand::Left), handName(ItemEntry::Hand::Left)});
        } else {
            cards.push_back({c, s.equippedIn(c), equipSlotLabel(c)});
        }
    }
    const float n = static_cast<float>(cards.size());
    const float cardW = (kGridW - (n - 1) * 12) / n;
    for (size_t i = 0; i < cards.size(); ++i) {
        const Rect r{kInvGridX + static_cast<float>(i) * (cardW + 12), y, cardW, 96};
        d.panel(lf_.map(r), PanelStyle::Card, th.panel);
        const ItemEntry* eq = cards[i].eq;
        const Rect icon{r.x + 8, r.y + 8, 80, 80};
        d.panel(lf_.map(icon), PanelStyle::Inset, th.inset);
        if (eq)
            d.imageOr(lf_.map(icon.inset(2)), {ImageKind::ItemIcon, uint32_t(eq->slot)},
                      {ImageKind::CategoryIcon, uint32_t(cards[i].cat)}, 0xffffffffu, withAlpha(0xffffffffu, 90));
        else
            d.image(lf_.map(icon.inset(18)), {ImageKind::CategoryIcon, uint32_t(cards[i].cat)},
                    withAlpha(0xffffffffu, 60));
        textD(d, {r.x + 102, r.y + 14, r.w - 112, 28}, cards[i].label, FontRole::Small, th.textMuted);
        textD(d, {r.x + 102, r.y + 44, r.w - 112, 34}, eq ? eq->name : std::string("None"), FontRole::Body,
              eq ? th.text : th.textDisabled);
    }
    y += 96 + 26;

    // Quick inventory.
    detail::drawLabel(d, th, lf_, {kInvGridX, y, 400, 30}, "Quick Inventory", th.brass, TextAlign::Left, 0);
    y += 36;
    int q = 0;
    for (int i = 0; i < 8; ++i) {
        const Rect r{kInvGridX + static_cast<float>(i) * (kCell + kCellGap), y, kCell, kCell};
        d.panel(lf_.map(r), PanelStyle::Inset, withAlpha(th.inset, 170));
    }
    // In the native bar's order: key N uses / equips item N (QUICK1..QUICK9).
    for (const ItemEntry* quick : quickSlotItems(s)) {
        if (q >= 8)
            break;
        const ItemEntry& it = *quick;
        const Rect r{kInvGridX + static_cast<float>(q) * (kCell + kCellGap), y, kCell, kCell};
        d.imageOr(lf_.map(r.inset(2)), {ImageKind::ItemIcon, uint32_t(it.slot)},
                  {ImageKind::CategoryIcon, uint32_t(it.category)}, 0xffffffffu, withAlpha(0xffffffffu, 90));
        textD(d, {r.x + 8, r.y + 4, 40, 26}, fmtInt(q + 1), FontRole::Small, th.textMuted);
        ++q;
    }
    if (q == 0)
        textD(d, {kInvGridX + 16, y, kGridW - 32, kCell}, "Mark items as quick slots to use them in battle.",
              FontRole::Small, th.textDisabled, TextAlign::Center);

    drawItemDetail(s, focusedItem(s), design::detailPanel(), d, s.shop.active);
}

void InventoryScreen::drawNameplate(const Snapshot& s, const Rect& pv, DrawList& d) const {
    const Theme& th = cfg_.theme;
    textD(d, {pv.x, pv.bottom() - 104, pv.w, 52}, "Rynn", FontRole::Title, th.heading, TextAlign::Center);
    textD(d, {pv.x, pv.bottom() - 52, pv.w, 32}, "Level " + fmtInt(s.stats.level), FontRole::Small, th.textMuted,
          TextAlign::Center);
}

void InventoryScreen::drawCharacter(const Snapshot& s, DrawList& d) const {
    const Theme& th = cfg_.theme;

    // Rynn, left, as on the Inventory page (the world view does not move between the two pages).
    const Rect pv = design::inventoryPreview();
    d.worldView(lf_.map(pv));
    d.gradient(lf_.map({pv.x, pv.bottom() - 260, pv.w, 260}), withAlpha(th.backdrop, 0), withAlpha(th.backdrop, 150));
    drawNameplate(s, pv, d);

    // The centre column covers the native grid the glue keeps drawn (for the icon crops): opaque underneath.
    const Rect col = design::centreColumn();
    d.rect(lf_.map(col), rgba(12, 10, 8, 255));
    d.panel(lf_.map(col), PanelStyle::Main, th.panel);
    textD(d, {kInvGridX, kContentTop + 12, 600, 44}, "Equipment", FontRole::Heading, th.heading);
    const ItemEntry* focusedAlt = nullptr;
    for (int i = 0; i < 3; ++i) {
        const Rect r = equipSlotRect(i);
        const ItemCategory cat = kEquipSlots[i];
        const bool foc = focus_.kind == TargetKind::EquipSlot && focus_.index == static_cast<int>(cat);
        d.panel(lf_.map(r), PanelStyle::Card, th.panel);
        if (foc)
            drawFocus(lf_.map({r.x, r.y, kAltX - 12, r.h}), d);
        const ItemEntry* eq = s.equippedIn(cat);
        const Rect icon{r.x + 12, r.y + 12, 172, 172};
        d.panel(lf_.map(icon), PanelStyle::Inset, th.inset);
        if (eq)
            d.imageOr(lf_.map(icon.inset(4)), {ImageKind::ItemIcon, uint32_t(eq->slot)},
                      {ImageKind::CategoryIcon, uint32_t(cat)}, 0xffffffffu, withAlpha(0xffffffffu, 90));
        else
            d.image(lf_.map(icon.inset(40)), {ImageKind::CategoryIcon, uint32_t(cat)}, withAlpha(0xffffffffu, 50));
        const float x = r.x + 200, w = kAltX - 220;
        std::string caption = equipSlotLabel(cat);
        if (cat == ItemCategory::Melee && s.handsShown()) { // dual wielding: the card is the right hand
            const ItemEntry* left = s.inHand(ItemEntry::Hand::Left);
            caption = std::string(handName(ItemEntry::Hand::Right)) + "  \xC2\xB7  Left hand: " +
                      (left ? (left->name.empty() ? std::string("...") : left->name) : std::string("free"));
        }
        textD(d, {x, r.y + 12, w, 28}, caption, FontRole::Small, th.textMuted);
        textD(d, {x, r.y + 40, w, 40}, eq ? (eq->name.empty() ? std::string("...") : eq->name) : std::string("None"),
              FontRole::Heading, eq ? th.heading : th.textDisabled);
        if (eq) {
            // Stat lines (the quantity is the bar below).
            const QuantityKind qk = quantityKindFor(eq->category);
            int n = 0;
            for (size_t l = 1; l < eq->details.size() && n < 3; ++l) {
                std::string label, value;
                if (!splitStatLine(eq->details[l], label, value) || label.find("Price") != std::string::npos ||
                    (eq->quantityMax > 0 && label == quantityLabel(qk)))
                    continue;
                const float cy = r.y + 84 + static_cast<float>(n) * 28;
                textD(d, {x, cy, w, 28}, label, FontRole::Small, th.textMuted);
                textD(d, {x, cy, w, 28}, value, FontRole::Small, th.text, TextAlign::Right);
                ++n;
            }
            if (n == 0 && eq->details.size() <= 1)
                textD(d, {x, r.y + 84, w, 28}, "...", FontRole::Small, th.textMuted);
            if (eq->quantityMax > 0 && qk != QuantityKind::None)
                drawBar({x, r.y + 168, w, 22}, eq->quantity, eq->quantityMax,
                        qk == QuantityKind::Durability ? th.good : th.xp, quantityLabel(qk), d);
        }
        // The pack's alternatives of this kind, with their headline difference to the equipped one.
        const std::vector<const ItemEntry*> alts = alternatives(s, cat);
        detail::drawLabel(d, th, lf_, {r.x + kAltX, r.y + 8, kAltW, 28},
                          alts.empty() ? "Nothing else in your pack" : "In your pack",
                          alts.empty() ? th.textDisabled : th.brass, TextAlign::Left, 0);
        for (size_t k = 0; k < alts.size() && k < kAltRows; ++k) {
            const ItemEntry& alt = *alts[k];
            const Rect ar = altRow(i, static_cast<int>(k));
            const bool af = focus_.kind == TargetKind::GridCell && focus_.index == alt.originCell;
            d.panel(lf_.map(ar), PanelStyle::Inset, withAlpha(th.inset, 220));
            if (af) {
                drawFocus(lf_.map(ar), d);
                focusedAlt = &alt;
            }
            d.imageOr(lf_.map({ar.x + 4, ar.y + 4, 62, 62}), {ImageKind::ItemIcon, uint32_t(alt.slot)},
                      {ImageKind::CategoryIcon, uint32_t(alt.category)}, 0xffffffffu, withAlpha(0xffffffffu, 90));
            textD(d, {ar.x + 74, ar.y + 4, ar.w - 80, 32}, alt.name.empty() ? std::string("...") : alt.name,
                  FontRole::Small, th.text);
            if (eq && cfg_.showComparison) {
                for (const StatDelta& sd : compareItems(alt, *eq)) {
                    if (sd.better == 0 || sd.label.find("Price") != std::string::npos)
                        continue;
                    textD(d, {ar.x + 74, ar.y + 36, ar.w - 80, 30}, sd.label + " " + sd.candidate, FontRole::Small,
                          sd.better > 0 ? th.good : th.bad);
                    break;
                }
            }
        }
    }

    // The comparison of the focused pack item against what is equipped (or the equipped item's own lines).
    const float tableY = 836;
    const ItemEntry* eqFor = focusedAlt ? s.equippedFor(*focusedAlt) : nullptr;
    if (focusedAlt && eqFor && cfg_.showComparison) {
        const std::string title = (focusedAlt->name.empty() ? std::string("...") : focusedAlt->name) + "  vs  " +
                                  (eqFor->name.empty() ? std::string("...") : eqFor->name);
        textD(d, {kInvGridX, tableY - 10, kGridW, 32}, title, FontRole::Small, th.brass);
        int n = 0;
        for (const StatDelta& sd : compareItems(*focusedAlt, *eqFor)) {
            if (n >= 6 || sd.label.find("Price") != std::string::npos)
                continue;
            const float cx = kInvGridX + static_cast<float>(n % 2) * (kGridW * 0.5f + 10);
            const float cy = tableY + 24 + static_cast<float>(n / 2) * 32;
            const float cw = kGridW * 0.5f - 10;
            const Color c = sd.better > 0 ? th.good : (sd.better < 0 ? th.bad : th.text);
            textD(d, {cx, cy, cw * 0.5f, 30}, sd.label, FontRole::Small, th.textMuted);
            textD(d, {cx + cw * 0.5f, cy, cw * 0.25f, 30}, sd.candidate.empty() ? std::string("-") : sd.candidate,
                  FontRole::Small, c, TextAlign::Right);
            textD(d, {cx + cw * 0.75f, cy, cw * 0.25f, 30}, sd.equipped.empty() ? std::string("-") : sd.equipped,
                  FontRole::Small, th.textMuted, TextAlign::Right);
            ++n;
        }
    } else if (focusedAlt) {
        textD(d, {kInvGridX, tableY - 10, kGridW, 32},
              handUse(s, *focusedAlt) == dualwield::Use::EquipLeft
                  ? std::string("Rynn's left hand is free: Equip Left Hand to wield both.")
                  : std::string("Nothing of this kind is equipped: ") + primaryLabel(s, *focusedAlt) +
                        " it to wear it.",
              FontRole::Small, th.textMuted);
    } else {
        textD(d, {kInvGridX, tableY - 10, kGridW, 32}, "Move right to compare the gear in your pack.", FontRole::Small,
              th.textDisabled);
    }

    // Attributes.
    const Rect at{kDetailX, kContentTop, kDesignW - kMargin - kDetailX, kContentBottom - kContentTop};
    d.panel(lf_.map(at), PanelStyle::Main, th.panel);
    const float x = at.x + 28, w = at.w - 56;
    float y = at.y + 24;
    textD(d, {x, y, w, 44}, "Attributes", FontRole::Heading, th.heading);
    y += 64;
    textD(d, {x, y, w, 36}, "Level", FontRole::Body, th.textMuted);
    textD(d, {x, y, w, 36}, fmtInt(s.stats.level), FontRole::Numeric, th.heading, TextAlign::Right);
    y += 44;
    textD(d, {x, y, w, 36}, "Experience", FontRole::Body, th.textMuted);
    textD(d, {x, y, w, 36}, fmtInt(s.stats.xp), FontRole::Numeric, th.text, TextAlign::Right);
    y += 56;
    drawBar({x, y, w, 36}, s.stats.hp, s.stats.hpMax, th.hp, "Health", d);
    y += 52;
    drawBar({x, y, w, 36}, s.stats.mana, s.stats.manaMax, th.mana, "Mana", d);
    y += 60;
    textD(d, {x, y, w, 36}, "Gold", FontRole::Body, th.textMuted);
    textD(d, {x, y, w, 36}, fmtInt(s.stats.gold), FontRole::Numeric, th.heading, TextAlign::Right);
    y += 64;
    textD(d, {x, y, w, 40}, "Skills", FontRole::Heading, th.heading);
    y += 52;
    for (const SkillEntry& sk : s.skills.entries) {
        textD(d, {x, y, w, 34}, sk.name, FontRole::Body, th.text);
        textD(d, {x, y, w, 34}, fmtPair(sk.level, sk.max), FontRole::Body, th.textMuted, TextAlign::Right);
        y += 42;
    }
    if (s.skills.unspentPoints > 0)
        textD(d, {x, y + 8, w, 34}, fmtInt(s.skills.unspentPoints) + " skill points to spend", FontRole::Body,
              th.ember);
}

void InventoryScreen::drawSpells(const Snapshot& s, DrawList& d) const {
    const Theme& th = cfg_.theme;
    const SpellEntry* focused = nullptr;
    for (const SpellEntry& sp : s.spells.entries) {
        if (sp.index >= 10)
            break;
        const Rect r = spellRow(sp.index);
        const bool known = sp.knownLevel > 0;
        const bool active = sp.index == s.spells.selected;
        d.panel(lf_.map(r), PanelStyle::Card, known ? th.panel : withAlpha(th.panel, 140));
        const Rect icon{r.x + 8, r.y + 8, 56, 56};
        d.panel(lf_.map(icon), PanelStyle::Inset, th.inset);
        if (known)
            d.imageOr(lf_.map(icon.inset(6)), {ImageKind::SpellIcon, uint32_t(sp.index)},
                      {ImageKind::PageIcon, uint32_t(Page::Spells)}, 0xffffffffu, withAlpha(0xffffffffu, 200));
        textD(d, {r.x + 80, r.y, r.w - 260, r.h}, known ? (sp.name.empty() ? std::string("...") : sp.name) : "Unknown",
              FontRole::Body, known ? th.text : th.textDisabled);
        if (known)
            textD(d, {r.right() - 200, r.y, 180, r.h},
                  active ? "Active  -  Level " + fmtInt(sp.knownLevel) : "Level " + fmtInt(sp.knownLevel),
                  FontRole::Small, active ? th.ember : th.textMuted, TextAlign::Right);
        if (active)
            d.rect(lf_.map({r.x, r.y + 8, 4, r.h - 16}), th.ember);
        if (focus_.kind == TargetKind::Spell && focus_.index == sp.index) {
            drawFocus(lf_.map(r), d);
            focused = &sp;
        }
    }
    const Rect panel{840, kContentTop, 984, kContentBottom - kContentTop};
    d.panel(lf_.map(panel), PanelStyle::Main, th.panel);
    const float x = panel.x + 36, w = panel.w - 72;
    float y = panel.y + 28;
    if (!focused || focused->knownLevel <= 0) {
        textD(d, {x, y, w, 44}, focused ? "Unknown spell" : "", FontRole::Heading, th.textMuted);
        if (focused)
            textD(d, {x, y + 60, w, 34}, "Learn it from a spell scroll.", FontRole::Body, th.textDisabled);
        return;
    }
    textD(d, {x, y, w, 48}, focused->name.empty() ? std::string("...") : focused->name, FontRole::Title, th.heading);
    y += 60;
    textD(d, {x, y, w, 32}, "Level " + fmtInt(focused->knownLevel), FontRole::Body, th.textMuted);
    y += 52;
    for (size_t i = 1; i < focused->details.size(); ++i) {
        std::string label, value;
        if (splitStatLine(focused->details[i], label, value)) {
            textD(d, {x, y, w, 36}, label, FontRole::Body, th.textMuted);
            textD(d, {x, y, w, 36}, value, FontRole::Body, th.text, TextAlign::Right);
        } else {
            textD(d, {x, y, w, 36}, focused->details[i], FontRole::Body, th.brass);
        }
        y += 42;
    }
    if (focused->details.empty())
        textD(d, {x, y, w, 34}, "...", FontRole::Body, th.textDisabled);
    // The requirement: the Magic skill against the spell's "Skill Needed".
    for (const std::string& line : focused->details) {
        std::string label, value;
        if (!splitStatLine(line, label, value) || label.find("Skill") == std::string::npos)
            continue;
        int magic = -1;
        for (const SkillEntry& sk : s.skills.entries)
            if (sk.id == 3)
                magic = sk.level;
        const int need = std::atoi(value.c_str());
        if (magic >= 0 && need > 0) {
            y += 12;
            textD(d, {x, y, w, 34},
                  magic >= need ? "Your Magic skill (" + fmtInt(magic) + ") is enough."
                                : "Needs Magic " + fmtInt(need) + " (yours: " + fmtInt(magic) + ").",
                  FontRole::Body, magic >= need ? th.good : th.bad);
        }
        break;
    }
    if (s.spells.summonBusy())
        textD(d, {x, panel.bottom() - 70, w, 34}, "A summoned creature is active.", FontRole::Body, th.ember);
}

void InventoryScreen::drawSkills(const Snapshot& s, DrawList& d) const {
    const Theme& th = cfg_.theme;
    textD(d, {360, kContentTop + 10, 600, 50}, "Skills", FontRole::Title, th.heading);
    textD(d, {960, kContentTop + 18, 600, 40},
          s.skills.unspentPoints > 0 ? fmtInt(s.skills.unspentPoints) + " points to spend" : "No points to spend",
          FontRole::Body, s.skills.unspentPoints > 0 ? th.ember : th.textMuted, TextAlign::Right);
    for (size_t i = 0; i < s.skills.entries.size() && i < 3; ++i) {
        const int row = static_cast<int>(i);
        const SkillEntry& sk = s.skills.entries[i];
        const Rect r = skillRow(row);
        d.panel(lf_.map(r), PanelStyle::Card, th.panel);
        textD(d, {r.x + 32, r.y + 20, 500, 44}, sk.name, FontRole::Heading, th.text);
        textD(d, {r.x + 32, r.y + 70, 300, 34}, "Level " + fmtPair(sk.level, sk.max), FontRole::Body, th.textMuted);
        // level pips
        const int pips = std::clamp(sk.max, 0, 12);
        for (int p = 0; p < pips; ++p) {
            const Rect pr{r.x + 420 + static_cast<float>(p) * 60, r.y + 62, 48, 26};
            d.panel(lf_.map(pr), PanelStyle::Inset, th.inset);
            if (p < sk.committed)
                d.gradient(lf_.map(pr.inset(3)), th.ember, withAlpha(th.ember, 170));
            else if (p < sk.level) // spent on this visit, applied when the page is left
                d.gradient(lf_.map(pr.inset(3)), th.heading, withAlpha(th.ember, 200));
        }
        for (TargetKind k : {TargetKind::SkillMinus, TargetKind::SkillPlus}) {
            const HitTarget* t = findTarget({k, row});
            if (!t)
                continue;
            const bool hov = hover_ == t->id && t->enabled;
            d.panel(t->rect, PanelStyle::Card, hov ? th.panelRaised : th.panel);
            d.outline(t->rect, t->enabled ? th.brass : th.brassDim, lf_.px(1.5f), lf_.px(4));
            d.text(t->rect, k == TargetKind::SkillPlus ? "+" : "-", FontRole::Heading, fontPx(FontRole::Heading),
                   t->enabled ? th.heading : th.textDisabled, TextAlign::Center);
        }
        if (focus_.kind == TargetKind::Skill && focus_.index == row)
            drawFocus(lf_.map(r), d);
    }
    textD(d, {360, 790, 1200, 34}, "Points are applied when you leave this page.", FontRole::Small, th.textMuted,
          TextAlign::Center);
}

void InventoryScreen::drawJournal(const Snapshot& s, DrawList& d) const {
    const Theme& th = cfg_.theme;
    textD(d, {kMargin, kContentTop, 720, 50}, "Journal", FontRole::Title, th.heading);
    const QuestEntry* focused = nullptr;
    for (int v = 0; v < kJournalVisibleRows; ++v) {
        const size_t r = static_cast<size_t>(questScroll_ + v);
        if (r >= journalOrder_.size())
            break;
        const Rect row = journalRow(v);
        const int qi = journalOrder_[r];
        if (qi < 0) {
            detail::drawLabel(d, th, lf_, {row.x, row.y + 14, row.w, row.h - 14},
                              qi == -1 ? "Primary Quests" : "Optional Quests", th.brass, TextAlign::Left, 0);
            continue;
        }
        const QuestEntry* q = nullptr;
        for (const QuestEntry& e : s.quests)
            if (e.index == qi)
                q = &e;
        if (!q)
            continue;
        const bool done = q->status == 2;
        d.panel(lf_.map(row), PanelStyle::Card, withAlpha(th.panel, 190));
        std::string title = q->text.empty() ? (q->caption.empty() ? std::string("...") : q->caption) : q->text;
        textD(d, {row.x + 20, row.y, row.w - 40, row.h}, title, FontRole::Body, done ? th.textDisabled : th.text);
        if (focus_.kind == TargetKind::Quest && focus_.index == qi) {
            drawFocus(lf_.map(row), d);
            focused = q;
        }
    }
    if (journalOrder_.empty())
        textD(d, {kMargin, kJournalListY + 20, 720, 34}, "No journal entries yet.", FontRole::Body, th.textDisabled);

    const Rect page{856, kContentTop, 968, kContentBottom - kContentTop};
    d.panel(lf_.map(page), PanelStyle::Parchment, th.parchment);
    if (!focused)
        return;
    const float x = page.x + 56, w = page.w - 112;
    float y = page.y + 48;
    for (const MapRegion& r : s.regions)
        if (r.index == focused->region)
            textD(d, {x, y, w, 44}, r.name, FontRole::Heading, th.ink);
    y += 64;
    textD(d, {x, y, w, 30}, focused->optional ? "Optional" : "Primary", FontRole::Small, withAlpha(th.ink, 170));
    if (focused->status == 2)
        textD(d, {x, y, w, 30}, "Completed", FontRole::Small, withAlpha(th.ink, 170), TextAlign::Right);
    y += 48;
    const float px = fontPx(FontRole::Body);
    for (const std::string& line : wrapText(focused->text, lf_.px(w), FontRole::Body, px, cfg_.measure)) {
        if (y + 40 > page.bottom() - 40)
            break;
        textD(d, {x, y, w, 38}, line, FontRole::Body, th.ink);
        y += 40;
    }
}

void InventoryScreen::drawMap(const Snapshot& s, DrawList& d) const {
    const Theme& th = cfg_.theme;
    const Rect mp = mapPanel();
    d.panel(lf_.map(mp), PanelStyle::Main, th.panel);
    const Rect inner = mp.inset(24);
    int region = 0;
    for (const QuestEntry& q : s.quests)
        if (q.marker()) {
            region = q.region;
            break;
        }
    Rect img = inner;
    const bool known = cfg_.mapImageW > 0 && cfg_.mapImageH > 0;
    if (known) {
        const float aspect = cfg_.mapAspect > 0 ? cfg_.mapAspect : cfg_.mapImageW / cfg_.mapImageH;
        const float w = std::min(inner.w, inner.h * aspect), h = w / aspect;
        img = {inner.cx() - w * 0.5f, inner.cy() - h * 0.5f, w, h};
    }
    d.image(lf_.map(img), {ImageKind::MapBackground, uint32_t(region)});
    textD(d, {1384, kContentTop, 440, 50}, "Locations", FontRole::Title, th.heading);
    int row = 0;
    for (const QuestEntry& q : s.quests) {
        if (!q.marker() || !q.enabled || row >= 13)
            continue;
        const bool foc = focus_.kind == TargetKind::Marker && focus_.index == q.index;
        if (known) {
            // The native markers are part of the map image; the focused one gets a ring.
            const Rect dot{img.x + static_cast<float>(q.mapX) / cfg_.mapImageW * img.w - 22,
                           img.y + static_cast<float>(q.mapY) / cfg_.mapImageH * img.h - 22, 44, 44};
            if (foc)
                d.outline(lf_.map(dot), th.select, lf_.px(2.5f), lf_.px(22));
        }
        const Rect r = markerRow(row++);
        d.panel(lf_.map(r), PanelStyle::Card, withAlpha(th.panel, 190));
        // Captions are resident only once the native map has shown them (the reader never loads strings).
        textD(d, {r.x + 20, r.y, r.w - 40, r.h}, q.caption.empty() ? "Location " + fmtInt(row) : q.caption,
              FontRole::Body, q.caption.empty() ? th.textMuted : th.text);
        if (foc)
            drawFocus(lf_.map(r), d);
    }
    float y = kMarkerRowY + static_cast<float>(row) * kMarkerRowH + 24;
    if (!s.regions.empty() && y < kContentBottom - 80) {
        detail::drawLabel(d, th, lf_, {1384, y, 440, 30}, "Regions", th.brass, TextAlign::Left, 0);
        y += 36;
        for (const MapRegion& r : s.regions) {
            if (y > kContentBottom - 40)
                break;
            textD(d, {1384, y, 440, 32}, r.name, FontRole::Body, r.index == region ? th.text : th.textMuted);
            y += 36;
        }
    }
}

void InventoryScreen::drawShop(const Snapshot& s, DrawList& d) const {
    const Theme& th = cfg_.theme;
    textD(d, {kMargin, kContentTop, 648, 50}, "Merchant", FontRole::Title, th.heading);
    const Rect card = shopStockCard();
    if (s.shop.hasStock) {
        const ItemEntry& st = s.shop.stock;
        d.panel(lf_.map(card), PanelStyle::Card, th.panel);
        const Rect icon{card.x + 20, card.y + 20, 180, 180};
        d.panel(lf_.map(icon), PanelStyle::Inset, th.inset);
        d.image(lf_.map(icon.inset(6)), {ImageKind::ItemIcon, uint32_t(st.slot)});
        textD(d, {card.x + 220, card.y + 24, card.w - 240, 44}, st.name.empty() ? std::string("...") : st.name,
              FontRole::Heading, th.heading);
        textD(d, {card.x + 220, card.y + 72, card.w - 240, 30}, categoryLabel(st.category), FontRole::Small,
              th.textMuted);
        float y = card.y + 112;
        for (size_t i = 1; i < st.details.size() && y < card.bottom() - 30; ++i) {
            textD(d, {card.x + 220, y, card.w - 240, 32}, st.details[i], FontRole::Small, th.text);
            y += 34;
        }
        if (focus_.kind == TargetKind::ShopStock)
            drawFocus(lf_.map(card), d);
    } else {
        d.panel(lf_.map(card), PanelStyle::Card, withAlpha(th.panel, 140));
        textD(d, card, "Nothing for sale", FontRole::Body, th.textDisabled, TextAlign::Center);
    }
    for (TargetKind k : {TargetKind::ShopPrev, TargetKind::ShopNext}) {
        const HitTarget* t = findTarget({k, 0});
        if (!t)
            continue;
        d.panel(t->rect, PanelStyle::Card, hover_ == t->id ? th.panelRaised : th.panel);
        d.outline(t->rect, th.brass, lf_.px(1.5f), lf_.px(4));
        d.text(t->rect, k == TargetKind::ShopPrev ? "<" : ">", FontRole::Heading, fontPx(FontRole::Heading), th.heading,
               TextAlign::Center);
    }
    textD(d, {kMargin + 110, 516, 648 - 220, 64}, "Browse", FontRole::Small, th.textMuted, TextAlign::Center);

    textD(d, {kShopGridX, kContentTop, 600, 50}, "Your Pack", FontRole::Heading, th.heading);
    drawGrid(s, d, false);
    const ItemEntry* own = focus_.kind == TargetKind::GridCell ? s.itemAtCell(focus_.index) : nullptr;
    const Rect detail{kShopGridX, 612, 1048, kContentBottom - 612};
    if (own) {
        drawItemDetail(s, own, detail, d, true);
    } else {
        d.panel(lf_.map(detail), PanelStyle::Main, th.panel);
        textD(d, {detail.x + 32, detail.y + 28, detail.w - 64, 34}, "Select an item from your pack to sell it.",
              FontRole::Body, th.textMuted);
    }
}

} // namespace gameui
