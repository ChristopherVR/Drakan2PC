// MatchServer state machine over the loopback transport: joins, rejections, phases, damage sanity, scoring, respawn.
#include "TestUtil.h"

using namespace drakan::net;
using namespace drakan::net::test;

namespace {

ServerOptions baseOptions() {
    ServerOptions o;
    o.config.level = "arena";
    o.config.fragLimit = 3;
    o.config.timeLimitSec = 60;
    o.config.maxPlayers = 3;
    o.config.respawnDelayMs = 500;
    o.config.startHealth = 100;
    o.respawnProtectionMs = 0;
    o.minHitIntervalMs = 0;
    o.loadTimeoutMs = 2000;
    o.resultsDurationMs = 1000;
    o.profileHash = 0xABCD;
    o.maxSpeed = 0; // speed check off unless a test turns it on
    return o;
}

PlayerState at(float x) {
    PlayerState s;
    s.pos[0] = x;
    return s;
}

// Two joined clients in the Playing phase.
struct PlayingRig {
    Rig rig;
    MatchClient& a;
    MatchClient& b;
    explicit PlayingRig(ServerOptions o = baseOptions())
        : rig(std::move(o)), a(rig.addClient("A", 0xABCD)), b(rig.addClient("B", 0xABCD)) {
        rig.run(100);
        CHECK(a.connected() && b.connected());
        CHECK(rig.server->startMatch(rig.now));
        rig.run(50);
        a.sendLoadDone();
        b.sendLoadDone();
        rig.run(100);
        CHECK(rig.server->phase() == MatchPhase::Playing);
        a.sendState(at(0));
        b.sendState(at(10));
        rig.run(50);
    }
    void hit(MatchClient& from, PlayerId victim, uint16_t dmg) {
        from.sendHit(victim, 1, dmg);
        rig.run(50);
    }
};

} // namespace

