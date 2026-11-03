#pragma once
// Protocol v3: versioned binary messages, explicit little-endian serialisation, bounds-checked reader.
// See docs/reference/netcode.md. Std-only.
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace drakan::net {

constexpr uint16_t kProtocolVersion = 8; // v8: PlayerState.pitch / roll / clipHi (a mounted partner's dragon);
                                         // v7: PlayerState.character (co-op stand-in character, 0 = Rynn);
                                         // v6: co-op mode (CoopAuthority / CoopCreatures / CoopEvent) and LevelChange;
                                         // v5: Hello.password, Ready, MatchConfig.serverName, LAN Query / ServerInfo;
                                         // v4: weaponRight/weaponLeft record ids replace the weapon byte; v3: v3:
                                         // levelHash in PlayerState (v2: upper-body clip + channel mask)
constexpr size_t kMaxPlayers = 16;
constexpr size_t kMaxTeams = 4; // custom game modes: 2..4 teams (standard team deathmatch has two)
constexpr size_t kMaxModeIdLength = 32;
// The first UDP port a server listens on and a LAN browser probes (kDiscoveryPortCount ports from it): kDefaultPort, or
// the number in the DRAKAN_MP_PORT environment variable (test rigs that must not meet other instances on the PC).
uint16_t basePort();
constexpr size_t kMaxNameLength = 24;
constexpr size_t kMaxChatLength = 128;
constexpr size_t kMaxLevelLength = 64;
constexpr size_t kMaxRejectMessageLength = 96;
constexpr size_t kMaxPasswordLength = 24;
constexpr size_t kMaxServerNameLength = 32;
constexpr size_t kMaxModsLength = 96;    // ServerInfo.mods: comma separated mod pack names
constexpr uint16_t kDefaultPort = 47800; // LAN discovery probes kDefaultPort .. kDefaultPort + kDiscoveryPortCount - 1
constexpr uint16_t kDiscoveryPortCount = 10;

using PlayerId = uint8_t; // 1..kMaxPlayers on the wire; 0 = none / server
constexpr PlayerId kNoPlayer = 0;
constexpr uint8_t kNoTeam = 255;
constexpr uint8_t kLivesUnlimited = 255; // Score.lives: the mode has no lives limit

enum class MatchMode : uint8_t { Deathmatch = 0, TeamDeathmatch = 1, Coop = 2 }; // Coop: v6
enum class MatchPhase : uint8_t { Lobby = 0, Loading = 1, Playing = 2, Results = 3 };
enum class RejectReason : uint8_t {
    VersionMismatch = 0,
    ProfileMismatch = 1,
    ServerFull = 2,
    BadName = 3,
    BadPassword = 4,
    Kicked = 5 // sent to a joined player the host removed (the client reads it as a rejection with this reason)
};

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
    Ready = 16,         // v5
    Query = 17,         // v5, LAN discovery request (UDP datagram type Query, outside a connection)
    ServerInfo = 18,    // v5, the answer
    CoopAuthority = 19, // v6, S>C: which player owns the creature world
    CoopCreatures = 20, // v6, creature stream (host -> clients, unreliable)
    CoopEvent = 21,     // v6, co-op events (kills, claims, objects, death / respawn, catch-up)
    LevelChange = 22,   // v6, the host went through a level exit: clients follow
    // PvP arena addendum (separate from the co-op block above, same wire version: older v6 peers drop unknown types).
    PickupClaim = 23, // C>S, the sender took arena pickup `index`
    PickupState = 24, // S>C, a pickup was taken / is back (index kAllPickups = reset everything)
    // Lobby / menu addendum (same wire version, older peers drop unknown types): the start and next-match countdowns,
    // the players' ping, and the host's kick.
    Status = 25,     // S>C, ~2 Hz, unreliable: countdown + every player's ping + a token to echo
    StatusEcho = 26, // C>S, unreliable: the Status token (the server measures the round trip)
    Kick = 27,       // C>S, reliable: the host removes a player
    Character = 28,  // C>S, reliable: the co-op character the sender picked in the lobby (PlayerState.character later)
    ResultsAction =
        30,       // C>S, reliable: the host's choice on the results screen (back to the lobby / next map / rematch)
    MapVote = 31, // C>S, reliable, Results phase: the sender's vote for the next map (index into the candidates)
    MapVoteState = 32, // S>C, reliable: the candidates, their tallies and (once decided) the winner
    Vitals = 29,       // C>S one entry (own health), S>C every player's, unreliable (co-op party HUD; no version bump)
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
    std::string serverName; // v5, shown in lobbies and server lists (<= kMaxServerNameLength)
    // Trailing optional field (older peers omit it): the id of the custom game mode ([mode <id>], GameModes.h) the
    // match runs; empty = a standard deathmatch / team deathmatch / co-op.
    std::string modeId;
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};

