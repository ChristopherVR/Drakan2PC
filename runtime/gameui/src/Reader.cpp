#include "gameui/Reader.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "gameui/Addresses.h"

namespace gameui {

namespace a = addr;

bool isRamPointer(uint32_t p) {
    return p >= a::kRamPtrMin && p < a::kRamPtrEnd;
}

bool isItemHandle(const IGuestMemory& m, uint32_t h) {
    return h > a::actor::kMaxContinuation && isRamPointer(h) && m.read16(h + a::actor::kResIndex) != 0xffff;
}

const char* readStatusName(ReadStatus s) {
    switch (s) {
        case ReadStatus::Ok:
            return "ok";
        case ReadStatus::NoContext:
            return "no-context";
        case ReadStatus::NoPlayer:
            return "no-player";
        case ReadStatus::Dragon:
            return "dragon";
    }
    return "?";
}

uint32_t resourceClass(const IGuestMemory& m, uint32_t res, uint32_t index) {
    const uint32_t table = m.read32(res + a::res::kRecordTable);
    const uint32_t count = m.read32(res + a::res::kRecordCount);
    if (!isRamPointer(table) || index >= count)
        return 0;
    return m.read32(table + index * a::res::kRecordSize + a::res::kRecClass);
}

uint32_t resourceProps(const IGuestMemory& m, uint32_t res, uint32_t index) {
    const uint32_t table = m.read32(res + a::res::kRecordTable);
    const uint32_t count = m.read32(res + a::res::kRecordCount);
    if (!isRamPointer(table) || index >= count)
        return 0;
    return m.read32(table + index * a::res::kRecordSize + a::res::kRecProps);
}

uint32_t prop(const IGuestMemory& m, uint32_t block, uint32_t id) {
    if (!isRamPointer(block))
        return 0;
    const uint32_t data = m.read32(block + a::props::kData);
    return isRamPointer(data) ? m.read32(data + id * 4) : 0;
}

uint32_t propIndexed(const IGuestMemory& m, uint32_t block, uint32_t id, uint32_t k) {
    if (!isRamPointer(block))
        return 0;
    const uint32_t data = m.read32(block + a::props::kData);
    if (!isRamPointer(data))
        return 0;
    const uint32_t base = m.read16(data + id * 4);
    return m.read32(data + (base + k) * 4);
}

std::optional<std::string> lookupString(const IGuestMemory& m, uint32_t mgr, uint32_t id) {
    namespace s = a::strings;
    if (!isRamPointer(mgr) || id == 0)
        return std::nullopt;
    uint32_t e = m.read32(mgr + (((id * s::kHashMul) & s::kBucketMask) + s::kBucketBaseWord) * 4);
    for (uint32_t n = 0; n < s::kMaxChain && isRamPointer(e); ++n) {
        if (m.read32(e + s::kEntryId) == id) {
            if ((m.read32(e + s::kEntryFlags) & s::kLoadedMask) != s::kLoadedMask)
                return std::nullopt;
            const uint32_t text = m.read32(e + s::kEntryText);
            if (!isRamPointer(text))
                return std::nullopt;
            return m.readString(text, s::kMaxText);
        }
        e = m.read32(e + s::kEntryNext);
    }
    return std::nullopt;
}

ReadStatus findRoots(const IGuestMemory& m, GuestRoots& r) {
    r = {};
    r.ctx = m.read32(a::kContextPtr);
    if (!isRamPointer(r.ctx))
        return ReadStatus::NoContext;
    r.res = m.read32(r.ctx + a::ctx::kResourceDb);
    r.strings = m.read32(r.ctx + a::ctx::kStringMgr);
    r.key = m.read32(a::kDecodeKey);
    r.topScreen = m.read32(a::kTopScreenPtr);
    if (isRamPointer(r.topScreen))
        r.topScreenId = m.read32(r.topScreen + a::screen::kId);

    const uint32_t actor = m.read32(r.ctx + a::ctx::kPlayerActor);
    if (isRamPointer(actor) && resourceClass(m, r.res, m.read16(actor + a::actor::kResIndex)) == a::cls::kDragon)
        return ReadStatus::Dragon;

    if (r.topScreenId == a::screen::kInventoryId)
        r.player = m.read32(r.topScreen + a::screen::kPlayer);
    if (!isRamPointer(r.player) && isRamPointer(actor)) {
        const uint32_t inst = m.read32(actor + a::actor::kInstance);
        if (isRamPointer(inst))
            r.player = m.read32(inst + a::actor::kInstanceData);
    }
    if (!isRamPointer(r.player)) {
        r.player = 0;
        return ReadStatus::NoPlayer;
    }
    r.ctl = r.player + a::player::kController;
    return ReadStatus::Ok;
}

void readStats(const IGuestMemory& m, uint32_t key, CharacterStats& out) {
    auto dec = [&](uint32_t off) { return static_cast<int32_t>(decodeValue(m.read32(a::kStatsBlock + off), key)); };
    out.hp = dec(a::stats::kHp);
    out.hpMax = dec(a::stats::kHpMax);
    out.mana = dec(a::stats::kMana);
    out.manaMax = dec(a::stats::kManaMax);
    out.gold = dec(a::stats::kGold);
    out.xp = dec(a::stats::kXp);
    out.level = dec(a::stats::kLevel);
}

void readSkills(const IGuestMemory& m, uint32_t key, SkillSheet& out) {
    out.entries.clear();
    out.unspentPoints = static_cast<int32_t>(decodeValue(m.read32(a::kStatsBlock + a::stats::kSkillPoints), key));
    const uint32_t table = m.read32(a::kStatsBlock + a::stats::kSkillTable);
    const uint32_t count = std::min(m.read32(a::kStatsBlock + a::stats::kSkillCount), a::stats::kMaxSkills);
    if (!isRamPointer(table))
        return;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t rec = table + i * a::stats::kSkillStride;
        SkillEntry e;
        e.id = m.readS32(rec + a::stats::kSkillId);
        e.level = m.readS32(rec + a::stats::kSkillLevel);
        e.max = m.readS32(rec + a::stats::kSkillMax);
        e.committed = e.level;
        e.name = m.readString(rec + a::stats::kSkillName, a::stats::kSkillNameMax);
        out.entries.push_back(std::move(e));
    }
}

