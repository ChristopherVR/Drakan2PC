// Drakan: networked ghosts (drakan_netplay.h).
#include "drakan_game.h"
#include "drakan_netplay.h"
#include "drakan_arena.h"
#include "drakan_coop_character.h"
#include "drakan_ghost.h"
#include "drakan_levelchange.h"
#include "drakan_lifecycle.h"
#include "drakan_pvp.h"
#include "drakan_region.h"
#include "net/MatchClient.h"
#include "net/MatchServer.h"
#include "net/ServerBrowser.h"
#include "net/UdpTransport.h"
#include "ps2_runtime.h"
#include "world/Pose.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <random>
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

// ------------------------------------------------------------------------------------------------ session control

struct Cmd {
    enum Kind { Host, Join, Leave, Start, Ready, Chat, Kick } kind = Leave;
    HostConfig host;
    std::string addr, password, name, text;
    bool flag = false;
    int value = 0; // Kick: the player id
};
std::mutex g_cmdM;
std::deque<Cmd> g_cmds;
std::mutex g_stM; // g_state, g_chat
State g_state;
std::vector<ChatLine> g_chat;
std::atomic<bool> g_inSession{false};
std::string g_unavailableWhy = "not installed";
std::mutex g_modsM;
std::string g_mods; // setLoadedMods
std::string loadedMods() {
    std::lock_guard<std::mutex> l(g_modsM);
    return g_mods;
}
std::mutex g_partyM;
std::vector<CoopPartnerInfo> g_party;
bool g_partyComplete = false;
std::mutex g_lanM;
std::vector<LanServer> g_lan;
std::atomic<uint64_t> g_lanRefreshAt{0}; // nowMs() + 1 when a sweep was asked for
std::atomic<uint64_t> g_lanWantedAt{0};  // last lanServers() call: the browser keeps sweeping while the UI looks
std::atomic<bool> g_lanScanning{false};
std::mutex g_launchM;
std::string g_launchLevel, g_matchLevel; // g_matchLevel: the level the redirect stands in for (hash of the real name)
bool g_launchPending = false;
bool g_launchDone = false; // this match already started its New Game
std::atomic<bool> g_launchTaps{false};
std::atomic<bool> g_redirectActive{false};
bool g_launchDirect = false; // g_launchM: the pending / running launch loads the map in place of "Level 0 - Intro"
std::string g_redirectFrom;  // g_launchM: the level name the file redirect replaces (what the lifecycle hooks report)
constexpr const char* kIntroLevel = "Level 0 - Intro"; // New Game opens this (the movie) before the first level
std::function<void(const std::string&, const std::string&)> g_redirectFn;
std::vector<std::string> g_extraLevels;
constexpr const char* kFirstLevel = "Level 1A - Surdana"; // the level New Game opens after the intro

// ---- co-op transport state (drakan_coop talks to the peer through this session's MatchClient) ----
std::mutex g_coopM; // everything below
std::deque<drakan::net::CoopCreatures> g_coopInCreatures;
std::deque<drakan::net::CoopEvent> g_coopInEvents;
std::deque<drakan::net::CoopCreatures> g_coopOutCreatures;
std::deque<drakan::net::CoopEvent> g_coopOutEvents;
CoopLink g_coopLink;
std::atomic<bool> g_coopMode{false};
uint64_t g_sessionSerial = 0;
constexpr size_t kCoopQueueMax = 8192;
struct LevelChangeOut {
    uint32_t destHash = 0;
    float pos[3] = {};
};
std::mutex g_lcM;
std::deque<LevelChangeOut> g_lcOut; // EE thread (exit listener) -> network thread

// The follower's check of a LevelChange (netcode.md "Level follow check"): after the leader's message the follower
// fires the same exit; when the next level has finished loading its hash must equal the message's destHash.
struct FollowCheck {
    bool active = false;
    uint32_t destHash = 0, srcHash = 0;
    float exit[3] = {0, 0, 0};
    uint64_t loadsAtStart = 0; // g_levelLoads when the wait for a load began
    double startedAt = 0;
    double retryAt = 0; // > 0: a mismatch was reported, the exit list is searched at this time
    int attempt = 0;
};
std::mutex g_followM;
FollowCheck g_followIn;                // network thread -> EE thread
bool g_followNew = false;              // guarded by g_followM
std::atomic<uint64_t> g_levelLoads{0}; // playable level loads finished (written by the lifecycle listener)
FollowCheck g_follow;                  // EE thread
uint32_t g_testDestHash = 0; // test aid DRAKAN_NET_TEST_LC_DEST=<level name>: the destination the follower expects
constexpr double kFollowTimeoutSec = 45.0; // no level load after the message: the exit did not fire, fire it again
constexpr int kFollowMaxAttempts = 3;

// ---- combat (PvP) state shared between the network thread, the EE thread and the UI ----
struct ClaimOut {
    uint8_t victim = 0, weapon = 0;
    uint16_t damage = 0;
};
std::mutex g_combatM; // g_claims, g_owner, g_board
std::deque<ClaimOut> g_claims;
std::map<int, int> g_ghostToPlayer, g_playerToGhost; // EE thread writes, anyone reads
Scoreboard g_board;
double g_deadSince = -1;   // net thread: when the server declared the local player dead
double g_respawnDelay = 0; // seconds
uint64_t g_playingSinceServerMs = 0;
bool g_havePlayingSince = false;

void pushCmd(Cmd c) {
    std::lock_guard<std::mutex> l(g_cmdM);
    g_cmds.push_back(std::move(c));
}
bool popCmd(Cmd& out) {
    std::lock_guard<std::mutex> l(g_cmdM);
    if (g_cmds.empty())
        return false;
    out = std::move(g_cmds.front());
    g_cmds.pop_front();
    return true;
}

void addChat(int player, const std::string& who, const std::string& text) {
    if (player != 0)
        netLog("chat: <%s> %s", who.c_str(), text.c_str());
    std::lock_guard<std::mutex> l(g_stM);
    g_chat.push_back({player, who, text, nowSec()});
    if (g_chat.size() > 64)
        g_chat.erase(g_chat.begin());
}

// `direct`: a deathmatch map is loaded in place of the intro level itself (Multiplayer maps: no movie, no cut scene, no
// intro taps, straight into the arena). Co-op keeps the old route: New Game, intro, the first level of the campaign.
// Called with g_launchM held (or from the network thread's cleanup).
void setRedirect(const std::string& level, bool direct = false) {
    if (!g_redirectFn)
        return;
    if (level.empty() || (!direct && (level == kFirstLevel || level == drakan_game::firstLevel()))) {
        // (the chosen game's own first level: New Game already opens it, for Order of the Flame through the campaign redirect)
        g_redirectFn("", "");
        g_redirectActive.store(false);
        g_redirectFrom.clear();
    } else {
        g_redirectFrom = direct ? kIntroLevel : kFirstLevel;
        g_redirectFn(g_redirectFrom, level);
        g_redirectActive.store(true);
    }
}

// The server entered Loading (or the match was already running when we joined): ask the title to start a New Game with
// the match level standing in for the first level.
constexpr const char* kDiscLevels[] = {"Level 1A - Surdana",
                                       "Level 1B - Kragmor",
                                       "Level 1C - Catacombs",
                                       "Level 1D - City of the Damned",
                                       "Level 1E - Inquisitor's Lair",
                                       "Level 2 - Shadowmire",
                                       "Level 3A - Andrellian Isles",
                                       "Level 3B - Crystal Mine",
                                       "Level 4A - Ravenshold",
                                       "Level 4B - Northern Tundra",
                                       "Level 5 - Shiverbane",
                                       "Level 6 - Valley of the Fallen",
                                       "Level 6 - Transition to Desert",
                                       "Level 8 - Stratos"};

// The name of the level whose FNV-1a hash this is (disc levels and the mod layer's levels), "" when unknown.
std::string levelNameForHash(uint32_t hash) {
    for (const char* n : kDiscLevels)
        if (fnv32(n) == hash)
            return n;
    for (const std::string& n : g_extraLevels)
        if (fnv32(n) == hash)
            return n;
    return {};
}

bool levelInstalled(const std::string& levelOrMap) {
    std::string level = levelOrMap;
    if (const drakan::net::ArenaDef* arena = drakan_arena::findMap(levelOrMap))
        level = arena->level; // a multiplayer map: its base level has to be here
    for (const char* n : kDiscLevels)
        if (level == n)
            return true;
    for (const std::string& n : g_extraLevels)
        if (level == n)
            return true;
    return false;
}

std::string g_launchError; // guarded by g_launchM

