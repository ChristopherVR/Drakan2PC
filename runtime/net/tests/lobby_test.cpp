// Lobby / menu addendum: the start and next-match countdowns, per-player ping (Status / StatusEcho), the host's kick,
// the mods column of ServerInfo (docs/reference/netcode.md "Lobby countdowns, ping, kick").
#include "TestUtil.h"

using namespace drakan::net;
using namespace drakan::net::test;

namespace {

ServerOptions lobbyOptions() {
    ServerOptions o;
    o.config.mode = MatchMode::Deathmatch;
    o.config.level = "Level 1A - Surdana";
    o.config.maxPlayers = 4;
    o.profileHash = 0x10BB;
    o.startCountdownMs = 3000;
    o.hostKick = true;
    o.resultsDurationMs = 4000;
    o.mods = "Test Pack,Other Pack";
    return o;
}

void wire() {
    Status s;
    s.token = 123456;
    s.countdown = CountdownKind::NextMatch;
    s.countdownMs = 9000;
    s.pings = {{1, 12, 0}, {2, kPingUnknown, 0x0ee7}};
    Status back;
    CHECK(decode(encode(s), back));
    CHECK(back.token == 123456 && back.countdown == CountdownKind::NextMatch && back.countdownMs == 9000);
    CHECK(back.pings.size() == 2 && back.pings[0].pingMs == 12 && back.pings[1].pingMs == kPingUnknown);
    CHECK(back.pings[1].character == 0x0ee7);
    StatusEcho e;
    e.token = 77;
    StatusEcho eb;
    CHECK(decode(encode(e), eb) && eb.token == 77);
    Kick k;
    k.player = 3;
    Kick kb;
    CHECK(decode(encode(k), kb) && kb.player == 3);
    k.player = 0;
    CHECK(!decode(encode(k), kb));
    // A countdown kind the protocol does not know is refused.
    std::vector<uint8_t> bad = encode(s);
    bad[5] = 9;
    CHECK(!decode(bad, back));
    // ServerInfo carries the mods; an older reply without them still parses.
    ServerInfo i;
    i.name = "x";
    i.level = "L";
    i.mods = "A,B";
    ServerInfo ib;
    CHECK(decode(encode(i), ib) && ib.mods == "A,B");
    std::vector<uint8_t> old = encode(i);
    old.resize(old.size() - 2 - 3); // drop the trailing str (u16 length + 3 bytes)
    CHECK(decode(old, ib) && ib.mods.empty());
}

void startCountdown() {
    Rig rig(lobbyOptions());
    MatchClient& a = rig.addClient("Host", 0x10BB);
    MatchClient& b = rig.addClient("Guest", 0x10BB);
    rig.run(300);
    CHECK(a.connected() && b.connected());
    uint32_t ms = 0;
    CHECK(b.countdown(rig.now, ms) == CountdownKind::None);
    CHECK(rig.server->startMatch(rig.now));
    CHECK(rig.server->phase() == MatchPhase::Lobby); // counting down, not loading
    rig.run(1000);
    CHECK(rig.server->phase() == MatchPhase::Lobby);
    CHECK(b.countdown(rig.now, ms) == CountdownKind::MatchStart);
    CHECK(ms > 1200 && ms <= 2100);
    rig.run(2400);
    CHECK(rig.server->phase() == MatchPhase::Loading);
    CHECK(b.phase() == MatchPhase::Loading);
}

void nextMatchCountdown() {
    ServerOptions o = lobbyOptions();
    o.startCountdownMs = 0;
    Rig rig(o);
    MatchClient& a = rig.addClient("Host", 0x10BB);
    rig.run(200);
    CHECK(rig.server->startMatch(rig.now));
    rig.run(100);
    a.sendLoadDone();
    rig.run(200);
    CHECK(rig.server->phase() == MatchPhase::Playing);
    rig.server->endMatch(rig.now);
    rig.run(1000);
    CHECK(a.phase() == MatchPhase::Results);
    uint32_t ms = 0;
    CHECK(a.countdown(rig.now, ms) == CountdownKind::NextMatch);
    CHECK(ms > 2000 && ms <= 3200);
    rig.run(4000);
    CHECK(a.phase() == MatchPhase::Lobby);
}

void pings() {
    Rig rig(lobbyOptions());
    MatchClient& a = rig.addClient("Host", 0x10BB);
    MatchClient& b = rig.addClient("Guest", 0x10BB);
    rig.run(2000);
    const RemotePlayer* other = a.roster().count(b.playerId()) ? &a.roster().at(b.playerId()) : nullptr;
    CHECK(other != nullptr);
    CHECK(other->pingMs != kPingUnknown && other->pingMs < 200);
    CHECK(rig.server->player(b.playerId())->pingMs != kPingUnknown);
    // The co-op character picked in the lobby reaches the others through Status.
    CHECK(b.sendCharacter(0x0ee7));
    rig.run(1200);
    CHECK(a.roster().at(b.playerId()).character == 0x0ee7);
    CHECK(a.roster().at(a.playerId()).character == 0);
    // Health (Vitals) travels client -> server -> every client, and nobody forges another player's entry.
    CHECK(!a.roster().at(b.playerId()).hasVitals);
    PlayerVitals v;
    v.player = a.playerId(); // ignored: the server uses the sender
    v.hp = 340;
    v.maxHp = 500;
    v.dragonHp = 90;
    v.dragonMaxHp = 200;
    CHECK(b.sendVitals(v));
    rig.run(1200);
    const RemotePlayer& rb = a.roster().at(b.playerId());
    CHECK(rb.hasVitals && rb.vitals.hp == 340 && rb.vitals.maxHp == 500 && rb.vitals.dragonHp == 90 &&
          rb.vitals.dragonMaxHp == 200);
    CHECK(!a.roster().at(a.playerId()).hasVitals);
    Vitals two;
    two.entries = {v, v};
    std::vector<uint8_t> raw = encode(two);
    Vitals back;
    CHECK(decode(raw, back) && back.entries.size() == 2 && back.entries[1].hp == 340);
    raw.pop_back();
    CHECK(!decode(raw, back));
}

void kicks() {
    Rig rig(lobbyOptions());
    MatchClient& a = rig.addClient("Host", 0x10BB);
    rig.run(100);
    MatchClient& b = rig.addClient("Guest", 0x10BB);
    MatchClient& c = rig.addClient("Third", 0x10BB);
    rig.run(300);
    CHECK(rig.server->hostPlayer() == a.playerId());
    // Only the host may kick.
    CHECK(b.sendKick(c.playerId()));
    rig.run(300);
    CHECK(c.connected() && rig.server->players().size() == 3);
    CHECK(a.sendKick(a.playerId())); // not oneself
    rig.run(200);
    CHECK(a.connected());
    CHECK(a.sendKick(c.playerId()));
    rig.run(400);
    CHECK(rig.server->players().size() == 2);
    CHECK(c.rejected() && c.rejectInfo().reason == RejectReason::Kicked);
    CHECK(!a.roster().count(c.playerId()) && !b.roster().count(c.playerId()));
    // Without the option nobody can kick.
    ServerOptions o = lobbyOptions();
    o.hostKick = false;
    Rig closed(o);
    MatchClient& h = closed.addClient("Host", 0x10BB);
    closed.run(100);
    MatchClient& g = closed.addClient("Guest", 0x10BB);
    closed.run(300);
    CHECK(h.sendKick(g.playerId()));
    closed.run(300);
    CHECK(g.connected());
}

void mods() {
    Rig rig(lobbyOptions());
    CHECK(rig.server->info().mods == "Test Pack,Other Pack");
    Query q;
    ServerInfo i;
    CHECK(decode(rig.server->answerQuery(encode(q)), i) && i.mods == "Test Pack,Other Pack");
}

} // namespace

int main() {
    wire();
    startCountdown();
    nextMatchCountdown();
    pings();
    kicks();
    mods();
    std::puts("net_lobby_test ok");
    return 0;
}