namespace {

uint32_t slotRecord(uint32_t ctl, int slot) {
    if (slot == a::ctl::kSelSlot1A)
        return ctl + a::ctl::kSlot1A;
    if (slot == a::ctl::kSelShopStock)
        return ctl + a::ctl::kSlotShopStock;
    return ctl + a::ctl::kSlots + static_cast<uint32_t>(slot) * a::ctl::kSlotStride;
}

void readItem(const IGuestMemory& m, const GuestRoots& r, uint32_t h, ItemEntry& e) {
    e.handle = h;
    e.objectType = m.read32(h + a::actor::kType);
    e.classId = resourceClass(m, r.res, m.read16(h + a::actor::kResIndex));
    e.category = categoryFor(e.classId, e.objectType);
    const uint32_t pb = m.read32(h + a::actor::kProps);
    if (isRamPointer(pb)) {
        e.nameId = prop(m, pb, a::props::kName);
        e.price = static_cast<int32_t>(prop(m, pb, a::props::kPrice));
        e.skillNeeded = static_cast<int32_t>(prop(m, pb, a::props::kSkillNeeded));
        if (e.category == ItemCategory::Melee) {
            e.meleeDamage = static_cast<int32_t>(prop(m, pb, a::props::kMeleeDamage));
            e.stance = prop(m, pb, dualwield::kPropStance);
        }
        e.dropAllowed = prop(m, pb, a::props::kDropAllowed) != 0;
    }
    const uint32_t inst = m.read32(h + a::actor::kInstance);
    const uint32_t data = isRamPointer(inst) ? m.read32(inst + a::actor::kInstanceData) : 0;
    if (isRamPointer(data)) {
        e.quantity = static_cast<int32_t>(decodeValue(m.read32(data + a::actor::kQtyCur), r.key));
        e.quantityMax = static_cast<int32_t>(decodeValue(m.read32(data + a::actor::kQtyMax), r.key));
    }
    if (auto name = lookupString(m, r.strings, e.nameId)) {
        e.name = std::move(*name);
        e.nameResident = true;
    }
}

} // namespace