void requestLaunch(const std::string& levelOrMap, bool direct) {
    std::lock_guard<std::mutex> l(g_launchM);
    if (g_launchDone)
        return;
    g_launchDone = true;
    std::string level = levelOrMap;
    if (const drakan::net::ArenaDef* arena = drakan_arena::findMap(levelOrMap))
        level = arena->level;
    // OotF requires identical level files on the client and the server. A map this client does not have (a mod pack
    // level that is not installed) cannot be played: say so instead of loading something else.
    if (!levelInstalled(levelOrMap)) {
        g_launchError = "Level files mismatch: '" + levelOrMap + "' is not installed here (mod pack missing?)";
        netLog("launch refused: %s", g_launchError.c_str());
        return;
    }
    g_launchError.clear();
    g_launchPending = true;
    g_launchLevel = level;
    g_matchLevel = level;
    g_launchDirect = direct;
    netLog("launch: New Game with level '%s'%s", level.c_str(), direct ? " (straight into the map, no intro)" : "");
}

void serviceLan(drakan::net::ServerBrowser& browser, uint64_t now, uint64_t& lastSweep) {
    using namespace drakan::net;
    const uint64_t asked = g_lanRefreshAt.exchange(0);
    const bool watching = g_lanWantedAt.load() != 0 && now - g_lanWantedAt.load() < 1500;
    if (asked || (watching && now - lastSweep > 2000)) {
        if (asked) {
            browser.clear();
            g_lanScanning.store(true);
        }
        browser.queryLan();
        lastSweep = now;
    }
    if (g_lanScanning.load() && now - lastSweep > 3000 && !asked)
        g_lanScanning.store(false);
    const std::vector<ServerEntry>& found = browser.poll();
    std::vector<LanServer> out;
    for (const ServerEntry& e : found) {
        if (e.ageMs > 8000)
            continue;
        LanServer s;
        s.host = e.host;
        s.port = e.port;
        s.name = e.info.name;
        s.level = e.info.level;
        s.mode = static_cast<int>(e.info.mode);
        s.players = e.info.players;
        s.maxPlayers = e.info.maxPlayers;
        s.passwordProtected = e.info.passwordProtected;
        s.inProgress = e.info.phase != MatchPhase::Lobby;
        s.pingMs = static_cast<int>(e.pingMs);
        s.mods = e.info.mods;
        out.push_back(std::move(s));
    }
    std::lock_guard<std::mutex> l(g_lanM);
    g_lan = std::move(out);
}

// ------------------------------------------------------------------------------------------------ network thread

