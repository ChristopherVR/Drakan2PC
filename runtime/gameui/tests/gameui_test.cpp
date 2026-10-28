// Unit tests for drakan::gameui: reader over a synthetic guest, model exchange, actions, layout and navigation.
// No game data, no window.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "FakeGame.h"
#include "gameui/Actions.h"
#include "gameui/Layout.h"
#include "gameui/Model.h"
#include "gameui/Reader.h"
#include "gameui/Screen.h"

using namespace gameui;
using gameui::test::FakeGame;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        ++g_checks;                                                                                                    \
        if (!(c)) {                                                                                                    \
            ++g_fail;                                                                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);                                                   \
        }                                                                                                              \
    } while (0)

namespace {

// The fixture of the data-model note: dagger (equipped) in 0, bow 1x2 in 1, club 1x2 in 2, elixir in 3, a scroll in
// 16, Fireball known, two quests, Surdana.
struct World {
    FakeGame g;
    uint32_t dagger = 0, bow = 0, club = 0, elixir = 0, scroll = 0;
    World() {
        g.setStats(84, 100, 70, 100, 376, 250, 1, 2);
        g.setSkills({{"Melee", 1}, {"Archery", 2}, {"Magic", 1}});
        g.addString(0x300ce, "Rynn's Dagger");
        g.addString(0x3000b, "Short Wood Bow");
        g.addString(0x300a0, "Health Elixir");
        g.addString(0x20016, "Find Lady Myschala at the palace in Surdana.");
        g.addString(0x2006d, "The Palace");
        g.addString(0x30032, "Wood Club", false); // in the cache but not loaded
        using S = FakeGame::ItemSpec;
        S d;
        d.nameId = 0x300ce;
        d.equipped = true;
        d.drop = false;
        d.damage = 4;
        dagger = g.addItem(0, d);
        S b;
        b.cls = addr::cls::kBow;
        b.nameId = 0x3000b;
        b.qty = 181;
        b.qtyMax = 200;
        b.h = 2;
        b.price = 325;
        bow = g.addItem(1, b);
        S c;
        c.nameId = 0x30032;
        c.qty = 144;
        c.qtyMax = 150;
        c.h = 2;
        c.damage = 8;
        c.price = 205;
        club = g.addItem(2, c);
        S e;
        e.cls = addr::cls::kHealthElixir;
        e.type = addr::actor::kTypeUsable;
        e.nameId = 0x300a0;
        e.qty = 100;
        e.qtyMax = 100;
        e.hot = true;
        elixir = g.addItem(3, e);
        S sc;
        sc.cls = addr::cls::kSpellScroll;
        sc.type = addr::actor::kTypeUsable;
        sc.nameId = 0x300ff; // not in the cache at all
        scroll = g.addItem(16, sc);
        g.setDetails(
            0, {"Rynn's Dagger", "Skill Needed: 1", " ", "Damage: 4", "Speed: Fast", "", " ", " ", "Indestructible"});
        g.setSpells({"Fireball", "Ice Blast", ""}, {1, 0, 0}, 0);
        g.setQuests({{1, false, 0x20016, 0, 0, 0, 0, true}, {0, false, 0, 0x2006d, 110, 100, 0, true}});
        g.setRegion(0, "Surdana");
        g.setRegion(13, "iStringTableSize"); // past the table: must not be read
    }
    Snapshot read(ReaderState& st) {
        Snapshot s;
        const FlatGuestMemory m = g.mem();
        readSnapshot(m, st, s);
        return s;
    }
};

void testCodec() {
    for (uint32_t v : {0u, 1u, 376u, 0xffffffffu, 0x12345678u})
        CHECK(decodeValue(encodeValue(v, FakeGame::kKey), FakeGame::kKey) == v);
    CHECK(decodeValue(0x739acd45u, 0x739acd45u) == 0);
    CHECK(latin1ToUtf8("\xe9t\xe9", 3) == "\xc3\xa9t\xc3\xa9");
}

void testReader() {
    World w;
    ReaderState st;
    Snapshot s = w.read(st);
    CHECK(s.native.valid);
    CHECK(s.native.inventoryOpen && s.native.topScreenId == 0x15 && s.native.section == 0);
    CHECK(s.stats.hp == 84 && s.stats.hpMax == 100 && s.stats.mana == 70 && s.stats.gold == 376);
    CHECK(s.stats.xp == 250 && s.stats.level == 1);
    CHECK(s.skills.unspentPoints == 2 && s.skills.entries.size() == 3);
    CHECK(s.skills.entries[1].name == "Archery" && s.skills.entries[1].level == 2 && s.skills.entries[1].max == 8);

    CHECK(s.items.size() == 5);
    const ItemEntry* d = s.itemByHandle(w.dagger);
    CHECK(d && d->name == "Rynn's Dagger" && d->nameResident && d->equipped && !d->dropAllowed);
    CHECK(d && d->category == ItemCategory::Melee && d->quantity == -1 && d->quantityMax == -1 && d->meleeDamage == 4);
    CHECK(d && d->detailsCurrent && d->details.size() == 5 && d->details[3] == "Speed: Fast");
    CHECK(s.details.size() == 5 && s.details.back() == "Indestructible");
    const ItemEntry* b = s.itemAtCell(9); // continuation cell of the bow
    CHECK(b && b->handle == w.bow && b->originCell == 1 && b->width == 1 && b->height == 2);
    CHECK(b && b->category == ItemCategory::Bow && b->quantity == 181 && b->quantityMax == 200 && b->price == 325);
    CHECK(b && !b->detailsCurrent && b->details.empty());
    const ItemEntry* c = s.itemAtCell(10);
    CHECK(c && c->handle == w.club && !c->nameResident && c->name.empty()); // cached but not loaded
    const ItemEntry* e = s.itemAtCell(3);
    CHECK(e && e->category == ItemCategory::Consumable && e->hotSlot &&
          quantityKindFor(e->category) == QuantityKind::Charges);
    const ItemEntry* sc = s.itemAtCell(16);
    CHECK(sc && sc->category == ItemCategory::SpellScroll && sc->name.empty());
    CHECK(s.cellItem[4] == -1 && s.cellItem[17] == -1 && !s.itemAtCell(23));

    CHECK(s.spells.entries.size() == 3 && s.spells.selected == 0);
    CHECK(s.spells.entries[0].name == "Fireball" && s.spells.entries[0].knownLevel == 1);
    CHECK(s.spells.entries[1].name == "Ice Blast" && s.spells.entries[1].knownLevel == 0);
    CHECK(!s.spells.entries[2].nameResident);

    CHECK(s.quests.size() == 2);
    CHECK(s.quests[0].listed() && s.quests[0].text == "Find Lady Myschala at the palace in Surdana.");
    CHECK(!s.quests[1].listed() && s.quests[1].marker() && s.quests[1].caption == "The Palace");
    CHECK(s.quests[1].mapX == 110 && s.quests[1].mapY == 100);
    CHECK(s.regions.size() == 1 && s.regions[0].name == "Surdana");
    CHECK(!s.shop.active && !s.shop.hasStock);

    // Selecting the club: its details are cached and give it a name; the dagger keeps its cached lines.
    w.g.setDetails(2, {"Wood Club", "Skill Needed: 1", "Damage: 8", "Speed: Medium", "Durability: 144/150"});
    s = w.read(st);
    c = s.itemAtCell(2);
    CHECK(c && c->detailsCurrent && c->name == "Wood Club" && !c->nameResident);
    d = s.itemByHandle(w.dagger);
    CHECK(d && !d->detailsCurrent && d->details.size() == 5);
    CHECK(s.sequence == 2);

    // Busy controller: details are not trusted.
    w.g.w32(w.g.ctl + addr::ctl::kBusy, 1);
    w.g.setDetails(1, {"Garbage"});
    s = w.read(st);
    CHECK(s.native.controllerBusy && s.details.empty());
    CHECK(s.itemByHandle(w.bow) && s.itemByHandle(w.bow)->details.empty());
    w.g.w32(w.g.ctl + addr::ctl::kBusy, 0);

    // Stale buffers (name mismatch) are not cached for the selected item.
    w.g.setDetails(1, {"Rynn's Dagger", "Damage: 4"});
    s = w.read(st);
    CHECK(s.itemByHandle(w.bow) && !s.itemByHandle(w.bow)->detailsCurrent);

    // Spells pane: the details belong to the selected spell.
    w.g.setDetails(0, {"Fireball", "Spell Level: 1", "Mana Cost: 15%"}, 1);
    s = w.read(st);
    CHECK(s.spells.entries[0].details.size() == 3 && s.spells.entries[0].details[2] == "Mana Cost: 15%");
    CHECK(s.itemByHandle(w.dagger)->details.size() == 5);

    // Dropping the dagger forgets its cache entry.
    w.g.clearCell(0);
    s = w.read(st);
    CHECK(!s.itemByHandle(w.dagger) && st.itemDetails.count(w.dagger) == 0);
}

void testShopAndGuards() {
    World w;
    using S = FakeGame::ItemSpec;
    S stock;
    stock.cls = addr::cls::kSpellScroll;
    stock.type = addr::actor::kTypeUsable;
    stock.nameId = 0x3000b;
    const uint32_t h = w.g.addItem(addr::ctl::kSelShopStock, stock);
    w.g.w32(w.g.slotRec(addr::ctl::kSelShopStock), 0x3f800000); // garbage flags as in the dump
    ReaderState st;
    Snapshot s = w.read(st);
    CHECK(!s.shop.active && !s.shop.hasStock); // record 0x1b ignored without a shop
    w.g.w32(w.g.ctl + addr::ctl::kShopMode, 1);
    w.g.w32(w.g.ctl + addr::ctl::kShopSelection, 3);
    s = w.read(st);
    CHECK(s.shop.active && s.shop.hasStock && s.shop.stock.handle == h && s.shop.selection == 3);
    CHECK(s.shop.stock.category == ItemCategory::SpellScroll && !s.shop.stock.equipped);
    CHECK(s.items.size() == 5);

    // Dragon: stats only.
    const uint32_t dragon = w.g.alloc(0x100);
    w.g.w16(dragon + addr::actor::kResIndex, static_cast<uint16_t>(w.g.resource(addr::cls::kDragon, w.g.props())));
    w.g.w32(FakeGame::kCtx + addr::ctx::kPlayerActor, dragon);
    Snapshot s2;
    const FlatGuestMemory m = w.g.mem();
    CHECK(readSnapshot(m, st, s2) == ReadStatus::Dragon);
    CHECK(!s2.native.valid && s2.native.onDragon && s2.stats.gold == 376 && s2.items.empty());

    // No context.
    w.g.w32(addr::kContextPtr, 0);
    CHECK(readSnapshot(m, st, s2) == ReadStatus::NoContext && !s2.native.valid);

    // Player from ctx+0x4b0 when 0x15 is not the top screen.
    World w2;
    w2.g.w32(w2.g.screen + addr::screen::kId, 0x16);
    ReaderState st2;
    Snapshot s3 = w2.read(st2);
    CHECK(s3.native.valid && s3.items.size() == 5 && s3.native.section == -1);
}

void testExchange() {
    ModelExchange x;
    CHECK(!x.acquire());
    x.back().stats.gold = 1;
    Snapshot* first = &x.back();
    x.publish();
    auto a = x.acquire();
    CHECK(a && a->stats.gold == 1 && x.published() == 1);
    x.back().stats.gold = 2;
    x.publish(); // `a` still holds buffer 1: the writer must not get it back
    CHECK(&x.back() != first);
    x.back().stats.gold = 3;
    CHECK(a->stats.gold == 1);
    a.reset();
    x.publish();
    CHECK(x.acquire()->stats.gold == 3);

    // Concurrent writer / reader: every acquired snapshot is internally consistent.
    ModelExchange y;
    bool ok = true;
    std::thread writer([&] {
        for (int i = 1; i <= 2000; ++i) {
            Snapshot& b = y.back();
            b.stats.gold = i;
            b.stats.xp = i * 2;
            y.publish();
        }
    });
    for (int i = 0; i < 2000; ++i)
        if (auto s = y.acquire())
            ok = ok && s->stats.xp == s->stats.gold * 2;
    writer.join();
    CHECK(ok && y.acquire()->stats.gold == 2000);
}

Snapshot step(World& w, ReaderState& st) {
    return w.read(st);
}

void testActions() {
    ActionQueue q;
    q.push({0, ActionKind::SelectItem, 1, 0});
    const uint32_t id = q.push({0, ActionKind::SelectItem, 2, 0});
    q.push({0, ActionKind::UseOrEquip, 2, 0});
    q.push({0, ActionKind::SelectItem, 3, 0});
    CHECK(q.size() == 3);
    auto a = q.pop();
    CHECK(a && a->kind == ActionKind::SelectItem && a->target == 2 && a->id == id);
    {
        // Map selection: the newest queued SelectMarker replaces an older one; confirmed when +0x6c is the record.
        ActionQueue mq;
        mq.push({0, ActionKind::SelectMarker, 1, 0});
        mq.push({0, ActionKind::SelectMarker, 4, 0});
        CHECK(mq.size() == 1 && mq.pop()->target == 4);
        Snapshot ms;
        ms.native.valid = ms.native.inventoryOpen = true;
        ms.native.mapSelection = 1;
        const Expectation e = makeExpectation({1, ActionKind::SelectMarker, 4, 0}, ms);
        CHECK(e.effect == Effect::MarkerIs && evaluate(e, ms) == Outcome::Pending);
        ms.native.mapSelection = 4;
        CHECK(evaluate(e, ms) == Outcome::Confirmed);
    }

    World w;
    ReaderState st;
    Snapshot before = step(w, st);

    // Equip the bow: confirmed when its flag flips.
    ActionTracker t;
    t.begin({7, ActionKind::UseOrEquip, 1, w.bow}, before);
    CHECK(t.busy() && !t.update(step(w, st)));
    w.g.w32(w.g.slotRec(1) + addr::slot::kEquipped, 1);
    auto r = t.update(step(w, st));
    CHECK(r && r->outcome == Outcome::Confirmed && r->id == 7 && !t.busy());

    // Hot slot that never changes: refused after the time-out.
    before = step(w, st);
    t.begin({8, ActionKind::ToggleHotSlot, 2, w.club}, before);
    std::optional<ActionResult> res;
    for (int i = 0; i < 40 && !res; ++i)
        res = t.update(step(w, st));
    CHECK(res && res->outcome == Outcome::Refused);
    // A fast EE (4 snapshots per 60 Hz tick) stretches the time-out: still pending after 40, refused by 15 * 4.
    before = step(w, st);
    t.begin({11, ActionKind::ToggleHotSlot, 2, w.club}, before, 4.0f);
    res.reset();
    int n = 0;
    for (; n < 100 && !res; ++n)
        res = t.update(step(w, st));
    CHECK(res && res->outcome == Outcome::Refused && n >= 59 && n <= 61);

    // Drinking the elixir: quantity change confirms.
    before = step(w, st);
    t.begin({9, ActionKind::UseOrEquip, 3, w.elixir}, before);
    w.g.setQuantity(w.elixir, 50);
    r = t.update(step(w, st));
    CHECK(r && r->outcome == Outcome::Confirmed);

    // Drop: the handle leaves the grid.
    before = step(w, st);
    t.begin({10, ActionKind::Drop, 2, w.club}, before);
    w.g.clearCell(2);
    w.g.clearCell(10);
    r = t.update(step(w, st));
    CHECK(r && r->outcome == Outcome::Confirmed);

    // Buy: a Yes / No box over 0x15 confirms (the purchase itself is answered natively).
    before = step(w, st);
    t.begin({11, ActionKind::Buy, 0, 0}, before);
    const uint32_t box = w.g.alloc(0x200);
    w.g.w32(box + addr::screen::kId, 0x30);
    w.g.w32(addr::kTopScreenPtr, box);
    r = t.update(step(w, st));
    CHECK(r && r->outcome == Outcome::Confirmed);
    w.g.w32(addr::kTopScreenPtr, w.g.screen);

    // Spend a point: points drop by one.
    before = step(w, st);
    t.begin({12, ActionKind::SpendSkillPoint, 0, 0}, before);
    w.g.enc(addr::kStatsBlock + addr::stats::kSkillPoints, 1);
    r = t.update(step(w, st));
    CHECK(r && r->outcome == Outcome::Confirmed);

    // Selection steer.
    before = step(w, st);
    t.begin({13, ActionKind::SelectItem, 16, w.scroll}, before);
    CHECK(!t.update(step(w, st)));
    w.g.w32(w.g.ctl + addr::ctl::kSelection, 16);
    r = t.update(step(w, st));
    CHECK(r && r->outcome == Outcome::Confirmed);

    // Screen closed under an equip: aborted, not refused.
    before = step(w, st);
    t.begin({14, ActionKind::UseOrEquip, 16, w.scroll}, before);
    w.g.w32(addr::kStatsBlock + addr::stats::kInventoryOpen, 0);
    r = t.update(step(w, st));
    CHECK(r && r->outcome == Outcome::Aborted);
    // Close: confirmed by the same flag.
    t.begin({15, ActionKind::Close, -1, 0}, before);
    r = t.update(step(w, st));
    CHECK(r && r->outcome == Outcome::Confirmed);

    ResultQueue rq;
    rq.push({1, ActionKind::Drop, 2, Outcome::Refused});
    auto rr = rq.pop();
    CHECK(rr && rr->outcome == Outcome::Refused && !rq.pop());
}

void testCompare() {
    ItemEntry club, dagger;
    club.category = dagger.category = ItemCategory::Melee;
    club.details = {"Wood Club", "Skill Needed: 2", "Damage: 8", "Speed: Medium", "Durability: 144/150"};
    dagger.details = {"Rynn's Dagger", "Skill Needed: 1", "Damage: 4", "Speed: Fast", "Indestructible"};
    const auto d = compareItems(club, dagger);
    auto find = [&](const char* l) -> const StatDelta* {
        for (const StatDelta& x : d)
            if (x.label == l)
                return &x;
        return nullptr;
    };
    CHECK(find("Damage") && find("Damage")->better == 1 && find("Damage")->equipped == "4");
    CHECK(find("Skill Needed") && find("Skill Needed")->better == -1);
    CHECK(find("Speed") && find("Speed")->better == -1);
    CHECK(find("Durability") && find("Durability")->equipped.empty() && find("Durability")->better == 0);
    // property fallback when no details are cached yet
    ItemEntry a, b;
    a.category = b.category = ItemCategory::Melee;
    a.meleeDamage = 8;
    b.meleeDamage = 4;
    const auto p = compareItems(a, b);
    CHECK(!p.empty() && p[0].label == "Damage" && p[0].better == 1);
    std::string l, v;
    CHECK(splitStatLine("Arrows: 200/200", l, v) && l == "Arrows" && v == "200/200");
    CHECK(!splitStatLine("Indestructible", l, v));
    CHECK(categoryFor(0x12345, 10) == ItemCategory::Melee && categoryFor(0x12345, 99) == ItemCategory::Other);
    CHECK(matchesFilter(ItemCategory::Bow, CategoryFilter::Weapons) &&
          !matchesFilter(ItemCategory::Bow, CategoryFilter::Consumables));
}

void testQuickSlotOrder() {
    // The native bar's order is column by column (33fa20): cells 0, 16, 1, 9, 7 for these hot items; 2 is not hot.
    Snapshot s;
    for (int cell : {9, 1, 16, 0, 2, 7}) {
        ItemEntry e;
        e.slot = e.originCell = cell;
        e.hotSlot = cell != 2;
        s.items.push_back(e);
    }
    std::sort(s.items.begin(), s.items.end(), [](const ItemEntry& a, const ItemEntry& b) { return a.slot < b.slot; });
    const auto q = quickSlotItems(s);
    CHECK(q.size() == 5);
    const int want[] = {0, 16, 1, 9, 7};
    for (size_t i = 0; i < q.size() && i < 5; ++i)
        CHECK(q[i]->originCell == want[i]);
    for (const ItemEntry& e : s.items) {
        if (e.originCell == 9)
            CHECK(quickSlotNumber(s, e) == 4);
        if (e.originCell == 2)
            CHECK(quickSlotNumber(s, e) == 0);
    }
    ItemEntry stock; // the shop stock record (0x1b) never counts
    stock.slot = stock.originCell = 0x1b;
    stock.hotSlot = true;
    s.items.push_back(stock);
    CHECK(quickSlotItems(s).size() == 5);
}

void testLayout() {
    LayoutFrame f = computeFrame({0, 0, 1920, 1080});
    CHECK(f.scale == 1 && f.ox == 0 && f.oy == 0);
    f = computeFrame({0, 0, 3840, 2160});
    CHECK(f.scale == 2 && f.map({10, 10, 5, 5}) == Rect({20, 20, 10, 10}));
    f = computeFrame({0, 0, 5120, 1440}); // 32:9: pillarboxed safe area, bands span everything
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.01f; };
    CHECK(near(f.scale, 1440.0f / 1080.0f) && near(f.ox, (5120 - 2560) / 2.0f) && f.oy == 0);
    CHECK(f.band(0, 100).w == 5120);
    f = computeFrame({100, 50, 1024, 768}); // 4:3 in an offset viewport: letterboxed
    CHECK(near(f.scale, 1024.0f / 1920.0f) && near(f.ox, 100) && near(f.oy, 50 + (768 - 576) / 2.0f));