struct Hello {
    static constexpr MsgType kType = MsgType::Hello;
    uint16_t protocolVersion = kProtocolVersion;
    uint64_t profileHash = 0; // disc profile hash; 0 = unknown
    std::string name;
    std::string password; // v5, empty when the server has none
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
    // Trailing optional field (older servers omit it): the change follows a host's choice on the results screen (back
    // to the lobby, next map, rematch) and not the results timer: clients that still sit in the match level leave it
    // for the title, where the lobby (and the next match's load) is.
    bool byHost = false;
    // Second trailing optional field: why the match ended when `phase` is Results (EndReason in net/Scoreboard.h: 1
    // frag limit, 2 time limit, 3 ended, 4 last one standing); 0 = not said (older servers: the client infers it).
    uint8_t endReason = 0;
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
    uint32_t weaponRight = 0;         // resource record id of the right-hand (melee) item, 0 = empty hand (v4)
    uint32_t weaponLeft = 0;          // same for the left hand (bow)
    bool mounted = false;             // riding the dragon
    // FNV-1a of the sender's loaded level name; 0 while it is loading or unknown. A receiver shows a ghost only when
    // this equals its own level's hash (clients run their own world, the server never sees level data).
    uint32_t levelHash = 0;
    // v7: the character the player is shown as (drakan_coop_character id = NPC class id; 0 = Rynn). Receivers spawn the
    // ghost as that character; the server relays it unchanged.
    uint16_t character = 0;
    // v8: the full orientation of a mounted sender (its pose is Arokh's, which pitches and banks): the rotation is
    // Ry(yaw) * Rx(pitch) * Rz(roll), radians, quantised to 1e-4 on the wire (0 / 0 on foot, where `yaw` alone is the
    // rotation). `clipHi` is the high half of the sender's clip ids when it is not the usual 0x2 (Arokh's clips are
    // 0x4xxxx in the levels that were seen; the wire carries the low 16 bits in animId / upperAnimId); 0 = 0x2.
    float pitch = 0;
    float roll = 0;
    uint16_t clipHi = 0;
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};

constexpr size_t kPlayerStateWireSize = 1 + 4 + 4 + 12 + 4 + 12 + 2 + 4 + 2 + 4 + 4 + 4 + 4 + 1 + 4 + 2 + 2 + 2 + 2;

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
    // Trailing optional fields (older servers omit them; then score = kills and teamScore = teamKills): the points a
    // custom game mode scores (kills x score_kill + deaths x score_death + objectives), per player and per team.
    int32_t score = 0;
    int32_t teamScore[kMaxTeams] = {0, 0, 0, 0};
    // Trailing optional (2026-11-02, a mode with `lives`): lives left (kLivesUnlimited = no limit) and whether the
    // player is out of the match. Older servers omit them (unlimited, not out).
    uint8_t lives = kLivesUnlimited;
    bool eliminated = false;
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

// Client to server: lobby ready toggle. Server to clients: the player (filled in by the server) and its state.
struct Ready {
    static constexpr MsgType kType = MsgType::Ready;
    PlayerId player = kNoPlayer;
    bool ready = false;
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};

// LAN / direct discovery (the GameSpy "\status\" spirit): one small request, one status answer. Carried as UDP
// datagram types Query / QueryReply of UdpTransport, outside any connection.
struct Query {
    static constexpr MsgType kType = MsgType::Query;
    uint16_t protocolVersion = kProtocolVersion;
    uint32_t token = 0; // echoed in the answer so the browser can measure the ping
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};

struct ServerInfo {
    static constexpr MsgType kType = MsgType::ServerInfo;
    uint16_t protocolVersion = kProtocolVersion;
    uint32_t token = 0;
    uint32_t serverId = 0; // random per server run, 0 = unknown: lets a browser merge the answers one server gives on
                           // several addresses (loopback, LAN adapters) into one entry
    std::string name;
    std::string level;
    MatchMode mode = MatchMode::Deathmatch;
    MatchPhase phase = MatchPhase::Lobby;
    uint8_t players = 0;
    uint8_t maxPlayers = 8;
    bool passwordProtected = false;
    // Trailing optional field (older servers omit it): comma separated names of the mod packs the server runs.
    std::string mods;
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};

// ---- v6: co-op (docs/reference/netcode.md "Co-op mode") -------------------------------------------------------------
// The server relays; it never sees creatures. The co-op authority (the first player to join a Coop match) owns the
// creature world: only it may send CoopCreatures and the authority-origin events. Every message carries the sender's
// level hash (FNV-1a of the level name) so a receiver on another level ignores it.

