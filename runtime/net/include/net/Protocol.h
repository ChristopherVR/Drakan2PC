#pragma once
// Protocol v3: versioned binary messages, explicit little-endian serialisation, bounds-checked reader.
// See docs/reference/netcode.md. Std-only.
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace drakan::net {

constexpr uint16_t kProtocolVersion = 3; // v3: levelHash in PlayerState (v2: upper-body clip + channel mask)
constexpr size_t kMaxPlayers = 16;
constexpr size_t kMaxNameLength = 24;
constexpr size_t kMaxChatLength = 128;
constexpr size_t kMaxLevelLength = 64;
constexpr size_t kMaxRejectMessageLength = 96;

using PlayerId = uint8_t; // 1..kMaxPlayers on the wire; 0 = none / server
constexpr PlayerId kNoPlayer = 0;
constexpr uint8_t kNoTeam = 255;

enum class MatchMode : uint8_t { Deathmatch = 0, TeamDeathmatch = 1 };
enum class MatchPhase : uint8_t { Lobby = 0, Loading = 1, Playing = 2, Results = 3 };
enum class RejectReason : uint8_t { VersionMismatch = 0, ProfileMismatch = 1, ServerFull = 2, BadName = 3 };

enum class MsgType : uint8_t {
    Hello = 1,
    Welcome = 2,
    Reject = 3,
    MatchConfig = 4,
    PhaseChange = 5,
    PlayerState = 6,
    Snapshot = 7,
    Hit = 8,
    Death = 9,
    Respawn = 10,
    Chat = 11,
    Score = 12,
    PlayerJoin = 13,
    PlayerLeave = 14,
    LoadDone = 15,
};

class ByteWriter {
public:
    void u8(uint8_t v) { buf_.push_back(v); }
    void u16(uint16_t v);
    void u32(uint32_t v);
    void u64(uint64_t v);
    void f32(float v);
    void str(const std::string& s); // u16 length + bytes
    const std::vector<uint8_t>& bytes() const { return buf_; }
    std::vector<uint8_t> take() { return std::move(buf_); }

private:
    std::vector<uint8_t> buf_;
};

// Never reads past the end: a failed read returns 0 / empty and latches ok() == false.
class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}
    uint8_t u8();
    uint16_t u16();
    uint32_t u32();
    uint64_t u64();
    float f32(); // non-finite values fail the reader
    std::string str(size_t maxLen);
    bool ok() const { return ok_; }
    bool atEnd() const { return pos_ == size_; }
    void fail() { ok_ = false; }

private:
    bool need(size_t n);
    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
    bool ok_ = true;
};

struct MatchConfigData {
    MatchMode mode = MatchMode::Deathmatch;
    std::string level;
    uint16_t fragLimit = 20;     // kills (deathmatch) or team kills (team deathmatch); 0 = none
    uint16_t timeLimitSec = 600; // 0 = none
    uint8_t maxPlayers = 8;      // 1..kMaxPlayers
    uint16_t respawnDelayMs = 3000;
    uint16_t startHealth = 100;
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};

struct Hello {
    static constexpr MsgType kType = MsgType::Hello;
    uint16_t protocolVersion = kProtocolVersion;
    uint64_t profileHash = 0; // disc profile hash; 0 = unknown
    std::string name;
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};

struct Welcome {
    static constexpr MsgType kType = MsgType::Welcome;
    PlayerId player = kNoPlayer;
    uint8_t tickRateHz = 30;
    MatchPhase phase = MatchPhase::Lobby;
    uint32_t serverTimeMs = 0;
    MatchConfigData config;
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};

struct Reject {
    static constexpr MsgType kType = MsgType::Reject;
    RejectReason reason = RejectReason::VersionMismatch;
    uint16_t serverVersion = kProtocolVersion;
    std::string message;
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};

// Reliable broadcast of a changed configuration (wire type MatchConfig).
struct MatchConfigMsg {
    static constexpr MsgType kType = MsgType::MatchConfig;
    MatchConfigData config;
    void write(ByteWriter& w) const { config.write(w); }
    bool read(ByteReader& r) { return config.read(r); }
};

struct PhaseChange {
    static constexpr MsgType kType = MsgType::PhaseChange;
    MatchPhase phase = MatchPhase::Lobby;
    uint32_t serverTimeMs = 0;
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};

