// Serialiser round trips and malformed-input rejection. No sockets.
#include "TestUtil.h"
#include "net/Protocol.h"

#include <cmath>
#include <limits>

using namespace drakan::net;

namespace {

PlayerState sampleState() {
    PlayerState s;
    s.player = 3;
    s.sequence = 0xFFFFFFF0u;
    s.tick = 12345;
    s.pos[0] = 1.5f;
    s.pos[1] = -2.25f;
    s.pos[2] = 1000.125f;
    s.yaw = 3.0f;
    s.vel[0] = 0.5f;
    s.vel[1] = 0;
    s.vel[2] = -8;
    s.animId = 513;
    s.animTime = 0.75f;
    s.upperAnimId = 1027;
    s.upperAnimTime = 0.25f;
    s.lowerMask = 0x19;
    s.weaponRight = 0x33cbc;
    s.weaponLeft = 0x4d1;
    s.mounted = true;
    s.levelHash = 0xCAFEF00Du;
    s.character = 0x0ee7;
    s.pitch = -0.4f;
    s.roll = 0.25f;
    s.clipHi = 4;
    return s;
}

// `olderSize`: the length (type byte included) an older peer wrote when the message later gained trailing optional
// fields; those prefixes are accepted on purpose (default: none).
template <class T>
void expectTruncationsFail(const T& msg, size_t olderSize = SIZE_MAX, size_t olderSize2 = SIZE_MAX) {
    const auto bytes = encode(msg);
    T out;
    CHECK(decode(bytes, out));
    for (size_t n = 0; n < bytes.size(); ++n) {
        T t;
        if (n == olderSize || n == olderSize2)
            CHECK(decode(bytes.data(), n, t));
        else
            CHECK(!decode(bytes.data(), n, t)); // every proper prefix is rejected
    }
    auto longer = bytes;
    longer.push_back(0);
    T t;
    CHECK(!decode(longer, t)); // trailing bytes are rejected
}

} // namespace