    auto lines = wrapText("Find Lady Myschala at the palace in Surdana.", 200, FontRole::Body, 20, {});
    CHECK(lines.size() >= 3);
    for (const std::string& l : lines)
        CHECK(estimateTextWidth(l, FontRole::Body, 20) <= 200 || l.find(' ') == std::string::npos);
    CHECK(wrapText("a\nb", 1000, FontRole::Body, 20, {}).size() == 2);
    const std::string e = ellipsize("A very long item name indeed", 100, FontRole::Body, 20, {});
    CHECK(e.size() > 3 && e.substr(e.size() - 3) == "..." && estimateTextWidth(e, FontRole::Body, 20) <= 100);
    CHECK(ellipsize("Short", 1000, FontRole::Body, 20, {}) == "Short");
}

UiInput keys(std::initializer_list<UiKey> k) {
    UiInput in;
    in.keys = k;
    return in;
}

bool hasAction(const FrameOutput& o, ActionKind k, int target = -999) {
    return std::any_of(o.actions.begin(), o.actions.end(),
                       [&](const UiAction& a) { return a.kind == k && (target == -999 || a.target == target); });
}

bool hasText(const DrawList& d, const std::string& t) {
    return std::any_of(d.commands().begin(), d.commands().end(),
                       [&](const DrawCmd& c) { return c.kind == DrawKind::Text && c.text == t; });
}