// Server to clients, reliable: the player that owns the creature world (0 = none: nobody joined yet, or it left).
struct CoopAuthority {
    static constexpr MsgType kType = MsgType::CoopAuthority;
    PlayerId player = kNoPlayer;
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};

struct CoopCreature {
    uint32_t key = 0; // (resource id, registry ordinal) key both sides compute from their identical level data
    float pos[3] = {0, 0, 0};
    float yaw = 0;
    int32_t hp = 0;
};
constexpr size_t kCoopCreatureWireSize = 4 + 12 + 4 + 4;
constexpr size_t kMaxCoopCreatures = 45; // 11 header bytes + 45 x 24 stays under kMaxPacketPayload

// Authority to the others, unreliable, 10 Hz: a slice of the creature list.
struct CoopCreatures {
    static constexpr MsgType kType = MsgType::CoopCreatures;
    uint32_t levelHash = 0;
    uint32_t sequence = 0;
    std::vector<CoopCreature> creatures; // at most kMaxCoopCreatures
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};

// Event kinds (the numbers are the ones the first co-op prototype used on its private socket).
enum class CoopKind : uint8_t {
    CreatureDied = 2, // authority -> all:       key
    Claim = 3,        // client -> authority:    key, value = damage (a hit on a creature proxy)
    Hurt = 4,         // authority -> player:    value = damage (a creature hit that player's ghost)
    ObjGone = 5,      // anyone -> all others:   key (a pickup was taken)
    ObjMsg = 6,       // anyone -> all others:   key, msgType, w[4] (a door / lever message to replay)
    PlayerDied = 7,   // client -> authority:    (empty) the sender's player is down
    Respawn = 8,      // authority -> player:    (empty) get up at the authority
    ObjState = 9,     // anyone -> all others:   key, value (a chest / lever changed state)
    SyncReq = 10,     // client -> authority:    (empty) the sender keyed the level: send the catch-up state
};
constexpr uint8_t kCoopKindMin = 2, kCoopKindMax = 10;
inline bool coopKindValid(uint8_t k) {
    return k >= kCoopKindMin && k <= kCoopKindMax;
}
// Routing classes the server enforces.
inline bool coopFromAuthorityOnly(CoopKind k) {
    return k == CoopKind::CreatureDied || k == CoopKind::Hurt || k == CoopKind::Respawn;
}
inline bool coopToAuthorityOnly(CoopKind k) {
    return k == CoopKind::Claim || k == CoopKind::PlayerDied || k == CoopKind::SyncReq;
}
// Fields used per kind: key (CreatureDied, Claim, ObjGone, ObjMsg, ObjState), value (Claim, Hurt, ObjState), msgType
// and w (ObjMsg). The rest are zero on the wire.
struct CoopEvent {
    static constexpr MsgType kType = MsgType::CoopEvent;
    CoopKind kind = CoopKind::CreatureDied;
    uint32_t levelHash = 0;
    // Sent by the authority for Hurt / Respawn: the target (0 = every other player). Filled in by the server for
    // everything else: the sender.
    PlayerId player = kNoPlayer;
    uint32_t key = 0;
    int32_t value = 0;
    uint16_t msgType = 0;
    int32_t w[4] = {0, 0, 0, 0};
    void write(ByteWriter& out) const;
    bool read(ByteReader& r);
};

// Authority (any player when the mode has no authority: the lowest id) to the others, reliable: it went through a level
// exit. `exit` is the trigger's world position (the same level data on every machine), destHash the FNV-1a of the
// destination level name (0 = unknown).
struct LevelChange {
    static constexpr MsgType kType = MsgType::LevelChange;
    PlayerId player = kNoPlayer; // the sender, filled in by the server
    uint32_t destHash = 0;
    float exit[3] = {0, 0, 0};
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};

// ---- PvP arena pickups (docs/reference/netcode.md "Multiplayer maps") ----
// The map's pickup list is the same on every machine (ArenaMaps.h); a pickup is named by its index in that list.
// Server: validates the claim (Playing, player alive and near it when its position is known, not already taken),
// marks the pickup taken for its respawn time and tells everybody; when the time is up it sends "available" again.
constexpr uint16_t kAllPickups = 0xffff;
struct PickupClaim {
    static constexpr MsgType kType = MsgType::PickupClaim;
    uint16_t index = 0;
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};
struct PickupState {
    static constexpr MsgType kType = MsgType::PickupState;
    uint16_t index = 0;          // kAllPickups with taken == 0: every pickup is available (a new round)
    bool taken = false;          // true: gone for its respawn time, false: available again
    PlayerId player = kNoPlayer; // who took it (taken only; 0 when the server cannot say)
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};

