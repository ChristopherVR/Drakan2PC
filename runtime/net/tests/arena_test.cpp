// Multiplayer maps (ArenaMaps.h), the pickup messages and the server's arena rules (spawn choice, pickup respawn).
#include "TestUtil.h"
#include "net/ArenaMaps.h"

#include <cmath>

using namespace drakan::net;
using namespace drakan::net::test;

namespace {

const char* kMini = R"(
; a comment
[arena demo-one]
name = Demo One
level = Level 1A - Surdana
description = Two corners.
modes = dm
players = 4
health = 80
bounds = 0 0 10000 10000
loadout = 0x20abc 0x20abd
preview = 500 60 700 135 -20
spawn = 1000 50 1000 90
spawn = 9000 50 9000
spawn = 1000 50 9000
pickup = 5000 10 5000 30 res=0x3372b name=Steel Club
pickup = 100 10 100 15

[arena demo-two]
name = Demo Two
level = Level 1D - City of the Damned
spawn = 0 0 0
spawn = 100 0 0
)";

ServerOptions arenaOptions(const ArenaDef& def) {
    ServerOptions o;
    o.config.level = def.name;
    o.config.fragLimit = 0;
    o.config.timeLimitSec = 0;
    o.config.maxPlayers = 4;
    o.config.respawnDelayMs = 500;
    o.config.startHealth = def.startHealth;
    o.respawnProtectionMs = 0;
    o.loadTimeoutMs = 2000;
    o.profileHash = 0xABCD;
    o.maxSpeed = 0;
    o.hasArena = true;
    o.arena = def;
    return o;
}

PlayerState at(float x, float z) {
    PlayerState s;
    s.pos[0] = x;
    s.pos[2] = z;
    return s;
}

} // namespace

