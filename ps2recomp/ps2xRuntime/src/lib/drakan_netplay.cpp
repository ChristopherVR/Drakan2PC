// Drakan: networked ghosts (drakan_netplay.h).
#include "drakan_netplay.h"
#include "drakan_ghost.h"
#include "drakan_lifecycle.h"
#include "drakan_region.h"
#include "net/MatchClient.h"
#include "net/MatchServer.h"
#include "net/UdpTransport.h"
#include "ps2_runtime.h"
#include "world/Pose.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace drakan_netplay {
namespace {

using Clock = std::chrono::steady_clock;
const Clock::time_point g_epoch = Clock::now();

uint64_t nowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g_epoch).count());
}
double nowSec() {
    return std::chrono::duration<double>(Clock::now() - g_epoch).count();
}

uint32_t fnv32(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h ? h : 1u; // 0 means "loading / unknown" on the wire
}

uint64_t fnv64(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h ? h : 1ull;
}

class RdramMemory final : public world::IGuestMemory {
public:
    explicit RdramMemory(const uint8_t* ram) : ram_(ram) {}
    uint8_t read8(uint32_t a) const override {
        a &= 0x1ffffffu;
        return a < PS2_RAM_SIZE ? ram_[a] : 0;
    }
    uint32_t read32(uint32_t a) const override {
        a &= 0x1ffffffu;
        if (a + 4 > PS2_RAM_SIZE)
            return 0;
        uint32_t v;
        std::memcpy(&v, ram_ + a, 4);
        return v;
    }

private:
    const uint8_t* ram_;
};

constexpr uint32_t kSendIntervalMs = 50; // 20 Hz
constexpr uint64_t kStaleLocalMs = 1000; // the EE thread stopped publishing (loading): tell the others "level 0"

struct RemoteIn {
    drakan::net::PlayerState state;
    std::string name;
    uint64_t serial = 0;         // bumped whenever the interpolated state changed
    uint32_t sampleServerMs = 0; // server time carried by `state`
    uint32_t serverNowMs = 0;    // our estimate of the server clock when this entry was last refreshed ...
    uint64_t wallMs = 0;         // ... at this local time
};

struct Shared {
    std::mutex m;
    // EE -> net
    drakan::net::PlayerState local;
    uint64_t localAtMs = 0;
    bool haveLocal = false;
    // net -> EE
    std::map<drakan::net::PlayerId, RemoteIn> remotes;
    bool playing = false;
};

Shared g_s;
std::atomic<bool> g_enabled{false};
std::atomic<bool> g_stop{false};
bool g_trace = false;                 // DRAKAN_NET_TRACE=1: one line per ghost per EE tick (harness: per-frame deltas)
std::atomic<uint32_t> g_levelHash{0}; // written by the lifecycle listeners (EE thread)
std::atomic<uint32_t> g_ghostsVisible{0};
std::thread g_thread;
bool g_host = false;
uint16_t g_hostPort = 0;
std::string g_connectHost;
uint16_t g_connectPort = 0;
std::string g_name = "Rynn";
uint64_t g_profileHash = 0;
std::mutex g_levelM;
std::string g_levelName; // last loaded level stem, for the host's MatchConfig

void netLog(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "[netplay %.3f] %s\n", nowSec(), buf);
}

// ------------------------------------------------------------------------------------------------ network thread

