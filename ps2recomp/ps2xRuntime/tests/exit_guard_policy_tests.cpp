#include "../src/lib/drakan_exit_guard.h"
#include <cassert>
#include <iostream>

using drakan_exit_guard::ArrivalGuard;

int main() {
    // Disc levels are never guarded.
    {
        ArrivalGuard g;
        g.levelStart(false, 0, 0);
        assert(!g.blocks());
        assert(!g.tick(true, false, 0, 0));
    }
    // Under the loading screen right after the level starts (the arrival box already overlaps the player): ignored.
    {
        ArrivalGuard g;
        g.levelStart(true, 0, 0);
        assert(g.blocks());
        g.tick(false, true, 0, 0);
        assert(g.blocks());
        g.tick(false, false, 0, 0);
        assert(!g.blocks());
    }
    // Standing on the spot (the start / arrival box): touches are ignored; walking off lets go.
    {
        ArrivalGuard g;
        g.levelStart(true, 100, 100);
        for (int i = 0; i < ArrivalGuard::kSettleTicks + 50; ++i)
            assert(!g.tick(true, false, 100 + (i % 3), 100));
        assert(g.blocks());
        assert(!g.tick(true, false, 100 + 1400, 100)); // still within the walk-off distance
        assert(g.blocks());
        assert(g.tick(true, false, 100 + 1600, 100));
        assert(!g.blocks());
        assert(!g.tick(true, false, 100, 100)); // back in the box: a real walk-in now counts
        assert(!g.blocks());
    }
    // The respawn lands a few ticks late: the spot is re-taken while settling.
    {
        ArrivalGuard g;
        g.levelStart(true, 0, 0);
        g.tick(true, false, 5000, 5000);
        for (int i = 0; i < ArrivalGuard::kSettleTicks; ++i)
            g.tick(true, false, 5000, 5000);
        assert(g.blocks());
    }
    // A cut scene moves her far away and puts her back: that is no walk.
    {
        ArrivalGuard g;
        g.levelStart(true, 0, 0);
        for (int i = 0; i < ArrivalGuard::kSettleTicks + 1; ++i)
            g.tick(true, false, 0, 0);
        for (int i = 0; i < 100; ++i)
            assert(!g.tick(false, false, 250000, 270000)); // the scene (no gameplay)
        for (int i = 0; i < ArrivalGuard::kSettleTicks + 10; ++i)
            assert(!g.tick(true, false, 0, 10)); // put back
        assert(g.blocks());
        g.tick(false, false, 0, 10); // a touch during a cut scene (a cut-scene level's own timer) is let through
        assert(!g.blocks());
        for (int i = 0; i < ArrivalGuard::kSettleTicks + 1; ++i)
            g.tick(true, false, 0, 10);
        assert(g.blocks());
        assert(g.tick(true, false, 2000, 10));
    }
    // One-way exits from a pack manifest.
    {
        using drakan_exit_guard::OneWayExit;
        OneWayExit w;
        assert(drakan_exit_guard::parseOneWayExit("OotF 01 - Ruined Village @ 279392, 14761, 152908", w));
        assert(w.level == "ootf 01 - ruined village" && w.x == 279392.0f && w.y == 14761.0f && w.z == 152908.0f);
        assert(drakan_exit_guard::isOneWayExit(w, " OOTF 01 - Ruined Village", 279392.5f, 14761, 152950));
        assert(!drakan_exit_guard::isOneWayExit(w, "OotF 01 - Ruined Village", 279392, 14761, 153000)); // another exit
        assert(!drakan_exit_guard::isOneWayExit(w, "OotF 03 - Wartok Canyons", 279392, 14761, 152908));
        assert(drakan_exit_guard::parseOneWayExit("L @ -1.5,2,3e2", w) && w.x == -1.5f && w.z == 300.0f);
        assert(!drakan_exit_guard::parseOneWayExit("no position", w));
        assert(!drakan_exit_guard::parseOneWayExit(" @ 1, 2, 3", w));
        assert(!drakan_exit_guard::parseOneWayExit("L @ 1, 2", w));
        assert(!drakan_exit_guard::parseOneWayExit("L @ 1, 2, 3, 4", w));
        assert(!drakan_exit_guard::parseOneWayExit("L @ 1, x, 3", w));
    }
    std::cout << "Exit guard policy tests passed\n";
}
