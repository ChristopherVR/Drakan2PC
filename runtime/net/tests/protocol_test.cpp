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
    return s;
}

template <class T>
void expectTruncationsFail(const T& msg) {
    const auto bytes = encode(msg);
    T out;
    CHECK(decode(bytes, out));
    for (size_t n = 0; n < bytes.size(); ++n) {
        T t;
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
    expectTruncationsFail(Welcome{.player = 1});
    expectTruncationsFail(Reject{});
    expectTruncationsFail(sampleState());
    expectTruncationsFail(Hit{});
    expectTruncationsFail(Death{});
    expectTruncationsFail(Respawn{});
    expectTruncationsFail(Chat{});
    expectTruncationsFail(Score{});
    expectTruncationsFail(PlayerJoin{});
    expectTruncationsFail(PlayerLeave{});
    expectTruncationsFail(PhaseChange{});

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
        bytes[bytes.size() - 1 - sizeof(uint32_t)] = 2; // the flag precedes levelHash
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
