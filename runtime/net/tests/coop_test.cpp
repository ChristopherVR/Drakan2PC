// Protocol v6: co-op mode in MatchServer / MatchClient. Server relay rules (authority, routing classes, targets,
// range checks), no PvP and no limits in Coop, version mismatch rejection, a loopback co-op session (creature stream,
// events, late join, authority leaving) and the LevelChange message.
#include "TestUtil.h"

using namespace drakan::net;
using namespace drakan::net::test;

namespace {

ServerOptions coopOptions(MatchMode mode = MatchMode::Coop) {
    ServerOptions o;
    o.config.mode = mode;
    o.config.level = "Level 1A - Surdana";
    o.config.maxPlayers = 4;
    o.config.fragLimit = 1;
    o.config.timeLimitSec = 1;
    o.profileHash = 0xC00;
    o.maxHitRange = 0;
    o.respawnProtectionMs = 0;
    return o;
}

// Lobby -> loading -> playing for every client.
void startPlaying(Rig& rig) {
    CHECK(rig.server->startMatch(rig.now));
    rig.run(100);
    for (auto& c : rig.clients)
        c->sendLoadDone();
    rig.run(200);
    CHECK(rig.server->phase() == MatchPhase::Playing);
}

CoopEvent event(CoopKind k, uint32_t key = 0, int32_t value = 0) {
    CoopEvent e;
    e.kind = k;
    e.levelHash = 0x1234ABCDu;
    e.key = key;
    e.value = value;
    return e;
}

// Events of one type a client got.
std::vector<CoopEvent> coopEvents(const std::vector<ClientEvent>& evs) {
    std::vector<CoopEvent> out;
    for (const auto& e : evs)
        if (const auto* c = std::get_if<CoopEvent>(&e))
            out.push_back(*c);
    return out;
}

void relayRules() {
    Rig rig(coopOptions());
    MatchClient& a = rig.addClient("Host", 0xC00);
    rig.run(100);
    MatchClient& b = rig.addClient("Guest", 0xC00);
    MatchClient& c = rig.addClient("Third", 0xC00);
    rig.run(200);
    CHECK(a.connected() && b.connected() && c.connected());

    // The first player to join owns the creature world; everyone learns it (also the ones that joined later).
    CHECK(rig.server->coopAuthority() == a.playerId());
    CHECK(a.isCoopAuthority() && !b.isCoopAuthority() && !c.isCoopAuthority());
    CHECK(b.coopAuthority() == a.playerId() && c.coopAuthority() == a.playerId());
    CHECK(count<CoopAuthority>(collect(c)) == 1);

    // Nothing is relayed before the match plays.
    a.sendCoopEvent(event(CoopKind::CreatureDied, 1));
    rig.run(100);
    CHECK(coopEvents(collect(b)).empty() && rig.server->stats().coopRejected == 1);

    startPlaying(rig);
    collect(a);
    collect(b);
    collect(c);
    const uint32_t rejected0 = rig.server->stats().coopRejected;

    // Creature stream: only the authority, to everyone else, unreliable, not echoed.
    CoopCreatures cs;
    cs.levelHash = 0x1234ABCDu;
    cs.sequence = 1;
    for (uint32_t i = 0; i < 40; ++i) {
        CoopCreature cr;
        cr.key = 100 + i;
        cr.pos[0] = static_cast<float>(i);
        cr.hp = 30;
        cs.creatures.push_back(cr);
    }
    CHECK(a.sendCoopCreatures(cs));
    rig.run(100);
    auto evB = collect(b), evC = collect(c);
    CHECK(count<CoopCreatures>(evB) == 1 && count<CoopCreatures>(evC) == 1 && count<CoopCreatures>(collect(a)) == 0);
    CHECK(first<CoopCreatures>(evB)->creatures.size() == 40 && first<CoopCreatures>(evB)->creatures[39].key == 139);
    CHECK(first<CoopCreatures>(evB)->levelHash == 0x1234ABCDu && first<CoopCreatures>(evB)->sequence == 1);
    CHECK(b.sendCoopCreatures(cs)); // a guest may not stream creatures
    rig.run(100);
    CHECK(count<CoopCreatures>(collect(a)) == 0 && count<CoopCreatures>(collect(c)) == 0);
    CHECK(rig.server->stats().coopRejected == rejected0 + 1);

    // Authority-origin kinds: CreatureDied to everyone else; a guest sending one is dropped.
    a.sendCoopEvent(event(CoopKind::CreatureDied, 0xAA));
    rig.run(100);
    auto dB = coopEvents(collect(b)), dC = coopEvents(collect(c));
    CHECK(dB.size() == 1 && dC.size() == 1 && dB[0].kind == CoopKind::CreatureDied && dB[0].key == 0xAA &&
          dB[0].levelHash == 0x1234ABCDu);
    CHECK(coopEvents(collect(a)).empty());
    b.sendCoopEvent(event(CoopKind::CreatureDied, 0xBB));
    b.sendCoopEvent(event(CoopKind::Respawn));
    b.sendCoopEvent(event(CoopKind::Hurt, 0, 5));
    rig.run(100);
    CHECK(coopEvents(collect(a)).empty() && coopEvents(collect(c)).empty());

    // Targeted kinds: Hurt / Respawn go to the named player only; target 0 = everyone else; unknown target dropped.
    CoopEvent hurt = event(CoopKind::Hurt, 0, 12);
    hurt.player = c.playerId();
    a.sendCoopEvent(hurt);
    rig.run(100);
    dB = coopEvents(collect(b));
    dC = coopEvents(collect(c));
    CHECK(dB.empty() && dC.size() == 1 && dC[0].value == 12 && dC[0].kind == CoopKind::Hurt);
    CoopEvent resp = event(CoopKind::Respawn);
    a.sendCoopEvent(resp); // target 0
    rig.run(100);
    CHECK(coopEvents(collect(b)).size() == 1 && coopEvents(collect(c)).size() == 1);
    resp.player = 9; // nobody
    a.sendCoopEvent(resp);
    resp.player = a.playerId(); // itself
    a.sendCoopEvent(resp);
    rig.run(100);
    CHECK(coopEvents(collect(b)).empty() && coopEvents(collect(c)).empty() && coopEvents(collect(a)).empty());

    // Guest kinds go to the authority only, stamped with the sender; the authority cannot send them.
    b.sendCoopEvent(event(CoopKind::Claim, 0x77, 9));
    c.sendCoopEvent(event(CoopKind::PlayerDied));
    c.sendCoopEvent(event(CoopKind::SyncReq));
    rig.run(100);
    auto dA = coopEvents(collect(a));
    CHECK(dA.size() == 3 && dA[0].kind == CoopKind::Claim && dA[0].player == b.playerId() && dA[0].key == 0x77 &&
          dA[0].value == 9);
    CHECK(dA[1].kind == CoopKind::PlayerDied && dA[1].player == c.playerId());
    CHECK(dA[2].kind == CoopKind::SyncReq && dA[2].player == c.playerId());
    CHECK(coopEvents(collect(b)).empty() && coopEvents(collect(c)).empty());
    a.sendCoopEvent(event(CoopKind::Claim, 1, 1));
    a.sendCoopEvent(event(CoopKind::SyncReq));
    b.sendCoopEvent(event(CoopKind::Claim, 1, 20000)); // absurd damage
    b.sendCoopEvent(event(CoopKind::Claim, 1, -5));
    rig.run(100);
    CHECK(coopEvents(collect(a)).empty() && coopEvents(collect(b)).empty() && coopEvents(collect(c)).empty());

    // Object events travel both ways to everyone else, stamped.
    b.sendCoopEvent(event(CoopKind::ObjGone, 0x31));
    rig.run(100);
    dA = coopEvents(collect(a));
    dC = coopEvents(collect(c));
    CHECK(dA.size() == 1 && dC.size() == 1 && dA[0].kind == CoopKind::ObjGone && dA[0].player == b.playerId() &&
          dC[0].key == 0x31);
    CHECK(coopEvents(collect(b)).empty());
    CoopEvent msg = event(CoopKind::ObjMsg, 0x40);
    msg.msgType = 0x1001;
    msg.w[2] = -7;
    a.sendCoopEvent(msg);
    CoopEvent st = event(CoopKind::ObjState, 0x41, 1);
    a.sendCoopEvent(st);
    rig.run(100);
    dB = coopEvents(collect(b));
    CHECK(dB.size() == 2 && dB[0].kind == CoopKind::ObjMsg && dB[0].msgType == 0x1001 && dB[0].w[2] == -7 &&
          dB[0].player == a.playerId() && dB[1].kind == CoopKind::ObjState && dB[1].value == 1);
    CHECK(coopEvents(collect(c)).size() == 2);

    // No PvP, no limits in Coop: a hit claim is rejected and neither frag nor time limit ends the match.
    CHECK(b.sendHit(a.playerId(), kHitMeleeRight, 10));
    rig.run(2500);
    CHECK(rig.server->stats().hitsAccepted == 0 && rig.server->phase() == MatchPhase::Playing);
    CHECK(count<Hit>(collect(a)) == 0);
}

void notCoopDropsEverything() {
    Rig rig(coopOptions(MatchMode::Deathmatch));
    MatchClient& a = rig.addClient("A", 0xC00);
    MatchClient& b = rig.addClient("B", 0xC00);
    rig.run(200);
    CHECK(rig.server->coopAuthority() == kNoPlayer && a.coopAuthority() == kNoPlayer);
    startPlaying(rig);
    collect(a);
    collect(b);
    CoopCreatures cs;
    cs.levelHash = 1;
    a.sendCoopCreatures(cs);
    a.sendCoopEvent(event(CoopKind::ObjGone, 5));
    rig.run(100);
    CHECK(count<CoopCreatures>(collect(b)) == 0);
    CHECK(rig.server->stats().coopRejected == 2 && rig.server->stats().coopRelayed == 0);
}

void authorityLifecycle() {
    Rig rig(coopOptions());
    MatchClient& a = rig.addClient("Host", 0xC00);
    rig.run(100);
    MatchClient& b = rig.addClient("Guest", 0xC00);
    rig.run(100);
    startPlaying(rig);
    // A late joiner learns who the authority is.
    MatchClient& c = rig.addClient("Late", 0xC00);
    rig.run(200);
    CHECK(c.connected() && c.coopAuthority() == a.playerId());
    collect(b);
    collect(c);

    // The authority leaves: the longest-connected remaining player (b) takes over and everyone is told (also the late
    // joiner).
    a.disconnect();
    rig.run(300);
    CHECK(rig.server->coopAuthority() == b.playerId() && b.isCoopAuthority() && c.coopAuthority() == b.playerId());
    CHECK(rig.server->stats().coopMigrations == 1);
    CHECK(count<PlayerLeave>(collect(b)) == 1);
    // A guest cannot stream or lead; the new owner can.
    const uint32_t before = rig.server->stats().coopRejected;
    CoopCreatures cs;
    c.sendCoopCreatures(cs);
    const float exit[3] = {1, 2, 3};
    c.sendLevelChange(7, exit);
    rig.run(100);
    CHECK(rig.server->stats().coopRejected == before + 2);
    collect(c);
    CHECK(b.sendCoopCreatures(cs));
    b.sendLevelChange(7, exit);
    rig.run(100);
    CHECK(count<CoopCreatures>(collect(c)) == 1);
    MatchClient& d = rig.addClient("Newer", 0xC00);
    rig.run(200);
    CHECK(d.connected() && d.coopAuthority() == b.playerId());

    // Everyone gone: the next first joiner owns the world again.
    b.disconnect();
    c.disconnect();
    d.disconnect();
    rig.run(300);
    CHECK(rig.server->players().empty());
    MatchClient& e = rig.addClient("Fresh", 0xC00);
    rig.run(200);
    CHECK(e.isCoopAuthority() && rig.server->coopAuthority() == e.playerId());
}

void configSwitch() {
    // A lobby configured as deathmatch has no authority; switching the mode to Coop appoints the lowest id, and back.
    Rig rig(coopOptions(MatchMode::Deathmatch));
    MatchClient& a = rig.addClient("A", 0xC00);
    MatchClient& b = rig.addClient("B", 0xC00);
    rig.run(200);
    CHECK(a.coopAuthority() == kNoPlayer);
    MatchConfigData cfg = rig.server->config();
    cfg.mode = MatchMode::Coop;
    CHECK(rig.server->setConfig(cfg));
    rig.run(100);
    CHECK(a.isCoopAuthority() && b.coopAuthority() == a.playerId() && b.config().mode == MatchMode::Coop);
    cfg.mode = MatchMode::Deathmatch;
    CHECK(rig.server->setConfig(cfg));
    rig.run(100);
    CHECK(a.coopAuthority() == kNoPlayer && b.coopAuthority() == kNoPlayer);
}

void levelChange() {
    for (MatchMode mode : {MatchMode::Coop, MatchMode::Deathmatch}) {
        Rig rig(coopOptions(mode));
        MatchClient& a = rig.addClient("Host", 0xC00);
        rig.run(100);
        MatchClient& b = rig.addClient("Guest", 0xC00);
        MatchClient& c = rig.addClient("Third", 0xC00);
        rig.run(200);
        const float pos[3] = {157696.0f, 193235.0f, 428032.0f};

        // Only while playing.
        a.sendLevelChange(1, pos);
        rig.run(100);
        CHECK(count<LevelChange>(collect(b)) == 0 && rig.server->stats().coopRejected == 1);

        startPlaying(rig);
        collect(a);
        collect(b);
        collect(c);
        // The leader (the co-op authority / the lowest id in deathmatch, where it is the host of a listen server).
        CHECK(a.sendLevelChange(0xCAFEF00Du, pos));
        rig.run(100);
        auto evB = collect(b), evC = collect(c);
        CHECK(count<LevelChange>(evB) == 1 && count<LevelChange>(evC) == 1 && count<LevelChange>(collect(a)) == 0);
        const LevelChange* lc = first<LevelChange>(evB);
        CHECK(lc->player == a.playerId() && lc->destHash == 0xCAFEF00Du && lc->exit[0] == 157696.0f &&
              lc->exit[1] == 193235.0f && lc->exit[2] == 428032.0f);
        // A guest cannot make the others change level.
        const uint32_t rej = rig.server->stats().coopRejected;
        b.sendLevelChange(2, pos);
        rig.run(100);
        CHECK(count<LevelChange>(collect(a)) == 0 && count<LevelChange>(collect(c)) == 0);
        CHECK(rig.server->stats().coopRejected == rej + 1);
        // Absurd coordinates are refused.
        const float far[3] = {5.0e7f, 0, 0};
        a.sendLevelChange(3, far);
        rig.run(100);
        CHECK(count<LevelChange>(collect(b)) == 0);
    }
}

void versionMismatch() {
    Rig rig(coopOptions());
    // A v5 peer: same Hello layout, older version number.
    auto raw = rig.net.createTransport();
    const PeerId p = raw->connect("127.0.0.1", kPort);
    rig.run(50);
    Hello h;
    h.protocolVersion = 5;
    h.profileHash = 0xC00;
    h.name = "Old";
    TransportEvent ev;
    CHECK(raw->poll(ev) && ev.type == TransportEventType::Connected);
    CHECK(raw->send(p, Channel::Reliable, encode(h)));
    rig.run(50);
    bool rejected = false;
    while (raw->poll(ev))
        if (ev.type == TransportEventType::Message) {
            Reject r;
            rejected =
                decode(ev.data, r) && r.reason == RejectReason::VersionMismatch && r.serverVersion == kProtocolVersion;
        }
    CHECK(rejected && rig.server->players().empty());
    // The discovery query of an old client is not answered either.
    Query q;
    q.protocolVersion = 5;
    CHECK(rig.server->answerQuery(encode(q)).empty());
    q.protocolVersion = kProtocolVersion;
    ServerInfo info;
    CHECK(decode(rig.server->answerQuery(encode(q)), info) && info.mode == MatchMode::Coop);
}

// A whole co-op session over a lossy, laggy link: reliable events arrive exactly once and in order, the unreliable
// creature stream arrives in part.
void sessionWithLoss() {
    Rig rig(coopOptions());
    LoopbackNetwork::Conditions cond;
    cond.latencyMs = 40;
    cond.lossPercent = 15.0;
    cond.seed = 11;
    rig.net.setConditions(cond);
    MatchClient& host = rig.addClient("Host", 0xC00);
    rig.run(400);
    MatchClient& guest = rig.addClient("Guest", 0xC00);
    rig.run(600);
    CHECK(host.isCoopAuthority() && guest.coopAuthority() == host.playerId());
    startPlaying(rig);
    rig.run(300);
    collect(host);
    collect(guest);

    uint32_t creaturesSent = 0;
    for (uint32_t i = 0; i < 60; ++i) {
        CoopCreatures cs;
        cs.levelHash = 5;
        cs.sequence = i;
        CoopCreature cr;
        cr.key = 1;
        cr.pos[0] = static_cast<float>(i);
        cs.creatures.push_back(cr);
        host.sendCoopCreatures(cs);
        ++creaturesSent;
        if (i % 10 == 0) {
            host.sendCoopEvent(event(CoopKind::CreatureDied, 1000 + i));
            guest.sendCoopEvent(event(CoopKind::Claim, 2000 + i, 4));
        }
        rig.run(100);
    }
    rig.run(1500);
    const auto gev = collect(guest), hev = collect(host);
    const auto died = coopEvents(gev);
    const auto claims = coopEvents(hev);
    CHECK(died.size() == 6 && claims.size() == 6);
    for (size_t i = 0; i < 6; ++i) {
        CHECK(died[i].key == 1000 + 10 * i); // exactly once, in order
        CHECK(claims[i].key == 2000 + 10 * i && claims[i].player == guest.playerId());
    }
    const size_t got = count<CoopCreatures>(gev);
    CHECK(got > creaturesSent / 2 && got < creaturesSent + 1);
}

// Host migration over a lossy, laggy link: the authority leaves mid-session, the oldest remaining player takes over,
// the reliable events keep arriving exactly once and in order, claims go to the new owner, a late joiner is told.
void migrationWithLoss() {
    Rig rig(coopOptions());
    LoopbackNetwork::Conditions cond;
    cond.latencyMs = 40;
    cond.lossPercent = 15.0;
    cond.seed = 23;
    rig.net.setConditions(cond);
    MatchClient& a = rig.addClient("Owner", 0xC00);
    rig.run(400);
    MatchClient& b = rig.addClient("Second", 0xC00);
    rig.run(400);
    MatchClient& c = rig.addClient("Third", 0xC00);
    rig.run(600);
    CHECK(a.isCoopAuthority() && b.coopAuthority() == a.playerId() && c.coopAuthority() == a.playerId());
    startPlaying(rig);
    rig.run(300);
    collect(a);
    collect(b);
    collect(c);

    // Before: the owner's deaths reach both guests exactly once, a guest's claim reaches the owner.
    for (uint32_t i = 0; i < 5; ++i) {
        a.sendCoopEvent(event(CoopKind::CreatureDied, 100 + i));
        b.sendCoopEvent(event(CoopKind::Claim, 200 + i, 3));
        rig.run(150);
    }
    rig.run(1500);
    auto evB = coopEvents(collect(b)), evC = coopEvents(collect(c)), evA = coopEvents(collect(a));
    CHECK(evB.size() == 5 && evC.size() == 5 && evA.size() == 5);
    for (size_t i = 0; i < 5; ++i)
        CHECK(evB[i].key == 100 + i && evC[i].key == 100 + i && evA[i].key == 200 + i);

    // The owner drops: the next oldest (b) is announced to c exactly once and b learns it owns the world.
    a.disconnect();
    rig.run(2500); // the disconnect itself may be lost: the server's timeout covers it
    CHECK(rig.server->players().size() == 2);
    CHECK(rig.server->coopAuthority() == b.playerId() && rig.server->stats().coopMigrations == 1);
    CHECK(b.isCoopAuthority() && c.coopAuthority() == b.playerId() && !c.isCoopAuthority());
    const auto cEv = collect(c);
    size_t announcements = 0;
    for (const auto& e : cEv)
        if (const auto* an = std::get_if<CoopAuthority>(&e))
            if (an->player == b.playerId())
                ++announcements;
    CHECK(announcements == 1);
    collect(b);

    // After: the new owner streams and sends deaths, the other guest's claims go to it, each exactly once, in order.
    uint32_t streamed = 0;
    for (uint32_t i = 0; i < 40; ++i) {
        CoopCreatures cs;
        cs.levelHash = 5;
        cs.sequence = i;
        CoopCreature cr;
        cr.key = 1;
        cs.creatures.push_back(cr);
        b.sendCoopCreatures(cs);
        ++streamed;
        if (i % 10 == 0) {
            b.sendCoopEvent(event(CoopKind::CreatureDied, 300 + i));
            c.sendCoopEvent(event(CoopKind::Claim, 400 + i, 2));
        }
        rig.run(100);
    }
    rig.run(1500);
    const auto cAfter = collect(c), bAfter = collect(b);
    const auto died = coopEvents(cAfter);
    const auto claims = coopEvents(bAfter);
    CHECK(died.size() == 4 && claims.size() == 4);
    for (size_t i = 0; i < 4; ++i) {
        CHECK(died[i].key == 300 + 10 * i && died[i].player == b.playerId());
        CHECK(claims[i].key == 400 + 10 * i && claims[i].player == c.playerId());
    }
    const size_t got = count<CoopCreatures>(cAfter);
    CHECK(got > streamed / 2 && got <= streamed);

    // A late joiner after the migration is told who owns the world and can claim to it.
    MatchClient& d = rig.addClient("Late", 0xC00);
    rig.run(1000);
    CHECK(d.connected() && d.coopAuthority() == b.playerId() && !d.isCoopAuthority());
    collect(b);
    d.sendCoopEvent(event(CoopKind::SyncReq));
    rig.run(1000);
    const auto sync = coopEvents(collect(b));
    CHECK(sync.size() == 1 && sync[0].kind == CoopKind::SyncReq && sync[0].player == d.playerId());

    // Chain: the second owner leaves too, c (next oldest) follows, then the last one is alone and owns it.
    b.disconnect();
    rig.run(8000);
    CHECK(rig.server->coopAuthority() == c.playerId() && c.isCoopAuthority() && d.coopAuthority() == c.playerId());
    c.disconnect();
    rig.run(8000);
    CHECK(rig.server->coopAuthority() == d.playerId() && d.isCoopAuthority());
    CHECK(rig.server->stats().coopMigrations == 3);
}

} // namespace

int main() {
    relayRules();
    notCoopDropsEverything();
    authorityLifecycle();
    configSwitch();
    levelChange();
    versionMismatch();
    sessionWithLoss();
    migrationWithLoss();
    std::puts("net_coop_test ok");
    return 0;
}
