// Results screen buttons (ResultsAction: back to the lobby, next map, rematch), custom game modes executed by the
// server (score values and limits, 2..4 teams, friendly fire, capture / hold objectives) and the mode picker's rules
// (modeFitsArena). docs/reference/netcode.md "Results screen" and "Custom game modes".
#include "TestUtil.h"

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

ArenaDef arena(const std::string& name, bool dm = true, bool tdm = true) {
    ArenaDef a;
    a.id = name;
    a.name = name;
    a.level = "Level 1A - Surdana";
    a.deathmatch = dm;
    a.teamDeathmatch = tdm;
    a.maxPlayers = 8;
    a.startHealth = 100;
    a.spawns.resize(2);
    return a;
}

ModeDef parseMode(const char* text) {
    ModeCatalog cat;
    std::vector<std::string> errors;
    CHECK(cat.add(text, "test", &errors) == 1);
    CHECK(errors.empty());
    return cat.modes().front();
}

// Joined clients in the Playing phase.
struct Match {
    Rig rig;
    std::vector<MatchClient*> c;
    Match(ServerOptions o, int players) : rig(std::move(o)) {
        for (int i = 0; i < players; ++i)
            c.push_back(&rig.addClient(std::string("P") + char('A' + i)));
        rig.run(200);
        for (MatchClient* p : c)
            CHECK(p->connected());
        CHECK(rig.server->startMatch(rig.now));
        rig.run(60);
        for (MatchClient* p : c)
            p->sendLoadDone();
        rig.run(100);
        CHECK(rig.server->phase() == MatchPhase::Playing);
        for (size_t i = 0; i < c.size(); ++i)
            c[i]->sendState(at(1000.0f * static_cast<float>(i), 5000)); // far apart, away from any marker
        rig.run(60);
    }
    void kill(size_t killer, size_t victim) {
        c[killer]->sendHit(c[victim]->playerId(), 0, 100);
        rig.run(60);
        rig.run(300); // the victim respawns (respawn delay 200 ms)
    }
};

void wire() {
    ResultsAction a;
    a.choice = ResultsChoice::NextMap;
    ResultsAction back;
    CHECK(decode(encode(a), back) && back.choice == ResultsChoice::NextMap);
    std::vector<uint8_t> bad = encode(a);
    bad[1] = 0;
    CHECK(!decode(bad, back));
    bad[1] = 4;
    CHECK(!decode(bad, back));
    // Score carries the points; a body of an older server (no trailing fields) means points = kills.
    Score s;
    s.player = 2;
    s.kills = 5;
    s.deaths = 1;
    s.teamKills[0] = 5;
    s.score = 9;
    s.teamScore[2] = -3;
    Score sb;
    CHECK(decode(encode(s), sb) && sb.score == 9 && sb.teamScore[2] == -3);
    std::vector<uint8_t> old = encode(s);
    old.resize(1 + 9);
    CHECK(decode(old, sb) && sb.score == 5 && sb.teamScore[0] == 5 && sb.teamScore[2] == 0);
    // The config carries the mode id; one without it still parses.
    MatchConfigMsg m;
    m.config.modeId = "instagib-lite";
    MatchConfigMsg mb;
    CHECK(decode(encode(m), mb) && mb.config.modeId == "instagib-lite");
    std::vector<uint8_t> oldCfg = encode(m);
    oldCfg.resize(oldCfg.size() - 2 - 13);
    CHECK(decode(oldCfg, mb) && mb.config.modeId.empty());
    // Teams 0..3 exist; 4 does not.
    PlayerJoin j;
    j.player = 1;
    j.team = 3;
    j.name = "x";
    PlayerJoin jb;
    CHECK(decode(encode(j), jb) && jb.team == 3);
    j.team = 4;
    CHECK(!decode(encode(j), jb));
}

