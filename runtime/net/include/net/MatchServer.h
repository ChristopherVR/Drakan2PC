#pragma once
// Authoritative match state: roster, phases, damage resolution, scoring, respawns. Driven by tick(nowMs) with an
// explicit clock so tests are deterministic. Owns no sockets: it talks through a Transport it is handed (a
// listen server hands it the loopback or UDP transport; the dedicated server hands it UdpTransport).
// See docs/reference/netcode.md for the authority model.
#include "net/Protocol.h"
#include "net/Scoreboard.h"
#include "net/Transport.h"

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
    // The accepted claim's damage is multiplied by this before it is applied and broadcast (the game's weapons do
    // 4..20 against a 100 point health bar, a deathmatch tuning knob). The limits above apply to the claimed damage.
    float damageScale = 1.0f;
    float maxSpeed = 30000.0f; // units/s between accepted states; <= 0 disables. Rynn runs ~2500 u/s (game units), so
                               // 2000 dropped real movement
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
};

struct ServerStats {
    uint32_t hitsAccepted = 0;
    uint32_t hitsRejected = 0;
    uint32_t statesRejected = 0;
    uint32_t joinsRejected = 0;
};

class MatchServer {
public:
    MatchServer(Transport& transport, ServerOptions options);

    // Process transport events and timers. Call regularly with a monotonic millisecond clock.
    void tick(uint64_t nowMs);

    // Lobby -> Loading. False if not in the lobby or nobody is connected.
    bool startMatch(uint64_t nowMs);
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
    uint64_t lastSnapshotMs_ = 0;
    uint8_t spawnCounter_ = 0;
    uint16_t teamKills_[2] = {0, 0};
    std::unordered_map<PeerId, Peer> peers_;
    std::map<PlayerId, ServerPlayer> players_;
    ServerStats stats_;
    MatchOutcome outcome_;
};

// Upper bounds for one melee hit of the weapons whose damage is known (item property 0x1a, "Damage" in the inventory
// screen, times the blade's multiplier, 1.0 .. 2.0 observed, with headroom): record id -> max claimed damage.
inline std::map<uint32_t, uint16_t> drakanWeaponDamageLimits() {
    return {{0x33cbcu, uint16_t{16}}}; // Rynn's Dagger: damage 4
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
    o.damageScale = 3.0f;
    o.config.respawnDelayMs = 3000;
}

} // namespace drakan::net
