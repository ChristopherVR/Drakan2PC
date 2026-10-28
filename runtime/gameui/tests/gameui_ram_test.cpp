// Data-dependent reader test over the local RAM dumps of docs/journal/2026-10-24/tab-rework-data-model.md
// (game-derived, under build/, never committed). Exits 77 (ctest: skipped) when none of them is present.
//   gameui_ram_test                    the default dumps below, relative to the repository root
//   gameui_ram_test <dump.ram> ...     generic invariants only, for any other dump
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "gameui/Reader.h"

#ifndef GAMEUI_REPO_ROOT
#define GAMEUI_REPO_ROOT "."
#endif

using namespace gameui;

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

bool load(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return out.size() >= 0x2000000;
}

void generic(const Snapshot& s) {
    CHECK(s.native.valid);
    CHECK(s.stats.hpMax > 0 && s.stats.hp >= 0 && s.stats.hp <= s.stats.hpMax);
    CHECK(s.stats.level >= 1 && s.stats.gold >= 0);
    CHECK(s.skills.entries.size() == 3);
    for (const ItemEntry& e : s.items) {
        CHECK(e.originCell >= 0 && e.originCell < 24 && e.width >= 1 && e.height >= 1);
        CHECK(e.originCell % 8 + e.width <= 8 && e.originCell / 8 + e.height <= 3);
        CHECK(e.objectType == 10 || e.objectType == 11);
    }
    int equipped = 0;
    for (const ItemEntry& e : s.items)
        equipped += e.equipped && e.category == ItemCategory::Melee;
    CHECK(equipped <= 1);
}

const ItemEntry* at(const Snapshot& s, int cell) {
    return s.itemAtCell(cell);
}

void fixture(const Snapshot& s, bool bowEquipped) {
    CHECK(s.native.inventoryOpen && s.native.topScreenId == 0x15);
    CHECK(s.stats.hp == 100 && s.stats.hpMax == 100 && s.stats.gold == 0 && s.stats.level == 1);
    CHECK(s.skills.entries[0].name == "Melee" && s.skills.entries[2].name == "Magic" && s.skills.entries[0].max == 8);
    CHECK(s.items.size() == 2);
    const ItemEntry* d = at(s, 0);
    const ItemEntry* b = at(s, 9);
    CHECK(d && d->name == "Rynn's Dagger" && d->category == ItemCategory::Melee && d->quantity == -1);
    CHECK(d && d->equipped == !bowEquipped && d->meleeDamage == 4 && !d->dropAllowed);
    CHECK(b && b->originCell == 1 && b->height == 2 && b->name == "Short Wood Bow");
    CHECK(b && b->category == ItemCategory::Bow && b->quantity == 200 && b->quantityMax == 200 && b->price == 325);
    CHECK(b && b->equipped == bowEquipped);
    CHECK(s.native.selection == (bowEquipped ? 1 : 0));
    if (!bowEquipped)
        CHECK(d && d->detailsCurrent && d->details.size() >= 4 && d->details[2] == "Damage: 4");
    CHECK(s.spells.entries.size() == 10);
    CHECK(s.quests.size() == 2 && s.quests[1].listed() && s.quests[1].text.rfind("Find Lady Myschala", 0) == 0);
    CHECK(s.quests[0].caption == "The Palace" && !s.quests[0].listed());
    CHECK(s.regions.size() == 1 && s.regions[0].name == "Surdana");
}

void magic5(const Snapshot& s, const ReaderState& st) {
    CHECK(s.stats.gold == 376 && s.stats.hp == 84 && s.stats.xp == 250);
    CHECK(s.shop.active && s.shop.hasStock && s.shop.stock.category == ItemCategory::SpellScroll);
    CHECK(s.items.size() == 4);
    const ItemEntry* bow = at(s, 8);
    const ItemEntry* club = at(s, 9);
    const ItemEntry* elixir = at(s, 2);
    const ItemEntry* dagger = at(s, 16);
    CHECK(bow && bow->originCell == 0 && bow->quantity == 181 && bow->quantityMax == 200);
    CHECK(club && club->originCell == 1 && club->name == "Wood Club" && club->quantity == 144);
    CHECK(club && club->quantityMax == 150 && club->meleeDamage == 8);
    CHECK(elixir && elixir->category == ItemCategory::Consumable && elixir->name == "Health Elixir");
    CHECK(dagger && dagger->name == "Rynn's Dagger");
    CHECK(s.native.pane == 1 && s.spells.selected == 0);
    CHECK(s.spells.entries.size() == 10 && s.spells.entries[0].name == "Fireball" &&
          s.spells.entries[0].knownLevel == 1);
    CHECK(s.spells.entries[0].details.size() >= 3 && st.spellDetails.count(0) == 1);
    CHECK(s.quests.size() == 6);
}

} // namespace

int main(int argc, char** argv) {
    const std::string root = GAMEUI_REPO_ROOT;
    const std::string fixtureDir = root + "/build/combined-build06-private/t/build06-final-native32/";
    struct Case {
        std::string path;
        int kind; // 0 generic, 1 fixture dagger, 2 fixture bow, 3 magic5 shop
    };
    std::vector<Case> cases;
    if (argc > 1) {
        for (int i = 1; i < argc; ++i)
            cases.push_back({argv[i], 0});
    } else {
        cases = {{fixtureDir + "ram_mouse_item0.ram", 1},
                 {fixtureDir + "ram_mouse_item1.ram", 2},
                 {fixtureDir + "ram_cat_journal.ram", 0},
                 {root + "/build/harness/magic5/ram_k0.ram", 3}};
    }
    int ran = 0;
    std::vector<uint8_t> ram;
    for (const Case& c : cases) {
        if (!load(c.path, ram)) {
            std::printf("skip %s (missing)\n", c.path.c_str());
            continue;
        }
        ++ran;
        const FlatGuestMemory m(ram.data(), ram.size());
        ReaderState st;
        Snapshot s;
        const ReadStatus rs = readSnapshot(m, st, s);
        CHECK(rs == ReadStatus::Ok);
        generic(s);
        if (c.kind == 1 || c.kind == 2)
            fixture(s, c.kind == 2);
        if (c.kind == 3)
            magic5(s, st);
        std::printf("%s: %zu items, %zu spells, %zu quests, gold %d\n", c.path.c_str(), s.items.size(),
                    s.spells.entries.size(), s.quests.size(), s.stats.gold);
    }
    if (ran == 0) {
        std::printf("no RAM dumps found: skipped\n");
        return 77;
    }
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