int main() {
    // Explicit little-endian layout.
    {
        ByteWriter w;
        w.u16(0x0102);
        w.u32(0x03040506);
        w.u64(0x0708090A0B0C0D0Eull);
        const uint8_t expect[] = {0x02, 0x01, 0x06, 0x05, 0x04, 0x03, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07};
        CHECK(w.bytes().size() == sizeof(expect));
        for (size_t i = 0; i < sizeof(expect); ++i)
            CHECK(w.bytes()[i] == expect[i]);
        ByteWriter f;
        f.f32(1.0f); // 0x3F800000
        CHECK(f.bytes()[0] == 0 && f.bytes()[1] == 0 && f.bytes()[2] == 0x80 && f.bytes()[3] == 0x3F);
    }

    // Round trips.
    {
        Hello h;
        h.profileHash = 0xDEADBEEFCAFEF00Dull;
        h.name = "Rynn";
        Hello o;
        CHECK(decode(encode(h), o));
        CHECK(o.protocolVersion == kProtocolVersion && o.profileHash == h.profileHash && o.name == "Rynn");

        Welcome w;
        w.player = 2;
        w.phase = MatchPhase::Loading;
        w.serverTimeMs = 99;
        w.config.mode = MatchMode::TeamDeathmatch;
        w.config.level = "ruins";
        w.config.fragLimit = 7;
        w.config.maxPlayers = 12;
        Welcome wo;
        CHECK(decode(encode(w), wo));
        CHECK(wo.player == 2 && wo.phase == MatchPhase::Loading && wo.serverTimeMs == 99);
        CHECK(wo.config.mode == MatchMode::TeamDeathmatch && wo.config.level == "ruins" && wo.config.fragLimit == 7 &&
              wo.config.maxPlayers == 12);

        Reject r;
        r.reason = RejectReason::ProfileMismatch;
        r.message = "nope";
        Reject ro;
        CHECK(decode(encode(r), ro) && ro.reason == RejectReason::ProfileMismatch && ro.message == "nope");

        const PlayerState s = sampleState();
        PlayerState so;
        CHECK(decode(encode(s), so));
        CHECK(so.player == 3 && so.sequence == s.sequence && so.tick == s.tick && so.pos[2] == s.pos[2] &&
              so.yaw == s.yaw && so.vel[2] == -8 && so.animId == 513 && so.animTime == 0.75f &&
              so.weaponRight == 0x33cbc && so.weaponLeft == 0x4d1 && so.mounted);
        CHECK(so.upperAnimId == 1027 && so.upperAnimTime == 0.25f && so.lowerMask == 0x19 &&
              so.levelHash == 0xCAFEF00Du);
        CHECK(encode(s).size() == 1 + kPlayerStateWireSize);
        CHECK(so.character == s.character &&
              so.character == 0x0ee7); // v7: the stand-in character travels with the state
        CHECK(std::fabs(so.pitch + 0.4f) < 1e-4f && std::fabs(so.roll - 0.25f) < 1e-4f &&
              so.clipHi == 4); // v8: a mounted sender's orientation and clip prefix

        Snapshot snap;
        snap.serverTimeMs = 4242;
        snap.players = {s, s};
        snap.players[1].player = 5;
        Snapshot sno;
        CHECK(decode(encode(snap), sno) && sno.players.size() == 2 && sno.players[1].player == 5 &&
              sno.serverTimeMs == 4242);
        // Worst case snapshot fits one packet.
        snap.players.assign(kMaxPlayers, s);
        CHECK(encode(snap).size() <= kMaxPacketPayload);

        Hit hit;
        hit.attacker = 1;
        hit.victim = 2;
        hit.weapon = 9;
        hit.damage = 33;
        hit.healthAfter = 67;
        Hit hito;
        CHECK(decode(encode(hit), hito) && hito.victim == 2 && hito.damage == 33 && hito.healthAfter == 67);

        Death d;
        d.victim = 2;
        d.killer = 1;
        Death dout;
        CHECK(decode(encode(d), dout) && dout.killer == 1);
        Respawn rs;
        rs.player = 4;
        rs.spawnIndex = 200;
        rs.health = 100;
        Respawn rso;
        CHECK(decode(encode(rs), rso) && rso.spawnIndex == 200 && rso.health == 100);
        Chat c;
        c.player = 1;
        c.text = "hello";
        Chat co;
        CHECK(decode(encode(c), co) && co.text == "hello");
        Score sc;
        sc.player = 1;
        sc.kills = 5;
        sc.deaths = 2;
        sc.teamKills[1] = 9;
        Score sco;
        CHECK(decode(encode(sc), sco) && sco.kills == 5 && sco.teamKills[1] == 9);
        PlayerJoin pj;
        pj.player = 1;
        pj.team = 1;
        pj.name = "A";
        PlayerJoin pjo;
        CHECK(decode(encode(pj), pjo) && pjo.team == 1 && pjo.name == "A");
        PlayerLeave pl;
        pl.player = 6;
        PlayerLeave plo;
        CHECK(decode(encode(pl), plo) && plo.player == 6);
        PhaseChange pc;
        pc.phase = MatchPhase::Results;
        PhaseChange pco;
        CHECK(decode(encode(pc), pco) && pco.phase == MatchPhase::Results);
        LoadDone ld;
        LoadDone ldo;
        CHECK(decode(encode(ld), ldo));
    }

    // Truncation and trailing garbage.
    expectTruncationsFail(Hello{});
    expectTruncationsFail(Welcome{.player = 1}, encode(Welcome{.player = 1}).size() - 2); // modeId is optional
    expectTruncationsFail(Reject{});
    expectTruncationsFail(sampleState());
    expectTruncationsFail(Hit{});
    expectTruncationsFail(Death{});
    expectTruncationsFail(Respawn{});
    expectTruncationsFail(Chat{});
    expectTruncationsFail(Score{}, 10, 30); // score / teamScore (the v8 body is 9 bytes) and lives are optional
    expectTruncationsFail(PlayerJoin{});
    expectTruncationsFail(PlayerLeave{});
    expectTruncationsFail(PhaseChange{}, 6); // byHost is optional (the older body is 5 bytes)

    // Wrong type byte, empty, unknown types.
    {
        MsgType t;
        CHECK(!peekType(nullptr, 0, t));
        const uint8_t zero[] = {0};
        const uint8_t big[] = {200};
        CHECK(!peekType(zero, 1, t));
        CHECK(!peekType(big, 1, t));
        Death d;
        CHECK(!decode(encode(Hit{}), d));
    }

    // Semantic rejects.
    {
        // Non-finite floats.
        PlayerState s = sampleState();
        s.pos[1] = std::numeric_limits<float>::quiet_NaN();
        PlayerState o;
        CHECK(!decode(encode(s), o));
        s = sampleState();
        s.yaw = std::numeric_limits<float>::infinity();
        CHECK(!decode(encode(s), o));

        // Bad enum values.
        auto bytes = encode(PhaseChange{});
        bytes[1] = 9;
        PhaseChange pc;
        CHECK(!decode(bytes, pc));

        // Bad mounted flag.
        bytes = encode(sampleState());
        bytes[bytes.size() - 1 - sizeof(uint32_t) - 4 * sizeof(uint16_t)] =
            2; // the flag precedes levelHash, character, pitch, roll, clipHi
        CHECK(!decode(bytes, o));

        // Oversized strings.
        Hello h;
        h.name = std::string(kMaxNameLength + 1, 'x');
        Hello ho;
        CHECK(!decode(encode(h), ho));
        Chat c;
        c.text = std::string(kMaxChatLength + 1, 'x');
        Chat co;
        CHECK(!decode(encode(c), co));

        // Length prefix larger than the buffer.
        bytes = encode(Hello{});
        bytes[1 + 2 + 8] = 0xFF;
        bytes[1 + 2 + 8 + 1] = 0xFF;
        CHECK(!decode(bytes, ho));

        // Invalid config and welcome player id.
        Welcome w;
        w.player = 1;
        w.config.maxPlayers = 0;
        Welcome wo;
        CHECK(!decode(encode(w), wo));
        w.config.maxPlayers = 8;
        w.player = 0;
        CHECK(!decode(encode(w), wo));
        w.player = 1;
        CHECK(decode(encode(w), wo));

        // Snapshot claiming more players than allowed.
        Snapshot snap;
        auto sb = encode(snap);
        sb[1 + 4] = 200;
        Snapshot sno;
        CHECK(!decode(sb, sno));

        // Bad team value.
        PlayerJoin pj;
        pj.team = 7;
        PlayerJoin pjo;
        CHECK(!decode(encode(pj), pjo));
    }

    // v5: password, server name, Ready, discovery messages.
    {
        Hello h;
        h.name = "Rynn";
        h.password = "secret";
        Hello o;
        CHECK(decode(encode(h), o) && o.password == "secret");
        h.password = std::string(kMaxPasswordLength + 1, 'x');
        CHECK(!decode(encode(h), o)); // over the per-field maximum

        MatchConfigData c;
        c.serverName = "Rynn's keep";
        MatchConfigMsg m, mo;
        m.config = c;
        CHECK(decode(encode(m), mo) && mo.config.serverName == "Rynn's keep");

        Ready r;
        r.player = 3;
        r.ready = true;
        Ready ro;
        CHECK(decode(encode(r), ro) && ro.player == 3 && ro.ready);
        std::vector<uint8_t> bad = encode(r);
        bad.back() = 2;
        CHECK(!decode(bad, ro));

        Query q;
        q.token = 0x12345678u;
        Query qo;
        CHECK(decode(encode(q), qo) && qo.token == q.token && qo.protocolVersion == kProtocolVersion);
        CHECK(encode(q).size() == 1 + 2 + 4);

        ServerInfo i;
        i.token = 9;
        i.serverId = 0xA1B2C3D4u;
        i.name = "LAN party";
        i.level = "Level 1B - Kragmor";
        i.mode = MatchMode::TeamDeathmatch;
        i.phase = MatchPhase::Playing;
        i.players = 3;
        i.maxPlayers = 8;
        i.passwordProtected = true;
        ServerInfo io;
        CHECK(decode(encode(i), io) && io.token == 9 && io.serverId == 0xA1B2C3D4u && io.name == "LAN party" &&
              io.level == i.level && io.mode == MatchMode::TeamDeathmatch && io.phase == MatchPhase::Playing &&
              io.players == 3 && io.maxPlayers == 8 && io.passwordProtected);
        i.players = 9; // more players than slots is not a status
        CHECK(!decode(encode(i), io));
        MsgType t;
        const std::vector<uint8_t> qb = encode(q);
        CHECK(peekType(qb.data(), qb.size(), t) && t == MsgType::Query);
    }

    // v6: co-op messages round trip, strict lengths and ranges.
    {
        static_assert(kProtocolVersion == 8);
        CoopAuthority a;
        a.player = 2;
        CoopAuthority ao;
        CHECK(decode(encode(a), ao) && ao.player == 2);
        expectTruncationsFail(a);
        a.player = kMaxPlayers + 1;
        CHECK(!decode(encode(a), ao));

        CoopCreatures cs;
        cs.levelHash = 0xABCD1234u;
        cs.sequence = 77;
        for (uint32_t i = 0; i < 3; ++i) {
            CoopCreature c;
            c.key = 0x1000 + i;
            c.pos[0] = 10.5f * static_cast<float>(i);
            c.pos[1] = -3;
            c.pos[2] = 4096.25f;
            c.yaw = 1.5f;
            c.hp = static_cast<int32_t>(50 - 30 * static_cast<int>(i)); // goes negative
            cs.creatures.push_back(c);
        }
        CoopCreatures co;
        CHECK(decode(encode(cs), co) && co.levelHash == 0xABCD1234u && co.sequence == 77 && co.creatures.size() == 3);
        CHECK(co.creatures[2].key == 0x1002 && co.creatures[2].hp == -10 && co.creatures[1].pos[0] == 10.5f);
        CHECK(encode(cs).size() == 1 + 4 + 4 + 2 + 3 * kCoopCreatureWireSize);
        expectTruncationsFail(cs);
        CoopCreatures full;
        full.creatures.assign(kMaxCoopCreatures, CoopCreature{});
        CHECK(encode(full).size() <= kMaxPacketPayload && decode(encode(full), co) && co.creatures.size() == 45);
        std::vector<uint8_t> big = encode(full);
        big[9] = static_cast<uint8_t>(kMaxCoopCreatures + 1); // count > max (header: type, level, seq, count lo)
        big.resize(big.size() + kCoopCreatureWireSize, 0);
        CHECK(!decode(big, co));
        CoopCreatures nan = cs;
        nan.creatures[1].pos[2] = std::numeric_limits<float>::quiet_NaN();
        CHECK(!decode(encode(nan), co));

        // Every kind round trips with exactly its fields and rejects truncation / trailing bytes.
        for (uint8_t k = kCoopKindMin; k <= kCoopKindMax; ++k) {
            CoopEvent e;
            e.kind = static_cast<CoopKind>(k);
            e.levelHash = 0x55AA55AAu;
            e.player = 3;
            e.key = 0xDEADBEEFu;
            e.value = -1234;
            e.msgType = 0x10c1;
            e.w[0] = 1;
            e.w[1] = -2;
            e.w[2] = 3;
            e.w[3] = 0x7fffffff;
            CoopEvent eo;
            const auto bytes = encode(e);
            CHECK(decode(bytes, eo) && eo.kind == e.kind && eo.levelHash == e.levelHash && eo.player == 3);
            const bool hasKey = e.kind == CoopKind::CreatureDied || e.kind == CoopKind::Claim ||
                                e.kind == CoopKind::ObjGone || e.kind == CoopKind::ObjMsg ||
                                e.kind == CoopKind::ObjState;
            const bool hasValue = e.kind == CoopKind::Claim || e.kind == CoopKind::Hurt || e.kind == CoopKind::ObjState;
            const bool isMsg = e.kind == CoopKind::ObjMsg;
            CHECK(eo.key == (hasKey ? e.key : 0u) && eo.value == (hasValue ? e.value : 0));
            CHECK(eo.msgType == (isMsg ? e.msgType : 0) && eo.w[1] == (isMsg ? -2 : 0) &&
                  eo.w[3] == (isMsg ? 0x7fffffff : 0));
            CHECK(bytes.size() == 1 + 1 + 4 + 1 + (hasKey ? 4u : 0u) + (hasValue ? 4u : 0u) + (isMsg ? 2u + 16u : 0u));
            expectTruncationsFail(e);
        }
        CoopEvent bad;
        auto bb = encode(bad);
        bb[1] = 1; // kind 1 does not exist
        CoopEvent bo;
        CHECK(!decode(bb, bo));
        bb[1] = 11;
        CHECK(!decode(bb, bo));
        bb[1] = 2;
        bb[6] = kMaxPlayers + 1; // player out of range
        CHECK(!decode(bb, bo));
        CHECK(coopFromAuthorityOnly(CoopKind::Hurt) && !coopFromAuthorityOnly(CoopKind::ObjGone));
        CHECK(coopToAuthorityOnly(CoopKind::SyncReq) && !coopToAuthorityOnly(CoopKind::Respawn));

        LevelChange lc;
        lc.player = 1;
        lc.destHash = 0x0BADF00Du;
        lc.exit[0] = 157696.0f;
        lc.exit[1] = 193235.5f;
        lc.exit[2] = -428032.0f;
        LevelChange lo;
        CHECK(decode(encode(lc), lo) && lo.player == 1 && lo.destHash == 0x0BADF00Du && lo.exit[0] == 157696.0f &&
              lo.exit[1] == 193235.5f && lo.exit[2] == -428032.0f);
        expectTruncationsFail(lc);
        lc.exit[1] = std::numeric_limits<float>::infinity();
        CHECK(!decode(encode(lc), lo));

        MsgType t;
        const auto lb = encode(LevelChange{});
        CHECK(peekType(lb.data(), lb.size(), t) && t == MsgType::LevelChange);
        const uint8_t beyond[] = {33}; // one past MapVoteState (32)
        CHECK(!peekType(beyond, 1, t));

        // The co-op mode travels in MatchConfig and ServerInfo.
        MatchConfigData cfg;
        cfg.mode = MatchMode::Coop;
        cfg.level = "Level 1A - Surdana";
        ByteWriter w;
        cfg.write(w);
        MatchConfigData cfgo;
        ByteReader r(w.bytes().data(), w.bytes().size());
        CHECK(cfgo.read(r) && cfgo.mode == MatchMode::Coop);
        std::vector<uint8_t> raw = w.bytes();
        raw[0] = 3; // no such mode
        ByteReader r2(raw.data(), raw.size());
        CHECK(!cfgo.read(r2));
        ServerInfo si;
        si.mode = MatchMode::Coop;
        ServerInfo sio;
        CHECK(decode(encode(si), sio) && sio.mode == MatchMode::Coop);
    }

    // Reader never reads out of bounds.
    {
        const uint8_t one[] = {1};
        ByteReader r(one, 1);
        CHECK(r.u8() == 1 && r.ok());
        CHECK(r.u32() == 0 && !r.ok());
        CHECK(r.u8() == 0 && !r.ok()); // stays failed
    }

    // Sequence wrap.
    CHECK(sequenceNewer(1, 0));
    CHECK(sequenceNewer(0, 0xFFFFFFFFu));
    CHECK(!sequenceNewer(0xFFFFFFFFu, 0));
    CHECK(!sequenceNewer(5, 5));

    std::puts("net_protocol_test ok");
    return 0;
}
