// Drakan: co-op prototype (drakan_coop.h).
#include "drakan_coop.h"
#include "drakan_ghost.h"
#include "drakan_lifecycle.h"
#include "drakan_region.h"
#include "input/GuestInterface.h"
#include "net/UdpTransport.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "world/Reader.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <set>
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
// Every datagram starts with u8 type, u32 levelHash (little endian, memcpy'd: every supported target is little endian).
//   1 creature states (Unreliable, host -> client): u32 seq, u16 count, count x {u32 key, f32 pos[3], f32 yaw, i32 hp}
//   2 creature died   (Reliable, host -> client):    u32 key
//   3 hit claim       (Reliable, client -> host):    u32 key, i32 damage
//   4 player hurt     (Reliable, host -> client):    i32 damage          (a host creature hit the client's ghost)
//   5 object gone     (Reliable, both ways):         u32 key             (a pickup was taken: the other side removes
//   it) 6 object message  (Reliable, both ways):         u32 key, u16 type, i32 w[4]   (a door / lever message to
//   replay)
//   7 player died     (Reliable, client -> host):    (empty) the client's player is down: its ghost stops being a
//   target 8 player respawn  (Reliable, host -> client):    (empty) get up at the host 9 object state    (Reliable,
//   both ways):         u32 key, i32 state   (a chest / lever changed state)
//  10 sync request    (Reliable, client -> host):    (empty) the client keyed the level: send the catch-up state
constexpr uint8_t kMsgCreatures = 1, kMsgDied = 2, kMsgClaim = 3, kMsgHurt = 4, kMsgObjGone = 5, kMsgObjMsg = 6,
                  kMsgPlayerDied = 7, kMsgRespawn = 8, kMsgObjState = 9, kMsgSyncReq = 10, kMsgLast = 10;
constexpr uint64_t kRespawnDelayMs = 5000;     // the death clip runs about 4.7 s
constexpr size_t kHeaderBytes = 1 + 4 + 4 + 2; // creature datagram header (type, level, seq, count)
constexpr size_t kEventHeader = 1 + 4;         // type, level
constexpr size_t kEntryBytes = 4 + 12 + 4 + 4;
constexpr size_t kEntriesPerPacket = (drakan::net::kMaxPacketPayload - kHeaderBytes) / kEntryBytes;
constexpr uint64_t kSendIntervalMs = 100; // 10 Hz
constexpr uint16_t kMsgTaken = 0x10c1;    // an item's message to itself when it is picked up
constexpr uint16_t kMsgDamage = 0x10db;   // the engine's damage message (type word at +0, damage at +0x10)

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
    std::vector<std::vector<uint8_t>> outbox;   // EE -> net: unreliable creature datagrams (host)
    std::vector<std::vector<uint8_t>> relOut;   // EE / hook -> net: reliable events
    std::vector<std::vector<uint8_t>> evIn;     // net -> EE: reliable events
    std::unordered_map<uint32_t, Remote> inbox; // net -> EE (client): key -> latest
    uint32_t inLevel = 0;
    uint64_t packets = 0, datagramsOut = 0, eventsOut = 0, eventsIn = 0;
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
uint32_t g_handlerSlot = 0, g_aiSlot = 0, g_chestSlot = 0;
PS2Runtime* g_rt = nullptr;
PS2Runtime::RecompiledFunction g_origSend = nullptr;
bool g_probe = false, g_killDestroy = false;
float g_damageScale = 1.0f;       // test aid: multiplies the damage of the local player's hits on creatures (host)
std::set<uint16_t> g_replayTypes; // object message types that are replayed on the other side
bool g_retarget = true;
std::atomic<bool> g_aiSwapActive{false}; // host: a creature tick is running with the body on the ghost
std::atomic<bool> g_noClaim{false};      // set while the client posts a message of its own to a proxy
std::set<uint32_t> g_forced;             // test aid: creatures that always pick the ghost
bool g_forceAll = false;                 // ... or every creature in range
bool g_forceGhost = false;               // test aid: host creatures always pick the partner's ghost when one exists
float g_hurtScale = 1.0f;                // test aid (client): multiplies the damage taken from host enemies
uint32_t g_deadSlot = 0;                 // function slot of deadHandler (the client's player while it is down)
std::atomic<uint32_t> g_savedHandler{0}; // the player's behaviour handler while the death is shown

std::atomic<uint32_t> g_levelHash{0};
std::atomic<uint64_t> g_loadEpoch{0};