void testScreenNavigation() {
    World w;
    ReaderState st;
    Snapshot s = w.read(st);
    InventoryScreen scr;
    const Viewport vp{0, 0, 2560, 1440};
    CHECK(scr.frame(s, {}, vp).draw.size() == 0); // closed
    scr.open(s);
    FrameOutput o = scr.frame(s, {}, vp);
    CHECK(scr.isOpen() && scr.page() == Page::Inventory);
    CHECK(scr.focus() == TargetId({TargetKind::GridCell, 0})); // first item
    CHECK(o.draw.size() > 50 && hasText(o.draw, "Rynn's Dagger") && hasText(o.draw, "Inventory"));
    CHECK(!hasAction(o, ActionKind::SelectItem)); // the dagger already has current details

    // Every command lies inside the viewport.
    bool inside = true;
    for (const DrawCmd& c : o.draw.commands())
        if (c.kind != DrawKind::Line && c.kind != DrawKind::ClipPop)
            inside =
                inside && c.rect.x >= -1 && c.rect.y >= -1 && c.rect.right() <= vp.w + 1 && c.rect.bottom() <= vp.h + 1;
    CHECK(inside);

    // Right -> the bow (1x2), steer its selection once.
    o = scr.frame(s, keys({UiKey::Right}), vp);
    CHECK(scr.focus() == TargetId({TargetKind::GridCell, 1}));
    CHECK(hasAction(o, ActionKind::SelectItem, 1) && o.actions[0].handle == w.bow);
    o = scr.frame(s, {}, vp);
    CHECK(!hasAction(o, ActionKind::SelectItem));
    // Down from the bow (rows 0-1) lands in row 2 below it.
    o = scr.frame(s, keys({UiKey::Down}), vp);
    CHECK(scr.focus() == TargetId({TargetKind::GridCell, 17}));
    o = scr.frame(s, keys({UiKey::Up, UiKey::Left}), vp);
    CHECK(scr.focus() == TargetId({TargetKind::GridCell, 0}));

    // Confirm on the equipped dagger does nothing; on the bow it equips.
    o = scr.frame(s, keys({UiKey::Confirm}), vp);
    CHECK(!hasAction(o, ActionKind::UseOrEquip));
    o = scr.frame(s, keys({UiKey::Right, UiKey::Confirm}), vp);
    CHECK(hasAction(o, ActionKind::UseOrEquip, 1));
    // Drop allowed for the bow, not for the dagger; hot slot.
    o = scr.frame(s, keys({UiKey::Secondary, UiKey::Tertiary}), vp);
    CHECK(hasAction(o, ActionKind::Drop, 1) && hasAction(o, ActionKind::ToggleHotSlot, 1));
    o = scr.frame(s, keys({UiKey::Left, UiKey::Secondary}), vp);
    CHECK(!hasAction(o, ActionKind::Drop));

    // Filter: Consumables -> only the elixir and the scroll are reachable; focus moves onto them.
    o = scr.frame(s, keys({UiKey::FilterNext, UiKey::FilterNext, UiKey::FilterNext}), vp);
    CHECK(scr.filter() == CategoryFilter::Consumables);
    CHECK(scr.focus() == TargetId({TargetKind::GridCell, 3}));
    for (const HitTarget& t : scr.targets())
        if (t.id.kind == TargetKind::GridCell && t.id.index == 0)
            CHECK(!t.navigable);
    o = scr.frame(s, keys({UiKey::FilterPrev, UiKey::FilterPrev, UiKey::FilterPrev}), vp);
    CHECK(scr.filter() == CategoryFilter::All);

    // Pages: next = Character (section 0, no switch), next = Spells (switch to section 1), wrap backwards to Map.
    o = scr.frame(s, keys({UiKey::PageNext}), vp);
    CHECK(scr.page() == Page::Character && !hasAction(o, ActionKind::SwitchSection));
    CHECK(!scr.previewRect().empty());
    CHECK(std::any_of(o.draw.commands().begin(), o.draw.commands().end(),
                      [](const DrawCmd& c) { return c.kind == DrawKind::WorldView; }));
    CHECK(scr.focus().kind == TargetKind::EquipSlot);
    CHECK(o.sound == UiSound::Page);
    // Character: the unequipped weapons are listed under the cards (navigable grid targets) for comparison.
    CHECK(std::any_of(scr.targets().begin(), scr.targets().end(),
                      [](const HitTarget& t) { return t.id.kind == TargetKind::GridCell && t.navigable; }));
    // Item icons carry the category silhouette only as their fallback (drawn when no crop exists).
    CHECK(std::any_of(o.draw.commands().begin(), o.draw.commands().end(), [](const DrawCmd& c) {
        return c.kind == DrawKind::Image && c.image.kind == ImageKind::ItemIcon &&
               c.fallback.kind == ImageKind::CategoryIcon;
    }));
    o = scr.frame(s, keys({UiKey::Right}), vp); // from the Weapon card into its pack column
    CHECK(scr.focus().kind == TargetKind::GridCell && o.sound == UiSound::Move);
    o = scr.frame(s, keys({UiKey::Left}), vp);
    CHECK(scr.focus().kind == TargetKind::EquipSlot);
    o = scr.frame(s, keys({UiKey::PageNext}), vp);
    CHECK(scr.page() == Page::Spells && hasAction(o, ActionKind::SwitchSection, 1));
    CHECK(scr.focus() == TargetId({TargetKind::Spell, 0}) && scr.previewRect().empty());
    // Focus selects natively (here it already is the selection); Confirm summons.
    o = scr.frame(s, keys({UiKey::Confirm}), vp);
    CHECK(hasAction(o, ActionKind::CastOrSummon, 0) && !hasAction(o, ActionKind::SelectSpell));
    o = scr.frame(s, keys({UiKey::Down}), vp); // Ice Blast not learned: not focusable
    CHECK(scr.focus() == TargetId({TargetKind::Spell, 0}) && !hasAction(o, ActionKind::SelectSpell));

    // Skills: Right spends, Left returns, Down moves.
    o = scr.frame(s, keys({UiKey::PageNext}), vp);
    CHECK(scr.page() == Page::Skills && scr.focus() == TargetId({TargetKind::Skill, 0}));
    o = scr.frame(s, keys({UiKey::Right, UiKey::Left, UiKey::Down}), vp);
    CHECK(hasAction(o, ActionKind::SpendSkillPoint, 0) && hasAction(o, ActionKind::ReturnSkillPoint, 0));
    CHECK(scr.focus() == TargetId({TargetKind::Skill, 1}));

    // Journal shows the listed quest; Map lists the marker.
    o = scr.frame(s, keys({UiKey::PageNext}), vp);
    CHECK(scr.page() == Page::Journal && scr.focus() == TargetId({TargetKind::Quest, 0}));
    CHECK(hasText(o.draw, "PRIMARY QUESTS") && hasText(o.draw, "Surdana"));
    // The game's map selection (another record) is steered to the focused marker, once per focus.
    s.native.section = 3;
    s.native.mapSelection = 0;
    o = scr.frame(s, keys({UiKey::PageNext}), vp);
    CHECK(scr.page() == Page::Map && scr.focus() == TargetId({TargetKind::Marker, 1}));
    CHECK(hasText(o.draw, "The Palace") && hasAction(o, ActionKind::SelectMarker, 1));
    o = scr.frame(s, {}, vp);
    CHECK(!hasAction(o, ActionKind::SelectMarker));
    scr.onResult({1, ActionKind::SelectMarker, 1, Outcome::Refused});
    CHECK(scr.toast().empty());
    s.native.section = -1;
    s.native.mapSelection = -1;
    o = scr.frame(s, keys({UiKey::PageNext}), vp);
    CHECK(scr.page() == Page::Inventory && scr.focus() == TargetId({TargetKind::GridCell, 0})); // remembered

    // Refusal toast.
    scr.onResult({1, ActionKind::Drop, 1, Outcome::Refused});
    o = scr.frame(s, {}, vp);
    CHECK(!scr.toast().empty() && hasText(o.draw, scr.toast()));

    // Close.
    o = scr.frame(s, keys({UiKey::Back}), vp);
    CHECK(o.wantsClose && hasAction(o, ActionKind::Close) && !scr.isOpen());
}

