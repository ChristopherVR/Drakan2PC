#include "net/Protocol.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace drakan::net {

void ByteWriter::u16(uint16_t v) {
    u8(static_cast<uint8_t>(v));
    u8(static_cast<uint8_t>(v >> 8));
}
void ByteWriter::u32(uint32_t v) {
    u16(static_cast<uint16_t>(v));
    u16(static_cast<uint16_t>(v >> 16));
}
void ByteWriter::u64(uint64_t v) {
    u32(static_cast<uint32_t>(v));
    u32(static_cast<uint32_t>(v >> 32));
}
void ByteWriter::f32(float v) {
    u32(std::bit_cast<uint32_t>(v));
}
void ByteWriter::str(const std::string& s) {
    const size_t n = s.size() > 0xFFFF ? 0xFFFF : s.size();
    u16(static_cast<uint16_t>(n));
    buf_.insert(buf_.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(n));
}

bool ByteReader::need(size_t n) {
    if (!ok_ || size_ - pos_ < n) {
        ok_ = false;
        return false;
    }
    return true;
}
uint8_t ByteReader::u8() {
    return need(1) ? data_[pos_++] : 0;
}
uint16_t ByteReader::u16() {
    if (!need(2))
        return 0;
    const uint16_t v = static_cast<uint16_t>(data_[pos_] | (data_[pos_ + 1] << 8));
    pos_ += 2;
    return v;
}
uint32_t ByteReader::u32() {
    const uint32_t lo = u16();
    const uint32_t hi = u16();
    return lo | (hi << 16);
}
uint64_t ByteReader::u64() {
    const uint64_t lo = u32();
    const uint64_t hi = u32();
    return lo | (hi << 32);
}
float ByteReader::f32() {
    const float v = std::bit_cast<float>(u32());
    if (!std::isfinite(v)) {
        ok_ = false;
        return 0;
    }
    return v;
}
std::string ByteReader::str(size_t maxLen) {
    const size_t n = u16();
    if (n > maxLen) {
        ok_ = false;
        return {};
    }
    if (!need(n))
        return {};
    std::string s(reinterpret_cast<const char*>(data_ + pos_), n);
    pos_ += n;
    return s;
}

namespace {
template <class E>
void readEnum(ByteReader& r, E& out, uint8_t maxValue) {
    const uint8_t v = r.u8();
    if (v > maxValue) {
        r.fail();
        return;
    }
    out = static_cast<E>(v);
}
} // namespace

void MatchConfigData::write(ByteWriter& w) const {
    w.u8(static_cast<uint8_t>(mode));
    w.str(level);
    w.u16(fragLimit);
    w.u16(timeLimitSec);
    w.u8(maxPlayers);
    w.u16(respawnDelayMs);
    w.u16(startHealth);
    w.str(serverName);
}
bool MatchConfigData::read(ByteReader& r) {
    readEnum(r, mode, 2);
    level = r.str(kMaxLevelLength);
    fragLimit = r.u16();
    timeLimitSec = r.u16();
    maxPlayers = r.u8();
    respawnDelayMs = r.u16();
    startHealth = r.u16();
    serverName = r.str(kMaxServerNameLength);
    if (maxPlayers < 1 || maxPlayers > kMaxPlayers || startHealth == 0)
        r.fail();
    return r.ok();
}

void Hello::write(ByteWriter& w) const {
    w.u16(protocolVersion);
    w.u64(profileHash);
    w.str(name);
    w.str(password);
}
bool Hello::read(ByteReader& r) {
    protocolVersion = r.u16();
    profileHash = r.u64();
    name = r.str(kMaxNameLength);
    password = r.str(kMaxPasswordLength);
    return r.ok();
}

void Welcome::write(ByteWriter& w) const {
    w.u8(player);
    w.u8(tickRateHz);
    w.u8(static_cast<uint8_t>(phase));
    w.u32(serverTimeMs);
    config.write(w);
}
bool Welcome::read(ByteReader& r) {
    player = r.u8();
    tickRateHz = r.u8();
    readEnum(r, phase, 3);
    serverTimeMs = r.u32();
    config.read(r);
    if (player == kNoPlayer || player > kMaxPlayers)
        r.fail();
    return r.ok();
}

