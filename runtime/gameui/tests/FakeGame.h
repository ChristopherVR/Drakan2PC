// Synthetic guest memory laid out like the PAL game (docs/journal/2026-10-24/tab-rework-data-model.md), for the
// reader and screen tests. No game data: every value here is made up or a small number from the note.
#pragma once
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "gameui/Addresses.h"
#include "gameui/GuestMemory.h"
#include "gameui/Reader.h"

namespace gameui::test {

class FakeGame {
public:
    static constexpr uint32_t kCtx = 0x4de5e0;
    static constexpr uint32_t kKey = 0x739acd45;
    static constexpr uint32_t kResDb = 0x5f0000; // *(ctx+0x320)
    static constexpr uint32_t kResTable = 0x600000;
    static constexpr uint32_t kStringMgr = 0x610000;

    std::vector<uint8_t> ram = std::vector<uint8_t>(0x2000000, 0);
    uint32_t heap = 0x800000;
    uint32_t resCount = 0;
    uint32_t screen = 0, player = 0, ctl = 0, actor = 0;
    uint32_t spellOwnerProps = 0;

    FlatGuestMemory mem() const { return FlatGuestMemory(ram.data(), ram.size()); }

    void w32(uint32_t a, uint32_t v) { std::memcpy(&ram[a], &v, 4); }
    void w16(uint32_t a, uint16_t v) { std::memcpy(&ram[a], &v, 2); }
    void wstr(uint32_t a, const std::string& s) {
        std::memcpy(&ram[a], s.data(), s.size());
        ram[a + s.size()] = 0;
    }
    void enc(uint32_t a, int32_t v) { w32(a, encodeValue(static_cast<uint32_t>(v), kKey)); }
    uint32_t alloc(uint32_t n) {
        const uint32_t p = heap;
        heap += (n + 15) & ~15u;
        return p;
    }

    // Property block with `n` data words.
    uint32_t props(uint32_t n = 0x200) {
        const uint32_t block = alloc(0x10), data = alloc(n * 4);
        w32(block + addr::props::kData, data);
        return block;
    }
    void setProp(uint32_t block, uint32_t id, uint32_t v) {
        const uint32_t data = rd(block + addr::props::kData);
        w32(data + id * 4, v);
    }
    uint32_t rd(uint32_t a) const {
        uint32_t v;
        std::memcpy(&v, &ram[a], 4);
        return v;
    }
    // Resource record; returns its index.
    uint32_t resource(uint32_t cls, uint32_t propsBlock, uint32_t type = 0) {
        const uint32_t ix = resCount++;
        const uint32_t rec = kResTable + ix * addr::res::kRecordSize;
        w32(rec + addr::res::kRecType, type);
        w32(rec + addr::res::kRecClass, cls);
        w32(rec + addr::res::kRecProps, propsBlock);
        w32(kResDb + addr::res::kRecordCount, resCount);
        return ix;
    }
    void addString(uint32_t id, const std::string& text, bool loaded = true) {
        const uint32_t e = alloc(0x20), t = alloc(static_cast<uint32_t>(text.size()) + 1);
        wstr(t, text);
        const uint32_t bucket = kStringMgr + (((id * 0x59) & 0x7f) + 8) * 4;
        w32(e + addr::strings::kEntryId, id);
        w32(e + addr::strings::kEntryFlags, loaded ? 3 : 1);
        w32(e + addr::strings::kEntryNext, rd(bucket));
        w32(e + addr::strings::kEntryText, t);
        w32(bucket, e);
    }

    FakeGame() {
        w32(addr::kContextPtr, kCtx);
        w32(addr::kDecodeKey, kKey);
        w32(kCtx + addr::ctx::kResourceDb, kResDb);
        w32(kResDb + addr::res::kRecordTable, kResTable);
        w32(kCtx + addr::ctx::kStringMgr, kStringMgr);
        resource(0, 0); // index 0 unused

        player = alloc(0x2000);
        ctl = player + addr::player::kController;
        screen = alloc(0x200);
        w32(addr::kTopScreenPtr, screen);
        w32(screen + addr::screen::kId, addr::screen::kInventoryId);
        w32(screen + addr::screen::kPlayer, player);
        w32(screen + addr::screen::kSection, 0);
        // the player actor (on foot)
        actor = alloc(0x100);
        w16(actor + addr::actor::kResIndex, static_cast<uint16_t>(resource(0x10009, props())));
        const uint32_t inst = alloc(0x20);
        w32(actor + addr::actor::kInstance, inst);
        w32(inst + addr::actor::kInstanceData, player);
        w32(kCtx + addr::ctx::kPlayerActor, actor);

        w32(addr::kStatsBlock + addr::stats::kInventoryOpen, 1);
        w32(ctl + addr::ctl::kSelection, 0xffffffffu);
    }

