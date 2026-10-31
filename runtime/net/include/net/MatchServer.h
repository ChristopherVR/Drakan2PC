#pragma once
// Authoritative match state: roster, phases, damage resolution, scoring, respawns. Driven by tick(nowMs) with an
// explicit clock so tests are deterministic. Owns no sockets: it talks through a Transport it is handed (a
// listen server hands it the loopback or UDP transport; the dedicated server hands it UdpTransport).
// See docs/reference/netcode.md for the authority model.
#include "net/ArenaMaps.h"
#include "net/Protocol.h"
#include "net/Scoreboard.h"
#include "net/Transport.h"
#include "net/WeaponTable.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace drakan::net {

struct ServerOptions {
    MatchConfigData config;
    uint64_t profileHash = 0; // required disc profile hash; 0 accepts any
    std::string password;     // v5: Hello.password must equal this when non-empty
    uint32_t serverId = 0;    // v5: reported in ServerInfo (random per run; 0 = unknown)
    uint8_t tickRateHz = 30;  // advertised to clients
    uint32_t snapshotIntervalMs = 50;
    uint32_t helloTimeoutMs = 5000; // a connection that never says Hello is dropped
    uint32_t rejectLingerMs = 1000; // keep a rejected peer around so the Reject can arrive
    uint32_t loadTimeoutMs = 60000; // Loading ends after this even if clients have not reported
    uint32_t resultsDurationMs = 15000;
    uint8_t autoStartPlayers = 0;     // 0 = never start on its own (host calls startMatch())
    uint32_t lobbyCountdownMs = 5000; // countdown once autoStartPlayers have joined
    uint32_t startCountdownMs = 0;    // startMatch(): 0 = Loading at once, else a countdown (Status shows it) first
    bool hostKick = false;            // honour Kick from the host (the longest connected player); a listen server sets it
    std::string mods;                 // comma separated mod pack names, reported in ServerInfo (<= kMaxModsLength)
    // Sanity limits for client claims (all in world units / ms).
    uint32_t respawnProtectionMs = 2000;
    uint16_t maxDamagePerHit = 200;
    uint32_t minHitIntervalMs = 50;     // per attacker
    float maxHitRange = 500.0f;         // melee: attacker to victim, from their last accepted states; <= 0 disables
    float maxHitRangeRanged = 12000.0f; // Hit.weapon == kHitProjectile (arrows, bolts); <= 0 disables
    // Damage table, opt in: a melee claim's damage may not exceed the entry of the record id the attacker holds in
    // the claimed hand (PlayerState.weaponRight / weaponLeft of its last accepted state), min'd with maxDamagePerHit.
    // A held record without an entry is only limited by maxDamagePerHit; an empty hand by unarmedMaxDamage (0 = no
    // such check). Projectile claims use the left-hand (bow) entry.
    std::map<uint32_t, uint16_t> weaponMaxDamage;
    uint16_t unarmedMaxDamage = 0;
    uint16_t maxProjectileDamage = 0; // cap on every projectile claim (arrows, bolts, spells); 0 = only maxDamagePerHit
    // The accepted claim's damage is multiplied by this before it is applied and broadcast (the game's weapons do
    // 4..20 against a 100 point health bar, a deathmatch tuning knob). The limits above apply to the claimed damage.
    float damageScale = 1.0f;
    float maxSpeed = 30000.0f; // units/s between accepted states; <= 0 disables. Rynn runs ~2500 u/s (game units), so
                               // 2000 dropped real movement
    // PvP arena (ArenaMaps.h, docs/reference/netcode.md "Multiplayer maps"): with spawns, every (re)spawn takes the
    // spawn point farthest from the other living players (Respawn.spawnIndex = its index); with pickups the server owns
    // their taken / available state and respawn timers (PickupClaim / PickupState). Off (hasArena == false) = the old
    // behaviour, spawnIndex only counts up and the clients choose a spot themselves.
    bool hasArena = false;
    ArenaDef arena;
    float pickupClaimRange = 4000.0f; // a claim from farther than this from the pickup (last known position) is refused
};