void resultsButtons() {
    ServerOptions o = baseOptions();
    o.rotation = {arena("alpha"), arena("beta"), arena("gamma", false, false)};
    o.hasArena = true;
    o.arena = o.rotation[0];
    o.startCountdownMs = 0;
    Match m(o, 2);
    MatchClient& host = *m.c[0];
    MatchClient& guest = *m.c[1];
    CHECK(m.rig.server->hostPlayer() == host.playerId());
    m.rig.run(700); // a Status tick: every client knows who the host is
    CHECK(host.hostPlayer() == host.playerId() && guest.hostPlayer() == host.playerId());

    // The buttons do nothing outside the results.
    host.sendResultsAction(ResultsChoice::Rematch);
    m.rig.run(100);
    CHECK(m.rig.server->phase() == MatchPhase::Playing);

    m.rig.server->endMatch(m.rig.now);
    m.rig.run(100);
    CHECK(host.phase() == MatchPhase::Results && guest.phase() == MatchPhase::Results);

    // Only the host decides.
    guest.sendResultsAction(ResultsChoice::BackToLobby);
    m.rig.run(100);
    CHECK(m.rig.server->phase() == MatchPhase::Results);

    // Back to the lobby: everybody, at once (not at the end of the results timer), marked as the host's choice.
    (void)collect(guest);
    host.sendResultsAction(ResultsChoice::BackToLobby);
    m.rig.run(100);
    CHECK(m.rig.server->phase() == MatchPhase::Lobby);
    CHECK(host.phase() == MatchPhase::Lobby && guest.phase() == MatchPhase::Lobby);
    {
        bool byHost = false;
        for (const auto& ev : collect(guest))
            if (const auto* pc = std::get_if<PhaseChange>(&ev))
                byHost = byHost || (pc->phase == MatchPhase::Lobby && pc->byHost);
        CHECK(byHost);
    }

    // Rematch: the same map and rules, straight into the next loading phase.
    auto toResults = [&] {
        CHECK(m.rig.server->phase() == MatchPhase::Loading || m.rig.server->phase() == MatchPhase::Lobby);
        if (m.rig.server->phase() == MatchPhase::Lobby)
            CHECK(m.rig.server->startMatch(m.rig.now));
        m.rig.run(60);
        host.sendLoadDone();
        guest.sendLoadDone();
        m.rig.run(100);
        CHECK(m.rig.server->phase() == MatchPhase::Playing);
        m.rig.server->endMatch(m.rig.now);
        m.rig.run(100);
        CHECK(m.rig.server->phase() == MatchPhase::Results);
    };
    CHECK(m.rig.server->startMatch(m.rig.now));
    toResults();
    host.sendResultsAction(ResultsChoice::Rematch);
    m.rig.run(100);
    CHECK(m.rig.server->phase() == MatchPhase::Loading);
    CHECK(guest.phase() == MatchPhase::Loading && guest.config().level == "alpha");

    // Next map: the following map the mode fits (gamma has no deathmatch, so beta, then alpha again).
    toResults();
    host.sendResultsAction(ResultsChoice::NextMap);
    m.rig.run(100);
    CHECK(m.rig.server->phase() == MatchPhase::Loading);
    CHECK(m.rig.server->config().level == "beta");
    CHECK(host.config().level == "beta" && guest.config().level == "beta");
    toResults();
    host.sendResultsAction(ResultsChoice::NextMap);
    m.rig.run(100);
    CHECK(guest.config().level == "alpha" && m.rig.server->phase() == MatchPhase::Loading);

    // The results timer still returns to the lobby on its own.
    toResults();
    (void)collect(guest);
    m.rig.run(5200);
    CHECK(m.rig.server->phase() == MatchPhase::Lobby);
    for (const auto& ev : collect(guest))
        if (const auto* pc = std::get_if<PhaseChange>(&ev))
            CHECK(!(pc->phase == MatchPhase::Lobby && pc->byHost)); // the timer is not the host's choice
}

void resultsCoop() {
    ServerOptions o = baseOptions();
    o.config.mode = MatchMode::Coop;
    o.rotation = {arena("alpha"), arena("beta")};
    o.startCountdownMs = 0;
    Match m(o, 2);
    m.rig.server->endMatch(m.rig.now);
    m.rig.run(100);
    CHECK(m.c[0]->phase() == MatchPhase::Results);
    m.c[0]->sendResultsAction(ResultsChoice::NextMap); // co-op: the same level again
    m.rig.run(100);
    CHECK(m.rig.server->phase() == MatchPhase::Loading && m.c[1]->config().level == "alpha");
    CHECK(m.c[1]->config().mode == MatchMode::Coop);
    m.c[0]->sendLoadDone();
    m.c[1]->sendLoadDone();
    m.rig.run(100);
    m.rig.server->endMatch(m.rig.now);
    m.rig.run(100);
    m.c[0]->sendResultsAction(ResultsChoice::BackToLobby);
    m.rig.run(100);
    CHECK(m.rig.server->phase() == MatchPhase::Lobby && m.c[1]->phase() == MatchPhase::Lobby);
}