void testScreenMouse() {
    World w;
    ReaderState st;
    Snapshot s = w.read(st);
    InventoryScreen scr;
    const Viewport vp{0, 0, 1920, 1080};
    scr.open(s);
    scr.frame(s, {}, vp);
    // Hover the elixir: focus follows; a single click only focuses, a double click uses.
    const Rect cell = design::gridCell(3);
    UiInput in;
    in.pointerValid = in.pointerMoved = true;
    in.pointerX = cell.cx();
    in.pointerY = cell.cy();
    FrameOutput o = scr.frame(s, in, vp);
    CHECK(scr.focus() == TargetId({TargetKind::GridCell, 3}) && scr.hover() == scr.focus());
    in.pointerMoved = false;
    in.leftClick = true;
    o = scr.frame(s, in, vp);
    CHECK(!hasAction(o, ActionKind::UseOrEquip));
    in.doubleClick = true;
    o = scr.frame(s, in, vp);
    CHECK(hasAction(o, ActionKind::UseOrEquip, 3));
    // Click a page tab.
    const Rect tab = design::pageTab(2, 6); // Inventory, Character, Spells, ... (no shop)
    in = {};
    in.pointerValid = in.leftClick = true;
    in.pointerX = tab.cx();
    in.pointerY = tab.cy();
    o = scr.frame(s, in, vp);
    CHECK(scr.page() == Page::Spells);
    // Click the Back hint in the footer.
    const HitTarget* back = nullptr;
    for (const HitTarget& t : scr.targets())
        if (t.id == TargetId{TargetKind::Hint, static_cast<int>(Prompt::Back)})
            back = &t;
    CHECK(back != nullptr);
    if (back) {
        in = {};
        in.pointerValid = in.leftClick = true;
        in.pointerX = back->rect.cx();
        in.pointerY = back->rect.cy();
        o = scr.frame(s, in, vp);
        CHECK(o.wantsClose);
    }
    // Right click closes.
    scr.open(s);
    in = {};
    in.rightClick = true;
    o = scr.frame(s, in, vp);
    CHECK(o.wantsClose && !scr.isOpen());
}

