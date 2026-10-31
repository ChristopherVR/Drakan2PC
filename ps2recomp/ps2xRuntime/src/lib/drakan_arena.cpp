// Drakan: multiplayer arena world mode (drakan_arena.h). PAL addresses are canonical, translated through the disc
// profile. Design, the list of removed classes and the evidence: docs/reference/netcode.md "Multiplayer maps",
// docs/journal/2026-10-31/mp-arenas.md.
#include "drakan_arena.h"
#include "drakan_config.h"
#include "drakan_ghost.h"
#include "drakan_lifecycle.h"
#include "drakan_region.h"
#include "input/GuestInterface.h"
#include "net/Protocol.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "gsr/gsr.h"
#include "world/Json.h"
#include "world/Reader.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace drakan_arena {
namespace {

using drakan::net::ArenaDef;
using drakan::net::ArenaPickup;
using drakan_region::guestAddress;

constexpr uint32_t kContextPtr = 0x5565d4u, kFactory = 0x181ad8u, kKillActor = 0x1413c0u;
// Inventory (docs/reference/world-model.md "Inventory", combat-controls.md): the controller is player+0x584 with 24
// cells of 16 bytes at +0x30 {equipped, hot, handle, icon}; kStow(player, item) hides the item model and files it in a
// free cell (what putting a weapon away does); kEquipPost(ctl, cell, slot, flags) posts the engine message 0x1045 the
// Use button sends (slot 0 right hand / melee, 1 bow hand; the handler draws the weapon). The right hand's item is
// player+0xa48.
constexpr uint32_t kStow = 0x320f70u, kEquipPost = 0x341058u;
constexpr uint32_t kCtlOffset = 0x584u, kCtlCells = 0x30u, kCellCount = 24u, kHandSlots = 0xa48u;
constexpr uint32_t kClassMelee = 0x1000eu, kClassBow = 0x1004fu;
// The tail of the speech controller's idle text buffer: pvp uses 0x5b8480 (4 bytes), coop 0x5b84c0.. (0x34 bytes); the
// 60 bytes between hold the spawn transform (12 floats) of one pickup / loadout item at a time.

// Classes of the single player story that have no place in an arena (docs/reference/netcode.md "Multiplayer maps").
struct StoryClass {
    uint32_t id;
    const char* what;
};
constexpr StoryClass kStoryClasses[] = {
    {0x10033u, "Stomp Player (cut scenes)"},
    {0x100b4u, "Conversation Handler"},
    {0x10016u, "Movie Player"},
    {0x10037u, "Level Transition (exits, level change saves)"},
    {0x10021u, "quest items (runes, amulets, journals, keys)"},
    {0x1006du, "treasure (gold sacks, goblets)"},
    {0x100c5u, "chests"},
};
constexpr unsigned kSweepTicks =
    420; // loose items that are not pickups are removed during this long after the first pass
constexpr unsigned kSettleTicks = 12;  // boundaries between "the player exists" and the first pass
constexpr float kKeepRadius = 2500.0f; // loose items this close to the player's start are her own gear
constexpr float kAdoptRadius = 600.0f; // a pickup adopts the loose item this close (xz) to its position
constexpr float kHomeRadius = 450.0f;  // an item farther than this from home is no longer at its pickup
constexpr unsigned kOutOfBoundsTicks = 240;
constexpr float kFallDepth = 6000.0f; // below the lowest spawn point by this much = fallen out of the map // outside
                                      // the map for this long = back to a spawn point

bool g_log = true;
void arenaLog(const char* fmt, ...) {
    if (!g_log)
        return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "[arena] %s\n", buf);
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

uint32_t rd32(const uint8_t* ram, uint32_t a) {
    a &= 0x1ffffffu;
    uint32_t v = 0;
    if (a + 4 <= PS2_RAM_SIZE)
        std::memcpy(&v, ram + a, 4);
    return v;
}
float rdf(const uint8_t* ram, uint32_t a) {
    const uint32_t v = rd32(ram, a);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
bool ramPtr(uint32_t p) {
    return p >= 0x100000u && p < 0x2000000u;
}

// ---------------------------------------------------------------------------------------------------- catalog

std::mutex g_catM;
drakan::net::ArenaCatalog g_catalog;
std::vector<std::string> g_installed; // base levels that exist (empty = unknown, everything counts as installed)
bool g_haveInstalled = false;

std::string exeDir() {
    return drakan_cfg::exeDir(); // the play folder (dist/DrakanPC): ini files, mods, dev dumps live there
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::stringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

void addFromText(const std::string& text, const std::string& source) {
    std::vector<std::string> errors;
    size_t n;
    {
        std::lock_guard<std::mutex> l(g_catM);
        n = g_catalog.add(text, source, &errors);
    }
    for (const std::string& e : errors)
        arenaLog("maps: %s", e.c_str());
    arenaLog("maps: %zu from %s", n, source.c_str());
}

// The default list (compiled in), the player's mp_maps.ini next to the exe and DRAKAN_MP_MAPS, once, before anything
// else is added or asked (the mod packs' lists are added afterwards, so they win by id).
void loadDefaults() {
    static std::once_flag once;
    std::call_once(once, [] {
        addFromText(drakan::net::builtinArenaText(), "built-in");
        std::string text;
        const std::string own = (std::filesystem::path(exeDir()) / "mp_maps.ini").string();
        if (readFile(own, text))
            addFromText(text, own);
        if (const char* f = std::getenv("DRAKAN_MP_MAPS"); f && *f && readFile(f, text))
            addFromText(text, f);
    });
}

// ---------------------------------------------------------------------------------------------------- session

std::mutex g_sessM;
std::atomic<bool> g_active{false};
ArenaDef g_def; // valid while g_hasDef
bool g_hasDef = false;
std::atomic<uint8_t> g_local{0};
std::atomic<uint32_t> g_sessionSerial{0}; // changes with every setSession(true, ...)

struct PickupEvent {
    uint16_t index;
    bool taken;
    uint8_t player;
};
std::deque<PickupEvent> g_pickupEvents; // g_sessM
std::deque<uint16_t> g_claims;          // g_sessM

// ---------------------------------------------------------------------------------------------------- EE thread

world::Globals g_globals;
world::ReaderState g_rs;
world::Snapshot g_snap;
bool g_worldOk = false;

enum class PState { Unbound, Present, Gone, Spawning };
struct Pick {
    ArenaPickup def;
    PState state = PState::Unbound;
    uint32_t handle = 0, ref = 0, resIndex = 0;
    bool serverTaken = false; // the server's view (taken for its respawn time)
    bool queued = false;      // a spawn of it waits in g_spawnQueue
    unsigned spawnTick = 0;   // tick the spawn was posted
    int spawnTries = 0;
    uint32_t spawnHandle = 0; // the handle the factory returned for the spawn in flight (0 = none / refused)
};
std::vector<Pick> g_picks;

struct SpawnReq {
    uint32_t resIndex = 0;
    float pos[3] = {0, 0, 0};
    int pickup = -1; // pickup index, or -1 for a loadout item
};
std::deque<SpawnReq> g_spawnQueue;
bool g_spawnInFlight = false;
unsigned g_spawnPostedTick = 0;
int g_inflightPickup = -1;                   // pickup index (or -1: a loadout item) of the spawn in flight
uint32_t g_inflightSerial = 0;               // g_itemResultSerial when it was posted
std::deque<drakan_input::GuestCall> g_calls; // follow-up guest calls (stow / equip), one post at a time
struct PendingStow {
    uint32_t handle;
    unsigned since;
};
std::deque<PendingStow> g_pendingStow; // loadout items the factory made, stowed once they are live actors
uint32_t controlledActor(const uint8_t* rdram);
bool snapshot(const uint8_t* rdram);

uint32_t g_seenSerial = 0;    // session serial the EE state belongs to
uint32_t g_loadGen = 0;       // bumps at every level load (BeforeLevelLoad)
uint32_t g_filteredGen = ~0u; // load generation the first pass ran for
unsigned g_ticks = 0;         // boundaries since the player was ready in this load
unsigned g_passTick = 0;      // tick of the first pass
float g_startPos[3] = {0, 0, 0};
bool g_haveStart = false;
unsigned g_outsideTicks = 0;
bool g_loadoutArmed = false, g_loadoutDone = false;
unsigned g_removed = 0;
unsigned g_tourStop = 0, g_tourTick = 0;
int g_tourEvery = 0;
bool g_vetoInstalled = false;
std::mutex g_levelM;
std::string g_loadedLevel;         // g_levelM: the level the last load opened (the lifecycle's mapped name)
std::atomic<bool> g_defFits{true}; // the session's map belongs to the level that is loaded (else it is a plain level)

uint32_t engineCtx(const uint8_t* rdram) {
    return rd32(rdram, g_globals.contextPtr);
}
uint32_t controlledActor(const uint8_t* rdram) {
    const uint32_t ctx = engineCtx(rdram);
    return ramPtr(ctx) ? rd32(rdram, ctx + 0x4b0) : 0;
}

void killActor(drakan_input::IGuest& guest, uint8_t* rdram, uint32_t actor) {
    const uint32_t eng = engineCtx(rdram);
    if (ramPtr(eng) && ramPtr(actor))
        guest.call(drakan_input::GuestCall(guestAddress(kKillActor)).arg(eng).arg(actor));
}

// The data object of an actor handle (handle +0xfc -> inner +0x10): Rynn's carries the inventory controller (+0x584)
// and the equipment slots (+0xa48).
uint32_t dataOf(const uint8_t* rdram, uint32_t h) {
    const uint32_t p = ramPtr(h) ? rd32(rdram, h + 0xfc) : 0;
    return ramPtr(p) ? rd32(rdram, p + 0x10) : 0;
}

bool isStoryClass(uint32_t cls) {
    for (const StoryClass& s : kStoryClasses)
        if (s.id == cls)
            return true;
    return false;
}

bool isItemEntity(const world::EntitySnapshot& e) {
    if (e.type != world::type::kWeapon && e.type != world::type::kUsable)
        return false;
    // The inventory's own copies are parked near the origin of the world (the levels lie at 1e5..1e6).
    return !(std::fabs(e.position.x) < 20000.0f && std::fabs(e.position.z) < 20000.0f);
}

float dist2xz(const world::Vec3& a, const float b[3]) {
    const float dx = a.x - b[0], dz = a.z - b[2];
    return std::sqrt(dx * dx + dz * dz);
}

// The resource index of resource id `resId` (record +0), or 0xffffffff.
uint32_t findResourceIndex(const uint8_t* rdram, uint32_t resId) {
    const uint32_t ctx = engineCtx(rdram);
    const uint32_t res = ramPtr(ctx) ? rd32(rdram, ctx + 0x320) : 0;
    if (!ramPtr(res))
        return 0xffffffffu;
    const uint32_t table = rd32(rdram, res + 8), count = rd32(rdram, res + 0x10);
    if (!ramPtr(table) || count > 0x20000u)
        return 0xffffffffu;
    for (uint32_t i = 0; i <= count; ++i)
        if (rd32(rdram, table + i * 0x24u) == resId)
            return i;
    return 0xffffffffu;
}

// The factory takes six arguments and a posted guest call carries four, so the post goes to a host trampoline (the
// ghost's way): stage 1 puts the transform on the dispatcher thread's stack, loads $a0..$a5 and calls the factory
// returning to stage 2, which restores the stack and returns to where the post began.
struct ItemFlight {
    float xform[12] = {};
    uint32_t idx = 0, savedSp = 0, savedRa = 0;
} g_item;
uint32_t g_itemSlot1 = 0, g_itemSlot2 = 0;
constexpr uint32_t kItemFrame = 0x60;
// The factory returns the new actor's handle ($v0, 0 = refused); stage 2 publishes it with a serial for the EE thread.
std::atomic<uint32_t> g_itemResult{0}, g_itemResultSerial{0};

void itemStage1(uint8_t* rdram, R5900Context* c, PS2Runtime*) {
    g_item.savedSp = getRegU32(c, 29);
    g_item.savedRa = getRegU32(c, 31);
    const uint32_t sp = g_item.savedSp - kItemFrame, xf = sp + 0x10;
    std::memcpy(rdram + (xf & 0x1ffffffu), g_item.xform, sizeof g_item.xform);
    SET_GPR_U32(c, 29, sp);
    SET_GPR_U32(c, 4, rd32(rdram, g_globals.contextPtr));
    SET_GPR_U32(c, 5, 0u);
    SET_GPR_U32(c, 6, xf);
    SET_GPR_U32(c, 7, 0u);
    SET_GPR_U32(c, 8, 0u);
    SET_GPR_U32(c, 9, g_item.idx);
    SET_GPR_U32(c, 31, g_itemSlot2);
    c->pc = guestAddress(kFactory);
}

void itemStage2(uint8_t*, R5900Context* c, PS2Runtime*) {
    g_itemResult.store(getRegU32(c, 2));
    g_itemResultSerial.fetch_add(1);
    SET_GPR_U32(c, 29, g_item.savedSp);
    SET_GPR_U32(c, 31, g_item.savedRa);
    c->pc = g_item.savedRa;
}

void queueSpawn(uint32_t resIndex, const float pos[3], int pickup) {
    SpawnReq r;
    r.resIndex = resIndex;
    std::memcpy(r.pos, pos, sizeof r.pos);
    r.pickup = pickup;
    g_spawnQueue.push_back(r);
}

// One spawn at a time through the engine's factory, posted to a guest dispatcher thread like the ghost's: the factory
// allocates (guest heap semaphore) and must not run nested.
void pumpSpawns(drakan_input::IGuest& guest, uint8_t* rdram) {
    if (g_spawnInFlight && g_itemResultSerial.load() != g_inflightSerial) {
        // The factory finished: it returned the actor's handle (0 = it refused). A loadout item goes straight into the
        // inventory (stowed: hidden and filed in a free cell); a pickup remembers the handle for binding.
        const uint32_t handle = g_itemResult.load();
        g_spawnInFlight = false;
        const uint32_t player = dataOf(rdram, controlledActor(rdram));
        if (g_inflightPickup < 0) {
            arenaLog("loadout: the factory returned actor %x (vtable %x flags %x type %u ref %x)", handle,
                     ramPtr(handle) ? rd32(rdram, handle) : 0u, ramPtr(handle) ? rd32(rdram, handle + 4) : 0u,
                     ramPtr(handle) ? rd32(rdram, handle + 0xc) : 0u, ramPtr(handle) ? rd32(rdram, handle + 0x10) : 0u);
            if (handle && ramPtr(player))
                g_pendingStow.push_back({handle, g_ticks});
        } else if (static_cast<size_t>(g_inflightPickup) < g_picks.size()) {
            g_picks[static_cast<size_t>(g_inflightPickup)].spawnHandle = handle;
            arenaLog("pickup %d: the factory returned actor %x (vtable %x type %u ref %x)", g_inflightPickup, handle,
                     ramPtr(handle) ? rd32(rdram, handle) : 0u, ramPtr(handle) ? rd32(rdram, handle + 0xc) : 0u,
                     ramPtr(handle) ? rd32(rdram, handle + 0x10) : 0u);
            if (!handle) {
                Pick& p = g_picks[static_cast<size_t>(g_inflightPickup)];
                arenaLog("pickup %d: the factory refused the spawn (try %d)", g_inflightPickup, p.spawnTries + 1);
                if (++p.spawnTries >= 8)
                    p.state = PState::Unbound;
                else
                    p.state = PState::Gone; // asked again at the next scan
            }
        }
    }
    if (!g_pendingStow.empty() && g_ticks >= g_pendingStow.front().since + 10) {
        const PendingStow ps = g_pendingStow.front();
        const uint32_t player = dataOf(rdram, controlledActor(rdram));
        bool live = false;
        if (snapshot(rdram))
            for (const auto& e : g_snap.entities)
                if (e.handle == ps.handle) {
                    live = true;
                    arenaLog("loadout: actor %x is live (type %u class %x resource index %u) at %.0f %.0f %.0f",
                             e.handle, e.type, e.classId, e.resourceIndex, static_cast<double>(e.position.x),
                             static_cast<double>(e.position.y), static_cast<double>(e.position.z));
                }
        if (live && ramPtr(player)) {
            arenaLog("loadout: inventory UI listener player+0xa28 = %x, stow state %x", rd32(rdram, player + 0xa28),
                     rd32(rdram, player + 0x584 + 0x20));
            g_pendingStow.pop_front();
            g_calls.push_back(drakan_input::GuestCall(guestAddress(kStow)).arg(player).arg(ps.handle));
        } else if (g_ticks > ps.since + 240) {
            arenaLog("loadout: actor %x never became a live actor, dropped", ps.handle);
            g_pendingStow.pop_front();
        }
    }
    if (g_spawnInFlight && !guest.postBusy() && g_ticks > g_spawnPostedTick + 4)
        g_spawnInFlight = false;
    if (g_spawnInFlight || guest.postBusy() || drakan_lifecycle::transitionActive())
        return;
    if (!g_calls.empty()) {
        guest.post(g_calls.front());
        g_calls.pop_front();
        return;
    }
    if (g_spawnQueue.empty())
        return;
    const SpawnReq r = g_spawnQueue.front();
    g_spawnQueue.pop_front();
    if (r.pickup >= 0 && static_cast<size_t>(r.pickup) < g_picks.size()) {
        Pick& p = g_picks[static_cast<size_t>(r.pickup)];
        p.queued = false;
        if (p.serverTaken || p.state != PState::Gone) // taken again (or bound) while it waited
            return;
    }
    const uint32_t ctx = engineCtx(rdram);
    if (!ramPtr(ctx))
        return;
    if (!g_itemSlot1 || !g_itemSlot2)
        return;
    const float xform[12] = {r.pos[0], r.pos[1], r.pos[2], 1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::memcpy(g_item.xform, xform, sizeof xform);
    g_item.idx = r.resIndex;
    g_inflightPickup = r.pickup;
    g_inflightSerial = g_itemResultSerial.load();
    guest.post(drakan_input::GuestCall(g_itemSlot1));
    g_spawnInFlight = true;
    g_spawnPostedTick = g_ticks;
    if (r.pickup >= 0 && static_cast<size_t>(r.pickup) < g_picks.size()) {
        g_picks[static_cast<size_t>(r.pickup)].state = PState::Spawning;
        g_picks[static_cast<size_t>(r.pickup)].spawnTick = g_ticks;
        g_picks[static_cast<size_t>(r.pickup)].spawnHandle = 0;
    }
    arenaLog("spawn: resource index %u at %.0f %.0f %.0f (%s %d)", r.resIndex, static_cast<double>(r.pos[0]),
             static_cast<double>(r.pos[1]), static_cast<double>(r.pos[2]), r.pickup >= 0 ? "pickup" : "loadout",
             r.pickup);
}

// Test aid DRAKAN_ARENA_DUMP=1: world dumps (docs/reference/netcode.md "Multiplayer maps") of the level as the engine
// built it ("before", the first pass) and after the campaign was removed ("after"), next to the F1 > World panel's
// dumps.
bool g_dumpWanted = false;
void dumpWorld(const char* tag) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::path(exeDir()) / "dev" / "world";
    fs::create_directories(dir, ec);
    const fs::path path = dir / (std::string("arena_") + tag + ".json");
    std::ofstream f(path, std::ios::binary);
    if (!f)
        return;
    f << world::toJson(g_snap);
    arenaLog("world dump '%s': %zu entities -> %s", tag, g_snap.entities.size(), path.string().c_str());
}

bool snapshot(const uint8_t* rdram) {
    g_worldOk = world::readSnapshot(RdramMemory(rdram), g_globals, g_rs, g_snap) == world::ReadStatus::Ok;
    return g_worldOk;
}

bool liveAt(const uint8_t* rdram, const Pick& p) {
    return p.handle && p.ref && world::isLiveHandle(RdramMemory(rdram), g_globals, p.handle, p.ref - 1);
}

// Binds the map's pickups to live actors (the level's own loose items) or queues the spawn of the ones it defines.
void bindPickups(drakan_input::IGuest&, const uint8_t* rdram) {
    for (size_t i = 0; i < g_picks.size(); ++i) {
        Pick& p = g_picks[i];
        p.state = PState::Unbound;
        p.handle = p.ref = 0;
        if (p.def.resId) {
            p.resIndex = findResourceIndex(rdram, p.def.resId);
            if (p.resIndex == 0xffffffffu) {
                arenaLog("pickup %zu (%s): resource %x is not part of this level, disabled", i, p.def.label.c_str(),
                         p.def.resId);
                continue;
            }
            p.state = PState::Gone; // spawned once the server says it is available (below)
            continue;
        }
        const world::EntitySnapshot* best = nullptr;
        float bestD = kAdoptRadius;
        for (const auto& e : g_snap.entities) {
            if (!isItemEntity(e))
                continue;
            const float d = dist2xz(e.position, p.def.pos);
            if (d < bestD && std::fabs(e.position.y - p.def.pos[1]) < 600.0f) {
                bestD = d;
                best = &e;
            }
        }
        if (!best) {
            arenaLog("pickup %zu (%s): no loose item within %.0f units of %.0f %.0f %.0f, disabled", i,
                     p.def.label.c_str(), static_cast<double>(kAdoptRadius), static_cast<double>(p.def.pos[0]),
                     static_cast<double>(p.def.pos[1]), static_cast<double>(p.def.pos[2]));
            continue;
        }
        p.handle = best->handle;
        p.ref = best->ref;
        p.resIndex = best->resourceIndex;
        p.def.pos[0] = best->position.x; // the item's own spot is its home
        p.def.pos[1] = best->position.y;
        p.def.pos[2] = best->position.z;
        p.state = PState::Present;
        arenaLog("pickup %zu (%s): bound to actor %x (resource index %u, class %x) at %.0f %.0f %.0f", i,
                 p.def.label.c_str(), best->handle, best->resourceIndex, best->classId,
                 static_cast<double>(best->position.x), static_cast<double>(best->position.y),
                 static_cast<double>(best->position.z));
    }
}

bool nearAnyPickup(const world::Vec3& pos) {
    for (const Pick& p : g_picks)
        if (p.state != PState::Unbound && dist2xz(pos, p.def.pos) < kHomeRadius)
            return true;
    return false;
}

// The campaign's population and story: see kStoryClasses. `sweepItems` also removes loose items that are not pickups.
bool g_shots =
    false; // DRAKAN_ARENA_SHOTS=1: a window screenshot at every pickup event (DRAKAN_SHOT_DIR/arena_<name>.png)
unsigned g_shotSerial = 0;
void arenaShot(const std::string& name) {
    const char* dir = std::getenv("DRAKAN_SHOT_DIR");
    if (g_shots && dir && *dir)
        gsr::requestWindowShot(std::string(dir) + "/arena_" + std::to_string(g_shotSerial++) + "_" + name + ".png");
}

bool g_noFilter = false; // test aid DRAKAN_ARENA_NOFILTER=1: place players and run pickups but leave the campaign alone

unsigned removeCampaign(drakan_input::IGuest& guest, uint8_t* rdram, bool sweepItems) {
    if (g_noFilter)
        return 0;
    unsigned n = 0, creatures = 0, story = 0, items = 0;
    for (const auto& e : g_snap.entities) {
        if (e.handle == g_snap.playerHandle || e.handle == g_snap.cameraHandle || e.type == world::type::kPlayer)
            continue;
        const bool creature = e.hpKind == world::HpKind::Creature && e.hpMax > 0;
        const bool storyObj = isStoryClass(e.classId);
        bool item = false;
        if (sweepItems && isItemEntity(e) && !storyObj) {
            const float d = dist2xz(e.position, g_startPos);
            item = !(g_haveStart && d < kKeepRadius) && !nearAnyPickup(e.position);
        }
        if (!creature && !storyObj && !item)
            continue;
        killActor(guest, rdram, e.handle);
        ++n;
        creatures += creature;
        story += storyObj && !creature;
        items += item && !creature && !storyObj;
    }
    if (n)
        arenaLog("removed %u campaign actors (%u creatures / NPCs, %u story objects, %u loose items)", n, creatures,
                 story, items);
    return n;
}

// The map's spawn heights are the level's own object heights (a creature's feet, a waypoint): a little above the ground
// at best, inside a slope at worst. Players appear 250 units higher and drop onto whatever is below.
constexpr float kSpawnLift = 250.0f;

void teleportOnce(drakan_input::IGuest& guest, uint32_t player, const float pos[3]) {
    for (int k = 0; k < 3; ++k)
        guest.writeF(player + 0x14 + 4u * static_cast<uint32_t>(k), pos[k] + (k == 1 ? kSpawnLift : 0.0f));
    drakan_ghost::refileActor(guest, player);
}

// The engine puts a player back where she came from when no floor is resident at the target (the terrain streams around
// her): a far teleport may fail the first time. The placement is watched and repeated (every 40 boundaries, 6 times); a
// spawn point that never takes is given up and she plays on from where she landed.
struct GotoStep {
    int pickup;
    unsigned at;
    bool done = false;
};
std::vector<GotoStep> g_goto; // test aid DRAKAN_ARENA_TEST_GOTO, see gotoTick
unsigned g_sweepFrom = 0;     // test aid: boundary the walk through the pickup starts (0 = none)
int g_useTaps = 0;            // test aid: Use presses still to send
unsigned g_useTapAt = 0;      // boundary of the next one
struct Placement {
    bool active = false;
    float target[3] = {0, 0, 0};
    unsigned tries = 0, nextTick = 0;
    int index = -1;
} g_place;
constexpr float kPlaceTolerance = 3500.0f;
constexpr unsigned kPlaceTries = 6, kPlaceRetryTicks = 40;

void teleport(drakan_input::IGuest& guest, uint32_t player, const float pos[3], int index = -1) {
    teleportOnce(guest, player, pos);
    g_place.active = true;
    std::memcpy(g_place.target, pos, sizeof g_place.target);
    g_place.tries = 1;
    g_place.nextTick = g_ticks + kPlaceRetryTicks;
    g_place.index = index;
}

void placementTick(drakan_input::IGuest& guest, const uint8_t* rdram, uint32_t player) {
    if (!g_place.active || g_ticks < g_place.nextTick)
        return;
    const float dx = rdf(rdram, player + 0x14) - g_place.target[0], dz = rdf(rdram, player + 0x1c) - g_place.target[2];
    const float d = std::sqrt(dx * dx + dz * dz);
    if (d <= kPlaceTolerance) {
        arenaLog("placement of spawn %d took after %u %s (%.0f units from the point)", g_place.index, g_place.tries,
                 g_place.tries == 1 ? "try" : "tries", static_cast<double>(d));
        g_place.active = false;
        if (g_place.index == -1 &&
            !g_goto.empty()) { // a test walk onto a pickup: sweep through it, press Use a few times
            g_useTaps = 4;
            g_useTapAt = g_ticks + 10;
            g_sweepFrom = g_ticks + 5;
        }
    } else if (g_place.tries >= kPlaceTries) {
        arenaLog("placement of spawn %d failed: the player is %.0f units from it after %u tries (no floor there?)",
                 g_place.index, static_cast<double>(d), g_place.tries);
        g_place.active = false;
    } else {
        ++g_place.tries;
        g_place.nextTick = g_ticks + kPlaceRetryTicks;
        teleportOnce(guest, player, g_place.target);
    }
}

// Where the player stood when the level came up (before any spawn placement): the loose items around it are her own
// gear.
void captureStart(const uint8_t* rdram, uint32_t player) {
    for (int k = 0; k < 3; ++k)
        g_startPos[k] = rdf(rdram, player + 0x14 + 4u * static_cast<uint32_t>(k));
    g_haveStart = true;
}

void armLoadout(const uint8_t* rdram, uint32_t player);

void firstPass(drakan_input::IGuest& guest, uint8_t* rdram, uint32_t player) {
    if (!snapshot(rdram))
        return;
    if (!g_haveStart)
        captureStart(rdram, player);
    if (g_dumpWanted)
        dumpWorld("before");
    if (g_dumpWanted)
        for (uint32_t a = guestAddress(0x4fca40u); a < guestAddress(0x4fd184u); a += 12) {
            const uint32_t id = rd32(rdram, a);
            if (id == 0x1000eu || id == 0x1000au || id == 0x1004fu || id == 0x10059u)
                arenaLog("class %x handler %x instance size %x", id, rd32(rdram, a + 4), rd32(rdram, a + 8));
        }
    arenaLog("level up: %zu actors, player at %.0f %.0f %.0f", g_snap.entities.size(),
             static_cast<double>(g_startPos[0]), static_cast<double>(g_startPos[1]),
             static_cast<double>(g_startPos[2]));
    {
        std::string level;
        {
            std::lock_guard<std::mutex> l(g_levelM);
            level = g_loadedLevel;
        }
        std::lock_guard<std::mutex> l(g_sessM);
        g_picks.clear();
        g_defFits.store(!g_hasDef || g_def.level == level);
        if (g_hasDef && !g_defFits.load())
            arenaLog("map '%s' is for '%s' but '%s' is loaded: playing it as a plain level", g_def.name.c_str(),
                     g_def.level.c_str(), level.c_str());
        if (g_hasDef && g_defFits.load())
            for (const ArenaPickup& p : g_def.pickups) {
                Pick pk;
                pk.def = p;
                g_picks.push_back(std::move(pk));
            }
    }
    bindPickups(guest, rdram);
    if (std::getenv("DRAKAN_ARENA_FORCE")) // test: no spawn event arms it
        armLoadout(rdram, player);
    g_removed += removeCampaign(guest, rdram, true);
    g_passTick = g_ticks;
    g_filteredGen = g_loadGen;
}

void applyPickupEvents(drakan_input::IGuest& guest, uint8_t* rdram) {
    std::deque<PickupEvent> evs;
    {
        std::lock_guard<std::mutex> l(g_sessM);
        evs.swap(g_pickupEvents);
    }
    for (const PickupEvent& e : evs) {
        const bool all = e.index == drakan::net::kAllPickups;
        for (size_t i = all ? 0 : e.index; i < g_picks.size() && (all || i == e.index); ++i) {
            Pick& p = g_picks[i];
            p.serverTaken = e.taken;
            if (e.taken && p.state == PState::Present) {
                if (e.player != g_local.load() || !g_local.load()) {
                    killActor(guest, rdram, p.handle); // somebody else took it: ours goes too
                    arenaLog("pickup %zu (%s) taken by player %u: removed here", i, p.def.label.c_str(), e.player);
                    arenaShot("pickup" + std::to_string(i) + "_removed_here");
                }
                p.state = PState::Gone;
            } else if (e.taken && p.state == PState::Spawning) {
                p.state = PState::Gone; // taken while its respawn was being made: the new copy goes at the next scan
            }
        }
    }
}

// Pickups: presence check, claims, respawns. Every third boundary.
void pickupTick(drakan_input::IGuest& guest, uint8_t* rdram) {
    if (g_picks.empty())
        return;
    const bool needScan =
        std::any_of(g_picks.begin(), g_picks.end(), [](const Pick& p) { return p.state == PState::Spawning; });
    if (needScan)
        snapshot(rdram);
    for (size_t i = 0; i < g_picks.size(); ++i) {
        Pick& p = g_picks[i];
        switch (p.state) {
            case PState::Unbound:
                break;
            case PState::Present: {
                bool here = liveAt(rdram, p);
                if (here) {
                    float pos[3];
                    for (int k = 0; k < 3; ++k)
                        pos[k] = rdf(rdram, p.handle + 0x14 + 4u * static_cast<uint32_t>(k));
                    const float dx = pos[0] - p.def.pos[0], dz = pos[2] - p.def.pos[2];
                    here = std::sqrt(dx * dx + dz * dz) < kHomeRadius && std::fabs(pos[1] - p.def.pos[1]) < 800.0f;
                }
                if (here)
                    break;
                // Gone from its spot without the server saying so: the local player took it.
                p.state = PState::Gone;
                if (!p.serverTaken) {
                    p.serverTaken = true;
                    std::lock_guard<std::mutex> l(g_sessM);
                    g_claims.push_back(static_cast<uint16_t>(i));
                    arenaLog("pickup %zu (%s) taken here: claim", i, p.def.label.c_str());
                    arenaShot("pickup" + std::to_string(i) + "_taken_here");
                }
                break;
            }
            case PState::Gone:
                if (!p.serverTaken && !p.queued && p.resIndex != 0xffffffffu && p.resIndex != 0 &&
                    g_ticks > g_passTick + 30) {
                    p.queued = true;
                    queueSpawn(p.resIndex, p.def.pos, static_cast<int>(i));
                }
                break;
            case PState::Spawning: {
                const world::EntitySnapshot* found = nullptr;
                if (p.spawnHandle)
                    for (const auto& e : g_snap.entities)
                        if (e.handle == p.spawnHandle) {
                            found = &e;
                            break;
                        }
                for (const auto& e : g_snap.entities) {
                    if (found)
                        break;
                    if (!isItemEntity(e) || e.resourceIndex != p.resIndex || dist2xz(e.position, p.def.pos) > 150.0f)
                        continue;
                    bool bound = false;
                    for (const Pick& o : g_picks)
                        bound |= &o != &p && o.handle == e.handle;
                    if (!bound) {
                        found = &e;
                        break;
                    }
                }
                if (found) {
                    p.handle = found->handle;
                    p.ref = found->ref;
                    p.state = PState::Present;
                    arenaLog("pickup %zu (%s) is back (actor %x, %s)", i, p.def.label.c_str(), found->handle,
                             p.spawnHandle == found->handle ? "by handle" : "by scan");
                    arenaShot("pickup" + std::to_string(i) + "_back");
                } else if (g_ticks > p.spawnTick + 180) {
                    for (const auto& e : g_snap.entities)
                        if (e.resourceIndex == p.resIndex)
                            arenaLog("pickup %zu: resource %u exists as actor %x type %u class %x at %.0f %.0f %.0f", i,
                                     p.resIndex, e.handle, e.type, e.classId, static_cast<double>(e.position.x),
                                     static_cast<double>(e.position.y), static_cast<double>(e.position.z));
                    if (++p.spawnTries >= 8) {
                        arenaLog("pickup %zu (%s): the respawn never showed up, giving up", i, p.def.label.c_str());
                        p.state = PState::Unbound;
                    } else {
                        p.state = PState::Gone;
                    }
                }
                break;
            }
        }
    }
}

void armLoadout(const uint8_t* rdram, uint32_t player) {
    std::lock_guard<std::mutex> l(g_sessM);
    if (!g_hasDef || !g_defFits.load() || g_def.loadout.empty() || g_loadoutDone)
        return;
    g_loadoutArmed = true;
    (void)rdram;
    (void)player;
}

// The starting loadout: the items of the map's `loadout` list are created through the engine's factory (the pickup
// spawn) and filed straight into the inventory with the engine's stow routine (docs/reference/netcode.md "Starting
// loadout"). Then armTick equips a weapon the way the Use button does, so every player starts armed on every map (a new
// game outside the first level has no weapon at all, the first level's has a dagger it does not hold).
std::vector<uint32_t> g_loadoutIdx;
int g_loadoutPhase = 0; // 0 not started, 1 waiting for the spawns, 2 done
unsigned g_loadoutStart = 0;

void loadoutTick(drakan_input::IGuest&, uint8_t* rdram, uint32_t player) {
    if (!g_loadoutArmed || g_loadoutPhase >= 2 || g_ticks < g_passTick + kSweepTicks + 30)
        return;
    if (g_loadoutPhase == 0) {
        std::vector<uint32_t> ids;
        {
            std::lock_guard<std::mutex> l(g_sessM);
            ids = g_def.loadout;
        }
        float pos[3];
        for (int k = 0; k < 3; ++k)
            pos[k] = rdf(rdram, player + 0x14 + 4u * static_cast<uint32_t>(k));
        g_loadoutPhase = 1;
        g_loadoutStart = g_ticks;
        g_loadoutIdx.clear();
        for (uint32_t id : ids) {
            const uint32_t idx = findResourceIndex(rdram, id);
            if (idx == 0xffffffffu) {
                arenaLog("loadout: resource %x is not part of this level, skipped", id);
                continue;
            }
            g_loadoutIdx.push_back(idx);
            queueSpawn(idx, pos, -1);
        }
        if (g_loadoutIdx.empty())
            g_loadoutPhase = 2;
        return;
    }
    if (g_spawnQueue.empty() && !g_spawnInFlight && g_calls.empty() && g_pendingStow.empty() &&
        g_ticks > g_loadoutStart + 20) {
        arenaLog("loadout: %zu items given", g_loadoutIdx.size());
        g_loadoutPhase = 2;
    } else if (g_ticks > g_loadoutStart + 900) {
        arenaLog("loadout: gave up waiting");
        g_loadoutPhase = 2;
    }
}

// Class id (0x1000e melee weapon, 0x1004f bow, ...) of the item actor `handle`, 0 when it is not an item.
uint32_t itemClassOf(const uint8_t* rdram, uint32_t handle) {
    if (!ramPtr(handle))
        return 0;
    const uint16_t ref = static_cast<uint16_t>(rd32(rdram, handle + 0x10) & 0xffffu);
    if (ref == 0xffffu)
        return 0;
    const uint32_t ctx = engineCtx(rdram);
    const uint32_t res = ramPtr(ctx) ? rd32(rdram, ctx + 0x320) : 0;
    if (!ramPtr(res))
        return 0;
    const uint32_t table = rd32(rdram, res + 8), count = rd32(rdram, res + 0x10);
    if (!ramPtr(table) || ref > count)
        return 0;
    return rd32(rdram, table + ref * 0x24u + 8);
}

// Auto-equip: once the loadout is in, a player whose right hand is empty equips the first melee weapon of the inventory
// (the loadout's first, else any; a bow when there is nothing else). Retried every two seconds: the engine refuses it
// while she is mid-animation or on a mount.
struct ArmState {
    bool done = false;
    unsigned next = 0;
    int tries = 0;
} g_arm;

void armTick(drakan_input::IGuest& guest, uint8_t* rdram, uint32_t actor) {
    const uint32_t player = dataOf(rdram, actor);
    if (!ramPtr(player))
        return;
    if (g_arm.done || g_ticks < g_passTick + kSweepTicks + 40 || g_ticks < g_arm.next)
        return;
    if (g_loadoutArmed && g_loadoutPhase < 2)
        return;
    if (guest.postBusy() || !g_calls.empty() || !g_pendingStow.empty() || g_spawnInFlight || !g_spawnQueue.empty())
        return;
    g_arm.next = g_ticks + 120;
    const uint32_t hand = rd32(rdram, player + kHandSlots);
    if (ramPtr(hand)) {
        arenaLog("armed: right hand holds actor %x (class %x)", hand, itemClassOf(rdram, hand));
        g_arm.done = true;
        return;
    }
    const uint32_t ctl = player + kCtlOffset;
    int cell = -1;
    uint32_t slot = 0;
    for (int pass = 0; pass < 2 && cell < 0; ++pass)
        for (uint32_t i = 0; i < kCellCount; ++i) {
            const uint32_t h = rd32(rdram, ctl + kCtlCells + i * 16u + 8u);
            if (h <= 0x53u || !ramPtr(h))
                continue;
            const uint32_t cls = itemClassOf(rdram, h);
            if (cls == (pass == 0 ? kClassMelee : kClassBow)) {
                cell = static_cast<int>(i);
                slot = pass;
                break;
            }
        }
    if (cell < 0) {
        arenaLog("armed: no weapon in the inventory (try %d)", g_arm.tries + 1);
        if (++g_arm.tries >= 10)
            g_arm.done = true;
        return;
    }
    if (++g_arm.tries > 8) {
        arenaLog("armed: gave up equipping cell %d", cell);
        g_arm.done = true;
        return;
    }
    arenaLog("armed: equipping cell %d (actor %x) into slot %u (try %d)", cell,
             rd32(rdram, ctl + kCtlCells + static_cast<uint32_t>(cell) * 16u + 8u), slot, g_arm.tries);
    guest.post(
        drakan_input::GuestCall(guestAddress(kEquipPost)).arg(ctl).arg(static_cast<uint32_t>(cell)).arg(slot).arg(0));
}

void boundsTick(drakan_input::IGuest& guest, uint8_t* rdram, uint32_t player) {
    ArenaDef def;
    {
        std::lock_guard<std::mutex> l(g_sessM);
        if (!g_hasDef || !g_defFits.load()) {
            g_outsideTicks = 0;
            return;
        }
        def = g_def;
    }
    const float x = rdf(rdram, player + 0x14), y = rdf(rdram, player + 0x18), z = rdf(rdram, player + 0x1c);
    // A fall past every spawn point (a map on a cliff top over a void) is out at once, leaving the rectangle after a
    // while.
    float lowest = 1e30f;
    for (const auto& s : def.spawns)
        lowest = std::min(lowest, s.pos[1]);
    const bool fell = y < lowest - kFallDepth;
    if (!fell && (!def.hasBounds || def.insideBounds(x, z))) {
        g_outsideTicks = 0;
        return;
    }
    if (!fell && ++g_outsideTicks < kOutOfBoundsTicks)
        return;
    g_outsideTicks = 0;
    std::vector<std::pair<float, float>> others;
    drakan_ghost::forEach([&](const drakan_ghost::Info& i) {
        if (i.handle)
            others.emplace_back(rdf(rdram, i.handle + 0x14), rdf(rdram, i.handle + 0x1c));
    });
    const int s = def.pickSpawn(others);
    if (s < 0)
        return;
    arenaLog("%s (%.0f %.0f %.0f): back to spawn %d", fell ? "fell out of the map" : "outside the map for too long",
             static_cast<double>(x), static_cast<double>(y), static_cast<double>(z), s);
    teleport(guest, player, def.spawns[static_cast<size_t>(s)].pos, s);
}

// Test aid DRAKAN_ARENA_TOUR=<boundaries per stop>: visits every spawn point in turn (screenshots come from the input
// script) and logs where the player stands a moment later, which shows spawn points that are inside geometry or over a
// drop.
void tourTick(drakan_input::IGuest& guest, uint8_t* rdram, uint32_t player) {
    if (g_tourEvery <= 0)
        return;
    ArenaDef def;
    {
        std::lock_guard<std::mutex> l(g_sessM);
        if (!g_hasDef || !g_defFits.load())
            return;
        def = g_def;
    }
    if (def.spawns.empty())
        return;
    const unsigned t = g_ticks - (g_passTick + 60);
    if (g_ticks < g_passTick + 60)
        return;
    const unsigned stop = t / static_cast<unsigned>(g_tourEvery), within = t % static_cast<unsigned>(g_tourEvery);
    const size_t n = def.spawns.size();
    if (within == 0 && stop < n * 2) {
        const size_t s = stop % n;
        teleport(guest, player, def.spawns[s].pos, static_cast<int>(s));
        arenaLog("tour: spawn %zu placed at %.0f %.0f %.0f", s, static_cast<double>(def.spawns[s].pos[0]),
                 static_cast<double>(def.spawns[s].pos[1]), static_cast<double>(def.spawns[s].pos[2]));
    } else if (within == static_cast<unsigned>(g_tourEvery) / 2 && stop < n * 2) {
        // A screenshot of this spawn point halfway through its stop (the camera has caught up), next to the input
        // script's own: DRAKAN_SHOT_DIR / tour_<round><spawn>.png
        if (const char* dir = std::getenv("DRAKAN_SHOT_DIR"); dir && *dir)
            gsr::requestWindowShot(std::string(dir) + "/tour_" + std::to_string(stop / n) + "_" +
                                   std::to_string(stop % n) + ".png");
    } else if (within == static_cast<unsigned>(g_tourEvery) - 2 && stop < n * 2) {
        const uint32_t cam = rd32(rdram, engineCtx(rdram) + 0x4b4);
        arenaLog("tour: spawn %zu: player now at %.0f %.0f %.0f, camera at %.0f %.0f %.0f", stop % n,
                 static_cast<double>(rdf(rdram, player + 0x14)), static_cast<double>(rdf(rdram, player + 0x18)),
                 static_cast<double>(rdf(rdram, player + 0x1c)), static_cast<double>(rdf(rdram, cam + 0x14)),
                 static_cast<double>(rdf(rdram, cam + 0x18)), static_cast<double>(rdf(rdram, cam + 0x1c)));
    }
}

// Test aid DRAKAN_ARENA_TEST_GOTO="<pickup index>@<boundaries after the first pass>[,...]": the player is put on that
// pickup (the engine takes the item the way it does when she walks over it), once each. Harness only: it makes a "take"
// repeatable.
unsigned g_gotoWatchUntil = 0;
int g_gotoWatch = -1;

void gotoTick(drakan_input::IGuest& guest, const uint8_t* rdram, uint32_t player) {
    if (g_sweepFrom && g_gotoWatch >= 0 && static_cast<size_t>(g_gotoWatch) < g_picks.size()) {
        // The engine's own movers raise the touch events: walk through the pickup in small steps instead of one jump.
        const unsigned t = g_ticks - g_sweepFrom;
        if (g_ticks >= g_sweepFrom && t < 90) {
            const Pick& p = g_picks[static_cast<size_t>(g_gotoWatch)];
            float pos[3] = {p.def.pos[0] - 900.0f + 20.0f * static_cast<float>(t), p.def.pos[1] - kSpawnLift + 40.0f,
                            p.def.pos[2]};
            teleportOnce(guest, player, pos);
        } else if (t >= 90) {
            g_sweepFrom = 0;
        }
    }
    if (g_gotoWatch >= 0 && g_ticks < g_gotoWatchUntil && g_ticks % 40 == 0 &&
        static_cast<size_t>(g_gotoWatch) < g_picks.size()) {
        const Pick& p = g_picks[static_cast<size_t>(g_gotoWatch)];
        arenaLog("test: player %.0f %.0f %.0f, pickup home %.0f %.0f %.0f, item actor %x %s at %.0f %.0f %.0f",
                 static_cast<double>(rdf(rdram, player + 0x14)), static_cast<double>(rdf(rdram, player + 0x18)),
                 static_cast<double>(rdf(rdram, player + 0x1c)), static_cast<double>(p.def.pos[0]),
                 static_cast<double>(p.def.pos[1]), static_cast<double>(p.def.pos[2]), p.handle,
                 liveAt(rdram, p) ? "live" : "gone", static_cast<double>(rdf(rdram, p.handle + 0x14)),
                 static_cast<double>(rdf(rdram, p.handle + 0x18)), static_cast<double>(rdf(rdram, p.handle + 0x1c)));
    }
    for (GotoStep& s : g_goto) {
        if (s.done || g_ticks < g_passTick + s.at || static_cast<size_t>(s.pickup) >= g_picks.size())
            continue;
        s.done = true;
        g_gotoWatch = s.pickup;
        g_gotoWatchUntil = g_ticks + 400;
        const Pick& p = g_picks[static_cast<size_t>(s.pickup)];
        arenaLog("test: going to pickup %d (%s) at %.0f %.0f %.0f", s.pickup, p.def.label.c_str(),
                 static_cast<double>(p.def.pos[0]), static_cast<double>(p.def.pos[1]),
                 static_cast<double>(p.def.pos[2]));
        float pos[3] = {p.def.pos[0], p.def.pos[1] - kSpawnLift + 60.0f, p.def.pos[2]};
        arenaShot("pickup" + std::to_string(s.pickup) + "_before");
        teleport(guest, player, pos, -1);
    }
}

void resetEeState() {
    g_picks.clear();
    g_spawnQueue.clear();
    g_spawnInFlight = false;
    g_haveStart = false;
    g_filteredGen = ~0u;
    g_ticks = 0;
    g_outsideTicks = 0;
    g_loadoutArmed = g_loadoutDone = false;
    g_loadoutPhase = 0;
    g_arm = ArmState();
    g_calls.clear();
    g_pendingStow.clear();
    g_removed = 0;
    g_place.active = false;
}

} // namespace

// ---------------------------------------------------------------------------------------------------- public API

std::vector<MapEntry> maps() {
    loadDefaults();
    std::lock_guard<std::mutex> l(g_catM);
    std::vector<MapEntry> out;
    for (const ArenaDef& a : g_catalog.arenas()) {
        MapEntry m;
        m.id = a.id;
        m.name = a.name;
        m.level = a.level;
        m.description = a.description;
        m.source = a.source;
        m.deathmatch = a.deathmatch;
        m.teamDeathmatch = a.teamDeathmatch;
        m.maxPlayers = a.maxPlayers;
        m.spawns = static_cast<int>(a.spawns.size());
        m.pickups = static_cast<int>(a.pickups.size());
        m.hasBounds = a.hasBounds;
        std::copy(a.bounds, a.bounds + 4, m.bounds);
        m.installed =
            !g_haveInstalled || std::find(g_installed.begin(), g_installed.end(), a.level) != g_installed.end();
        out.push_back(std::move(m));
    }
    return out;
}

const ArenaDef* findMap(const std::string& idOrName) {
    loadDefaults();
    std::lock_guard<std::mutex> l(g_catM);
    return g_catalog.find(idOrName);
}

size_t addMaps(const std::string& text, const std::string& source) {
    loadDefaults();
    std::vector<std::string> errors;
    size_t n;
    {
        std::lock_guard<std::mutex> l(g_catM);
        n = g_catalog.add(text, source, &errors);
    }
    for (const std::string& e : errors)
        arenaLog("maps: %s", e.c_str());
    arenaLog("maps: %zu from %s", n, source.c_str());
    return n;
}

void setInstalledLevels(std::vector<std::string> levels) {
    std::lock_guard<std::mutex> l(g_catM);
    g_installed = std::move(levels);
    g_haveInstalled = true;
}

void setSession(bool active, const ArenaDef* def) {
    std::lock_guard<std::mutex> l(g_sessM);
    const bool was = g_active.load();
    if (active) {
        const bool changed = !was || (def == nullptr) != !g_hasDef || (def && g_hasDef && def->id != g_def.id);
        if (def) {
            g_def = *def;
            g_hasDef = true;
        } else {
            g_def = ArenaDef();
            g_hasDef = false;
        }
        if (changed) {
            g_pickupEvents.clear();
            g_claims.clear();
            g_sessionSerial.fetch_add(1);
            arenaLog("arena world mode on: %s", def ? def->name.c_str() : "(plain level)");
        }
    } else if (was) {
        g_hasDef = false;
        g_def = ArenaDef();
        g_pickupEvents.clear();
        g_claims.clear();
        g_sessionSerial.fetch_add(1);
        arenaLog("arena world mode off");
    }
    g_active.store(active);
}

bool active() {
    return g_active.load();
}

bool hasSpawns() {
    std::lock_guard<std::mutex> l(g_sessM);
    return g_active.load() && g_hasDef && !g_def.spawns.empty();
}

void setLocalPlayer(uint8_t id) {
    g_local.store(id);
}

void onPickupState(uint16_t index, bool taken, uint8_t player) {
    std::lock_guard<std::mutex> l(g_sessM);
    if (g_pickupEvents.size() < 256)
        g_pickupEvents.push_back({index, taken, player});
}

bool takePickupClaim(uint16_t& index) {
    std::lock_guard<std::mutex> l(g_sessM);
    if (g_claims.empty())
        return false;
    index = g_claims.front();
    g_claims.pop_front();
    return true;
}

bool takeUseTap() {
    if (g_useTaps <= 0 || g_ticks < g_useTapAt)
        return false;
    --g_useTaps;
    g_useTapAt = g_ticks + 40;
    arenaLog("test: Use (L1) pressed");
    return true;
}

bool journalHidden() {
    return g_active.load();
}

bool placeAtSpawn(drakan_input::IGuest& guest, uint8_t* rdram, int index) {
    ArenaDef def;
    {
        std::lock_guard<std::mutex> l(g_sessM);
        if (!g_active.load() || !g_hasDef || !g_defFits.load() || index < 0 ||
            static_cast<size_t>(index) >= g_def.spawns.size())
            return false;
        def = g_def;
    }
    const uint32_t player = controlledActor(rdram);
    if (!ramPtr(player))
        return false;
    const auto& s = def.spawns[static_cast<size_t>(index)];
    if (!g_haveStart)
        captureStart(rdram, player);
    teleport(guest, player, s.pos, index);
    g_outsideTicks = 0;
    arenaLog("player placed at spawn %d: %.0f %.0f %.0f", index, static_cast<double>(s.pos[0]),
             static_cast<double>(s.pos[1]), static_cast<double>(s.pos[2]));
    armLoadout(rdram, player);
    return true;
}

void install(PS2Runtime& rt) {
    (void)rt;
    if (const char* v = std::getenv("DRAKAN_ARENA_LOG"))
        g_log = v[0] != '0';
    world::Globals g;
    g.contextPtr = guestAddress(g.contextPtr);
    g.hpKey = guestAddress(g.hpKey);
    g.playerStats = guestAddress(g.playerStats);
    g.handleVtable = guestAddress(g.handleVtable);
    if (!g.contextPtr || !g.hpKey || !g.playerStats || !g.handleVtable || !guestAddress(kKillActor)) {
        arenaLog("disabled: no address translation for this disc profile");
        return;
    }
    g_globals = g;
    for (uint32_t a = 0x100400u; a < 0x100500u && !(g_itemSlot1 && g_itemSlot2); a += 4) {
        if (rt.hasFunction(a))
            continue;
        if (!g_itemSlot1 && rt.registerFunction(a, &itemStage1))
            g_itemSlot1 = a;
        else if (g_itemSlot1 && !g_itemSlot2 && rt.registerFunction(a, &itemStage2))
            g_itemSlot2 = a;
    }
    loadDefaults();
    g_dumpWanted = std::getenv("DRAKAN_ARENA_DUMP") != nullptr;
    g_noFilter = std::getenv("DRAKAN_ARENA_NOFILTER") != nullptr;
    g_shots = std::getenv("DRAKAN_ARENA_SHOTS") != nullptr;
    if (const char* t = std::getenv("DRAKAN_ARENA_TOUR"))
        g_tourEvery = std::max(0, std::atoi(t));
    if (const char* t = std::getenv("DRAKAN_ARENA_TEST_GOTO")) {
        for (const char* p = t; *p;) {
            int idx = 0;
            unsigned at = 0;
            if (std::sscanf(p, "%d@%u", &idx, &at) == 2 && idx >= 0)
                g_goto.push_back({idx, at});
            while (*p && *p != ',')
                ++p;
            if (*p == ',')
                ++p;
        }
    }

    drakan_lifecycle::subscribe(drakan_lifecycle::Kind::AfterLevelLoad, [](const drakan_lifecycle::Event& e) {
        std::lock_guard<std::mutex> l(g_levelM);
        g_loadedLevel = e.level;
    });
    drakan_lifecycle::subscribe(drakan_lifecycle::Kind::BeforeLevelLoad, [](const drakan_lifecycle::Event&) {
        ++g_loadGen;
        g_ticks = 0;
        g_haveStart = false;
        g_place.active = false;
        g_picks.clear();
        g_spawnQueue.clear();
        g_spawnInFlight = false;
        g_calls.clear();
        g_pendingStow.clear();
        g_arm = ArmState();
        g_loadoutPhase = 0;
        g_loadoutArmed = false;
    });
    if (!g_vetoInstalled) {
        g_vetoInstalled = true;
        drakan_lifecycle::setSaveWriteVeto([] { return g_active.load(); });
    }
    // Test aid: DRAKAN_ARENA_FORCE=<map name or id> turns the world mode on without a network session (one instance).
    if (const char* f = std::getenv("DRAKAN_ARENA_FORCE"); f && *f) {
        const ArenaDef* d = findMap(f);
        if (d)
            setSession(true, d);
        else
            arenaLog("DRAKAN_ARENA_FORCE: unknown map '%s'", f);
    }
}

void beforePadMgr(drakan_input::IGuest& guest, uint8_t* rdram) {
    if (!g_globals.contextPtr)
        return;
    const uint32_t serial = g_sessionSerial.load();
    if (serial != g_seenSerial) {
        g_seenSerial = serial;
        resetEeState();
    }
    if (!g_active.load())
        return;
    const uint32_t player = controlledActor(rdram);
    if (!ramPtr(player) || !drakan_ghost::ready())
        return;
    ++g_ticks;
    if (g_filteredGen != g_loadGen) {
        if (g_ticks >= kSettleTicks)
            firstPass(guest, rdram, player);
        return;
    }
    applyPickupEvents(guest, rdram);
    const unsigned since = g_ticks - g_passTick;
    if (since < kSweepTicks ? since % 20 == 0 : since % 120 == 0) {
        if (snapshot(rdram))
            g_removed += removeCampaign(guest, rdram, since < kSweepTicks);
    }
    if (g_dumpWanted && since == kSweepTicks + 120 && snapshot(rdram))
        dumpWorld("after");
    if (g_ticks % 3 == 0)
        pickupTick(guest, rdram);
    pumpSpawns(guest, rdram);
    loadoutTick(guest, rdram, player);
    armTick(guest, rdram, player);
    placementTick(guest, rdram, player);
    gotoTick(guest, rdram, player);
    boundsTick(guest, rdram, player);
    tourTick(guest, rdram, player);
}

} // namespace drakan_arena