void scoring() {
    const ModeDef mode = parseMode("[mode quick]\nname = Quick Frags\nbase = dm\nscore_kill = 2\nscore_death = -1\n"
                                   "frag_limit = 6\nrespawn_delay = 0\n");
    ServerOptions o = baseOptions();
    o.modes = {mode};
    applyModeToConfig(mode, o.config);
    CHECK(o.config.modeId == "quick" && o.config.fragLimit == 6);
    Match m(o, 2);
    CHECK(m.c[0]->config().modeId == "quick");
    m.kill(0, 1);
    CHECK(m.rig.server->player(m.c[0]->playerId())->score == 2);
    CHECK(m.rig.server->player(m.c[1]->playerId())->score == -1);
    CHECK(m.c[1]->roster().at(m.c[0]->playerId()).score == 2); // the points reach the clients
    CHECK(m.rig.server->phase() == MatchPhase::Playing);
    m.kill(0, 1);
    CHECK(m.rig.server->phase() == MatchPhase::Playing); // 4 points, three kills worth 6 are needed
    m.kill(0, 1);
    CHECK(m.rig.server->phase() == MatchPhase::Results);
    CHECK(m.rig.server->outcome().reason == EndReason::FragLimit &&
          m.rig.server->outcome().winner == m.c[0]->playerId());
    const std::vector<ScoreRow> rows = m.c[1]->scoreboard();
    CHECK(rows.size() == 2 && rows[0].player == m.c[0]->playerId() && rows[0].points() == 6 && rows[1].points() == -3);
    CHECK(m.c[1]->outcome().reason == EndReason::FragLimit && m.c[1]->outcome().winner == m.c[0]->playerId());
    // A standard match counts kills.
    Match plain(baseOptions(), 2);
    plain.kill(1, 0);
    CHECK(plain.rig.server->player(plain.c[1]->playerId())->score == 1);
}

void teamsAndFriendlyFire() {
    const ModeDef three =
        parseMode("[mode trio]\nname = Three Teams\nbase = tdm\nteams = 3\nscore_kill = 3\nfrag_limit = 6\n"
                  "respawn_delay = 0\n");
    ServerOptions o = baseOptions();
    o.modes = {three};
    applyModeToConfig(three, o.config);
    CHECK(o.config.mode == MatchMode::TeamDeathmatch);
    Match m(o, 4);
    CHECK(m.rig.server->teamCount() == 3);
    int count[3] = {0, 0, 0};
    for (const auto& kv : m.rig.server->players()) {
        CHECK(kv.second.team < 3);
        ++count[kv.second.team];
    }
    CHECK(count[0] == 2 && count[1] == 1 && count[2] == 1);   // balanced, lowest team first
    CHECK(m.c[3]->roster().at(m.c[2]->playerId()).team == 2); // the clients know teams 3 and 4 exist
    CHECK(m.c[0]->teamCount() == 3);
    // No friendly fire: P0 (team 0) and P3 (team 0).
    const ServerPlayer* p0 = m.rig.server->player(m.c[0]->playerId());
    const ServerPlayer* p3 = m.rig.server->player(m.c[3]->playerId());
    CHECK(p0->team == p3->team);
    m.c[0]->sendHit(m.c[3]->playerId(), 0, 100);
    m.rig.run(60);
    CHECK(m.rig.server->stats().hitsRejected == 1);
    // Team 2 (P2) kills team 1 (P1) twice: 6 team points end the match for team 2.
    CHECK(m.rig.server->player(m.c[2]->playerId())->team == 2);
    m.kill(2, 1);
    CHECK(m.rig.server->teamScore(2) == 3 && m.rig.server->phase() == MatchPhase::Playing);
    m.kill(2, 1);
    CHECK(m.rig.server->phase() == MatchPhase::Results);
    CHECK(m.rig.server->outcome().winningTeam == 2 && !m.rig.server->outcome().tie);
    CHECK(m.c[0]->teamScore(2) == 6 && m.c[0]->outcome().winningTeam == 2);

    // Friendly fire on: the hit lands, the team kill scores nothing for the shooter.
    const ModeDef ff =
        parseMode("[mode brawl]\nname = Brawl\nbase = tdm\nfriendly_fire = on\nscore_kill = 5\nfrag_limit = 0\n"
                  "respawn_delay = 0\n");
    ServerOptions o2 = baseOptions();
    o2.modes = {ff};
    applyModeToConfig(ff, o2.config);
    Match m2(o2, 4);
    CHECK(m2.rig.server->teamCount() == 2);
    m2.c[0]->sendHit(m2.c[2]->playerId(), 0, 100); // P0 and P2 share team 0
    m2.rig.run(60);
    CHECK(m2.rig.server->stats().hitsAccepted == 1);
    CHECK(m2.rig.server->player(m2.c[0]->playerId())->score == 0 && m2.rig.server->teamScore(0) == 0);
    m2.rig.run(300);
    m2.c[0]->sendHit(m2.c[1]->playerId(), 0, 100); // an enemy kill scores
    m2.rig.run(60);
    CHECK(m2.rig.server->player(m2.c[0]->playerId())->score == 5 && m2.rig.server->teamScore(0) == 5);
}

