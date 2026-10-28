// Synthetic game state for the strip / map / pause screen tests and the offline preview. Made-up values only.
#pragma once
#include <cstdint>

#include "FakeGame.h"
#include "gameui/Model.h"
#include "gameui/Reader.h"

namespace gameui::test {

// Items: dagger (equipped, cell 0), bow 1x2 (1), club 1x2 (2), elixir (3, quick slot), armour 2x2 (5), arrows (12),
// scroll (16). The bow's details are the native ones. Quests: two main (one done), two optional (one with only a
// caption resident, one with no string at all), two places (one caption not resident), one disabled marker.
struct ScreenWorld {
    FakeGame g;
    uint32_t dagger = 0, bow = 0, club = 0, elixir = 0, armour = 0, arrows = 0, scroll = 0;
    ScreenWorld() {
        g.setStats(84, 100, 70, 100, 646, 250, 3, 2);
        g.setSkills({{"Melee", 2}, {"Archery", 2}, {"Magic", 1}});
        g.addString(0x300ce, "Rynn's Dagger");
        g.addString(0x3000b, "Short Wood Bow");
        g.addString(0x300a0, "Health Elixir");
        g.addString(0x30032, "Wood Club");
        g.addString(0x30040, "Banded Mail");
        g.addString(0x30041, "Wooden Arrows");
        g.addString(0x20016, "Find Lady Myschala at the palace in Surdana.");
        g.addString(0x20017, "Escape from the Wartok camp.");
        g.addString(0x20018, "Help the miller", false); // in the cache, not loaded
        g.addString(0x2006d, "The Palace");
        g.addString(0x2006e, "The Old Mill");
        g.addString(0x2006f, "Surdana Gate");
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
        c.qty = 30;
        c.qtyMax = 150;
        c.h = 2;
        c.damage = 8;
        club = g.addItem(2, c);
        S e;
        e.cls = addr::cls::kHealthElixir;
        e.type = addr::actor::kTypeUsable;
        e.nameId = 0x300a0;
        e.qty = 3;
        e.qtyMax = 5;
        e.hot = true;
        elixir = g.addItem(3, e);
        S a;
        a.cls = addr::cls::kArmourA;
        a.nameId = 0x30040;
        a.qty = 100;
        a.qtyMax = 100;
        a.w = 2;
        a.h = 2;
        armour = g.addItem(5, a);
        S ar;
        ar.cls = addr::cls::kAmmoA;
        ar.type = addr::actor::kTypeUsable;
        ar.nameId = 0x30041;
        ar.qty = 38;
        ar.qtyMax = 99;
        ar.hot = true;
        arrows = g.addItem(12, ar);
        S sc;
        sc.cls = addr::cls::kSpellScroll;
        sc.type = addr::actor::kTypeUsable;
        sc.nameId = 0x300ff; // not in the cache
        scroll = g.addItem(16, sc);
        g.setDetails(
            1, {"Short Wood Bow", "Skill Needed: 1", "Damage: 5", "Wooden Arrows", "Range: Short", "Arrows: 181/200"});
        g.setSpells({"Fireball", "Ice Blast"}, {1, 0}, 0);
        g.setQuests({
            {1, false, 0x20016, 0x2006d, 110, 100, 0, true}, // 0 main, marker
            {2, false, 0x20017, 0, 0, 0, 0, true},           // 1 main, done
            {1, true, 0x20018, 0x2006e, 300, 220, 0, true},  // 2 optional: text not resident -> caption
            {1, true, 0x20019, 0, 0, 0, 0, true},            // 3 optional: nothing resident -> region name
            {0, false, 0, 0x2006f, 40, 380, 0, true},        // 4 place
            {0, false, 0, 0x20070, 500, 60, 0, true},        // 5 place, caption not resident
            {0, false, 0, 0x20071, 10, 10, 0, false},        // 6 disabled marker
        });
        g.setRegion(0, "Surdana");
    }
    Snapshot read(ReaderState& st) {
        Snapshot s;
        const FlatGuestMemory m = g.mem();
        readSnapshot(m, st, s);
        return s;
    }
    Snapshot read() {
        ReaderState st;
        return read(st);
    }
};

} // namespace gameui::test