// ---- lobby / menu addendum: countdowns, pings, kick ----
enum class CountdownKind : uint8_t {
    None = 0,
    MatchStart = 1, // the host pressed Start Match (or the auto start is running): the Loading phase follows
    NextMatch = 2,  // Results: the lobby (the next match) follows
};
constexpr size_t kMaxStatusPlayers = kMaxPlayers;
constexpr uint16_t kPingUnknown = 0xffff;
struct PlayerPing {
    PlayerId player = kNoPlayer;
    uint16_t pingMs = kPingUnknown; // the server's measured round trip to the player (kPingUnknown until measured)
    uint16_t character = 0;         // the co-op character the player picked (drakan_coop_character id, 0 = Rynn)
};
// Server to clients, unreliable, every kStatusIntervalMs: the running countdown (remaining milliseconds on the server's
// clock at send time) and each player's ping. The client answers with a StatusEcho carrying `token`.
constexpr uint32_t kStatusIntervalMs = 500;
struct Status {
    static constexpr MsgType kType = MsgType::Status;
    uint32_t token = 0;
    CountdownKind countdown = CountdownKind::None;
    uint32_t countdownMs = 0;
    std::vector<PlayerPing> pings; // at most kMaxStatusPlayers
    // Trailing optional field (older servers omit it): the host, the longest connected player, who may kick and use the
    // results screen's buttons (kNoPlayer = unknown).
    PlayerId host = kNoPlayer;
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};
struct StatusEcho {
    static constexpr MsgType kType = MsgType::StatusEcho;
    uint32_t token = 0;
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};
// Client to server: remove `player`. Honoured only from the host (the longest connected player) of a listen server
// (ServerOptions::hostKick); the kicked player gets a Reject with RejectReason::Kicked.
struct Kick {
    static constexpr MsgType kType = MsgType::Kick;
    PlayerId player = kNoPlayer;
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};

// Client to server: the character picked in the lobby (the server relays it to everybody in Status.pings, so the lobby
// can show "as <character>" before any PlayerState flows).
struct SetCharacter {
    static constexpr MsgType kType = MsgType::Character;
    uint16_t character = 0;
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};

// Co-op party HUD: each player's health. The client sends its own as a single entry (the server overwrites `player`
// and stores it); the server relays every stored entry to everybody with each Status tick (and at once on a change).
// A partner that never sends one has no entry (the HUD then draws no bar). Dragon fields: 0 / 0 = not mounted /
// unknown.
struct PlayerVitals {
    PlayerId player = kNoPlayer;
    uint16_t hp = 0;
    uint16_t maxHp = 0;
    uint16_t dragonHp = 0;
    uint16_t dragonMaxHp = 0;
};
struct Vitals {
    static constexpr MsgType kType = MsgType::Vitals;
    std::vector<PlayerVitals> entries; // at most kMaxStatusPlayers
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};

// Client to server, reliable, during the Results phase: what the results screen's buttons ask for. Honoured only from
// the host (the longest connected player); the server answers with the usual PhaseChange / MatchConfig messages.
enum class ResultsChoice : uint8_t {
    BackToLobby = 1, // everybody returns to the lobby (also what the results timer does on its own)
    NextMap = 2,     // the next map of the server's rotation (co-op: the same level again), then the match start
    Rematch = 3,     // the same map and rules again, then the match start
};
struct ResultsAction {
    static constexpr MsgType kType = MsgType::ResultsAction;
    ResultsChoice choice = ResultsChoice::BackToLobby;
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};

// Map vote on the results screen (no version bump, older peers drop the unknown types). The server picks up to
// kMaxVoteCandidates maps of its rotation the mode fits when the results start and broadcasts them with a tally on
// every change; when the results end the leading map (ties: the lowest index) becomes the next map.
constexpr size_t kMaxVoteCandidates = 3;
constexpr uint8_t kNoVote = 0xFF;
struct MapVote {
    static constexpr MsgType kType = MsgType::MapVote;
    uint8_t choice = kNoVote; // candidate index, kNoVote withdraws the vote
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
};
struct MapVoteState {
    static constexpr MsgType kType = MsgType::MapVoteState;
    std::vector<std::string> candidates; // map names (ArenaDef::name), at most kMaxVoteCandidates; empty = no vote
    std::vector<uint8_t> tally;          // votes per candidate
    uint8_t winner = kNoVote;            // set once the vote is decided
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);
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