void Reject::write(ByteWriter& w) const {
    w.u8(static_cast<uint8_t>(reason));
    w.u16(serverVersion);
    w.str(message);
}
bool Reject::read(ByteReader& r) {
    readEnum(r, reason, 5);
    serverVersion = r.u16();
    message = r.str(kMaxRejectMessageLength);
    return r.ok();
}

void PhaseChange::write(ByteWriter& w) const {
    w.u8(static_cast<uint8_t>(phase));
    w.u32(serverTimeMs);
}
bool PhaseChange::read(ByteReader& r) {
    readEnum(r, phase, 3);
    serverTimeMs = r.u32();
    return r.ok();
}

void PlayerState::write(ByteWriter& w) const {
    w.u8(player);
    w.u32(sequence);
    w.u32(tick);
    for (float v : pos)
        w.f32(v);
    w.f32(yaw);
    for (float v : vel)
        w.f32(v);
    w.u16(animId);
    w.f32(animTime);
    w.u16(upperAnimId);
    w.f32(upperAnimTime);
    w.u32(lowerMask);
    w.u32(weaponRight);
    w.u32(weaponLeft);
    w.u8(mounted ? 1 : 0);
    w.u32(levelHash);
    w.u16(character);
    w.u16(static_cast<uint16_t>(static_cast<int16_t>(std::lround(pitch * 10000.0f))));
    w.u16(static_cast<uint16_t>(static_cast<int16_t>(std::lround(roll * 10000.0f))));
    w.u16(clipHi);
}
bool PlayerState::read(ByteReader& r) {
    player = r.u8();
    sequence = r.u32();
    tick = r.u32();
    for (float& v : pos)
        v = r.f32();
    yaw = r.f32();
    for (float& v : vel)
        v = r.f32();
    animId = r.u16();
    animTime = r.f32();
    upperAnimId = r.u16();
    upperAnimTime = r.f32();
    lowerMask = r.u32();
    weaponRight = r.u32();
    weaponLeft = r.u32();
    const uint8_t flags = r.u8();
    if (flags > 1)
        r.fail();
    mounted = flags == 1;
    levelHash = r.u32();
    character = r.u16();
    pitch = static_cast<float>(static_cast<int16_t>(r.u16())) / 10000.0f;
    roll = static_cast<float>(static_cast<int16_t>(r.u16())) / 10000.0f;
    clipHi = r.u16();
    return r.ok();
}

void Snapshot::write(ByteWriter& w) const {
    w.u32(serverTimeMs);
    const size_t n = players.size() > kMaxPlayers ? kMaxPlayers : players.size();
    w.u8(static_cast<uint8_t>(n));
    for (size_t i = 0; i < n; ++i)
        players[i].write(w);
}
bool Snapshot::read(ByteReader& r) {
    serverTimeMs = r.u32();
    const size_t n = r.u8();
    if (n > kMaxPlayers) {
        r.fail();
        return false;
    }
    players.assign(n, PlayerState{});
    for (auto& p : players)
        p.read(r);
    return r.ok();
}

void Hit::write(ByteWriter& w) const {
    w.u8(attacker);
    w.u8(victim);
    w.u8(weapon);
    w.u16(damage);
    w.u16(healthAfter);
}
bool Hit::read(ByteReader& r) {
    attacker = r.u8();
    victim = r.u8();
    weapon = r.u8();
    damage = r.u16();
    healthAfter = r.u16();
    return r.ok();
}

void Death::write(ByteWriter& w) const {
    w.u8(victim);
    w.u8(killer);
    w.u8(weapon);
}
bool Death::read(ByteReader& r) {
    victim = r.u8();
    killer = r.u8();
    weapon = r.u8();
    return r.ok();
}

void Respawn::write(ByteWriter& w) const {
    w.u8(player);
    w.u8(spawnIndex);
    w.u16(health);
}
bool Respawn::read(ByteReader& r) {
    player = r.u8();
    spawnIndex = r.u8();
    health = r.u16();
    return r.ok();
}

void Chat::write(ByteWriter& w) const {
    w.u8(player);
    w.str(text);
}
bool Chat::read(ByteReader& r) {
    player = r.u8();
    text = r.str(kMaxChatLength);
    return r.ok();
}

void Score::write(ByteWriter& w) const {
    w.u8(player);
    w.u16(kills);
    w.u16(deaths);
    w.u16(teamKills[0]);
    w.u16(teamKills[1]);
}
bool Score::read(ByteReader& r) {
    player = r.u8();
    kills = r.u16();
    deaths = r.u16();
    teamKills[0] = r.u16();
    teamKills[1] = r.u16();
    return r.ok();
}