void readItems(const IGuestMemory& m, const GuestRoots& r, Snapshot& out) {
    out.items.clear();
    out.cellItem.fill(-1);
    uint32_t values[a::ctl::kSlotCount];
    for (int i = 0; i < a::ctl::kSlotCount; ++i)
        values[i] = m.read32(slotRecord(r.ctl, i) + a::slot::kItem);

    // Origins first, in slot order.
    for (int i = 0; i < a::ctl::kSlotCount; ++i) {
        if (!isItemHandle(m, values[i]))
            continue;
        ItemEntry e;
        e.slot = e.originCell = i;
        const uint32_t rec = slotRecord(r.ctl, i);
        e.equipped = m.read32(rec + a::slot::kEquipped) != 0;
        e.hotSlot = m.read32(rec + a::slot::kHotSlot) != 0;
        readItem(m, r, values[i], e);
        out.cellItem[static_cast<size_t>(i)] = static_cast<int8_t>(out.items.size());
        out.items.push_back(std::move(e));
    }
    // Continuation markers v = dx*10 + dy point back to the origin: origin = cell - v/10 - (v%10)*8.
    for (int i = 0; i < a::ctl::kSlotCount; ++i) {
        const uint32_t v = values[i];
        if (v == 0 || v > a::actor::kMaxContinuation)
            continue;
        const int dx = static_cast<int>(v / 10), dy = static_cast<int>(v % 10);
        const int origin = i - dx - dy * a::ctl::kGridColumns;
        if (origin < 0 || origin % a::ctl::kGridColumns + dx != i % a::ctl::kGridColumns)
            continue; // would wrap a row: not a marker of this grid
        const int idx = out.cellItem[static_cast<size_t>(origin)];
        if (idx < 0 || out.items[static_cast<size_t>(idx)].originCell != origin)
            continue;
        ItemEntry& e = out.items[static_cast<size_t>(idx)];
        e.width = std::max(e.width, dx + 1);
        e.height = std::max(e.height, dy + 1);
        out.cellItem[static_cast<size_t>(i)] = static_cast<int8_t>(idx);
    }

    // Hands (dual wielding): equipment slot 0 is the right hand, slot 2 (the shield slot) the left-hand weapon.
    const uint32_t right = m.read32(r.player + dualwield::kPlayerSlots);
    out.shieldSlot = m.read32(r.player + dualwield::kPlayerSlots + 2 * dualwield::kSlotStride);
    out.bowDrawn = m.read32(r.player + dualwield::kPlayerLeftHolder) == dualwield::kBowMode;
    out.dualWield = dualwield::enabled();
    for (ItemEntry& e : out.items)
        if (e.equipped && e.category == ItemCategory::Melee)
            e.hand = e.handle == out.shieldSlot ? ItemEntry::Hand::Left
                     : e.handle == right        ? ItemEntry::Hand::Right
                                                : ItemEntry::Hand::None;

    // Shop: record 0x1b is the current stock item while a shop is open (stale garbage otherwise).
    ShopState& shop = out.shop;
    shop = {};
    shop.active = m.read32(r.ctl + a::ctl::kShopMode) != 0;
    if (shop.active) {
        shop.shopkeeper = m.read32(r.ctl + a::ctl::kShopkeeper);
        shop.selection = m.readS32(r.ctl + a::ctl::kShopSelection);
        const uint32_t h = m.read32(slotRecord(r.ctl, a::ctl::kSelShopStock) + a::slot::kItem);
        if (isItemHandle(m, h)) {
            shop.hasStock = true;
            shop.stock.slot = shop.stock.originCell = a::ctl::kSelShopStock;
            readItem(m, r, h, shop.stock);
        }
    }
}

void readSpells(const IGuestMemory& m, const GuestRoots& r, SpellBook& out) {
    out.entries.clear();
    out.selected = m.readS32(r.ctl + a::ctl::kSelectedSpell);
    out.summonState = m.read32(r.player + a::player::kSummonState);
    const uint32_t list = m.read32(r.player + a::player::kSpellList);
    const uint32_t owner = isRamPointer(list) ? m.read32(list + a::player::kSpellOwner) : 0;
    const uint32_t pb = isRamPointer(owner) ? m.read32(owner + a::actor::kProps) : 0;
    const uint32_t table = m.read32(r.player + a::player::kSpellTable);
    const uint32_t count = std::min(m.read32(r.player + a::player::kSpellCount), a::player::kMaxSpells);
    if (!isRamPointer(table))
        return;
    for (uint32_t i = 0; i < count; ++i) {
        SpellEntry e;
        e.index = static_cast<int>(i);
        const uint32_t rec = table + i * a::player::kSpellStride;
        e.knownLevel = m.readS32(rec + a::player::kSpellKnownLevel);
        e.levelResource = m.read32(rec + a::player::kSpellLevelRes);
        e.state = m.read32(rec + a::player::kSpellState);
        if (isRamPointer(pb)) {
            const uint32_t type = propIndexed(m, pb, a::props::kSpellTypes, i);
            const uint32_t typeProps = resourceProps(m, r.res, type);
            if (auto name = lookupString(m, r.strings, prop(m, typeProps, a::props::kName))) {
                e.name = std::move(*name);
                e.nameResident = true;
            }
        }
        out.entries.push_back(std::move(e));
    }
}