void capture() {
    const ModeDef mode =
        parseMode("[mode ctf]\nname = Flags\nbase = tdm\nscore_kill = 0\nscore_objective = 1\nfrag_limit = 4\n"
                  "objective = capture points=2\n");
    ServerOptions o = baseOptions();
    o.modes = {mode};
    applyModeToConfig(mode, o.config);
    ArenaDef a = arena("alpha");
    ArenaMarker flag, base0, base1;
    flag.kind = ArenaMarker::Kind::Flag; // team 2's flag, in the middle
    flag.pos[0] = 0;
    flag.pos[2] = 0;
    flag.radius = 300;
    flag.team = 2;
    base0.kind = ArenaMarker::Kind::Base;
    base0.pos[0] = 4000;
    base0.pos[2] = 0;
    base0.radius = 300;
    base0.team = 1;
    base1 = base0;
    base1.pos[0] = -4000;
    base1.team = 2;
    a.markers = {flag, base0, base1};
    o.hasArena = true;
    o.arena = a;
    Match m(o, 2);
    MatchClient& p0 = *m.c[0]; // team 0 = "team 1" of the markers
    MatchClient& p1 = *m.c[1];
    CHECK(m.rig.server->player(p0.playerId())->team == 0 && m.rig.server->player(p1.playerId())->team == 1);
    // Team 2's own flag: P1 stands on it and takes nothing.
    p1.sendState(at(0, 0));
    m.rig.run(100);
    CHECK(m.rig.server->player(p1.playerId())->carriedFlag < 0);
    p1.sendState(at(1000, 5000));
    // P0 takes the flag, a trip to the wrong base does nothing, the own base scores points + score_objective.
    p0.sendState(at(0, 0));
    m.rig.run(100);
    CHECK(m.rig.server->player(p0.playerId())->carriedFlag == 0);
    p0.sendState(at(-4000, 0)); // team 2's base: not ours
    m.rig.run(100);
    CHECK(m.rig.server->player(p0.playerId())->score == 0 && m.rig.server->player(p0.playerId())->carriedFlag == 0);
    p0.sendState(at(4000, 0));
    m.rig.run(100);
    CHECK(m.rig.server->player(p0.playerId())->score == 3 && m.rig.server->player(p0.playerId())->objectives == 1);
    CHECK(m.rig.server->teamScore(0) == 3 && m.rig.server->player(p0.playerId())->carriedFlag < 0);
    CHECK(m.c[1]->roster().at(p0.playerId()).score == 3);
    // The flag is home again; a second capture ends the match (limit 4 points, 6 reached).
    p0.sendState(at(0, 0));
    m.rig.run(100);
    CHECK(m.rig.server->player(p0.playerId())->carriedFlag == 0);
    p0.sendState(at(4000, 0));
    m.rig.run(100);
    CHECK(m.rig.server->phase() == MatchPhase::Results && m.rig.server->outcome().winningTeam == 0);
    // Announcements arrived as system chat.
    bool announced = false;
    for (const auto& ev : collect(p1))
        if (const auto* ch = std::get_if<Chat>(&ev))
            announced = announced || (ch->player == 0 && ch->text.find("captured the flag") != std::string::npos);
    CHECK(announced);
}

