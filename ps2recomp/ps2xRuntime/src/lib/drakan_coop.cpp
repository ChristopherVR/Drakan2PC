// Drakan: co-op prototype (drakan_coop.h).
#include "drakan_coop.h"
#include "drakan_coop_target.h"
#include "drakan_ghost.h"
#include "drakan_lifecycle.h"
#include "drakan_region.h"
#include "input/GuestInterface.h"
#include "drakan_netplay.h"
#include "net/Protocol.h"
#include "net/Transport.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "world/Inventory.h"
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
#include <utility>
#include <vector>

void ps2xNoPreemptEnter() noexcept;
void ps2xNoPreemptLeave() noexcept;

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
// The co-op logic keeps its own compact datagram form internally (type, level hash, payload; little endian, memcpy'd:
// every supported target is little endian). The transport (netThread below) converts it to / from the protocol v6
// messages of drakan::net (CoopCreatures / CoopEvent, net/Protocol.h) which the server relays:
//   1 creature states (host -> client):  u32 seq, u16 count, count x {u32 key, f32 pos[3], f32 yaw, i32 hp}
//   2 creature died   (host -> client):  u32 key
//   3 hit claim       (client -> host):  u32 key, i32 damage
//   4 player hurt     (host -> client):  i32 damage          (a host creature hit the client's ghost)
//   5 object gone     (both ways):       u32 key             (a pickup was taken: the other side removes it)
//   6 object message  (both ways):       u32 key, u16 type, u16 pad, i32 w[4]   (a door / lever message to replay)
//   7 player died     (client -> host):  (empty) the client's player is down: its ghost stops being a target
//   8 player respawn  (host -> client):  (empty) get up at the host
//   9 object state    (both ways):       u32 key, i32 state   (a chest / lever changed state)
//  10 sync request    (client -> host):   (empty) the client keyed the level: send the catch-up state
constexpr uint8_t kMsgCreatures = 1, kMsgDied = 2, kMsgClaim = 3, kMsgHurt = 4, kMsgObjGone = 5, kMsgObjMsg = 6,
                  kMsgPlayerDied = 7, kMsgRespawn = 8, kMsgObjState = 9, kMsgSyncReq = 10, kMsgLast = 10;
constexpr uint64_t kRespawnDelayMs = 5000;     // the death clip runs about 4.7 s
constexpr size_t kHeaderBytes = 1 + 4 + 4 + 2; // creature datagram header (type, level, seq, count)
constexpr size_t kEventHeader = 1 + 4 + 1;     // type, level, player (the sender; for Hurt / Respawn the target)
constexpr size_t kEntryBytes = 4 + 12 + 4 + 4;
constexpr size_t kEntriesPerPacket = std::min<size_t>((drakan::net::kMaxPacketPayload - kHeaderBytes) / kEntryBytes,
                                                      drakan::net::kMaxCoopCreatures); // the message's own limit
constexpr uint64_t kSendIntervalMs = 100;                                              // 10 Hz
constexpr uint16_t kChestSpewType = 0xfff1u; // ObjMsg pseudo type: w[0] RNG state, w[1] gold carry (host -> client)
constexpr int32_t kNoBudget = INT32_MIN;     // the chest has no entry in the gold generator's table
constexpr int32_t kStateDoorClosed = 0, kStateDoorOpen = 1, kStateChestOpen = 2; // ObjState values
constexpr uint16_t kMsgTaken = 0x10c1;  // an item's message to itself when it is picked up
constexpr uint16_t kMsgDamage = 0x10db; // the engine's damage message (type word at +0, damage at +0x10)

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
std::atomic<bool> g_enabled{false}; // a Coop match is playing and this module follows it (see syncLink)
std::atomic<bool> g_installed{false};
std::atomic<bool> g_stop{false};
std::thread g_thread;
bool g_host = false, g_follow = true;
world::Globals g_globals;
uint32_t g_swallow = 0x4;
uint32_t g_handlerSlot = 0, g_aiSlot = 0;
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

// `player`: Hurt / Respawn name their target (0 = every other player); everything else leaves it 0 (the server stamps
// the sender on the way).
void pushReliableTo(uint8_t player, uint8_t type, const void* payload, size_t n) {
    const uint32_t level = g_levelHash.load();
    if (!level)
        return;
    std::vector<uint8_t> d(kEventHeader + n);
    d[0] = type;
    std::memcpy(&d[1], &level, 4);
    d[5] = player;
    if (n)
        std::memcpy(&d[kEventHeader], payload, n);
    std::lock_guard<std::mutex> l(g_s.m);
    g_s.relOut.push_back(std::move(d));
}
void pushReliable(uint8_t type, const void* payload, size_t n) {
    pushReliableTo(0, type, payload, n);
}

// ---- legacy datagram <-> protocol v6 messages ----

bool eventFromLegacy(const std::vector<uint8_t>& d, drakan::net::CoopEvent& e) {
    using drakan::net::CoopKind;
    if (d.size() < kEventHeader || !drakan::net::coopKindValid(d[0]))
        return false;
    const size_t n = d.size() - kEventHeader;
    const uint8_t* p = d.data() + kEventHeader;
    e = drakan::net::CoopEvent{};
    e.kind = static_cast<CoopKind>(d[0]);
    std::memcpy(&e.levelHash, &d[1], 4);
    e.player = d[5];
    switch (e.kind) {
        case CoopKind::CreatureDied:
        case CoopKind::ObjGone:
            if (n < 4)
                return false;
            std::memcpy(&e.key, p, 4);
            break;
        case CoopKind::Claim:
        case CoopKind::ObjState:
            if (n < 8)
                return false;
            std::memcpy(&e.key, p, 4);
            std::memcpy(&e.value, p + 4, 4);
            break;
        case CoopKind::Hurt:
            if (n < 4)
                return false;
            std::memcpy(&e.value, p, 4);
            break;
        case CoopKind::ObjMsg:
            if (n < 24)
                return false;
            std::memcpy(&e.key, p, 4);
            std::memcpy(&e.msgType, p + 4, 2);
            std::memcpy(e.w, p + 8, 16);
            break;
        default:
            break;
    }
    return true;
}

std::vector<uint8_t> eventToLegacy(const drakan::net::CoopEvent& e) {
    using drakan::net::CoopKind;
    std::vector<uint8_t> d(kEventHeader);
    d[0] = static_cast<uint8_t>(e.kind);
    std::memcpy(&d[1], &e.levelHash, 4);
    d[5] = static_cast<uint8_t>(e.player);
    auto put = [&d](const void* v, size_t n) {
        const auto* b = static_cast<const uint8_t*>(v);
        d.insert(d.end(), b, b + n);
    };
    switch (e.kind) {
        case CoopKind::CreatureDied:
        case CoopKind::ObjGone:
            put(&e.key, 4);
            break;
        case CoopKind::Claim:
        case CoopKind::ObjState:
            put(&e.key, 4);
            put(&e.value, 4);
            break;
        case CoopKind::Hurt:
            put(&e.value, 4);
            break;
        case CoopKind::ObjMsg: {
            const uint16_t pad = 0;
            put(&e.key, 4);
            put(&e.msgType, 2);
            put(&pad, 2);
            put(e.w, 16);
            break;
        }
        default:
            break;
    }
    return d;
}