void readQuests(const IGuestMemory& m, const GuestRoots& r, std::vector<QuestEntry>& out) {
    out.clear();
    const uint32_t table = m.read32(a::kQuestTable);
    const uint32_t count = std::min(m.read32(a::kQuestCount), a::quest::kMaxQuests);
    if (!isRamPointer(table))
        return;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t rec = table + i * a::quest::kStride;
        QuestEntry q;
        q.index = static_cast<int>(i);
        q.mapX = m.readS32(rec + a::quest::kMapX);
        q.mapY = m.readS32(rec + a::quest::kMapY);
        q.icon = m.read32(rec + a::quest::kIcon);
        q.enabled = m.read32(rec + a::quest::kEnabled) != 0;
        q.region = m.readS32(rec + a::quest::kRegion);
        const uint32_t pb = resourceProps(m, r.res, m.read32(rec + a::quest::kResIndex));
        q.status = static_cast<int>(prop(m, pb, a::props::kQuestStatus));
        q.optional = prop(m, pb, a::props::kQuestOptional) != 0;
        q.textId = prop(m, pb, a::props::kQuestText);
        q.captionId = prop(m, pb, a::props::kQuestCaption);
        if (auto t = lookupString(m, r.strings, q.textId)) {
            q.text = std::move(*t);
            q.textResident = true;
        }
        if (auto c = lookupString(m, r.strings, q.captionId)) {
            q.caption = std::move(*c);
            q.captionResident = true;
        }
        out.push_back(std::move(q));
    }
}

void readRegions(const IGuestMemory& m, std::vector<MapRegion>& out) {
    out.clear();
    for (uint32_t i = 0; i < a::kRegionCount; ++i) {
        std::string name = m.readString(a::kRegionNames + i * a::kRegionStride, a::kRegionStride);
        if (!name.empty())
            out.push_back({static_cast<int>(i), std::move(name)});
    }
}

std::vector<std::string> readDetails(const IGuestMemory& m, uint32_t ctl) {
    std::vector<std::string> lines;
    for (uint32_t off : a::ctl::kDetailLines) {
        std::string s = m.readString(ctl + off, a::ctl::kDetailLineSize);
        const bool blank = std::all_of(s.begin(), s.end(), [](char c) { return c == ' '; });
        // The runtime's hand line for the native panel: the modern screens label hands themselves.
        if (!blank && !dualwield::isHandLine(s))
            lines.push_back(std::move(s));
    }
    return lines;
}

