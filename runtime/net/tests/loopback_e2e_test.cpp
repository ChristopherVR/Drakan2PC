// One server, two clients over the loopback transport: handshake -> playing -> states replicated -> a Hit produces
// Death / Score / Respawn. Run clean and with simulated latency and loss.
#include "TestUtil.h"

#include <cmath>

using namespace drakan::net;
using namespace drakan::net::test;

namespace {

void scenario(uint32_t latencyMs, double lossPercent) {
    ServerOptions o;
    o.config.level = "arena";
    o.config.fragLimit = 5;
    o.config.respawnDelayMs = 400;
    o.config.maxPlayers = 4;
    o.profileHash = 0x5EC5;
    o.respawnProtectionMs = 0;
    o.maxHitRange = 0; // A walks far away from B in this test
    Rig rig(o);
    LoopbackNetwork::Conditions c;
    c.latencyMs = latencyMs;
    c.lossPercent = lossPercent;
    c.seed = 7;
    rig.net.setConditions(c);

    MatchClient& a = rig.addClient("Rynn", 0x5EC5);
    MatchClient& b = rig.addClient("Arokh", 0x5EC5);
    rig.run(500);

    // Handshake.
    CHECK(a.connected() && b.connected());
    CHECK(a.playerId() != b.playerId() && a.playerId() != kNoPlayer);
    CHECK(a.config().level == "arena" && a.phase() == MatchPhase::Lobby);
    CHECK(a.roster().size() == 2 && b.roster().size() == 2);
    CHECK(a.roster().at(b.playerId()).name == "Arokh");

    // Lobby -> loading -> playing.
    CHECK(rig.server->startMatch(rig.now));
    rig.run(300);
    CHECK(a.phase() == MatchPhase::Loading && b.phase() == MatchPhase::Loading);
    a.sendLoadDone();
    b.sendLoadDone();
    rig.run(300);
    CHECK(rig.server->phase() == MatchPhase::Playing);
    CHECK(a.phase() == MatchPhase::Playing && b.phase() == MatchPhase::Playing);
    CHECK(a.self()->alive && b.self()->alive);

    // States replicate: A walks along +x at 1 unit per ms; B sees it, delayed and interpolated.
    const uint64_t t0 = rig.now;
    for (int i = 0; i < 80; ++i) {
        PlayerState s;
        s.pos[0] = static_cast<float>(rig.now - t0);
        s.vel[0] = 1000.0f;
        s.animId = 7;
        s.weapon = 2;
        a.sendState(s);
        PlayerState s2;
        s2.pos[2] = 50;
        b.sendState(s2);
        rig.run(50);
    }
    CHECK(b.bufferedStates(a.playerId()) >= 2);
    CHECK(a.bufferedStates(a.playerId()) == 0); // own state is not buffered
    PlayerState seen;
    CHECK(b.remoteState(a.playerId(), rig.now, seen));
    CHECK(seen.animId == 7 && seen.weapon == 2);
    // Expected: A's position at (now - interpolation delay - latency), within a snapshot interval or so.
    const float expected = static_cast<float>(rig.now - t0) - 100.0f - static_cast<float>(latencyMs);
    CHECK(std::fabs(seen.pos[0] - expected) < 120.0f);
    PlayerState later;
    rig.run(200);
    CHECK(b.remoteState(a.playerId(), rig.now, later));
    CHECK(later.pos[0] >= seen.pos[0]);
    PlayerState bSeenByA;
    CHECK(a.remoteState(b.playerId(), rig.now, bSeenByA) && bSeenByA.pos[2] == 50);

    // A hits B until B dies.
    collect(a);
    collect(b);
    a.sendHit(b.playerId(), 2, 60);
    rig.run(300);
    a.sendHit(b.playerId(), 2, 60);
    rig.run(300);
    auto evA = collect(a);
    auto evB = collect(b);
    CHECK(count<Hit>(evA) == 2 && count<Hit>(evB) == 2);
    CHECK(count<Death>(evA) == 1 && count<Death>(evB) == 1);
    CHECK(first<Death>(evB)->victim == b.playerId() && first<Death>(evB)->killer == a.playerId());
    CHECK(count<Score>(evB) == 2);
    CHECK(a.roster().at(a.playerId()).kills == 1 && b.roster().at(b.playerId()).deaths == 1);
    CHECK(!b.self()->alive);
    // The dead player's pose is dropped from everyone's buffers.
    CHECK(a.bufferedStates(b.playerId()) == 0);

    // Respawn after the delay, full health.
    rig.run(600);
    evB = collect(b);
    CHECK(count<Respawn>(evB) == 1 && first<Respawn>(evB)->health == 100);
    CHECK(b.self()->alive && b.self()->health == 100);

    // Chat is relayed with the sender id filled in by the server.
    a.sendChat("good game");
    rig.run(300);
    evB = collect(b);
    CHECK(count<Chat>(evB) == 1 && first<Chat>(evB)->player == a.playerId() && first<Chat>(evB)->text == "good game");

    // Leaving.
    b.disconnect();
    rig.run(300);
    CHECK(rig.server->players().size() == 1 && a.roster().size() == 1);
    evA = collect(a);
    CHECK(count<PlayerLeave>(evA) == 1);
}

} // namespace

int main() {
    scenario(0, 0.0);
    scenario(30, 10.0);
    std::puts("net_loopback_e2e_test ok");
    return 0;
}