struct ServerPlayer {
    PlayerId id = kNoPlayer;
    PeerId peer = kInvalidPeer;
    std::string name;
    uint8_t team = kNoTeam;
    bool alive = false;
    bool loaded = false;
    bool ready = false; // v5 lobby ready flag
    int32_t health = 0;
    uint16_t kills = 0;
    uint16_t deaths = 0;
    bool hasState = false;
    PlayerState state;
    uint64_t stateAtMs = 0;
    bool acceptAnyState = true; // first state after (re)spawn is not speed checked
    uint64_t respawnAtMs = 0;
    uint64_t protectedUntilMs = 0;
    uint64_t lastHitAtMs = 0;
    bool hasHit = false;
    int lastSpawn = -1; // arena: index of the spawn point of the previous life
    float spawnPos[2] = {
        0, 0}; // arena: x / z of the spawn point it was given (stands in for its position until it publishes)
    bool hasSpawnPos = false;
    uint64_t spawnedAtMs = 0;   // arena: when it last (re)spawned
    uint32_t joinSeq = 0; // join order (ids are recycled, so the lowest id is not the oldest player)
    uint16_t character = 0;         // co-op character picked in the lobby (SetCharacter), 0 = Rynn
    uint16_t pingMs = kPingUnknown; // round trip measured with Status / StatusEcho (smoothed)
};

struct ServerStats {
    uint32_t hitsAccepted = 0;
    uint32_t hitsRejected = 0;
    uint32_t statesRejected = 0;
    uint32_t joinsRejected = 0;
    uint32_t coopRelayed = 0;    // v6: co-op messages forwarded
    uint32_t coopRejected = 0;   // v6: co-op messages dropped by the routing rules
    uint32_t coopMigrations = 0; // v6: times the co-op authority left and another player took over
};

class MatchServer {
public:
    MatchServer(Transport& transport, ServerOptions options);

    // Process transport events and timers. Call regularly with a monotonic millisecond clock.
    void tick(uint64_t nowMs);

    // Lobby -> Loading. False if not in the lobby or nobody is connected. With options.startCountdownMs > 0 this starts
    // the countdown (CountdownKind::MatchStart in Status) and Loading follows when it is up; true while one runs.
    bool startMatch(uint64_t nowMs);
    // The countdown a client would be shown right now (kind None / 0 when nothing counts down).
    CountdownKind countdown(uint32_t& remainingMs) const;
    // The host (longest connected player, kNoPlayer when empty).
    PlayerId hostPlayer() const;
    // Remove a player as the host would (Kick message). False when unknown.
    bool kick(PlayerId target);
    // Change the configuration while in the lobby (broadcast to clients). False otherwise or if invalid.
    bool setConfig(const MatchConfigData& config);
    // End the current match early (Playing -> Results).
    void endMatch(uint64_t nowMs);
    // v5: the status answer for a LAN / direct query (see UdpTransport::setQueryResponder).
    ServerInfo info() const;
    // The wire answer for a raw Query body (empty when the request does not parse or the version differs).
    std::vector<uint8_t> answerQuery(const std::vector<uint8_t>& request) const;
    bool allReady() const; // at least one player and every player ready

    MatchPhase phase() const { return phase_; }
    const MatchConfigData& config() const { return options_.config; }
    const std::map<PlayerId, ServerPlayer>& players() const { return players_; }
    const ServerPlayer* player(PlayerId id) const;
    uint16_t teamKills(uint8_t team) const { return team < 2 ? teamKills_[team] : 0; }
    const ServerStats& stats() const { return stats_; }
    // Scoreboard (sorted, see sortScoreboard) and, once the phase is Results, how and with whom the match ended.
    std::vector<ScoreRow> scoreboard() const;
    const MatchOutcome& outcome() const { return outcome_; }
    uint32_t serverTimeMs() const { return static_cast<uint32_t>(now_); }
    // v6: the player that owns the creature world in a Coop match (first to join; 0 = none). When it leaves or
    // times out the longest-connected remaining player takes over (CoopAuthority broadcast, stats().coopMigrations).
    PlayerId coopAuthority() const { return coopAuthority_; }

    // Optional log sink (joins, leaves, phase changes, rejections).
    std::function<void(const std::string&)> onLog;

private:
    struct Peer {
        PlayerId player = kNoPlayer;
        uint64_t connectedAtMs = 0;
        uint64_t closeAtMs = 0; // rejected: disconnect at this time
    };