void PlayerJoin::write(ByteWriter& w) const {
    w.u8(player);
    w.u8(team);
    w.str(name);
}
bool PlayerJoin::read(ByteReader& r) {
    player = r.u8();
    team = r.u8();
    name = r.str(kMaxNameLength);
    if (team != kNoTeam && team > 1)
        r.fail();
    return r.ok();
}

void PlayerLeave::write(ByteWriter& w) const {
    w.u8(player);
}
bool PlayerLeave::read(ByteReader& r) {
    player = r.u8();
    return r.ok();
}

bool peekType(const uint8_t* data, size_t size, MsgType& out) {
    if (size == 0)
        return false;
    if (data[0] < static_cast<uint8_t>(MsgType::Hello) || data[0] > static_cast<uint8_t>(MsgType::Character))
        return false;
    out = static_cast<MsgType>(data[0]);
    return true;
}

void Ready::write(ByteWriter& w) const {
    w.u8(player);
    w.u8(ready ? 1 : 0);
}
bool Ready::read(ByteReader& r) {
    player = r.u8();
    const uint8_t v = r.u8();
    ready = v == 1;
    if (v > 1)
        r.fail();
    return r.ok();
}

void Query::write(ByteWriter& w) const {
    w.u16(protocolVersion);
    w.u32(token);
}
bool Query::read(ByteReader& r) {
    protocolVersion = r.u16();
    token = r.u32();
    return r.ok();
}

void ServerInfo::write(ByteWriter& w) const {
    w.u16(protocolVersion);
    w.u32(token);
    w.u32(serverId);
    w.str(name);
    w.str(level);
    w.u8(static_cast<uint8_t>(mode));
    w.u8(static_cast<uint8_t>(phase));
    w.u8(players);
    w.u8(maxPlayers);
    w.u8(passwordProtected ? 1 : 0);
    w.str(mods);
}
bool ServerInfo::read(ByteReader& r) {
    protocolVersion = r.u16();
    token = r.u32();
    serverId = r.u32();
    name = r.str(kMaxServerNameLength);
    level = r.str(kMaxLevelLength);
    readEnum(r, mode, 2);
    readEnum(r, phase, 3);
    players = r.u8();
    maxPlayers = r.u8();
    const uint8_t pw = r.u8();
    passwordProtected = pw == 1;
    mods = r.atEnd() ? std::string() : r.str(kMaxModsLength); // older servers stop after the password flag
    if (pw > 1 || maxPlayers < 1 || maxPlayers > kMaxPlayers || players > maxPlayers)
        r.fail();
    return r.ok();
}

void CoopAuthority::write(ByteWriter& w) const {
    w.u8(player);
}
bool CoopAuthority::read(ByteReader& r) {
    player = r.u8();
    if (player > kMaxPlayers)
        r.fail();
    return r.ok();
}

void CoopCreatures::write(ByteWriter& w) const {
    w.u32(levelHash);
    w.u32(sequence);
    const size_t n = std::min(creatures.size(), kMaxCoopCreatures);
    w.u16(static_cast<uint16_t>(n));
    for (size_t i = 0; i < n; ++i) {
        const CoopCreature& c = creatures[i];
        w.u32(c.key);
        for (float v : c.pos)
            w.f32(v);
        w.f32(c.yaw);
        w.u32(static_cast<uint32_t>(c.hp));
    }
}
bool CoopCreatures::read(ByteReader& r) {
    levelHash = r.u32();
    sequence = r.u32();
    const size_t n = r.u16();
    if (n > kMaxCoopCreatures) {
        r.fail();
        return false;
    }
    creatures.clear();
    creatures.reserve(n);
    for (size_t i = 0; i < n && r.ok(); ++i) {
        CoopCreature c;
        c.key = r.u32();
        for (float& v : c.pos)
            v = r.f32();
        c.yaw = r.f32();
        c.hp = static_cast<int32_t>(r.u32());
        creatures.push_back(c);
    }
    return r.ok();
}

