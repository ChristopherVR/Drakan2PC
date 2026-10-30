#pragma once
// Authoritative match state: roster, phases, damage resolution, scoring, respawns. Driven by tick(nowMs) with an
// explicit clock so tests are deterministic. Owns no sockets: it talks through a Transport it is handed (a
// listen server hands it the loopback or UDP transport; the dedicated server hands it UdpTransport).
// See docs/reference/netcode.md for the authority model.
#include "net/Protocol.h"
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
    uint32_t minHitIntervalMs = 50; // per attacker
    float maxHitRange = 500.0f;     // attacker to victim, from their last accepted states; <= 0 disables
    float maxSpeed = 2000.0f;       // units per second between accepted states; <= 0 disables
};

struct ServerPlayer {
    PlayerId id = kNoPlayer;
    PeerId peer = kInvalidPeer;
    std::string name;
    uint8_t team = kNoTeam;
    bool alive = false;
    bool loaded = false;
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

    MatchPhase phase() const { return phase_; }
    const MatchConfigData& config() const { return options_.config; }
    const std::map<PlayerId, ServerPlayer>& players() const { return players_; }
    const ServerPlayer* player(PlayerId id) const;
    uint16_t teamKills(uint8_t team) const { return team < 2 ? teamKills_[team] : 0; }
    const ServerStats& stats() const { return stats_; }
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
    void reject(PeerId peer, Peer& state, RejectReason why, const std::string& text);
    void removePlayer(PeerId peer);
    void setPhase(MatchPhase p);
    void beginPlaying();
    void spawnPlayer(ServerPlayer& p);
    void scoreFor(const ServerPlayer& p);
    void checkMatchEnd();
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
};

} // namespace drakan::net
