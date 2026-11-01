#include "net/MatchServer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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
            if (startPending_) {
                if (players_.empty())
                    startPending_ = false;
                else if (now_ - startPendingAtMs_ >= options_.startCountdownMs) {
                    startPending_ = false;
                    beginLoading();
                }
            }
            if (phase_ != MatchPhase::Lobby)
                break;
            if (options_.autoStartPlayers > 0 && players_.size() >= options_.autoStartPlayers) {
                if (!countdownActive_) {
                    countdownActive_ = true;
                    lobbyCountdownStartMs_ = now_;
                    log("lobby countdown started");
                } else if (now_ - lobbyCountdownStartMs_ >= options_.lobbyCountdownMs) {
                    beginLoading();
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
            for (size_t i = 0; i < pickupBackAtMs_.size(); ++i) { // arena pickups coming back
                if (pickupBackAtMs_[i] != 0 && now_ >= pickupBackAtMs_[i]) {
                    pickupBackAtMs_[i] = 0;
                    pickupTakenBy_[i] = kNoPlayer;
                    PickupState ps;
                    ps.index = static_cast<uint16_t>(i);
                    broadcast(encode(ps), Channel::Reliable);
                }
            }
            tickObjectives();
            const uint32_t limit = options_.config.mode == MatchMode::Coop ? 0 : options_.config.timeLimitSec;
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
            if (now_ - phaseStartMs_ >= options_.resultsDurationMs)
                leaveResults(false);
            break;
    }
    if (!players_.empty() && now_ - lastStatusMs_ >= kStatusIntervalMs) {
        lastStatusMs_ = now_;
        sendStatus();
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
        case MsgType::CoopCreatures:
            onCoopCreatures(p, data);
            break;
        case MsgType::CoopEvent:
            onCoopEvent(p, data);
            break;
        case MsgType::LevelChange:
            onLevelChange(p, data);
            break;
        case MsgType::PickupClaim:
            onPickupClaim(p, data);
            break;
        case MsgType::StatusEcho:
            onStatusEcho(p, data);
            break;
        case MsgType::Kick:
            onKick(p, data);
            break;
        case MsgType::Character:
            onSetCharacter(p, data);
            break;
        case MsgType::ResultsAction:
            onResultsAction(p, data);
            break;
        case MsgType::MapVote:
            onMapVote(p, data);
            break;
        case MsgType::Vitals:
            onVitals(p, data);
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

const ModeDef* MatchServer::activeMode() const {
    if (options_.config.modeId.empty())
        return nullptr;
    for (const ModeDef& m : options_.modes)
        if (m.id == options_.config.modeId)
            return &m;
    return nullptr;
}

uint8_t MatchServer::teamCount() const {
    if (options_.config.mode != MatchMode::TeamDeathmatch)
        return 0;
    const ModeDef* m = activeMode();
    return m ? std::min<uint8_t>(m->teamCount(), static_cast<uint8_t>(kMaxTeams)) : 2;
}

// The team with the fewest players (the lowest index on a tie); `except` is a player that is being placed.
uint8_t MatchServer::pickTeam(PlayerId except) const {
    int count[kMaxTeams] = {0, 0, 0, 0};
    for (const auto& kv : players_)
        if (kv.first != except && kv.second.team < kMaxTeams)
            ++count[kv.second.team];
    const size_t teams = std::max<size_t>(teamCount(), 2);
    size_t best = 0;
    for (size_t t = 1; t < teams; ++t)
        if (count[t] < count[best])
            best = t;
    return static_cast<uint8_t>(best);
}

// A match is about to start: every player has a team of the mode's team count (or none in a free for all). Players that
// joined under other rules (the host changed the mode in the lobby) are placed now and the clients told.
void MatchServer::fixTeams() {
    const uint8_t teams = teamCount();
    for (auto& kv : players_) {
        ServerPlayer& p = kv.second;
        const uint8_t want = teams == 0 ? kNoTeam : (p.team < teams ? p.team : pickTeam(p.id));
        if (want == p.team)
            continue;
        p.team = want;
        PlayerJoin j;
        j.player = p.id;
        j.team = p.team;
        j.name = p.name;
        broadcast(encode(j), Channel::Reliable);
    }
}

Score MatchServer::makeScore(const ServerPlayer& p) const {
    Score s;
    s.player = p.id;
    s.kills = p.kills;
    s.deaths = p.deaths;
    s.teamKills[0] = teamKills_[0];
    s.teamKills[1] = teamKills_[1];
    s.score = p.score;
    for (size_t t = 0; t < kMaxTeams; ++t)
        s.teamScore[t] = teamScore_[t];
    return s;
}

// Objective points to a player (and its team).
void MatchServer::award(ServerPlayer& p, int32_t points) {
    p.score += points;
    if (p.team < kMaxTeams)
        teamScore_[p.team] += points;
}

void MatchServer::announce(const std::string& text) {
    Chat c;
    c.player = 0; // system line
    c.text = text;
    broadcast(encode(c), Channel::Reliable);
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
    p.team = teamCount() > 0 ? pickTeam() : kNoTeam;
    p.joinSeq = ++joinCounter_;
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
        if (kv.second.kills || kv.second.deaths || kv.second.score)
            sendTo(peer, encode(makeScore(kv.second)), Channel::Reliable);
    }
    PlayerJoin self;
    self.player = id;
    self.team = p.team;
    self.name = p.name;
    broadcast(encode(self), Channel::Reliable); // includes the newcomer: it learns its own team this way

    players_[id] = p;
    log("player " + std::to_string(id) + " '" + p.name + "' joined (" + std::to_string(players_.size()) + "/" +
        std::to_string(options_.config.maxPlayers) + ")");

    if (options_.config.mode == MatchMode::Coop) {
        if (coopAuthority_ == kNoPlayer)
            assignCoopAuthority();
        else {
            CoopAuthority a;
            a.player = coopAuthority_;
            sendTo(peer, encode(a), Channel::Reliable);
        }
    }

    if (phase_ == MatchPhase::Playing) {
        for (size_t i = 0; i < pickupBackAtMs_.size(); ++i) { // the newcomer learns which arena pickups are gone
            if (pickupBackAtMs_[i] == 0)
                continue;
            PickupState ps;
            ps.index = static_cast<uint16_t>(i);
            ps.taken = true;
            ps.player = pickupTakenBy_[i];
            sendTo(peer, encode(ps), Channel::Reliable);
        }
        spawnPlayer(players_[id]);
    }
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
    for (PlayerId& c : flagCarrier_)
        if (c == id)
            c = kNoPlayer; // its flag is back home
    players_.erase(it);
    if (votes_.erase(id) && phase_ == MatchPhase::Results)
        sendVoteState();
    pit->second.player = kNoPlayer;
    PlayerLeave l;
    l.player = id;
    broadcast(encode(l), Channel::Reliable);
    if (id == coopAuthority_) {
        // Host migration: the longest-connected remaining player owns the creature world from now on. The announcement
        // follows PlayerLeave on the same reliable channel, so every client sees the old owner go before the new one is
        // named.
        coopAuthority_ = kNoPlayer;
        if (!players_.empty()) {
            assignCoopAuthority();
            ++stats_.coopMigrations;
        } else
            broadcast(encode(CoopAuthority{}), Channel::Reliable);
    }
    if (players_.empty() && phase_ != MatchPhase::Lobby) {
        log("last player left, back to lobby");
        setPhase(MatchPhase::Lobby);
    }
}

void MatchServer::setPhase(MatchPhase p, bool byHost) {
    phase_ = p;
    phaseStartMs_ = now_;
    countdownActive_ = false;
    startPending_ = false;
    PhaseChange pc;
    pc.phase = p;
    pc.serverTimeMs = serverTimeMs();
    pc.byHost = byHost;
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
    for (int32_t& t : teamScore_)
        t = 0;
    resetObjectives();
    for (auto& kv : players_) {
        ServerPlayer& p = kv.second;
        p.kills = p.deaths = 0;
        p.score = 0;
        p.objectives = 0;
        p.carriedFlag = -1;
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
    if (options_.startCountdownMs > 0) {
        if (!startPending_) {
            startPending_ = true;
            startPendingAtMs_ = now_;
            log("start countdown " + std::to_string(options_.startCountdownMs) + " ms");
            lastStatusMs_ = 0; // tell the clients now
        }
        return true;
    }
    beginLoading();
    return true;
}

CountdownKind MatchServer::countdown(uint32_t& remainingMs) const {
    remainingMs = 0;
    const auto left = [&](uint64_t startedAt, uint32_t total) {
        const uint64_t el = now_ >= startedAt ? now_ - startedAt : 0;
        return static_cast<uint32_t>(el >= total ? 0 : total - el);
    };
    if (phase_ == MatchPhase::Lobby && startPending_) {
        remainingMs = left(startPendingAtMs_, options_.startCountdownMs);
        return CountdownKind::MatchStart;
    }
    if (phase_ == MatchPhase::Lobby && countdownActive_) {
        remainingMs = left(lobbyCountdownStartMs_, options_.lobbyCountdownMs);
        return CountdownKind::MatchStart;
    }
    if (phase_ == MatchPhase::Results) {
        remainingMs = left(phaseStartMs_, options_.resultsDurationMs);
        return CountdownKind::NextMatch;
    }
    return CountdownKind::None;
}

PlayerId MatchServer::hostPlayer() const {
    const ServerPlayer* best = nullptr;
    for (const auto& kv : players_)
        if (!best || kv.second.joinSeq < best->joinSeq)
            best = &kv.second;
    return best ? best->id : kNoPlayer;
}

void MatchServer::sendStatus() {
    Status st;
    st.token = serverTimeMs();
    uint32_t ms = 0;
    st.countdown = countdown(ms);
    st.countdownMs = ms;
    st.host = hostPlayer();
    for (const auto& kv : players_)
        if (st.pings.size() < kMaxStatusPlayers)
            st.pings.push_back({kv.first, kv.second.pingMs, kv.second.character});
    broadcast(encode(st), Channel::Unreliable);
    Vitals v;
    for (const auto& kv : players_)
        if (kv.second.hasVitals && v.entries.size() < kMaxStatusPlayers)
            v.entries.push_back(kv.second.vitals);
    if (!v.entries.empty())
        broadcast(encode(v), Channel::Unreliable);
}

void MatchServer::onVitals(ServerPlayer& p, const std::vector<uint8_t>& data) {
    Vitals v;
    if (!decode(data, v) || v.entries.size() != 1)
        return;
    const PlayerVitals old = p.vitals;
    p.vitals = v.entries[0];
    p.vitals.player = p.id;
    p.hasVitals = true;
    if (old.hp != p.vitals.hp || old.maxHp != p.vitals.maxHp || old.dragonHp != p.vitals.dragonHp)
        lastStatusMs_ = 0; // a change reaches everybody at the next tick
}

void MatchServer::onStatusEcho(ServerPlayer& p, const std::vector<uint8_t>& data) {
    StatusEcho e;
    if (!decode(data, e))
        return;
    const uint32_t rtt = serverTimeMs() - e.token; // wraps like the clock
    if (rtt > 20000)
        return; // a stale echo
    p.pingMs = p.pingMs == kPingUnknown
                   ? static_cast<uint16_t>(rtt)
                   : static_cast<uint16_t>(p.pingMs + (static_cast<int>(rtt) - static_cast<int>(p.pingMs)) / 3);
}

bool MatchServer::kick(PlayerId target) {
    auto it = players_.find(target);
    if (it == players_.end())
        return false;
    const PeerId peer = it->second.peer;
    auto pit = peers_.find(peer);
    if (pit == peers_.end())
        return false;
    log("player " + std::to_string(target) + " '" + it->second.name + "' kicked by the host");
    Reject r;
    r.reason = RejectReason::Kicked;
    r.message = "Kicked by the host";
    sendTo(peer, encode(r), Channel::Reliable);
    removePlayer(peer);
    pit->second.closeAtMs = now_ + options_.rejectLingerMs; // the Reject has time to arrive, then the peer goes
    return true;
}

void MatchServer::onSetCharacter(ServerPlayer& p, const std::vector<uint8_t>& data) {
    SetCharacter c;
    if (!decode(data, c) || phase_ != MatchPhase::Lobby)
        return;
    p.character = c.character;
    lastStatusMs_ = 0; // everybody sees it at the next tick
}

void MatchServer::onKick(ServerPlayer& p, const std::vector<uint8_t>& data) {
    Kick k;
    if (!decode(data, k) || !options_.hostKick || p.id != hostPlayer() || k.player == p.id)
        return;
    kick(k.player);
}

void MatchServer::beginLoading() {
    startPending_ = false;
    if (phase_ != MatchPhase::Lobby || players_.empty())
        return;
    fixTeams();
    resetMatch();
    outcome_ = MatchOutcome{};
    MatchConfigMsg c;
    c.config = options_.config;
    broadcast(encode(c), Channel::Reliable);
    setPhase(MatchPhase::Loading);
}

bool MatchServer::setConfig(const MatchConfigData& config) {
    if (phase_ != MatchPhase::Lobby || !configValid(config) || config.maxPlayers < players_.size())
        return false;
    options_.config = config;
    MatchConfigMsg c;
    c.config = config;
    broadcast(encode(c), Channel::Reliable);
    if (config.mode != MatchMode::Coop && coopAuthority_ != kNoPlayer) {
        coopAuthority_ = kNoPlayer;
        broadcast(encode(CoopAuthority{}), Channel::Reliable);
    }
    if (config.mode == MatchMode::Coop && coopAuthority_ == kNoPlayer)
        assignCoopAuthority();
    return true;
}

void MatchServer::beginPlaying() {
    setPhase(MatchPhase::Playing);
    lastSnapshotMs_ = now_;
    lastObjectiveMs_ = now_;
    resetPickups();
    for (auto& kv : players_) {
        kv.second.lastSpawn = -1;
        kv.second.hasSpawnPos = false;
    }
    for (auto& kv : players_)
        spawnPlayer(kv.second);
}

void MatchServer::endMatch(uint64_t nowMs) {
    now_ = nowMs;
    if (phase_ == MatchPhase::Playing)
        endMatchWith(EndReason::Ended);
}

void MatchServer::endMatchWith(EndReason reason) {
    outcome_ = decideOutcome(scoreboard(), options_.config.mode, teamScore_, reason, teamCount());
    setPhase(MatchPhase::Results);
    if (outcome_.tie)
        log("match over: tie");
    else if (outcome_.winner != kNoPlayer)
        log("match over: player " + std::to_string(outcome_.winner) + " wins");
    else
        log("match over: team " + std::to_string(outcome_.winningTeam) + " wins");
    beginVote();
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
        r.hasScore = true;
        r.score = kv.second.score;
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
    const int arenaSpawn = chooseSpawn(p);
    if (arenaSpawn >= 0) {
        r.spawnIndex = static_cast<uint8_t>(arenaSpawn);
        p.lastSpawn = arenaSpawn;
        p.spawnPos[0] = options_.arena.spawns[static_cast<size_t>(arenaSpawn)].pos[0];
        p.spawnPos[1] = options_.arena.spawns[static_cast<size_t>(arenaSpawn)].pos[2];
        p.hasSpawnPos = true;
        p.spawnedAtMs = now_;
        p.spawnPending = true;
    } else {
        r.spawnIndex = spawnCounter_++;
    }
    r.health = static_cast<uint16_t>(p.health);
    broadcast(encode(r), Channel::Reliable);
}

void MatchServer::scoreFor(const ServerPlayer& p) {
    broadcast(encode(makeScore(p)), Channel::Reliable);
}

constexpr uint64_t kSpawnTrustMs = 8000; // arena: how long a fresh spawn point stands in for the player's position
constexpr float kSpawnArrivalRadius = 3000.0f; // a state this close to the spawn point is the player arriving there
constexpr uint32_t kResyncStreak = 12;         // agreeing speed-rejected states before the server believes a teleport

void MatchServer::onPlayerState(ServerPlayer& p, const std::vector<uint8_t>& data) {
    if (phase_ != MatchPhase::Playing) // a dead player keeps publishing: its death animation is shown to the others
        return;
    PlayerState s;
    if (!decode(data, s))
        return;
    if (p.hasState && !sequenceNewer(s.sequence, p.state.sequence))
        return;                         // stale or duplicate
    constexpr float kMaxCoord = 4.0e6f; // Ravenshold lies at x = 1.3e6
    for (float v : s.pos)
        if (std::fabs(v) > kMaxCoord) {
            ++stats_.statesRejected;
            return;
        }
    // The spawn point stands in for a stale position right after a (re)spawn: a state that arrives there is the game
    // having moved the player (her last published position is the old life's or the level start's). A level takes
    // longer than any fixed window to load, so this holds until she has arrived once.
    bool arrivedAtSpawn = false;
    if (p.hasSpawnPos && p.spawnPending) {
        const float sx = s.pos[0] - p.spawnPos[0], sz = s.pos[2] - p.spawnPos[1];
        arrivedAtSpawn = sx * sx + sz * sz < kSpawnArrivalRadius * kSpawnArrivalRadius;
        if (arrivedAtSpawn)
            p.spawnPending = false;
    }
    // A level change (levelHash differs) moves the player to another coordinate space: not a speed violation.
    const bool levelChanged = p.hasState && s.levelHash != p.state.levelHash;
    if (p.hasState && !p.acceptAnyState && !levelChanged && options_.maxSpeed > 0.0f) {
        const float dx = s.pos[0] - p.state.pos[0];
        const float dy = s.pos[1] - p.state.pos[1];
        const float dz = s.pos[2] - p.state.pos[2];
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float dt = std::max<float>(static_cast<float>(now_ - p.stateAtMs), 20.0f) / 1000.0f;
        if (dist / dt > options_.maxSpeed && !arrivedAtSpawn) {
            // Several rejected states that follow each other at a believable speed are a teleport (a respawn the
            // server was not told about, a level change on the client): resync instead of rejecting for ever.
            const float rx = s.pos[0] - p.lastRejectPos[0], ry = s.pos[1] - p.lastRejectPos[1],
                        rz = s.pos[2] - p.lastRejectPos[2];
            const bool consistent =
                p.rejectStreak > 0 && std::sqrt(rx * rx + ry * ry + rz * rz) / dt <= options_.maxSpeed;
            p.rejectStreak = consistent ? p.rejectStreak + 1 : 1;
            std::memcpy(p.lastRejectPos, s.pos, sizeof p.lastRejectPos);
            ++stats_.statesRejected;
            if (p.rejectStreak < kResyncStreak)
                return;
            log("player " + std::to_string(p.id) + " resynced to " + std::to_string(static_cast<int>(s.pos[0])) + " " +
                std::to_string(static_cast<int>(s.pos[2])) + " after " + std::to_string(p.rejectStreak) +
                " rejected states (stale last position)");
        }
        p.rejectStreak = 0;
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
    if (options_.config.mode == MatchMode::Coop)
        why = "co-op: players cannot hurt each other";
    else if (phase_ != MatchPhase::Playing)
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
        else if (options_.config.mode == MatchMode::TeamDeathmatch && v.team == attacker.team &&
                 !(activeMode() && activeMode()->friendlyFire))
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
    {
        // Points: a kill is worth the mode's score_kill, a death its score_death; a team kill (friendly fire on) earns
        // nothing. Without a custom mode that is one point per kill.
        const ModeDef* mode = activeMode();
        const int32_t perKill = mode ? mode->scoreKill : 1, perDeath = mode ? mode->scoreDeath : 0;
        const bool teamKill = options_.config.mode == MatchMode::TeamDeathmatch && v.team == attacker.team;
        if (!teamKill)
            award(attacker, perKill);
        award(v, perDeath);
    }
    if (v.carriedFlag >= 0 && static_cast<size_t>(v.carriedFlag) < flagCarrier_.size()) {
        flagCarrier_[static_cast<size_t>(v.carriedFlag)] = kNoPlayer; // dropped: the flag is back home
        announce(v.name + " lost the flag");
    }
    v.carriedFlag = -1;
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
    if (limit == 0 || phase_ != MatchPhase::Playing || options_.config.mode == MatchMode::Coop)
        return;
    bool reached = false;
    if (options_.config.mode == MatchMode::TeamDeathmatch) {
        for (size_t t = 0; t < std::max<size_t>(teamCount(), 2); ++t)
            reached = reached || teamScore_[t] >= limit;
    } else {
        for (const auto& kv : players_)
            if (kv.second.score >= limit)
                reached = true;
    }
    if (reached) {
        log("frag limit reached");
        endMatchWith(EndReason::FragLimit);
    }
}

void MatchServer::returnToLobby(bool byHost) {
    for (auto& kv : players_)
        kv.second.alive = false;
    setPhase(MatchPhase::Lobby, byHost);
}

// "Next map": the arena after the current one in options_.rotation that the match's mode fits (wrapping around).
// False when there is none other than the current one (the caller then replays the same map).
bool MatchServer::advanceMap() {
    if (options_.rotation.empty() || options_.config.mode == MatchMode::Coop)
        return false;
    size_t cur = options_.rotation.size();
    for (size_t i = 0; i < options_.rotation.size(); ++i)
        if (options_.rotation[i].name == options_.config.level || options_.rotation[i].id == options_.config.level)
            cur = i;
    const ModeDef* mode = activeMode();
    for (size_t step = 1; step <= options_.rotation.size(); ++step) {
        const size_t i = (cur == options_.rotation.size() ? step - 1 : (cur + step) % options_.rotation.size());
        const ArenaDef& a = options_.rotation[i];
        if (i == cur)
            break;
        const bool fits = mode ? modeFitsArena(*mode, a)
                               : (options_.config.mode == MatchMode::TeamDeathmatch ? a.teamDeathmatch : a.deathmatch);
        if (!fits)
            continue;
        return applyArena(a);
    }
    return false;
}

// Makes `a` the match's map (lobby only, like setConfig).
bool MatchServer::applyArena(const ArenaDef& a) {
    const ModeDef* mode = activeMode();
    MatchConfigData c = options_.config;
    c.level = a.name;
    c.startHealth = a.startHealth;
    if (mode && mode->startHealth)
        c.startHealth = mode->startHealth;
    c.maxPlayers = std::max<uint8_t>(static_cast<uint8_t>(players_.size()),
                                     std::min<uint8_t>(options_.config.maxPlayers, a.maxPlayers));
    if (!setConfig(c))
        return false;
    options_.hasArena = true;
    options_.arena = a;
    log("next map: " + a.name);
    return true;
}

// The vote opens with the results: up to kMaxVoteCandidates maps of the rotation that follow the current one and fit
// the mode; with fewer than that the current map fills the list, so there are two or three choices (none when the
// rotation offers no other map, in co-op or without a rotation).
void MatchServer::beginVote() {
    voteCands_.clear();
    votes_.clear();
    if (options_.rotation.empty() || options_.config.mode == MatchMode::Coop)
        return;
    const size_t n = options_.rotation.size();
    size_t cur = n;
    for (size_t i = 0; i < n; ++i)
        if (options_.rotation[i].name == options_.config.level || options_.rotation[i].id == options_.config.level)
            cur = i;
    const ModeDef* mode = activeMode();
    for (size_t step = 1; step <= n && voteCands_.size() < kMaxVoteCandidates; ++step) {
        const size_t i = (cur == n ? step - 1 : (cur + step) % n);
        if (i == cur)
            break;
        const ArenaDef& a = options_.rotation[i];
        const bool fits = mode ? modeFitsArena(*mode, a)
                               : (options_.config.mode == MatchMode::TeamDeathmatch ? a.teamDeathmatch : a.deathmatch);
        if (fits)
            voteCands_.push_back(a);
    }
    if (cur != n && !voteCands_.empty() && voteCands_.size() < kMaxVoteCandidates)
        voteCands_.push_back(options_.rotation[cur]);
    if (voteCands_.size() < 2) {
        voteCands_.clear();
        return;
    }
    sendVoteState();
}

int MatchServer::voteTally(size_t candidate) const {
    int n = 0;
    for (const auto& kv : votes_)
        if (kv.second == candidate)
            ++n;
    return n;
}

// The leading candidate (ties: the lowest index), -1 without votes.
int MatchServer::voteLeader() const {
    int best = -1, bestN = 0;
    for (size_t i = 0; i < voteCands_.size(); ++i) {
        const int n = voteTally(i);
        if (n > bestN) {
            bestN = n;
            best = static_cast<int>(i);
        }
    }
    return best;
}

void MatchServer::sendVoteState(uint8_t winner) {
    MapVoteState s;
    for (size_t i = 0; i < voteCands_.size(); ++i) {
        s.candidates.push_back(voteCands_[i].name);
        s.tally.push_back(static_cast<uint8_t>(std::min(voteTally(i), 255)));
    }
    s.winner = winner;
    broadcast(encode(s), Channel::Reliable);
}

void MatchServer::onMapVote(ServerPlayer& p, const std::vector<uint8_t>& data) {
    MapVote v;
    if (!decode(data, v) || phase_ != MatchPhase::Results || voteCands_.empty())
        return;
    if (v.choice == kNoVote)
        votes_.erase(p.id);
    else if (v.choice < voteCands_.size())
        votes_[p.id] = v.choice;
    else
        return;
    log("vote: player " + std::to_string(p.id) +
        (v.choice == kNoVote ? std::string(" withdrew") : " voted " + voteCands_[v.choice].name));
    sendVoteState();
}

// Results over: back to the lobby, on the map the vote chose (when anybody voted).
bool MatchServer::leaveResults(bool byHost) {
    const int w = voteLeader();
    if (w >= 0)
        sendVoteState(static_cast<uint8_t>(w));
    returnToLobby(byHost);
    bool applied = false;
    if (w >= 0) {
        const ArenaDef a = voteCands_[static_cast<size_t>(w)];
        applied = applyArena(a);
        log("vote result: " + a.name);
    }
    voteCands_.clear();
    votes_.clear();
    return applied;
}

// The results screen's buttons. Only the host (the longest connected player) decides; Leave is a disconnect, which
// needs no message.
void MatchServer::onResultsAction(ServerPlayer& p, const std::vector<uint8_t>& data) {
    ResultsAction a;
    if (!decode(data, a) || phase_ != MatchPhase::Results || p.id != hostPlayer())
        return;
    switch (a.choice) {
        case ResultsChoice::BackToLobby:
            log("results: back to the lobby (host)");
            leaveResults(true);
            break;
        case ResultsChoice::NextMap:
        case ResultsChoice::Rematch:
            log(a.choice == ResultsChoice::NextMap ? "results: next map (host)" : "results: rematch (host)");
            if (a.choice == ResultsChoice::NextMap) {
                if (!leaveResults(true))
                    advanceMap();
            } else
                returnToLobby(true);
            startMatch(now_);
            break;
    }
}

void MatchServer::resetObjectives() {
    const size_t n = options_.hasArena ? options_.arena.markers.size() : 0;
    flagCarrier_.assign(n, kNoPlayer);
    zones_.assign(n, ZoneState{});
    lastObjectiveMs_ = now_;
}

// Capture and hold objectives of the active custom mode, on the arena's markers (docs/reference/netcode.md "Custom game
// modes"). Capture: touching a flag marker that is not your team's takes it, bringing it to one of your team's base
// markers scores `points + score_objective`; dying or leaving returns the flag. Hold: the only team (the only player in
// a free for all) inside a zone for `seconds` scores the same; a contested or empty zone starts over.
void MatchServer::tickObjectives() {
    const ModeDef* mode = activeMode();
    if (!mode || mode->objectives.empty() || !options_.hasArena || phase_ != MatchPhase::Playing)
        return;
    const std::vector<ArenaMarker>& markers = options_.arena.markers;
    if (flagCarrier_.size() != markers.size())
        resetObjectives();
    const uint64_t dt = now_ >= lastObjectiveMs_ ? now_ - lastObjectiveMs_ : 0;
    lastObjectiveMs_ = now_;
    const auto inside = [](const ServerPlayer& p, const ArenaMarker& m, float radius) {
        if (!p.alive || !p.hasState)
            return false;
        const float dx = p.state.pos[0] - m.pos[0], dz = p.state.pos[2] - m.pos[2];
        return dx * dx + dz * dz <= radius * radius;
    };
    const ModeObjective* capture = nullptr;
    for (const ModeObjective& o : mode->objectives)
        if (o.kind == ModeObjective::Kind::Capture && !capture)
            capture = &o;
    if (capture) {
        for (auto& kv : players_) {
            ServerPlayer& p = kv.second;
            if (p.carriedFlag < 0) {
                for (size_t i = 0; i < markers.size(); ++i) {
                    const ArenaMarker& m = markers[i];
                    if (m.kind != ArenaMarker::Kind::Flag || flagCarrier_[i] != kNoPlayer)
                        continue;
                    if (m.team != 0 && p.team != kNoTeam && m.team == p.team + 1)
                        continue; // a team does not take its own flag
                    if (!inside(p, m, capture->radius > 0 ? capture->radius : m.radius))
                        continue;
                    flagCarrier_[i] = p.id;
                    p.carriedFlag = static_cast<int>(i);
                    announce(p.name + " took the flag");
                    break;
                }
            } else {
                const size_t f = static_cast<size_t>(p.carriedFlag);
                for (size_t i = 0; i < markers.size(); ++i) {
                    const ArenaMarker& m = markers[i];
                    if (m.kind != ArenaMarker::Kind::Base)
                        continue;
                    if (m.team != 0 && p.team != kNoTeam && m.team != p.team + 1)
                        continue; // another team's base
                    if (!inside(p, m, capture->radius > 0 ? capture->radius : m.radius))
                        continue;
                    const int32_t pts = static_cast<int32_t>(capture->points) + mode->scoreObjective;
                    award(p, pts);
                    ++p.objectives;
                    if (f < flagCarrier_.size())
                        flagCarrier_[f] = kNoPlayer;
                    p.carriedFlag = -1;
                    announce(p.name + " captured the flag (+" + std::to_string(pts) + ")");
                    scoreFor(p);
                    checkMatchEnd();
                    break;
                }
            }
            if (phase_ != MatchPhase::Playing)
                return;
        }
    }
    for (const ModeObjective& o : mode->objectives) {
        if (o.kind != ModeObjective::Kind::Hold)
            continue;
        for (size_t i = 0; i < markers.size(); ++i) {
            const ArenaMarker& m = markers[i];
            if (m.kind != ArenaMarker::Kind::Zone)
                continue;
            int holder = -1;
            bool contested = false;
            ServerPlayer* who = nullptr;
            for (auto& kv : players_) {
                ServerPlayer& p = kv.second;
                if (!inside(p, m, o.radius > 0 ? o.radius : m.radius))
                    continue;
                const int key = p.team != kNoTeam ? p.team : 100 + p.id;
                if (holder >= 0 && holder != key)
                    contested = true;
                holder = key;
                who = &p;
            }
            ZoneState& z = zones_[i];
            if (contested || holder < 0 || holder != z.holder) {
                z.holder = contested ? -1 : holder;
                z.heldMs = 0;
                continue;
            }
            z.heldMs += dt;
            if (z.heldMs < uint64_t(o.seconds) * 1000)
                continue;
            z.heldMs = 0;
            const int32_t pts = static_cast<int32_t>(o.points) + mode->scoreObjective;
            award(*who, pts);
            ++who->objectives;
            announce((who->team != kNoTeam ? std::string("Team ") + std::to_string(who->team + 1) : who->name) +
                     " held the zone (+" + std::to_string(pts) + ")");
            scoreFor(*who);
            checkMatchEnd();
            if (phase_ != MatchPhase::Playing)
                return;
        }
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

void MatchServer::sendExcept(PlayerId except, const std::vector<uint8_t>& bytes, Channel ch) {
    for (const auto& kv : peers_)
        if (kv.second.player != kNoPlayer && kv.second.player != except)
            transport_.send(kv.first, ch, bytes);
}

void MatchServer::assignCoopAuthority() {
    if (options_.config.mode != MatchMode::Coop || coopAuthority_ != kNoPlayer || players_.empty())
        return;
    // The longest-connected player (lowest join sequence; ids are recycled, so the lowest id is not necessarily the
    // oldest). For the first appointment that is the first joiner, after a migration the next one in line.
    coopAuthority_ = players_.begin()->first;
    for (const auto& kv : players_)
        if (kv.second.joinSeq < players_[coopAuthority_].joinSeq)
            coopAuthority_ = kv.first;
    CoopAuthority a;
    a.player = coopAuthority_;
    broadcast(encode(a), Channel::Reliable);
    log("co-op authority: player " + std::to_string(coopAuthority_));
}

// v6 relay rules (Coop mode, phase Playing): creature streams and authority events only from the authority; claims,
// death notices and catch-up requests only from the others and only to the authority; object events from anyone to
// everyone else. The server stamps the sender and never looks at the payload beyond range checks.
void MatchServer::onCoopCreatures(ServerPlayer& p, const std::vector<uint8_t>& data) {
    CoopCreatures c;
    if (options_.config.mode != MatchMode::Coop || phase_ != MatchPhase::Playing || p.id != coopAuthority_ ||
        !decode(data, c)) {
        ++stats_.coopRejected;
        return;
    }
    ++stats_.coopRelayed;
    sendExcept(p.id, data, Channel::Unreliable);
}

void MatchServer::onCoopEvent(ServerPlayer& p, const std::vector<uint8_t>& data) {
    CoopEvent e;
    if (options_.config.mode != MatchMode::Coop || phase_ != MatchPhase::Playing || !decode(data, e)) {
        ++stats_.coopRejected;
        return;
    }
    const bool fromAuthority = p.id == coopAuthority_;
    if ((coopFromAuthorityOnly(e.kind) && !fromAuthority) || (coopToAuthorityOnly(e.kind) && fromAuthority) ||
        (e.kind == CoopKind::Claim && (e.value < 0 || e.value > 10000)) ||
        (e.kind == CoopKind::Hurt && (e.value < 0 || e.value > 10000))) {
        ++stats_.coopRejected;
        return;
    }
    if (coopToAuthorityOnly(e.kind)) {
        e.player = p.id;
        const auto auth = players_.find(coopAuthority_);
        if (auth == players_.end()) {
            ++stats_.coopRejected;
            return;
        }
        ++stats_.coopRelayed;
        sendTo(auth->second.peer, encode(e), Channel::Reliable);
        return;
    }
    if (e.kind == CoopKind::Hurt || e.kind == CoopKind::Respawn) {
        // Authority-origin and targeted: player = target (0 = everyone else); a target that is not here drops it.
        const PlayerId target = e.player;
        if (target != kNoPlayer) {
            const auto t = players_.find(target);
            if (t == players_.end() || target == p.id) {
                ++stats_.coopRejected;
                return;
            }
            ++stats_.coopRelayed;
            sendTo(t->second.peer, encode(e), Channel::Reliable);
            return;
        }
        ++stats_.coopRelayed;
        sendExcept(p.id, encode(e), Channel::Reliable);
        return;
    }
    e.player = p.id; // CreatureDied, ObjGone, ObjMsg, ObjState: the sender, to everyone else
    ++stats_.coopRelayed;
    sendExcept(p.id, encode(e), Channel::Reliable);
}

void MatchServer::onLevelChange(ServerPlayer& p, const std::vector<uint8_t>& data) {
    LevelChange lc;
    if (phase_ != MatchPhase::Playing || !decode(data, lc)) {
        ++stats_.coopRejected;
        return;
    }
    // Who may lead: the co-op authority; without one (deathmatch) the lowest player id (the host of a listen server).
    const PlayerId leader = options_.config.mode == MatchMode::Coop ? coopAuthority_ : players_.begin()->first;
    constexpr float kMaxCoord = 4.0e6f; // Ravenshold lies at x = 1.3e6
    for (float v : lc.exit)
        if (std::fabs(v) > kMaxCoord) {
            ++stats_.coopRejected;
            return;
        }
    if (p.id != leader) {
        ++stats_.coopRejected;
        return;
    }
    lc.player = p.id;
    ++stats_.coopRelayed;
    log("player " + std::to_string(p.id) + " changed level (dest hash " + std::to_string(lc.destHash) + ")");
    sendExcept(p.id, encode(lc), Channel::Reliable);
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
    i.mods = options_.mods.substr(0, kMaxModsLength);
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

// ---- PvP arena: spawn choice and pickups (docs/reference/netcode.md "Multiplayer maps") ----

namespace {}

int MatchServer::chooseSpawn(const ServerPlayer& p) const {
    if (!options_.hasArena || options_.arena.spawns.empty())
        return -1;
    std::vector<std::pair<float, float>> others;
    for (const auto& kv : players_) {
        const ServerPlayer& o = kv.second;
        if (o.id == p.id || !o.alive)
            continue;
        // A player's published position is the old life's / the level start's for a few seconds after a spawn (the game
        // moves her to the spawn point at the next frame boundary after the level is ready): trust the spawn point
        // first.
        const bool fresh = o.hasSpawnPos && now_ - o.spawnedAtMs < kSpawnTrustMs;
        if (!fresh && o.hasState && o.stateAtMs != 0)
            others.emplace_back(o.state.pos[0], o.state.pos[2]);
        else if (o.hasSpawnPos)
            others.emplace_back(o.spawnPos[0], o.spawnPos[1]);
    }
    return options_.arena.pickSpawn(others, p.lastSpawn);
}

void MatchServer::resetPickups() {
    const size_t n = options_.hasArena ? options_.arena.pickups.size() : 0;
    pickupBackAtMs_.assign(n, 0);
    pickupTakenBy_.assign(n, kNoPlayer);
    if (n == 0)
        return;
    PickupState all;
    all.index = kAllPickups;
    broadcast(encode(all), Channel::Reliable);
}

void MatchServer::onPickupClaim(ServerPlayer& p, const std::vector<uint8_t>& data) {
    PickupClaim c;
    if (!decode(data, c) || phase_ != MatchPhase::Playing || c.index >= pickupBackAtMs_.size())
        return;
    if (!p.alive) {
        log("pickup " + std::to_string(c.index) + " claim refused: player " + std::to_string(p.id) + " is dead");
        return;
    }
    const ArenaPickup& def = options_.arena.pickups[c.index];
    if (pickupBackAtMs_[c.index] != 0) {
        log("pickup " + std::to_string(c.index) + " claim refused: already taken");
        return; // the claimer took it in its own world already: nothing to undo, both copies are gone
    }
    // Right after a (re)spawn the last published position is the old life's (or the level start's) until the game has
    // moved the player to the spawn point: the spawn point stands in for it, so a claim from either is in reach.
    const bool spawnFresh = p.hasSpawnPos && (p.spawnPending || now_ - p.spawnedAtMs < kSpawnTrustMs);
    if (options_.pickupClaimRange > 0 && (p.hasState || spawnFresh)) {
        const float r2 = options_.pickupClaimRange * options_.pickupClaimRange;
        bool inReach = false;
        if (spawnFresh) {
            const float sx = p.spawnPos[0] - def.pos[0], sz = p.spawnPos[1] - def.pos[2];
            inReach = sx * sx + sz * sz <= r2;
        }
        const float dx = p.hasState ? p.state.pos[0] - def.pos[0] : 0.0f;
        const float dz = p.hasState ? p.state.pos[2] - def.pos[2] : 0.0f;
        inReach = inReach || !p.hasState || dx * dx + dz * dz <= r2;
        // The newest published position may be one the speed check refused (the player was just moved far by the game
        // and the resync has not come yet): it is the freshest there is.
        if (!inReach && p.rejectStreak > 0) {
            const float rx = p.lastRejectPos[0] - def.pos[0], rz = p.lastRejectPos[2] - def.pos[2];
            inReach = rx * rx + rz * rz <= r2;
        }
        if (!inReach) {
            log("pickup " + std::to_string(c.index) + " claim refused: player " + std::to_string(p.id) +
                " is too far (distance " + std::to_string(static_cast<int>(std::sqrt(dx * dx + dz * dz))) +
                ", player at " + std::to_string(static_cast<int>(p.state.pos[0])) + " " +
                std::to_string(static_cast<int>(p.state.pos[2])) + ", pickup at " +
                std::to_string(static_cast<int>(def.pos[0])) + " " + std::to_string(static_cast<int>(def.pos[2])) +
                ")");
            return;
        }
    }
    pickupBackAtMs_[c.index] = now_ + uint64_t(def.respawnSec) * 1000u;
    pickupTakenBy_[c.index] = p.id;
    PickupState ps;
    ps.index = c.index;
    ps.taken = true;
    ps.player = p.id;
    broadcast(encode(ps), Channel::Reliable);
    log("pickup " + std::to_string(c.index) + " (" + def.label + ") taken by player " + std::to_string(p.id) +
        ", back in " + std::to_string(def.respawnSec) + " s");
}

} // namespace drakan::net
