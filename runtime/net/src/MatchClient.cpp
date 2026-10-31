#include "net/MatchClient.h"

#include <cmath>

namespace drakan::net {

namespace {

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float lerpAngle(float a, float b, float t) {
    constexpr float kPi = 3.14159265358979f;
    constexpr float kTwoPi = 2.0f * kPi;
    float d = std::fmod(b - a, kTwoPi);
    if (d > kPi)
        d -= kTwoPi;
    else if (d < -kPi)
        d += kTwoPi;
    return a + d * t;
}

} // namespace

MatchClient::MatchClient(Transport& transport, ClientOptions options)
    : transport_(transport), options_(std::move(options)) {}

const RemotePlayer* MatchClient::self() const {
    auto it = roster_.find(self_);
    return it == roster_.end() ? nullptr : &it->second;
}

std::vector<ScoreRow> MatchClient::scoreboard() const {
    std::vector<ScoreRow> rows;
    for (const auto& kv : roster_) {
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

MatchOutcome MatchClient::outcome() const {
    EndReason reason = EndReason::Ended;
    const std::vector<ScoreRow> rows = scoreboard();
    if (config_.fragLimit > 0) {
        bool hit = false;
        if (config_.mode == MatchMode::TeamDeathmatch)
            hit = teamKills_[0] >= config_.fragLimit || teamKills_[1] >= config_.fragLimit;
        else
            for (const ScoreRow& r : rows)
                hit = hit || r.kills >= config_.fragLimit;
        if (hit)
            reason = EndReason::FragLimit;
    }
    if (reason == EndReason::Ended && config_.timeLimitSec > 0)
        reason = EndReason::TimeLimit;
    return decideOutcome(rows, config_.mode, teamKills_, reason);
}

bool MatchClient::connect(const std::string& host, uint16_t port, uint64_t nowMs) {
    if (state_ != ClientState::Disconnected && state_ != ClientState::Rejected)
        return false;
    peer_ = transport_.connect(host, port);
    if (peer_ == kInvalidPeer)
        return false;
    state_ = ClientState::Connecting;
    connectStartMs_ = now_ = nowMs;
    self_ = kNoPlayer;
    roster_.clear();
    buffers_.clear();
    haveClock_ = false;
    sendSequence_ = 0;
    return true;
}

void MatchClient::disconnect() {
    if (peer_ != kInvalidPeer)
        transport_.disconnect(peer_);
    peer_ = kInvalidPeer;
    if (state_ != ClientState::Rejected)
        state_ = ClientState::Disconnected;
}

void MatchClient::tick(uint64_t nowMs) {
    now_ = nowMs;
    TransportEvent ev;
    while (transport_.poll(ev))
        handleMessage(ev);
    if ((state_ == ClientState::Connecting || state_ == ClientState::Handshaking) &&
        now_ - connectStartMs_ > options_.connectTimeoutMs) {
        disconnect();
        events_.push_back(DisconnectedEvent{false});
    }
}

bool MatchClient::sendReliable(const std::vector<uint8_t>& bytes) {
    return state_ == ClientState::Connected && transport_.send(peer_, Channel::Reliable, bytes);
}

bool MatchClient::sendState(PlayerState s) {
    if (state_ != ClientState::Connected)
        return false;
    s.player = self_;
    s.sequence = ++sendSequence_;
    if (s.tick == 0)
        s.tick = ++sendTick_; // a caller-supplied tick (the game's world tick) is kept
    sent_[s.sequence & 63] = {s.sequence, now_};
    return transport_.send(peer_, Channel::Unreliable, encode(s));
}

bool MatchClient::sendHit(PlayerId victim, uint8_t weapon, uint16_t damage) {
    Hit h;
    h.victim = victim;
    h.weapon = weapon;
    h.damage = damage;
    return sendReliable(encode(h));
}

bool MatchClient::sendReady(bool ready) {
    Ready r;
    r.ready = ready;
    return sendReliable(encode(r));
}

bool MatchClient::sendCharacter(uint16_t character) {
    SetCharacter c;
    c.character = character;
    return sendReliable(encode(c));
}

bool MatchClient::sendKick(PlayerId player) {
    Kick k;
    k.player = player;
    return sendReliable(encode(k));
}

bool MatchClient::sendChat(const std::string& text) {
    Chat c;
    c.text = text.substr(0, kMaxChatLength);
    return sendReliable(encode(c));
}

bool MatchClient::sendLoadDone() {
    return sendReliable(encode(LoadDone{}));
}

bool MatchClient::sendCoopCreatures(const CoopCreatures& c) {
    if (state_ != ClientState::Connected)
        return false;
    return transport_.send(peer_, Channel::Unreliable, encode(c));
}

bool MatchClient::sendCoopEvent(const CoopEvent& e) {
    return sendReliable(encode(e));
}

bool MatchClient::sendLevelChange(uint32_t destHash, const float exit[3]) {
    LevelChange lc;
    lc.destHash = destHash;
    for (int i = 0; i < 3; ++i)
        lc.exit[i] = exit[i];
    return sendReliable(encode(lc));
}

bool MatchClient::sendPickupClaim(uint16_t index) {
    PickupClaim c;
    c.index = index;
    return sendReliable(encode(c));
}

bool MatchClient::pollEvent(ClientEvent& out) {
    if (events_.empty())
        return false;
    out = std::move(events_.front());
    events_.pop_front();
    return true;
}

void MatchClient::applyScore(const Score& s) {
    teamKills_[0] = s.teamKills[0];
    teamKills_[1] = s.teamKills[1];
    auto it = roster_.find(s.player);
    if (it != roster_.end()) {
        it->second.kills = s.kills;
        it->second.deaths = s.deaths;
    }
}

void MatchClient::handleMessage(const TransportEvent& ev) {
    if (ev.peer != peer_)
        return;
    if (ev.type == TransportEventType::Connected) {
        Hello h;
        h.profileHash = options_.profileHash;
        h.name = options_.name;
        h.password = options_.password;
        transport_.send(peer_, Channel::Reliable, encode(h));
        state_ = ClientState::Handshaking;
        return;
    }
    if (ev.type == TransportEventType::Disconnected) {
        const bool wasRejected = state_ == ClientState::Rejected;
        peer_ = kInvalidPeer;
        if (!wasRejected)
            state_ = ClientState::Disconnected;
        events_.push_back(DisconnectedEvent{wasRejected});
        return;
    }

    MsgType type;
    if (!peekType(ev.data.data(), ev.data.size(), type))
        return;

    if (state_ == ClientState::Handshaking) {
        if (type == MsgType::Welcome) {
            Welcome w;
            if (!decode(ev.data, w))
                return;
            self_ = w.player;
            coopAuthority_ = kNoPlayer;
            phase_ = w.phase;
            config_ = w.config;
            tickRateHz_ = w.tickRateHz;
            state_ = ClientState::Connected;
        } else if (type == MsgType::Reject) {
            Reject r;
            if (!decode(ev.data, r))
                return;
            reject_ = r;
            state_ = ClientState::Rejected;
            transport_.disconnect(peer_);
            peer_ = kInvalidPeer;
        }
        return;
    }
    if (state_ != ClientState::Connected)
        return;

    switch (type) {
        case MsgType::Status: {
            Status st;
            if (!decode(ev.data, st))
                break;
            countdownKind_ = st.countdown;
            countdownMs_ = st.countdownMs;
            countdownAtMs_ = now_;
            for (const PlayerPing& pp : st.pings) {
                auto it = roster_.find(pp.player);
                if (it != roster_.end()) {
                    it->second.pingMs = pp.pingMs;
                    it->second.character = pp.character;
                }
            }
            StatusEcho echo;
            echo.token = st.token;
            transport_.send(peer_, Channel::Unreliable, encode(echo));
            break;
        }
        case MsgType::Reject: { // the host removed us
            Reject r;
            if (decode(ev.data, r)) {
                reject_ = r;
                state_ = ClientState::Rejected;
                transport_.disconnect(peer_);
                peer_ = kInvalidPeer;
            }
            break;
        }
        case MsgType::MatchConfig: {
            MatchConfigMsg m;
            if (decode(ev.data, m)) {
                config_ = m.config;
                events_.push_back(m);
            }
            break;
        }
        case MsgType::PhaseChange: {
            PhaseChange p;
            if (decode(ev.data, p)) {
                phase_ = p.phase;
                events_.push_back(p);
            }
            break;
        }
        case MsgType::PlayerJoin: {
            PlayerJoin j;
            if (decode(ev.data, j)) {
                RemotePlayer& r = roster_[j.player];
                r.id = j.player;
                r.name = j.name;
                r.team = j.team;
                events_.push_back(j);
            }
            break;
        }
        case MsgType::Ready: {
            Ready r;
            if (decode(ev.data, r)) {
                auto it = roster_.find(r.player);
                if (it != roster_.end())
                    it->second.ready = r.ready;
                events_.push_back(r);
            }
            break;
        }
        case MsgType::PlayerLeave: {
            PlayerLeave l;
            if (decode(ev.data, l)) {
                roster_.erase(l.player);
                buffers_.erase(l.player);
                events_.push_back(l);
            }
            break;
        }
        case MsgType::Hit: {
            Hit h;
            if (decode(ev.data, h)) {
                auto it = roster_.find(h.victim);
                if (it != roster_.end())
                    it->second.health = h.healthAfter;
                events_.push_back(h);
            }
            break;
        }
        case MsgType::Death: {
            Death d;
            if (decode(ev.data, d)) {
                auto it = roster_.find(d.victim);
                if (it != roster_.end()) {
                    it->second.alive = false;
                    it->second.health = 0;
                }
                // The buffer stays: a dead player keeps publishing its death animation (until the Respawn).
                events_.push_back(d);
            }
            break;
        }
        case MsgType::Respawn: {
            Respawn r;
            if (decode(ev.data, r)) {
                auto it = roster_.find(r.player);
                if (it != roster_.end()) {
                    it->second.alive = true;
                    it->second.health = r.health;
                }
                buffers_.erase(r.player); // the teleport must not be interpolated
                events_.push_back(r);
            }
            break;
        }
        case MsgType::CoopAuthority: {
            CoopAuthority a;
            if (decode(ev.data, a)) {
                coopAuthority_ = a.player;
                events_.push_back(a);
            }
            break;
        }
        case MsgType::CoopCreatures: {
            CoopCreatures c;
            if (decode(ev.data, c))
                events_.push_back(std::move(c));
            break;
        }
        case MsgType::CoopEvent: {
            CoopEvent e;
            if (decode(ev.data, e))
                events_.push_back(e);
            break;
        }
        case MsgType::LevelChange: {
            LevelChange lc;
            if (decode(ev.data, lc))
                events_.push_back(lc);
            break;
        }
        case MsgType::PickupState: {
            PickupState ps;
            if (decode(ev.data, ps))
                events_.push_back(ps);
            break;
        }
        case MsgType::Chat: {
            Chat c;
            if (decode(ev.data, c))
                events_.push_back(c);
            break;
        }
        case MsgType::Score: {
            Score s;
            if (decode(ev.data, s)) {
                applyScore(s);
                events_.push_back(s);
            }
            break;
        }
        case MsgType::Snapshot: {
            Snapshot s;
            if (!decode(ev.data, s))
                break;
            const int64_t offset = static_cast<int64_t>(s.serverTimeMs) - static_cast<int64_t>(now_);
            if (!haveClock_) {
                clockOffset_ = offset;
                haveClock_ = true;
            } else {
                // Snapshots only arrive late, never early: follow the smallest offset, ease towards bigger ones.
                clockOffset_ = offset < clockOffset_ ? offset : clockOffset_ + (offset - clockOffset_) / 32;
            }
            ++snapshotCount_;
            for (const PlayerState& ps : s.players) {
                if (ps.player == self_) {
                    const SentMark& m = sent_[ps.sequence & 63];
                    if (m.sequence == ps.sequence && now_ >= m.atMs) {
                        const double rtt = static_cast<double>(now_ - m.atMs);
                        rttMs_ = rttMs_ < 0 ? rtt : rttMs_ + (rtt - rttMs_) * 0.1;
                    }
                    continue;
                }
                if (!roster_.count(ps.player))
                    continue;
                auto& buf = buffers_[ps.player];
                if (!buf.empty() && s.serverTimeMs <= buf.back().serverTimeMs)
                    continue; // out of order (unreliable channel)
                buf.push_back({s.serverTimeMs, ps});
                while (buf.size() > options_.maxBufferedStates)
                    buf.pop_front();
            }
            break;
        }
        default:
            break;
    }
}

CountdownKind MatchClient::countdown(uint64_t nowMs, uint32_t& remainingMs) const {
    remainingMs = 0;
    if (countdownKind_ == CountdownKind::None || nowMs < countdownAtMs_ || nowMs - countdownAtMs_ > 2000)
        return CountdownKind::None;
    const uint64_t el = nowMs - countdownAtMs_;
    remainingMs = el >= countdownMs_ ? 0 : static_cast<uint32_t>(countdownMs_ - el);
    return countdownKind_;
}

uint32_t MatchClient::serverTimeAt(uint64_t localNowMs) const {
    return static_cast<uint32_t>(static_cast<int64_t>(localNowMs) + clockOffset_);
}

size_t MatchClient::bufferedStates(PlayerId id) const {
    auto it = buffers_.find(id);
    return it == buffers_.end() ? 0 : it->second.size();
}

bool MatchClient::newestSample(PlayerId id, PlayerState& out, uint32_t& serverTimeMs) const {
    auto it = buffers_.find(id);
    if (it == buffers_.end() || it->second.empty())
        return false;
    out = it->second.back().state;
    serverTimeMs = it->second.back().serverTimeMs;
    return true;
}

bool MatchClient::remoteState(PlayerId id, uint64_t localNowMs, PlayerState& out) const {
    auto it = buffers_.find(id);
    if (it == buffers_.end() || it->second.empty() || !haveClock_)
        return false;
    const auto& buf = it->second;
    const int64_t renderTime = static_cast<int64_t>(serverTimeAt(localNowMs)) - options_.interpolationDelayMs;
    const auto toTime = [](const Sample& s) { return static_cast<int64_t>(s.serverTimeMs); };

    if (renderTime <= toTime(buf.front())) {
        out = buf.front().state;
        return true;
    }
    if (renderTime >= toTime(buf.back())) {
        out = buf.back().state; // no extrapolation: hold the newest state
        return true;
    }
    for (size_t i = 1; i < buf.size(); ++i) {
        if (renderTime > toTime(buf[i]))
            continue;
        const Sample& a = buf[i - 1];
        const Sample& b = buf[i];
        const float span = static_cast<float>(toTime(b) - toTime(a));
        const float t = span > 0 ? static_cast<float>(renderTime - toTime(a)) / span : 1.0f;
        out = t < 0.5f ? a.state : b.state; // discrete fields: animation id, weapons, mounted
        for (int k = 0; k < 3; ++k) {
            out.pos[k] = lerp(a.state.pos[k], b.state.pos[k], t);
            out.vel[k] = lerp(a.state.vel[k], b.state.vel[k], t);
        }
        out.yaw = lerpAngle(a.state.yaw, b.state.yaw, t);
        out.animTime = a.state.animId == b.state.animId ? lerp(a.state.animTime, b.state.animTime, t) : out.animTime;
        out.upperAnimTime = a.state.upperAnimId == b.state.upperAnimId
                                ? lerp(a.state.upperAnimTime, b.state.upperAnimTime, t)
                                : out.upperAnimTime;
        return true;
    }
    out = buf.back().state;
    return true;
}

} // namespace drakan::net