ReadStatus readSnapshot(const IGuestMemory& m, ReaderState& state, Snapshot& out) {
    out.sequence = ++state.sequence;
    out.native = {};
    out.stats = {};
    out.items.clear();
    out.cellItem.fill(-1);
    out.spells = {};
    out.skills = {};
    out.quests.clear();
    out.regions.clear();
    out.mapRegion = -1;
    out.shop = {};
    out.details.clear();

    GuestRoots r;
    const ReadStatus st = findRoots(m, r);
    out.native.topScreen = r.topScreen;
    out.native.topScreenId = r.topScreenId;
    if (st == ReadStatus::NoContext)
        return st;
    out.native.inventoryOpen = m.read32(a::kStatsBlock + a::stats::kInventoryOpen) != 0;
    readStats(m, r.key, out.stats);
    readSkills(m, r.key, out.skills);
    // The quest table and region names are globals: the pause and map screens need them on the dragon too.
    readQuests(m, r, out.quests);
    readRegions(m, out.regions);
    if (st == ReadStatus::Dragon) {
        out.native.onDragon = true;
        return st;
    }
    if (st != ReadStatus::Ok)
        return st;

    if (r.topScreenId == a::screen::kInventoryId) {
        out.native.section = m.readS32(r.topScreen + a::screen::kSection);
        {
            const uint32_t bits = m.read32(r.topScreen + a::screen::kAnimSpeed);
            float v = 0;
            std::memcpy(&v, &bits, sizeof v);
            out.native.animation = std::isfinite(v) ? v : 0.0f;
        }
        const uint32_t sections = m.read32(r.topScreen + a::screen::kSections);
        if (isRamPointer(sections) && m.read32(r.topScreen + a::screen::kSectionCount) == 5) {
            const uint32_t sec = m.read32(sections + 2 * 4);
            if (isRamPointer(sec) && m.read32(sec + a::section::kVtable) == a::section::kSkillsVtable) {
                out.native.skillRow = m.readS32(sec + a::section::kSkillRow);
                // On the Skills page the section's working copies are what the page shows and what Left / Right
                // change; the stats block only changes when they are applied.
                const uint32_t rows = m.read32(sec + a::section::kSkillRows);
                if (out.native.section == 2 && isRamPointer(rows)) {
                    out.skills.pending = true;
                    out.skills.unspentPoints = m.readS32(sec + a::section::kSkillPending);
                    for (size_t i = 0; i < out.skills.entries.size() && i < 3; ++i) {
                        const uint32_t row = rows + static_cast<uint32_t>(i) * a::section::kSkillRowStride;
                        out.skills.entries[i].level = m.readS32(row + a::section::kRowLevel);
                        out.skills.entries[i].committed = m.readS32(row + a::section::kRowCommitted);
                    }
                }
            }
            const uint32_t map = m.read32(sections + a::section::kMapSectionIndex * 4);
            if (isRamPointer(map)) {
                const int32_t region = m.readS32(map + a::section::kMapRegion);
                if (region >= 0 && static_cast<uint32_t>(region) < a::kRegionCount)
                    out.mapRegion = region;
                const int32_t sel = m.readS32(map + a::section::kMapSelection);
                if (sel >= 0 && sel < 256)
                    out.native.mapSelection = sel;
            }
        }
    }
    out.native.controllerBusy = m.read32(r.ctl + a::ctl::kBusy) != 0;
    out.native.selection = m.readS32(r.ctl + a::ctl::kSelection);
    out.native.pane = m.readS32(r.ctl + a::ctl::kPane);
    out.native.detailsValid = m.read32(r.ctl + a::ctl::kDetailsValid) != 0;

    readItems(m, r, out);
    readSpells(m, r, out.spells);

    // Details describe the native selection: the spell ctl+0x4b4 on the Spells pane, the shop stock item on the
    // shop pane, else the grid cell ctl+0x20. Only trusted while the controller is idle; a name mismatch (stale
    // buffers) is not cached.
    if (!out.native.controllerBusy && out.native.detailsValid) {
        out.details = readDetails(m, r.ctl);
        const bool req = m.read32(r.ctl + a::ctl::kRequirementMet) != 0;
        auto adopt = [&](ItemEntry& e) {
            if (out.details.empty() || (e.nameResident && out.details.front() != e.name))
                return;
            state.itemDetails[e.handle] = out.details;
            e.detailsCurrent = true;
            e.requirementMet = req;
        };
        if (out.native.pane == 1) {
            if (out.spells.selected >= 0)
                state.spellDetails[out.spells.selected] = out.details;
        } else if (out.native.pane == 2 || out.native.selection == a::ctl::kSelShopStock) {
            if (out.shop.hasStock)
                adopt(out.shop.stock);
        } else if (out.native.selection >= 0 && out.native.selection < a::ctl::kSlotCount) {
            const int idx = out.cellItem[static_cast<size_t>(out.native.selection)];
            if (idx >= 0)
                adopt(out.items[static_cast<size_t>(idx)]);
        }
    }

    // Cached details, and the name fallback from details line 0.
    auto fill = [&](ItemEntry& e) {
        auto it = state.itemDetails.find(e.handle);
        if (it == state.itemDetails.end())
            return;
        if (!e.detailsCurrent)
            e.details = it->second;
        else
            e.details = out.details;
        if (!e.nameResident && !e.details.empty())
            e.name = e.details.front();
    };
    for (ItemEntry& e : out.items)
        fill(e);
    if (out.shop.hasStock)
        fill(out.shop.stock);
    for (SpellEntry& s : out.spells.entries) {
        auto it = state.spellDetails.find(s.index);
        if (it != state.spellDetails.end()) {
            s.details = it->second;
            if (!s.nameResident && !s.details.empty())
                s.name = s.details.front();
        }
    }
    // Forget handles that left the inventory (the cache would otherwise grow with every pickup).
    for (auto it = state.itemDetails.begin(); it != state.itemDetails.end();) {
        const bool alive = out.itemByHandle(it->first) || (out.shop.hasStock && out.shop.stock.handle == it->first);
        it = alive ? std::next(it) : state.itemDetails.erase(it);
    }

    out.native.valid = true;
    return ReadStatus::Ok;
}

} // namespace gameui