void pushReliable(uint8_t type, const void* payload, size_t n) {
    const uint32_t level = g_levelHash.load();
    if (!level)
        return;
    std::vector<uint8_t> d(kEventHeader + n);
    d[0] = type;
    std::memcpy(&d[1], &level, 4);
    if (n)
        std::memcpy(&d[kEventHeader], payload, n);
    std::lock_guard<std::mutex> l(g_s.m);
    g_s.relOut.push_back(std::move(d));
}

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
    bool connected = false;
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
                else
                    connected = true;
            } else if (ev.type == TransportEventType::Disconnected) {
                coopLog("peer %u disconnected", ev.peer);
                peers.erase(std::remove(peers.begin(), peers.end(), ev.peer), peers.end());
                if (!g_host) {
                    server = kInvalidPeer;
                    tried = false;
                    connected = false;
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
            } else if (ev.data.size() >= kEventHeader && ev.data[0] >= kMsgDied && ev.data[0] <= kMsgLast) {
                std::lock_guard<std::mutex> l(g_s.m);
                g_s.evIn.push_back(std::move(ev.data));
                ++g_s.eventsIn;
            }
        }
        std::vector<std::vector<uint8_t>> out, rel;
        {
            std::lock_guard<std::mutex> l(g_s.m);
            out.swap(g_s.outbox);
            rel.swap(g_s.relOut);
        }
        if (g_host) {
            for (const auto& d : out)
                for (PeerId p : peers)
                    if (t.send(p, Channel::Unreliable, d)) {
                        std::lock_guard<std::mutex> l(g_s.m);
                        ++g_s.datagramsOut;
                    }
            for (const auto& d : rel)
                for (PeerId p : peers)
                    if (t.send(p, Channel::Reliable, d)) {
                        std::lock_guard<std::mutex> l(g_s.m);
                        ++g_s.eventsOut;
                    }
        } else if (connected && server != kInvalidPeer) {
            for (const auto& d : rel)
                if (t.send(server, Channel::Reliable, d)) {
                    std::lock_guard<std::mutex> l(g_s.m);
                    ++g_s.eventsOut;
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
    bool dying = false;    // client: the host reported the death, the creature runs its own death now
    bool deadSent = false; // host: the death was reported
    uint32_t dyingTicks = 0;
    bool destroyed = false;
    bool killSent = false; // client: the lethal damage message was posted to the proxy
    uint64_t dyingSinceMs = 0;
    uint32_t absentCount = 0; // host: consecutive refreshes the creature was missing from the registry
    uint64_t lastClaimMs = 0;
    float lastPos[3] = {};
    float err0 = 0;
};

// A watched placed object (door, pickup): keyed like the creatures, by (class, resource, ordinal).
struct Obj {
    uint32_t key = 0, resId = 0, classId = 0, type = 0, ref = 0;
    bool pickup = false, gone = false, swapped = false;
    float pos0[3] = {}; // where it lay when it was keyed (a loose item that is taken is moved to the inventory)
};

std::unordered_map<uint32_t, Keyed> g_keyed; // handle -> key
std::unordered_map<uint32_t, uint32_t> g_keyToHandle;
std::unordered_map<uint32_t, Obj> g_obj;
std::unordered_map<uint32_t, uint32_t> g_objKeyToHandle;
std::set<uint32_t> g_goneKeys;                       // pickups taken this level (either side): the late-join catch-up
std::set<uint32_t> g_forceOpen;                      // client: doors (handles) the host reported open at the catch-up
bool g_syncPending = false, g_syncSent = false;      // host: a client asked for the catch-up; client: it asked
std::unordered_map<uint32_t, uint32_t> g_dynCounter; // (class, resource) -> pickups keyed after the level load
bool g_keyedThisLevel = false;
uint32_t g_ticksSinceLoad = 0, g_tickTotal = 0;
world::ReaderState g_rs;
world::Snapshot g_snap;
uint64_t g_lastHostSend = 0, g_lastRefresh = 0;
uint32_t g_seq = 0;
double g_lastTick = 0, g_lastStats = 0;

float g_moveDir[2] = {};
bool g_haveMoveDir = false;
const uint8_t* g_rdramForLog = nullptr; // set each boundary (probe logging only)
std::mutex g_origM;
std::unordered_map<uint32_t, uint32_t> g_orig; // actor -> original behaviour handler

struct Claim {
    uint32_t key;
    int32_t damage;
};
std::deque<Claim> g_claims; // host: validated-on-apply claims from the client
int32_t g_pendingHurt = 0;  // client: damage taken from host enemies, applied at the next boundary
std::mutex g_hookM;
std::deque<std::vector<uint8_t>> g_objEvents;      // replays waiting for the guest (EE thread)
std::unordered_map<uint64_t, uint64_t> g_suppress; // (key, type) -> until ms: a replayed message must not echo
std::mutex g_supM;

struct Stats {
    uint32_t hostCreatures = 0, hostMoved = 0;
    uint32_t localKeyed = 0, matched = 0, unmatchedRemote = 0, swapped = 0, hostDead = 0;
    double errSum = 0, errMax = 0;
    uint32_t errN = 0, near100 = 0, near1000 = 0, far = 0;
    double curSum = 0, curMax = 0; // error right now (before this tick's correction) over the matched creatures
    uint32_t curOver200 = 0;
    uint32_t deathsSent = 0, deathsRecv = 0, claimsSent = 0, claimsApplied = 0, claimsRejected = 0, hurtSent = 0,
             hurtApplied = 0, goneSent = 0, goneRecv = 0, msgSent = 0, msgRecv = 0, aiSwaps = 0, aiTicks = 0,
             aiGhostTicks = 0, refiled = 0, hurtSwallowed = 0;
} g_st;

uint32_t rd32(const uint8_t* ram, uint32_t a) {
    a &= 0x1ffffffu;
    uint32_t v = 0;
    if (a + 4 <= PS2_RAM_SIZE)
        std::memcpy(&v, ram + a, 4);
    return v;
}
uint16_t rd16(const uint8_t* ram, uint32_t a) {
    a &= 0x1ffffffu;
    uint16_t v = 0;
    if (a + 2 <= PS2_RAM_SIZE)
        std::memcpy(&v, ram + a, 2);
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
bool ramPtr(uint32_t p) {
    return world::isRamPointer(p);
}

uint32_t engineCtx(const uint8_t* rdram) {
    return rd32(rdram, g_globals.contextPtr);
}
uint32_t controlled(const uint8_t* rdram) {
    const uint32_t ctx = engineCtx(rdram);
    return ramPtr(ctx) ? rd32(rdram, ctx + 0x4b0) : 0;
}
uint32_t creatureData(const uint8_t* rdram, uint32_t h) {
    const uint32_t inner = rd32(rdram, h + 0xfc);
    const uint32_t q = ramPtr(inner) ? rd32(rdram, inner + world::handle::kInnerData) : 0;
    return ramPtr(q) && rd32(rdram, q + world::handle::kCreatureSelf) == h ? q : 0;
}
int32_t creatureHp(const uint8_t* rdram, uint32_t h) {
    const uint32_t q = creatureData(rdram, h);
    return q ? static_cast<int32_t>(rd32(rdram, q + world::handle::kCreatureHp)) : 0;
}
void setCreatureHp(uint8_t* rdram, uint32_t h, int32_t hp) {
    if (const uint32_t q = creatureData(rdram, h))
        wr32(rdram, q + world::handle::kCreatureHp, static_cast<uint32_t>(hp));
}

// The weapon actors in the local player's hands: the damage message's sender is the weapon actor of the attacker, not
// the attacker (right holder at data+0xeb4, left at +0xe90, the holder's actor ref at +0x1c; registry slot = ref - 1).
uint32_t registryObject(const uint8_t* rdram, uint32_t ref) {
    const uint32_t ctx = engineCtx(rdram);
    if (!ramPtr(ctx) || !ref || ref > rd32(rdram, ctx + 0x84) + 1)
        return 0;
    return rd32(rdram, rd32(rdram, ctx + 0x7c) + (ref - 1) * 8);
}
bool isLocalAttacker(const uint8_t* rdram, uint32_t sender) {
    const uint32_t me = controlled(rdram);
    if (!sender || !ramPtr(me))
        return false;
    if (sender == me)
        return true;
    const uint32_t inner = rd32(rdram, me + 0xfc);
    const uint32_t data = ramPtr(inner) ? rd32(rdram, inner + 0x10) : 0;
    if (!ramPtr(data))
        return false;
    for (uint32_t holder : {0xeb4u, 0xe90u})
        if (registryObject(rdram, rd16(rdram, data + holder + 0x1c)) == sender)
            return true;
    return false;
}

// Remote players: the ghosts drakan_netplay keeps (any ghost is a partner).
std::vector<uint32_t> ghostHandles() {
    std::vector<uint32_t> v;
    drakan_ghost::forEach([&](const drakan_ghost::Info& i) {
        if (i.handle)
            v.push_back(i.handle);
    });
    return v;
}
bool isGhostHandle(uint32_t h) {
    bool f = false;
    drakan_ghost::forEach([&](const drakan_ghost::Info& i) {
        if (i.handle && i.handle == h)
            f = true;
    });
    return f;
}

// ---------------------------------------------------------------------------------------- the client's player life
// The engine's own death (HP 0) hides the body and raises the single-player game over ("load the last saved game"),
// which is wrong for a session (pvp.md found the same). So a co-op death keeps HP at 1: the player's behaviour handler
// is swapped for one that swallows everything but the destroy, and Rynn's death clip is written into her skeleton every
// tick. The host sees it through the replicated pose of the ghost. The host answers with a respawn after a delay and
// the player gets up beside the host (or where she fell when the host is too far for the floor to be resident).
constexpr uint32_t kStatsBlock = 0x4fbac8u, kDeathClip = 0x20577u;
constexpr float kDeathClipHold = 4.0f;

struct Life {
    bool dead = false, respawnReq = false, checking = false;
    uint32_t actor = 0, checkTicks = 0;
    float clipTime = 0, target[3] = {};
    int pendingHp = -1; // HP to write once no posted engine call is in flight
    uint64_t diedMs = 0;
} g_life;
bool g_partnerDead = false; // host: the client's player is down (its ghost is not a target)
uint64_t g_partnerDeadMs = 0;

uint32_t rotr8(uint32_t x) {
    return (x >> 8) | (x << 24);
}
uint32_t rotl8(uint32_t x) {
    return (x << 8) | (x >> 24);
}
int32_t playerHp(const uint8_t* rdram) {
    return static_cast<int32_t>(
        rotr8(rd32(rdram, drakan_region::guestAddress(kStatsBlock) + 0xc) ^ rd32(rdram, g_globals.hpKey)));
}
int32_t playerHpMax(const uint8_t* rdram) {
    return static_cast<int32_t>(
        rotr8(rd32(rdram, drakan_region::guestAddress(kStatsBlock) + 0x10) ^ rd32(rdram, g_globals.hpKey)));
}
void setPlayerHp(uint8_t* rdram, int32_t hp) {
    wr32(rdram, drakan_region::guestAddress(kStatsBlock) + 0xc,
         rotl8(static_cast<uint32_t>(hp)) ^ rd32(rdram, g_globals.hpKey));
}

void deadHandler(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    if (getRegU32(c, 5) == 2u && g_savedHandler.load()) { // destroy: the real handler releases her sub-objects
        c->pc = g_savedHandler.load();
        rt->lookupFunction(c->pc)(rdram, c, rt);
        return;
    }
    SET_GPR_U32(c, 2, 0u);
    c->pc = getRegU32(c, 31);
}

void startDeath(uint8_t* rdram) {
    const uint32_t actor = controlled(rdram);
    const uint32_t inner = ramPtr(actor) ? rd32(rdram, actor + 0xfc) : 0;
    if (!ramPtr(inner) || !g_deadSlot)
        return;
    g_life = Life();
    g_life.dead = true;
    g_life.actor = actor;
    g_life.diedMs = nowMs();
    g_savedHandler.store(rd32(rdram, inner + 0x14));
    wr32(rdram, inner + 0x14, g_deadSlot);
    setPlayerHp(rdram, 1);
    pushReliable(kMsgPlayerDied, nullptr, 0);
    coopLog("client: the local player died (host enemies): frozen with the death clip, waiting for the host's respawn");
}

void endDeath(uint8_t* rdram) {
    const uint32_t actor = controlled(rdram);
    const uint32_t inner = ramPtr(actor) && actor == g_life.actor ? rd32(rdram, actor + 0xfc) : 0;
    if (ramPtr(inner) && g_savedHandler.load())
        wr32(rdram, inner + 0x14, g_savedHandler.load());
    g_savedHandler.store(0);
}

// Client, every boundary: keeps the death clip going, gets the player up on the host's respawn, writes the HP that
// had to wait for the engine's posted damage.
void tickLife(drakan_input::IGuest& guest, uint8_t* rdram) {
    const uint32_t me = controlled(rdram);
    if (g_life.dead && me == g_life.actor) {
        float dt = guest.readF(engineCtx(rdram) + 0x24);
        if (!(dt > 0.0f && dt < 0.25f))
            dt = 1.0f / 60.0f;
        g_life.clipTime += dt;
        const uint32_t sk = rd32(rdram, me + 0x50);
        const uint32_t ch = ramPtr(sk) ? rd32(rdram, sk + 0xe8) : 0;
        const uint32_t n = ramPtr(sk) ? rd32(rdram, sk + 0xf0) : 0;
        if (ramPtr(ch) && n && n <= 32)
            for (uint32_t i = 0; i < n; ++i) {
                guest.write32(ch + i * 0x80u + 0x50u, kDeathClip);
                guest.writeF(ch + i * 0x80u + 0x58u, std::min(g_life.clipTime, kDeathClipHold));
            }
        if (g_life.respawnReq && ramPtr(me)) {
            endDeath(rdram);
            const auto gs = ghostHandles();
            if (!gs.empty()) {
                const uint32_t g = gs.front();
                g_life.target[0] = rdF(rdram, g + 0x14) + 200.0f;
                g_life.target[1] = rdF(rdram, g + 0x18) + 20.0f;
                g_life.target[2] = rdF(rdram, g + 0x1c);
                for (int i = 0; i < 3; ++i)
                    guest.writeF(me + 0x14 + 4u * i, g_life.target[i]);
                drakan_ghost::refileActor(guest, me);
                g_life.checking = true;
                g_life.checkTicks = 0;
            }
            g_life.pendingHp = std::max(1, playerHpMax(rdram));
            g_life.dead = false;
            g_life.respawnReq = false;
            coopLog("client: respawned %s (HP -> %d)",
                    gs.empty() ? "where she fell (no host ghost)" : "beside the host", g_life.pendingHp);
        }
    } else if (g_life.dead && me != g_life.actor) {
        g_life.dead = false; // the controlled actor changed (a level load, the inventory): the death is over
        g_savedHandler.store(0);
    }
    if (g_life.checking && ++g_life.checkTicks == 90) {
        g_life.checking = false;
        if (ramPtr(me)) {
            const float d =
                std::hypot(rdF(rdram, me + 0x14) - g_life.target[0], rdF(rdram, me + 0x1c) - g_life.target[2]);
            coopLog("client: 90 ticks after the respawn the player is %.0f units from the host spot (%s)", d,
                    d < 600 ? "stayed" : "put back by the engine");
        }
    }
    if (g_life.pendingHp >= 0 && !g_life.dead && !guest.postBusy()) {
        setPlayerHp(rdram, g_life.pendingHp);
        g_life.pendingHp = -1;
    }
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

// Host side: the creature's own handler, but its per-tick event runs with the local player's body moved onto the
// partner's ghost (the controlled actor ctx+0x4b0 is what every AI perception reads, 135 readers) when the ghost is
// clearly closer. Every other event passes through untouched.
std::unordered_map<uint32_t, uint32_t> g_aiLast; // creature -> ghost it last picked (0 = the local player)
uint32_t g_aiLogged = 0;

void aiHandler(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
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
    auto fn = orig ? rt->lookupFunction(orig) : nullptr;
    if (!fn) {
        SET_GPR_U32(c, 2, 0u);
        c->pc = getRegU32(c, 31);
        return;
    }
    uint32_t alt = 0, ctxAddr = 0, saved = 0;
    if (event == 4 && g_retarget) {
        ++g_st.aiTicks;
        ctxAddr = engineCtx(rdram);
        saved = ramPtr(ctxAddr) ? rd32(rdram, ctxAddr + 0x4b0) : 0;
        if (ramPtr(saved)) {
            const float cp[3] = {rdF(rdram, actor + 0x14), rdF(rdram, actor + 0x18), rdF(rdram, actor + 0x1c)};
            auto d2 = [&](uint32_t h) {
                float s = 0;
                for (int i = 0; i < 3; ++i) {
                    const float d = rdF(rdram, h + 0x14 + 4 * i) - cp[i];
                    s += d * d;
                }
                return s;
            };
            const float dp = d2(saved);
            float best = 1e30f;
            uint32_t bestG = 0;
            for (uint32_t g : ghostHandles()) {
                if (!ramPtr(rd32(rdram, g + 0xfc)) || g == saved || g_partnerDead)
                    continue;
                const float dg = d2(g);
                if (dg < best) {
                    best = dg;
                    bestG = g;
                }
            }
            const uint32_t last = g_aiLast.count(actor) ? g_aiLast[actor] : 0;
            // 20 percent hysteresis on the distance so two equidistant players do not flip every tick.
            // Only a partner within aggro range counts (a far ghost would send the AI after a target it cannot reach).
            constexpr float kRange2 = 6000.0f * 6000.0f;
            const bool pickGhost = bestG && best < kRange2 &&
                                   ((g_forceGhost && (g_forceAll || g_forced.count(actor))) ||
                                    (last == bestG ? best < dp * 1.44f : best < dp * 0.64f));
            alt = pickGhost ? bestG : 0;
            if (alt != last) {
                g_aiLast[actor] = alt;
                ++g_st.aiSwaps;
                if (g_aiLogged++ < 24)
                    coopLog("host: creature %08x now targets %s (ghost %.0f, player %.0f units)", actor,
                            alt ? "the partner's ghost" : "the local player", bestG ? std::sqrt(best) : -1.0,
                            std::sqrt(dp));
            }
            if (alt)
                ++g_st.aiGhostTicks;
        }
    }
    // A door the host reported open (late join catch-up): its tick sees "the player" standing at the door.
    if (event == 4 && !g_forceOpen.empty() && g_forceOpen.count(actor)) {
        ctxAddr = engineCtx(rdram);
        saved = ramPtr(ctxAddr) ? rd32(rdram, ctxAddr + 0x4b0) : 0;
        if (ramPtr(saved))
            alt = actor;
    }
    // The tick runs with the local player's body standing where the partner's ghost is (position and rotation, 0x30
    // bytes at +0x14) and put back afterwards. Swapping the controlled actor itself crashed the guest: the ghost's data
    // object is never initialised (its handler swallows the init event), and the AI's attack code reads it.
    uint32_t backup[12] = {};
    if (alt) {
        for (int i = 0; i < 12; ++i) {
            backup[i] = rd32(rdram, saved + 0x14 + 4 * i);
            wr32(rdram, saved + 0x14 + 4 * i, rd32(rdram, alt + 0x14 + 4 * i));
        }
    }
    c->pc = orig;
    if (alt)
        g_aiSwapActive = true;
    fn(rdram, c, rt);
    if (alt) {
        g_aiSwapActive = false;
        for (int i = 0; i < 12; ++i)
            wr32(rdram, saved + 0x14 + 4 * i, backup[i]);
    }
}

// A chest's own handler with a log of every event but the per-tick one (probe: what does opening a chest send?).
void chestHandler(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
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
    auto fn = orig ? rt->lookupFunction(orig) : nullptr;
    if (!fn) {
        SET_GPR_U32(c, 2, 0u);
        c->pc = getRegU32(c, 31);
        return;
    }
    static uint32_t budget = 200;
    if (g_probe && event != 4 && budget) {
        --budget;
        const uint32_t a2 = getRegU32(c, 6), a3 = getRegU32(c, 7);
        uint32_t w[6] = {};
        if (ramPtr(a2))
            for (int i = 0; i < 6; ++i)
                w[i] = rd32(rdram, a2 + 4 * i);
        coopLog("chest %08x handler event %x a2 %08x a3 %08x [%08x %08x %08x %08x %08x %08x]", actor, event, a2, a3,
                w[0], w[1], w[2], w[3], w[4], w[5]);
    }
    c->pc = orig;
    fn(rdram, c, rt);
}

bool swapHandlerTo(uint8_t* rdram, uint32_t actor, uint32_t slot) {
    const uint32_t inner = rd32(rdram, actor + 0xfc);
    if (!ramPtr(inner) || !slot)
        return false;
    const uint32_t cur = rd32(rdram, inner + 0x14);
    if (cur == slot)
        return true;
    if (!ramPtr(cur))
        return false;
    {
        std::lock_guard<std::mutex> l(g_origM);
        g_orig[actor] = cur;
    }
    wr32(rdram, inner + 0x14, slot);
    return true;
}

// Puts the creature's own handler back (it stays remembered until its destroy event).
bool restoreHandler(uint8_t* rdram, uint32_t actor) {
    uint32_t orig = 0;
    {
        std::lock_guard<std::mutex> l(g_origM);
        auto it = g_orig.find(actor);
        if (it == g_orig.end())
            return false;
        orig = it->second;
    }
    const uint32_t inner = rd32(rdram, actor + 0xfc);
    if (!ramPtr(inner))
        return false;
    wr32(rdram, inner + 0x14, orig);
    return true;
}

void forgetLevel() {
    g_keyed.clear();
    g_keyToHandle.clear();
    g_obj.clear();
    g_objKeyToHandle.clear();
    g_keyedThisLevel = false;
    g_ticksSinceLoad = 0;
    g_st = Stats();
    g_claims.clear();
    g_pendingHurt = 0;
    g_life = Life();
    g_savedHandler.store(0);
    g_partnerDead = false;
    g_goneKeys.clear();
    g_forceOpen.clear();
    g_syncPending = g_syncSent = false;
    g_aiLast.clear();
    g_forced.clear();
    g_dynCounter.clear();
    {
        std::lock_guard<std::mutex> l(g_supM);
        g_suppress.clear();
    }
    {
        std::lock_guard<std::mutex> l(g_hookM);
        g_objEvents.clear();
    }
    {
        std::lock_guard<std::mutex> l(g_s.m);
        g_s.evIn.clear();
        g_s.relOut.clear();
    }
    std::lock_guard<std::mutex> l(g_origM);
    g_orig.clear(); // the pools are torn down with the level, every proxy is gone
}

// Items the engine keeps "parked" at one spot (the player's inventory objects, all at the same position) are not world
// pickups: a position shared by more than three type 11 objects is parked.
std::set<std::array<int, 3>> g_parked;
std::array<int, 3> posKey(const world::EntitySnapshot& e) {
    return {static_cast<int>(e.position.x), static_cast<int>(e.position.y), static_cast<int>(e.position.z)};
}
void refreshParked(const world::Snapshot& s) {
    std::map<std::array<int, 3>, int> n;
    for (const auto& e : s.entities)
        if (e.type == 11)
            ++n[posKey(e)];
    g_parked.clear();
    for (const auto& [k, c] : n)
        if (c > 3)
            g_parked.insert(k);
}
constexpr uint32_t kChestClass = 0x100c5u;
bool isPickupEntity(const world::EntitySnapshot& e) {
    // Treasure (gold sacks, goblets) and the other loose items (type 11: elixirs, potions, ...); a taken one leaves the
    // registry.
    if (e.classId == 0x1006du)
        return true;
    // Loose items; the inventory's own copies are parked near the origin of the world (the levels lie at 1e5..1e6).
    const bool nearOrigin = std::fabs(e.position.x) < 20000.0f && std::fabs(e.position.z) < 20000.0f;
    return e.type == 11 && !g_parked.count(posKey(e)) && !nearOrigin;
}
bool isWatchedEntity(const world::EntitySnapshot& e) {
    return e.classId == 0x10013u /* doors */ || e.classId == kChestClass || isPickupEntity(e);
}

// Registry order is slot (allocation) order, i.e. level list order, in both instances: the k-th creature of a resource
// id is taken to be the same placed creature on both sides. Handles that appear later (summons) are not synced.
void keyCreatures(const world::Snapshot& s) {
    refreshParked(s);
    std::unordered_map<uint32_t, uint32_t> counter, objCounter;
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> hist;
    for (const auto& e : s.entities) {
        if (e.hpKind == world::HpKind::Creature && e.hpMax > 0) {
            Keyed k;
            k.resId = e.resourceId;
            k.ref = e.ref;
            k.key = mixKey(e.resourceId, counter[e.resourceId]++);
            std::memcpy(k.lastPos, &e.position, 12);
            g_keyed[e.handle] = k;
            g_keyToHandle[k.key] = e.handle;
        } else if (isWatchedEntity(e)) {
            Obj o;
            o.resId = e.resourceId;
            o.classId = e.classId;
            o.type = e.type;
            o.ref = e.ref;
            o.pickup = isPickupEntity(e);
            std::memcpy(o.pos0, &e.position, 12);
            o.key = mixKey(e.resourceId * 2654435761u ^ e.classId, objCounter[e.resourceId ^ e.classId]++);
            g_obj[e.handle] = o;
            g_objKeyToHandle[o.key] = e.handle;
            ++hist[{e.classId, e.type}];
        }
    }
    g_keyedThisLevel = true;
    coopLog("%s: keyed %zu creatures and %zu watched objects in '%s'", g_host ? "host" : "client", g_keyed.size(),
            g_obj.size(), s.levelName.c_str());
    for (const auto& [ct, n] : hist)
        coopLog("  watched class %x type %u: %u", ct.first, ct.second, n);
    if (g_probe) {
        // The chests and pickups nearest to the local player (where to walk in a test).
        const uint32_t me = controlled(g_rdramForLog);
        std::vector<std::pair<float, const world::EntitySnapshot*>> near;
        for (const auto& e : s.entities)
            if (ramPtr(me) && (e.classId == kChestClass || isPickupEntity(e)))
                near.push_back({std::hypot(e.position.x - rdF(g_rdramForLog, me + 0x14),
                                           e.position.z - rdF(g_rdramForLog, me + 0x1c)),
                                &e});
        std::sort(near.begin(), near.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (size_t i = 0; i < near.size() && i < 10; ++i)
            coopLog("  near: class %x type %u res %x at %.0f %.0f %.0f, %.0f from the local player",
                    near[i].second->classId, near[i].second->type, near[i].second->resourceId,
                    near[i].second->position.x, near[i].second->position.y, near[i].second->position.z, near[i].first);
    }
}

bool refreshSnapshot(const uint8_t* rdram) {
    return world::readSnapshot(RdramMemory(rdram), g_globals, g_rs, g_snap) == world::ReadStatus::Ok;
}

void destroyActor(drakan_input::IGuest& guest, uint8_t* rdram, uint32_t actor) {
    const uint32_t eng = engineCtx(rdram);
    if (!ramPtr(eng) || !ramPtr(actor))
        return;
    // 0x1413c0 only flags the actor and appends it to the kill list; the engine frees it in its frame tail flush.
    guest.call(drakan_input::GuestCall(drakan_region::guestAddress(0x1413c0)).arg(eng).arg(actor));
}

// The engine's message send (vtable +0x14 of the message object at ctx+0x33c = 0x171150): replays a message on an
// object.
bool sendEngineMessage(drakan_input::IGuest& guest, uint8_t* rdram, uint32_t sender, uint32_t target,
                       const uint8_t msg[0x34]) {
    constexpr uint32_t kScratchMsg =
        0x5b84c0u; // inside the speech controller's idle text buffer (drakan_pvp's scratch)
    const uint32_t ctx = engineCtx(rdram);
    const uint32_t obj = ramPtr(ctx) ? rd32(rdram, ctx + 0x33c) : 0;
    const uint32_t vt = ramPtr(obj) ? rd32(rdram, obj) : 0;
    if (!ramPtr(vt) || guest.postBusy())
        return false;
    const uint32_t fn = rd32(rdram, vt + 0x14);
    const int16_t adj = static_cast<int16_t>(rd16(rdram, vt + 0x10));
    if (!fn)
        return false;
    const uint32_t scratch = drakan_region::guestAddress(kScratchMsg);
    for (uint32_t i = 0; i < 0x34; i += 4) {
        uint32_t w;
        std::memcpy(&w, msg + i, 4);
        guest.write32(scratch + i, w);
    }
    guest.post(drakan_input::GuestCall(fn)
                   .arg(obj + static_cast<uint32_t>(static_cast<int32_t>(adj)))
                   .arg(sender)
                   .arg(target)
                   .arg(scratch)
                   .arg(0x34)
                   .arg(1));
    return true;
}

// ------------------------------------------------------------------------------------------------- message hook

uint64_t suppressKey(uint32_t key, uint16_t type) {
    return (uint64_t(key) << 16) | type;
}
bool suppressed(uint32_t key, uint16_t type) {
    std::lock_guard<std::mutex> l(g_supM);
    auto su = g_suppress.find(suppressKey(key, type));
    return su != g_suppress.end() && su->second >= nowMs();
}
void suppressFor(uint32_t key, uint16_t type, uint64_t ms) {
    std::lock_guard<std::mutex> l(g_supM);
    g_suppress[suppressKey(key, type)] = nowMs() + ms;
}

// Every engine message goes through here (0x171150). The damage message (0x10db) gives both directions of the combat
// sync; object messages in the replay set give the door / lever sync.
void hookSend(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t sender = getRegU32(c, 5), target = getRegU32(c, 6), msg = getRegU32(c, 7);
    if (!g_enabled.load(std::memory_order_relaxed) || !ramPtr(msg) || !ramPtr(target) || g_levelHash.load() == 0) {
        g_origSend(rdram, c, rt);
        return;
    }
    const uint16_t type = rd16(rdram, msg);
    int32_t hpBefore = 0;
    uint32_t restoreHp = 0;
    if (type == kMsgDamage) {
        int32_t dmg = static_cast<int32_t>(rd32(rdram, msg + 0x10));
        const uint32_t me = controlled(rdram);
        if (g_probe) {
            static uint32_t logged = 0;
            if (logged++ < 80)
                coopLog("damage msg: sender %08x (me %08x) target %08x dmg %d, target is %s", sender, me, target, dmg,
                        g_keyed.count(target) ? (g_keyed[target].swapped ? "a swapped creature" : "a creature")
                                              : (isGhostHandle(target) ? "a ghost" : "other"));
        }
        if (g_host) {
            if (g_damageScale != 1.0f && isLocalAttacker(rdram, sender) && g_keyed.count(target)) {
                dmg = static_cast<int32_t>(dmg * g_damageScale);
                wr32(rdram, msg + 0x10, static_cast<uint32_t>(dmg)); // test aid
            }
            if (g_aiSwapActive.load() && target == me) {
                // The creature attacks the partner: the local player's body is standing on the ghost for this tick and
                // was swept too. The damage belongs to the partner (the ghost gets its own copy of the message).
                ++g_st.hurtSwallowed;
                SET_GPR_U32(c, 2, 1u);
                c->pc = getRegU32(c, 31);
                return;
            }
            if (!g_partnerDead && !isLocalAttacker(rdram, sender) && dmg > 0 && isGhostHandle(target)) {
                // A host creature (or anything else) hurt the partner's ghost: the client takes the damage.
                const int32_t d = dmg;
                pushReliable(kMsgHurt, &d, 4);
                ++g_st.hurtSent;
                coopLog("host: ghost %08x hit by %08x for %d -> client", target, sender, d);
            }
        } else {
            auto it = g_keyed.find(target);
            if (!g_noClaim.load() && isLocalAttacker(rdram, sender) && dmg > 0 && it != g_keyed.end() &&
                it->second.swapped && !it->second.dying) {
                // The local player hit a proxy: the host decides what that does. The engine's hit reaction runs, the
                // proxy's HP is kept (it follows the host's).
                struct {
                    uint32_t key;
                    int32_t damage;
                } p{it->second.key, dmg};
                pushReliable(kMsgClaim, &p, sizeof p);
                ++g_st.claimsSent;
                coopLog("client: claim on creature %08x (key %08x) damage %d", target, it->second.key, dmg);
                hpBefore = creatureHp(rdram, target);
                restoreHp = creatureData(rdram, target);
            }
        }
    } else if (type) {
        auto it = g_obj.find(target);
        if (g_probe && (sender != 0 || g_obj.count(target))) {
            static std::set<std::pair<uint16_t, uint32_t>> seen;
            const uint32_t cls = g_obj.count(target) ? g_obj[target].classId : 0;
            if (seen.insert({type, cls && cls != kChestClass ? cls : target}).second && seen.size() < 400)
                coopLog("msg %04x sender %08x target %08x watched-class %x w8 %x wc %x w10 %x w14 %x", type, sender,
                        target, cls, rd32(rdram, msg + 8), rd32(rdram, msg + 0xc), rd32(rdram, msg + 0x10),
                        rd32(rdram, msg + 0x14));
        }
        if (type == kMsgTaken && sender == target && it != g_obj.end() && it->second.pickup && !it->second.gone &&
            !g_aiSwapActive.load()) {
            // The item sent itself the "taken" message (0x10c1): the local player picked it up (E, or walking over
            // treasure). It goes into her inventory; the partner's world copy is removed, without the item.
            Obj& o = it->second;
            o.gone = true;
            g_goneKeys.insert(o.key);
            pushReliable(kMsgObjGone, &o.key, 4);
            ++g_st.goneSent;
            coopLog("%s: pickup %08x (class %x key %08x) taken by the local player (msg 10c1) -> peer",
                    g_host ? "host" : "client", target, o.classId, o.key);
        }
        if (it != g_obj.end() && g_replayTypes.count(type)) {
            if (!suppressed(it->second.key, type)) {
                struct {
                    uint32_t key;
                    uint16_t type, pad;
                    int32_t w[4];
                } p{it->second.key,
                    type,
                    0,
                    {int32_t(rd32(rdram, msg + 8)), int32_t(rd32(rdram, msg + 0xc)), int32_t(rd32(rdram, msg + 0x10)),
                     int32_t(rd32(rdram, msg + 0x14))}};
                pushReliable(kMsgObjMsg, &p, sizeof p);
                ++g_st.msgSent;
                coopLog("%s: object message %04x to %08x (class %x key %08x) -> peer", g_host ? "host" : "client", type,
                        target, it->second.classId, it->second.key);
                suppressFor(it->second.key, type, 1500);
            }
        }
    }
    g_origSend(rdram, c, rt);
    if (restoreHp)
        wr32(rdram, restoreHp + world::handle::kCreatureHp, static_cast<uint32_t>(hpBefore));
}

// -------------------------------------------------------------------------------------------------- per tick work

// Host: report deaths, apply the client's claims.
void hostEvents(drakan_input::IGuest& guest, uint8_t* rdram) {
    if (g_partnerDead && nowMs() - g_partnerDeadMs >= kRespawnDelayMs) {
        g_partnerDead = false;
        pushReliable(kMsgRespawn, nullptr, 0);
        coopLog("host: the partner's respawn is granted (ghost is a target again)");
    }
    // Claims: one per boundary, because the damage message is posted (it plays sounds behind the heap semaphore; a
    // nested call hung the guest in PvP, see pvp.md).
    if (!g_claims.empty() && !guest.postBusy()) {
        const Claim cl = g_claims.front();
        g_claims.pop_front();
        auto hi = g_keyToHandle.find(cl.key);
        const char* why = nullptr;
        uint32_t h = 0;
        if (hi == g_keyToHandle.end())
            why = "unknown creature";
        else if (!world::isLiveHandle(RdramMemory(rdram), g_globals, h = hi->second, g_keyed[hi->second].ref - 1))
            why = "creature gone";
        else if (creatureHp(rdram, h) <= 0)
            why = "creature already dead";
        else if (cl.damage <= 0 || cl.damage > 400)
            why = "damage out of range";
        else if (g_partnerDead)
            why = "claimant is down";
        else {
            // The claimant must be near: its ghost within 3000 units of the creature (when a ghost exists).
            const auto gs = ghostHandles();
            if (!gs.empty()) {
                float best = 1e30f;
                for (uint32_t g : gs)
                    best = std::min(best, std::hypot(rdF(rdram, g + 0x14) - rdF(rdram, h + 0x14),
                                                     rdF(rdram, g + 0x1c) - rdF(rdram, h + 0x1c)));
                if (best > 3000.0f)
                    why = "claimant too far";
            }
        }
        if (why) {
            ++g_st.claimsRejected;
            coopLog("host: claim rejected: %s (key %08x damage %d)", why, cl.key, cl.damage);
        } else {
            uint8_t msg[0x34] = {};
            const uint16_t t = kMsgDamage;
            std::memcpy(msg, &t, 2);
            const uint32_t w8 = 3, wc = 1, w10 = static_cast<uint32_t>(cl.damage * g_damageScale), w14 = 1,
                           w30 = 0xffffffffu;
            std::memcpy(msg + 8, &w8, 4);
            std::memcpy(msg + 0xc, &wc, 4);
            std::memcpy(msg + 0x10, &w10, 4);
            std::memcpy(msg + 0x14, &w14, 4);
            std::memcpy(msg + 0x30, &w30, 4);
            const auto gs = ghostHandles();
            const uint32_t sender = !gs.empty() ? gs.front() : controlled(rdram);
            const int32_t before = creatureHp(rdram, h);
            if (sendEngineMessage(guest, rdram, sender, h, msg)) {
                ++g_st.claimsApplied;
                coopLog("host: claim applied to creature %08x key %08x: damage %d, hp was %d", h, cl.key, cl.damage,
                        before);
            } else {
                g_claims.push_front(cl); // the guest is busy: try again at the next boundary
            }
        }
    }
    // Replays (object messages from the client).
    std::vector<uint8_t> ev;
    {
        std::lock_guard<std::mutex> l(g_hookM);
        if (!g_objEvents.empty() && !guest.postBusy()) {
            ev = std::move(g_objEvents.front());
            g_objEvents.pop_front();
        }
    }
    if (!ev.empty()) {
        uint32_t key;
        uint16_t type;
        int32_t w[4];
        std::memcpy(&key, &ev[kEventHeader], 4);
        std::memcpy(&type, &ev[kEventHeader + 4], 2);
        std::memcpy(w, &ev[kEventHeader + 8], 16);
        auto oi = g_objKeyToHandle.find(key);
        if (oi != g_objKeyToHandle.end() &&
            world::isLiveHandle(RdramMemory(rdram), g_globals, oi->second, g_obj[oi->second].ref - 1)) {
            uint8_t msg[0x34] = {};
            std::memcpy(msg, &type, 2);
            std::memcpy(msg + 8, w, 16);
            suppressFor(key, type, 1500);
            sendEngineMessage(guest, rdram, controlled(rdram), oi->second, msg);
            coopLog("replayed object message %04x on %08x (key %08x)", type, oi->second, key);
        }
    }
}

void drainEvents(drakan_input::IGuest& guest, uint8_t* rdram, uint32_t level) {
    std::vector<std::vector<uint8_t>> evs;
    {
        std::lock_guard<std::mutex> l(g_s.m);
        evs.swap(g_s.evIn);
    }
    for (auto& d : evs) {
        uint32_t lv;
        std::memcpy(&lv, &d[1], 4);
        if (lv != level)
            continue;
        const uint8_t type = d[0];
        const size_t n = d.size() - kEventHeader;
        const uint8_t* p = d.data() + kEventHeader;
        if (type == kMsgDied && !g_host && n >= 4) {
            uint32_t key;
            std::memcpy(&key, p, 4);
            ++g_st.deathsRecv;
            auto hi = g_keyToHandle.find(key);
            if (hi == g_keyToHandle.end() || !g_keyed.count(hi->second))
                continue;
            Keyed& k = g_keyed[hi->second];
            if (k.dying || !world::isLiveHandle(RdramMemory(rdram), g_globals, hi->second, k.ref - 1))
                continue;
            k.dying = true;
            k.dyingTicks = 0;
            k.dyingSinceMs = nowMs();
            const bool restored = restoreHandler(rdram, hi->second);
            setCreatureHp(rdram, hi->second, 1);
            coopLog("client: creature %08x (key %08x) died on the host: own handler %s, lethal hit follows", hi->second,
                    key, restored ? "restored" : "NOT restored");
        } else if (type == kMsgClaim && g_host && n >= 8) {
            Claim cl;
            std::memcpy(&cl.key, p, 4);
            std::memcpy(&cl.damage, p + 4, 4);
            g_claims.push_back(cl);
        } else if (type == kMsgHurt && !g_host && n >= 4) {
            int32_t dmg;
            std::memcpy(&dmg, p, 4);
            if (!g_life.dead)
                g_pendingHurt += std::clamp(static_cast<int32_t>(dmg * g_hurtScale), 0, 1000);
        } else if (type == kMsgPlayerDied && g_host) {
            g_partnerDead = true;
            g_partnerDeadMs = nowMs();
            coopLog("host: the partner died: creatures stop targeting its ghost, respawn in %llu ms",
                    static_cast<unsigned long long>(kRespawnDelayMs));
        } else if (type == kMsgSyncReq && g_host) {
            g_syncPending = true;
            coopLog("host: the client asked for the catch-up state");
        } else if (type == kMsgObjState && !g_host && n >= 8) {
            uint32_t key;
            int32_t state;
            std::memcpy(&key, p, 4);
            std::memcpy(&state, p + 4, 4);
            auto oi = g_objKeyToHandle.find(key);
            if (oi != g_objKeyToHandle.end() && state == 1 && g_obj[oi->second].classId == 0x10013u) {
                g_forceOpen.insert(oi->second);
                coopLog("client: door %08x (key %08x) was open on the host: opening it", oi->second, key);
            }
        } else if (type == kMsgRespawn && !g_host) {
            if (g_life.dead)
                g_life.respawnReq = true;
        } else if (type == kMsgObjGone && n >= 4) {
            uint32_t key;
            std::memcpy(&key, p, 4);
            ++g_st.goneRecv;
            auto oi = g_objKeyToHandle.find(key);
            if (oi == g_objKeyToHandle.end())
                continue;
            Obj& o = g_obj[oi->second];
            if (o.gone)
                continue;
            o.gone = true; // do not echo the removal back
            g_goneKeys.insert(key);
            if (world::isLiveHandle(RdramMemory(rdram), g_globals, oi->second, o.ref - 1)) {
                destroyActor(guest, rdram, oi->second);
                coopLog("%s: pickup %08x (class %x key %08x) taken by the partner: removed", g_host ? "host" : "client",
                        oi->second, o.classId, key);
            }
        } else if (type == kMsgObjMsg && n >= 4 + 4 + 16) {
            ++g_st.msgRecv;
            std::lock_guard<std::mutex> l(g_hookM);
            g_objEvents.push_back(std::vector<uint8_t>(d));
        }
    }
}

// Client: pending damage from host enemies, then the replays of object messages.
void clientEvents(drakan_input::IGuest& guest, uint8_t* rdram) {
    if (g_pendingHurt > 0 && !g_life.dead && !guest.postBusy()) {
        constexpr uint32_t kDamageMethod = 0x314ab0u, kScratch = 0x5b8480u;
        const uint32_t me = controlled(rdram);
        const uint32_t inner = ramPtr(me) ? rd32(rdram, me + 0xfc) : 0;
        const uint32_t data = ramPtr(inner) ? rd32(rdram, inner + 0x10) : 0;
        if (ramPtr(data)) {
            // The engine's own death reloads the last save, so a damage that would kill is a co-op death instead
            // (startDeath); anything smaller goes through the engine's damage method (flinch, sound, HUD).
            const int32_t hp = playerHp(rdram);
            const int d = std::clamp(g_pendingHurt, 0, std::max(0, hp - 1));
            const bool lethal = g_pendingHurt >= hp;
            g_pendingHurt = 0;
            if (lethal) {
                startDeath(rdram);
            } else if (d > 0) {
                guest.write32(drakan_region::guestAddress(kScratch), static_cast<uint32_t>(d));
                guest.post(drakan_input::GuestCall(drakan_region::guestAddress(kDamageMethod))
                               .arg(data)
                               .arg(drakan_region::guestAddress(kScratch))
                               .arg(0)
                               .arg(0)
                               .arg(0)
                               .arg(1));
                ++g_st.hurtApplied;
                coopLog("client: took %d damage from a host enemy (hp %d)", d, hp);
            }
        }
    }
    hostEvents(guest, rdram); // object replays are symmetric (claims are empty on the client)
}

// Pickups that appeared after the level was keyed (loot dropped by a death): keyed by (class, resource, ordinal of
// appearance). Both worlds run the same death, so the drops show up in the same order; it is a heuristic, logged.
void scanNewObjects() {
    refreshParked(g_snap);
    for (const auto& e : g_snap.entities) {
        if (!isPickupEntity(e) || g_keyed.count(e.handle))
            continue;
        auto old = g_obj.find(e.handle);
        if (old != g_obj.end()) {
            if (!old->second.gone || (old->second.resId == e.resourceId && old->second.ref == e.ref))
                continue; // known (a gone one whose slot is empty again would not be in the snapshot)
            g_objKeyToHandle.erase(old->second.key); // the handle was recycled for a new item
        }
        Obj o;
        o.resId = e.resourceId;
        o.classId = e.classId;
        o.type = e.type;
        o.ref = e.ref;
        o.pickup = true;
        std::memcpy(o.pos0, &e.position, 12);
        const uint32_t cls = e.resourceId * 2654435761u ^ e.classId;
        o.key = mixKey(cls, 0x10000u + g_dynCounter[cls]++);
        g_obj[e.handle] = o;
        g_objKeyToHandle[o.key] = e.handle;
        coopLog("%s: new pickup %08x class %x type %u res %x at %.0f %.0f %.0f -> key %08x", g_host ? "host" : "client",
                e.handle, e.classId, e.type, e.resourceId, e.position.x, e.position.y, e.position.z, o.key);
    }
}

// Probe (DRAKAN_COOP_PROBE): which fields of a door change when it opens. Initial copies of the handle (0x100 bytes)
// and the instance data (0x200 bytes) are kept per door, a changed word is logged once per change.
struct DoorProbe {
    std::vector<uint32_t> h, q;
    std::unordered_map<uint32_t, uint32_t>
        changes; // offset (data: | 0x10000) -> times it changed (noisy words are muted)
    uint32_t qaddr = 0;
};
std::unordered_map<uint32_t, DoorProbe> g_doorProbe;
void probeDoors(const uint8_t* rdram) {
    static uint32_t budget = 300;
    const uint32_t me = controlled(rdram);
    uint32_t nearest = 0, nearestChest = 0;
    float best = 1e30f, bestChest = 1e30f;
    for (auto& [h, o] : g_obj)
        if ((o.classId == 0x10013u || o.classId == kChestClass) && ramPtr(me)) {
            const float d =
                std::hypot(rdF(rdram, h + 0x14) - rdF(rdram, me + 0x14), rdF(rdram, h + 0x1c) - rdF(rdram, me + 0x1c));
            float& b = o.classId == kChestClass ? bestChest : best;
            uint32_t& n = o.classId == kChestClass ? nearestChest : nearest;
            if (d < b) {
                b = d;
                n = h;
            }
        }
    for (auto& [h, o] : g_obj) {
        if ((o.classId != 0x10013u && o.classId != kChestClass) || (h != nearest && h != nearestChest))
            continue; // only the door and the chest nearest to the player are watched (the rest is noise)
        const uint32_t inner = rd32(rdram, h + 0xfc);
        const uint32_t q = ramPtr(inner) ? rd32(rdram, inner + 0x10) : 0;
        auto it = g_doorProbe.find(h);
        if (it == g_doorProbe.end()) {
            DoorProbe d;
            for (uint32_t i = 0; i < 0x40; ++i)
                d.h.push_back(rd32(rdram, h + i * 4));
            if (ramPtr(q))
                for (uint32_t i = 0; i < 0x80; ++i)
                    d.q.push_back(rd32(rdram, q + i * 4));
            d.qaddr = q;
            g_doorProbe[h] = d;
            continue;
        }
        DoorProbe& d = it->second;
        for (uint32_t i = 0; i < d.h.size() && budget; ++i) {
            const uint32_t v = rd32(rdram, h + i * 4);
            if (v != d.h[i]) {
                if (++d.changes[i * 4] > 4) {
                    d.h[i] = v;
                    continue; // a word that keeps changing (counters, the player's position)
                }
                coopLog("%s %08x key %08x (local player %.0f away): handle+%x %08x -> %08x",
                        o.classId == kChestClass ? "chest" : "door", h, o.key,
                        o.classId == kChestClass ? bestChest : best, i * 4, d.h[i], v);
                d.h[i] = v;
                --budget;
            }
        }
        for (uint32_t i = 0; i < d.q.size() && budget && q == d.qaddr; ++i) {
            const uint32_t v = rd32(rdram, q + i * 4);
            if (v != d.q[i]) {
                if (++d.changes[0x10000u | i * 4] > 4) {
                    d.q[i] = v;
                    continue;
                }
                coopLog("%s %08x key %08x (local player %.0f away): data+%x %08x -> %08x",
                        o.classId == kChestClass ? "chest" : "door", h, o.key,
                        o.classId == kChestClass ? bestChest : best, i * 4, d.q[i], v);
                d.q[i] = v;
                --budget;
            }
        }
    }
}

// Doors open by themselves when the player comes near (the handler reads the controlled actor, like the AI). Their tick
// gets the same retargeting wrapper as the creatures: with the partner's ghost near the door and the local player far
// away, the door sees "the player" at the ghost, so a door one player opens opens for the other as well.
void wrapDoors(uint8_t* rdram) {
    if (g_retarget && g_aiSlot)
        for (auto& [h, o] : g_obj)
            if (o.classId == 0x10013u && !o.swapped && !o.gone)
                o.swapped = swapHandlerTo(rdram, h, g_aiSlot);
    if (g_probe && g_chestSlot)
        for (auto& [h, o] : g_obj)
            if (o.classId == kChestClass && !o.swapped && !o.gone)
                o.swapped = swapHandlerTo(rdram, h, g_chestSlot);
}

// Pickups that left the registry: tell the other side (it removes its copy); both directions.
void watchObjects(const uint8_t* rdram) {
    const RdramMemory mem(rdram);
    for (auto& [h, o] : g_obj) {
        if (!o.pickup || o.gone)
            continue;
        const char* how = "left the registry";
        if (world::isLiveHandle(mem, g_globals, h, o.ref - 1)) {
            // A loose item that was picked up is not destroyed: it goes into the inventory (moved far away).
            const float p[3] = {rdF(rdram, h + 0x14), rdF(rdram, h + 0x18), rdF(rdram, h + 0x1c)};
            if (!std::isfinite(p[0]))
                continue;
            if (o.pos0[0] == 0.0f && o.pos0[1] == 0.0f && o.pos0[2] == 0.0f) { // spawned at the origin, not placed yet
                if (p[0] != 0.0f || p[2] != 0.0f)
                    std::memcpy(o.pos0, p, 12);
                continue;
            }
            if (std::hypot(p[0] - o.pos0[0], p[2] - o.pos0[2]) < 3000.0f)
                continue;
            how = "was taken into an inventory";
        }
        o.gone = true;
        g_goneKeys.insert(o.key);
        pushReliable(kMsgObjGone, &o.key, 4);
        ++g_st.goneSent;
        coopLog("%s: pickup %08x (class %x key %08x) %s -> peer", g_host ? "host" : "client", h, o.classId, o.key, how);
    }
}

void hostTick(drakan_input::IGuest& guest, uint8_t* rdram, uint32_t levelHash) {
    hostEvents(guest, rdram);
    const uint64_t now = nowMs();
    if (now - g_lastHostSend < kSendIntervalMs)
        return;
    g_lastHostSend = now;
    if (!refreshSnapshot(rdram))
        return;
    if (!g_keyedThisLevel)
        keyCreatures(g_snap);
    scanNewObjects();
    watchObjects(rdram);
    if (g_syncPending) {
        g_syncPending = false;
        uint32_t doors = 0;
        for (uint32_t key : g_goneKeys)
            pushReliable(kMsgObjGone, &key, 4);
        for (auto& [h, o] : g_obj) {
            const uint32_t inner = rd32(rdram, h + 0xfc);
            const uint32_t q = ramPtr(inner) ? rd32(rdram, inner + 0x10) : 0;
            if (o.classId == 0x10013u && ramPtr(q) && rd32(rdram, q + 0x10) == 1 &&
                world::isLiveHandle(RdramMemory(rdram), g_globals, h, o.ref - 1)) {
                struct {
                    uint32_t key;
                    int32_t state;
                } st{o.key, 1};
                pushReliable(kMsgObjState, &st, sizeof st);
                ++doors;
            }
        }
        coopLog("host: catch-up sent: %zu taken pickups, %u open doors", g_goneKeys.size(), doors);
    }
    if (g_probe)
        probeDoors(rdram);
    wrapDoors(rdram);
    // Creatures: their own handler gets the retargeting wrapper once (the first time they are keyed).
    if (g_retarget && g_aiSlot)
        for (auto& [h, k] : g_keyed)
            if (!k.swapped)
                k.swapped = swapHandlerTo(rdram, h, g_aiSlot);
    std::vector<Entry> entries;
    std::unordered_map<uint32_t, const world::EntitySnapshot*> present;
    g_st.hostMoved = 0;
    for (const auto& e : g_snap.entities) {
        if (e.hpKind != world::HpKind::Creature)
            continue;
        present[e.handle] = &e;
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
    // Deaths: HP <= 0 at once; a creature that left the registry (checked on three refreshes: a torn read is possible).
    for (auto& [h, k] : g_keyed) {
        if (k.deadSent)
            continue;
        auto pi = present.find(h);
        bool dead = false;
        const char* why = "hp <= 0";
        if (pi != present.end() && pi->second->resourceId == k.resId) {
            k.absentCount = 0;
            dead = pi->second->hp <= 0;
        } else if (++k.absentCount >= 3) {
            dead = true;
            why = "left the registry";
        }
        if (!dead)
            continue;
        k.deadSent = true;
        ++g_st.deathsSent;
        pushReliable(kMsgDied, &k.key, 4);
        coopLog("host: creature %08x (key %08x) died (%s) -> client", h, k.key, why);
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

void clientTick(drakan_input::IGuest& guest, uint8_t* rdram, uint32_t levelHash, double dt) {
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
        if (!refreshSnapshot(rdram))
            return;
        keyCreatures(g_snap);
    }
    if (!g_syncSent) {
        g_syncSent = true;
        pushReliable(kMsgSyncReq, nullptr, 0);
        coopLog("client: keyed the level, asked the host for the catch-up state");
    }
    clientEvents(guest, rdram);
    const uint64_t nowM = nowMs();
    if (nowM - g_lastRefresh >= kSendIntervalMs) {
        g_lastRefresh = nowM;
        if (refreshSnapshot(rdram))
            scanNewObjects();
        watchObjects(rdram);
        if (g_probe)
            probeDoors(rdram);
        wrapDoors(rdram);
    }
    const RdramMemory mem(rdram);
    const float alpha = static_cast<float>(std::min(1.0, dt * 12.0));
    uint32_t matched = 0, swapped = 0, dead = 0, unmatched = 0, over200 = 0, refiled = 0;
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
        if (k.dying) {
            // The creature's own handler is back; the lethal damage message makes its own death logic run (animation,
            // loot). If it is still in the registry a few seconds later it is taken out.
            if (!k.killSent) {
                uint8_t msg[0x34] = {};
                const uint16_t t = kMsgDamage;
                std::memcpy(msg, &t, 2);
                const uint32_t w8 = 3, wc = 1, w10 = 1000, w14 = 1, w30 = 0xffffffffu;
                std::memcpy(msg + 8, &w8, 4);
                std::memcpy(msg + 0xc, &wc, 4);
                std::memcpy(msg + 0x10, &w10, 4);
                std::memcpy(msg + 0x14, &w14, 4);
                std::memcpy(msg + 0x30, &w30, 4);
                g_noClaim = true; // our own message must not become a claim
                if (sendEngineMessage(guest, rdram, controlled(rdram), h, msg)) {
                    k.killSent = true;
                    coopLog("client: lethal damage posted to the proxy %08x", h);
                }
                g_noClaim = false;
            } else if (g_killDestroy && !k.destroyed && nowM - k.dyingSinceMs > 5000) {
                k.destroyed = true;
                destroyActor(guest, rdram, h);
                coopLog("client: creature %08x still in the registry 5 s after its death: destroyed", h);
            }
            ++dead;
            continue;
        }
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
        if (e.hp <= 0) {
            // A death whose event was lost, or one that happened before we keyed: same handling.
            ++dead;
            if (g_follow && k.swapped && !k.dying) {
                k.dying = true;
                k.dyingSinceMs = nowMs();
                restoreHandler(rdram, h);
                setCreatureHp(rdram, h, 1);
                coopLog("client: creature %08x (key %08x) has hp <= 0 on the host: own handler restored", h, k.key);
            }
            continue;
        }
        if (!g_follow)
            continue;
        if (!k.swapped)
            k.swapped = swapHandlerTo(rdram, h, g_handlerSlot);
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
        setCreatureHp(rdram, h, e.hp);
        // A host write of the position does not move the actor in the collision cells; the player's blade only finds
        // actors in the cell lists (the same fix as for ghosts, pvp.md), so a proxy that moved is filed again.
        const float moved = std::fabs(e.pos[0] - pos[0]) + std::fabs(e.pos[1] - pos[1]) + std::fabs(e.pos[2] - pos[2]);
        if (moved * a > 1.0f && refiled < 24) {
            ++refiled;
            drakan_ghost::refileActor(guest, h);
        }
    }
    g_st.refiled += refiled;
    g_st.localKeyed = static_cast<uint32_t>(g_keyed.size());
    g_st.matched = matched;
    g_st.swapped = swapped;
    g_st.hostDead = dead;
    g_st.unmatchedRemote = unmatched;
    g_st.curSum = matched ? curSum / matched : 0.0;
    g_st.curMax = curMax;
    g_st.curOver200 = over200;
}

// ------------------------------------------------------------------------------------------------------ test aids
// DRAKAN_COOP_TEST_GOTO=x,y,z@<file>: teleport the local player once the file exists (the harness creates it).
// DRAKAN_COOP_TEST_SUMMON=<res hex>,<n>@<file> (host): move n live creatures of that resource beside the players.
// DRAKAN_COOP_TEST_FORCE_GHOST=1     (host): every creature targets the partner's ghost when there is one.
// DRAKAN_COOP_TEST_TAKE=<file>       : remove the first live pickup once the file exists (a stand-in for taking it).
// DRAKAN_COOP_TEST_GOD=1             : keep the local player's HP at its maximum.
// DRAKAN_COOP_TEST_CLAIM=<ms>       : (client) claim damage on the nearest live proxy every <ms> (no weapon needed).
struct TestGoto {
    bool want = false, done = false;
    float p[3] = {};
    std::string file; // the teleport happens once this file exists (the harness creates it when the setup is done)
} g_goto;
struct TestSummon {
    bool want = false, done = false;
    uint32_t res = 0;
    int count = 0;
    std::string file;
} g_summon;
struct TestTo { // DRAKAN_COOP_TEST_TO=<class hex>,<dist>@<file>: stand <dist> in front of the nearest live object of a
                // class
    bool want = false, done = false;
    uint32_t cls = 0;
    float dist = 0;
    std::string file;
} g_to;
std::string g_takeFile;
bool g_takeDone = false;
bool g_god = false; // test aid: the local player's HP is kept at its maximum (host: the orcs cannot end the run)
uint32_t g_claimEveryMs = 0;
uint64_t g_lastTestClaim = 0, g_lastForcedLog = 0;

void testAids(drakan_input::IGuest& guest, uint8_t* rdram) {
    if (g_god) {
        const uint32_t S = drakan_region::guestAddress(0x4fbac8u);
        const uint32_t key = rd32(rdram, g_globals.hpKey);
        auto dec = [&](uint32_t off) {
            const uint32_t raw = rd32(rdram, S + off) ^ key;
            return (raw >> 8) | (raw << 24);
        };
        const uint32_t mx = dec(0x10);
        if (mx && mx < 100000u)
            wr32(rdram, S + 0xc, ((mx << 8) | (mx >> 24)) ^ key);
    }
    static uint32_t fileTick = 0;
    ++fileTick;
    // The local player's heading from how she actually moved (her matrix axes were not reliable for this): the last
    // horizontal step bigger than 3 and smaller than 400 units.
    {
        static float prev[2] = {};
        static bool havePrev = false;
        const uint32_t me = controlled(rdram);
        if (ramPtr(me)) {
            const float x = rdF(rdram, me + 0x14), z = rdF(rdram, me + 0x1c);
            const float dx = x - prev[0], dz = z - prev[1], d = std::hypot(dx, dz);
            if (havePrev && d > 3.0f && d < 400.0f) {
                g_moveDir[0] = dx / d;
                g_moveDir[1] = dz / d;
                g_haveMoveDir = true;
            }
            prev[0] = x;
            prev[1] = z;
            havePrev = true;
        }
    }
    bool go = false;
    if (g_goto.want && !g_goto.done && fileTick % 20 == 0)
        if (std::FILE* f = std::fopen(g_goto.file.c_str(), "rb")) {
            std::fclose(f);
            go = true;
        }
    if (go) {
        const uint32_t me = controlled(rdram);
        if (ramPtr(me)) {
            for (int i = 0; i < 3; ++i)
                guest.writeF(me + 0x14 + 4u * i, g_goto.p[i]);
            drakan_ghost::refileActor(guest, me);
            g_goto.done = true;
            coopLog("test: teleported the local player to %.0f %.0f %.0f", g_goto.p[0], g_goto.p[1], g_goto.p[2]);
        }
    }
    if (g_to.want && !g_to.done && g_keyedThisLevel && fileTick % 20 == 5) {
        if (std::FILE* f = std::fopen(g_to.file.c_str(), "rb")) {
            std::fclose(f);
            const uint32_t me = controlled(rdram);
            const RdramMemory mem(rdram);
            uint32_t bestH = 0;
            float best = 1e30f;
            for (auto& [h, o] : g_obj)
                if (ramPtr(me) && o.classId == g_to.cls && !o.gone &&
                    world::isLiveHandle(mem, g_globals, h, o.ref - 1)) {
                    const float d = std::hypot(rdF(rdram, h + 0x14) - rdF(rdram, me + 0x14),
                                               rdF(rdram, h + 0x1c) - rdF(rdram, me + 0x1c));
                    if (d < best) {
                        best = d;
                        bestH = h;
                    }
                }
            if (bestH) {
                // The player's forward axis is row 2 of her matrix (+0x38 / +0x40): she ends up facing the object.
                float fx = rdF(rdram, me + 0x38), fz = rdF(rdram, me + 0x40);
                float fl = std::max(1e-3f, std::hypot(fx, fz));
                fx /= fl;
                fz /= fl;
                if (g_haveMoveDir) { // she ends up standing dist before the object, still heading the way she walked
                    fx = -g_moveDir[0];
                    fz = -g_moveDir[1];
                }
                guest.writeF(me + 0x14, rdF(rdram, bestH + 0x14) + g_to.dist * fx);
                guest.writeF(me + 0x18, rdF(rdram, bestH + 0x18) + 60.0f);
                guest.writeF(me + 0x1c, rdF(rdram, bestH + 0x1c) + g_to.dist * fz);
                drakan_ghost::refileActor(guest, me);
                coopLog("test: stood %.0f units in front of %08x (class %x, was %.0f away)", g_to.dist, bestH, g_to.cls,
                        best);
                coopLog("test: chest at %.0f %.0f, me at %.0f %.0f, moveDir %d %.2f %.2f, row0 %.2f %.2f %.2f row2 "
                        "%.2f %.2f %.2f",
                        rdF(rdram, bestH + 0x14), rdF(rdram, bestH + 0x1c), rdF(rdram, me + 0x14),
                        rdF(rdram, me + 0x1c), g_haveMoveDir, g_moveDir[0], g_moveDir[1], rdF(rdram, me + 0x20),
                        rdF(rdram, me + 0x24), rdF(rdram, me + 0x28), rdF(rdram, me + 0x38), rdF(rdram, me + 0x3c),
                        rdF(rdram, me + 0x40));
                g_to.done = true;
            }
        }
    }
    if (g_host && g_summon.want && !g_summon.done && fileTick % 20 == 1) {
        if (std::FILE* f = std::fopen(g_summon.file.c_str(), "rb")) {
            std::fclose(f);
            // Bring `count` live creatures of the resource to the players (the player cannot be teleported across the
            // level: the floor under the target is not resident): alternately beside the host player and the ghost.
            const uint32_t me = controlled(rdram);
            const auto gs = ghostHandles();
            int moved = 0;
            if (ramPtr(me))
                for (auto& [h, k] : g_keyed) {
                    if (moved >= g_summon.count || k.resId != g_summon.res || creatureHp(rdram, h) <= 0)
                        continue;
                    const uint32_t anchor = (moved % 2 == 1 && !gs.empty()) ? gs.front() : me;
                    // In front of the anchor (its matrix row 2 is the forward axis), 450 units away.
                    const float fx = rdF(rdram, anchor + 0x38), fz = rdF(rdram, anchor + 0x40);
                    const float fl = std::max(1e-3f, std::hypot(fx, fz));
                    guest.writeF(h + 0x14, rdF(rdram, anchor + 0x14) + 450.0f * fx / fl);
                    guest.writeF(h + 0x18, rdF(rdram, anchor + 0x18) + 20.0f);
                    guest.writeF(h + 0x1c, rdF(rdram, anchor + 0x1c) + 450.0f * fz / fl);
                    drakan_ghost::refileActor(guest, h);
                    coopLog("test: creature %08x (key %08x, hp %d) moved beside %s", h, k.key, creatureHp(rdram, h),
                            anchor == me ? "the local player" : "the partner's ghost");
                    if (moved % 2 == 1)
                        g_forced.insert(h);
                    ++moved;
                }
            g_summon.done = true;
        }
    }
    if (g_host && !g_forced.empty() && nowMs() - g_lastForcedLog > 2000) {
        g_lastForcedLog = nowMs();
        const uint32_t me = controlled(rdram);
        for (uint32_t h : g_forced)
            coopLog("test: forced creature %08x at %.0f %.0f %.0f hp %d, %.0f units from the local player, target %s",
                    h, rdF(rdram, h + 0x14), rdF(rdram, h + 0x18), rdF(rdram, h + 0x1c), creatureHp(rdram, h),
                    ramPtr(me) ? std::hypot(rdF(rdram, h + 0x14) - rdF(rdram, me + 0x14),
                                            rdF(rdram, h + 0x1c) - rdF(rdram, me + 0x1c))
                               : -1.0,
                    g_aiLast.count(h) && g_aiLast[h] ? "ghost" : "player");
    }
    if (!g_takeFile.empty() && !g_takeDone && g_keyedThisLevel && fileTick % 20 == 3) {
        if (std::FILE* f = std::fopen(g_takeFile.c_str(), "rb")) {
            std::fclose(f);
            // Stand-in for walking over a treasure: the pickup is removed through the engine's kill list, which is how
            // the registry loses a taken one. The first live pickup of the level (registry order) goes.
            const RdramMemory mem(rdram);
            for (auto& [h, o] : g_obj)
                if (o.pickup && !o.gone && world::isLiveHandle(mem, g_globals, h, o.ref - 1)) {
                    destroyActor(guest, rdram, h);
                    coopLog("test: pickup %08x (class %x key %08x) taken", h, o.classId, o.key);
                    g_takeDone = true;
                    break;
                }
        }
    }
    if (g_claimEveryMs && !g_host && nowMs() - g_lastTestClaim > g_claimEveryMs) {
        g_lastTestClaim = nowMs();
        const uint32_t me = controlled(rdram);
        float best = 1e30f;
        uint32_t bk = 0, bh = 0;
        for (auto& [h, k] : g_keyed) {
            if (!k.swapped || k.dying || !ramPtr(me))
                continue;
            const float d =
                std::hypot(rdF(rdram, h + 0x14) - rdF(rdram, me + 0x14), rdF(rdram, h + 0x1c) - rdF(rdram, me + 0x1c));
            if (d < best) {
                best = d;
                bk = k.key;
                bh = h;
            }
        }
        if (bk && best < 4000.0f) {
            struct {
                uint32_t key;
                int32_t damage;
            } p{bk, 6};
            pushReliable(kMsgClaim, &p, sizeof p);
            ++g_st.claimsSent;
            coopLog("client: test claim on creature %08x (key %08x) damage 6, %.0f units away", bh, bk, best);
        }
    }
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
    g_probe = std::getenv("DRAKAN_COOP_PROBE") != nullptr;
    g_killDestroy = std::getenv("DRAKAN_COOP_DESTROY") !=
                    nullptr; // remove the corpse after the death (off: it stays, like on the host)
    if (const char* s = std::getenv("DRAKAN_COOP_DAMAGE_SCALE"))
        g_damageScale = std::strtof(s, nullptr);
    if (const char* s = std::getenv("DRAKAN_COOP_RETARGET"))
        g_retarget = s[0] != '0';
    if (const char* s = std::getenv("DRAKAN_COOP_MSGS")) { // hex list: "1001,1002"
        for (const char* p = s; *p;) {
            char* end = nullptr;
            const unsigned long v = std::strtoul(p, &end, 16);
            if (end == p)
                break;
            g_replayTypes.insert(static_cast<uint16_t>(v));
            p = *end ? end + 1 : end;
        }
    }
    if (const char* s = std::getenv("DRAKAN_COOP_TEST_GOTO")) {
        float x, y, z;
        char file[512] = {};
        if (std::sscanf(s, "%f,%f,%f@%511[^\n]", &x, &y, &z, file) == 4) {
            g_goto.want = true;
            g_goto.p[0] = x;
            g_goto.p[1] = y;
            g_goto.p[2] = z;
            g_goto.file = file;
        }
    }
    g_god = std::getenv("DRAKAN_COOP_TEST_GOD") != nullptr;
    if (const char* s = std::getenv("DRAKAN_COOP_TEST_TAKE"))
        g_takeFile = s;
    g_forceGhost = std::getenv("DRAKAN_COOP_TEST_FORCE_GHOST") != nullptr;
    g_forceAll = g_forceGhost && std::string(std::getenv("DRAKAN_COOP_TEST_FORCE_GHOST")) == "all";
    if (const char* s = std::getenv("DRAKAN_COOP_TEST_SUMMON")) {
        unsigned res = 0;
        int count = 0;
        char file[512] = {};
        if (std::sscanf(s, "%x,%d@%511[^\n]", &res, &count, file) == 3) {
            g_summon.want = true;
            g_summon.res = res;
            g_summon.count = count;
            g_summon.file = file;
        }
    }
    if (const char* s = std::getenv("DRAKAN_COOP_TEST_TO")) {
        unsigned cls = 0;
        float dist = 0;
        char file[512] = {};
        if (std::sscanf(s, "%x,%f@%511[^\n]", &cls, &dist, file) == 3) {
            g_to.want = true;
            g_to.cls = cls;
            g_to.dist = dist;
            g_to.file = file;
        }
    }
    if (const char* s = std::getenv("DRAKAN_COOP_TEST_HURT_SCALE"))
        g_hurtScale = std::strtof(s, nullptr);
    if (const char* s = std::getenv("DRAKAN_COOP_TEST_CLAIM"))
        g_claimEveryMs = static_cast<uint32_t>(std::strtoul(s, nullptr, 10));
    g_rt = &rt;
    for (uint32_t a = 0x100100u; a < 0x100600u && (!g_handlerSlot || !g_aiSlot || !g_deadSlot || !g_chestSlot);
         a += 4) {
        if (rt.hasFunction(a))
            continue;
        if (!g_handlerSlot) {
            if (rt.registerFunction(a, &proxyHandler))
                g_handlerSlot = a;
        } else if (!g_aiSlot) {
            if (rt.registerFunction(a, &aiHandler))
                g_aiSlot = a;
        } else if (!g_deadSlot) {
            if (rt.registerFunction(a, &deadHandler))
                g_deadSlot = a;
        } else if (rt.registerFunction(a, &chestHandler)) {
            g_chestSlot = a;
        }
    }
    if (!g_handlerSlot || !g_aiSlot || !g_deadSlot || !g_chestSlot) {
        coopLog("disabled: no free function slot");
        return;
    }
    if (const uint32_t sendAddr = drakan_region::guestAddress(0x171150u)) {
        if ((g_origSend = rt.lookupFunction(sendAddr)))
            rt.replaceFunction(sendAddr, &hookSend);
    }
    if (!g_origSend)
        coopLog("warning: message hook not installed (combat and object sync off)");
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
    coopLog("enabled: %s %s:%u, %s, swallow mask %x, retarget %d, replay types %zu", g_host ? "host on" : "client of",
            g_host ? "0.0.0.0" : g_connectHost.c_str(), g_port, g_follow ? "follow" : "mark", g_swallow, g_retarget,
            g_replayTypes.size());
}

void beforePadMgr(drakan_input::IGuest& guest, uint8_t* rdram) {
    if (!g_enabled.load(std::memory_order_relaxed) || !rdram)
        return;
    g_rdramForLog = rdram;
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
    ++g_tickTotal;
    if (!g_host)
        tickLife(guest, rdram);
    if (g_keyedThisLevel) // events name objects by key: they wait (in the inbox) until this side has keyed the level
        drainEvents(guest, rdram, level);
    if (g_host)
        hostTick(guest, rdram, level);
    else
        clientTick(guest, rdram, level, dt);
    testAids(guest, rdram);
    if (t - g_lastStats >= 5.0) {
        g_lastStats = t;
        if (g_host) {
            uint64_t out;
            {
                std::lock_guard<std::mutex> l(g_s.m);
                out = g_s.datagramsOut;
            }
            coopLog("host stats: %u creatures sent, %u moved since keying, %llu datagrams; deaths sent %u, claims "
                    "applied %u rejected %u, hurt sent %u, pickups gone %u, object msgs %u, ai ticks %u (%u on the "
                    "ghost, %u switches)",
                    g_st.hostCreatures, g_st.hostMoved, static_cast<unsigned long long>(out), g_st.deathsSent,
                    g_st.claimsApplied, g_st.claimsRejected, g_st.hurtSent, g_st.goneSent, g_st.msgSent, g_st.aiTicks,
                    g_st.aiGhostTicks, g_st.aiSwaps);
        } else {
            uint64_t pk;
            {
                std::lock_guard<std::mutex> l(g_s.m);
                pk = g_s.packets;
            }
            coopLog("client stats: %llu packets, keyed %u, matched %u, swapped %u, host-dead %u, host-only %u; "
                    "initial error mean %.0f max %.0f (<100: %u, <1000: %u, far: %u); "
                    "now mean %.1f max %.0f, %u over 200; deaths recv %u, claims sent %u, hurt applied %u, pickups "
                    "gone sent %u recv %u, object msgs recv %u, refiled %u",
                    static_cast<unsigned long long>(pk), g_st.localKeyed, g_st.matched, g_st.swapped, g_st.hostDead,
                    g_st.unmatchedRemote, g_st.errN ? g_st.errSum / g_st.errN : 0.0, g_st.errMax, g_st.near100,
                    g_st.near1000, g_st.far, g_st.curSum, g_st.curMax, g_st.curOver200, g_st.deathsRecv,
                    g_st.claimsSent, g_st.hurtApplied, g_st.goneSent, g_st.goneRecv, g_st.msgRecv, g_st.refiled);
        }
    }
}

} // namespace drakan_coop
