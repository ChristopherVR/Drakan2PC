#pragma once
// Client side of a match: connect handshake, roster, snapshot buffer with interpolation of remote players, and an
// event queue for the game to drain. Driven by tick(nowMs) with an explicit clock (deterministic in tests).
#include "net/Protocol.h"
#include "net/Scoreboard.h"
#include "net/Transport.h"

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace drakan::net {

struct ClientOptions {
    std::string name = "Player";
    uint64_t profileHash = 0;
    std::string password;                // v5: sent in Hello
    uint32_t interpolationDelayMs = 100; // remote players are shown this far behind the server clock
    uint32_t connectTimeoutMs = 5000;    // handshake (transport connect + Welcome) must finish within this
    size_t maxBufferedStates = 32;       // per remote player
};

enum class ClientState : uint8_t { Disconnected, Connecting, Handshaking, Connected, Rejected };

struct RemotePlayer {
    PlayerId id = kNoPlayer;
    std::string name;
    uint8_t team = kNoTeam;
    uint16_t kills = 0;
    uint16_t deaths = 0;
    bool alive = false;
    bool ready = false; // v5 lobby ready flag
    int32_t health = 0;
};

struct DisconnectedEvent {
    bool wasRejected = false;
};

using ClientEvent = std::variant<Hit, Death, Respawn, Chat, Score, PhaseChange, PlayerJoin, PlayerLeave, MatchConfigMsg,
                                 Ready, DisconnectedEvent>;

class MatchClient {
public:
    MatchClient(Transport& transport, ClientOptions options);

    // Start connecting. False if the transport refuses the address.
    bool connect(const std::string& host, uint16_t port, uint64_t nowMs);
    void disconnect();
    // Pump the transport, advance the handshake, buffer snapshots, queue events.
    void tick(uint64_t nowMs);

    ClientState state() const { return state_; }
    bool connected() const { return state_ == ClientState::Connected; }
    bool rejected() const { return state_ == ClientState::Rejected; }
    const Reject& rejectInfo() const { return reject_; }
    PlayerId playerId() const { return self_; }
    MatchPhase phase() const { return phase_; }
    const MatchConfigData& config() const { return config_; }
    uint8_t tickRateHz() const { return tickRateHz_; }
    const std::map<PlayerId, RemotePlayer>& roster() const { return roster_; }
    const RemotePlayer* self() const;
    uint16_t teamKills(uint8_t team) const { return team < 2 ? teamKills_[team] : 0; }
    // Scoreboard from the roster (sorted, see sortScoreboard) and the outcome the current scores give. The outcome is
    // final once phase() == Results; its reason is inferred from the config (frag limit reached, else time limit).
    std::vector<ScoreRow> scoreboard() const;
    MatchOutcome outcome() const;

    // Outgoing. All are no-ops (false) until connected.
    bool sendState(PlayerState state);                              // fills player + sequence
    bool sendHit(PlayerId victim, uint8_t weapon, uint16_t damage); // a claim; the server decides
    bool sendChat(const std::string& text);
    bool sendLoadDone();
    bool sendReady(bool ready); // lobby only (the server ignores it elsewhere)

    bool pollEvent(ClientEvent& out);

    // Server clock estimate for a local time, and the interpolated pose of a remote player at that local time
    // (renders interpolationDelayMs behind). False if fewer than one buffered state exists.
    uint32_t serverTimeAt(uint64_t localNowMs) const;
    bool remoteState(PlayerId id, uint64_t localNowMs, PlayerState& out) const;
    size_t bufferedStates(PlayerId id) const;
    // The newest buffered raw snapshot sample of a remote player and the server time it carries (for a game-side pose
    // buffer that interpolates on its own clock). False when nothing is buffered.
    bool newestSample(PlayerId id, PlayerState& out, uint32_t& serverTimeMs) const;

    // Diagnostics. rttMs() is the smoothed round trip of our own PlayerState: send time to the arrival of the snapshot
    // that echoes its sequence (so it includes up to one server snapshot interval); < 0 until measured.
    uint64_t snapshotCount() const { return snapshotCount_; }
    double rttMs() const { return rttMs_; }

private:
    struct Sample {
        uint32_t serverTimeMs = 0;
        PlayerState state;
    };

    void handleMessage(const TransportEvent& ev);
    void applyScore(const Score& s);
    bool sendReliable(const std::vector<uint8_t>& bytes);

    Transport& transport_;
    ClientOptions options_;
    ClientState state_ = ClientState::Disconnected;
    PeerId peer_ = kInvalidPeer;
    uint64_t connectStartMs_ = 0;
    uint64_t now_ = 0;
    Reject reject_;
    PlayerId self_ = kNoPlayer;
    MatchPhase phase_ = MatchPhase::Lobby;
    MatchConfigData config_;
    uint8_t tickRateHz_ = 30;
    uint16_t teamKills_[2] = {0, 0};
    uint32_t sendSequence_ = 0;
    uint32_t sendTick_ = 0;
    struct SentMark {
        uint32_t sequence = 0;
        uint64_t atMs = 0;
    };
    SentMark sent_[64];
    uint64_t snapshotCount_ = 0;
    double rttMs_ = -1.0;
    bool haveClock_ = false;
    int64_t clockOffset_ = 0; // serverTime - localTime
    std::map<PlayerId, RemotePlayer> roster_;
    std::map<PlayerId, std::deque<Sample>> buffers_;
    std::deque<ClientEvent> events_;
};

} // namespace drakan::net