// Client to server (own movement, client authoritative) and the element of a Snapshot.
struct PlayerState {
    static constexpr MsgType kType = MsgType::PlayerState;
    PlayerId player = kNoPlayer; // server overwrites with the sender's id
    uint32_t sequence = 0;       // increases per state sent; wraps
    uint32_t tick = 0;           // sender's world tick (PadMgr boundary); receivers replay on it. 0 = client numbers it
    float pos[3] = {0, 0, 0};
    float yaw = 0; // radians
    float vel[3] = {0, 0, 0};
    uint16_t animId = 0; // lower body / locomotion clip
    float animTime = 0;
    uint16_t upperAnimId = 0; // upper body (attacks, block, aim); equals animId when one clip plays
    float upperAnimTime = 0;
    uint32_t lowerMask = 0xffffffffu; // bit i: skeleton channel i plays the lower clip, else the upper one
    uint8_t weapon = 0;
    bool mounted = false; // riding the dragon
    // FNV-1a of the sender's loaded level name; 0 while it is loading or unknown. A receiver shows a ghost only when
    // this equals its own level's hash (clients run their own world, the server never sees level data).
    uint32_t levelHash = 0;
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};

constexpr size_t kPlayerStateWireSize = 1 + 4 + 4 + 12 + 4 + 12 + 2 + 4 + 2 + 4 + 4 + 1 + 1 + 4;

// Server to clients, unreliable, every snapshot interval: all live players with a known state.
struct Snapshot {
    static constexpr MsgType kType = MsgType::Snapshot;
    uint32_t serverTimeMs = 0;
    std::vector<PlayerState> players; // at most kMaxPlayers
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};

// Client to server: a claim (attacker 0, healthAfter ignored). Server to clients: an accepted hit.
struct Hit {
    static constexpr MsgType kType = MsgType::Hit;
    PlayerId attacker = kNoPlayer;
    PlayerId victim = kNoPlayer;
    uint8_t weapon = 0;
    uint16_t damage = 0;
    uint16_t healthAfter = 0;
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};

struct Death {
    static constexpr MsgType kType = MsgType::Death;
    PlayerId victim = kNoPlayer;
    PlayerId killer = kNoPlayer;
    uint8_t weapon = 0;
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};

struct Respawn {
    static constexpr MsgType kType = MsgType::Respawn;
    PlayerId player = kNoPlayer;
    uint8_t spawnIndex = 0; // server's rolling counter; the client maps it onto its level's spawn points
    uint16_t health = 0;
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};

struct Chat {
    static constexpr MsgType kType = MsgType::Chat;
    PlayerId player = kNoPlayer; // 0 from clients; server fills in the sender
    std::string text;
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};

struct Score {
    static constexpr MsgType kType = MsgType::Score;
    PlayerId player = kNoPlayer;
    uint16_t kills = 0;
    uint16_t deaths = 0;
    uint16_t teamKills[2] = {0, 0};
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};

struct PlayerJoin {
    static constexpr MsgType kType = MsgType::PlayerJoin;
    PlayerId player = kNoPlayer;
    uint8_t team = kNoTeam;
    std::string name;
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};

struct PlayerLeave {
    static constexpr MsgType kType = MsgType::PlayerLeave;
    PlayerId player = kNoPlayer;
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};

// Client to server: the level finished loading (answers the Loading phase).
struct LoadDone {
    static constexpr MsgType kType = MsgType::LoadDone;
    void write(ByteWriter&) const {}
    bool read(ByteReader&) { return true; }
};

// Wire message = 1 type byte + body.
template <class T>
std::vector<uint8_t> encode(const T& msg) {
    ByteWriter w;
    w.u8(static_cast<uint8_t>(T::kType));
    msg.write(w);
    return w.take();
}

bool peekType(const uint8_t* data, size_t size, MsgType& out); // false for empty / unknown types

// True when the type byte matches, the body parses and no bytes are left over.
template <class T>
bool decode(const uint8_t* data, size_t size, T& out) {
    MsgType t;
    if (!peekType(data, size, t) || t != T::kType)
        return false;
    ByteReader r(data + 1, size - 1);
    T tmp;
    if (!tmp.read(r) || !r.ok() || !r.atEnd())
        return false;
    out = std::move(tmp);
    return true;
}
template <class T>
bool decode(const std::vector<uint8_t>& bytes, T& out) {
    return decode(bytes.data(), bytes.size(), out);
}

// Wrap-safe "a is newer than b" for sequence numbers.
inline bool sequenceNewer(uint32_t a, uint32_t b) {
    return static_cast<int32_t>(a - b) > 0;
}

} // namespace drakan::net
