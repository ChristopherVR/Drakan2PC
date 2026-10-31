// Drakan: co-op prototype (drakan_coop.h).
#include "drakan_coop.h"
#include "drakan_lifecycle.h"
#include "drakan_region.h"
#include "net/UdpTransport.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "world/Reader.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace drakan_coop {
namespace {

using Clock = std::chrono::steady_clock;
const Clock::time_point g_epoch = Clock::now();

double nowSec() {
    return std::chrono::duration<double>(Clock::now() - g_epoch).count();
}
uint64_t nowMs() {
    return static_cast<uint64_t>(nowSec() * 1000.0);
}

void coopLog(const char* fmt, ...) {
    char buf[320];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "[coop %.3f] %s\n", nowSec(), buf);
}

uint32_t fnv32(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h ? h : 1u;
}
uint32_t mixKey(uint32_t resId, uint32_t ordinal) {
    uint32_t h = 2166136261u;
    for (uint32_t v : {resId, ordinal})
        for (int i = 0; i < 4; ++i) {
            h ^= (v >> (8 * i)) & 0xffu;
            h *= 16777619u;
        }
    return h;
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

// ----------------------------------------------------------------------------------------------------- wire format
// Datagram (Unreliable channel): u8 type (1 = creature states), u32 levelHash, u32 seq, u16 count, count x Entry
// {u32 key, f32 pos[3], f32 yaw, i32 hp}. Little endian (the host layouts are written with memcpy; every supported
// target is little endian).
constexpr uint8_t kMsgCreatures = 1;
constexpr size_t kHeaderBytes = 1 + 4 + 4 + 2;
constexpr size_t kEntryBytes = 4 + 12 + 4 + 4;
constexpr size_t kEntriesPerPacket = (drakan::net::kMaxPacketPayload - kHeaderBytes) / kEntryBytes;
constexpr uint64_t kSendIntervalMs = 100; // 10 Hz

struct Entry {
    uint32_t key = 0;
    float pos[3] = {};
    float yaw = 0;
    int32_t hp = 0;
};

struct Remote {
    Entry e;
    uint64_t atMs = 0;
};

struct Shared {
    std::mutex m;
    std::vector<std::vector<uint8_t>> outbox;   // EE -> net (host)
    std::unordered_map<uint32_t, Remote> inbox; // net -> EE (client): key -> latest
    uint32_t inLevel = 0;
    uint64_t packets = 0, datagramsOut = 0;
};

Shared g_s;
std::atomic<bool> g_enabled{false};
std::atomic<bool> g_stop{false};
std::thread g_thread;
bool g_host = false, g_follow = true;
uint16_t g_port = 0;
std::string g_connectHost;
world::Globals g_globals;
uint32_t g_swallow = 0x4;
uint32_t g_handlerSlot = 0;

std::atomic<uint32_t> g_levelHash{0};
std::atomic<uint64_t> g_loadEpoch{0};

void netThread() {
    using namespace drakan::net;
    UdpTransport t;
    if (!t.available()) {
        coopLog("sockets unavailable; co-op off");
        return;
    }
    std::vector<PeerId> peers;
    if (g_host) {
        if (!t.listen(g_port)) {
            coopLog("cannot listen on UDP %u; co-op off", g_port);
            return;
        }
        coopLog("host: listening on UDP %u", t.localPort());
    }
    PeerId server = kInvalidPeer;
    uint64_t lastTry = 0;
    bool tried = false;
    while (!g_stop.load()) {
        const uint64_t now = nowMs();
        if (!g_host && server == kInvalidPeer && (!tried || now - lastTry > 2000)) {
            tried = true;
            lastTry = now;
            coopLog("connecting to %s:%u", g_connectHost.c_str(), g_port);
            server = t.connect(g_connectHost, g_port);
        }
        TransportEvent ev;
        while (t.poll(ev)) {
            if (ev.type == TransportEventType::Connected) {
                coopLog("peer %u connected", ev.peer);
                if (g_host)
                    peers.push_back(ev.peer);
            } else if (ev.type == TransportEventType::Disconnected) {
                coopLog("peer %u disconnected", ev.peer);
                peers.erase(std::remove(peers.begin(), peers.end(), ev.peer), peers.end());
                if (!g_host) {
                    server = kInvalidPeer;
                    tried = false;
                }
            } else if (!g_host && ev.data.size() >= kHeaderBytes && ev.data[0] == kMsgCreatures) {
                uint32_t level;
                uint16_t count;
                std::memcpy(&level, &ev.data[1], 4);
                std::memcpy(&count, &ev.data[9], 2);
                if (ev.data.size() != kHeaderBytes + size_t(count) * kEntryBytes)
                    continue;
                std::lock_guard<std::mutex> l(g_s.m);
                if (level != g_s.inLevel) {
                    g_s.inbox.clear(); // the host changed level: forget the old level's creatures
                    g_s.inLevel = level;
                }
                ++g_s.packets;
                for (uint16_t i = 0; i < count; ++i) {
                    const uint8_t* p = &ev.data[kHeaderBytes + size_t(i) * kEntryBytes];
                    Remote r;
                    std::memcpy(&r.e.key, p, 4);
                    std::memcpy(r.e.pos, p + 4, 12);
                    std::memcpy(&r.e.yaw, p + 16, 4);
                    std::memcpy(&r.e.hp, p + 20, 4);
                    r.atMs = now;
                    if (std::isfinite(r.e.pos[0]) && std::isfinite(r.e.pos[1]) && std::isfinite(r.e.pos[2]) &&
                        std::isfinite(r.e.yaw))
                        g_s.inbox[r.e.key] = r;
                }
            }
        }
        if (g_host) {
            std::vector<std::vector<uint8_t>> out;
            {
                std::lock_guard<std::mutex> l(g_s.m);
                out.swap(g_s.outbox);
            }
            for (const auto& d : out)
                for (PeerId p : peers)
                    if (t.send(p, Channel::Unreliable, d)) {
                        std::lock_guard<std::mutex> l(g_s.m);
                        ++g_s.datagramsOut;
                    }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void stopThread() {
    g_stop.store(true);
    if (g_thread.joinable())
        g_thread.join();
}

// ------------------------------------------------------------------------------------------------ guest side (EE)

struct Keyed {
    uint32_t key = 0;
    uint32_t resId = 0;
    uint32_t ref = 0;
    bool swapped = false;
    bool matched = false;
    float lastPos[3] = {};
    float err0 = 0;
};

std::unordered_map<uint32_t, Keyed> g_keyed; // handle -> key
std::unordered_map<uint32_t, uint32_t> g_keyToHandle;
bool g_keyedThisLevel = false;
uint32_t g_ticksSinceLoad = 0;
world::ReaderState g_rs;
world::Snapshot g_snap;
uint64_t g_lastHostSend = 0;
uint32_t g_seq = 0;
double g_lastTick = 0, g_lastStats = 0;

std::mutex g_origM;
std::unordered_map<uint32_t, uint32_t> g_orig; // actor -> original behaviour handler

struct Stats {
    uint32_t hostCreatures = 0, hostMoved = 0;
    uint32_t localKeyed = 0, matched = 0, unmatchedRemote = 0, swapped = 0, hostDead = 0;
    double errSum = 0, errMax = 0;
    uint32_t errN = 0, near100 = 0, near1000 = 0, far = 0;
    double curSum = 0, curMax = 0; // error right now (before this tick's correction) over the matched creatures
    uint32_t curOver200 = 0;
} g_st;

uint32_t rd32(const uint8_t* ram, uint32_t a) {
    a &= 0x1ffffffu;
    uint32_t v = 0;
    if (a + 4 <= PS2_RAM_SIZE)
        std::memcpy(&v, ram + a, 4);
    return v;
}
void wr32(uint8_t* ram, uint32_t a, uint32_t v) {
    a &= 0x1ffffffu;
    if (a + 4 <= PS2_RAM_SIZE)
        std::memcpy(ram + a, &v, 4);
}
void wrF(uint8_t* ram, uint32_t a, float v) {
    uint32_t u;
    std::memcpy(&u, &v, 4);
    wr32(ram, a, u);
}
float rdF(const uint8_t* ram, uint32_t a) {
    const uint32_t u = rd32(ram, a);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

// The proxy's behaviour handler: handler(actor, event). Swallowed events return 0; everything else (init, destroy,
// messages) goes to the creature's own handler so its objects are still built and torn down properly.
void proxyHandler(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t actor = getRegU32(c, 4), event = getRegU32(c, 5);
    uint32_t orig = 0;
    {
        std::lock_guard<std::mutex> l(g_origM);
        auto it = g_orig.find(actor);
        if (it != g_orig.end()) {
            orig = it->second;
            if (event == 2)
                g_orig.erase(it);
        }
    }
    const bool swallow = event && (event & (event - 1)) == 0 && (g_swallow & event);
    if (orig && !swallow) {
        if (auto fn = rt->lookupFunction(orig)) {
            c->pc = orig; // the handler resumes by pc: it was entered through our slot
            fn(rdram, c, rt);
            return;
        }
    }
    SET_GPR_U32(c, 2, 0u);
    c->pc = getRegU32(c, 31);
}

bool swapHandler(uint8_t* rdram, uint32_t actor) {
    const uint32_t inner = rd32(rdram, actor + 0xfc);
    if (!world::isRamPointer(inner))
        return false;
    const uint32_t cur = rd32(rdram, inner + 0x14);
    if (cur == g_handlerSlot)
        return true;
    if (!world::isRamPointer(cur))
        return false;
    {
        std::lock_guard<std::mutex> l(g_origM);
        g_orig[actor] = cur;
    }
    wr32(rdram, inner + 0x14, g_handlerSlot);
    return true;
}

void forgetLevel() {
    g_keyed.clear();
    g_keyToHandle.clear();
    g_keyedThisLevel = false;
    g_ticksSinceLoad = 0;
    g_st = Stats();
    std::lock_guard<std::mutex> l(g_origM);
    g_orig.clear(); // the pools are torn down with the level, every proxy is gone
}

// Registry order is slot (allocation) order, i.e. level list order, in both instances: the k-th creature of a resource
// id is taken to be the same placed creature on both sides. Handles that appear later (summons) are not synced.
void keyCreatures(const world::Snapshot& s) {
    std::unordered_map<uint32_t, uint32_t> counter;
    for (const auto& e : s.entities) {
        if (e.hpKind != world::HpKind::Creature || e.hpMax <= 0)
            continue;
        Keyed k;
        k.resId = e.resourceId;
        k.ref = e.ref;
        k.key = mixKey(e.resourceId, counter[e.resourceId]++);
        std::memcpy(k.lastPos, &e.position, 12);
        g_keyed[e.handle] = k;
        g_keyToHandle[k.key] = e.handle;
    }
    g_keyedThisLevel = true;
    coopLog("%s: keyed %zu creatures in '%s'", g_host ? "host" : "client", g_keyed.size(), s.levelName.c_str());
}

void hostTick(const uint8_t* rdram, uint32_t levelHash) {
    const uint64_t now = nowMs();
    if (now - g_lastHostSend < kSendIntervalMs)
        return;
    g_lastHostSend = now;
    if (world::readSnapshot(RdramMemory(rdram), g_globals, g_rs, g_snap) != world::ReadStatus::Ok)
        return;
    if (!g_keyedThisLevel)
        keyCreatures(g_snap);
    std::vector<Entry> entries;
    g_st.hostMoved = 0;
    for (const auto& e : g_snap.entities) {
        if (e.hpKind != world::HpKind::Creature)
            continue;
        auto it = g_keyed.find(e.handle);
        if (it == g_keyed.end() || it->second.resId != e.resourceId)
            continue; // created after keying, or a recycled handle
        Entry en;
        en.key = it->second.key;
        std::memcpy(en.pos, &e.position, 12);
        en.yaw = e.yaw;
        en.hp = e.hp;
        entries.push_back(en);
        const float d =
            std::fabs(e.position.x - it->second.lastPos[0]) + std::fabs(e.position.z - it->second.lastPos[2]);
        if (d > 50.0f)
            ++g_st.hostMoved;
    }
    g_st.hostCreatures = static_cast<uint32_t>(entries.size());
    std::vector<std::vector<uint8_t>> packets;
    ++g_seq;
    for (size_t i = 0; i < entries.size(); i += kEntriesPerPacket) {
        const uint16_t n = static_cast<uint16_t>(std::min(kEntriesPerPacket, entries.size() - i));
        std::vector<uint8_t> d(kHeaderBytes + size_t(n) * kEntryBytes);
        d[0] = kMsgCreatures;
        std::memcpy(&d[1], &levelHash, 4);
        std::memcpy(&d[5], &g_seq, 4);
        std::memcpy(&d[9], &n, 2);
        for (uint16_t j = 0; j < n; ++j) {
            uint8_t* p = &d[kHeaderBytes + size_t(j) * kEntryBytes];
            const Entry& en = entries[i + j];
            std::memcpy(p, &en.key, 4);
            std::memcpy(p + 4, en.pos, 12);
            std::memcpy(p + 16, &en.yaw, 4);
            std::memcpy(p + 20, &en.hp, 4);
        }
        packets.push_back(std::move(d));
    }
    std::lock_guard<std::mutex> l(g_s.m);
    g_s.outbox.insert(g_s.outbox.end(), packets.begin(), packets.end());
}

void clientTick(uint8_t* rdram, uint32_t levelHash, double dt) {
    std::unordered_map<uint32_t, Remote> in;
    uint32_t inLevel;
    {
        std::lock_guard<std::mutex> l(g_s.m);
        in = g_s.inbox;
        inLevel = g_s.inLevel;
    }
    if (in.empty() || inLevel != levelHash)
        return;
    if (!g_keyedThisLevel) {
        if (world::readSnapshot(RdramMemory(rdram), g_globals, g_rs, g_snap) != world::ReadStatus::Ok)
            return;
        keyCreatures(g_snap);
    }
    const RdramMemory mem(rdram);
    const float alpha = static_cast<float>(std::min(1.0, dt * 12.0));
    uint32_t matched = 0, swapped = 0, dead = 0, unmatched = 0, over200 = 0;
    double curSum = 0, curMax = 0;
    for (const auto& kv : in) {
        auto hi = g_keyToHandle.find(kv.first);
        if (hi == g_keyToHandle.end()) {
            ++unmatched;
            continue;
        }
        const uint32_t h = hi->second;
        Keyed& k = g_keyed[h];
        if (!world::isLiveHandle(mem, g_globals, h, k.ref - 1))
            continue; // destroyed locally (or recycled): leave it
        ++matched;
        const Entry& e = kv.second.e;
        const float pos[3] = {rdF(rdram, h + 0x14), rdF(rdram, h + 0x18), rdF(rdram, h + 0x1c)};
        const float err =
            std::sqrt((pos[0] - e.pos[0]) * (pos[0] - e.pos[0]) + (pos[2] - e.pos[2]) * (pos[2] - e.pos[2]));
        if (!k.matched) {
            k.matched = true;
            k.err0 = err;
            g_st.errSum += err;
            g_st.errMax = std::max<double>(g_st.errMax, err);
            ++g_st.errN;
            (err < 100 ? g_st.near100 : err < 1000 ? g_st.near1000 : g_st.far)++;
        }
        curSum += err;
        curMax = std::max<double>(curMax, err);
        over200 += err > 200.0f;
        if (e.hp <= 0)
            ++dead;
        if (!g_follow)
            continue;
        if (!k.swapped)
            k.swapped = swapHandler(rdram, h);
        if (!k.swapped)
            continue;
        ++swapped;
        const float a = err > 3000.0f ? 1.0f : alpha;
        for (int i = 0; i < 3; ++i)
            wrF(rdram, h + 0x14 + 4 * i, pos[i] + (e.pos[i] - pos[i]) * a);
        // Pure yaw rotation, the reader's convention (yaw = atan2(m[0][2], m[0][0])).
        const float cs = std::cos(e.yaw), sn = std::sin(e.yaw);
        const float rot[9] = {cs, 0, sn, 0, 1, 0, -sn, 0, cs};
        for (int i = 0; i < 9; ++i)
            wrF(rdram, h + 0x20 + 4 * i, rot[i]);
        const uint32_t inner = rd32(rdram, h + 0xfc);
        const uint32_t q = world::isRamPointer(inner) ? rd32(rdram, inner + world::handle::kInnerData) : 0;
        if (world::isRamPointer(q) && rd32(rdram, q + world::handle::kCreatureSelf) == h)
            wr32(rdram, q + world::handle::kCreatureHp, static_cast<uint32_t>(e.hp));
    }
    g_st.localKeyed = static_cast<uint32_t>(g_keyed.size());
    g_st.matched = matched;
    g_st.swapped = swapped;
    g_st.hostDead = dead;
    g_st.unmatchedRemote = unmatched;
    g_st.curSum = matched ? curSum / matched : 0.0;
    g_st.curMax = curMax;
    g_st.curOver200 = over200;
}

} // namespace

bool enabled() {
    return g_enabled.load();
}

void install(PS2Runtime& rt) {
    const char* host = std::getenv("DRAKAN_COOP_HOST");
    const char* conn = std::getenv("DRAKAN_COOP_CONNECT");
    const bool wantHost = host && *host, wantConn = conn && *conn;
    if (!wantHost && !wantConn)
        return;
    if (wantHost && wantConn) {
        coopLog("set only one of DRAKAN_COOP_HOST / DRAKAN_COOP_CONNECT");
        return;
    }
    if (!drakan_lifecycle::installed()) {
        coopLog("disabled: lifecycle hooks are not installed (unsupported disc profile)");
        return;
    }
    world::Globals g;
    g.contextPtr = drakan_region::guestAddress(g.contextPtr);
    g.hpKey = drakan_region::guestAddress(g.hpKey);
    g.playerStats = drakan_region::guestAddress(g.playerStats);
    g.handleVtable = drakan_region::guestAddress(g.handleVtable);
    if (!g.contextPtr || !g.hpKey || !g.playerStats || !g.handleVtable) {
        coopLog("disabled: no address translation for this disc profile");
        return;
    }
    g_globals = g;
    if (wantHost) {
        g_host = true;
        g_port = static_cast<uint16_t>(std::strtol(host, nullptr, 10));
    } else {
        const std::string c = conn;
        const size_t colon = c.rfind(':');
        if (colon == std::string::npos || colon == 0) {
            coopLog("bad DRAKAN_COOP_CONNECT '%s' (want host:port)", conn);
            return;
        }
        g_connectHost = c.substr(0, colon);
        g_port = static_cast<uint16_t>(std::strtol(c.c_str() + colon + 1, nullptr, 10));
    }
    if (!g_port) {
        coopLog("bad port");
        return;
    }
    if (const char* m = std::getenv("DRAKAN_COOP_MODE"))
        g_follow = std::string(m) != "mark";
    if (const char* s = std::getenv("DRAKAN_COOP_SWALLOW"))
        g_swallow = static_cast<uint32_t>(std::strtoul(s, nullptr, 16));
    for (uint32_t a = 0x100100u; a < 0x100600u && !g_handlerSlot; a += 4)
        if (!rt.hasFunction(a) && rt.registerFunction(a, &proxyHandler))
            g_handlerSlot = a;
    if (!g_handlerSlot) {
        coopLog("disabled: no free function slot");
        return;
    }
    using drakan_lifecycle::Kind;
    auto before = [](const drakan_lifecycle::Event&) {
        g_levelHash.store(0);
        ++g_loadEpoch;
    };
    drakan_lifecycle::subscribe(Kind::BeforeLevelLoad, before);
    drakan_lifecycle::subscribe(Kind::BeforeSaveRestore, before);
    drakan_lifecycle::subscribe(Kind::AfterLevelLoad, [](const drakan_lifecycle::Event& e) {
        if (!e.ok || e.level.empty())
            return;
        const bool playable = e.level.rfind("Level 0", 0) != 0; // the intro cut scene has no co-op state
        g_levelHash.store(playable ? fnv32(e.level) : 0);
        coopLog("level '%s' loaded (%s)", e.level.c_str(), playable ? "playable" : "cut scene, no co-op");
    });
    g_enabled.store(true);
    g_thread = std::thread(netThread);
    std::atexit(stopThread);
    coopLog("enabled: %s %s:%u, %s, swallow mask %x", g_host ? "host on" : "client of",
            g_host ? "0.0.0.0" : g_connectHost.c_str(), g_port, g_follow ? "follow" : "mark", g_swallow);
}

void beforePadMgr(uint8_t* rdram) {
    if (!g_enabled.load(std::memory_order_relaxed) || !rdram)
        return;
    static uint64_t epoch = ~0ull;
    if (epoch != g_loadEpoch.load()) {
        epoch = g_loadEpoch.load();
        forgetLevel();
    }
    const uint32_t level = g_levelHash.load();
    if (!level || drakan_lifecycle::transitionActive())
        return;
    const double t = nowSec();
    const double dt = std::min(0.25, t - g_lastTick);
    g_lastTick = t;
    constexpr uint32_t kSettleTicks = 150; // the engine is still creating its own actors right after a load
    if (++g_ticksSinceLoad < kSettleTicks)
        return;
    if (g_host)
        hostTick(rdram, level);
    else
        clientTick(rdram, level, dt);
    if (t - g_lastStats >= 5.0) {
        g_lastStats = t;
        if (g_host) {
            uint64_t out;
            {
                std::lock_guard<std::mutex> l(g_s.m);
                out = g_s.datagramsOut;
            }
            coopLog("host stats: %u creatures sent, %u moved since keying, %llu datagrams", g_st.hostCreatures,
                    g_st.hostMoved, static_cast<unsigned long long>(out));
        } else {
            uint64_t pk;
            {
                std::lock_guard<std::mutex> l(g_s.m);
                pk = g_s.packets;
            }
            coopLog("client stats: %llu packets, keyed %u, matched %u, swapped %u, host-dead %u, host-only %u; "
                    "initial error mean %.0f max %.0f (<100: %u, <1000: %u, far: %u); "
                    "now mean %.1f max %.0f, %u over 200",
                    static_cast<unsigned long long>(pk), g_st.localKeyed, g_st.matched, g_st.swapped, g_st.hostDead,
                    g_st.unmatchedRemote, g_st.errN ? g_st.errSum / g_st.errN : 0.0, g_st.errMax, g_st.near100,
                    g_st.near1000, g_st.far, g_st.curSum, g_st.curMax, g_st.curOver200);
        }
    }
}

} // namespace drakan_coop