int main() {
    // Rejections: version, profile hash, name, full server.
    {
        Rig rig(baseOptions());
        MatchClient& bad = rig.addClient("X", 0x1111);
        rig.run(100);
        CHECK(bad.rejected() && bad.rejectInfo().reason == RejectReason::ProfileMismatch);
        CHECK(rig.server->players().empty());

        MatchClient& noName = rig.addClient("", 0xABCD);
        rig.run(100);
        CHECK(noName.rejected() && noName.rejectInfo().reason == RejectReason::BadName);

        // Raw peer speaking another protocol version.
        auto raw = rig.net.createTransport();
        const PeerId p = raw->connect("127.0.0.1", kPort);
        rig.run(50);
        Hello h;
        h.protocolVersion = kProtocolVersion + 1;
        h.profileHash = 0xABCD;
        h.name = "Old";
        TransportEvent ev;
        CHECK(raw->poll(ev) && ev.type == TransportEventType::Connected);
        CHECK(raw->send(p, Channel::Reliable, encode(h)));
        rig.run(50);
        bool gotReject = false;
        while (raw->poll(ev))
            if (ev.type == TransportEventType::Message) {
                Reject r;
                gotReject = decode(ev.data, r) && r.reason == RejectReason::VersionMismatch &&
                            r.serverVersion == kProtocolVersion;
            }
        CHECK(gotReject);

        MatchClient& c1 = rig.addClient("C1", 0xABCD);
        MatchClient& c2 = rig.addClient("C2", 0xABCD);
        MatchClient& c3 = rig.addClient("C3", 0xABCD);
        MatchClient& c4 = rig.addClient("C4", 0xABCD);
        rig.run(200);
        CHECK(c1.connected() && c2.connected() && c3.connected());
        CHECK(c4.rejected() && c4.rejectInfo().reason == RejectReason::ServerFull);
        CHECK(rig.server->players().size() == 3);
        CHECK(rig.server->stats().joinsRejected == 4);
        // Rejected peers are cleaned up after the linger time.
        rig.run(1500);
        CHECK(rig.server->players().size() == 3);
    }

    // A peer that never says Hello is dropped.
    {
        Rig rig(baseOptions());
        auto raw = rig.net.createTransport();
        raw->connect("127.0.0.1", kPort);
        rig.run(100);
        rig.run(6000, 100);
        TransportEvent ev;
        bool gotDisconnect = false;
        while (raw->poll(ev))
            gotDisconnect |= ev.type == TransportEventType::Disconnected;
        CHECK(gotDisconnect);
    }

    // Phases: lobby -> loading -> playing -> results -> lobby; join/leave bookkeeping.
    {
        Rig rig(baseOptions());
        MatchClient& a = rig.addClient("A", 0xABCD);
        rig.run(100);
        CHECK(a.connected() && a.playerId() == 1 && rig.server->phase() == MatchPhase::Lobby);
        CHECK(a.config().level == "arena");
        CHECK(rig.server->startMatch(rig.now)); // one player is enough
        rig.run(50);
        CHECK(rig.server->phase() == MatchPhase::Loading && a.phase() == MatchPhase::Loading);
        CHECK(!rig.server->startMatch(rig.now)); // not from Loading
        // Load timeout moves on even when the client never reports.
        rig.run(2100);
        CHECK(rig.server->phase() == MatchPhase::Playing && a.phase() == MatchPhase::Playing);
        CHECK(a.self() && a.self()->alive && a.self()->health == 100);

        MatchClient& b = rig.addClient("B", 0xABCD); // late join while playing: spawned at once
        rig.run(100);
        CHECK(b.connected() && b.phase() == MatchPhase::Playing);
        CHECK(b.roster().size() == 2 && a.roster().size() == 2);
        CHECK(rig.server->player(b.playerId())->alive);

        rig.server->endMatch(rig.now);
        rig.run(50);
        CHECK(rig.server->phase() == MatchPhase::Results && b.phase() == MatchPhase::Results);
        rig.run(1100);
        CHECK(rig.server->phase() == MatchPhase::Lobby);

        b.disconnect();
        rig.run(50);
        CHECK(rig.server->players().size() == 1 && a.roster().size() == 1);

        // Last player leaving mid-match returns the server to the lobby.
        CHECK(rig.server->startMatch(rig.now));
        rig.run(50);
        a.disconnect();
        rig.run(50);
        CHECK(rig.server->phase() == MatchPhase::Lobby && rig.server->players().empty());
    }

    // Lobby countdown start and config changes.
    {
        ServerOptions o = baseOptions();
        o.autoStartPlayers = 2;
        o.lobbyCountdownMs = 500;
        Rig rig(o);
        rig.addClient("A", 0xABCD);
        rig.run(1000);
        CHECK(rig.server->phase() == MatchPhase::Lobby);
        MatchConfigData cfg = rig.server->config();
        cfg.level = "other";
        cfg.maxPlayers = 1; // fewer than joined later: legal now (1 player)
        CHECK(rig.server->setConfig(cfg));
        cfg.maxPlayers = 8;
        CHECK(rig.server->setConfig(cfg));
        rig.run(50);
        CHECK(rig.clients[0]->config().level == "other");
        MatchConfigData invalid = cfg;
        invalid.maxPlayers = 0;
        CHECK(!rig.server->setConfig(invalid));
        rig.addClient("B", 0xABCD);
        rig.run(300);
        CHECK(rig.server->phase() == MatchPhase::Lobby);
        rig.run(400);
        CHECK(rig.server->phase() == MatchPhase::Loading);
        CHECK(!rig.server->setConfig(cfg)); // not outside the lobby
    }

    // Damage resolution, sanity checks, scoring and respawn (deathmatch).
    {
        PlayingRig p;
        MatchServer& s = *p.rig.server;
        const PlayerId ia = p.a.playerId(), ib = p.b.playerId();
        collect(p.a);
        collect(p.b);

        p.hit(p.a, ib, 30);
        CHECK(s.player(ib)->health == 70 && s.stats().hitsAccepted == 1);
        auto evs = collect(p.b);
        const Hit* h = first<Hit>(evs);
        CHECK(h && h->attacker == ia && h->victim == ib && h->healthAfter == 70);
        CHECK(p.b.self()->health == 70);

        // Rejected claims: self hit, zero / absurd damage, unknown victim.
        const uint32_t before = s.stats().hitsRejected;
        p.hit(p.a, ia, 10);
        p.hit(p.a, ib, 0);
        p.hit(p.a, ib, 5000);
        p.hit(p.a, 9, 10);
        CHECK(s.stats().hitsRejected == before + 4);
        CHECK(s.player(ib)->health == 70);

        // Out of range (claimed from far away).
        p.a.sendState(at(5000));
        p.rig.run(50);
        p.hit(p.a, ib, 10);
        CHECK(s.player(ib)->health == 70);
        p.a.sendState(at(0)); // back in range
        p.rig.run(50);

        // Kill: Hit, Death, Score x2, then respawn after the delay.
        p.b.sendState(at(10));
        p.rig.run(50);
        collect(p.a);
        collect(p.b);
        p.hit(p.a, ib, 70);
        CHECK(!s.player(ib)->alive && s.player(ia)->kills == 1 && s.player(ib)->deaths == 1);
        evs = collect(p.a);
        CHECK(count<Death>(evs) == 1 && count<Score>(evs) == 2);
        CHECK(first<Death>(evs)->victim == ib && first<Death>(evs)->killer == ia);
        CHECK(p.a.roster().at(ia).kills == 1 && p.a.roster().at(ib).deaths == 1);
        // A dead player cannot be hit again nor attack, and its states are ignored.
        const uint32_t acc = s.stats().hitsAccepted;
        p.hit(p.a, ib, 10);
        p.hit(p.b, ia, 10);
        CHECK(s.stats().hitsAccepted == acc);
        p.rig.run(600);
        CHECK(s.player(ib)->alive && s.player(ib)->health == 100);
        evs = collect(p.b);
        CHECK(count<Respawn>(evs) == 1);
        CHECK(p.b.self()->alive);

        // Frag limit (3) ends the match.
        for (int i = 0; i < 2; ++i) {
            p.b.sendState(at(10));
            p.rig.run(50);
            p.hit(p.a, ib, 100);
            p.rig.run(600);
        }
        CHECK(s.player(ia)->kills == 3);
        CHECK(s.phase() == MatchPhase::Results && p.b.phase() == MatchPhase::Results);
    }

    // Respawn protection and the per-attacker hit rate limit.
    {
        ServerOptions o = baseOptions();
        o.respawnProtectionMs = 1000;
        o.minHitIntervalMs = 200;
        PlayingRig p(o);
        const PlayerId ib = p.b.playerId();
        p.hit(p.a, ib, 10); // protected right after spawn
        CHECK(p.rig.server->player(ib)->health == 100);
        p.rig.run(1200);
        p.hit(p.a, ib, 10);
        CHECK(p.rig.server->player(ib)->health == 90);
        p.rig.run(250);
        p.a.sendHit(ib, 1, 10);
        p.a.sendHit(ib, 1, 10); // second one inside the interval
        p.rig.run(50);
        CHECK(p.rig.server->player(ib)->health == 80);
        p.rig.run(250);
        p.hit(p.a, ib, 10);
        CHECK(p.rig.server->player(ib)->health == 70);
    }

    // Team deathmatch: team assignment, friendly fire ignored, team score limit.
    {
        ServerOptions o = baseOptions();
        o.config.mode = MatchMode::TeamDeathmatch;
        o.config.fragLimit = 2;
        o.config.maxPlayers = 4;
        Rig rig(o);
        MatchClient& a = rig.addClient("A", 0xABCD);
        MatchClient& b = rig.addClient("B", 0xABCD);
        MatchClient& c = rig.addClient("C", 0xABCD);
        MatchClient& d = rig.addClient("D", 0xABCD);
        rig.run(100);
        const auto team = [&](MatchClient& m) { return rig.server->player(m.playerId())->team; };
        CHECK(team(a) == 0 && team(b) == 1 && team(c) == 0 && team(d) == 1);
        CHECK(a.self()->team == 0 && d.roster().at(c.playerId()).team == 0);
        CHECK(rig.server->startMatch(rig.now));
        rig.run(50);
        for (auto& cl : rig.clients)
            cl->sendLoadDone();
        rig.run(100);
        CHECK(rig.server->phase() == MatchPhase::Playing);
        for (auto& cl : rig.clients) {
            PlayerState st;
            cl->sendState(st);
        }
        rig.run(50);

        a.sendHit(c.playerId(), 1, 50); // same team
        rig.run(50);
        CHECK(rig.server->player(c.playerId())->health == 100);

        a.sendHit(b.playerId(), 1, 100);
        rig.run(50);
        CHECK(rig.server->teamKills(0) == 1 && rig.server->teamKills(1) == 0);
        CHECK(d.teamKills(0) == 1);
        c.sendHit(d.playerId(), 1, 100);
        rig.run(50);
        CHECK(rig.server->teamKills(0) == 2);
        CHECK(rig.server->phase() == MatchPhase::Results);
    }

    // Time limit and movement sanity.
    {
        ServerOptions o = baseOptions();
        o.config.timeLimitSec = 5;
        o.maxSpeed = 100;
        PlayingRig p(o);
        const PlayerId ia = p.a.playerId();
        // A teleport is dropped, a plausible step is taken, stale sequences are dropped.
        p.a.sendState(at(5000));
        p.rig.run(50);
        CHECK(p.rig.server->player(ia)->state.pos[0] == 0 && p.rig.server->stats().statesRejected == 1);
        p.rig.run(1000);
        p.a.sendState(at(30));
        p.rig.run(50);
        CHECK(p.rig.server->player(ia)->state.pos[0] == 30);
        // A level change (new levelHash) may jump anywhere; the same jump in the same level is still dropped.
        PlayerState lv = at(9000);
        lv.levelHash = 77;
        p.a.sendState(lv);
        p.rig.run(50);
        CHECK(p.rig.server->player(ia)->state.pos[0] == 9000 && p.rig.server->player(ia)->state.levelHash == 77);
        lv = at(50000);
        lv.levelHash = 77;
        p.a.sendState(lv);
        p.rig.run(50);
        CHECK(p.rig.server->player(ia)->state.pos[0] != 50000);
        const uint32_t seq = p.rig.server->player(ia)->state.sequence;
        CHECK(seq > 0);
        p.rig.run(3000, 50);
        CHECK(p.rig.server->phase() == MatchPhase::Playing);
        p.rig.run(1000, 50);
        CHECK(p.rig.server->phase() == MatchPhase::Results);
    }

    std::puts("net_match_test ok");
    return 0;
}