void testScreenShopAndGuards() {
    World w;
    using S = FakeGame::ItemSpec;
    S stock;
    stock.cls = addr::cls::kSpellScroll;
    stock.type = addr::actor::kTypeUsable;
    stock.nameId = 0x3000b;
    w.g.addItem(addr::ctl::kSelShopStock, stock);
    w.g.w32(w.g.ctl + addr::ctl::kShopMode, 1);
    ReaderState st;
    Snapshot s = w.read(st);
    InventoryScreen scr;
    const Viewport vp{0, 0, 1920, 1080};
    scr.open(s);
    FrameOutput o = scr.frame(s, {}, vp);
    CHECK(scr.page() == Page::Shop && scr.focus() == TargetId({TargetKind::ShopStock, 0}));
    CHECK(hasText(o.draw, "Merchant"));
    o = scr.frame(s, keys({UiKey::Confirm, UiKey::FilterNext}), vp);
    CHECK(hasAction(o, ActionKind::Buy) && hasAction(o, ActionKind::ShopBrowse, 1));
    o = scr.frame(s, keys({UiKey::Right, UiKey::Up, UiKey::Confirm}), vp); // card -> empty cell 8 -> dagger
    CHECK(scr.focus() == TargetId({TargetKind::GridCell, 0}) && hasAction(o, ActionKind::Sell, 0));
    // No hot slot in a shop.
    o = scr.frame(s, keys({UiKey::PageNext, UiKey::Tertiary}), vp);
    CHECK(scr.page() == Page::Inventory && !hasAction(o, ActionKind::ToggleHotSlot));

    // Invalid snapshot: a message, Back still closes.
    Snapshot bad;
    bad.native.onDragon = true;
    o = scr.frame(bad, {}, vp);
    CHECK(hasText(o.draw, "The inventory can't be opened while riding Arokh."));
    o = scr.frame(bad, keys({UiKey::Back}), vp);
    CHECK(o.wantsClose);
}

