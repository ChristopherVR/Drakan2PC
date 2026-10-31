#include "net/MatchServer.h"

#include <algorithm>
#include <cmath>

namespace drakan::net {

namespace {

bool validName(const std::string& n) {
    if (n.empty() || n.size() > kMaxNameLength)
        return false;
    for (unsigned char c : n)
        if (c < 0x20 || c == 0x7f)
            return false;
    return true;
}

const char* phaseName(MatchPhase p) {
    switch (p) {
        case MatchPhase::Lobby:
            return "lobby";
        case MatchPhase::Loading:
            return "loading";
        case MatchPhase::Playing:
            return "playing";
        case MatchPhase::Results:
            return "results";
    }
    return "?";
}

bool configValid(const MatchConfigData& c) {
    return c.maxPlayers >= 1 && c.maxPlayers <= kMaxPlayers && c.startHealth > 0 && c.level.size() <= kMaxLevelLength;
}

} // namespace

MatchServer::MatchServer(Transport& transport, ServerOptions options)
    : transport_(transport), options_(std::move(options)) {
    if (!configValid(options_.config))
        options_.config = MatchConfigData{};
}

void MatchServer::log(const std::string& s) {
    if (onLog)
        onLog(s);
}

const ServerPlayer* MatchServer::player(PlayerId id) const {
    auto it = players_.find(id);
    return it == players_.end() ? nullptr : &it->second;
}

void MatchServer::sendTo(PeerId peer, const std::vector<uint8_t>& bytes, Channel ch) {
    transport_.send(peer, ch, bytes);
}

void MatchServer::broadcast(const std::vector<uint8_t>& bytes, Channel ch) {
    for (const auto& kv : peers_)
        if (kv.second.player != kNoPlayer)
            transport_.send(kv.first, ch, bytes);
}

void MatchServer::tick(uint64_t nowMs) {
    now_ = nowMs;

    TransportEvent ev;
    while (transport_.poll(ev))
        handleEvent(ev);

    // Peers that never said Hello, and rejected peers whose Reject had time to arrive.
    std::vector<PeerId> drop;
    for (const auto& kv : peers_) {
        const Peer& p = kv.second;
        if (p.closeAtMs != 0) {
            if (now_ >= p.closeAtMs)
                drop.push_back(kv.first);
        } else if (p.player == kNoPlayer && now_ - p.connectedAtMs > options_.helloTimeoutMs) {
            log("dropping peer that sent no Hello");
            drop.push_back(kv.first);
        }
    }
    for (PeerId id : drop) {
        transport_.disconnect(id);
        peers_.erase(id);
    }

    switch (phase_) {
        case MatchPhase::Lobby:
            if (options_.autoStartPlayers > 0 && players_.size() >= options_.autoStartPlayers) {
                if (!countdownActive_) {
                    countdownActive_ = true;
                    lobbyCountdownStartMs_ = now_;
                    log("lobby countdown started");
                } else if (now_ - lobbyCountdownStartMs_ >= options_.lobbyCountdownMs) {
                    startMatch(now_);
                }
            } else {
                countdownActive_ = false;
            }
            break;
        case MatchPhase::Loading: {
            const bool allLoaded = !players_.empty() && std::all_of(players_.begin(), players_.end(),
                                                                    [](const auto& kv) { return kv.second.loaded; });
            if (allLoaded || now_ - phaseStartMs_ >= options_.loadTimeoutMs)
                beginPlaying();
            break;
        }
        case MatchPhase::Playing: {
            for (auto& kv : players_) {
                ServerPlayer& p = kv.second;
                if (!p.alive && p.respawnAtMs != 0 && now_ >= p.respawnAtMs)
                    spawnPlayer(p);
            }
            const uint32_t limit = options_.config.timeLimitSec;
            if (limit > 0 && now_ - phaseStartMs_ >= uint64_t(limit) * 1000) {
                log("time limit reached");
                endMatchWith(EndReason::TimeLimit);
            }
            if (phase_ == MatchPhase::Playing && now_ - lastSnapshotMs_ >= options_.snapshotIntervalMs) {
                lastSnapshotMs_ = now_;
                sendSnapshot();
            }
            break;
        }
        case MatchPhase::Results:
            if (now_ - phaseStartMs_ >= options_.resultsDurationMs) {
                for (auto& kv : players_)
                    kv.second.alive = false;
                setPhase(MatchPhase::Lobby);
            }
            break;
    }
}

void MatchServer::handleEvent(const TransportEvent& ev) {
    switch (ev.type) {
        case TransportEventType::Connected: {
            Peer p;
            p.connectedAtMs = now_;
            peers_[ev.peer] = p;
            break;
        }
        case TransportEventType::Disconnected:
            removePlayer(ev.peer);
            peers_.erase(ev.peer);
            break;
        case TransportEventType::Message: {
            auto it = peers_.find(ev.peer);
            if (it != peers_.end() && it->second.closeAtMs == 0)
                handleMessage(ev.peer, it->second, ev.data);
            break;
        }
    }
}

void MatchServer::handleMessage(PeerId peer, Peer& state, const std::vector<uint8_t>& data) {
    MsgType type;
    if (!peekType(data.data(), data.size(), type))
        return;
    if (type == MsgType::Hello) {
        if (state.player == kNoPlayer)
            onHello(peer, state, data);
        return;
    }
    if (state.player == kNoPlayer)
        return; // everything else needs a completed handshake
    auto it = players_.find(state.player);
    if (it == players_.end())
        return;
    ServerPlayer& p = it->second;
    switch (type) {
        case MsgType::LoadDone:
            if (phase_ == MatchPhase::Loading)
                p.loaded = true;
            break;
        case MsgType::PlayerState:
            onPlayerState(p, data);
            break;
        case MsgType::Hit:
            onHit(p, data);
            break;
        case MsgType::Chat:
            onChat(p, data);
            break;
        case MsgType::Ready:
            onReady(p, data);
            break;
        default:
            break; // server-to-client types from a client are ignored
    }
}

void MatchServer::reject(PeerId peer, Peer& state, RejectReason why, const std::string& text) {
    Reject r;
    r.reason = why;
    r.serverVersion = kProtocolVersion;
    r.message = text.substr(0, kMaxRejectMessageLength);
    sendTo(peer, encode(r), Channel::Reliable);
    state.closeAtMs = now_ + options_.rejectLingerMs;
    ++stats_.joinsRejected;
    log("rejected join: " + text);
}

uint8_t MatchServer::pickTeam() const {
    int count[2] = {0, 0};
    for (const auto& kv : players_)
        if (kv.second.team < 2)
            ++count[kv.second.team];
    return count[1] < count[0] ? 1 : 0;
}

void MatchServer::onHello(PeerId peer, Peer& state, const std::vector<uint8_t>& data) {
    Hello h;
    if (!decode(data, h)) {
        reject(peer, state, RejectReason::BadName, "malformed Hello");
        return;
    }
    if (h.protocolVersion != kProtocolVersion) {
        reject(peer, state, RejectReason::VersionMismatch,
               "protocol version " + std::to_string(kProtocolVersion) + " required");
        return;
    }
    if (options_.profileHash != 0 && h.profileHash != options_.profileHash) {
        reject(peer, state, RejectReason::ProfileMismatch, "disc profile mismatch");
        return;
    }
    if (!options_.password.empty() && h.password != options_.password) {
        reject(peer, state, RejectReason::BadPassword, "wrong password");
        return;
    }
    if (!validName(h.name)) {
        reject(peer, state, RejectReason::BadName, "invalid player name");
        return;
    }
    if (players_.size() >= options_.config.maxPlayers) {
        reject(peer, state, RejectReason::ServerFull, "server full");
        return;
    }

    PlayerId id = kNoPlayer;
    for (PlayerId c = 1; c <= kMaxPlayers; ++c)
        if (!players_.count(c)) {
            id = c;
            break;
        }
    if (id == kNoPlayer) {
        reject(peer, state, RejectReason::ServerFull, "server full");
        return;
    }

    ServerPlayer p;
    p.id = id;
    p.peer = peer;
    p.name = h.name;
    p.team = options_.config.mode == MatchMode::TeamDeathmatch ? pickTeam() : kNoTeam;
    state.player = id;

    Welcome w;
    w.player = id;
    w.tickRateHz = options_.tickRateHz;
    w.phase = phase_;
    w.serverTimeMs = serverTimeMs();
    w.config = options_.config;
    sendTo(peer, encode(w), Channel::Reliable);

    // Roster for the newcomer, then the newcomer to everyone else.
    for (const auto& kv : players_) {
        PlayerJoin j;
        j.player = kv.first;
        j.team = kv.second.team;
        j.name = kv.second.name;
        sendTo(peer, encode(j), Channel::Reliable);
        if (kv.second.ready) {
            Ready rd;
            rd.player = kv.first;
            rd.ready = true;
            sendTo(peer, encode(rd), Channel::Reliable);
        }
        if (kv.second.kills || kv.second.deaths) {
            Score s;
            s.player = kv.first;
            s.kills = kv.second.kills;
            s.deaths = kv.second.deaths;
            s.teamKills[0] = teamKills_[0];
            s.teamKills[1] = teamKills_[1];
            sendTo(peer, encode(s), Channel::Reliable);
        }
    }
    PlayerJoin self;
    self.player = id;
    self.team = p.team;
    self.name = p.name;
    broadcast(encode(self), Channel::Reliable); // includes the newcomer: it learns its own team this way

    players_[id] = p;
    log("player " + std::to_string(id) + " '" + p.name + "' joined (" + std::to_string(players_.size()) + "/" +
        std::to_string(options_.config.maxPlayers) + ")");

    if (phase_ == MatchPhase::Playing)
        spawnPlayer(players_[id]);
}

void MatchServer::removePlayer(PeerId peer) {
    auto pit = peers_.find(peer);
    if (pit == peers_.end() || pit->second.player == kNoPlayer)
        return;
    const PlayerId id = pit->second.player;
    auto it = players_.find(id);
    if (it == players_.end())
        return;
    log("player " + std::to_string(id) + " '" + it->second.name + "' left");
    players_.erase(it);
    pit->second.player = kNoPlayer;
    PlayerLeave l;
    l.player = id;
    broadcast(encode(l), Channel::Reliable);
    if (players_.empty() && phase_ != MatchPhase::Lobby) {
        log("last player left, back to lobby");
        setPhase(MatchPhase::Lobby);
    }
}

void MatchServer::setPhase(MatchPhase p) {
    phase_ = p;
    phaseStartMs_ = now_;
    countdownActive_ = false;
    PhaseChange pc;
    pc.phase = p;
    pc.serverTimeMs = serverTimeMs();
    broadcast(encode(pc), Channel::Reliable);
    log(std::string("phase -> ") + phaseName(p));
    if (p == MatchPhase::Lobby)
        for (auto& kv : players_)
            if (kv.second.ready) {
                kv.second.ready = false;
                Ready r;
                r.player = kv.first;
                r.ready = false;
                broadcast(encode(r), Channel::Reliable);
            }
}

void MatchServer::resetMatch() {
    teamKills_[0] = teamKills_[1] = 0;
    for (auto& kv : players_) {
        ServerPlayer& p = kv.second;
        p.kills = p.deaths = 0;
        p.alive = false;
        p.loaded = false;
        p.health = 0;
        p.hasState = false;
        p.hasHit = false;
        p.respawnAtMs = 0;
        scoreFor(p);
    }
}

bool MatchServer::startMatch(uint64_t nowMs) {
    now_ = nowMs;
    if (phase_ != MatchPhase::Lobby || players_.empty())
        return false;
    resetMatch();
    outcome_ = MatchOutcome{};
    MatchConfigMsg c;
    c.config = options_.config;
    broadcast(encode(c), Channel::Reliable);
    setPhase(MatchPhase::Loading);
    return true;
}

bool MatchServer::setConfig(const MatchConfigData& config) {
    if (phase_ != MatchPhase::Lobby || !configValid(config) || config.maxPlayers < players_.size())
        return false;
    options_.config = config;
    MatchConfigMsg c;
    c.config = config;
    broadcast(encode(c), Channel::Reliable);
    return true;
}

void MatchServer::beginPlaying() {
    setPhase(MatchPhase::Playing);
    lastSnapshotMs_ = now_;
    for (auto& kv : players_)
        spawnPlayer(kv.second);
}

void MatchServer::endMatch(uint64_t nowMs) {
    now_ = nowMs;
    if (phase_ == MatchPhase::Playing)
        endMatchWith(EndReason::Ended);
}

void MatchServer::endMatchWith(EndReason reason) {
    outcome_ = decideOutcome(scoreboard(), options_.config.mode, teamKills_, reason);
    setPhase(MatchPhase::Results);
    if (outcome_.tie)
        log("match over: tie");
    else if (outcome_.winner != kNoPlayer)
        log("match over: player " + std::to_string(outcome_.winner) + " wins");
    else
        log("match over: team " + std::to_string(outcome_.winningTeam) + " wins");
}

std::vector<ScoreRow> MatchServer::scoreboard() const {
    std::vector<ScoreRow> rows;
    for (const auto& kv : players_) {
        ScoreRow r;
        r.player = kv.first;
        r.name = kv.second.name;
        r.team = kv.second.team;
        r.kills = kv.second.kills;
        r.deaths = kv.second.deaths;
        r.health = kv.second.health;
        r.alive = kv.second.alive;
        rows.push_back(std::move(r));
    }
    sortScoreboard(rows);
    return rows;
}

uint16_t MatchServer::hitLimit(const ServerPlayer& attacker, uint8_t weapon) const {
    uint16_t limit = options_.maxDamagePerHit;
    if (weapon == kHitProjectile && options_.maxProjectileDamage > 0)
        limit = std::min(limit, options_.maxProjectileDamage);
    if (!attacker.hasState)
        return limit;
    const uint32_t record = weapon == kHitMeleeRight ? attacker.state.weaponRight : attacker.state.weaponLeft;
    if (record == 0) {
        if (weapon != kHitProjectile && options_.unarmedMaxDamage > 0)
            limit = std::min(limit, options_.unarmedMaxDamage);
        return limit;
    }
    const auto it = options_.weaponMaxDamage.find(record);
    return it == options_.weaponMaxDamage.end() ? limit : std::min(limit, it->second);
}

void MatchServer::spawnPlayer(ServerPlayer& p) {
    p.health = options_.config.startHealth;
    p.alive = true;
    p.respawnAtMs = 0;
    p.acceptAnyState = true;
    p.hasState = false; // the corpse's last pose must not be replicated as the new life's first one
    p.protectedUntilMs = now_ + options_.respawnProtectionMs;
    Respawn r;
    r.player = p.id;
    r.spawnIndex = spawnCounter_++;
    r.health = static_cast<uint16_t>(p.health);
    broadcast(encode(r), Channel::Reliable);
}

void MatchServer::scoreFor(const ServerPlayer& p) {
    Score s;
    s.player = p.id;
    s.kills = p.kills;
    s.deaths = p.deaths;
    s.teamKills[0] = teamKills_[0];
    s.teamKills[1] = teamKills_[1];
    broadcast(encode(s), Channel::Reliable);
}

void MatchServer::onPlayerState(ServerPlayer& p, const std::vector<uint8_t>& data) {
    if (phase_ != MatchPhase::Playing) // a dead player keeps publishing: its death animation is shown to the others
        return;
    PlayerState s;
    if (!decode(data, s))
        return;
    if (p.hasState && !sequenceNewer(s.sequence, p.state.sequence))
        return; // stale or duplicate
    constexpr float kMaxCoord = 1.0e6f;
    for (float v : s.pos)
        if (std::fabs(v) > kMaxCoord) {
            ++stats_.statesRejected;
            return;
        }
    // A level change (levelHash differs) moves the player to another coordinate space: not a speed violation.
    const bool levelChanged = p.hasState && s.levelHash != p.state.levelHash;
    if (p.hasState && !p.acceptAnyState && !levelChanged && options_.maxSpeed > 0.0f) {
        const float dx = s.pos[0] - p.state.pos[0];
        const float dy = s.pos[1] - p.state.pos[1];
        const float dz = s.pos[2] - p.state.pos[2];
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float dt = std::max<float>(static_cast<float>(now_ - p.stateAtMs), 20.0f) / 1000.0f;
        if (dist / dt > options_.maxSpeed) {
            ++stats_.statesRejected;
            return;
        }
    }
    s.player = p.id;
    p.state = s;
    p.stateAtMs = now_;
    p.hasState = true;
    p.acceptAnyState = false;
}

void MatchServer::onHit(ServerPlayer& attacker, const std::vector<uint8_t>& data) {
    Hit h;
    if (!decode(data, h)) {
        return;
    }
    auto vit = players_.find(h.victim);
    const char* why = nullptr; // first failed check, for the log
    if (phase_ != MatchPhase::Playing)
        why = "match not playing";
    else if (!attacker.alive)
        why = "attacker dead";
    else if (vit == players_.end() || vit->second.id == attacker.id)
        why = "no such victim";
    else if (!vit->second.alive)
        why = "victim dead";
    else if (h.damage < 1 || h.weapon > kHitProjectile)
        why = "bad damage or weapon";
    else if (h.damage > hitLimit(attacker, h.weapon))
        why = "damage above the weapon's limit";
    if (!why) {
        ServerPlayer& v = vit->second;
        const float range = h.weapon == kHitProjectile ? options_.maxHitRangeRanged : options_.maxHitRange;
        float dist = 0.0f;
        if (now_ < v.protectedUntilMs)
            why = "victim protected after its respawn";
        else if (options_.config.mode == MatchMode::TeamDeathmatch && v.team == attacker.team)
            why = "friendly fire";
        else if (attacker.hasHit && now_ - attacker.lastHitAtMs < options_.minHitIntervalMs)
            why = "attacker too fast";
        else if (range > 0.0f) {
            if (!attacker.hasState || !v.hasState) {
                why = "no position yet";
            } else {
                const float dx = attacker.state.pos[0] - v.state.pos[0];
                const float dy = attacker.state.pos[1] - v.state.pos[1];
                const float dz = attacker.state.pos[2] - v.state.pos[2];
                dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (dist > range)
                    why = "out of range";
            }
        }
        if (why && std::string(why) == "out of range")
            log("hit rejected: " + std::string(why) + " (" + std::to_string(static_cast<int>(dist)) + " > " +
                std::to_string(static_cast<int>(range)) + ")");
    }
    if (why) {
        ++stats_.hitsRejected;
        if (std::string(why) != "out of range" && stats_.hitsRejected <= 50)
            log(std::string("hit rejected: ") + why);
        return;
    }

    ServerPlayer& v = vit->second;
    ++stats_.hitsAccepted;
    attacker.lastHitAtMs = now_;
    attacker.hasHit = true;
    const uint16_t applied = static_cast<uint16_t>(
        std::clamp<long>(std::lround(static_cast<double>(h.damage) * options_.damageScale), 1L, 65535L));
    v.health = std::max<int32_t>(0, v.health - applied);

    Hit out;
    out.attacker = attacker.id;
    out.victim = v.id;
    out.weapon = h.weapon;
    out.damage = applied;
    out.healthAfter = static_cast<uint16_t>(v.health);
    broadcast(encode(out), Channel::Reliable);

    if (v.health > 0)
        return;
    v.alive = false;
    ++v.deaths;
    ++attacker.kills;
    if (attacker.team < 2)
        ++teamKills_[attacker.team];
    v.respawnAtMs = now_ + options_.config.respawnDelayMs;
    if (v.respawnAtMs == 0)
        v.respawnAtMs = 1;
    Death d;
    d.victim = v.id;
    d.killer = attacker.id;
    d.weapon = h.weapon;
    broadcast(encode(d), Channel::Reliable);
    scoreFor(attacker);
    scoreFor(v);
    log("player " + std::to_string(v.id) + " killed by " + std::to_string(attacker.id));
    checkMatchEnd();
}

void MatchServer::checkMatchEnd() {
    const uint16_t limit = options_.config.fragLimit;
    if (limit == 0 || phase_ != MatchPhase::Playing)
        return;
    bool reached = false;
    if (options_.config.mode == MatchMode::TeamDeathmatch) {
        reached = teamKills_[0] >= limit || teamKills_[1] >= limit;
    } else {
        for (const auto& kv : players_)
            if (kv.second.kills >= limit)
                reached = true;
    }
    if (reached) {
        log("frag limit reached");
        endMatchWith(EndReason::FragLimit);
    }
}

void MatchServer::onChat(ServerPlayer& p, const std::vector<uint8_t>& data) {
    Chat c;
    if (!decode(data, c))
        return;
    std::string clean;
    for (unsigned char ch : c.text)
        if (ch >= 0x20 && ch != 0x7f)
            clean.push_back(static_cast<char>(ch));
    if (clean.empty())
        return;
    c.player = p.id;
    c.text = clean;
    broadcast(encode(c), Channel::Reliable);
}

void MatchServer::onReady(ServerPlayer& p, const std::vector<uint8_t>& data) {
    Ready r;
    if (!decode(data, r) || phase_ != MatchPhase::Lobby)
        return;
    p.ready = r.ready;
    r.player = p.id;
    broadcast(encode(r), Channel::Reliable);
}

bool MatchServer::allReady() const {
    if (players_.empty())
        return false;
    for (const auto& kv : players_)
        if (!kv.second.ready)
            return false;
    return true;
}

ServerInfo MatchServer::info() const {
    ServerInfo i;
    i.serverId = options_.serverId;
    i.name = options_.config.serverName;
    i.level = options_.config.level;
    i.mode = options_.config.mode;
    i.phase = phase_;
    i.players = static_cast<uint8_t>(std::min<size_t>(players_.size(), options_.config.maxPlayers));
    i.maxPlayers = options_.config.maxPlayers;
    i.passwordProtected = !options_.password.empty();
    return i;
}

std::vector<uint8_t> MatchServer::answerQuery(const std::vector<uint8_t>& request) const {
    Query q;
    if (!decode(request, q) || q.protocolVersion != kProtocolVersion)
        return {};
    ServerInfo i = info();
    i.token = q.token;
    return encode(i);
}

void MatchServer::sendSnapshot() {
    Snapshot s;
    s.serverTimeMs = serverTimeMs();
    for (const auto& kv : players_)
        if (kv.second.hasState)
            s.players.push_back(kv.second.state);
    if (s.players.empty())
        return;
    broadcast(encode(s), Channel::Unreliable);
}

} // namespace drakan::net
