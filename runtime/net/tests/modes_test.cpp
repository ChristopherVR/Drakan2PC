// Arena authoring keys (spawn team, marker, keep / remove, custom mode ids) and the [mode] descriptor (GameModes.h).
#include "TestUtil.h"
#include "net/ArenaMaps.h"
#include "net/GameModes.h"

using namespace drakan::net;

namespace {

const char* kText = R"(
[arena a1]
name = A One
level = Level 1A - Surdana
modes = dm, ctf-lite, hill
spawn = 0 0 0 90 team=1
spawn = 100 0 0 team=2
spawn = 200 0 0 45
marker = base 10 0 10 team=1 name=Red Base
marker = flag 20 0 20 radius=99
marker = zone 30 0 30
keep = 1 2 3
remove = 4 5 6
remove = 7 8 9

[mode ctf-lite]
name = Capture Lite
base = tdm
score_objective = 10
frag_limit = 0
time_limit = 900
respawn_delay = 5
friendly_fire = on
start_health = 150
loadout = 0x20abc
weapons = 0x1 0x2
objective = capture points=3
objective = hold seconds=20 radius=500

[mode bad-range]
name = Bad
frag_limit = 5000

[mode bad-teams]
name = Bad Teams
teams = 3
)";

} // namespace

int main() {
    ArenaCatalog ac;
    std::vector<std::string> errs;
    CHECK(ac.add(kText, "t", &errs) == 1);
    CHECK(errs.empty());
    const ArenaDef* a = ac.find("a1");
    CHECK(a && a->spawns.size() == 3);
    CHECK(a->spawns[0].team == 1 && a->spawns[0].yawDeg == 90 && a->spawns[1].team == 2 && a->spawns[1].yawDeg == 0);
    CHECK(a->spawns[2].team == 0 && a->spawns[2].yawDeg == 45);
    CHECK(a->deathmatch && !a->teamDeathmatch && a->customModes.size() == 2 && a->customModes[0] == "ctf-lite");
    CHECK(a->markers.size() == 3 && a->markers[0].kind == ArenaMarker::Kind::Base && a->markers[0].team == 1 &&
          a->markers[0].label == "Red Base" && a->markers[0].radius == 600);
    CHECK(a->markers[1].kind == ArenaMarker::Kind::Flag && a->markers[1].radius == 99);
    CHECK(a->markers[2].kind == ArenaMarker::Kind::Zone && a->markers[2].radius == 600);
    CHECK(a->keepAt.size() == 1 && a->removeAt.size() == 2 && a->removeAt[1][2] == 9);

    errs.clear();
    ArenaCatalog bad;
    bad.add("[arena x]\nname=X\nlevel=L\nspawn=0 0 0 team=9\nspawn=1 0 0\n", "b", &errs);
    CHECK(bad.arenas().empty() && !errs.empty());
    errs.clear();
    bad.add("[arena x]\nname=X\nlevel=L\nspawn=0 0 0\nspawn=1 0 0\nmarker = tower 1 2 3\n", "b", &errs);
    CHECK(bad.arenas().empty() && !errs.empty());

    ModeCatalog mc;
    errs.clear();
    CHECK(mc.add(kText, "t", &errs) == 1);
    CHECK(errs.size() == 2); // bad-range (key), bad-teams (dropped)
    const ModeDef* m = mc.find("ctf-lite");
    CHECK(m && m == mc.find("Capture Lite") && m->teamBase && m->teams == 2 && m->scoreKill == 1 &&
          m->scoreObjective == 10);
    CHECK(m->fragLimit == 0 && m->timeLimitSec == 900 && m->respawnDelaySec == 5 && m->friendlyFire &&
          m->startHealth == 150);
    CHECK(m->loadout.size() == 1 && m->weapons.size() == 2 && m->objectives.size() == 2);
    CHECK(m->objectives[0].kind == ModeObjective::Kind::Capture && m->objectives[0].points == 3);
    CHECK(m->objectives[1].kind == ModeObjective::Kind::Hold && m->objectives[1].seconds == 20 &&
          m->objectives[1].radius == 500);
    CHECK(!mc.find("bad-range") && !mc.find("bad-teams"));

    // later sources win
    CHECK(mc.add("[mode ctf-lite]\nname = Replaced\n", "later") == 1);
    CHECK(mc.modes().size() == 1 && mc.find("ctf-lite")->name == "Replaced" && mc.find("ctf-lite")->source == "later");
    errs.clear();
    CHECK(mc.add("[mode h]\nname = H\nobjective = hold\n", "x", &errs) == 0 && !errs.empty());

    MatchConfigData cfg;
    applyModeToConfig(*mc.find("ctf-lite"), cfg);
    CHECK(cfg.mode == MatchMode::Deathmatch && cfg.fragLimit == 20 && cfg.respawnDelayMs == 3000);
    ModeCatalog mc2;
    mc2.add(kText, "t");
    applyModeToConfig(*mc2.find("ctf-lite"), cfg);
    CHECK(cfg.mode == MatchMode::TeamDeathmatch && cfg.fragLimit == 0 && cfg.timeLimitSec == 900 &&
          cfg.respawnDelayMs == 5000 && cfg.startHealth == 150);

    // the built-in file parses in both catalogs without errors and carries the example mode
    ArenaCatalog ba;
    ModeCatalog bm;
    errs.clear();
    CHECK(ba.add(builtinArenaText(), "built-in", &errs) > 0);
    bm.add(builtinArenaText(), "built-in", &errs);
    CHECK(errs.empty());
    CHECK(bm.find("instagib-lite"));
    std::puts("net_modes_test ok");
    return 0;
}