// The key that opened the screen (held, late key-down message, auto-repeat) does not count until it was released.
void testKeyGate() {
    KeyGate g;
    constexpr int kTab = 512, kEsc = 513, kPadBack = 600;
    g.reset();
    // Tab still held from the open: its press (a late WM_KEYDOWN or a repeat after the overlay cleared its key state)
    // is ignored, and so are repeats while it stays down.
    CHECK(!g.accept(kTab, true, true));
    CHECK(!g.armed(kTab));
    CHECK(!g.accept(kTab, true, true)); // auto-repeat
    CHECK(!g.accept(kTab, true, false));
    // Released: armed, the release itself is no press.
    CHECK(!g.accept(kTab, false, false) && g.armed(kTab));
    // A new press counts.
    CHECK(g.accept(kTab, true, true));
    // Keys up at the open are armed at once.
    CHECK(g.accept(kEsc, false, false) == false && g.armed(kEsc));
    CHECK(g.accept(kEsc, true, true));
    // Polled pad buttons follow the same rule: Select held through the open does not close.
    CHECK(!g.accept(kPadBack, true, true));
    CHECK(!g.accept(kPadBack, false, false));
    CHECK(g.accept(kPadBack, true, true));
    // Reopening forgets everything: Tab held again is held over again.
    g.reset();
    CHECK(!g.armed(kTab) && !g.accept(kTab, true, true));
    // Out-of-range ids pass through.
    CHECK(g.accept(-1, true, true) && g.accept(KeyGate::kKeys, true, true));
}