    void setStats(int hp, int hpMax, int mana, int manaMax, int gold, int xp, int level, int points) {
        const uint32_t S = addr::kStatsBlock;
        enc(S + addr::stats::kHp, hp);
        enc(S + addr::stats::kHpMax, hpMax);
        enc(S + addr::stats::kMana, mana);
        enc(S + addr::stats::kManaMax, manaMax);
        enc(S + addr::stats::kGold, gold);
        enc(S + addr::stats::kXp, xp);
        enc(S + addr::stats::kLevel, level);
        enc(S + addr::stats::kSkillPoints, points);
    }

    void setSkills(const std::vector<std::pair<std::string, int>>& skills, int max = 8) {
        const uint32_t t = alloc(static_cast<uint32_t>(skills.size()) * addr::stats::kSkillStride);
        for (size_t i = 0; i < skills.size(); ++i) {
            const uint32_t r = t + static_cast<uint32_t>(i) * addr::stats::kSkillStride;
            w32(r + addr::stats::kSkillId, static_cast<uint32_t>(i + 1));
            w32(r + addr::stats::kSkillLevel, static_cast<uint32_t>(skills[i].second));
            w32(r + addr::stats::kSkillMax, static_cast<uint32_t>(max));
            wstr(r + addr::stats::kSkillName, skills[i].first);
        }
        w32(addr::kStatsBlock + addr::stats::kSkillTable, t);
        w32(addr::kStatsBlock + addr::stats::kSkillCount, static_cast<uint32_t>(skills.size()));
    }

    uint32_t slotRec(int cell) const {
        if (cell == addr::ctl::kSelShopStock)
            return ctl + addr::ctl::kSlotShopStock;
        return ctl + addr::ctl::kSlots + static_cast<uint32_t>(cell) * addr::ctl::kSlotStride;
    }

    struct ItemSpec {
        uint32_t cls = addr::cls::kMelee;
        uint32_t type = addr::actor::kTypeWeapon;
        uint32_t nameId = 0;
        int qty = -1, qtyMax = -1;
        int w = 1, h = 1;
        bool equipped = false, hot = false, drop = true;
        int price = 0, skill = 1, damage = 0;
    };
    // Item actor in grid cell `cell` (or the shop stock record 0x1b); returns the handle.
    uint32_t addItem(int cell, const ItemSpec& s) {
        const uint32_t h = alloc(0x100);
        const uint32_t pb = props();
        setProp(pb, addr::props::kName, s.nameId);
        setProp(pb, addr::props::kPrice, static_cast<uint32_t>(s.price));
        setProp(pb, addr::props::kSkillNeeded, static_cast<uint32_t>(s.skill));
        setProp(pb, addr::props::kMeleeDamage, static_cast<uint32_t>(s.damage));
        setProp(pb, addr::props::kDropAllowed, s.drop ? 1 : 0);
        w32(h + addr::actor::kType, s.type);
        w16(h + addr::actor::kResIndex, static_cast<uint16_t>(resource(s.cls, pb)));
        w32(h + addr::actor::kProps, pb);
        const uint32_t inst = alloc(0x20), data = alloc(0x20);
        w32(h + addr::actor::kInstance, inst);
        w32(inst + addr::actor::kInstanceData, data);
        enc(data + addr::actor::kQtyCur, s.qty);
        enc(data + addr::actor::kQtyMax, s.qtyMax);
        const uint32_t rec = slotRec(cell);
        w32(rec + addr::slot::kEquipped, s.equipped ? 1 : 0);
        w32(rec + addr::slot::kHotSlot, s.hot ? 1 : 0);
        w32(rec + addr::slot::kItem, h);
        for (int dy = 0; dy < s.h; ++dy)
            for (int dx = 0; dx < s.w; ++dx)
                if (dx || dy)
                    w32(slotRec(cell + dx + dy * 8) + addr::slot::kItem, static_cast<uint32_t>(dx * 10 + dy));
        return h;
    }
    void setQuantity(uint32_t handle, int qty) {
        const uint32_t data = rd(rd(handle + addr::actor::kInstance) + addr::actor::kInstanceData);
        enc(data + addr::actor::kQtyCur, qty);
    }
    void clearCell(int cell) { w32(slotRec(cell) + addr::slot::kItem, 0); }

