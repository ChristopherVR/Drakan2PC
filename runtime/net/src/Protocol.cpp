#include "net/Protocol.h"

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
}
bool MatchConfigData::read(ByteReader& r) {
    readEnum(r, mode, 1);
    level = r.str(kMaxLevelLength);
    fragLimit = r.u16();
    timeLimitSec = r.u16();
    maxPlayers = r.u8();
    respawnDelayMs = r.u16();
    startHealth = r.u16();
    if (maxPlayers < 1 || maxPlayers > kMaxPlayers || startHealth == 0)
        r.fail();
    return r.ok();
}

void Hello::write(ByteWriter& w) const {
    w.u16(protocolVersion);
    w.u64(profileHash);
    w.str(name);
}
bool Hello::read(ByteReader& r) {
    protocolVersion = r.u16();
    profileHash = r.u64();
    name = r.str(kMaxNameLength);
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
    readEnum(r, reason, 3);
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
    w.u8(weapon);
    w.u8(mounted ? 1 : 0);
    w.u32(levelHash);
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
    weapon = r.u8();
    const uint8_t flags = r.u8();
    if (flags > 1)
        r.fail();
    mounted = flags == 1;
    levelHash = r.u32();
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
    if (data[0] < static_cast<uint8_t>(MsgType::Hello) || data[0] > static_cast<uint8_t>(MsgType::LoadDone))
        return false;
    out = static_cast<MsgType>(data[0]);
    return true;
}

} // namespace drakan::net