void CoopEvent::write(ByteWriter& out) const {
    out.u8(static_cast<uint8_t>(kind));
    out.u32(levelHash);
    out.u8(player);
    switch (kind) {
        case CoopKind::CreatureDied:
        case CoopKind::ObjGone:
            out.u32(key);
            break;
        case CoopKind::Claim:
        case CoopKind::ObjState:
            out.u32(key);
            out.u32(static_cast<uint32_t>(value));
            break;
        case CoopKind::Hurt:
            out.u32(static_cast<uint32_t>(value));
            break;
        case CoopKind::ObjMsg:
            out.u32(key);
            out.u16(msgType);
            for (int32_t v : w)
                out.u32(static_cast<uint32_t>(v));
            break;
        case CoopKind::PlayerDied:
        case CoopKind::Respawn:
        case CoopKind::SyncReq:
            break;
    }
}
bool CoopEvent::read(ByteReader& r) {
    const uint8_t k = r.u8();
    if (!coopKindValid(k)) {
        r.fail();
        return false;
    }
    *this = CoopEvent{};
    kind = static_cast<CoopKind>(k);
    levelHash = r.u32();
    player = r.u8();
    if (player > kMaxPlayers)
        r.fail();
    switch (kind) {
        case CoopKind::CreatureDied:
        case CoopKind::ObjGone:
            key = r.u32();
            break;
        case CoopKind::Claim:
        case CoopKind::ObjState:
            key = r.u32();
            value = static_cast<int32_t>(r.u32());
            break;
        case CoopKind::Hurt:
            value = static_cast<int32_t>(r.u32());
            break;
        case CoopKind::ObjMsg:
            key = r.u32();
            msgType = r.u16();
            for (int32_t& v : w)
                v = static_cast<int32_t>(r.u32());
            break;
        case CoopKind::PlayerDied:
        case CoopKind::Respawn:
        case CoopKind::SyncReq:
            break;
    }
    return r.ok();
}

void LevelChange::write(ByteWriter& w) const {
    w.u8(player);
    w.u32(destHash);
    for (float v : exit)
        w.f32(v);
}
bool LevelChange::read(ByteReader& r) {
    player = r.u8();
    if (player > kMaxPlayers)
        r.fail();
    destHash = r.u32();
    for (float& v : exit)
        v = r.f32();
    return r.ok();
}

// ---- PvP arena pickups ----
void PickupClaim::write(ByteWriter& w) const {
    w.u16(index);
}
bool PickupClaim::read(ByteReader& r) {
    index = r.u16();
    return r.ok() && index != kAllPickups;
}
void PickupState::write(ByteWriter& w) const {
    w.u16(index);
    w.u8(taken ? 1 : 0);
    w.u8(player);
}
bool PickupState::read(ByteReader& r) {
    index = r.u16();
    const uint8_t t = r.u8();
    taken = t != 0;
    player = r.u8();
    if (t > 1 || player > kMaxPlayers || (index == kAllPickups && taken))
        r.fail();
    return r.ok();
}

void Status::write(ByteWriter& w) const {
    w.u32(token);
    w.u8(static_cast<uint8_t>(countdown));
    w.u32(countdownMs);
    w.u8(static_cast<uint8_t>(std::min(pings.size(), kMaxStatusPlayers)));
    for (size_t i = 0; i < pings.size() && i < kMaxStatusPlayers; ++i) {
        w.u8(pings[i].player);
        w.u16(pings[i].pingMs);
        w.u16(pings[i].character);
    }
}
bool Status::read(ByteReader& r) {
    token = r.u32();
    readEnum(r, countdown, 2);
    countdownMs = r.u32();
    const uint8_t n = r.u8();
    if (n > kMaxStatusPlayers)
        r.fail();
    pings.clear();
    for (uint8_t i = 0; i < n && r.ok(); ++i) {
        PlayerPing p;
        p.player = r.u8();
        p.pingMs = r.u16();
        p.character = r.u16();
        if (p.player == kNoPlayer || p.player > kMaxPlayers)
            r.fail();
        pings.push_back(p);
    }
    return r.ok();
}
void StatusEcho::write(ByteWriter& w) const {
    w.u32(token);
}
bool StatusEcho::read(ByteReader& r) {
    token = r.u32();
    return r.ok();
}
void SetCharacter::write(ByteWriter& w) const {
    w.u16(character);
}
bool SetCharacter::read(ByteReader& r) {
    character = r.u16();
    return r.ok();
}
void Kick::write(ByteWriter& w) const {
    w.u8(player);
}
bool Kick::read(ByteReader& r) {
    player = r.u8();
    if (player == kNoPlayer || player > kMaxPlayers)
        r.fail();
    return r.ok();
}

} // namespace drakan::net