void capturedFlagDrops() {
    const ModeDef mode = parseMode("[mode ctf]\nname = Flags\nbase = dm\nfrag_limit = 0\nobjective = capture\n");
    ServerOptions o = baseOptions();
    o.modes = {mode};
    applyModeToConfig(mode, o.config);
    ArenaDef a = arena("alpha");
    ArenaMarker flag, base;
    flag.kind = ArenaMarker::Kind::Flag;
    flag.radius = 300;
    base.kind = ArenaMarker::Kind::Base;
    base.pos[0] = 4000;
    base.radius = 300;
    a.markers = {flag, base};
    o.hasArena = true;
    o.arena = a;
    Match m(o, 2);
    m.c[0]->sendState(at(0, 0));
    m.rig.run(100);
    CHECK(m.rig.server->player(m.c[0]->playerId())->carriedFlag == 0);
    m.kill(1, 0); // the carrier dies: the flag is back home, and the killer can take it
    CHECK(m.rig.server->player(m.c[0]->playerId())->carriedFlag < 0);
    m.c[1]->sendState(at(0, 0));
    m.rig.run(100);
    CHECK(m.rig.server->player(m.c[1]->playerId())->carriedFlag == 0);
}

void hold() {
    const ModeDef mode = parseMode("[mode koth]\nname = Hill\nbase = dm\nscore_kill = 0\nfrag_limit = 3\n"
                                   "objective = hold points=1 seconds=2\n");
    ServerOptions o = baseOptions();
    o.modes = {mode};
    applyModeToConfig(mode, o.config);
    ArenaDef a = arena("alpha");
    ArenaMarker zone;
    zone.kind = ArenaMarker::Kind::Zone;
    zone.pos[0] = 0;
    zone.pos[2] = 0;
    zone.radius = 500;
    a.markers = {zone};
    o.hasArena = true;
    o.arena = a;
    Match m(o, 2);
    MatchClient& p0 = *m.c[0];
    MatchClient& p1 = *m.c[1];
    p0.sendState(at(100, 100));
    m.rig.run(1500);
    CHECK(m.rig.server->player(p0.playerId())->score == 0); // 1.5 s, not yet
    // A second player contests the zone: the clock starts over.
    p1.sendState(at(-100, 0));
    m.rig.run(1500);
    CHECK(m.rig.server->player(p0.playerId())->score == 0 && m.rig.server->player(p1.playerId())->score == 0);
    p1.sendState(at(3000, 3000));
    m.rig.run(2400);
    CHECK(m.rig.server->player(p0.playerId())->score == 1);
    m.rig.run(4200); // two more completions: 3 points end the match
    CHECK(m.rig.server->phase() == MatchPhase::Results && m.rig.server->outcome().winner == p0.playerId());
}

void fits() {
    ModeDef dm = parseMode("[mode a]\nname = A\nbase = dm\n");
    ModeDef tdm = parseMode("[mode b]\nname = B\nbase = tdm\nteams = 4\n");
    ModeDef cap = parseMode("[mode c]\nname = C\nbase = dm\nobjective = capture\n");
    ModeDef hold = parseMode("[mode d]\nname = D\nbase = dm\nobjective = hold seconds=5\n");
    ArenaDef a = arena("m");
    std::string why;
    CHECK(modeFitsArena(dm, a) && modeFitsArena(tdm, a));
    CHECK(!modeFitsArena(cap, a, &why) && !why.empty());
    CHECK(!modeFitsArena(hold, a));
    ArenaMarker f, b, z;
    f.kind = ArenaMarker::Kind::Flag;
    b.kind = ArenaMarker::Kind::Base;
    z.kind = ArenaMarker::Kind::Zone;
    a.markers = {f, b};
    CHECK(modeFitsArena(cap, a) && !modeFitsArena(hold, a));
    a.markers.push_back(z);
    CHECK(modeFitsArena(hold, a));
    ArenaDef noTdm = arena("n", true, false);
    CHECK(modeFitsArena(dm, noTdm) && !modeFitsArena(tdm, noTdm, &why));
    noTdm.customModes = {"b"}; // a map that lists the mode id takes it
    CHECK(modeFitsArena(tdm, noTdm));
    ArenaDef tiny = arena("t");
    tiny.maxPlayers = 3;
    CHECK(!modeFitsArena(tdm, tiny, &why)); // four teams need four players
}

} // namespace

int main() {
    wire();
    resultsButtons();
    resultsCoop();
    scoring();
    teamsAndFriendlyFire();
    capture();
    capturedFlagDrops();
    hold();
    fits();
    std::puts("results_modes_test: ok");
    return 0;
}