bool creaturesFromLegacy(const std::vector<uint8_t>& d, drakan::net::CoopCreatures& c) {
    if (d.size() < kHeaderBytes || d[0] != kMsgCreatures)
        return false;
    uint16_t count;
    std::memcpy(&c.levelHash, &d[1], 4);
    std::memcpy(&c.sequence, &d[5], 4);
    std::memcpy(&count, &d[9], 2);
    if (count > drakan::net::kMaxCoopCreatures || d.size() != kHeaderBytes + size_t(count) * kEntryBytes)
        return false;
    c.creatures.clear();
    for (uint16_t i = 0; i < count; ++i) {
        const uint8_t* p = &d[kHeaderBytes + size_t(i) * kEntryBytes];
        drakan::net::CoopCreature e;
        std::memcpy(&e.key, p, 4);
        std::memcpy(e.pos, p + 4, 12);
        std::memcpy(&e.yaw, p + 16, 4);
        std::memcpy(&e.hp, p + 20, 4);
        c.creatures.push_back(e);
    }
    return true;
}

// Moves what the co-op logic queued (EE thread / the message hook) to the session's MatchClient and what the server
// relayed into the logic's inboxes. No socket here: drakan_netplay's network thread owns the connection.
void netThread() {
    while (!g_stop.load()) {
        const uint64_t now = nowMs();
        drakan::net::CoopCreatures cc;
        while (drakan_netplay::coopPoll(cc)) {
            if (g_host || !g_enabled.load())
                continue; // the owner of the creature world never takes a stream
            std::lock_guard<std::mutex> l(g_s.m);
            if (cc.levelHash != g_s.inLevel) {
                g_s.inbox.clear(); // the host changed level: forget the old level's creatures
                g_s.inLevel = cc.levelHash;
            }
            ++g_s.packets;
            for (const drakan::net::CoopCreature& e : cc.creatures) {
                Remote r;
                r.e.key = e.key;
                std::memcpy(r.e.pos, e.pos, sizeof r.e.pos);
                r.e.yaw = e.yaw;
                r.e.hp = e.hp;
                r.atMs = now;
                g_s.inbox[r.e.key] = r;
            }
        }
        drakan::net::CoopEvent ce;
        while (drakan_netplay::coopPoll(ce)) {
            if (!g_enabled.load())
                continue;
            std::lock_guard<std::mutex> l(g_s.m);
            g_s.evIn.push_back(eventToLegacy(ce));
            ++g_s.eventsIn;
        }
        std::vector<std::vector<uint8_t>> out, rel;
        {
            std::lock_guard<std::mutex> l(g_s.m);
            out.swap(g_s.outbox);
            rel.swap(g_s.relOut);
        }
        if (g_enabled.load()) {
            for (const auto& d : out) {
                drakan::net::CoopCreatures c;
                if (g_host && creaturesFromLegacy(d, c)) {
                    drakan_netplay::coopSend(c);
                    std::lock_guard<std::mutex> l(g_s.m);
                    ++g_s.datagramsOut;
                }
            }
            for (const auto& d : rel) {
                drakan::net::CoopEvent e;
                if (eventFromLegacy(d, e)) {
                    drakan_netplay::coopSend(e); // Hurt / Respawn: player 0 = the other players
                    std::lock_guard<std::mutex> l(g_s.m);
                    ++g_s.eventsOut;
                }
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
    float pos0[3] = {};         // where it lay when it was keyed (a loose item that is taken is moved to the inventory)
    uint64_t rebaseUntilMs = 0; // pickups near an opening chest: the item flies about, so pos0 follows it until then
    int doorState = -1;         // doors: last seen data+0x10 == 1 (open) as 0 / 1, -1 unknown
};

// A chest (class 0x100c5): the engine's chest component (vtable 0x523150, coop-design.md section 9) opens when the
// controlled actor is near and on its open side, and spews its loot 1.5 s later with the engine's RNG. Both sides
// report the opening; the host's spew is replayed on the client with the host's RNG state, so the same loot appears.
struct ChestSync {
    bool inited = false;      // first poll done (the state it started in is known)
    bool opened = false;      // it opened during play (a chest placed open is not): the late-join catch-up lists these
    bool openSeen = false;    // the local component is (or was made) open: reported or applied
    bool pendingOpen = false; // the partner opened it: open the local one at the next boundary
    bool spewKnown = false;   // client: the host's RNG state for the spew is here
    bool spewDone = false;    // this side's spew finished
    bool firstCall = false;   // host: the spew's first call happened (its RNG state was sent)
    bool applied = false;     // client: the host's RNG state was put in for the first call
    bool healed = false;      // client: the loot was checked after the spew (nothing left parked)
    int healTries = 0;
    uint64_t appliedMs = 0;
    uint32_t seed = 0, carry = 0;
    uint32_t seq = 0; // host: order of the spews (the generator hands the gold of all chests to the first caller)
    int32_t budget = kNoBudget; // the chest's gold budget in the generator's table (random per world)
    uint64_t waitSince = 0;
};

std::unordered_map<uint32_t, Keyed> g_keyed; // handle -> key
std::unordered_map<uint32_t, uint32_t> g_keyToHandle;
std::unordered_map<uint32_t, Obj> g_obj;
std::unordered_map<uint32_t, uint32_t> g_objKeyToHandle;
std::set<uint32_t> g_goneKeys;    // pickups taken this level (either side): the late-join catch-up
std::set<uint32_t> g_pendingGone; // taken pickups the partner reported before the loot they name exists here
uint32_t g_spewSeq = 0;           // host: spews numbered in the order they began
std::unordered_map<uint32_t, ChestSync> g_chest;      // object key -> chest state
std::unordered_map<uint32_t, uint32_t> g_chestByComp; // chest component pointer -> object key
PS2Runtime::RecompiledFunction g_origSpew = nullptr;
std::set<uint32_t> g_forceOpen;                      // client: doors (handles) the host reported open at the catch-up
uint32_t g_syncRequests = 0;                         // host: catch-up requests folded into the next catch-up
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
    int player; // the claimant (stamped by the server)
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

// AI targeting and partner respawns use peer bodies. The host's own selected costume is also a ghost.
std::vector<uint32_t> ghostHandles() {
    std::vector<uint32_t> v;
    drakan_ghost::forEach([&](const drakan_ghost::Info& i) {
        if (i.handle && i.remotePlayer)
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
// Host: one slot per remote player (keyed by player id, created when it first reports). A player who is down keeps its
// ghost but is no target for creatures, its claims are refused and it gets a Respawn after kRespawnDelayMs.
struct PlayerSlot {
    bool dead = false;
    uint64_t deadMs = 0;
};
std::map<int, PlayerSlot> g_players;
bool playerDown(int player) {
    const auto it = g_players.find(player);
    return it != g_players.end() && it->second.dead;
}
// The remote player behind a ghost actor (0 = not a ghost) and a player's ghost actor (0 = none shown right now).
int playerOfHandle(uint32_t h) {
    int p = 0;
    drakan_ghost::forEach([&](const drakan_ghost::Info& i) {
        if (i.handle && i.handle == h)
            p = drakan_netplay::playerOfGhost(i.id);
    });
    return p;
}
uint32_t handleOfPlayer(int player) {
    const int id = player > 0 ? drakan_netplay::ghostOfPlayer(player) : 0;
    uint32_t h = 0;
    if (id)
        drakan_ghost::forEach([&](const drakan_ghost::Info& i) {
            if (i.id == id)
                h = i.handle;
        });
    return h;
}
bool handleDown(uint32_t h) {
    const int p = playerOfHandle(h);
    return p && playerDown(p);
}

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
            // Beside the creature world's owner (its ghost), else beside any other player that is shown.
            uint32_t g = handleOfPlayer(drakan_netplay::coopLink().authority);
            if (!g) {
                const auto gs = ghostHandles();
                if (!gs.empty())
                    g = gs.front();
            }
            if (g) {
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
            coopLog("client: respawned %s (HP -> %d)", g ? "beside the host" : "where she fell (no host ghost)",
                    g_life.pendingHp);
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
            // Candidates: the local player (0) and the ghost of every remote player who is not down and is within
            // aggro range (a far ghost would send the AI after a target it cannot reach). The nearest wins; the
            // creature's current target is kept unless the nearest is clearly nearer (20 percent hysteresis on the
            // distance, so two equidistant players do not flip every tick).
            constexpr float kRange2 = 6000.0f * 6000.0f;
            const uint32_t last = g_aiLast.count(actor) ? g_aiLast[actor] : 0;
            float best = 1e30f, dLast = dp;
            uint32_t bestG = 0;
            bool lastSeen = false; // the creature's current ghost target is still a candidate
            for (uint32_t g : ghostHandles()) {
                if (!ramPtr(rd32(rdram, g + 0xfc)) || g == saved || handleDown(g))
                    continue;
                const float dg = d2(g);
                if (dg >= kRange2)
                    continue;
                if (g == last) {
                    dLast = dg;
                    lastSeen = true;
                }
                if (dg < best) {
                    best = dg;
                    bestG = g;
                }
            }
            const bool lastIsGhost = last && lastSeen;
            const uint32_t cur = lastIsGhost ? last : 0;
            const float dCur = lastIsGhost ? dLast : dp;
            // The nearest candidate overall (the local player when no ghost is nearer).
            const uint32_t nearestId = bestG && best < dp ? bestG : 0;
            const float nearestD = nearestId ? best : dp;
            uint32_t pick = cur;
            if (g_forceGhost && (g_forceAll || g_forced.count(actor)) && bestG)
                pick = bestG;
            else if (nearestId != cur && nearestD < dCur * 0.64f)
                pick = nearestId;
            alt = pick;
            if (alt != last) {
                g_aiLast[actor] = alt;
                ++g_st.aiSwaps;
                if (g_aiLogged++ < 24)
                    coopLog(
                        "host: creature %08x now targets %s (player %d; nearest ghost %.0f, local player %.0f units)",
                        actor, alt ? "a remote player's ghost" : "the local player", alt ? playerOfHandle(alt) : 0,
                        bestG ? std::sqrt(best) : -1.0, std::sqrt(dp));
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
    c->pc = orig;
    constexpr uint32_t kLastActor = PS2_RAM_SIZE - 0x14 - ScopedAiTarget::kTransformBytes;
    if (!alt || saved > kLastActor || alt > kLastActor) {
        fn(rdram, c, rt);
        return;
    }
    try {
        ScopedAiTarget target(rdram + saved + 0x14, rdram + alt + 0x14, g_aiSwapActive);
        fn(rdram, c, rt);
    } catch (...) {
        static unsigned interrupted = 0;
        if (g_probe && interrupted++ < 8)
            coopLog("restored local transform after interrupted target handler %08x (actor %08x)", orig, actor);
        throw;
    }
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
    g_players.clear();
    g_goneKeys.clear();
    g_pendingGone.clear();
    g_chest.clear();
    g_chestByComp.clear();
    g_forceOpen.clear();
    g_syncPending = g_syncSent = false;
    g_syncRequests = 0;
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
    // The inventory's own copies (what taking an item leaves behind) are parked near the origin of the world (the
    // levels lie at 1e5..1e6), a taken sack's copy too.
    const bool nearOrigin = std::fabs(e.position.x) < 20000.0f && std::fabs(e.position.z) < 20000.0f;
    if (nearOrigin)
        return false;
    if (e.classId == 0x1006du)
        return true;
    return e.type == 11 && !g_parked.count(posKey(e));
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
        if (isGhostHandle(e.handle))
            continue; // a partner's stand-in character (drakan_coop_character) is an NPC-class actor: not a placed
                      // creature
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
    // The local copy may have been collected before the peer's ObjGone arrived. Never
    // leave a cell or hand pointing into the actor that the frame tail will free.
    const uint32_t me = controlled(rdram);
    world::EntitySnapshot owner;
    const bool nativePlayer =
        world::readEntity(RdramMemory(rdram), g_globals, eng, me, owner) && owner.classId == world::kClassRynn;
    const uint32_t player = nativePlayer ? creatureData(rdram, me) : 0;
    const unsigned detached = world::detachInventoryItem(
        owner.classId, player, actor, [&](uint32_t address) { return rd32(rdram, address); },
        [&](uint32_t address, uint32_t value) { guest.write32(address, value); });
    if (detached)
        coopLog("pickup copy %08x: cleared %u inventory references before removal", actor, detached);
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
            if (!isLocalAttacker(rdram, sender) && dmg > 0) {
                // A host creature (or anything else) hurt a remote player's ghost: that player takes the damage (a
                // player who is down takes none; a ghost struck by another ghost's weapon is not a creature hit).
                const int victim = playerOfHandle(target);
                if (victim && !playerDown(victim) && !playerOfHandle(sender)) {
                    const int32_t d = dmg;
                    pushReliableTo(static_cast<uint8_t>(victim), kMsgHurt, &d, 4);
                    ++g_st.hurtSent;
                    coopLog("host: ghost %08x (player %d) hit by %08x for %d -> that player", target, victim, sender,
                            d);
                }
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
    // Respawns, per player. When the host's own player is down too (HP 0: the engine's game over reloads the last save)
    // nobody is left to respawn beside, so the others get up at once, where they fell; the usual delay otherwise.
    {
        const bool hostDown = !g_players.empty() && playerHp(rdram) <= 0;
        for (auto& [pid, slot] : g_players) {
            if (!slot.dead || (!hostDown && nowMs() - slot.deadMs < kRespawnDelayMs))
                continue;
            slot.dead = false;
            pushReliableTo(static_cast<uint8_t>(pid), kMsgRespawn, nullptr, 0);
            coopLog("host: player %d's respawn is granted (%s); its ghost is a target again", pid,
                    hostDown ? "every player is down: the host world reloads, no host to respawn beside"
                             : "beside the host");
        }
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
        else if (playerDown(cl.player))
            why = "claimant is down";
        else {
            // The claimant must be near: its own ghost within 3000 units of the creature (when that ghost exists).
            const uint32_t g = handleOfPlayer(cl.player);
            if (g && std::hypot(rdF(rdram, g + 0x14) - rdF(rdram, h + 0x14),
                                rdF(rdram, g + 0x1c) - rdF(rdram, h + 0x1c)) > 3000.0f)
                why = "claimant too far";
        }
        if (why) {
            ++g_st.claimsRejected;
            coopLog("host: claim from player %d rejected: %s (key %08x damage %d)", cl.player, why, cl.key, cl.damage);
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
            const uint32_t claimant = handleOfPlayer(cl.player);
            const uint32_t sender = claimant ? claimant : controlled(rdram);
            const int32_t before = creatureHp(rdram, h);
            if (sendEngineMessage(guest, rdram, sender, h, msg)) {
                ++g_st.claimsApplied;
                coopLog("host: claim from player %d applied to creature %08x key %08x: damage %d, hp was %d", cl.player,
                        h, cl.key, cl.damage, before);
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

bool isSpewMessage(const uint8_t* p) { // an ObjMsg payload (key, u16 type, ...) of the chest spew pseudo type
    uint16_t t;
    std::memcpy(&t, p + 4, 2);
    return t == kChestSpewType;
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
            cl.player = d[5];
            g_claims.push_back(cl);
        } else if (type == kMsgHurt && !g_host && n >= 4) {
            int32_t dmg;
            std::memcpy(&dmg, p, 4);
            if (!g_life.dead)
                g_pendingHurt += std::clamp(static_cast<int32_t>(dmg * g_hurtScale), 0, 1000);
        } else if (type == kMsgPlayerDied && g_host) {
            PlayerSlot& slot = g_players[d[5]];
            slot.dead = true;
            slot.deadMs = nowMs();
            coopLog("host: player %d died: creatures stop targeting its ghost, respawn in %llu ms", int(d[5]),
                    static_cast<unsigned long long>(kRespawnDelayMs));
        } else if (type == kMsgSyncReq && g_host) {
            g_syncPending = true;
            ++g_syncRequests;
            coopLog("host: player %d asked for the catch-up state", int(d[5]));
        } else if (type == kMsgObjState && n >= 8) {
            uint32_t key;
            int32_t state;
            std::memcpy(&key, p, 4);
            std::memcpy(&state, p + 4, 4);
            auto oi = g_objKeyToHandle.find(key);
            if (oi == g_objKeyToHandle.end())
                continue;
            const uint32_t cls = g_obj[oi->second].classId;
            if (cls == 0x10013u && !g_host) {
                if (state == kStateDoorOpen) {
                    g_forceOpen.insert(oi->second);
                    coopLog("client: door %08x (key %08x) was open on the host: opening it", oi->second, key);
                } else if (state == kStateDoorClosed && g_forceOpen.erase(oi->second)) {
                    coopLog("client: door %08x (key %08x) closed again on the host", oi->second, key);
                }
            } else if (cls == kChestClass && state == kStateChestOpen) {
                ChestSync& cs = g_chest[key];
                if (!cs.openSeen)
                    cs.pendingOpen = true;
            }
        } else if (type == kMsgRespawn && !g_host) {
            if (g_life.dead)
                g_life.respawnReq = true;
        } else if (type == kMsgObjGone && n >= 4) {
            uint32_t key;
            std::memcpy(&key, p, 4);
            ++g_st.goneRecv;
            auto oi = g_objKeyToHandle.find(key);
            if (oi == g_objKeyToHandle.end()) {
                g_pendingGone.insert(key); // loot that is not here yet (a spew still to come): removed when it appears
                g_goneKeys.insert(key);
                continue;
            }
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
        } else if (type == kMsgObjMsg && n >= 4 + 4 + 16 && isSpewMessage(p)) {
            // The host's chest spew (RNG state and gold carry): the client's chest replays it with these.
            if (!g_host) {
                uint32_t key;
                int32_t w[4];
                std::memcpy(&key, p, 4);
                std::memcpy(w, p + 8, 16);
                ChestSync& cs = g_chest[key];
                cs.spewKnown = true;
                cs.seed = static_cast<uint32_t>(w[0]);
                cs.carry = static_cast<uint32_t>(w[1]);
                cs.budget = w[2];
                cs.seq = static_cast<uint32_t>(w[3]);
                coopLog("client: the host's chest spew for key %08x arrived (rng %08x)", key, cs.seed);
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
void keyChestChildren(const uint8_t* rdram, uint32_t chest, uint32_t chestKey);
void scanNewObjects() {
    // Loot of a chest whose spew has begun is keyed by (chest, child index) first (a spew the scheduler preempted
    // resumes without passing the hook, and its children are visible here before any other path keyed them).
    if (g_rdramForLog) {
        std::vector<std::pair<uint32_t, uint32_t>> chests;
        for (const auto& [h, o] : g_obj)
            if (o.classId == kChestClass) {
                const auto cs = g_chest.find(o.key);
                if (cs != g_chest.end() && (cs->second.firstCall || cs->second.applied))
                    chests.emplace_back(h, o.key);
            }
        for (const auto& [h, key] : chests)
            keyChestChildren(g_rdramForLog, h, key);
    }
    refreshParked(g_snap);
    for (const auto& e : g_snap.entities) {
        if (g_probe && e.type == 11 && !isPickupEntity(e) && !g_obj.count(e.handle)) {
            static std::set<uint32_t> shown; // an item that is not (yet) placed in the world: where does it sit?
            if (shown.insert(e.handle).second && shown.size() < 60 && g_keyedThisLevel)
                coopLog("%s: item %08x class %x res %x is not a world pickup (at %.0f %.0f %.0f)",
                        g_host ? "host" : "client", e.handle, e.classId, e.resourceId, e.position.x, e.position.y,
                        e.position.z);
        }
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
        o.rebaseUntilMs = nowMs() + 10000;
        std::memcpy(o.pos0, &e.position, 12);
        const uint32_t cls = e.resourceId * 2654435761u ^ e.classId;
        o.key = mixKey(cls, 0x10000u + g_dynCounter[cls]++);
        g_obj[e.handle] = o;
        g_objKeyToHandle[o.key] = e.handle;
        coopLog("%s: new pickup %08x class %x type %u res %x at %.0f %.0f %.0f -> key %08x", g_host ? "host" : "client",
                e.handle, e.classId, e.type, e.resourceId, e.position.x, e.position.y, e.position.z, o.key);
        if (g_probe && g_rdramForLog) { // what the loot is: the first words of its instance data (the gold amount)
            const uint32_t in = rd32(g_rdramForLog, e.handle + 0xfc);
            const uint32_t q = ramPtr(in) ? rd32(g_rdramForLog, in + 0x10) : 0;
            if (ramPtr(q))
                coopLog("  loot key %08x data: %08x %08x %08x %08x %08x %08x", o.key, rd32(g_rdramForLog, q + 0x10),
                        rd32(g_rdramForLog, q + 0x14), rd32(g_rdramForLog, q + 0x18), rd32(g_rdramForLog, q + 0x1c),
                        rd32(g_rdramForLog, q + 0x20), rd32(g_rdramForLog, q + 0x24));
        }
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
    if (!g_retarget || !g_aiSlot)
        return;
    const RdramMemory mem(rdram);
    for (auto& [h, o] : g_obj)
        // A door that is gone, or whose handle was recycled for another object, must not get the wrapper: it would
        // be written into whatever lives in that handle now.
        if (o.classId == 0x10013u && !o.swapped && !o.gone && world::isLiveHandle(mem, g_globals, h, o.ref - 1))
            o.swapped = swapHandlerTo(rdram, h, g_aiSlot);
}

// ------------------------------------------------------------------------------------------------------- chests
constexpr uint32_t kChestOpenFn = 0x227d80u, kChestSpewFn = 0x227ec8u, kChestVtbl = 0x523150u;
constexpr uint32_t kRngPtr = 0x518e94u;  // pointer to the block whose +0x58 is the engine's LCG state (0x4ce028)
constexpr uint32_t kGoldGen = 0x4fbee0u; // the gold generator: +0x24 is the carried remainder of the gold budget
constexpr uint64_t kSpewWaitMs = 10000;  // client: how long a spew waits for the host's RNG state

// The chest component of a chest actor (actor+0xfc), or 0.
uint32_t chestComp(const uint8_t* rdram, uint32_t actor) {
    const uint32_t comp = rd32(rdram, actor + 0xfc);
    if (!ramPtr(comp) || rd32(rdram, comp + 0xc) != drakan_region::guestAddress(kChestVtbl) ||
        rd32(rdram, comp + 8) != actor)
        return 0;
    return comp;
}

// The chest's gold budget cell in the generator's table (12-byte entries {actor, budget, ?}), or 0.
uint32_t goldBudgetCell(const uint8_t* rdram, uint32_t actor) {
    const uint32_t gen = drakan_region::guestAddress(kGoldGen);
    const uint32_t tab = rd32(rdram, gen + 0x18);
    const int32_t n = static_cast<int32_t>(rd32(rdram, gen + 0x20));
    if (!ramPtr(tab) || n <= 0 || n > 4096)
        return 0;
    for (int32_t i = 0; i < n; ++i)
        if (rd32(rdram, tab + i * 12u) == actor)
            return tab + i * 12u + 4;
    return 0;
}

uint32_t rngStateAddr(const uint8_t* rdram) {
    const uint32_t blk = rd32(rdram, drakan_region::guestAddress(kRngPtr));
    return ramPtr(blk) ? blk + 0x58 : 0;
}

// Items near a chest that opens fly out of it: their start position is not where "taken" is measured from.
void rebaseLoot(const uint8_t* rdram, uint32_t chest, uint64_t now) {
    for (auto& [h, o] : g_obj)
        if (o.pickup && !o.gone &&
            std::hypot(rdF(rdram, h + 0x14) - rdF(rdram, chest + 0x14),
                       rdF(rdram, h + 0x1c) - rdF(rdram, chest + 0x1c)) < 3000.0f)
            o.rebaseUntilMs = now + 10000;
}

// Probe: the gold generator's denomination table (12-byte entries {?, flag, amount}) as one log line.
void logGoldTable(const uint8_t* rdram, const char* who, uint32_t key) {
    const uint32_t gen = drakan_region::guestAddress(kGoldGen);
    const uint32_t tab = rd32(rdram, gen);
    const int32_t n = static_cast<int32_t>(rd32(rdram, gen + 8));
    if (!ramPtr(tab) || n <= 0 || n > 64)
        return;
    char buf[300];
    int len = std::snprintf(buf, sizeof buf, "%s: gold table at chest %08x (%d entries, carry %d):", who, key, n,
                            static_cast<int32_t>(rd32(rdram, gen + 0x24)));
    for (int32_t i = 0; i < n && len < 260; ++i)
        len +=
            std::snprintf(buf + len, sizeof buf - len, " %d/%d", static_cast<int32_t>(rd32(rdram, tab + i * 12u + 4)),
                          static_cast<int32_t>(rd32(rdram, tab + i * 12u + 8)));
    coopLog("%s", buf);
}

// Keys the loot a chest's spew just made (the chest's children, actor+0x90 refs / +0x98 count) at the spew itself:
// (chest key, index in the child list). Both worlds run the same spew with the same RNG, so the list is the same, and
// an item that one player takes the instant it appears already has its key (scanNewObjects only sees what is still
// lying about at its next refresh and keys by order of appearance, which the early taker never gave it).
void addChild(uint32_t handle, uint32_t chestKey, uint32_t ordinal, uint32_t ref, uint32_t resId, uint32_t classId,
              uint32_t type, const char* how) {
    Obj o;
    o.resId = resId;
    o.classId = classId;
    o.type = type;
    o.ref = ref;
    o.pickup = true;
    o.rebaseUntilMs = nowMs() + 10000;
    o.key = mixKey(chestKey, 0x20000u + ordinal);
    g_obj[handle] = o;
    g_objKeyToHandle[o.key] = handle;
    coopLog("%s: chest key %08x child %u -> pickup %08x class %x res %x key %08x (keyed %s)",
            g_host ? "host" : "client", chestKey, ordinal, handle, classId, resId, o.key, how);
}

void keyChestChildren(const uint8_t* rdram, uint32_t chest, uint32_t chestKey) {
    const uint32_t refs = rd32(rdram, chest + 0x90);
    const int32_t n = static_cast<int32_t>(rd32(rdram, chest + 0x98));
    const uint32_t ctx = engineCtx(rdram);
    if (!ramPtr(refs) || n <= 0 || n > 64 || !ramPtr(ctx))
        return;
    for (int32_t i = 0; i < n; ++i) {
        const uint32_t child = registryObject(rdram, rd16(rdram, refs + 2u * i));
        if (!ramPtr(child) || rd32(rdram, child + 0xc) != 11)
            continue;
        world::EntitySnapshot e;
        if (!world::readEntity(RdramMemory(rdram), g_globals, ctx, child, e))
            continue;
        // A handle the engine recycled for the new loot may still be filed as an earlier pickup that a player took a
        // frame before the spew (watchObjects has not seen it leave yet): that entry is stale and the child is a new
        // item. The old pickup's removal is reported now, or the partner would keep its copy.
        const auto old = g_obj.find(child);
        if (old != g_obj.end()) {
            Obj& prev = old->second;
            if (prev.resId == e.resourceId && prev.ref == e.ref)
                continue;
            if (prev.pickup && !prev.gone) {
                prev.gone = true;
                g_goneKeys.insert(prev.key);
                pushReliable(kMsgObjGone, &prev.key, 4);
                ++g_st.goneSent;
                coopLog("%s: pickup %08x (class %x key %08x) left the registry (its handle was reused by chest loot) "
                        "-> peer",
                        g_host ? "host" : "client", child, prev.classId, prev.key);
            }
            g_objKeyToHandle.erase(prev.key);
        }
        addChild(child, chestKey, static_cast<uint32_t>(i), e.ref, e.resourceId, e.classId, e.type, "from the list");
    }
}

void sendSpew(uint32_t key, uint32_t seed, uint32_t carry, int32_t budget, uint32_t seq) {
    struct {
        uint32_t key;
        uint16_t type, pad;
        int32_t w[4];
    } p{key, kChestSpewType, 0, {int32_t(seed), int32_t(carry), budget, int32_t(seq)}};
    pushReliable(kMsgObjMsg, &p, sizeof p);
}

// The engine's spew (0x227ec8, called by the chest's tick once its 1.5 s delay is over; returns 0 = not yet, the tick
// asks again next frame). Host: remember the RNG state before it and tell the client. Client: wait for the host's state
// and run the spew with it, so the loot is the host's loot.
// The original spew without scheduler preemption: a preempted spew resumes later outside this hook, and its children
// (made while it runs) could be taken by a player before anything keyed them. DRAKAN_COOP_SPEW_PREEMPT=1 allows
// preemption.
void runSpew(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    static const bool allow = std::getenv("DRAKAN_COOP_SPEW_PREEMPT") != nullptr;
    if (allow) {
        g_origSpew(rdram, c, rt);
        return;
    }
    ps2xNoPreemptEnter();
    g_origSpew(rdram, c, rt);
    ps2xNoPreemptLeave();
}

void hookSpew(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t comp = getRegU32(c, 4);
    auto ci = g_chestByComp.find(comp);
    if (!g_enabled.load(std::memory_order_relaxed) || g_levelHash.load() == 0 || ci == g_chestByComp.end()) {
        runSpew(rdram, c, rt);
        return;
    }
    const uint32_t key = ci->second;
    ChestSync& cs = g_chest[key];
    // The cooperative scheduler can preempt the spew mid-way (the heap semaphore, a checkpoint): the callee then
    // unwinds with the guest pc left inside it and resumes later, not through this hook. Only a call that returned to
    // its caller (pc == the return address) has a result.
    const uint32_t ra = getRegU32(c, 31);
    const uint32_t seedAt = rngStateAddr(rdram);
    const uint32_t carryAt = drakan_region::guestAddress(kGoldGen) + 0x24;
    // The generator runs on the first call (it takes the chest's gold budget out of the table and draws the gold with
    // the RNG); a call that returns 0 (a child is still moving) is asked again next frame and generates nothing more.
    // So the state that matters is the state at the first call.
    if (g_host) {
        if (!cs.firstCall && seedAt) {
            cs.firstCall = true;
            const uint32_t cell = goldBudgetCell(rdram, rd32(rdram, comp + 8));
            cs.seed = rd32(rdram, seedAt);
            cs.carry = rd32(rdram, carryAt);
            cs.budget = cell ? static_cast<int32_t>(rd32(rdram, cell)) : kNoBudget;
            cs.seq = ++g_spewSeq;
            sendSpew(key, cs.seed, cs.carry, cs.budget, cs.seq);
            if (g_probe)
                logGoldTable(rdram, "host", key);
            coopLog("host: chest key %08x spews its loot (rng %08x, gold carry %d, gold budget %d) -> client", key,
                    cs.seed, int32_t(cs.carry), cs.budget);
        }
        runSpew(rdram, c, rt);
        keyChestChildren(rdram, rd32(rdram, comp + 8), key);
        if (g_probe) {
            static uint32_t shown = 0;
            if (shown++ < 40)
                coopLog("host: chest key %08x spew call: %s, result %u, %d children listed", key,
                        c->pc == ra ? "returned" : "preempted", getRegU32(c, 2),
                        static_cast<int32_t>(rd32(rdram, rd32(rdram, comp + 8) + 0x98)));
        }
        if (c->pc == ra && getRegU32(c, 2) != 0)
            cs.spewDone = true;
        return;
    }
    if (cs.spewDone || !seedAt) {
        runSpew(rdram, c, rt);
        return;
    }
    if (!cs.spewKnown && !cs.applied) {
        const uint64_t now = nowMs();
        if (!cs.waitSince)
            cs.waitSince = now;
        if (now - cs.waitSince < kSpewWaitMs) {
            SET_GPR_U32(c, 2, 0u); // not yet: the chest's tick calls again next frame
            c->pc = getRegU32(c, 31);
            return;
        }
        coopLog("client: chest key %08x: no spew from the host after %llu ms, spewing with the local RNG", key,
                static_cast<unsigned long long>(kSpewWaitMs));
        cs.applied = true;
    }
    if (!cs.applied && cs.seq) {
        // The host's gold generator hands the loot of every chest in its table to whichever chest spews first: spew in
        // the host's order, or the same children would be listed under another chest here.
        bool earlier = false;
        for (const auto& [k2, c2] : g_chest)
            if (k2 != key && c2.spewKnown && !c2.applied && c2.seq && c2.seq < cs.seq)
                earlier = true;
        if (earlier && !cs.waitSince)
            cs.waitSince = nowMs();
        if (earlier && nowMs() - cs.waitSince < kSpewWaitMs) {
            SET_GPR_U32(c, 2, 0u);
            c->pc = getRegU32(c, 31);
            return;
        }
    }
    const bool apply = !cs.applied;
    if (apply) {
        cs.applied = true;
        cs.appliedMs = nowMs();
        wr32(rdram, seedAt, cs.seed);
        wr32(rdram, carryAt, cs.carry);
        // The gold each chest holds was dealt out with the engine's RNG: the host's number wins (none: no gold).
        const int32_t want = cs.budget == kNoBudget ? 0 : cs.budget;
        if (const uint32_t cell = goldBudgetCell(rdram, rd32(rdram, comp + 8))) {
            if (static_cast<int32_t>(rd32(rdram, cell)) != want)
                coopLog("client: chest key %08x: gold budget %d here, %d on the host: using the host's", key,
                        static_cast<int32_t>(rd32(rdram, cell)), want);
            wr32(rdram, cell, static_cast<uint32_t>(want));
        } else if (want) {
            coopLog("client: chest key %08x: no gold budget entry here (the host had %d)", key, want);
        }
        if (g_probe)
            logGoldTable(rdram, "client", key);
        coopLog("client: chest key %08x spews the host's loot (rng %08x, gold carry %d, gold budget %d)", key, cs.seed,
                int32_t(cs.carry), cs.budget);
    }
    runSpew(rdram, c, rt);
    keyChestChildren(rdram, rd32(rdram, comp + 8), key);
    // The RNG state is left as the spew made it: a preempted spew carries on later with it (putting the local state
    // back now would hand the rest of the loot to the wrong random stream), and the stream stays random either way.
    if (g_probe) {
        static uint32_t shown = 0;
        if (shown++ < 40)
            coopLog("client: chest key %08x spew call (first %d): %s, result %u", key, apply,
                    c->pc == ra ? "returned" : "preempted", getRegU32(c, 2));
    }
    if (c->pc == ra && getRegU32(c, 2) != 0)
        cs.spewDone = true;
}

// Client: loot of a chest that was made at the spew but never flew out (the new treasure sits parked near the world
// origin) is thrown out by asking the spew again: the gold generator has nothing left to do, the launch loop takes
// every child of the chest.
int parkedLoot(const uint8_t* rdram, uint32_t chest) {
    const uint32_t refs = rd32(rdram, chest + 0x90);
    const int32_t n = static_cast<int32_t>(rd32(rdram, chest + 0x98));
    if (!ramPtr(refs) || n <= 0 || n > 64)
        return 0;
    int parked = 0;
    for (int32_t i = 0; i < n; ++i) {
        const uint32_t child = registryObject(rdram, rd16(rdram, refs + 2u * i));
        if (!ramPtr(child) || rd32(rdram, child + 0xc) != 11)
            continue;
        if (std::fabs(rdF(rdram, child + 0x14)) < 20000.0f && std::fabs(rdF(rdram, child + 0x1c)) < 20000.0f)
            ++parked;
    }
    return parked;
}

void healChest(drakan_input::IGuest& guest, const uint8_t* rdram, uint32_t chest, uint32_t comp, ChestSync& cs,
               uint32_t key) {
    if (g_host || !cs.applied || cs.healed || nowMs() - cs.appliedMs < 2500)
        return;
    const int parked = parkedLoot(rdram, chest);
    if (!parked) {
        cs.healed = true;
        return;
    }
    if (cs.healTries >= 3) {
        cs.healed = true;
        coopLog("client: chest key %08x: %d loot items are still parked after %d tries", key, parked, cs.healTries);
        return;
    }
    if (guest.postBusy())
        return;
    ++cs.healTries;
    cs.appliedMs = nowMs();
    guest.post(drakan_input::GuestCall(drakan_region::guestAddress(kChestSpewFn)).arg(comp));
    coopLog("client: chest key %08x: %d loot items never flew out, spewing again (try %d)", key, parked, cs.healTries);
}

// Both sides, every refresh: report a chest that opened here, open one the partner opened.
void pollChests(drakan_input::IGuest& guest, uint8_t* rdram) {
    const uint64_t now = nowMs();
    const RdramMemory mem(rdram);
    std::vector<std::pair<uint32_t, uint32_t>> spewed; // (chest handle, key) of chests whose spew has begun
    struct KeyLater {                                  // keys the children after the loop: it inserts into g_obj
        std::vector<std::pair<uint32_t, uint32_t>>& v;
        const uint8_t* ram;
        ~KeyLater() {
            for (const auto& [chest, key] : v)
                keyChestChildren(ram, chest, key);
        }
    } keyLater{spewed, rdram};
    for (auto& [h, o] : g_obj) {
        if (o.classId != kChestClass || !world::isLiveHandle(mem, g_globals, h, o.ref - 1))
            continue;
        const uint32_t comp = chestComp(rdram, h);
        if (!comp)
            continue;
        g_chestByComp[comp] = o.key;
        ChestSync& cs = g_chest[o.key];
        // A spew that the cooperative scheduler preempted resumes without passing the hook: its children are keyed
        // here.
        if (cs.firstCall || cs.applied)
            spewed.emplace_back(h, o.key);
        healChest(guest, rdram, h, comp, cs, o.key);
        const int32_t st = static_cast<int32_t>(rd32(rdram, comp + 0x10));
        if (!cs.inited) {
            cs.inited = true;
            if (st == kStateChestOpen)
                cs.openSeen = true; // placed open: nothing to replicate
            if (g_probe) {
                const uint32_t cell = goldBudgetCell(rdram, h);
                coopLog("chest %08x key %08x: state %d, lid ref %u, side vector %.2f %.2f %.2f, delay %.2f, gold %d", h,
                        o.key, st, rd32(rdram, comp + 0x18) & 0xffffu, rdF(rdram, comp + 0x1c), rdF(rdram, comp + 0x20),
                        rdF(rdram, comp + 0x24), rdF(rdram, comp + 0x14),
                        cell ? static_cast<int32_t>(rd32(rdram, cell)) : -1);
            }
        }
        if (st == kStateChestOpen && !cs.openSeen) {
            cs.openSeen = true;
            cs.opened = true;
            const struct {
                uint32_t key;
                int32_t state;
            } p{o.key, kStateChestOpen};
            pushReliable(kMsgObjState, &p, sizeof p);
            rebaseLoot(rdram, h, now);
            coopLog("%s: chest %08x (key %08x) opened by the local player -> peer", g_host ? "host" : "client", h,
                    o.key);
        } else if (cs.pendingOpen) {
            if (st == kStateChestOpen) {
                cs.pendingOpen = false;
                cs.openSeen = true;
            } else if (!guest.postBusy()) {
                // The engine's own open routine: state 2, awake, and the message that swings the lid.
                cs.pendingOpen = false;
                cs.openSeen = true;
                cs.opened = true;
                guest.post(drakan_input::GuestCall(drakan_region::guestAddress(kChestOpenFn)).arg(comp));
                rebaseLoot(rdram, h, now);
                coopLog("%s: chest %08x (key %08x) was opened by the partner: opening it", g_host ? "host" : "client",
                        h, o.key);
            }
        }
    }
}

// Doors: report one that closed again (the partner stops forcing its copy open).
void pollDoors(const uint8_t* rdram) {
    const RdramMemory mem(rdram);
    for (auto& [h, o] : g_obj) {
        if (o.classId != 0x10013u || !world::isLiveHandle(mem, g_globals, h, o.ref - 1))
            continue;
        const uint32_t inner = rd32(rdram, h + 0xfc);
        const uint32_t q = ramPtr(inner) ? rd32(rdram, inner + 0x10) : 0;
        if (!ramPtr(q))
            continue;
        const int now = rd32(rdram, q + 0x10) == static_cast<uint32_t>(kStateDoorOpen) ? 1 : 0;
        if (o.doorState >= 0 && now != o.doorState) {
            coopLog("%s: door %08x (key %08x) %s", g_host ? "host" : "client", h, o.key,
                    now ? "opened" : "closed again");
            if (!now) {
                const struct {
                    uint32_t key;
                    int32_t state;
                } p{o.key, kStateDoorClosed};
                pushReliable(kMsgObjState, &p, sizeof p);
            }
        }
        o.doorState = now;
    }
}

// Taken pickups that were reported before the loot they name existed here (late join, a spew that has not happened
// yet).
void applyPendingGone(drakan_input::IGuest& guest, uint8_t* rdram) {
    for (auto it = g_pendingGone.begin(); it != g_pendingGone.end();) {
        auto oi = g_objKeyToHandle.find(*it);
        if (oi == g_objKeyToHandle.end()) {
            ++it;
            continue;
        }
        Obj& o = g_obj[oi->second];
        if (!o.gone && world::isLiveHandle(RdramMemory(rdram), g_globals, oi->second, o.ref - 1)) {
            o.gone = true;
            destroyActor(guest, rdram, oi->second);
            coopLog("%s: pickup %08x (class %x key %08x) was taken by the partner earlier: removed",
                    g_host ? "host" : "client", oi->second, o.classId, o.key);
        }
        it = g_pendingGone.erase(it);
    }
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
            if (o.rebaseUntilMs > nowMs()) { // loot flying out of a chest that just opened: not "taken"
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
    applyPendingGone(guest, rdram);
    watchObjects(rdram);
    pollChests(guest, rdram);
    pollDoors(rdram);
    if (g_syncPending) {
        g_syncPending = false;
        const uint32_t askers = std::exchange(g_syncRequests, 0u);
        uint32_t doors = 0;
        for (uint32_t key : g_goneKeys)
            pushReliable(kMsgObjGone, &key, 4);
        // Creatures that died before the late joiner keyed the level (a corpse that left the registry is not in the
        // stream any more): their deaths again. The relay is broadcast, so the players already here ignore the repeat
        // (a proxy that is dying, or a key they never had).
        uint32_t deaths = 0;
        for (auto& [h, k] : g_keyed)
            if (k.deadSent) {
                pushReliable(kMsgDied, &k.key, 4);
                ++deaths;
            }
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
        uint32_t chests = 0;
        for (auto& [key, cs] : g_chest) {
            if (!cs.opened)
                continue;
            const struct {
                uint32_t key;
                int32_t state;
            } st{key, kStateChestOpen};
            pushReliable(kMsgObjState, &st, sizeof st);
            if (cs.firstCall)
                sendSpew(key, cs.seed, cs.carry, cs.budget, cs.seq);
            ++chests;
        }
        coopLog("host: catch-up sent (%u request%s): %zu taken pickups, %u open doors, %u open chests, %u deaths",
                askers, askers == 1 ? "" : "s", g_goneKeys.size(), doors, chests, deaths);
    }
    if (g_probe)
        probeDoors(rdram);
    wrapDoors(rdram);
    // Creatures: their own handler gets the retargeting wrapper once (the first time they are keyed).
    if (g_retarget && g_aiSlot) {
        const RdramMemory mem(rdram);
        for (auto& [h, k] : g_keyed)
            if (!k.swapped && world::isLiveHandle(mem, g_globals, h, k.ref - 1))
                k.swapped = swapHandlerTo(rdram, h, g_aiSlot);
    }
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
        applyPendingGone(guest, rdram);
        watchObjects(rdram);
        pollChests(guest, rdram);
        pollDoors(rdram);
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
    uint32_t key = 0; // DRAKAN_COOP_TEST_TO_KEY (hex): only the object with this key (0: the nearest)
    float dist = 0;
    std::string file;
    uint32_t flipMs = 0; // DRAKAN_COOP_TEST_TO_FLIP: stand on the other side of the object every <flipMs> (3 times)
    int flips = 0;
    uint64_t lastMs = 0;
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
    if (g_to.done && g_to.flipMs && g_to.flips < 3 && nowMs() - g_to.lastMs > g_to.flipMs) {
        g_to.done = false;
        g_to.dist = -g_to.dist;
        ++g_to.flips;
    }
    if (g_to.want && !g_to.done && g_keyedThisLevel && fileTick % 20 == 5) {
        if (std::FILE* f = std::fopen(g_to.file.c_str(), "rb")) {
            std::fclose(f);
            const uint32_t me = controlled(rdram);
            const RdramMemory mem(rdram);
            uint32_t bestH = 0;
            float best = 1e30f;
            for (auto& [h, o] : g_obj)
                if (ramPtr(me) && o.classId == g_to.cls && !o.gone && (!g_to.key || o.key == g_to.key) &&
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
                if (const uint32_t comp = g_to.cls == kChestClass ? chestComp(rdram, bestH) : 0) {
                    // A chest opens for a player on its open side (the component's vector, +0x1c): stand along it.
                    const float vx = rdF(rdram, comp + 0x1c), vz = rdF(rdram, comp + 0x24);
                    const float vl = std::hypot(vx, vz);
                    if (vl > 1e-3f) {
                        fx = vx / vl;
                        fz = vz / vl;
                    }
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
                g_to.lastMs = nowMs();
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
    // Co-op rides on the multiplayer session (drakan_netplay): the hooks go in with the multiplayer module and stay
    // idle until the session is a Coop match that is playing (syncLink). DRAKAN_COOP=0 leaves them out.
    const char* off = std::getenv("DRAKAN_COOP");
    if (!drakan_netplay::wanted() || (off && off[0] == '0'))
        return;
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
    if (const char* s = std::getenv("DRAKAN_COOP_TEST_TO_KEY"))
        g_to.key = static_cast<uint32_t>(std::strtoul(s, nullptr, 16));
    if (const char* s = std::getenv("DRAKAN_COOP_TEST_TO_FLIP"))
        g_to.flipMs = static_cast<uint32_t>(std::strtoul(s, nullptr, 10));
    if (const char* s = std::getenv("DRAKAN_COOP_TEST_HURT_SCALE"))
        g_hurtScale = std::strtof(s, nullptr);
    if (const char* s = std::getenv("DRAKAN_COOP_TEST_CLAIM"))
        g_claimEveryMs = static_cast<uint32_t>(std::strtoul(s, nullptr, 10));
    g_rt = &rt;
    for (uint32_t a = 0x100100u; a < 0x100600u && (!g_handlerSlot || !g_aiSlot || !g_deadSlot); a += 4) {
        if (rt.hasFunction(a))
            continue;
        if (!g_handlerSlot) {
            if (rt.registerFunction(a, &proxyHandler))
                g_handlerSlot = a;
        } else if (!g_aiSlot) {
            if (rt.registerFunction(a, &aiHandler))
                g_aiSlot = a;
        } else if (rt.registerFunction(a, &deadHandler)) {
            g_deadSlot = a;
        }
    }
    if (!g_handlerSlot || !g_aiSlot || !g_deadSlot) {
        coopLog("disabled: no free function slot");
        return;
    }
    if (const uint32_t sendAddr = drakan_region::guestAddress(0x171150u)) {
        if ((g_origSend = rt.lookupFunction(sendAddr)))
            rt.replaceFunction(sendAddr, &hookSend);
    }
    if (!g_origSend)
        coopLog("warning: message hook not installed (combat and object sync off)");
    if (const uint32_t spewAddr = drakan_region::guestAddress(kChestSpewFn)) {
        if ((g_origSpew = rt.lookupFunction(spewAddr)))
            rt.replaceFunction(spewAddr, &hookSpew);
    }
    if (!g_origSpew)
        coopLog("warning: chest spew hook not installed (chest loot is not shared)");
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
    g_installed.store(true);
    g_thread = std::thread(netThread);
    std::atexit(stopThread);
    coopLog("installed (idle until a Coop match plays): %s, swallow mask %x, retarget %d, replay types %zu",
            g_follow ? "follow" : "mark", g_swallow, g_retarget, g_replayTypes.size());
}

// EE thread: this player became the owner of the creature world while the match runs (the old owner left or timed
// out, see MatchServer host migration). The level is already loaded and keyed as a guest: the proxies become the
// real creatures. Their own behaviour handlers come back (hostTick then wraps them with the AI handler like any
// owner's creatures), HP stays what the last stream wrote, and the first hostTick afterwards streams them. Creatures
// that were already dying keep their own handler and are announced once more by hostTick's death detection.
void handoverToAuthority(uint8_t* rdram) {
    uint32_t restored = 0, dying = 0;
    if (g_keyedThisLevel)
        for (auto& [h, k] : g_keyed) {
            if (k.dying) {
                ++dying; // its own handler is back already; k.swapped stays set so hostTick leaves it alone
                continue;
            }
            if (k.swapped && restoreHandler(rdram, h))
                ++restored;
            k.swapped = false; // hostTick installs the AI wrapper
            k.matched = false;
            k.deadSent = false;
            k.absentCount = 0;
            k.lastClaimMs = 0;
        }
    if (g_life.dead) { // the local player was down waiting for the old owner's respawn: get up now
        endDeath(rdram);
        setPlayerHp(rdram, playerHpMax(rdram));
        g_life = Life();
    }
    g_pendingHurt = 0;
    g_claims.clear(); // claims queued for an owner that is gone
    g_syncPending = g_syncSent = false;
    g_lastHostSend = 0;
    {
        std::lock_guard<std::mutex> l(g_s.m);
        g_s.inbox.clear(); // the old owner's last snapshots are not a stream to follow any more
        g_s.inLevel = 0;
    }
    g_host = true;
    coopLog("handover: this player owns the creature world now (%u creatures made real, %u already dying, level %s)",
            restored, dying, g_keyedThisLevel ? "keyed" : "not keyed yet");
}

// EE thread: follow the session. A Coop match that is playing switches the module on (as the owner of the creature
// world or as a guest); anything else switches it off and drops the state of that match.
void syncLink(uint8_t* rdram) {
    static uint64_t session = ~0ull;
    const drakan_netplay::CoopLink l = drakan_netplay::coopLink();
    const bool want = l.active;
    const bool on = g_enabled.load(std::memory_order_relaxed);
    if (want && (!on || session != l.session)) {
        g_host = l.host;
        session = l.session;
        g_enabled.store(true);
        ++g_loadEpoch; // key the level afresh for this match
        coopLog("co-op on: %s (player %d, creature world owned by player %d, %d players)", g_host ? "host" : "client",
                l.self, l.authority, l.players);
    } else if (want && on && l.host && !g_host && rdram) {
        handoverToAuthority(rdram); // the server named this player the new owner
    } else if (!want && on) {
        g_enabled.store(false);
        forgetLevel();
        session = ~0ull;
        coopLog("co-op off");
    }
}

// Feeds the co-op party panel (drakan_netplay::setCoopParty): the host knows which guests are down and when the
// respawn comes; the wire carries no partner health (hpKnown stays false). "Level complete" = a level change (exit)
// is under way in the match.
void publishParty(const uint8_t* rdram) {
    std::vector<drakan_netplay::CoopPartnerInfo> v;
    const uint64_t now = nowMs();
    // Own health to the server (the wire message Vitals); garbage reads (no level) are not sent.
    {
        const int hp = g_globals.hpKey && rdram ? playerHp(rdram) : -1;
        const int mx = g_globals.hpKey && rdram ? playerHpMax(rdram) : 0;
        if (mx > 0 && mx <= 65535 && hp >= 0 && hp <= mx)
            drakan_netplay::setLocalVitals(hp, mx, 0, 0); // the dragon's own health is not located yet
        else
            drakan_netplay::setLocalVitals(-1, 0, 0, 0);
    }
    for (const drakan_netplay::PartnerVitals& pv : drakan_netplay::partnerVitals()) {
        drakan_netplay::CoopPartnerInfo i;
        i.player = pv.player;
        if (pv.hpMax > 0) {
            i.hpKnown = true;
            i.hp = pv.hp;
            i.hpMax = pv.hpMax;
            i.hpFrac = static_cast<float>(pv.hp) / static_cast<float>(pv.hpMax);
            i.down = pv.hp <= 0;
        }
        if (pv.dragonMax > 0) {
            i.dragonKnown = true;
            i.dragonFrac = static_cast<float>(pv.dragonHp) / static_cast<float>(pv.dragonMax);
        }
        v.push_back(i);
    }
    if (g_host)
        for (const auto& [pid, slot] : g_players) {
            drakan_netplay::CoopPartnerInfo* found = nullptr;
            for (auto& e : v)
                if (e.player == pid)
                    found = &e;
            if (!found) {
                v.emplace_back();
                found = &v.back();
                found->player = pid;
            }
            drakan_netplay::CoopPartnerInfo& i = *found;
            i.down = i.down || slot.dead;
            if (slot.dead)
                i.respawnInSec =
                    now - slot.deadMs >= kRespawnDelayMs ? 0.0 : (kRespawnDelayMs - (now - slot.deadMs)) / 1000.0;
        }
    drakan_netplay::setCoopParty(v, drakan_lifecycle::transitionActive());
}

void beforePadMgr(drakan_input::IGuest& guest, uint8_t* rdram) {
    if (!g_installed.load(std::memory_order_relaxed) || !rdram)
        return;
    syncLink(rdram);
    if (!g_enabled.load(std::memory_order_relaxed))
        return;
    publishParty(rdram);
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