    void log(const std::string& s);
    void handleEvent(const TransportEvent& ev);
    void handleMessage(PeerId peer, Peer& state, const std::vector<uint8_t>& data);
    void onHello(PeerId peer, Peer& state, const std::vector<uint8_t>& data);
    void onPlayerState(ServerPlayer& p, const std::vector<uint8_t>& data);
    void onHit(ServerPlayer& attacker, const std::vector<uint8_t>& data);
    void onChat(ServerPlayer& p, const std::vector<uint8_t>& data);
    void onReady(ServerPlayer& p, const std::vector<uint8_t>& data);
    void onCoopCreatures(ServerPlayer& p, const std::vector<uint8_t>& data);
    void onCoopEvent(ServerPlayer& p, const std::vector<uint8_t>& data);
    void onLevelChange(ServerPlayer& p, const std::vector<uint8_t>& data);
    void onPickupClaim(ServerPlayer& p, const std::vector<uint8_t>& data);
    void onStatusEcho(ServerPlayer& p, const std::vector<uint8_t>& data);
    void onKick(ServerPlayer& p, const std::vector<uint8_t>& data);
    void onSetCharacter(ServerPlayer& p, const std::vector<uint8_t>& data);
    void sendStatus();
    void beginLoading();
    void resetPickups();
    int chooseSpawn(const ServerPlayer& p) const;
    void assignCoopAuthority();
    void sendExcept(PlayerId except, const std::vector<uint8_t>& bytes, Channel ch);
    void reject(PeerId peer, Peer& state, RejectReason why, const std::string& text);
    void removePlayer(PeerId peer);
    void setPhase(MatchPhase p);
    void beginPlaying();
    void spawnPlayer(ServerPlayer& p);
    void scoreFor(const ServerPlayer& p);
    void checkMatchEnd();
    void endMatchWith(EndReason reason);
    uint16_t hitLimit(const ServerPlayer& attacker, uint8_t weapon) const;
    void resetMatch();
    void sendSnapshot();
    uint8_t pickTeam() const;

    void sendTo(PeerId peer, const std::vector<uint8_t>& bytes, Channel ch);
    void broadcast(const std::vector<uint8_t>& bytes, Channel ch);

    Transport& transport_;
    ServerOptions options_;
    uint64_t now_ = 0;
    MatchPhase phase_ = MatchPhase::Lobby;
    uint64_t phaseStartMs_ = 0;
    uint64_t lobbyCountdownStartMs_ = 0;
    bool countdownActive_ = false;
    bool startPending_ = false; // startMatch() countdown running
    uint64_t startPendingAtMs_ = 0;
    uint64_t lastStatusMs_ = 0;
    uint64_t lastSnapshotMs_ = 0;
    uint8_t spawnCounter_ = 0;
    uint16_t teamKills_[2] = {0, 0};
    std::vector<uint64_t> pickupBackAtMs_; // arena pickups: 0 = available, else the time it returns
    std::vector<PlayerId> pickupTakenBy_;
    PlayerId coopAuthority_ = kNoPlayer;
    uint32_t joinCounter_ = 0;
    std::unordered_map<PeerId, Peer> peers_;
    std::map<PlayerId, ServerPlayer> players_;
    ServerStats stats_;
    MatchOutcome outcome_;
};

// Upper bounds for one hit of the weapons whose damage is known (WeaponTable.h: item property 0x1a, "Damage" in the
// inventory screen, times the blade's multiplier, 1.0 .. 2.0 observed, with headroom): record id -> max claimed damage.
// Melee records get kMeleeLimitFactor x their damage; bows get kMaxProjectileDamage.
inline std::map<uint32_t, uint16_t> drakanWeaponDamageLimits() {
    std::map<uint32_t, uint16_t> m;
    for (const MeleeWeapon& w : kMeleeWeapons)
        m[w.record] = static_cast<uint16_t>(w.damage * kMeleeLimitFactor);
    for (const BowWeapon& b : kBows)
        m[b.record] = kMaxProjectileDamage;
    return m;
}

// Hit.weapon values (v1..v5 left the byte unspecified): which of the attacker's items dealt the hit.
constexpr uint8_t kHitMeleeRight = 0;
constexpr uint8_t kHitMeleeLeft = 1;
constexpr uint8_t kHitProjectile = 2;

// The deathmatch tuning the game uses (listen server and drakan_server), in real game units: melee reach (the dagger
// hit ghosts 90..260 units away in game runs) plus two snapshot intervals of running either way (Rynn runs ~2500 u/s),
// arrows fly far, the weapons of drakanWeaponDamageLimits(), damage x3 (the dagger does 4 against a 100 point bar),
// respawn after 3 s (the death clip is ~4.7 s: the victim must be back before the engine's game over would show).
inline void applyDrakanPvpTuning(ServerOptions& o) {
    o.maxHitRange = 900.0f;
    o.maxHitRangeRanged = 20000.0f;
    o.minHitIntervalMs = 100;
    o.respawnProtectionMs = 2000;
    o.weaponMaxDamage = drakanWeaponDamageLimits();
    o.unarmedMaxDamage = 8;
    o.maxProjectileDamage = kMaxProjectileDamage;
    o.damageScale = 3.0f;
    o.config.respawnDelayMs = 3000;
}

} // namespace drakan::net
