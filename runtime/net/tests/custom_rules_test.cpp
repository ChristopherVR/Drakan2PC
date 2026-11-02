// Custom game mode rules added 2026-11-02 (docs/reference/netcode.md "Custom game modes"): lives and last one standing
// (free for all and teams, a late joiner spectates, a leaver ends it), the team kill score, the spawn protection and
// pickup respawn overrides, the allowed weapons, the HUD names / colours, the plain-language goal and summary, and the
// Score wire form with lives.
#include "TestUtil.h"

#include <cstring>

using namespace drakan::net;
using namespace drakan::net::test;

namespace {

ServerOptions baseOptions() {
    ServerOptions o;
    o.config.level = "alpha";
    o.config.fragLimit = 0;
    o.config.timeLimitSec = 0;
    o.config.maxPlayers = 6;
    o.config.respawnDelayMs = 200;
    o.config.startHealth = 100;
    o.respawnProtectionMs = 0;
    o.minHitIntervalMs = 0;
    o.maxHitRange = 0;
    o.maxHitRangeRanged = 0;
    o.loadTimeoutMs = 2000;
    o.resultsDurationMs = 5000;
    o.maxSpeed = 0;
    return o;
}

PlayerState at(float x, float z = 0) {
    PlayerState s;
    s.pos[0] = x;
    s.pos[2] = z;
    return s;
}

ModeDef parseMode(const char* text) {
    ModeCatalog cat;
    std::vector<std::string> errors;
    CHECK(cat.add(text, "test", &errors) == 1);
    for (const std::string& e : errors)
        std::fprintf(stderr, "%s\n", e.c_str());
    CHECK(errors.empty());
    return cat.modes().front();
}

ServerOptions withMode(const ModeDef& mode) {
    ServerOptions o = baseOptions();
    o.modes = {mode};
    applyModeToConfig(mode, o.config);
    o.config.respawnDelayMs = 200;
    return o;
}

struct Match {
    Rig rig;
    std::vector<MatchClient*> c;
    Match(ServerOptions o, int players) : rig(std::move(o)) {
        for (int i = 0; i < players; ++i)
            c.push_back(&rig.addClient(std::string("P") + char('A' + i)));
        rig.run(200);
        CHECK(rig.server->startMatch(rig.now));
        rig.run(60);
        for (MatchClient* p : c)
            p->sendLoadDone();
        rig.run(100);
        CHECK(rig.server->phase() == MatchPhase::Playing);
        for (size_t i = 0; i < c.size(); ++i)
            c[i]->sendState(at(1000.0f * static_cast<float>(i), 5000));
        rig.run(60);
    }
    const ServerPlayer& sp(size_t i) const { return *rig.server->player(c[i]->playerId()); }
    void kill(size_t killer, size_t victim) {
        c[killer]->sendHit(c[victim]->playerId(), 0, 100);
        rig.run(60);
        rig.run(300);
    }
};

void parsing() {
    const ModeDef m = parseMode("[mode lrs]\nname = Last Rynn Standing\nbase = dm\nlives = 3\nscore_team_kill = -2\n"
                                "spawn_protection = 4\npickup_respawn = never\nteam_names = Red, Blue\n"
                                "team_colors = c83c28 3c78dc\ntime_limit = 300\nfrag_limit = 0\n");
    CHECK(m.lives == 3 && m.scoreTeamKill == -2 && m.spawnProtectionSec == 4 && m.pickupRespawnSec == 0);
    CHECK(m.teamNames.size() == 2 && m.teamNames[0] == "Red" && m.teamNames[1] == "Blue");
    CHECK(m.teamColors.size() == 2 && m.teamColors[0] == 0xc83c28u && m.teamColors[1] == 0x3c78dcu);
    CHECK(parseMode("[mode p]\nname = P\npickup_respawn = 45\n").pickupRespawnSec == 45);
    CHECK(parseMode("[mode p]\nname = P\npickup_respawn = map\n").pickupRespawnSec == -1);
    const ModeDef d = parseMode("[mode q]\nname = Q\n");
    CHECK(d.lives == 0 && d.spawnProtectionSec == -1 && d.pickupRespawnSec == -1 && d.scoreTeamKill == 0);
    // Bad values drop the mode with a message.
    for (const char* bad : {"[mode x]\nname = X\nlives = 100\n", "[mode x]\nname = X\npickup_respawn = soon\n",
                            "[mode x]\nname = X\nspawn_protection = 11\n", "[mode x]\nname = X\nteam_colors = red\n",
                            "[mode x]\nname = X\nteam_names = a, b, c, d, e\n",
                            "[mode x]\nname = X\nteam_names = a name that is far too long\n"}) {
        ModeCatalog cat;
        std::vector<std::string> errors;
        CHECK(cat.add(bad, "t", &errors) == 0 && !errors.empty());
    }
    // Plain-language text for the menus and the HUD.
    CHECK(modeGoal(m) == "Last one standing, 3 lives each");
    const std::string sum = modeSummary(m);
    CHECK(sum.find("Free for all.") == 0 &&
          sum.find("3 lives each; the last player standing wins.") != std::string::npos &&
          sum.find("5 minutes.") != std::string::npos && sum.find("Pickups do not come back.") != std::string::npos);
    CHECK(modeGoal(parseMode("[mode f]\nname = F\n")) == "First to 20 points");
    CHECK(modeGoal(parseMode("[mode t]\nname = T\nbase = tdm\nlives = 1\nfrag_limit = 0\n")) ==
          "Last team standing, 1 life each");
    CHECK(modeGoal(parseMode("[mode c]\nname = C\nobjective = capture\nfrag_limit = 5\n")) ==
          "Capture the flag, or first to 5 points");
}

void wire() {
    Score s;
    s.player = 3;
    s.lives = 2;
    s.eliminated = true;
    Score b;
    CHECK(decode(encode(s), b) && b.lives == 2 && b.eliminated);
    std::vector<uint8_t> old = encode(s);
    old.resize(old.size() - 2); // a server before lives
    CHECK(decode(old, b) && b.lives == kLivesUnlimited && !b.eliminated);
}

void lastOneStanding() {
    const ModeDef mode =
        parseMode("[mode lrs]\nname = Last Rynn Standing\nlives = 2\nfrag_limit = 0\ntime_limit = 300\n");
    Match m(withMode(mode), 3);
    CHECK(m.sp(0).lives == 2 && m.sp(1).lives == 2 && !m.sp(0).eliminated);
    m.kill(0, 1);
    CHECK(m.sp(1).lives == 1 && !m.sp(1).eliminated && m.sp(1).alive); // respawned with one life left
    CHECK(m.c[2]->roster().at(m.c[1]->playerId()).lives == 1);         // the clients know
    m.kill(0, 1);
    CHECK(m.sp(1).eliminated && !m.sp(1).alive && m.sp(1).lives == 0);
    m.rig.run(1000);
    CHECK(!m.sp(1).alive); // no respawn
    CHECK(m.c[0]->roster().at(m.c[1]->playerId()).eliminated);
    CHECK(m.rig.server->phase() == MatchPhase::Playing); // two players are still in
    // An eliminated player can neither hurt nor be hurt.
    const uint32_t rejected = m.rig.server->stats().hitsRejected;
    m.c[1]->sendHit(m.c[0]->playerId(), 0, 100);
    m.c[0]->sendHit(m.c[1]->playerId(), 0, 100);
    m.rig.run(60);
    CHECK(m.rig.server->stats().hitsRejected == rejected + 2);
    // P2 kills P0 twice: P2 is the last one standing even with fewer points than P0.
    m.kill(2, 0);
    m.kill(2, 0);
    CHECK(m.rig.server->phase() == MatchPhase::Results);
    CHECK(m.rig.server->outcome().reason == EndReason::LastStanding);
    CHECK(m.rig.server->outcome().winner == m.c[2]->playerId() && !m.rig.server->outcome().tie);
    CHECK(m.c[1]->outcome().reason == EndReason::LastStanding && m.c[1]->outcome().winner == m.c[2]->playerId());
    const std::vector<ScoreRow> rows = m.c[0]->scoreboard();
    CHECK(rows.front().player == m.c[2]->playerId() && !rows.front().eliminated); // the survivor heads the table
    // A rematch gives everybody their lives back.
    m.c[0]->sendResultsAction(ResultsChoice::Rematch);
    m.rig.run(100);
    for (MatchClient* p : m.c)
        p->sendLoadDone();
    m.rig.run(200);
    if (m.rig.server->phase() == MatchPhase::Playing)
        CHECK(m.sp(1).lives == 2 && !m.sp(1).eliminated && m.sp(1).alive);
}

void lateJoinAndLeaver() {
    const ModeDef mode = parseMode("[mode lrs]\nname = LRS\nlives = 1\nfrag_limit = 0\n");
    // A listen server starts alone: the opponent who joins a moment later still plays.
    {
        Match m(withMode(mode), 1);
        MatchClient& late = m.rig.addClient("Late");
        m.rig.run(300);
        const ServerPlayer& lp = *m.rig.server->player(late.playerId());
        CHECK(lp.alive && !lp.eliminated && lp.lives == 1);
        CHECK(m.rig.server->phase() == MatchPhase::Playing);
        // A third player after that watches.
        MatchClient& third = m.rig.addClient("Third");
        m.rig.run(300);
        const ServerPlayer& tp = *m.rig.server->player(third.playerId());
        CHECK(!tp.alive && tp.eliminated);
        CHECK(m.rig.server->phase() == MatchPhase::Playing);
        m.c[0]->sendHit(late.playerId(), 0, 100);
        m.rig.run(100);
        CHECK(m.rig.server->phase() == MatchPhase::Results && m.rig.server->outcome().winner == m.c[0]->playerId());
    }
    // The last opponent leaving ends the match.
    {
        Match m(withMode(mode), 2);
        m.rig.clients[1]->disconnect();
        m.rig.run(2000);
        CHECK(m.rig.server->phase() == MatchPhase::Results &&
              m.rig.server->outcome().reason == EndReason::LastStanding);
    }
    // Without lives nothing of this happens.
    {
        Match m(baseOptions(), 2);
        m.kill(0, 1);
        m.kill(0, 1);
        CHECK(m.sp(1).alive && m.sp(1).lives == kLivesUnlimited && m.rig.server->phase() == MatchPhase::Playing);
    }
}

void teamLastStanding() {
    const ModeDef mode = parseMode("[mode tls]\nname = Team LS\nbase = tdm\nlives = 1\nfrag_limit = 0\n"
                                   "friendly_fire = on\nscore_team_kill = -3\n");
    Match m(withMode(mode), 4); // teams: P0, P2 = 0; P1, P3 = 1
    CHECK(m.sp(0).team == m.sp(2).team && m.sp(1).team == m.sp(3).team && m.sp(0).team != m.sp(1).team);
    m.kill(0, 2); // a team kill: -3 for P0, P2 is out
    CHECK(m.sp(0).score == -3 && m.sp(2).eliminated);
    CHECK(m.rig.server->phase() == MatchPhase::Playing);
    m.kill(1, 0); // team 0 has nobody left
    CHECK(m.rig.server->phase() == MatchPhase::Results);
    CHECK(m.rig.server->outcome().reason == EndReason::LastStanding &&
          m.rig.server->outcome().winningTeam == m.sp(1).team);
    CHECK(m.c[2]->outcome().winningTeam == m.sp(1).team);
}

void protectionPickupsWeapons() {
    // Spawn protection from the mode (the base options have none).
    {
        const ModeDef mode = parseMode("[mode sp]\nname = SP\nspawn_protection = 5\nfrag_limit = 0\n");
        Match m(withMode(mode), 2);
        m.c[0]->sendHit(m.c[1]->playerId(), 0, 10);
        m.rig.run(60);
        CHECK(m.rig.server->stats().hitsAccepted == 0); // still protected
        m.rig.run(5200);
        m.c[0]->sendHit(m.c[1]->playerId(), 0, 10);
        m.rig.run(60);
        CHECK(m.rig.server->stats().hitsAccepted == 1);
    }
    // Pickups that never come back, and pickups on the mode's own clock.
    for (int variant = 0; variant < 2; ++variant) {
        const ModeDef mode = parseMode(variant == 0 ? "[mode np]\nname = NP\npickup_respawn = never\n"
                                                    : "[mode np]\nname = NP\npickup_respawn = 2\n");
        ServerOptions o = withMode(mode);
        o.hasArena = true;
        o.arena.id = o.arena.name = "alpha";
        o.arena.spawns.resize(2);
        o.arena.spawns[1].pos[0] = 3000;
        ArenaPickup pk;
        pk.respawnSec = 30;
        pk.pos[0] = 1000;
        pk.pos[2] = 5000;
        o.arena.pickups = {pk};
        Match m(o, 2);
        collect(*m.c[1]);
        CHECK(m.c[1]->sendPickupClaim(0));
        m.rig.run(60);
        collect(*m.c[0]);
        m.rig.run(3000);
        bool back = false;
        for (const auto& e : collect(*m.c[0]))
            if (const auto* p = std::get_if<PickupState>(&e))
                back = back || (p->index == 0 && !p->taken);
        CHECK(back == (variant == 1));
    }
    // Allowed weapons: a hit with another one does nothing, bare hands and the listed weapon count.
    {
        const ModeDef mode = parseMode("[mode clubs]\nname = Clubs\nweapons = 0x3372b\nfrag_limit = 0\n");
        Match m(withMode(mode), 2);
        PlayerState s = at(0, 5000);
        s.weaponRight = 0x31f30; // great sword
        m.c[0]->sendState(s);
        m.rig.run(60);
        m.c[0]->sendHit(m.c[1]->playerId(), 0, 10);
        m.rig.run(60);
        CHECK(m.rig.server->stats().hitsAccepted == 0);
        s.weaponRight = 0x3372b;
        m.c[0]->sendState(s);
        m.rig.run(60);
        m.c[0]->sendHit(m.c[1]->playerId(), 0, 10);
        m.rig.run(60);
        CHECK(m.rig.server->stats().hitsAccepted == 1);
    }
}

} // namespace

int main() {
    parsing();
    wire();
    lastOneStanding();
    lateJoinAndLeaver();
    teamLastStanding();
    protectionPickupsWeapons();
    std::puts("custom_rules_test: ok");
    return 0;
}