void setAnimation(World& w, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    w.g.w32(w.g.screen + addr::screen::kAnimSpeed, bits);
}

// A close asked for during the open animation waits for it (never dropped); ignored taps are repeated; a screen that
// never closes ends in Failed (the glue falls back to the native page).
void testCloseExecutor() {
    World w;
    ReaderState st;
    setAnimation(w, 1.0f); // opening
    Snapshot s = w.read(st);
    CHECK(s.native.animation > 0);
    CloseExecutor c;
    c.begin();
    for (int i = 0; i < 50; ++i)
        CHECK(c.update(w.read(st)) == CloseExecutor::Step::Wait);
    CHECK(c.deferredFrames() == 50 && c.taps() == 0 && c.active());
    setAnimation(w, 0.0f); // open animation over: tap once the controller has been idle for idleFrames snapshots
    CHECK(c.update(w.read(st)) == CloseExecutor::Step::Wait && c.update(w.read(st)) == CloseExecutor::Step::Wait);
    CHECK(c.update(w.read(st)) == CloseExecutor::Step::Tap && c.taps() == 1);
    setAnimation(w, -2.0f); // 431d10 started the close animation
    CHECK(c.update(w.read(st)) == CloseExecutor::Step::Wait);
    w.g.w32(addr::kStatsBlock + addr::stats::kInventoryOpen, 0);
    CHECK(c.update(w.read(st)) == CloseExecutor::Step::Done && !c.active());

    // Ignored taps: repeated after tapWait, Failed after maxTaps.
    World w2;
    ReaderState st2;
    CloseExecutor::Limits lim;
    lim.tapWait = 5;
    lim.maxTaps = 2;
    CloseExecutor c2(lim);
    c2.begin();
    int taps = 0;
    CloseExecutor::Step last = CloseExecutor::Step::Wait;
    for (int i = 0; i < 40 && last != CloseExecutor::Step::Failed; ++i) {
        last = c2.update(w2.read(st2));
        taps += last == CloseExecutor::Step::Tap;
    }
    CHECK(taps == 2 && last == CloseExecutor::Step::Failed && !c2.active());

    // A box over the screen: the close waits without spending taps.
    World w3;
    ReaderState st3;
    CloseExecutor c3;
    c3.begin();
    const uint32_t box = w3.g.alloc(0x200);
    w3.g.w32(box + addr::screen::kId, 0x30);
    w3.g.w32(addr::kTopScreenPtr, box);
    CHECK(c3.update(w3.read(st3)) == CloseExecutor::Step::Wait && c3.taps() == 0);
    w3.g.w32(addr::kTopScreenPtr, w3.g.screen);
    CloseExecutor::Step s3 = CloseExecutor::Step::Wait;
    for (int i = 0; i < 5 && s3 == CloseExecutor::Step::Wait; ++i)
        s3 = c3.update(w3.read(st3));
    CHECK(s3 == CloseExecutor::Step::Tap && c3.taps() == 1);

    // A busy controller (a selection change in flight): no tap until it has been idle for idleFrames snapshots; a
    // controller that never settles gets the tap after busyWait.
    World w5;
    ReaderState st5;
    CloseExecutor c5;
    c5.begin();
    w5.g.w32(w5.g.ctl + addr::ctl::kBusy, 1);
    for (int i = 0; i < 20; ++i)
        CHECK(c5.update(w5.read(st5)) == CloseExecutor::Step::Wait);
    w5.g.w32(w5.g.ctl + addr::ctl::kBusy, 0);
    CHECK(c5.update(w5.read(st5)) == CloseExecutor::Step::Wait && c5.update(w5.read(st5)) == CloseExecutor::Step::Wait);
    CHECK(c5.update(w5.read(st5)) == CloseExecutor::Step::Tap && c5.busyFrames() == 22);
    // A screen whose controller was already idle before the close was asked for taps at once; a busy controller
    // still waits whatever was seen before.
    World w7;
    ReaderState st7;
    CloseExecutor c7;
    c7.begin(2);
    CHECK(c7.update(w7.read(st7)) == CloseExecutor::Step::Tap && c7.busyFrames() == 0);
    CloseExecutor c8;
    c8.begin(5);
    w7.g.w32(w7.g.ctl + addr::ctl::kBusy, 1);
    CHECK(c8.update(w7.read(st7)) == CloseExecutor::Step::Wait);
    CloseExecutor::Limits lim6;
    lim6.busyWait = 8;
    CloseExecutor c6(lim6);
    c6.begin();
    World w6;
    ReaderState st6;
    w6.g.w32(w6.g.ctl + addr::ctl::kBusy, 1);
    int waits = 0;
    while (c6.update(w6.read(st6)) == CloseExecutor::Step::Wait && waits < 50)
        ++waits;
    CHECK(waits == 8);

    // Endless open animation: Failed after openWait.
    World w4;
    ReaderState st4;
    setAnimation(w4, 1.0f);
    CloseExecutor::Limits lim4;
    lim4.openWait = 10;
    CloseExecutor c4(lim4);
    c4.begin();
    last = CloseExecutor::Step::Wait;
    int n = 0;
    while (last == CloseExecutor::Step::Wait && n < 100) {
        last = c4.update(w4.read(st4));
        ++n;
    }
    CHECK(last == CloseExecutor::Step::Failed && n == 11);
}