// One session (hosting or joined) until leave() or exit. `legacy` = started from the DRAKAN_NET_* shortcut.
void runSession(const Cmd& c, drakan::net::ServerBrowser& browser, uint64_t& lastSweep) {
    using namespace drakan::net;
    const bool hosting = c.kind == Cmd::Host;
    const bool legacy = hosting ? c.host.autoStart : c.flag;
    std::string connectHost = "127.0.0.1";
    uint16_t connectPort = hosting ? c.host.port : 0;
    if (!hosting) {
        const size_t colon = c.addr.rfind(':');
        connectHost = colon == std::string::npos ? c.addr : c.addr.substr(0, colon);
        const long p = colon == std::string::npos ? kDefaultPort : std::strtol(c.addr.c_str() + colon + 1, nullptr, 10);
        connectPort = static_cast<uint16_t>(p > 0 && p < 65536 ? p : kDefaultPort);
    }
    auto publish = [&](const State& s) {
        std::lock_guard<std::mutex> l(g_stM);
        g_state = s;
    };
    State base;
    base.available = true;
    base.inSession = true;
    base.hosting = hosting;
    base.conn = Conn::Connecting;
    base.message = "Connecting to " + connectHost + ":" + std::to_string(connectPort) + " ...";
    base.maxPlayers = hosting ? c.host.maxPlayers : 8;
    publish(base);
    {
        std::lock_guard<std::mutex> l(g_launchM);
        g_launchDone = false;
        g_launchPending = false;
    }

    {
        std::lock_guard<std::mutex> lk(g_coopM);
        ++g_sessionSerial;
        g_coopLink = CoopLink();
        g_coopInCreatures.clear();
        g_coopInEvents.clear();
        g_coopOutCreatures.clear();
        g_coopOutEvents.clear();
    }
    std::unique_ptr<UdpTransport> serverT;
    std::unique_ptr<MatchServer> server;
    if (hosting) {
        serverT = std::make_unique<UdpTransport>();
        // The first free port from the requested one (a second instance on this PC takes the next, LAN discovery probes
        // ten ports).
        bool listening = false;
        for (uint16_t p = c.host.port; p < c.host.port + 10 && serverT->available() && !listening; ++p) {
            serverT = std::make_unique<UdpTransport>();
            listening = serverT->listen(p);
        }
        if (listening)
            connectPort = serverT->localPort();
        if (!listening) {
            netLog("host: cannot listen on UDP port %u", c.host.port);
            base.conn = Conn::Rejected;
            base.message = "Cannot listen on UDP port " + std::to_string(c.host.port) + " (already in use?)";
            publish(base);
            while (!g_stop.load()) { // stay until the UI leaves, so it can show the message
                Cmd x;
                if (popCmd(x) && x.kind == Cmd::Leave)
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            return;
        }
        std::string level = c.host.level;
        if (level.empty()) {
            std::lock_guard<std::mutex> l(g_levelM);
            level = g_levelName;
        }
        ServerOptions so;
        so.config.mode = c.host.mode == 2   ? MatchMode::Coop
                         : c.host.mode == 1 ? MatchMode::TeamDeathmatch
                                            : MatchMode::Deathmatch;
        if (const char* ml = std::getenv("DRAKAN_MP_MAP"); ml && *ml) // test aid: pick the map without the menu
            level = ml;
        so.config.level = (level.empty() ? std::string("any") : level).substr(0, kMaxLevelLength);
        if (const drakan::net::ArenaDef* arena = drakan_arena::findMap(level);
            arena && so.config.mode != MatchMode::Coop &&
            (so.config.mode == MatchMode::TeamDeathmatch ? arena->teamDeathmatch : arena->deathmatch)) {
            // A multiplayer map: the server also owns its spawn choice and its pickups (MatchServer arena options).
            so.hasArena = true;
            so.arena = *arena;
            so.config.level = arena->name.substr(0, kMaxLevelLength);
            so.config.startHealth = arena->startHealth;
            if (const char* ps = std::getenv("DRAKAN_ARENA_PICKUP_SEC"); ps && *ps) // test aid: quick respawns
                for (drakan::net::ArenaPickup& p : so.arena.pickups)
                    p.respawnSec = static_cast<uint32_t>(std::clamp(std::atoi(ps), 1, 3600));
        }
        so.config.serverName = c.host.serverName.substr(0, kMaxServerNameLength);
        so.config.fragLimit = static_cast<uint16_t>(std::clamp(c.host.fragLimit, 0, 65535));
        so.config.timeLimitSec = static_cast<uint16_t>(std::clamp(c.host.timeLimitSec, 0, 65535));
        // Test aids for the menu host (the menu offers 5 .. 200 kills and 5 .. 60 minutes): a short match for the
        // harness.
        if (const char* fl = std::getenv("DRAKAN_MP_FRAG_LIMIT"); fl && *fl)
            so.config.fragLimit = static_cast<uint16_t>(std::clamp(std::atoi(fl), 0, 65535));
        if (const char* tl = std::getenv("DRAKAN_MP_TIME_LIMIT_SEC"); tl && *tl)
            so.config.timeLimitSec = static_cast<uint16_t>(std::clamp(std::atoi(tl), 0, 65535));
        so.config.maxPlayers = static_cast<uint8_t>(std::clamp(c.host.maxPlayers, 1, static_cast<int>(kMaxPlayers)));
        if (so.hasArena)
            so.config.maxPlayers = std::min<uint8_t>(so.config.maxPlayers, so.arena.maxPlayers);
        so.password = c.host.password.substr(0, kMaxPasswordLength);
        so.serverId = std::random_device{}() | 1u;
        so.profileHash = g_profileHash;
        so.autoStartPlayers = c.host.autoStart ? 1 : 0;
        applyDrakanPvpTuning(so); // melee reach, weapon limits, damage x3, respawn after 3 s (MatchServer.h)
        if (const char* hr = std::getenv("DRAKAN_PVP_MAX_HIT_RANGE");
            hr && *hr)                                          // test aid: the harness's ghosts are held
            so.maxHitRange = static_cast<float>(std::atof(hr)); // next to each player, far from the real spawns
        if (const char* ds = std::getenv("DRAKAN_PVP_DAMAGE_SCALE"); ds && *ds)
            so.damageScale = std::clamp(static_cast<float>(std::atof(ds)), 0.1f, 100.0f);
        if (const char* rd = std::getenv("DRAKAN_PVP_RESPAWN_MS"); rd && *rd)
            so.config.respawnDelayMs = static_cast<uint16_t>(std::clamp(std::atoi(rd), 500, 60000));
        so.lobbyCountdownMs = 200;
        so.hostKick = true;
        so.mods = loadedMods();
        // The menu Start Match counts down 3 s (Status shows it); the shortcuts start at once.
        so.startCountdownMs = c.host.autoStart ? 0 : 3000;
        if (const char* sc = std::getenv("DRAKAN_MP_START_COUNTDOWN_MS"); sc && *sc)
            so.startCountdownMs = static_cast<uint32_t>(std::clamp(std::atoi(sc), 0, 60000));
        if (const char* rs = std::getenv("DRAKAN_MP_RESULTS_SEC"); rs && *rs)
            so.resultsDurationMs = static_cast<uint32_t>(std::clamp(std::atoi(rs), 1, 600)) * 1000u;
        so.loadTimeoutMs = c.host.autoStart ? 3000 : 120000; // a New Game from the title takes a while to load
        server = std::make_unique<MatchServer>(*serverT, so);
        server->onLog = [](const std::string& s) { netLog("server: %s", s.c_str()); };
        serverT->setQueryResponder([&server](const std::vector<uint8_t>& req) { return server->answerQuery(req); });
        netLog("host: listening on UDP %u (profile hash %016llx, level '%s')", serverT->localPort(),
               static_cast<unsigned long long>(g_profileHash), so.config.level.c_str());
    }

    UdpTransport clientT;
    if (!clientT.available()) {
        netLog("sockets unavailable; networking off");
        return;
    }
    ClientOptions co;
    co.name = c.name.empty() ? g_name : c.name;
    co.password = hosting ? c.host.password : c.password;
    co.profileHash = g_profileHash;
    co.interpolationDelayMs = 100;
    MatchClient client(clientT, co);
    const std::string host = connectHost;
    const uint16_t port = connectPort;

    uint64_t lastSend = 0, lastConnectTry = 0, lastStats = 0;
    uint64_t statSnapshots = 0, statSent = 0;
    MatchPhase lastPhase = MatchPhase::Lobby;
    bool loadDoneSent = false;
    bool charSent = false;
    uint16_t lastChar = 0;
    bool wasConnected = false;
    bool tried = false;
    bool rejectLogged = false;
    bool failed = false;
    std::string failMessage;
    bool leaving = false;
    std::map<int, uint64_t> seenAt; // player id -> when this client first saw it (scoreboard Time column)

    while (!g_stop.load() && !leaving) {
        const uint64_t now = nowMs();
        Cmd cmd;
        while (popCmd(cmd)) {
            switch (cmd.kind) {
                case Cmd::Leave:
                    leaving = true;
                    break;
                case Cmd::Start:
                    if (server && !server->startMatch(now))
                        netLog("start refused (not in the lobby, or nobody connected)");
                    break;
                case Cmd::Ready:
                    client.sendReady(cmd.flag);
                    break;
                case Cmd::Kick:
                    if (!client.sendKick(static_cast<PlayerId>(cmd.value)))
                        netLog("kick refused (not connected)");
                    break;
                case Cmd::Chat:
                    client.sendChat(cmd.text);
                    break;
                default: // Host / Join while in a session: ignored (the API refuses them first)
                    break;
            }
        }
        if (leaving)
            break;
        serviceLan(browser, now, lastSweep);
        if (server)
            server->tick(now);
        client.tick(now);
        g_coopMode.store(client.connected() && client.config().mode == MatchMode::Coop);

        if (!client.connected() && !client.rejected() && !failed &&
            (!tried || (client.state() == ClientState::Disconnected && now - lastConnectTry > 2000))) {
            if (tried && !legacy) { // a menu join does not retry forever
                failed = true;
                failMessage = "Could not connect to " + host + ":" + std::to_string(port);
            } else {
                tried = true;
                lastConnectTry = now;
                netLog("connecting to %s:%u as '%s'", host.c_str(), port, co.name.c_str());
                if (!client.connect(host, port, now)) {
                    netLog("cannot use address %s:%u", host.c_str(), port);
                    failed = true;
                    failMessage = "Cannot use address " + host + ":" + std::to_string(port);
                }
            }
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
                if (!legacy && !d->wasRejected && wasConnected) {
                    failed = true;
                    failMessage = "Connection to the server lost";
                }
                tried = legacy ? false : tried;
            } else if (const auto* j = std::get_if<PlayerJoin>(&ev)) {
                netLog("player %u '%s' joined", j->player, j->name.c_str());
                addChat(0, "", j->name + " joined");
            } else if (const auto* l = std::get_if<PlayerLeave>(&ev)) {
                netLog("player %u left", l->player);
            } else if (const auto* ch = std::get_if<Chat>(&ev)) {
                const auto it = client.roster().find(ch->player);
                addChat(ch->player, it != client.roster().end() ? it->second.name : std::string("?"), ch->text);
            } else if (const auto* ca = std::get_if<CoopAuthority>(&ev)) {
                netLog("co-op authority: player %u%s", ca->player, ca->player == client.playerId() ? " (us)" : "");
            } else if (auto* cc = std::get_if<CoopCreatures>(&ev)) {
                std::lock_guard<std::mutex> lk(g_coopM);
                g_coopInCreatures.push_back(std::move(*cc));
                if (g_coopInCreatures.size() > 64)
                    g_coopInCreatures.pop_front();
            } else if (const auto* ce = std::get_if<CoopEvent>(&ev)) {
                std::lock_guard<std::mutex> lk(g_coopM);
                if (g_coopInEvents.size() < kCoopQueueMax)
                    g_coopInEvents.push_back(*ce);
            } else if (const auto* pu = std::get_if<PickupState>(&ev)) {
                drakan_arena::onPickupState(pu->index, pu->taken, pu->player);
            } else if (const auto* lc = std::get_if<LevelChange>(&ev)) {
                // The leader went through a level exit: fire the same exit in our copy of the level (the world position
                // of the trigger is the same on every machine, drakan_levelchange).
                netLog("player %u changed level through the exit at %.0f %.0f %.0f (destination hash %08x): following",
                       lc->player, static_cast<double>(lc->exit[0]), static_cast<double>(lc->exit[1]),
                       static_cast<double>(lc->exit[2]), lc->destHash);
                drakan_levelchange::followExit(lc->exit[0], lc->exit[1], lc->exit[2]);
                {
                    std::lock_guard<std::mutex> fl(g_followM);
                    g_followIn = FollowCheck();
                    g_followIn.active = true;
                    g_followIn.destHash = g_testDestHash ? g_testDestHash : lc->destHash;
                    g_followIn.srcHash = g_levelHash.load();
                    g_followIn.loadsAtStart = g_levelLoads.load();
                    std::memcpy(g_followIn.exit, lc->exit, sizeof g_followIn.exit);
                    g_followNew = true;
                }
            } else if (const auto* hit = std::get_if<Hit>(&ev)) {
                netLog("hit: player %u hit player %u for %u (weapon %u), health now %u", hit->attacker, hit->victim,
                       hit->damage, hit->weapon, hit->healthAfter);
                drakan_pvp::onServerHit(hit->attacker, hit->victim, hit->weapon, hit->damage, hit->healthAfter);
            } else if (const auto* dth = std::get_if<Death>(&ev)) {
                const auto nameOf = [&](PlayerId id) {
                    const auto r = client.roster().find(id);
                    return r != client.roster().end() ? r->second.name : std::string("?");
                };
                netLog("death: player %u killed by player %u (weapon %u)", dth->victim, dth->killer, dth->weapon);
                {
                    std::lock_guard<std::mutex> lk(g_combatM);
                    KillFeedEntry k;
                    k.killer = dth->killer;
                    k.victim = dth->victim;
                    k.killerName = nameOf(dth->killer);
                    k.victimName = nameOf(dth->victim);
                    k.weapon = dth->weapon;
                    {
                        // The killer's held item as of its newest state: the hand that struck, the bow for a
                        // projectile.
                        drakan::net::PlayerState ks;
                        uint32_t ksMs = 0;
                        uint32_t record = 0;
                        if (client.newestSample(dth->killer, ks, ksMs))
                            record = dth->weapon == drakan::net::kHitMeleeRight ? ks.weaponRight : ks.weaponLeft;
                        else if (dth->killer == client.playerId()) {
                            std::lock_guard<std::mutex> ll(g_s.m);
                            record = dth->weapon == drakan::net::kHitMeleeRight ? g_s.local.weaponRight
                                                                                : g_s.local.weaponLeft;
                        }
                        k.weaponName = drakan::net::weaponDisplayName(dth->weapon, record);
                    }
                    k.at = nowSec();
                    g_board.killFeed.push_back(std::move(k));
                    if (g_board.killFeed.size() > 8)
                        g_board.killFeed.erase(g_board.killFeed.begin());
                    if (dth->victim == client.playerId())
                        g_deadSince = nowSec();
                }
                drakan_pvp::onServerDeath(dth->victim, dth->killer, dth->weapon);
            } else if (const auto* rsp = std::get_if<Respawn>(&ev)) {
                netLog("respawn: player %u at spawn %u with health %u", rsp->player, rsp->spawnIndex, rsp->health);
                if (rsp->player == client.playerId()) {
                    std::lock_guard<std::mutex> lk(g_combatM);
                    g_deadSince = -1;
                }
                drakan_pvp::onServerRespawn(rsp->player, rsp->spawnIndex, rsp->health);
            } else if (const auto* sc = std::get_if<Score>(&ev)) {
                netLog("score: player %u kills %u deaths %u (team kills %u / %u)", sc->player, sc->kills, sc->deaths,
                       sc->teamKills[0], sc->teamKills[1]);
            } else if (const auto* pc = std::get_if<PhaseChange>(&ev)) {
                drakan_pvp::onServerPhase(static_cast<uint8_t>(pc->phase));
                if (pc->phase == MatchPhase::Results) {
                    const MatchOutcome o = client.outcome();
                    std::string rows;
                    for (const drakan::net::ScoreRow& r : client.scoreboard())
                        rows += " [" + std::to_string(r.player) + " '" + r.name + "' " + std::to_string(r.kills) + "/" +
                                std::to_string(r.deaths) + "]";
                    netLog("match over (%s): %s, results (kills/deaths):%s",
                           o.reason == EndReason::FragLimit   ? "frag limit"
                           : o.reason == EndReason::TimeLimit ? "time limit"
                                                              : "ended",
                           o.tie                   ? "tie"
                           : o.winner != kNoPlayer ? ("player " + std::to_string(o.winner) + " wins").c_str()
                                                   : ("team " + std::to_string(o.winningTeam) + " wins").c_str(),
                           rows.c_str());
                }
                if (pc->phase == MatchPhase::Playing) {
                    std::lock_guard<std::mutex> lk(g_combatM);
                    g_playingSinceServerMs = pc->serverTimeMs;
                    g_havePlayingSince = true;
                    g_board.killFeed.clear();
                    g_deadSince = -1;
                }
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
                // A new match loads its level from the title: the hash of the level of the match before (the title
                // was reached by "Quit to Title", which reports no load) must not count as "our level is up".
                if (client.phase() == MatchPhase::Loading)
                    g_levelHash.store(0);
                if (client.phase() ==
                    MatchPhase::Lobby) { // the match is over: a New Game from the title is an ordinary one
                    std::lock_guard<std::mutex> l(g_launchM);
                    setRedirect("");
                }
            }
            if (!legacy && (client.phase() == MatchPhase::Loading || client.phase() == MatchPhase::Playing))
                requestLaunch(client.config().level, client.config().mode != MatchMode::Coop);
            // LoadDone only once our own level is up (a New Game from the title needs the intro and the load first).
            if (client.phase() == MatchPhase::Loading && !loadDoneSent && g_levelHash.load() != 0)
                loadDoneSent = client.sendLoadDone();
            if (client.phase() == MatchPhase::Lobby || client.phase() == MatchPhase::Results) {
                std::lock_guard<std::mutex> l(g_launchM);
                if (client.phase() == MatchPhase::Lobby)
                    g_launchDone = false; // the next match launches again
            }
        }

        // The arena world mode (drakan_arena) follows the session: on for a deathmatch match that is loading, playing
        // or showing its results; a menu launched match strips the campaign from any level, the DRAKAN_NET_* shortcuts
        // only for a multiplayer map.
        {
            const bool dm = connected && client.config().mode != MatchMode::Coop &&
                            (client.phase() == MatchPhase::Loading || client.phase() == MatchPhase::Playing ||
                             client.phase() == MatchPhase::Results);
            const drakan::net::ArenaDef* def = dm ? drakan_arena::findMap(client.config().level) : nullptr;
            drakan_arena::setSession(dm && (!legacy || def), def);
            drakan_arena::setLocalPlayer(connected ? client.playerId() : 0);
        }

        // Outgoing hit claims from the EE thread (the server validates them).
        if (connected) {
            std::deque<ClaimOut> claims;
            {
                std::lock_guard<std::mutex> lk(g_combatM);
                claims.swap(g_claims);
            }
            for (const ClaimOut& cl : claims) {
                const bool sent = client.sendHit(cl.victim, cl.weapon, cl.damage);
                netLog("claim: hit on player %u, weapon %u, damage %u%s", cl.victim, cl.weapon, cl.damage,
                       sent ? "" : " (not sent)");
            }
            drakan_pvp::setLocalPlayer(client.playerId());
            uint16_t pickup = 0;
            while (drakan_arena::takePickupClaim(pickup)) {
                const bool sent = client.sendPickupClaim(pickup);
                netLog("claim: pickup %u%s", pickup, sent ? "" : " (not sent)");
            }
        } else {
            drakan_pvp::setLocalPlayer(0);
        }

        // Co-op: the link for drakan_coop, then the outgoing creature stream / events and the host's level changes.
        {
            const bool coopMatch = connected && client.config().mode == MatchMode::Coop;
            const bool active = coopMatch && client.phase() == MatchPhase::Playing;
            g_coopMode.store(coopMatch);
            CoopLink link;
            link.active = active;
            link.host = active && client.isCoopAuthority();
            link.self = client.playerId();
            link.authority = client.coopAuthority();
            link.players = static_cast<int>(client.roster().size());
            for (const auto& kv : client.roster())
                if (kv.first != client.playerId()) {
                    link.partner = kv.first;
                    break;
                }
            std::deque<CoopCreatures> oc;
            std::deque<CoopEvent> oe;
            {
                std::lock_guard<std::mutex> lk(g_coopM);
                link.session = g_sessionSerial;
                g_coopLink = link;
                if (active) {
                    oc.swap(g_coopOutCreatures);
                    oe.swap(g_coopOutEvents);
                } else {
                    g_coopOutCreatures.clear();
                    g_coopOutEvents.clear();
                    g_coopInCreatures.clear();
                    g_coopInEvents.clear();
                }
            }
            for (const CoopCreatures& c : oc)
                client.sendCoopCreatures(c);
            for (const CoopEvent& e : oe)
                client.sendCoopEvent(e);
            // A level change goes out from the one the server lets lead: the co-op authority, else the lowest id.
            std::deque<LevelChangeOut> lcs;
            {
                std::lock_guard<std::mutex> lk(g_lcM);
                lcs.swap(g_lcOut);
            }
            if (connected && client.phase() == MatchPhase::Playing) {
                const bool leader =
                    client.config().mode == MatchMode::Coop
                        ? client.isCoopAuthority()
                        : (!client.roster().empty() && client.roster().begin()->first == client.playerId());
                if (leader)
                    for (const LevelChangeOut& l : lcs) {
                        netLog("level change: telling the others (exit %.0f %.0f %.0f, destination hash %08x)",
                               static_cast<double>(l.pos[0]), static_cast<double>(l.pos[1]),
                               static_cast<double>(l.pos[2]), l.destHash);
                        client.sendLevelChange(l.destHash, l.pos);
                    }
            }
        }

        // The co-op character picked in the lobby goes to the server (it relays it to everybody with Status).
        if (!connected)
            charSent = false;
        else if (client.phase() == MatchPhase::Lobby) {
            const uint16_t ch = drakan_coop_character::selected();
            if (!charSent || ch != lastChar) {
                charSent = client.sendCharacter(ch);
                lastChar = ch;
            }
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

        // State for the UI.
        std::vector<Player> boardRoster; // the roster of this pass, for the scoreboard's ping / time columns
        {
            State s = base;
            s.conn = client.rejected() ? Conn::Rejected
                     : connected       ? Conn::Connected
                     : failed          ? Conn::Rejected
                                       : Conn::Connecting;
            if (client.rejected())
                s.message = client.rejectInfo().message.empty() ? std::string("Rejected by the server")
                                                                : client.rejectInfo().message;
            else if (failed)
                s.message = failMessage;
            else if (connected)
                s.message.clear();
            if (connected) {
                const MatchConfigData& cf = client.config();
                s.phase = static_cast<Phase>(static_cast<int>(client.phase()));
                s.serverName = cf.serverName;
                s.level = cf.level;
                s.mode = static_cast<int>(cf.mode);
                s.maxPlayers = cf.maxPlayers;
                s.fragLimit = cf.fragLimit;
                s.timeLimitSec = cf.timeLimitSec;
                s.selfId = client.playerId();
                {
                    uint32_t ms = 0;
                    s.countdownKind = static_cast<int>(client.countdown(now, ms));
                    s.countdownSec = s.countdownKind ? ms / 1000.0 : -1.0;
                }
                s.teamKills[0] = client.teamKills(0);
                s.teamKills[1] = client.teamKills(1);
                for (const auto& kv : client.roster()) {
                    Player p;
                    p.id = kv.first;
                    p.name = kv.second.name;
                    p.team = kv.second.team < 2 ? kv.second.team : -1;
                    p.ready = kv.second.ready;
                    p.self = kv.first == client.playerId();
                    p.host = kv.first == 1;
                    p.kills = kv.second.kills;
                    p.deaths = kv.second.deaths;
                    if (!seenAt.count(p.id))
                        seenAt[p.id] = now;
                    p.timeSec = static_cast<int>((now - seenAt[p.id]) / 1000);
                    if (kv.second.pingMs != kPingUnknown)
                        p.pingMs = kv.second.pingMs; // the server measure (Status), for every player
                    else if (p.self && client.rttMs() >= 0)
                        p.pingMs = static_cast<int>(client.rttMs());
                    p.character = kv.second.character;
                    if (p.self)
                        s.selfReady = p.ready;
                    s.roster.push_back(std::move(p));
                }
            }
            {
                std::lock_guard<std::mutex> l(g_launchM);
                s.launching = g_launchPending || g_launchTaps.load();
                s.launchError = g_launchError;
                if (!g_launchError.empty())
                    s.message = g_launchError;
            }
            boardRoster = s.roster;
            publish(s);
        }

        // Scoreboard for the overlay / results screen (the kill feed is appended by the event loop above).
        {
            std::lock_guard<std::mutex> lk(g_combatM);
            Scoreboard b;
            b.killFeed = std::move(g_board.killFeed);
            b.now = nowSec();
            if (connected) {
                const MatchConfigData& cf = client.config();
                b.phase = static_cast<Phase>(static_cast<int>(client.phase()));
                b.inMatch = client.phase() == MatchPhase::Playing || client.phase() == MatchPhase::Results;
                b.mode = static_cast<int>(cf.mode);
                b.fragLimit = cf.fragLimit;
                b.timeLimitSec = cf.timeLimitSec;
                b.teamKills[0] = client.teamKills(0);
                b.teamKills[1] = client.teamKills(1);
                for (const ScoreRow& r : [&] {
                         std::vector<ScoreRow> rows;
                         for (const drakan::net::ScoreRow& n : client.scoreboard()) {
                             ScoreRow r;
                             r.id = n.player;
                             r.name = n.name;
                             r.team = n.team < 2 ? n.team : -1;
                             r.kills = n.kills;
                             r.deaths = n.deaths;
                             r.health = n.health;
                             r.alive = n.alive;
                             for (const Player& pl : boardRoster)
                                 if (pl.id == r.id) {
                                     r.pingMs = pl.pingMs;
                                     r.timeSec = pl.timeSec;
                                 }
                             r.self = n.player == client.playerId();
                             rows.push_back(std::move(r));
                         }
                         return rows;
                     }()) {
                    if (r.self) {
                        b.selfHealth = r.health;
                        b.selfAlive = r.alive;
                    }
                    b.rows.push_back(r);
                }
                b.selfMaxHealth = cf.startHealth;
                if (client.phase() == MatchPhase::Playing && cf.timeLimitSec > 0 && g_havePlayingSince) {
                    const int64_t elapsed =
                        static_cast<int64_t>(client.serverTimeAt(now)) - static_cast<int64_t>(g_playingSinceServerMs);
                    b.timeLeftSec =
                        static_cast<int>(std::max<int64_t>(0, static_cast<int64_t>(cf.timeLimitSec) - elapsed / 1000));
                }
                if (!b.selfAlive && g_deadSince >= 0)
                    b.respawnInSec = std::max(0.0, cf.respawnDelayMs / 1000.0 - (b.now - g_deadSince));
                if (client.phase() == MatchPhase::Results) {
                    uint32_t ms = 0;
                    if (client.countdown(now, ms) == CountdownKind::NextMatch)
                        b.nextMatchSec = ms / 1000.0;
                    const MatchOutcome o = client.outcome();
                    b.over = true;
                    b.tie = o.tie;
                    b.winnerId = o.winner;
                    b.winnerTeam = o.winningTeam < 2 ? o.winningTeam : -1;
                    if (o.winner != kNoPlayer)
                        for (const ScoreRow& r : b.rows)
                            if (r.id == o.winner)
                                b.winnerName = r.name;
                    b.reason = o.reason == EndReason::FragLimit   ? "frag limit"
                               : o.reason == EndReason::TimeLimit ? "time limit"
                                                                  : "match ended";
                }
            }
            g_board = std::move(b);
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
    {
        std::lock_guard<std::mutex> l(g_s.m);
        g_s.playing = false;
        g_s.remotes.clear();
    }
    g_coopMode.store(false);
    {
        std::lock_guard<std::mutex> lk(g_coopM);
        ++g_sessionSerial;
        g_coopLink = CoopLink();
        g_coopInCreatures.clear();
        g_coopInEvents.clear();
        g_coopOutCreatures.clear();
        g_coopOutEvents.clear();
    }
    setRedirect("");
    g_launchTaps.store(false);
    {
        std::lock_guard<std::mutex> l(g_launchM);
        g_launchPending = false;
        g_launchDone = false;
        g_matchLevel.clear();
    }
}

void netThread() {
    using namespace drakan::net;
    ServerBrowser browser;
    uint64_t lastSweep = 0;
    while (!g_stop.load()) {
        Cmd c;
        if (popCmd(c)) {
            if (c.kind == Cmd::Host || c.kind == Cmd::Join) {
                g_inSession.store(true);
                runSession(c, browser, lastSweep);
                g_inSession.store(false);
                std::lock_guard<std::mutex> l(g_stM);
                g_state = State();
                g_state.available = true;
            }
            continue; // Leave / Start / Ready / Chat outside a session: dropped
        }
        serviceLan(browser, nowMs(), lastSweep);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
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
    int dragonId = 0; // the partner's Arokh while the partner is mounted (a second, visual-only dragon body)
    double dragonRetryAt = 0;
    uint16_t character = 0; // the stand-in character the ghost was spawned as (PlayerState.character)
    bool wasVisible = false;
    double unwantedSince = -1;
    double lastWantLog = -10; // DRAKAN_NET_TRACE diagnostics
    double retryAt = 0;       // after a ghost that vanished by itself
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
        kv.second.dragonId = 0;
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
    if (v.dragonId) {
        drakan_ghost::destroy(v.dragonId);
        v.dragonId = 0;
    }
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

bool wanted() {
    const char* e = std::getenv("DRAKAN_MULTIPLAYER");
    return !(e && e[0] == '0');
}

// ---- control API ----

namespace {
std::string clampName(const std::string& n) {
    std::string out;
    for (unsigned char c : n)
        if (c >= 0x20 && c < 0x7f && out.size() < drakan::net::kMaxNameLength)
            out.push_back(static_cast<char>(c));
    return out.empty() ? std::string("Rynn") : out;
}
} // namespace

bool hostMatch(const HostConfig& cfg, const std::string& playerName) {
    if (!g_enabled.load() || g_inSession.load())
        return false;
    Cmd c;
    c.kind = Cmd::Host;
    c.host = cfg;
    c.name = clampName(playerName);
    g_inSession.store(true); // refuses a second call at once, the thread clears it when the session ends
    {
        std::lock_guard<std::mutex> l(g_stM);
        g_state = State();
        g_state.available = true;
        g_state.inSession = true;
        g_state.hosting = true;
        g_state.conn = Conn::Connecting;
    }
    pushCmd(std::move(c));
    return true;
}

bool joinMatch(const std::string& address, const std::string& password, const std::string& playerName) {
    if (!g_enabled.load() || g_inSession.load() || address.empty())
        return false;
    Cmd c;
    c.kind = Cmd::Join;
    c.addr = address;
    c.password = password;
    c.name = clampName(playerName);
    g_inSession.store(true);
    {
        std::lock_guard<std::mutex> l(g_stM);
        g_state = State();
        g_state.available = true;
        g_state.inSession = true;
        g_state.conn = Conn::Connecting;
        g_state.message = "Connecting to " + address + " ...";
    }
    pushCmd(std::move(c));
    return true;
}

void leave() {
    Cmd c;
    c.kind = Cmd::Leave;
    pushCmd(std::move(c));
}

bool startMatch() {
    if (!g_inSession.load())
        return false;
    Cmd c;
    c.kind = Cmd::Start;
    pushCmd(std::move(c));
    return true;
}

void setReady(bool ready) {
    Cmd c;
    c.kind = Cmd::Ready;
    c.flag = ready;
    pushCmd(std::move(c));
}

void kickPlayer(int playerId) {
    if (!g_inSession.load() || playerId <= 0)
        return;
    Cmd c;
    c.kind = Cmd::Kick;
    c.value = playerId;
    pushCmd(std::move(c));
}

void pickCharacter(uint16_t id) {
    drakan_coop_character::setSelected(id);
}

void setLoadedMods(const std::string& names) {
    std::lock_guard<std::mutex> l(g_modsM);
    g_mods = names;
}

void setCoopParty(const std::vector<CoopPartnerInfo>& partners, bool levelComplete) {
    std::lock_guard<std::mutex> l(g_partyM);
    g_party = partners;
    g_partyComplete = levelComplete;
}
std::vector<CoopPartnerInfo> coopParty(bool& levelComplete) {
    std::lock_guard<std::mutex> l(g_partyM);
    levelComplete = g_partyComplete;
    return g_party;
}

void sendChat(const std::string& text) {
    if (text.empty())
        return;
    Cmd c;
    c.kind = Cmd::Chat;
    c.text = text;
    pushCmd(std::move(c));
}

State state() {
    State s;
    {
        std::lock_guard<std::mutex> l(g_stM);
        s = g_state;
    }
    s.available = g_enabled.load();
    if (!s.available)
        s.unavailableWhy = g_unavailableWhy;
    s.now = nowSec();
    return s;
}

std::vector<ChatLine> chatLog() {
    std::lock_guard<std::mutex> l(g_stM);
    return g_chat;
}

void refreshLan() {
    g_lanRefreshAt.store(nowMs() + 1);
    g_lanWantedAt.store(nowMs() + 1);
    g_lanScanning.store(true);
}

std::vector<LanServer> lanServers() {
    g_lanWantedAt.store(nowMs() + 1);
    std::lock_guard<std::mutex> l(g_lanM);
    return g_lan;
}

CoopLink coopLink() {
    std::lock_guard<std::mutex> l(g_coopM);
    return g_coopLink;
}
bool coopMatch() {
    return g_coopMode.load();
}
void coopSend(const drakan::net::CoopCreatures& c) {
    std::lock_guard<std::mutex> l(g_coopM);
    if (!g_coopLink.active)
        return;
    g_coopOutCreatures.push_back(c);
    if (g_coopOutCreatures.size() > 64)
        g_coopOutCreatures.pop_front();
}
void coopSend(const drakan::net::CoopEvent& e) {
    std::lock_guard<std::mutex> l(g_coopM);
    if (g_coopLink.active && g_coopOutEvents.size() < kCoopQueueMax)
        g_coopOutEvents.push_back(e);
}
bool coopPoll(drakan::net::CoopCreatures& out) {
    std::lock_guard<std::mutex> l(g_coopM);
    if (g_coopInCreatures.empty())
        return false;
    out = std::move(g_coopInCreatures.front());
    g_coopInCreatures.pop_front();
    return true;
}
bool coopPoll(drakan::net::CoopEvent& out) {
    std::lock_guard<std::mutex> l(g_coopM);
    if (g_coopInEvents.empty())
        return false;
    out = g_coopInEvents.front();
    g_coopInEvents.pop_front();
    return true;
}

void sendHitClaim(uint8_t victimPlayer, uint8_t weapon, uint16_t damage) {
    std::lock_guard<std::mutex> l(g_combatM);
    if (g_claims.size() < 64)
        g_claims.push_back({victimPlayer, weapon, damage});
}

int playerOfGhost(int ghostId) {
    std::lock_guard<std::mutex> l(g_combatM);
    const auto it = g_ghostToPlayer.find(ghostId);
    return it == g_ghostToPlayer.end() ? 0 : it->second;
}

int ghostOfPlayer(int player) {
    std::lock_guard<std::mutex> l(g_combatM);
    const auto it = g_playerToGhost.find(player);
    return it == g_playerToGhost.end() ? 0 : it->second;
}

Scoreboard scoreboard() {
    std::lock_guard<std::mutex> l(g_combatM);
    Scoreboard b = g_board;
    b.now = nowSec();
    return b;
}

bool lanScanning() {
    return g_lanScanning.load();
}

bool takeLaunchRequest(std::string& level) {
    std::lock_guard<std::mutex> l(g_launchM);
    if (!g_launchPending)
        return false;
    g_launchPending = false;
    level = g_launchLevel;
    g_launchTaps.store(!g_launchDirect); // a direct map launch has no intro to skip
    setRedirect(level, g_launchDirect);
    return true;
}

bool launchTaps() {
    return g_launchTaps.load();
}

void setLevelRedirectFn(std::function<void(const std::string&, const std::string&)> fn) {
    g_redirectFn = std::move(fn);
}

void setExtraLevels(std::vector<std::string> names) {
    g_extraLevels = std::move(names);
}

std::vector<std::string> extraLevels() {
    return g_extraLevels;
}

void install(PS2Runtime& rt) {
    (void)rt;
    if (!wanted())
        return;
    if (const char* d = std::getenv("DRAKAN_NET_TEST_LC_DEST");
        d && *d) // test aid: expect another level after a follow
        g_testDestHash = fnv32(d);
    const char* host = std::getenv("DRAKAN_NET_HOST");
    const char* conn = std::getenv("DRAKAN_NET_CONNECT");
    const bool wantHost = host && *host;
    const bool wantConn = conn && *conn;
#if !defined(DRAKAN_NET_HAVE_UDP)
    g_unavailableWhy = "this build has no UDP transport";
    std::fprintf(stderr, "[netplay] disabled: %s\n", g_unavailableWhy.c_str());
#else
    if (wantHost && wantConn) {
        std::fprintf(stderr, "[netplay] set only one of DRAKAN_NET_HOST / DRAKAN_NET_CONNECT\n");
        return;
    }
    if (!drakan_ghost::enabled()) {
        g_unavailableWhy = "the ghost module is not installed";
        std::fprintf(stderr, "[netplay] disabled: %s\n", g_unavailableWhy.c_str());
        return;
    }
    if (!drakan_lifecycle::installed()) {
        g_unavailableWhy = "unsupported disc profile";
        std::fprintf(stderr, "[netplay] disabled: lifecycle hooks are not installed (unsupported disc profile)\n");
        return;
    }
    world::Globals g;
    g.contextPtr = drakan_region::guestAddress(g.contextPtr);
    g.hpKey = drakan_region::guestAddress(g.hpKey);
    g.playerStats = drakan_region::guestAddress(g.playerStats);
    g.handleVtable = drakan_region::guestAddress(g.handleVtable);
    if (!g.contextPtr || !g.hpKey || !g.playerStats || !g.handleVtable) {
        g_unavailableWhy = "unsupported disc profile";
        std::fprintf(stderr, "[netplay] disabled: no address translation for this disc profile\n");
        return;
    }
    g_globals = g;
    if (const char* n = std::getenv("DRAKAN_NET_NAME"); n && *n)
        g_name = std::string(n).substr(0, drakan::net::kMaxNameLength);
    const char* tr = std::getenv("DRAKAN_NET_TRACE");
    g_trace = tr && tr[0] && tr[0] != '0';
    const auto& prof = drakan_region::build();
    g_profileHash = fnv64(std::string(prof.serial ? prof.serial : "") + "|" + (prof.saveId ? prof.saveId : ""));

    // The leader's level change reaches the others as a LevelChange message (protocol v6): the exit's world position
    // and the hash of the destination name when the exit list knows it. The network thread sends it when this player
    // leads.
    drakan_levelchange::setExitListener([](const float* pos) {
        if (!g_inSession.load())
            return;
        LevelChangeOut o;
        std::memcpy(o.pos, pos, sizeof o.pos);
        for (const auto& e : drakan_levelchange::exits()) {
            const float dx = e.pos[0] - pos[0], dy = e.pos[1] - pos[1], dz = e.pos[2] - pos[2];
            if (dx * dx + dy * dy + dz * dz < 1.0f && !e.destName.empty()) {
                o.destHash = fnv32(e.destName);
                break;
            }
        }
        std::lock_guard<std::mutex> lk(g_lcM);
        if (g_lcOut.size() < 8)
            g_lcOut.push_back(o);
    });
    // The lifecycle hooks report the level name the guest asked for; behind a file redirect (a menu launch, the
    // DRAKAN_START_LEVEL test aid) another level loads under that name. Every listener (ghosts, co-op, the arena) gets
    // the real one.
    if (const char* to = std::getenv("DRAKAN_START_LEVEL"); to && *to) {
        const char* from = std::getenv("DRAKAN_START_LEVEL_FROM");
        std::lock_guard<std::mutex> lk(g_launchM);
        g_redirectFrom = from && *from ? from : kFirstLevel;
        g_matchLevel = to;
        g_redirectActive.store(true);
    }
    drakan_lifecycle::setLevelNameMapper([](const std::string& name) {
        std::lock_guard<std::mutex> lk(g_launchM);
        return g_redirectActive.load() && name == g_redirectFrom && !g_matchLevel.empty()
                   ? g_matchLevel
                   : drakan_game::mapLevelName(name); // d1: the opening cut scene level is the campaign's start
    });
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
        std::string name = e.level;
        {
            // A menu launch loads its level under the first level's name (file redirect, the engine and the lifecycle
            // hooks still report "Level 1A - Surdana"): the match level is the real name.
            std::lock_guard<std::mutex> lk(g_launchM);
            if (g_redirectActive.load() && name == g_redirectFrom && !g_matchLevel.empty())
                name = g_matchLevel;
        }
        {
            std::lock_guard<std::mutex> lk(g_levelM);
            g_levelName = name;
        }
        // "Level 0 - Intro" is the opening cut scene: Rynn is there but a second actor breaks it (a ghost spawned in it
        // made the guest jump to a null target, docs/journal/2026-10-31/netplay-spike.md), so it is not a level for
        // ghosts. Spawns also wait kSettleSec after a load (the engine is still creating its own actors).
        const bool playable = name.rfind("Level 0", 0) != 0;
        g_levelHash.store(playable ? fnv32(name) : 0);
        if (playable)
            ++g_levelLoads;
        g_spawnNotBefore = nowSec() + kSettleSec;
        if (playable)
            g_launchTaps.store(false); // the intro was skipped, the match level is up
        netLog("level '%s' loaded (%s, hash %08x)", name.c_str(), playable ? "playable" : "cut scene: no ghosts",
               playable ? fnv32(name) : 0);
    });

    g_enabled.store(true);
    g_state.available = true;
    g_thread = std::thread(netThread);
    std::atexit(stopThread);
    std::fprintf(stderr, "[netplay] enabled (multiplayer menu and DRAKAN_NET_* shortcuts), name '%s'\n",
                 g_name.c_str());

    // Dev shortcuts: the same calls the menu makes.
    if (wantHost) {
        const long p = std::strtol(host, nullptr, 10);
        if (p < 1 || p > 65535) {
            std::fprintf(stderr, "[netplay] bad DRAKAN_NET_HOST port '%s'\n", host);
            return;
        }
        HostConfig hc;
        hc.port = static_cast<uint16_t>(p);
        hc.fragLimit = 0;
        hc.timeLimitSec = 0;
        if (const char* fl = std::getenv("DRAKAN_NET_FRAG_LIMIT"); fl && *fl) // test aids: end the match with results
            hc.fragLimit = std::clamp(std::atoi(fl), 0, 65535);
        if (const char* tl = std::getenv("DRAKAN_NET_TIME_LIMIT"); tl && *tl)
            hc.timeLimitSec = std::clamp(std::atoi(tl), 0, 65535);
        hc.autoStart = true;
        if (const char* m = std::getenv("DRAKAN_NET_MODE"); m && *m) // test aid: co-op host
            hc.mode = std::string(m) == "coop" ? 2 : std::string(m) == "tdm" ? 1 : 0;
        hc.serverName = g_name + "'s game";
        std::fprintf(stderr, "[netplay] DRAKAN_NET_HOST: host on UDP %u, name '%s'\n", hc.port, g_name.c_str());
        hostMatch(hc, g_name);
    } else if (wantConn) {
        const std::string c = conn;
        const size_t colon = c.rfind(':');
        const long p = colon == std::string::npos ? 0 : std::strtol(c.c_str() + colon + 1, nullptr, 10);
        if (colon == std::string::npos || colon == 0 || p < 1 || p > 65535) {
            std::fprintf(stderr, "[netplay] bad DRAKAN_NET_CONNECT '%s' (want host:port)\n", conn);
            return;
        }
        std::fprintf(stderr, "[netplay] DRAKAN_NET_CONNECT: client of %s, name '%s'\n", conn, g_name.c_str());
        Cmd cmd;
        cmd.kind = Cmd::Join;
        cmd.addr = c;
        cmd.name = g_name;
        cmd.flag = true; // legacy: no launch, retries the connection
        g_inSession.store(true);
        pushCmd(std::move(cmd));
    }
#endif
}

// EE thread, every boundary: the follower's check that a level change it followed ended in the leader's level. A
// matching hash ends the check. A mismatch is reported (log and a chat line), then the exit list of the level it landed
// in is searched for an exit to the destination and taken; no such exit means the destination cannot be reached from
// here (there is no direct level load: the engine's entry hangs when called from outside its own transition) and the
// check gives up. A follower whose exit never fired (no load within kFollowTimeoutSec) fires it again. Until the hashes
// are equal the ghosts stay hidden by themselves: updateGhosts() only shows a player whose level hash equals ours.
void serviceFollow(double t, uint32_t myLevel) {
    {
        std::lock_guard<std::mutex> l(g_followM);
        if (g_followNew) {
            g_follow = g_followIn;
            g_follow.startedAt = t;
            g_followNew = false;
        }
    }
    FollowCheck& f = g_follow;
    if (!f.active)
        return;
    if (f.retryAt > 0) { // a mismatch was reported: look for the way on once the exit list of this level is there
        drakan_levelchange::requestScan();
        if (t < f.retryAt || myLevel == 0)
            return;
        f.retryAt = 0;
        for (const auto& e : drakan_levelchange::exits())
            if (!e.destName.empty() && fnv32(e.destName) == f.destHash) {
                netLog("level follow: retry %d, taking the exit to '%s' (%.0f %.0f %.0f)", f.attempt,
                       e.destName.c_str(), static_cast<double>(e.pos[0]), static_cast<double>(e.pos[1]),
                       static_cast<double>(e.pos[2]));
                f.loadsAtStart = g_levelLoads.load();
                f.startedAt = t;
                drakan_levelchange::requestExit(e.handle);
                return;
            }
        const std::string want = levelNameForHash(f.destHash);
        netLog("level follow: giving up, no exit of this level leads to '%s' (hash %08x)",
               want.empty() ? "an unknown level" : want.c_str(), f.destHash);
        addChat(0, "", "Could not follow the leader to " + (want.empty() ? std::string("the next level") : want));
        f.active = false;
        return;
    }
    if (g_levelLoads.load() > f.loadsAtStart) {
        if (myLevel == 0)
            return; // a cut scene or still loading
        if (f.destHash == 0 || myLevel == f.destHash) {
            netLog("level follow: arrived in the leader's level (hash %08x)%s", myLevel,
                   f.destHash == 0 ? " (the message carried no destination to compare)" : "");
            f.active = false;
            return;
        }
        std::string have;
        {
            std::lock_guard<std::mutex> l(g_levelM);
            have = g_levelName;
        }
        const std::string want = levelNameForHash(f.destHash);
        netLog("level follow: MISMATCH, loaded '%s' (hash %08x) but the leader went to '%s' (hash %08x)", have.c_str(),
               myLevel, want.empty() ? "?" : want.c_str(), f.destHash);
        addChat(0, "", "Level mismatch: you are in " + have + ", the leader is in " + (want.empty() ? "?" : want));
        if (++f.attempt > kFollowMaxAttempts) {
            netLog("level follow: giving up after %d attempts", kFollowMaxAttempts);
            f.active = false;
            return;
        }
        f.retryAt = t + 4.0; // the exit list is scanned about once a second after a load
        return;
    }
    if (t - f.startedAt > kFollowTimeoutSec) {
        if (++f.attempt > kFollowMaxAttempts) {
            netLog("level follow: giving up, the exit never fired (%d attempts)", kFollowMaxAttempts);
            addChat(0, "", "Could not follow the leader through the level exit");
            f.active = false;
            return;
        }
        netLog("level follow: no level load %.0f s after the message, firing the exit again (attempt %d)",
               t - f.startedAt, f.attempt);
        f.startedAt = t;
        drakan_levelchange::followExit(f.exit[0], f.exit[1], f.exit[2]);
    }
}

// Where a partner who rides Arokh sits, in the dragon's own frame (world = R * local + position; measured from the
// local player's own mount, DRAKAN_DRAGON_PROBE=1 logs it): DRAKAN_DRAGON_RIDER_OFFSET=x,y,z overrides.
float g_riderOffset[3] = {0.0f, 330.0f, -40.0f};
float g_riderYaw = 0.0f; // DRAKAN_DRAGON_RIDER_YAW=<radians>: the rider's heading relative to the dragon
bool g_dragonProbe = false;
float g_dragonTestOffset[3] = {0, 0,
                               0}; // DRAKAN_DRAGON_TEST_OFFSET=x,y,z: draws a mounted partner this far off (test aid)
constexpr uint32_t kRiderRideClip = 0x5afu; // Rynn's "Ride" clip, low half (mapped to the stand-in's idle)

void loadDragonEnv() {
    static bool done = false;
    if (done)
        return;
    done = true;
    g_dragonProbe = std::getenv("DRAKAN_DRAGON_PROBE") != nullptr;
    if (const char* e = std::getenv("DRAKAN_DRAGON_RIDER_OFFSET"))
        std::sscanf(e, "%f,%f,%f", &g_riderOffset[0], &g_riderOffset[1], &g_riderOffset[2]);
    if (const char* e = std::getenv("DRAKAN_DRAGON_TEST_OFFSET"))
        std::sscanf(e, "%f,%f,%f", &g_dragonTestOffset[0], &g_dragonTestOffset[1], &g_dragonTestOffset[2]);
    if (const char* e = std::getenv("DRAKAN_DRAGON_RIDER_YAW"))
        g_riderYaw = static_cast<float>(std::atof(e));
}

// The pose of the rider body of a mounted partner: the dragon's transform plus the saddle offset, the dragon's
// orientation (turned by g_riderYaw) and the "Ride" clip.
world::PlayerPose riderPose(const world::PlayerPose& dragon) {
    world::PlayerPose b = dragon;
    b.mounted = false;
    const float* r = dragon.rotation;
    const float* o = g_riderOffset;
    b.position.x = dragon.position.x + r[0] * o[0] + r[1] * o[1] + r[2] * o[2];
    b.position.y = dragon.position.y + r[3] * o[0] + r[4] * o[1] + r[5] * o[2];
    b.position.z = dragon.position.z + r[6] * o[0] + r[7] * o[1] + r[8] * o[2];
    if (g_riderYaw != 0.0f) { // R * Ry(a): a turn about the dragon's own up axis
        const float c = std::cos(g_riderYaw), sn = std::sin(g_riderYaw);
        for (int row = 0; row < 3; ++row) {
            const float x = r[row * 3], z = r[row * 3 + 2];
            b.rotation[row * 3] = x * c - z * sn;
            b.rotation[row * 3 + 2] = x * sn + z * c;
        }
    }
    b.lower = b.upper = {(dragon.lower.clip & 0xffff0000u) | kRiderRideClip, 0.0f};
    b.lowerMask = ~0u;
    b.weaponRight = b.weaponLeft = 0;
    b.attacking = false;
    return b;
}

// DRAKAN_DRAGON_PROBE: the local player's rider seen from the dragon (the controlled actor is the dragon, its
// data+0x4d8 holds the rider's registry ref): offset in the dragon's frame and the heading difference, one line a
// second.
void probeRider(const uint8_t* rdram, const world::PlayerPose& dragon, double t) {
    static double last = -10;
    if (!g_dragonProbe || t - last < 1.0)
        return;
    last = t;
    const RdramMemory m(rdram);
    const auto f = [&](uint32_t a) {
        const uint32_t v = m.read32(a);
        float x;
        std::memcpy(&x, &v, 4);
        return x;
    };
    const uint32_t inner = m.read32(dragon.handle + 0xfc);
    const uint32_t data = m.read32(inner + 0x10);
    const uint32_t ref = m.read32(data + 0x4d8) & 0xffffu;
    const uint32_t ctx = m.read32(g_globals.contextPtr);
    const uint32_t table = m.read32(ctx + 0x7c);
    const uint32_t rider = ref && ref <= m.read32(ctx + 0x84) + 1 ? m.read32(table + (ref - 1) * 8u) : 0;
    if (!rider) {
        netLog("dragon probe: no rider (ref %x)", ref);
        return;
    }
    const float d[3] = {f(rider + 0x14) - f(dragon.handle + 0x14), f(rider + 0x18) - f(dragon.handle + 0x18),
                        f(rider + 0x1c) - f(dragon.handle + 0x1c)};
    const float* r = dragon.rotation;
    float local[3];
    for (int c = 0; c < 3; ++c)
        local[c] = r[c] * d[0] + r[3 + c] * d[1] + r[6 + c] * d[2];
    const float ryaw = std::atan2(f(rider + 0x28), f(rider + 0x20));
    const uint32_t sk = m.read32(rider + 0x50), ch = m.read32(sk + 0xe8);
    netLog("dragon probe: rider %x offset in the dragon frame %.1f %.1f %.1f, rider yaw %.3f (dragon yaw %.3f pitch "
           "%.3f roll %.3f), "
           "rider clip %x@%.3f, dragon clip %x@%.3f",
           rider, local[0], local[1], local[2], ryaw, dragon.yaw, dragon.pitch, dragon.roll, m.read32(ch + 0x50),
           f(ch + 0x58), dragon.lower.clip, dragon.lower.time);
}

void beforePadMgr(const uint8_t* rdram) {
    if (!g_enabled.load(std::memory_order_relaxed) || !rdram)
        return;
    loadDragonEnv();
    const double t = nowSec();
    const uint32_t myLevel = g_levelHash.load();
    serviceFollow(t, myLevel);
    const bool transition = drakan_lifecycle::transitionActive();

    // Local pose -> published slot.
    world::PlayerPose pose;
    world::PoseStatus status = world::PoseStatus::NoContext;
    if (!transition)
        status = world::readPlayerPose(RdramMemory(rdram), g_globals, g_poseState, t, ++g_tick, pose);
    const bool posed = status == world::PoseStatus::Ok && myLevel != 0 && !transition;
    if (posed && pose.mounted)
        probeRider(rdram, pose, t);
    if (g_trace && posed)
        std::fprintf(stderr, "[nettrace-local %.4f] %.2f %.2f %.2f yaw %.3f lower %x@%.3f\n", t, pose.position.x,
                     pose.position.y, pose.position.z, pose.yaw, pose.lower.clip, pose.lower.time);
    {
        std::lock_guard<std::mutex> l(g_s.m);
        if (posed) {
            if (pose.weaponRight != g_s.local.weaponRight || pose.weaponLeft != g_s.local.weaponLeft)
                netLog("local weapons: right %x left %x", pose.weaponRight, pose.weaponLeft);
            world::toPlayerState(pose, g_s.local);
            g_s.local.character = drakan_coop_character::selected();
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
        const bool want = posed && playing && sameLevel;
        if (g_trace && t - v.lastWantLog >= 2.0) { // one line per remote every 2 s: why a ghost is (not) wanted
            v.lastWantLog = t;
            std::fprintf(
                stderr, "[nettrace-want %.1f] p%u posed=%d playing=%d sameLevel=%d mounted=%d local=%08x remote=%08x\n",
                t, kv.first, posed, playing, sameLevel, r.state.mounted, myLevel, r.state.levelHash);
        }
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
                                  : "match not playing");
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
        if (p.mounted) {
            p.position.x += g_dragonTestOffset[0];
            p.position.y += g_dragonTestOffset[1];
            p.position.z += g_dragonTestOffset[2];
        }
        v.wasVisible = true;
        if (v.ghostId && !ghostExists(v.ghostId)) {
            // drakan_ghost drops a ghost whose spawn failed (e.g. a position outside the loaded level): back off, or
            // this respawns every frame.
            v.failures = std::min(v.failures + 1, 5);
            v.retryAt = t + std::min(2.0 * v.failures, 10.0);
            netLog("ghost of player %u vanished (failure %d); retry in %.0f s", kv.first, v.failures, v.retryAt - t);
            v.ghostId = 0;
        }
        if (v.ghostId && v.character != r.state.character) { // the partner picked another character: respawn it
            netLog("player %u is now character %x (was %x): ghost respawned", kv.first, r.state.character, v.character);
            drakan_ghost::destroy(v.ghostId);
            v.ghostId = 0;
            v.retryAt = t + 0.3;
        }
        // A mounted partner is two bodies: the dragon (the pose we receive is Arokh's) and the rider on it.
        const world::PlayerPose bodyPose = p.mounted ? riderPose(p) : p;
        if (!v.ghostId && t >= g_spawnNotBefore && t >= v.retryAt && drakan_ghost::ready()) {
            drakan_ghost::Transform tr;
            std::memcpy(tr.pos, &bodyPose.position, sizeof tr.pos);
            std::memcpy(tr.rot, bodyPose.rotation, sizeof tr.rot);
            v.character = r.state.character;
            v.ghostId = drakan_ghost::spawn(&tr, v.character);
            if (!v.ghostId) { // refused (engine not ready, transition): try again shortly
                v.retryAt = t + 0.5;
                continue;
            }
            netLog("ghost of player %u '%s' spawned (id %d) at %.0f %.0f %.0f", kv.first, r.name.c_str(), v.ghostId,
                   p.position.x, p.position.y, p.position.z);
        } else if (v.ghostId) {
            drakan_ghost::setPose(v.ghostId, bodyPose);
            if (g_trace)
                std::fprintf(stderr, "[nettrace %.4f] p%u %.2f %.2f %.2f yaw %.3f lower %x@%.3f upper %x@%.3f\n", t,
                             kv.first, p.position.x, p.position.y, p.position.z, p.yaw, p.lower.clip, p.lower.time,
                             p.upper.clip, p.upper.time);
        }
        // The partner's own Arokh: a visual dragon body replaying the received dragon pose and clips.
        if (v.dragonId && !ghostExists(v.dragonId)) {
            netLog("dragon of player %u vanished; retry in 2 s", kv.first);
            v.dragonId = 0;
            v.dragonRetryAt = t + 2.0;
        }
        if (p.mounted && v.ghostId) {
            if (!v.dragonId && t >= g_spawnNotBefore && t >= v.dragonRetryAt && drakan_ghost::ready()) {
                drakan_ghost::Transform tr;
                std::memcpy(tr.pos, &p.position, sizeof tr.pos);
                std::memcpy(tr.rot, p.rotation, sizeof tr.rot);
                v.dragonId = drakan_ghost::spawnDragon(&tr);
                if (!v.dragonId)
                    v.dragonRetryAt = t + 0.5;
                else
                    netLog("dragon of player %u spawned (id %d) at %.0f %.0f %.0f, clips %x", kv.first, v.dragonId,
                           p.position.x, p.position.y, p.position.z, p.lower.clip);
            } else if (v.dragonId) {
                drakan_ghost::setPose(v.dragonId, p);
            }
        } else if (v.dragonId) { // dismounted (or the body is gone): Arokh stays behind in the partner's world only
            drakan_ghost::destroy(v.dragonId);
            netLog("dragon of player %u removed (dismounted)", kv.first);
            v.dragonId = 0;
        }
        ++shown;
    }
    g_ghostsVisible.store(shown);
    {
        std::lock_guard<std::mutex> l(g_combatM);
        g_ghostToPlayer.clear();
        g_playerToGhost.clear();
        for (const auto& kv : g_views)
            if (kv.second.ghostId) {
                g_ghostToPlayer[kv.second.ghostId] = kv.first;
                g_playerToGhost[kv.first] = kv.second.ghostId;
            }
    }
}

} // namespace drakan_netplay