    void setDetails(int selection, const std::vector<std::string>& lines, int pane = 0, bool reqMet = true) {
        w32(ctl + addr::ctl::kSelection, static_cast<uint32_t>(selection));
        w32(ctl + addr::ctl::kPane, static_cast<uint32_t>(pane));
        w32(ctl + addr::ctl::kDetailsValid, 1);
        w32(ctl + addr::ctl::kRequirementMet, reqMet ? 1 : 0);
        for (size_t i = 0; i < std::size(addr::ctl::kDetailLines); ++i)
            wstr(ctl + addr::ctl::kDetailLines[i], i < lines.size() ? lines[i] : std::string(" "));
    }

    // Spells: names[i] empty = name not resident; known[i] = learned level.
    void setSpells(const std::vector<std::string>& names, const std::vector<int>& known, int selected) {
        const uint32_t list = alloc(0x100), owner = alloc(0x100);
        spellOwnerProps = props(0x400);
        w32(player + addr::player::kSpellList, list);
        w32(list + addr::player::kSpellOwner, owner);
        w32(owner + addr::actor::kProps, spellOwnerProps);
        const uint32_t data = rd(spellOwnerProps + addr::props::kData);
        const uint32_t base = 0x1c0;
        w16(data + addr::props::kSpellTypes * 4, static_cast<uint16_t>(base));
        const uint32_t table = alloc(static_cast<uint32_t>(names.size()) * addr::player::kSpellStride);
        for (size_t i = 0; i < names.size(); ++i) {
            const uint32_t typeProps = props(0x10);
            const uint32_t nameId = 0x60000 + static_cast<uint32_t>(i);
            setProp(typeProps, 0, nameId);
            if (!names[i].empty())
                addString(nameId, names[i]);
            w32(data + (base + static_cast<uint32_t>(i)) * 4, resource(0x100ae, typeProps));
            w32(table + static_cast<uint32_t>(i) * addr::player::kSpellStride, static_cast<uint32_t>(known[i]));
        }
        w32(player + addr::player::kSpellTable, table);
        w32(player + addr::player::kSpellCount, static_cast<uint32_t>(names.size()));
        w32(ctl + addr::ctl::kSelectedSpell, static_cast<uint32_t>(selected));
    }

    struct QuestSpec {
        int status = 1;
        bool optional = false;
        uint32_t textId = 0, captionId = 0;
        int x = 0, y = 0, region = 0;
        bool enabled = true;
    };
    void setQuests(const std::vector<QuestSpec>& qs) {
        const uint32_t t = alloc(static_cast<uint32_t>(qs.size()) * addr::quest::kStride);
        for (size_t i = 0; i < qs.size(); ++i) {
            const uint32_t r = t + static_cast<uint32_t>(i) * addr::quest::kStride;
            const uint32_t pb = props(0x10);
            setProp(pb, addr::props::kQuestStatus, static_cast<uint32_t>(qs[i].status));
            setProp(pb, addr::props::kQuestOptional, qs[i].optional ? 1 : 0);
            setProp(pb, addr::props::kQuestText, qs[i].textId);
            setProp(pb, addr::props::kQuestCaption, qs[i].captionId);
            w32(r + addr::quest::kMapX, static_cast<uint32_t>(qs[i].x));
            w32(r + addr::quest::kMapY, static_cast<uint32_t>(qs[i].y));
            w32(r + addr::quest::kEnabled, qs[i].enabled ? 1 : 0);
            w32(r + addr::quest::kRegion, static_cast<uint32_t>(qs[i].region));
            w32(r + addr::quest::kResIndex, resource(0x100f0, pb));
        }
        w32(addr::kQuestTable, t);
        w32(addr::kQuestCount, static_cast<uint32_t>(qs.size()));
    }
    void setRegion(int i, const std::string& name) {
        wstr(addr::kRegionNames + static_cast<uint32_t>(i) * addr::kRegionStride, name);
    }
};

} // namespace gameui::test