// The host screen closes itself on Back; a Close the game refused brings it back where it was.
void testScreenCloseRefused() {
    World w;
    ReaderState st;
    Snapshot s = w.read(st);
    InventoryScreen scr;
    const Viewport vp{0, 0, 1920, 1080};
    scr.open(s);
    scr.frame(s, keys({UiKey::PageNext}), vp);
    CHECK(scr.page() == Page::Character);
    const Rect preview = scr.previewRect();
    FrameOutput o = scr.frame(s, keys({UiKey::Close}), vp);
    CHECK(o.wantsClose && !scr.isOpen() && scr.closePending());
    CHECK(scr.previewRect() == preview && !preview.empty()); // kept through the close animation
    scr.onResult({1, ActionKind::Close, -1, Outcome::Refused});
    CHECK(scr.isOpen() && !scr.closePending() && scr.page() == Page::Character);
    o = scr.frame(s, {}, vp);
    CHECK(o.draw.size() > 0 && !o.wantsClose);
    // Confirmed: stays closed.
    o = scr.frame(s, keys({UiKey::Back}), vp);
    scr.onResult({2, ActionKind::Close, -1, Outcome::Confirmed});
    CHECK(!scr.isOpen() && !scr.closePending() && scr.previewRect().empty());
    // A late refusal for a screen that was not waiting does not reopen it.
    scr.onResult({3, ActionKind::Close, -1, Outcome::Refused});
    CHECK(!scr.isOpen());
}

} // namespace

int main() {
    testCodec();
    testReader();
    testShopAndGuards();
    testExchange();
    testActions();
    testCompare();
    testQuickSlotOrder();
    testLayout();
    testScreenNavigation();
    testScreenMouse();
    testScreenShopAndGuards();
    testKeyGate();
    testCloseExecutor();
    testScreenCloseRefused();
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