int main() {
    // ---- the INI format ----
    {
        ArenaCatalog cat;
        std::vector<std::string> errors;
        CHECK(cat.add(kMini, "test", &errors) == 2);
        CHECK(errors.empty());
        const ArenaDef* a = cat.find("DEMO-ONE");
        CHECK(a && a == cat.find("demo one")); // id or name, any case
        CHECK(a->name == "Demo One" && a->level == "Level 1A - Surdana" && a->maxPlayers == 4 && a->startHealth == 80);
        CHECK(a->deathmatch && !a->teamDeathmatch);
        CHECK(a->hasBounds && a->bounds[2] == 10000 && a->insideBounds(5000, 5000) && !a->insideBounds(-1, 5000));
        CHECK(a->loadout.size() == 2 && a->loadout[1] == 0x20abd);
        CHECK(a->hasPreview && a->preview[0] == 500 && a->preview[3] == 135 && a->preview[4] == -20);
        CHECK(a->spawns.size() == 3 && a->spawns[0].yawDeg == 90 && a->spawns[1].pos[2] == 9000);
        CHECK(a->pickups.size() == 2 && a->pickups[0].resId == 0x3372b && a->pickups[0].label == "Steel Club" &&
              a->pickups[0].respawnSec == 30 && a->pickups[1].resId == 0 && a->pickups[1].respawnSec == 15);
        const ArenaDef* b = cat.find("demo-two");
        CHECK(b && b->deathmatch && b->teamDeathmatch && !b->hasBounds && b->maxPlayers == 8 && b->startHealth == 100);

        // a later source replaces an arena of the same id
        CHECK(cat.add("[arena demo-two]\nname = Demo Two B\nlevel = X\nspawn = 1 2 3\nspawn = 4 5 6\n", "mod") == 1);
        CHECK(cat.arenas().size() == 2 && cat.find("demo-two")->name == "Demo Two B");
        CHECK(cat.find("demo-two")->source == "mod");
        CHECK(cat.find("nonsense") == nullptr);
    }
    {
        // errors drop the arena, name the line and never throw
        ArenaCatalog cat;
        std::vector<std::string> errors;
        const char* bad = "[arena a]\nname = A\nlevel = L\nspawn = 1 2 3\n"                // too few spawns
                          "[arena b]\nname = B\nlevel = L\nspawn = 1 2 3\nspawn = x y z\n" // bad number
                          "[arena c]\nname = C\nlevel = L\nspawn = 1 2 3\nspawn = 4 5 6\nplayers = 99\n"
                          "[arena d]\nname = D\nlevel = L\nspawn = 1 2 3\nspawn = 4 5 6\nbounds = 5 5 1 1\n"
                          "[arena e]\nname = E\nlevel = L\nspawn = 1 2 3\nspawn = 4 5 6\nbogus = 1\n"
                          "[arena g]\nname = G\nlevel = L\nspawn = 1 2 3\nspawn = 4 5 6\npreview = 1 2\n"
                          "[arena bad id!]\nname = F\n"
                          "orphan = 1\n"
                          "[arena ok]\nname = OK\nlevel = L\nspawn = 1 2 3\nspawn = 4 5 6\npickup = 1 2 3 0\n";
        CHECK(cat.add(bad, "bad.ini", &errors) == 0);
        CHECK(cat.arenas().empty());
        CHECK(errors.size() >= 7);
        CHECK(errors[0].rfind("bad.ini:1:", 0) == 0); // source:line prefix
        CHECK(cat.add("[arena ok]\nname = OK\nlevel = L\nspawn = 1 2 3\nspawn = 4 5 6\n", "ok") == 1);
        CHECK(cat.add("", "empty") == 0);
        CHECK(cat.add(std::string(1000, 'x') + "\n[arena z", "junk") == 0);
    }
    {
        // the compiled-in default list (configs/mp_maps.ini) is valid: every arena has spawns inside its bounds
        ArenaCatalog cat;
        std::vector<std::string> errors;
        const size_t n = cat.add(builtinArenaText(), "built-in", &errors);
        for (const std::string& e : errors)
            std::fprintf(stderr, "built-in maps: %s\n", e.c_str());
        CHECK(errors.empty());
        CHECK(n >= 3);
        // Campaign pickup initialization is filtered before the arena loads. City must
        // instantiate these resources rather than adopt actors that no longer exist.
        const ArenaDef* city = cat.find("city-skeleton-square");
        CHECK(city && city->pickups.size() == 4);
        if (city && city->pickups.size() == 4) {
            CHECK(city->pickups[0].resId == 0x3373cu);
            CHECK(city->pickups[0].label == "Lastat's Blade");
            CHECK(city->pickups[0].pos[0] == 157758 && city->pickups[0].pos[1] == 170900 &&
                  city->pickups[0].pos[2] == 469570);
            CHECK(city->pickups[1].resId == 0x31b33u);
            CHECK(city->pickups[1].label == "Full Mana");
            CHECK(city->pickups[1].pos[0] == 198101 && city->pickups[1].pos[1] == 168300 &&
                  city->pickups[1].pos[2] == 425685);
        }
        for (const ArenaDef& a : cat.arenas()) {
            CHECK(!a.description.empty() && a.spawns.size() >= 4 && !a.pickups.empty());
            for (const ArenaSpawn& s : a.spawns)
                CHECK(a.insideBounds(s.pos[0], s.pos[2]));
            CHECK(a.name.size() <= kMaxLevelLength);
        }
    }
    {
        // spawn choice: the farthest from the others, never the one just used when another exists
        ArenaCatalog cat;
        cat.add(kMini, "t");
        const ArenaDef& a = *cat.find("demo-one");
        CHECK(a.pickSpawn({}) == 0);
        CHECK(a.pickSpawn({{1000.0f, 1000.0f}}) == 1); // 9000,9000 is the farthest corner
        CHECK(a.pickSpawn({{1000.0f, 1000.0f}, {9000.0f, 9000.0f}}) == 2);
        CHECK(a.pickSpawn({{1000.0f, 1000.0f}}, 1) == 2); // 1 is the previous life's spawn
        CHECK(a.pickSpawn({}, 0) == 1);
        ArenaDef none;
        CHECK(none.pickSpawn({}) == -1);
    }

    // ---- pickup messages ----
    {
        PickupClaim c;
        c.index = 7;
        PickupClaim c2;
        CHECK(decode(encode(c), c2) && c2.index == 7);
        c.index = kAllPickups;
        CHECK(!decode(encode(c), c2)); // an index nobody can own
        PickupState s;
        s.index = 3;
        s.taken = true;
        s.player = 2;
        PickupState s2;
        CHECK(decode(encode(s), s2) && s2.index == 3 && s2.taken && s2.player == 2);
        s.index = kAllPickups;
        CHECK(!decode(encode(s), s2)); // "everything is taken" is not a message
        s.taken = false;
        s.player = 0;
        CHECK(decode(encode(s), s2) && s2.index == kAllPickups && !s2.taken);
        auto bytes = encode(s2);
        bytes.push_back(0);
        CHECK(!decode(bytes, s2)); // trailing bytes
        MsgType t;
        CHECK(peekType(encode(PickupClaim{}).data(), 1, t) && t == MsgType::PickupClaim);
    }

    // ---- the server: spawn choice and pickups ----
    {
        ArenaCatalog cat;
        cat.add(kMini, "t");
        const ArenaDef def = *cat.find("demo-one");
        Rig rig(arenaOptions(def));
        MatchClient& a = rig.addClient("A", 0xABCD);
        MatchClient& b = rig.addClient("B", 0xABCD);
        rig.run(100);
        CHECK(rig.server->startMatch(rig.now));
        rig.run(50);
        a.sendLoadDone();
        b.sendLoadDone();
        rig.run(100);
        CHECK(rig.server->phase() == MatchPhase::Playing);

        std::vector<ClientEvent> ea = collect(a);
        std::vector<uint8_t> spawn;
        for (const auto& e : ea)
            if (const auto* r = std::get_if<Respawn>(&e))
                spawn.push_back(r->spawnIndex);
        CHECK(spawn.size() == 2 && spawn[0] != spawn[1] && spawn[0] < 3 && spawn[1] < 3); // two different points
        CHECK(count<PickupState>(ea) == 1); // the reset: everything is available
        for (const auto& e : ea)
            if (const auto* p = std::get_if<PickupState>(&e))
                CHECK(p->index == kAllPickups && !p->taken);

        // A is at spawn[0]; a respawn of B goes to the point farthest from A
        a.sendState(at(def.spawns[spawn[0]].pos[0], def.spawns[spawn[0]].pos[2]));
        b.sendState(at(def.spawns[spawn[1]].pos[0], def.spawns[spawn[1]].pos[2]));
        rig.run(60);
        collect(b);

        // pickups: claim, refusal while taken, respawn after the delay, and a newcomer learns what is gone
        collect(a);
        collect(b);
        a.sendState(at(5000, 5000)); // next to pickup 0
        rig.run(60);
        CHECK(a.sendPickupClaim(0));
        rig.run(60);
        auto ea2 = collect(a), eb2 = collect(b);
        CHECK(count<PickupState>(ea2) == 1 && count<PickupState>(eb2) == 1);
        for (const auto& e : eb2)
            if (const auto* p = std::get_if<PickupState>(&e))
                CHECK(p->index == 0 && p->taken && p->player == a.playerId());
        b.sendPickupClaim(0); // already taken: refused, nothing is broadcast
        b.sendPickupClaim(9); // no such pickup
        rig.run(60);
        CHECK(count<PickupState>(collect(a)) == 0);
        CHECK(rig.server->phase() == MatchPhase::Playing);
        // a claim from far away is refused
        b.sendState(at(-90000, -90000));
        rig.run(60);
        b.sendPickupClaim(1);
        rig.run(60);
        CHECK(count<PickupState>(collect(b)) == 0);

        MatchClient& c = rig.addClient("C", 0xABCD); // late join: learns pickup 0 is gone
        rig.run(150);
        auto ec = collect(c);
        bool sawTaken = false;
        for (const auto& e : ec)
            if (const auto* p = std::get_if<PickupState>(&e))
                sawTaken |= p->index == 0 && p->taken;
        CHECK(sawTaken);

        rig.run(31000); // the 30 s delay is over
        auto ea3 = collect(a);
        bool back = false;
        for (const auto& e : ea3)
            if (const auto* p = std::get_if<PickupState>(&e))
                back |= p->index == 0 && !p->taken;
        CHECK(back);
        a.sendState(at(5000, 5000));
        rig.run(60);
        CHECK(a.sendPickupClaim(0)); // takeable again
        rig.run(60);
        CHECK(count<PickupState>(collect(b)) >= 1);
    }
    {
        // a claim right after a respawn: the published position is still the old life's (stale), the spawn point counts
        ArenaCatalog cat;
        cat.add("[arena stale]\nname = Stale\nlevel = X\nspawn = 1000 50 1000\nspawn = 1100 50 1000\n"
                "pickup = 1000 10 1000 30\npickup = 1050 10 1000 30\n",
                "t");
        const ArenaDef def = *cat.find("stale");
        Rig rig(arenaOptions(def));
        MatchClient& a = rig.addClient("A", 0xABCD);
        rig.run(100);
        CHECK(rig.server->startMatch(rig.now));
        rig.run(50);
        a.sendLoadDone();
        rig.run(100);
        CHECK(rig.server->phase() == MatchPhase::Playing);
        collect(a);
        a.sendState(at(-50000, -50000)); // the old life / level start, far from the spawn point
        rig.run(60);
        CHECK(a.sendPickupClaim(0)); // at the spawn point as far as the server knows: accepted
        rig.run(60);
        CHECK(count<PickupState>(collect(a)) == 1);
        rig.run(9000); // a level takes long to load: the spawn point still stands in until she has arrived
        CHECK(a.sendPickupClaim(1));
        rig.run(60);
        CHECK(count<PickupState>(collect(a)) == 1);
        a.sendState(at(1000, 1000)); // arrived at the spawn point; now her position is real
        rig.run(60);
        a.sendState(at(-50000, -50000));
        rig.run(60);
        collect(a);
        rig.run(31000); // pickup 0 is back
        collect(a);
        CHECK(a.sendPickupClaim(0)); // far from it now: refused
        rig.run(60);
        CHECK(count<PickupState>(collect(a)) == 0);
    }
    {
        // moves after a respawn / teleport the server was not told about: the stale last position must not make it
        // refuse every later state (speed check on)
        ArenaCatalog cat;
        cat.add("[arena far]\nname = Far\nlevel = X\nspawn = 1000 50 1000\nspawn = 1100 50 1000\n"
                "pickup = 500000 10 500000 30\n",
                "t");
        const ArenaDef def = *cat.find("far");
        ServerOptions o = arenaOptions(def);
        o.maxSpeed = 30000.0f;
        Rig rig(o);
        MatchClient& a = rig.addClient("A", 0xABCD);
        rig.run(100);
        CHECK(rig.server->startMatch(rig.now));
        rig.run(50);
        a.sendLoadDone();
        rig.run(100);
        CHECK(rig.server->phase() == MatchPhase::Playing);
        a.sendState(at(-50000, -50000)); // the level start: accepted as the first state
        rig.run(60);
        a.sendState(at(1000, 1000)); // the game moved her to the spawn point: far from the stale position, accepted
        rig.run(60);
        const ServerPlayer* sp = rig.server->player(a.playerId());
        CHECK(sp && sp->state.pos[0] == 1000 && sp->state.pos[2] == 1000);
        rig.run(9000); // trust window over: a jump is a speed violation again
        a.sendState(at(500000, 500000));
        rig.run(60);
        CHECK(rig.server->player(a.playerId())->state.pos[0] == 1000);
        collect(a);
        CHECK(a.sendPickupClaim(0)); // she is really there: the refused newest position counts for a claim
        rig.run(60);
        CHECK(count<PickupState>(collect(a)) == 1);
        for (int i = 0; i < 14; ++i) { // but states that keep agreeing from the new place are a teleport: resync
            a.sendState(at(500000 + static_cast<float>(i), 500000));
            rig.run(60);
        }
        CHECK(rig.server->player(a.playerId())->state.pos[0] >= 500000);
    }
    {
        // a newcomer is spread away from the spawn point a player was just given, not from the (stale) position she
        // still publishes while the game moves her there
        ArenaCatalog cat;
        cat.add(kMini, "t");
        const ArenaDef def = *cat.find("demo-one");
        Rig rig(arenaOptions(def));
        MatchClient& a = rig.addClient("A", 0xABCD);
        rig.run(100);
        CHECK(rig.server->startMatch(rig.now));
        rig.run(50);
        a.sendLoadDone();
        rig.run(100);
        CHECK(rig.server->phase() == MatchPhase::Playing);
        uint8_t spawnA = 255;
        for (const auto& e : collect(a))
            if (const auto* r = std::get_if<Respawn>(&e))
                spawnA = r->spawnIndex;
        CHECK(spawnA < 3);
        a.sendState(at(-50000, -50000)); // still at the level start, far from every spawn point
        rig.run(60);
        MatchClient& b = rig.addClient("B", 0xABCD);
        rig.run(150);
        uint8_t spawnB = 255;
        for (const auto& e : collect(b))
            if (const auto* r = std::get_if<Respawn>(&e); r && r->player == b.playerId())
                spawnB = r->spawnIndex;
        CHECK(spawnB < 3 && spawnB != spawnA);
        const auto& sa = def.spawns[spawnA].pos;
        const auto& sb = def.spawns[spawnB].pos;
        CHECK(std::hypot(sa[0] - sb[0], sa[2] - sb[2]) >
              5000.0f); // opposite corners of the 10000 square, not neighbours
    }
    {
        // without an arena the old behaviour: no pickup traffic, spawnIndex counts up, claims are ignored
        ServerOptions o = arenaOptions(ArenaDef());
        o.hasArena = false;
        Rig rig(o);
        MatchClient& a = rig.addClient("A", 0xABCD);
        rig.run(100);
        CHECK(rig.server->startMatch(rig.now));
        rig.run(50);
        a.sendLoadDone();
        rig.run(100);
        CHECK(rig.server->phase() == MatchPhase::Playing);
        a.sendPickupClaim(0);
        rig.run(60);
        CHECK(count<PickupState>(collect(a)) == 0);
    }
    std::puts("net_arena_test: ok");
    return 0;
}