void netThread() {
    using namespace drakan::net;
    std::unique_ptr<UdpTransport> serverT;
    std::unique_ptr<MatchServer> server;
    if (g_host) {
        serverT = std::make_unique<UdpTransport>();
        if (!serverT->available() || !serverT->listen(g_hostPort)) {
            netLog("host: cannot listen on UDP port %u; networking off", g_hostPort);
            return;
        }
        std::string level;
        {
            std::lock_guard<std::mutex> l(g_levelM);
            level = g_levelName;
        }
        ServerOptions so;
        so.config.mode = MatchMode::Deathmatch;
        so.config.level = (level.empty() ? std::string("any") : level).substr(0, kMaxLevelLength);
        so.config.fragLimit = 0;
        so.config.timeLimitSec = 0;
        so.config.maxPlayers = 8;
        so.profileHash = g_profileHash;
        so.autoStartPlayers = 1;
        so.lobbyCountdownMs = 200;
        so.loadTimeoutMs = 3000;
        server = std::make_unique<MatchServer>(*serverT, so);
        server->onLog = [](const std::string& s) { netLog("server: %s", s.c_str()); };
        netLog("host: listening on UDP %u (profile hash %016llx)", serverT->localPort(),
               static_cast<unsigned long long>(g_profileHash));
    }

    UdpTransport clientT;
    if (!clientT.available()) {
        netLog("sockets unavailable; networking off");
        return;
    }
    ClientOptions co;
    co.name = g_name;
    co.profileHash = g_profileHash;
    co.interpolationDelayMs = 100;
    MatchClient client(clientT, co);
    const std::string host = g_host ? std::string("127.0.0.1") : g_connectHost;
    const uint16_t port = g_host ? g_hostPort : g_connectPort;

    uint64_t lastSend = 0, lastConnectTry = 0, lastStats = 0;
    uint64_t statSnapshots = 0, statSent = 0;
    MatchPhase lastPhase = MatchPhase::Lobby;
    bool loadDoneSent = false;
    bool wasConnected = false;
    bool tried = false;
    bool rejectLogged = false;

    while (!g_stop.load()) {
        const uint64_t now = nowMs();
        if (server)
            server->tick(now);
        client.tick(now);

        if (!client.connected() && !client.rejected() &&
            (!tried || (client.state() == ClientState::Disconnected && now - lastConnectTry > 2000))) {
            tried = true;
            lastConnectTry = now;
            netLog("connecting to %s:%u as '%s'", host.c_str(), port, g_name.c_str());
            if (!client.connect(host, port, now))
                netLog("cannot use address %s:%u", host.c_str(), port);
        }
        if (client.rejected() && !rejectLogged) {
            rejectLogged = true;
            netLog("REJECTED by the server: reason %d, '%s' (server protocol %u, ours %u)",
                   static_cast<int>(client.rejectInfo().reason), client.rejectInfo().message.c_str(),
                   client.rejectInfo().serverVersion, kProtocolVersion);
        }

        ClientEvent ev;
        while (client.pollEvent(ev)) {
            if (const auto* d = std::get_if<DisconnectedEvent>(&ev)) {
                netLog("disconnected%s", d->wasRejected ? " (rejected)" : "");
                loadDoneSent = false;
                tried = false;
            } else if (const auto* j = std::get_if<PlayerJoin>(&ev)) {
                netLog("player %u '%s' joined", j->player, j->name.c_str());
            } else if (const auto* l = std::get_if<PlayerLeave>(&ev)) {
                netLog("player %u left", l->player);
            }
        }

        const bool connected = client.connected();
        if (connected != wasConnected) {
            netLog("%s (player id %u, tick rate %u Hz)", connected ? "connected" : "connection lost", client.playerId(),
                   client.tickRateHz());
            wasConnected = connected;
        }
        if (connected) {
            if (client.phase() != lastPhase) {
                netLog("phase -> %d (match level '%s')", static_cast<int>(client.phase()),
                       client.config().level.c_str());
                lastPhase = client.phase();
                loadDoneSent = false;
            }
            if (client.phase() == MatchPhase::Loading && !loadDoneSent)
                loadDoneSent = client.sendLoadDone();
        }

        // Outgoing: the pose the EE thread last published, at 20 Hz.
        if (connected && now - lastSend >= kSendIntervalMs) {
            lastSend = now;
            PlayerState s;
            bool have;
            uint64_t at;
            {
                std::lock_guard<std::mutex> l(g_s.m);
                s = g_s.local;
                have = g_s.haveLocal;
                at = g_s.localAtMs;
            }
            if (!have || now - at > kStaleLocalMs)
                s.levelHash = 0;
            if (client.sendState(s))
                ++statSent;
        }

        // Incoming: interpolated remote states for the EE thread.
        {
            std::lock_guard<std::mutex> l(g_s.m);
            g_s.playing = connected && client.phase() == MatchPhase::Playing;
            for (auto it = g_s.remotes.begin(); it != g_s.remotes.end();) {
                if (!connected || !client.roster().count(it->first)) {
                    it = g_s.remotes.erase(it);
                } else
                    ++it;
            }
            if (connected)
                for (const auto& kv : client.roster()) {
                    if (kv.first == client.playerId())
                        continue;
                    PlayerState ps;
                    uint32_t sampleMs;
                    if (!client.newestSample(kv.first, ps, sampleMs))
                        continue;
                    RemoteIn& r = g_s.remotes[kv.first];
                    r.name = kv.second.name;
                    r.state = ps;
                    r.sampleServerMs = sampleMs;
                    r.serverNowMs = client.serverTimeAt(now);
                    r.wallMs = now;
                }
        }

        if (now - lastStats >= 5000) {
            const uint64_t dt = lastStats ? now - lastStats : 5000;
            const uint64_t snaps = client.snapshotCount();
            if (connected)
                netLog("stats: rtt %.1f ms, snapshots %.1f/s, states sent %.1f/s, remotes %zu, ghosts shown %u",
                       client.rttMs(), static_cast<double>(snaps - statSnapshots) * 1000.0 / static_cast<double>(dt),
                       static_cast<double>(statSent) * 1000.0 / static_cast<double>(dt), client.roster().size() - 1,
                       g_ghostsVisible.load());
            statSnapshots = snaps;
            statSent = 0;
            lastStats = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    client.disconnect();
    for (int i = 0; i < 5; ++i) { // let the Disconnect datagram out and the server see it
        if (server)
            server->tick(nowMs());
        client.tick(nowMs());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void stopThread() {
    g_stop.store(true);
    if (g_thread.joinable())
        g_thread.join();
}

// ------------------------------------------------------------------------------------------------ EE thread

world::Globals g_globals;
world::PoseReadState g_poseState;
uint32_t g_tick = 0;
double g_spawnNotBefore = 0; // EE thread
constexpr double kSettleSec = 1.5;
constexpr double kDebounceSec = 0.5;
constexpr double kTickHz = 60.0;  // PoseBuffer time unit: sender ticks / kTickHz
constexpr double kDelaySec = 0.1; // replay this far behind the newest sample (two snapshot intervals)

world::PoseBufferConfig bufferConfig() {
    world::PoseBufferConfig c;
    c.delaySec = 0; // unused: the replay position is chosen by tick (sampleAt)
    return c;
}

struct RemoteView {
    world::PoseBuffer buffer{bufferConfig()};
    bool haveSample = false, havePlay = false;
    uint32_t newestTick = 0, prevSampleMs = 0;
    double playTick = 0;
    double tickRate = 60.0; // sender world ticks per second, smoothed
    int ghostId = 0;
    bool wasVisible = false;
    double unwantedSince = -1;
    double retryAt = 0; // after a ghost that vanished by itself
    int failures = 0;
};
std::map<drakan::net::PlayerId, RemoteView> g_views; // EE thread

// A load is about to block the EE thread (no more publishes): tell the network thread at once that we are loading.
void publishLoading() {
    std::lock_guard<std::mutex> l(g_s.m);
    if (g_s.haveLocal) {
        g_s.local.levelHash = 0;
        g_s.localAtMs = nowMs();
    }
}

// The level / save is being replaced: drakan_ghost destroys the actors itself, we only drop our ids.
void forgetGhosts() {
    for (auto& kv : g_views) {
        kv.second.ghostId = 0;
        kv.second.wasVisible = false;
        kv.second.buffer.clear();
        kv.second.haveSample = false;
        kv.second.havePlay = false;
    }
    g_ghostsVisible.store(0);
}

bool ghostExists(int id) {
    bool found = false;
    drakan_ghost::forEach([&](const drakan_ghost::Info& i) {
        if (i.id == id)
            found = true;
    });
    return found;
}

void hide(drakan::net::PlayerId id, RemoteView& v, const char* why) {
    if (v.ghostId) {
        drakan_ghost::destroy(v.ghostId);
        netLog("ghost of player %u removed (%s)", id, why);
        v.ghostId = 0;
    }
    v.buffer.clear();
    v.haveSample = false;
    v.havePlay = false;
    v.wasVisible = false;
    v.failures = 0;
    v.retryAt = 0;
}

} // namespace

bool enabled() {
    return g_enabled.load();
}

void install(PS2Runtime& rt) {
    (void)rt;
    const char* host = std::getenv("DRAKAN_NET_HOST");
    const char* conn = std::getenv("DRAKAN_NET_CONNECT");
    const bool wantHost = host && *host;
    const bool wantConn = conn && *conn;
    if (!wantHost && !wantConn)
        return;
#if !defined(DRAKAN_NET_HAVE_UDP)
    std::fprintf(stderr, "[netplay] disabled: this build has no UDP transport\n");
#else
    if (wantHost && wantConn) {
        std::fprintf(stderr, "[netplay] set only one of DRAKAN_NET_HOST / DRAKAN_NET_CONNECT\n");
        return;
    }
    if (!drakan_ghost::enabled()) {
        std::fprintf(stderr, "[netplay] disabled: DRAKAN_GHOST=1 is required (ghosts are drakan_ghost actors)\n");
        return;
    }
    if (!drakan_lifecycle::installed()) {
        std::fprintf(stderr, "[netplay] disabled: lifecycle hooks are not installed (unsupported disc profile)\n");
        return;
    }
    world::Globals g;
    g.contextPtr = drakan_region::guestAddress(g.contextPtr);
    g.hpKey = drakan_region::guestAddress(g.hpKey);
    g.playerStats = drakan_region::guestAddress(g.playerStats);
    g.handleVtable = drakan_region::guestAddress(g.handleVtable);
    if (!g.contextPtr || !g.hpKey || !g.playerStats || !g.handleVtable) {
        std::fprintf(stderr, "[netplay] disabled: no address translation for this disc profile\n");
        return;
    }
    g_globals = g;
    if (wantHost) {
        const long p = std::strtol(host, nullptr, 10);
        if (p < 1 || p > 65535) {
            std::fprintf(stderr, "[netplay] bad DRAKAN_NET_HOST port '%s'\n", host);
            return;
        }
        g_host = true;
        g_hostPort = static_cast<uint16_t>(p);
    } else {
        const std::string c = conn;
        const size_t colon = c.rfind(':');
        const long p = colon == std::string::npos ? 0 : std::strtol(c.c_str() + colon + 1, nullptr, 10);
        if (colon == std::string::npos || colon == 0 || p < 1 || p > 65535) {
            std::fprintf(stderr, "[netplay] bad DRAKAN_NET_CONNECT '%s' (want host:port)\n", conn);
            return;
        }
        g_connectHost = c.substr(0, colon);
        g_connectPort = static_cast<uint16_t>(p);
    }
    if (const char* n = std::getenv("DRAKAN_NET_NAME"); n && *n)
        g_name = std::string(n).substr(0, drakan::net::kMaxNameLength);
    const char* tr = std::getenv("DRAKAN_NET_TRACE");
    g_trace = tr && tr[0] && tr[0] != '0';
    const auto& prof = drakan_region::build();
    g_profileHash = fnv64(std::string(prof.serial ? prof.serial : "") + "|" + (prof.saveId ? prof.saveId : ""));

    using drakan_lifecycle::Kind;
    drakan_lifecycle::subscribe(Kind::BeforeLevelLoad, [](const drakan_lifecycle::Event&) {
        g_levelHash.store(0); // still loading: peers hide their ghost of us
        publishLoading();
        forgetGhosts();
    });
    drakan_lifecycle::subscribe(Kind::BeforeSaveRestore, [](const drakan_lifecycle::Event&) {
        g_levelHash.store(0);
        publishLoading();
    });
    drakan_lifecycle::subscribe(Kind::AfterSaveRestore, [](const drakan_lifecycle::Event&) {
        // A restore that failed before its Level_Load leaves the old level loaded: take it back.
        std::lock_guard<std::mutex> lk(g_levelM);
        if (g_levelHash.load() == 0 && !g_levelName.empty() && g_levelName.rfind("Level 0", 0) != 0)
            g_levelHash.store(fnv32(g_levelName));
    });
    drakan_lifecycle::subscribe(Kind::BeforeSaveWrite, [](const drakan_lifecycle::Event&) { forgetGhosts(); });
    drakan_lifecycle::subscribe(Kind::AfterLevelLoad, [](const drakan_lifecycle::Event& e) {
        if (!e.ok || e.level.empty())
            return;
        {
            std::lock_guard<std::mutex> lk(g_levelM);
            g_levelName = e.level;
        }
        // "Level 0 - Intro" is the opening cut scene: Rynn is there but a second actor breaks it (a ghost spawned in it
        // made the guest jump to a null target, docs/journal/2026-10-31/netplay-spike.md), so it is not a level for
        // ghosts. Spawns also wait kSettleSec after a load (the engine is still creating its own actors).
        const bool playable = e.level.rfind("Level 0", 0) != 0;
        g_levelHash.store(playable ? fnv32(e.level) : 0);
        g_spawnNotBefore = nowSec() + kSettleSec;
        netLog("level '%s' loaded (%s, hash %08x)", e.level.c_str(), playable ? "playable" : "cut scene: no ghosts",
               playable ? fnv32(e.level) : 0);
    });

    g_enabled.store(true);
    g_thread = std::thread(netThread);
    std::atexit(stopThread);
    if (g_host)
        std::fprintf(stderr, "[netplay] enabled: host on UDP %u, name '%s'\n", g_hostPort, g_name.c_str());
    else
        std::fprintf(stderr, "[netplay] enabled: client of %s:%u, name '%s'\n", g_connectHost.c_str(), g_connectPort,
                     g_name.c_str());
#endif
}

void beforePadMgr(const uint8_t* rdram) {
    if (!g_enabled.load(std::memory_order_relaxed) || !rdram)
        return;
    const double t = nowSec();
    const uint32_t myLevel = g_levelHash.load();
    const bool transition = drakan_lifecycle::transitionActive();

    // Local pose -> published slot.
    world::PlayerPose pose;
    world::PoseStatus status = world::PoseStatus::NoContext;
    if (!transition)
        status = world::readPlayerPose(RdramMemory(rdram), g_globals, g_poseState, t, ++g_tick, pose);
    const bool posed = status == world::PoseStatus::Ok && myLevel != 0 && !transition;
    if (g_trace && posed)
        std::fprintf(stderr, "[nettrace-local %.4f] %.2f %.2f %.2f yaw %.3f lower %x@%.3f\n", t, pose.position.x,
                     pose.position.y, pose.position.z, pose.yaw, pose.lower.clip, pose.lower.time);
    {
        std::lock_guard<std::mutex> l(g_s.m);
        if (posed) {
            if (pose.weaponRight != g_s.local.weaponRight || pose.weaponLeft != g_s.local.weaponLeft)
                netLog("local weapons: right %x left %x", pose.weaponRight, pose.weaponLeft);
            world::toPlayerState(pose, g_s.local);
            g_s.local.levelHash = myLevel;
            g_s.haveLocal = true;
            g_s.localAtMs = nowMs();
        } else if (g_s.haveLocal) {
            g_s.local.levelHash = 0;
            g_s.localAtMs = nowMs();
        }
    }

    // Remote poses -> ghosts.
    std::map<drakan::net::PlayerId, RemoteIn> remotes;
    bool playing;
    {
        std::lock_guard<std::mutex> l(g_s.m);
        remotes = g_s.remotes;
        playing = g_s.playing;
    }
    for (auto it = g_views.begin(); it != g_views.end();) {
        if (!remotes.count(it->first)) {
            hide(it->first, it->second, "player left");
            it = g_views.erase(it);
        } else
            ++it;
    }
    uint32_t shown = 0;
    for (const auto& kv : remotes) {
        RemoteView& v = g_views[kv.first];
        const RemoteIn& r = kv.second;
        const bool sameLevel = r.state.levelHash != 0 && r.state.levelHash == myLevel;
        const bool want = posed && playing && sameLevel && !r.state.mounted;
        if (!want) {
            // Our own load / save is immediate (the guest is about to free the actors). Anything else must persist
            // kDebounceSec: the pose read fails for a frame or two when a menu opens, which must not flap the ghost.
            const bool immediate = transition || myLevel == 0;
            if (v.unwantedSince < 0)
                v.unwantedSince = t;
            if ((v.ghostId || v.wasVisible) && (immediate || t - v.unwantedSince >= kDebounceSec))
                hide(kv.first, v,
                     immediate    ? "local level change"
                     : !posed     ? "local pose unavailable"
                     : !sameLevel ? "player in another level"
                     : !playing   ? "match not playing"
                                  : "player mounted");
            continue;
        }
        v.unwantedSince = -1;
        // Replay on the sender's world-tick clock (ghost-spike.md "Ghost smoothness"): the game advances in bursts, so
        // wall-clock replay turns them into stalls and jumps. Samples are stamped tick / 60; the playback position
        // advances once per OUR PadMgr boundary and is steered to stay `delay` ticks behind the newest sample.
        if (!v.haveSample || r.state.tick > v.newestTick) {
            if (v.haveSample && r.sampleServerMs > v.prevSampleMs) {
                const double inst = (r.state.tick - v.newestTick) * 1000.0 / (r.sampleServerMs - v.prevSampleMs);
                if (inst > 5.0 && inst < 500.0)
                    v.tickRate = v.tickRate * 0.8 + inst * 0.2;
            }
            v.haveSample = true;
            v.newestTick = r.state.tick;
            v.prevSampleMs = r.sampleServerMs;
            v.buffer.push(world::fromPlayerState(r.state, r.state.tick / kTickHz));
        }
        const double delayTicks = std::max(3.0, kDelaySec * v.tickRate);
        if (!v.havePlay) {
            v.playTick = v.newestTick - delayTicks;
            v.havePlay = true;
        } else {
            const double lag = v.newestTick - v.playTick - delayTicks;
            v.playTick += 1.0 + std::clamp(lag * 0.05, -0.5, 3.0);
        }
        world::PlayerPose p;
        if (!v.buffer.sampleAt(v.playTick / kTickHz, p))
            continue;
        v.wasVisible = true;
        if (v.ghostId && !ghostExists(v.ghostId)) {
            // drakan_ghost drops a ghost whose spawn failed (e.g. a position outside the loaded level): back off, or
            // this respawns every frame.
            v.failures = std::min(v.failures + 1, 5);
            v.retryAt = t + std::min(2.0 * v.failures, 10.0);
            netLog("ghost of player %u vanished (failure %d); retry in %.0f s", kv.first, v.failures, v.retryAt - t);
            v.ghostId = 0;
        }
        if (!v.ghostId && t >= g_spawnNotBefore && t >= v.retryAt && drakan_ghost::ready()) {
            drakan_ghost::Transform tr;
            std::memcpy(tr.pos, &p.position, sizeof tr.pos);
            std::memcpy(tr.rot, p.rotation, sizeof tr.rot);
            v.ghostId = drakan_ghost::spawn(&tr);
            if (!v.ghostId) { // refused (engine not ready, transition): try again shortly
                v.retryAt = t + 0.5;
                continue;
            }
            netLog("ghost of player %u '%s' spawned (id %d) at %.0f %.0f %.0f", kv.first, r.name.c_str(), v.ghostId,
                   p.position.x, p.position.y, p.position.z);
        } else if (v.ghostId) {
            drakan_ghost::setPose(v.ghostId, p);
            if (g_trace)
                std::fprintf(stderr, "[nettrace %.4f] p%u %.2f %.2f %.2f yaw %.3f lower %x@%.3f upper %x@%.3f\n", t,
                             kv.first, p.position.x, p.position.y, p.position.z, p.yaw, p.lower.clip, p.lower.time,
                             p.upper.clip, p.upper.time);
        }
        ++shown;
    }
    g_ghostsVisible.store(shown);
}

} // namespace drakan_netplay
