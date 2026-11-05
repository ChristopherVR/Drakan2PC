// Drakan: multiplayer arena world mode (drakan_arena.h). PAL addresses are canonical, translated through the disc
// profile. Design, the list of removed classes and the evidence: docs/reference/netcode.md "Multiplayer maps",
// docs/journal/2026-10-31/mp-arenas.md.
#include "drakan_arena.h"
#include "drakan_config.h"
#include "drakan_coop_character.h"
#include "drakan_arena_filter.h"
#include "drakan_arena_placement.h"
#include "drakan_ghost.h"
#include "drakan_lifecycle.h"
#include "drakan_region.h"
#include "input/GuestInterface.h"
#include "net/Protocol.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "gsr/gsr.h"
#include "world/Inventory.h"
#include "world/Json.h"
#include "world/Reader.h"
#include "world/SpatialRegistration.h"

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
#include <future>
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
constexpr uint32_t kStow = 0x320f70u, kEquipPost = 0x341058u, kEquipDirect = 0x31e460u;
constexpr uint32_t kCtlOffset = 0x584u, kCtlCells = 0x30u, kCellCount = 24u, kHandSlots = 0xa48u;
constexpr uint32_t kClassMelee = 0x1000eu, kClassBow = 0x1004fu, kClassDragon = 0x1001cu;
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
    float home[3] = {0, 0, 0}; // where the live item rests (a spawned one settles away from its map position)
    unsigned boundTick = 0;    // boundary it became present
    uint32_t spawnHandle = 0;  // the handle the factory returned for the spawn in flight (0 = none / refused)
    world::SpatialRegistrationRetry spatialRegistration;
};
std::vector<Pick> g_picks;

struct SpawnReq {
    uint32_t resIndex = 0;
    float pos[3] = {0, 0, 0};
    int pickup = -1; // pickup index, -1 for a loadout item, kEffectSpawn for a short-lived effect (the dragon's breath)
    float rot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
};
constexpr int kEffectSpawn = -2;
struct LiveEffect {
    uint32_t handle = 0;
    unsigned dieTick = 0;
};
std::deque<LiveEffect> g_effects; // effect actors the factory made, killed when their life is over
unsigned g_effectLife = 40;       // ticks an effect actor lives
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
// DRAKAN_ARENA_LOADOUT_ADOPT="x,y,z;x,y,z": the level's own loose items at these spots (a bow cannot be made by the
// factory, see "Which res= resources spawn") are protected from the world filter and filed into the inventory as the
// loadout of every player (each instance owns a copy of the level's items).
std::vector<uint32_t> g_adoptLoadout;
uint32_t controlledActor(const uint8_t* rdram);
bool snapshot(const uint8_t* rdram);

uint32_t g_seenSerial = 0;         // session serial the EE state belongs to
uint32_t g_loadGen = 0;            // bumps at every level load (BeforeLevelLoad)
uint32_t g_filteredGen = ~0u;      // load generation the first pass ran for
unsigned g_ticks = 0;              // boundaries since the player was ready in this load
bool g_deferredLooseSweep = false; // recheck loose campaign items after loadout stow completes
unsigned g_passTick = 0;           // tick of the first pass
float g_startPos[3] = {0, 0, 0};
bool g_haveStart = false;
unsigned g_outsideTicks = 0;
bool g_loadoutArmed = false, g_loadoutDone = false;
unsigned g_removed = 0;
unsigned g_tourStop = 0, g_tourTick = 0;
int g_tourEvery = 0;
// The map's menu picture (requestPreviewShot): stage 0 idle, 1 placing / settling, 2 shot taken, waiting for the file.
std::atomic<int> g_shotStage{0};
std::atomic<int> g_shotDone{0};
std::string g_shotFile;
std::mutex g_shotM;
unsigned g_shotAt = 0;
unsigned g_shotTries = 0;
std::string g_envShotFile; // DRAKAN_ARENA_PREVIEW_SHOT: the test aid takes the picture by itself
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

void dropFromInventory(drakan_input::IGuest& guest, uint8_t* rdram, uint32_t handle);

void killActor(drakan_input::IGuest& guest, uint8_t* rdram, uint32_t actor) {
    const uint32_t eng = engineCtx(rdram);
    if (ramPtr(eng) && ramPtr(actor)) {
        // Campaign sweeps also remove items: a touch may already have filed this copy
        // into Rynn's inventory before the sweep's snapshot is consumed.
        dropFromInventory(guest, rdram, actor);
        guest.call(drakan_input::GuestCall(guestAddress(kKillActor)).arg(eng).arg(actor));
    }
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
        if (g_inflightPickup == kEffectSpawn) {
            if (handle)
                g_effects.push_back({handle, g_ticks + g_effectLife});
        } else if (g_inflightPickup < 0) {
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
                if (e.handle == ps.handle && isItemEntity(e)) {
                    live = true;
                    arenaLog("loadout: actor %x is live (type %u class %x resource index %u) at %.0f %.0f %.0f",
                             e.handle, e.type, e.classId, e.resourceIndex, static_cast<double>(e.position.x),
                             static_cast<double>(e.position.y), static_cast<double>(e.position.z));
                }
        // Inventory-only items (notably the starting dagger) are fully constructed but deliberately have no world
        // registry reference. Stow is the native path that takes ownership; waiting for a world snapshot deadlocks
        // the loadout after campaign placements have been stripped before initialization.
        const uint32_t type = ramPtr(ps.handle) ? rd32(rdram, ps.handle + 0xc) : 0;
        if (!live && ramPtr(ps.handle) && rd32(rdram, ps.handle) == g_globals.handleVtable &&
            !(rd32(rdram, ps.handle + 4) & 0x80000000u) && ramPtr(dataOf(rdram, ps.handle)) &&
            (type == world::type::kWeapon || type == world::type::kUsable)) {
            live = true;
            arenaLog("loadout: constructed private item %x accepted for native inventory stow", ps.handle);
        }
        if (live && ramPtr(player)) {
            arenaLog("loadout: inventory UI listener player+0xa28 = %x, stow state %x", rd32(rdram, player + 0xa28),
                     rd32(rdram, player + 0x584 + 0x20));
            g_pendingStow.pop_front();
            g_calls.push_back(drakan_input::GuestCall(guestAddress(kStow)).arg(player).arg(ps.handle));
        } else if (g_ticks > ps.since + 1200) { // a bow's model may still be loading
            arenaLog("loadout: actor %x never became a live actor, dropped (now vtable %x flags %x type %u ref %x "
                     "selfref %x, snapshot %s with %zu entities)",
                     ps.handle, rd32(rdram, ps.handle), rd32(rdram, ps.handle + 4), rd32(rdram, ps.handle + 0xc),
                     rd32(rdram, ps.handle + 0x10), (rd32(rdram, ps.handle + (world::handle::kSelfRef & ~3u)) >> 16),
                     g_worldOk ? "ok" : "failed", g_snap.entities.size());
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
    const float xform[12] = {r.pos[0], r.pos[1], r.pos[2], r.rot[0], r.rot[1], r.rot[2],
                             r.rot[3], r.rot[4], r.rot[5], r.rot[6], r.rot[7], r.rot[8]};
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
    if (r.pickup == kEffectSpawn)
        return;
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
        p.boundTick = g_ticks;
        std::memcpy(p.home, p.def.pos, sizeof p.home);
        arenaLog("pickup %zu (%s): bound to actor %x (resource index %u, class %x) at %.0f %.0f %.0f", i,
                 p.def.label.c_str(), best->handle, best->resourceIndex, best->classId,
                 static_cast<double>(best->position.x), static_cast<double>(best->position.y),
                 static_cast<double>(best->position.z));
    }
}

// A loose item is the map's own when it is bound to a pickup, or lies at an adopted one; items next to a spawned pickup
// (`res=`) are the level's own copies and go: the map spawns its own (and can respawn it).
bool nearAnyPickup(const world::Vec3& pos, uint32_t handle) {
    if (drakan_ghost::isGhostItem(handle))
        return true;
    for (uint32_t h : g_adoptLoadout)
        if (h == handle)
            return true;
    for (const Pick& p : g_picks) {
        if (p.state == PState::Unbound && !p.def.resId)
            continue;
        if (p.handle == handle || p.spawnHandle == handle)
            return true;
        if (!p.def.resId && dist2xz(pos, p.def.pos) < kHomeRadius)
            return true;
    }
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

// The engine takes a loose item into the inventory by touch. A pickup copy that someone else took first is removed
// here, but our own player may already hold it (she stands next to it at the spawn): the inventory cell and the hand
// slot would then point at a destroyed actor and the engine's equip / use code calls through its freed data (a null
// JALR at 0x341548 that ended the guest thread). Clear every reference to the handle before the actor goes.
uint32_t itemClassOf(const uint8_t* rdram, uint32_t handle);

void dropFromInventory(drakan_input::IGuest& guest, uint8_t* rdram, uint32_t handle) {
    const uint32_t actor = controlledActor(rdram);
    if (!ramPtr(actor) || itemClassOf(rdram, actor) != world::kClassRynn)
        return;
    const uint32_t player = dataOf(rdram, actor);
    if (!ramPtr(player) || !handle)
        return;
    const unsigned n = world::detachInventoryItem(
        itemClassOf(rdram, actor), player, handle, [&](uint32_t address) { return rd32(rdram, address); },
        [&](uint32_t address, uint32_t value) { guest.write32(address, value); });
    if (n) {
        arenaLog("pickup copy %x was already in our inventory: %u reference(s) cleared before it is removed", handle,
                 n);
    }
}

unsigned removeCampaign(drakan_input::IGuest& guest, uint8_t* rdram, bool sweepItems) {
    if (g_noFilter)
        return 0;
    unsigned n = 0, creatures = 0, story = 0, items = 0;
    // Player avatars can be NPC-class actors. Protect every ghost-owned body (local and remote), not just Rynn-class
    // players and held items; otherwise the periodic campaign sweep deletes the avatar and starts a respawn loop.
    std::vector<uint32_t> playerBodies;
    drakan_ghost::forEach([&](const drakan_ghost::Info& body) {
        if (body.handle)
            playerBodies.push_back(body.handle);
    });
    const uint32_t owner = controlledActor(rdram);
    const uint32_t ownerData = ramPtr(owner) ? dataOf(rdram, owner) : 0;
    const uint32_t ownerClass = ramPtr(ownerData) ? itemClassOf(rdram, owner) : 0;
    for (const auto& e : g_snap.entities) {
        if (detail::protectedPlayerActor(e.handle, g_snap.playerHandle, g_snap.cameraHandle,
                                         e.type == world::type::kPlayer, playerBodies))
            continue;
        const bool creature = e.hpKind == world::HpKind::Creature && e.hpMax > 0;
        const bool storyObj = isStoryClass(e.classId);
        bool item = false;
        if (sweepItems && isItemEntity(e) && !storyObj) {
            const float d = dist2xz(e.position, g_startPos);
            item = !(g_haveStart && d < kKeepRadius) && !nearAnyPickup(e.position, e.handle) &&
                   !world::inventoryReferencesItem(ownerClass, ownerData, e.handle,
                                                   [&](uint32_t address) { return rd32(rdram, address); });
        }
        if (!creature && !storyObj && !item)
            continue;
        if (item && !creature && !storyObj)
            arenaLog("sweep: loose item %x (class %x) at %.0f %.0f %.0f", e.handle, e.classId,
                     static_cast<double>(e.position.x), static_cast<double>(e.position.y),
                     static_cast<double>(e.position.z));
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

// Facing of the placement in degrees (0 = +Z, 90 = +X), NaN = keep the heading. The actor's rotation is three rows of
// three floats at +0x20: rows 0 and 2 hold the horizontal axes, row 2 is where she looks (drakan_pvp's test placement
// puts a ghost in front along it).
float g_placeYaw = std::nanf("");

void writeYaw(drakan_input::IGuest& guest, uint32_t player, float yawDeg) {
    const float rad = yawDeg * 3.14159265f / 180.0f, fx = std::sin(rad), fz = std::cos(rad);
    const float m[9] = {fz, 0, -fx, 0, 1, 0, fx, 0, fz};
    for (uint32_t i = 0; i < 9; ++i)
        guest.writeF(player + 0x20 + 4u * i, m[i]);
}

bool placementUsesPalRynnLayout(drakan_input::IGuest& guest, uint32_t actor) {
    if (!drakan_region::build().identityAddresses)
        return false;
    const auto& layout = drakan_region::build().camera;
    const uint32_t context = guest.read32(guestAddress(kContextPtr));
    if (!detail::placementPointer(context, layout.contextResources + 4))
        return false;
    const uint32_t resources = guest.read32(context + layout.contextResources);
    if (!detail::placementPointer(resources, std::max(layout.resourceTable, layout.resourceCount) + 4) ||
        !detail::placementPointer(actor, layout.handleResource + 2))
        return false;
    const uint16_t index = guest.read16(actor + layout.handleResource);
    if (index == 0xffff || index > guest.read32(resources + layout.resourceCount))
        return false;
    const uint64_t record =
        uint64_t(guest.read32(resources + layout.resourceTable)) + uint64_t(index) * layout.resourceStride;
    return record <= UINT32_MAX && detail::placementPointer(static_cast<uint32_t>(record), layout.resourceClass + 4) &&
           guest.read32(static_cast<uint32_t>(record) + layout.resourceClass) == world::kClassRynn;
}

void teleportOnce(drakan_input::IGuest& guest, uint32_t player, const float pos[3]) {
    const float placed[3] = {pos[0], pos[1] + kSpawnLift, pos[2]};
    for (uint32_t k = 0; k < 3; ++k)
        guest.writeF(player + 0x14 + 4 * k, placed[k]);
    // Placement bypasses native movement. Do not turn its displacement or an old
    // impact-speed cache into landing damage on the next native physics tick.
    detail::resetPlacementMotion(guest, player, placed, placementUsesPalRynnLayout(guest, player));
    if (!std::isnan(g_placeYaw))
        writeYaw(guest, player, g_placeYaw);
    drakan_ghost::refileActor(guest, player);
}

// The way a spawn point faces: its own `yaw` when the map gives one, else toward the middle of the arena.
float spawnYawDeg(const ArenaDef& def, const drakan::net::ArenaSpawn& s) {
    if (s.yawDeg != 0.0f)
        return s.yawDeg;
    float cx = 0, cz = 0;
    if (def.hasBounds) {
        cx = (def.bounds[0] + def.bounds[2]) * 0.5f;
        cz = (def.bounds[1] + def.bounds[3]) * 0.5f;
    } else {
        for (const auto& o : def.spawns) {
            cx += o.pos[0];
            cz += o.pos[2];
        }
        cx /= static_cast<float>(std::max<size_t>(def.spawns.size(), 1));
        cz /= static_cast<float>(std::max<size_t>(def.spawns.size(), 1));
    }
    return std::atan2(cx - s.pos[0], cz - s.pos[2]) * 180.0f / 3.14159265f;
}

// The engine puts a player back where she came from when no floor is resident at the target (the terrain streams around
// her): a far teleport may fail the first time. The placement is watched and repeated (every 40 boundaries, 40 times:
// terrain streams in by wall time, a fast machine runs through six tries before the floor is there); a spawn point that
// never takes is given up and she plays on from where she landed.
struct GotoStep {
    int pickup;
    unsigned at;
    bool take = false; // DRAKAN_ARENA_TEST_TAKE: the item vanishes (a take without walking)
    bool done = false;
    std::chrono::steady_clock::time_point killTime;
    unsigned killAt = 0; // take aid: boundary the item vanishes (0 = the player is not on her way yet)
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
// The spawn point the server gave this player: the game applies its own start position when a level comes up, which can
// land after a placement made during the load (the player then stands at the level start, outside the arena). The
// assignment is kept across level loads and applied again once the level is up (firstPass); a placement that "took" is
// watched for kPlaceHoldTicks more boundaries and made again if she is pulled away.
int g_assignedSpawn = -1;
unsigned g_placeHoldUntil = 0;
constexpr unsigned kPlaceHoldTicks = 120;
constexpr unsigned kPlaceTries = 40, kPlaceRetryTicks = 40;

void teleport(drakan_input::IGuest& guest, uint32_t player, const float pos[3], int index = -1,
              float yawDeg = std::nanf("")) {
    g_placeYaw = yawDeg;
    teleportOnce(guest, player, pos);
    g_place.active = true;
    std::memcpy(g_place.target, pos, sizeof g_place.target);
    g_place.tries = 1;
    g_place.nextTick = g_ticks + kPlaceRetryTicks;
    g_place.index = index;
}

void placementTick(drakan_input::IGuest& guest, const uint8_t* rdram, uint32_t player) {
    if (!g_place.active && g_placeHoldUntil > g_ticks && g_place.index >= 0) {
        const float hx = rdf(rdram, player + 0x14) - g_place.target[0],
                    hz = rdf(rdram, player + 0x1c) - g_place.target[2];
        const float hd = std::sqrt(hx * hx + hz * hz);
        if (hd > kPlaceTolerance) {
            arenaLog("pulled %.0f units away from spawn %d right after the placement: placing again",
                     static_cast<double>(hd), g_place.index);
            g_place.active = true;
            g_place.tries = 1;
            g_place.nextTick = g_ticks + kPlaceRetryTicks;
            teleportOnce(guest, player, g_place.target);
        }
        return;
    }
    if (!g_place.active || g_ticks < g_place.nextTick)
        return;
    const float dx = rdf(rdram, player + 0x14) - g_place.target[0], dz = rdf(rdram, player + 0x1c) - g_place.target[2];
    const float d = std::sqrt(dx * dx + dz * dz);
    if (d <= kPlaceTolerance) {
        arenaLog("placement of spawn %d took after %u %s (%.0f units from the point)", g_place.index, g_place.tries,
                 g_place.tries == 1 ? "try" : "tries", static_cast<double>(d));
        g_place.active = false;
        g_placeHoldUntil = g_ticks + kPlaceHoldTicks;
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

void bindAdoptLoadout() {
    g_adoptLoadout.clear();
    const char* e = std::getenv("DRAKAN_ARENA_LOADOUT_ADOPT");
    if (!e || !*e)
        return;
    for (const char* p = e; *p;) {
        float at[3];
        if (std::sscanf(p, "%f,%f,%f", &at[0], &at[1], &at[2]) == 3) {
            const world::EntitySnapshot* best = nullptr;
            float bestD = kAdoptRadius * kAdoptRadius;
            for (const auto& en : g_snap.entities) {
                if (!isItemEntity(en) || std::fabs(en.position.y - at[1]) > 600.0f)
                    continue;
                const float d = dist2xz(en.position, at);
                if (d < bestD) {
                    bestD = d;
                    best = &en;
                }
            }
            if (best) {
                g_adoptLoadout.push_back(best->handle);
                arenaLog("loadout adopt: the level's item %x (class %x) at %.0f %.0f %.0f is every player's loadout",
                         best->handle, best->classId, static_cast<double>(best->position.x),
                         static_cast<double>(best->position.y), static_cast<double>(best->position.z));
            } else {
                arenaLog("loadout adopt: no loose item near %.0f %.0f %.0f", static_cast<double>(at[0]),
                         static_cast<double>(at[1]), static_cast<double>(at[2]));
            }
        }
        while (*p && *p != ';')
            ++p;
        if (*p == ';')
            ++p;
    }
}

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
    bindAdoptLoadout();
    if (std::getenv("DRAKAN_ARENA_FORCE")) // test: no spawn event arms it
        armLoadout(rdram, player);
    g_removed += removeCampaign(guest, rdram, true);
    g_passTick = g_ticks;
    g_deferredLooseSweep = false;
    g_filteredGen = g_loadGen;
    if (g_assignedSpawn >=
        0) { // the placement made before / during the load is not trusted: the level start was applied
        arenaLog("level up: placing at the assigned spawn %d again", g_assignedSpawn);
        placeAtSpawn(guest, rdram, g_assignedSpawn);
    }
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
    bool attemptedSpatialRegistration = false;
    const uint32_t owner = controlledActor(rdram);
    const uint32_t ownerClass = itemClassOf(rdram, owner);
    const uint32_t ownerData = ownerClass == world::kClassRynn ? dataOf(rdram, owner) : 0;
    for (size_t i = 0; i < g_picks.size(); ++i) {
        Pick& p = g_picks[i];
        switch (p.state) {
            case PState::Unbound:
                break;
            case PState::Present: {
                bool here = liveAt(rdram, p);
                // A newly spawned item may be collected while its initial position is
                // still settling. Inventory ownership outranks following that position.
                if (here && world::inventoryReferencesItem(ownerClass, ramPtr(ownerData) ? ownerData : 0, p.handle,
                                                           [&](uint32_t address) { return rd32(rdram, address); }))
                    here = false;
                if (here) {
                    float pos[3];
                    for (int k = 0; k < 3; ++k)
                        pos[k] = rdf(rdram, p.handle + 0x14 + 4u * static_cast<uint32_t>(k));
                    if (g_ticks < p.boundTick + 150) // still settling (a spawned item falls to the ground): follow it
                        std::memcpy(p.home, pos, sizeof pos);
                    const float dx = pos[0] - p.home[0], dz = pos[2] - p.home[2];
                    here = std::sqrt(dx * dx + dz * dz) < kHomeRadius && std::fabs(pos[1] - p.home[1]) < 800.0f;
                    // The factory can run before the destination terrain streams in, leaving
                    // a valid actor with cell -1: invisible and absent from native Use queries.
                    // Never refile an item already moved into inventory or claimed by a peer.
                    if (p.def.resId &&
                        p.spatialRegistration.service(
                            g_ticks, here && !p.serverTaken && !(rd32(rdram, p.handle + 4) & 0x80000000u),
                            static_cast<int32_t>(rd32(rdram, p.handle + 0x7cu)), attemptedSpatialRegistration, [&] {
                                drakan_ghost::refileActor(guest, p.handle);
                                return static_cast<int32_t>(rd32(rdram, p.handle + 0x7cu));
                            })) {
                        arenaLog("pickup %zu (%s): recovered native collision cell %d for actor %x", i,
                                 p.def.label.c_str(), static_cast<int32_t>(rd32(rdram, p.handle + 0x7cu)), p.handle);
                    }
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
                    p.boundTick = g_ticks;
                    p.home[0] = found->position.x;
                    p.home[1] = found->position.y;
                    p.home[2] = found->position.z;
                    arenaLog("pickup %zu (%s) is back (actor %x, %s)", i, p.def.label.c_str(), found->handle,
                             p.spawnHandle == found->handle ? "by handle" : "by scan");
                    arenaShot("pickup" + std::to_string(i) + "_back");
                } else if (p.spawnHandle && g_ticks > p.spawnTick + 45 && ramPtr(p.spawnHandle) &&
                           (rd32(rdram, p.spawnHandle + 0x10) >> 16) == 0) {
                    // The factory made an object the registry never took: the resource's model is not part of this
                    // level, a retry cannot help.
                    arenaLog(
                        "pickup %zu (%s): resource %x cannot be spawned in this level (never registered), disabled", i,
                        p.def.label.c_str(), p.def.resId);
                    p.state = PState::Unbound;
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
    if (!g_hasDef || !g_defFits.load() ||
        (g_def.loadout.empty() && !std::getenv("DRAKAN_ARENA_LOADOUT") && !std::getenv("DRAKAN_ARENA_LOADOUT_ADOPT")) ||
        g_loadoutDone)
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

uint32_t itemClassOf(const uint8_t* rdram, uint32_t handle);

// The controlled actor is Arokh while Rynn rides him (air arenas start mounted, netcode.md "Air arenas"): his data
// object has no inventory, so the loadout stow / equip routines would call through garbage (JALR into 0x56d2c0).
bool controlledIsDragon(const uint8_t* rdram, uint32_t actor) {
    return itemClassOf(rdram, actor) == kClassDragon;
}

void loadoutTick(drakan_input::IGuest&, uint8_t* rdram, uint32_t player) {
    if (controlledIsDragon(rdram, player))
        return;
    if (!g_loadoutArmed || g_loadoutPhase >= 2)
        return;
    if (g_loadoutPhase == 0) {
        std::vector<uint32_t> ids;
        {
            std::lock_guard<std::mutex> l(g_sessM);
            ids = g_def.loadout;
        }
        // Test aid DRAKAN_ARENA_LOADOUT="0x31bc9 0x3379d": replaces the map's loadout for this instance (a bow for one
        // player, something else for the other).
        if (const char* e = std::getenv("DRAKAN_ARENA_LOADOUT"); e && *e) {
            ids.clear();
            for (const char* q = e; *q;) {
                char* end = nullptr;
                const unsigned long v = std::strtoul(q, &end, 0);
                if (end == q) {
                    ++q;
                    continue;
                }
                ids.push_back(static_cast<uint32_t>(v));
                q = end;
            }
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
        for (uint32_t h : g_adoptLoadout) {
            g_loadoutIdx.push_back(0);
            g_pendingStow.push_back({h, g_ticks});
        }
        if (g_loadoutIdx.empty())
            g_loadoutPhase = 2;
        return;
    }
    if (g_spawnQueue.empty() && !g_spawnInFlight && g_calls.empty() && g_pendingStow.empty() &&
        g_ticks > g_loadoutStart + 20) {
        arenaLog("loadout: %zu items given", g_loadoutIdx.size());
        g_loadoutPhase = 2;
    } else if (g_ticks > g_loadoutStart + 1500) {
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
std::atomic<bool> g_quickRequest{false};
struct ArmState {
    bool done = false;
    unsigned next = 0;
    int tries = 0;
} g_arm;

void armTick(drakan_input::IGuest& guest, uint8_t* rdram, uint32_t actor) {
    const uint32_t player = dataOf(rdram, actor);
    if (!ramPtr(player))
        return;
    if (g_arm.done)
        return;
    if (controlledIsDragon(rdram, actor))
        return;
    if (std::getenv("DRAKAN_ARENA_SPELL")) // a caster keeps her hands for the spell (spellTick)
        return;
    if (g_loadoutArmed && g_loadoutPhase < 2)
        return;
    if (guest.postBusy() || !g_calls.empty() || !g_pendingStow.empty() || g_spawnInFlight || !g_spawnQueue.empty())
        return;
    const uint32_t hand = rd32(rdram, player + kHandSlots);
    const char* preferEnv0 = std::getenv("DRAKAN_ARENA_EQUIP");
    if (ramPtr(hand) && !(preferEnv0 && std::strcmp(preferEnv0, "bow") == 0)) {
        arenaLog("armed: right hand holds actor %x (class %x)", hand, itemClassOf(rdram, hand));
        g_arm.done = true;
        return;
    }
    if (g_ticks < g_arm.next)
        return;
    g_arm.next = g_ticks + 120;
    const uint32_t ctl = player + kCtlOffset;
    int cell = -1;
    uint32_t slot = 0;
    const char* preferEnv = std::getenv("DRAKAN_ARENA_EQUIP"); // test aid: "bow" equips a bow before any melee weapon
    const bool preferBow = preferEnv && std::strcmp(preferEnv, "bow") == 0;
    if (preferBow) {
        // The archer waits for her bow (picked up from the ground: a bow cannot be spawned, see "Which res= resources
        // spawn") and does not fall back to a melee weapon; the bow goes to the left hand, so a hand check would
        // re-equip (= put away) it.
        bool haveBow = false;
        for (uint32_t i = 0; i < kCellCount; ++i) {
            const uint32_t h = rd32(rdram, ctl + kCtlCells + i * 16u + 8u);
            if (h <= 0x53u || !ramPtr(h) || itemClassOf(rdram, h) != kClassBow)
                continue;
            haveBow = true;
            if (rd32(rdram, ctl + kCtlCells + i * 16u) != 0) {
                arenaLog("armed: the bow (actor %x) is equipped", h);
                g_arm.done = true;
                return;
            }
        }
        if (!haveBow)
            return;
    }
    for (int pass = 0; pass < (preferBow ? 1 : 2) && cell < 0; ++pass)
        for (uint32_t i = 0; i < kCellCount; ++i) {
            const uint32_t h = rd32(rdram, ctl + kCtlCells + i * 16u + 8u);
            if (h <= 0x53u || !ramPtr(h))
                continue;
            const uint32_t cls = itemClassOf(rdram, h);
            // The engine refuses to equip an item its usability check (0x341858: broken / unusable) fails.
            if (cls == ((pass == 0) != preferBow ? kClassMelee : kClassBow) &&
                guest.call(drakan_input::GuestCall(guestAddress(0x341858u)).arg(h)) != 0) {
                cell = static_cast<int>(i);
                slot = pass;
                break;
            }
        }
    if (cell < 0) {
        // No weapon the player may use: a new game outside the first level starts with a weapon that needs more melee
        // skill than she has (property 0x13 vs the skill record, 0x341858). Everybody in the arena gets the skill the
        // first melee weapon asks for (skill 1 = melee, 2 = bow; the record's +4 is the plain level).
        for (uint32_t i = 0; i < kCellCount && g_arm.tries < 3; ++i) {
            const uint32_t h = rd32(rdram, ctl + kCtlCells + i * 16u + 8u);
            if (h <= 0x53u || !ramPtr(h))
                continue;
            const uint32_t cls = itemClassOf(rdram, h);
            if (cls != kClassMelee && cls != kClassBow)
                continue;
            const uint32_t block = rd32(rdram, h + 0x54), vt = ramPtr(block) ? rd32(rdram, block + 8) : 0;
            if (!ramPtr(vt))
                continue;
            const uint32_t fn = rd32(rdram, vt + 0x1c);
            const int16_t off = static_cast<int16_t>(rd32(rdram, vt + 0x18) & 0xffffu);
            const uint32_t pp = guest.call(drakan_input::GuestCall(fn)
                                               .arg(block + static_cast<uint32_t>(static_cast<int32_t>(off)))
                                               .arg(0x13u)
                                               .arg(0xffffffffu));
            const uint32_t need = ramPtr(pp) ? rd32(rdram, pp) : 0;
            const uint32_t skill = guest.call(drakan_input::GuestCall(guestAddress(0x3d5a30u))
                                                  .arg(guestAddress(0x4fbac8u))
                                                  .arg(cls == kClassBow ? 2u : 1u));
            if (!ramPtr(skill) || need > 50)
                continue;
            const uint32_t have = rd32(rdram, skill + 4);
            arenaLog("  weapon %x needs skill %u, has %u%s", h, need, have, have < need ? ": raised" : "");
            if (have < need)
                guest.write32(skill + 4, need);
        } // every weapon of the inventory: a bow needs its own skill (2) next to the melee one
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
    if (g_arm.tries == 1 || g_arm.tries == 4) {
        for (uint32_t i = 0; i < kCellCount; ++i) {
            const uint32_t h = rd32(rdram, ctl + kCtlCells + i * 16u + 8u);
            if (h)
                arenaLog("  cell %u: equipped %u hot %u handle %x class %x", i, rd32(rdram, ctl + kCtlCells + i * 16u),
                         rd32(rdram, ctl + kCtlCells + i * 16u + 4u), h, itemClassOf(rdram, h));
        }
        arenaLog("  hands: %x %x %x %x, state %x flags %x, ctl select %d", rd32(rdram, player + kHandSlots),
                 rd32(rdram, player + kHandSlots + 0x10), rd32(rdram, player + kHandSlots + 0x20),
                 rd32(rdram, player + kHandSlots + 0x30), rd32(rdram, player + 0x1040), rd32(rdram, player + 0x94),
                 static_cast<int>(rd32(rdram, ctl + 0x20)));
    }
    arenaLog("armed: equipping cell %d (actor %x) into slot %u (try %d, %s)", cell,
             rd32(rdram, ctl + kCtlCells + static_cast<uint32_t>(cell) * 16u + 8u), slot, g_arm.tries, "quick slot");
    if (g_arm.tries == 1) { // why would the engine refuse? (31e460's checks, read-only helpers)
        const uint32_t item = rd32(rdram, ctl + kCtlCells + static_cast<uint32_t>(cell) * 16u + 8u);
        const uint32_t r858 = guest.call(drakan_input::GuestCall(guestAddress(0x341858u)).arg(item));
        const uint32_t r4a8 = guest.call(drakan_input::GuestCall(guestAddress(0x3414a8u)).arg(ctl).arg(item));
        const uint32_t r510 = guest.call(drakan_input::GuestCall(guestAddress(0x341510u)).arg(ctl).arg(item));
        arenaLog("  equip checks: stats+0x70 %u, player+0x1040 %u, can %u, 3414a8 %u, 341510 %u",
                 rd32(rdram, guestAddress(0x4fbac8u) + 0x70), rd32(rdram, player + 0x1040), r858, r4a8, r510);
    }
    // Equipping needs the engine's own use path (a posted message / a direct call did nothing visible): mark the cell
    // as the only quick slot and let the input glue press QUICK1 (the native bar confirms and equips).
    for (uint32_t i = 0; i < kCellCount; ++i)
        guest.write32(ctl + kCtlCells + i * 16u + 4u, i == static_cast<uint32_t>(cell) ? 1u : 0u);
    guest.write32(ctl + 0x238u, 1u); // list dirty: rebuilt at the next open
    g_quickRequest.store(true);
}

// Test aid DRAKAN_ARENA_SPELL=<index>: gives this instance the spells and selects one (a caster instead of an archer or
// swordsman). The debug console's "shaman" command unlocks the spells (the string is the game's own, 0x51e3c8); the
// selector 0x3422c8 (controller = player + 0x584, selected index at controller + 0x4b4) arms it with commit = 1. Posted
// calls: both play sounds and show messages.
struct SpellState {
    int phase = 0;
    unsigned next = 0;
    int tries = 0;
} g_spell;

void spellTick(drakan_input::IGuest& guest, uint8_t* rdram, uint32_t actor) {
    const char* e = std::getenv("DRAKAN_ARENA_SPELL");
    const uint32_t player = dataOf(rdram, actor);
    if (!e || !*e || !ramPtr(player) || g_ticks < g_passTick + kSweepTicks + 60 || g_ticks < g_spell.next ||
        guest.postBusy())
        return;
    if (g_spell.phase >= 3) {
        // A spell costs 15 percent of the mana bar: refill it every five seconds so a scripted caster keeps casting.
        // The stats block (g_globals.playerStats) keeps current mana at +0x1c and the maximum at +0x20 in the same
        // encoding as the health words (enc(v) = rotl32(v, 8) ^ key). A new game has no mana bar (maximum 0, the orb is
        // empty), so the maximum is raised to 100 first. (The console's `heal` also refills health, which the server
        // owns in a match.)
        g_spell.next = g_ticks + 300;
        const uint32_t stats = g_globals.playerStats;
        const auto dec = [&](uint32_t raw) {
            const uint32_t x = raw ^ rd32(rdram, g_globals.hpKey);
            return static_cast<int32_t>((x >> 8) | (x << 24));
        };
        const uint32_t key = rd32(rdram, g_globals.hpKey);
        const auto enc = [&](int32_t v) {
            const uint32_t u = static_cast<uint32_t>(v);
            return ((u << 8) | (u >> 24)) ^ key;
        };
        const int32_t before = dec(rd32(rdram, stats + 0x1c));
        int32_t mx = dec(rd32(rdram, stats + 0x20));
        if (mx < 100) { // a new game starts without a mana bar (the orb is empty): give her the full one
            guest.write32(stats + 0x20, enc(100));
            mx = 100;
        }
        if (before < mx) {
            guest.write32(stats + 0x1c, enc(mx));
            arenaLog("spell: mana refilled %d -> %d", before, mx);
        }
        return;
    }
    const uint32_t ctl = player + 0x584u; // the inventory controller: selected spell at +0x4b4, spells at player+0x1028
    g_spell.next = g_ticks + 90;
    if (g_spell.phase == 0) {
        arenaLog("spell: unlocking the spells (shaman)");
        guest.post(drakan_input::GuestCall(guestAddress(0x240480u)).arg(guestAddress(0x51e3c8u)));
        g_spell.phase = 1;
    } else if (g_spell.phase == 1) {
        const int idx = std::atoi(e);
        const uint32_t table = rd32(rdram, player + 0x1028u);
        arenaLog("spell: selecting spell %d (selected now %d, cast state %u, %u spells, first record level %u)", idx,
                 static_cast<int>(rd32(rdram, ctl + 0x4b4u)), rd32(rdram, player + 0x1040u),
                 rd32(rdram, player + 0x1030u), ramPtr(table) ? rd32(rdram, table) : 0u);
        guest.post(drakan_input::GuestCall(guestAddress(0x3422c8u)).arg(ctl).arg(static_cast<uint32_t>(idx)).arg(1u));
        g_spell.phase = 2;
    } else {
        arenaLog("spell: selected %d, hand %x, mana word %u", static_cast<int>(rd32(rdram, ctl + 0x4b4u)),
                 rd32(rdram, player + kHandSlots), rd32(rdram, player + 0x1040u));
        if (static_cast<int>(rd32(rdram, ctl + 0x4b4u)) == std::atoi(e) || ++g_spell.tries > 4)
            g_spell.phase = 3;
        else
            g_spell.phase = 1;
    }
}

void boundsTick(drakan_input::IGuest& guest, uint8_t* rdram, uint32_t player) {
    if (g_shotStage.load()) {
        g_outsideTicks = 0;
        return;
    }
    ArenaDef def;
    {
        std::lock_guard<std::mutex> l(g_sessM);
        if (!g_hasDef || !g_defFits.load()) {
            g_outsideTicks = 0;
            return;
        }
        def = g_def;
    }
    if (g_place.active) { // a placement is already retrying: a fresh teleport would only reset its tries
        g_outsideTicks = 0;
        return;
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
    teleport(guest, player, def.spawns[static_cast<size_t>(s)].pos, s,
             spawnYawDeg(def, def.spawns[static_cast<size_t>(s)]));
}

// Test aid DRAKAN_ARENA_TEST_FALL=<boundaries after the level pass>: once, the player is dropped far below the map so
// the fall-out-of-world respawn (boundsTick) can be watched.
void testFallTick(drakan_input::IGuest& guest, uint8_t* rdram, uint32_t player, unsigned since) {
    static const unsigned at = [] {
        const char* e = std::getenv("DRAKAN_ARENA_TEST_FALL");
        return e ? static_cast<unsigned>(std::max(1, std::atoi(e))) : 0u;
    }();
    static unsigned doneGen = ~0u;
    if (at == 0 || since < at || doneGen == g_loadGen)
        return;
    doneGen = g_loadGen;
    const float pos[3] = {rdf(rdram, player + 0x14), rdf(rdram, player + 0x18) - 30000.0f, rdf(rdram, player + 0x1c)};
    arenaLog("test: dropping the player to y=%.0f", static_cast<double>(pos[1]));
    for (int k = 0; k < 3; ++k)
        guest.writeF(player + 0x14 + 4u * static_cast<uint32_t>(k), pos[k]);
}

// Test aid DRAKAN_ARENA_TEST_SAVE=<boundaries after the level pass>: the engine's save writer (0x169c28, what the menu
// save, the quick save and the level-change save all call) is called once, so the veto can be seen refusing it
// ('[lifecycle] save write vetoed' and a return value of 1).
void testSaveTick(drakan_input::IGuest& guest, unsigned since) {
    static const unsigned at = [] {
        const char* e = std::getenv("DRAKAN_ARENA_TEST_SAVE");
        return e ? static_cast<unsigned>(std::max(1, std::atoi(e))) : 0u;
    }();
    static unsigned doneGen = ~0u;
    if (at == 0 || since < at || doneGen == g_loadGen)
        return;
    doneGen = g_loadGen;
    const uint32_t r = guest.call(drakan_input::GuestCall(guestAddress(0x169c28u)).arg(0).arg(0).arg(0).arg(0));
    arenaLog("test: save writer called, returned %u (1 = refused by the veto)", r);
}

// The menu picture: the player is placed at the map's preview point (yaw from the map, the engine's follow camera comes
// along behind her), the camera's look pitch is set, and after the terrain around has streamed in the window is saved.
constexpr unsigned kShotSettleTicks = 240, kShotWriteTicks = 40;

void previewShotTick(drakan_input::IGuest& guest, uint8_t* rdram, uint32_t player) {
    const int stage = g_shotStage.load();
    if (stage == 0) {
        if (g_envShotFile.empty() || g_ticks < g_passTick + 600)
            return;
        requestPreviewShot(g_envShotFile);
        g_envShotFile.clear();
        return;
    }
    ArenaDef def;
    {
        std::lock_guard<std::mutex> l(g_sessM);
        if (!g_hasDef || !g_defFits.load()) {
            g_shotStage = 0;
            return;
        }
        def = g_def;
    }
    float pos[3], yaw = 0, pitch = 0;
    if (def.hasPreview) {
        std::copy(def.preview, def.preview + 3, pos);
        yaw = def.preview[3];
        pitch = def.preview[4];
    } else if (!def.spawns.empty()) {
        std::copy(def.spawns[0].pos, def.spawns[0].pos + 3, pos);
        yaw = spawnYawDeg(def, def.spawns[0]);
    } else {
        g_shotStage = 0;
        return;
    }
    const uint32_t camH = rd32(rdram, engineCtx(rdram) + 0x4b4);
    const uint32_t camObj = ramPtr(camH) ? rd32(rdram, rd32(rdram, camH + 0xfc) + 0x10) : 0;
    if (stage == 1) {
        if (g_shotAt == 0) {
            g_shotAt = g_ticks + kShotSettleTicks;
            teleport(guest, player, pos, -3, yaw);
            arenaLog("preview shot: standing at %.0f %.0f %.0f yaw %.0f pitch %.0f", static_cast<double>(pos[0]),
                     static_cast<double>(pos[1]), static_cast<double>(pos[2]), static_cast<double>(yaw),
                     static_cast<double>(pitch));
        }
        if (ramPtr(camObj))
            guest.writeF(camObj + 0x84, pitch * 3.14159265f / 180.0f);
        // The follow camera glides to its place after a placement: the shot waits until it has held still.
        static float lastCam[3] = {0, 0, 0};
        static unsigned steady = 0, waited = 0;
        if (g_shotAt == g_ticks + kShotSettleTicks) // first tick of the stage
            steady = waited = 0;
        if (ramPtr(camH)) {
            const float c[3] = {rdf(rdram, camH + 0x14), rdf(rdram, camH + 0x18), rdf(rdram, camH + 0x1c)};
            const float moved =
                std::fabs(c[0] - lastCam[0]) + std::fabs(c[1] - lastCam[1]) + std::fabs(c[2] - lastCam[2]);
            steady = moved < 12.0f ? steady + 1 : 0;
            std::copy(c, c + 3, lastCam);
        }
        if (g_ticks >= g_shotAt && steady < 60 && waited < 900) {
            ++waited;
            return;
        }
        if (g_ticks >= g_shotAt) {
            if (ramPtr(camH))
                arenaLog("preview shot: camera at %.0f %.0f %.0f (%.0f from the player), steady %u, waited %u",
                         static_cast<double>(lastCam[0]), static_cast<double>(lastCam[1]),
                         static_cast<double>(lastCam[2]),
                         static_cast<double>(std::hypot(lastCam[0] - rdf(rdram, player + 0x14),
                                                        lastCam[2] - rdf(rdram, player + 0x1c))),
                         steady, waited);
            // Only with her standing on the point: a spawn the engine would not take (no floor yet) gives a picture
            // of a fall. Try again a few times, then give up without a file.
            const float dx = rdf(rdram, player + 0x14) - pos[0], dy = rdf(rdram, player + 0x18) - pos[1],
                        dz = rdf(rdram, player + 0x1c) - pos[2];
            if (std::sqrt(dx * dx + dz * dz) > 1500.0f || std::fabs(dy) > 2500.0f) {
                if (++g_shotTries >= 4) {
                    arenaLog("preview shot: the player never settled at the point, no picture");
                    g_shotTries = 0;
                    g_shotAt = 0;
                    g_shotStage = 0;
                    g_shotDone = 1;
                    return;
                }
                g_shotAt = g_ticks + 120;
                teleport(guest, player, pos, -3, yaw);
                return;
            }
            std::string file;
            {
                std::lock_guard<std::mutex> l(g_shotM);
                file = g_shotFile;
            }
            gsr::requestWindowShot(file);
            g_shotAt = g_ticks + kShotWriteTicks;
            g_shotStage = 2;
        }
    } else if (stage == 2 && g_ticks >= g_shotAt) {
        const int s = def.pickSpawn({});
        if (s >= 0)
            teleport(guest, player, def.spawns[static_cast<size_t>(s)].pos, s,
                     spawnYawDeg(def, def.spawns[static_cast<size_t>(s)]));
        g_shotAt = 0;
        g_shotDone = 1;
        g_shotStage = 0;
    }
}

// ---- Character turntables for the co-op character picker (docs/journal/2026-11-01/character-previews.md) ----
// The title menu has no model renderer, so the pictures are made from a loaded level: with DRAKAN_CHAR_PREVIEWS set
// (all, or hex class ids separated by commas) the player stands still, one window shot is taken of the empty spot
// beside her, and each character is spawned as a ghost stand-in there and turned in kStudioFrames steps with a shot per
// step. A thread subtracts the empty shot from each frame (the camera and the world hold still), cuts the character out
// (alpha = how much the pixel changed), and writes one strip PNG per character into <previews>/chars/<id>.png, which
// the menu plays back. Characters with a strip are skipped, so an interrupted run resumes.
constexpr int kStudioFrames = 12;
constexpr unsigned kStudioSettle = 220, kStudioWrite = 14, kStudioTurn = 12;
struct Studio {
    int phase = 0; // 0 waiting, 1 empty shot, 2 spawn, 3 turn, 4 clean up, 5 retry spawn
    bool wanted = false, all = true, done = false;
    std::vector<uint16_t> ids;
    size_t next = 0;
    unsigned ticks = 0, at = 0;
    int ghost = 0, frame = 0;
    uint16_t cur = 0;
    float base[3] = {0, 0, 0}, rot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::vector<std::future<void>> jobs;
} g_studio;

std::string studioDir() {
    std::string d;
    if (const char* e = std::getenv("DRAKAN_PREVIEW_DIR"); e && *e)
        d = e;
    else if (const char* l = std::getenv("LOCALAPPDATA"); l && *l)
        d = std::string(l) + "/Drakan2PC/previews/";
    else if (const char* h = std::getenv("HOME"); h && *h)
        d = std::string(h) + "/.local/share/Drakan2PC/previews/";
    else
        d = drakan_cfg::exeDir() + "previews/captured/";
    if (!d.empty() && d.back() != '/' && d.back() != '\\')
        d += '/';
    return d + "chars/";
}
std::string hex4(unsigned v) {
    char b[16];
    std::snprintf(b, sizeof b, "%04x", v & 0xffffu);
    return b;
}

// Builds <dir>/<id>.png from the empty shot and the frames (worker thread). False without a usable cut.
bool studioAssemble(const std::string& dir, uint16_t id) {
    constexpr int cw = 160, ch = 240;
    gsr::Image bg;
    if (!gsr::readImage((dir + "raw/bg.png").c_str(), bg) || !bg)
        return false;
    std::vector<gsr::Image> fr(kStudioFrames);
    for (int k = 0; k < kStudioFrames; ++k)
        if (!gsr::readImage((dir + "raw/" + hex4(id) + "_" + std::to_string(k) + ".png").c_str(), fr[k]) || !fr[k] ||
            fr[k].width != bg.width || fr[k].height != bg.height)
            return false;
    const unsigned W = bg.width, H = bg.height;
    // Where the stand-in is: the right half of the picture (the player is in the middle).
    const unsigned zx0 = W * 62 / 100, zy0 = H / 10, zy1 = H * 9 / 10;
    const auto diff = [&](const gsr::Image& a, unsigned x, unsigned y) {
        const uint8_t* p = a.rgba.data() + (size_t(y) * W + x) * 4;
        const uint8_t* q = bg.rgba.data() + (size_t(y) * W + x) * 4;
        return std::max({std::abs(int(p[0]) - q[0]), std::abs(int(p[1]) - q[1]), std::abs(int(p[2]) - q[2])});
    };
    unsigned x0 = W, y0 = H, x1 = 0, y1 = 0;
    for (const gsr::Image& f : fr) {
        std::vector<int> col(W, 0), row(H, 0);
        for (unsigned y = zy0; y < zy1; ++y)
            for (unsigned x = zx0; x < W; ++x)
                if (diff(f, x, y) > 40) {
                    ++col[x];
                    ++row[y];
                }
        for (unsigned x = zx0; x < W; ++x)
            if (col[x] >= 4) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
            }
        for (unsigned y = zy0; y < zy1; ++y)
            if (row[y] >= 4) {
                y0 = std::min(y0, y);
                y1 = std::max(y1, y);
            }
    }
    if (x1 <= x0 + 20 || y1 <= y0 + 40)
        return false;
    // A cell of 2 : 3 around the cut, a little air all round.
    double h = (y1 - y0 + 1) * 1.08, w = (x1 - x0 + 1) * 1.08;
    h = std::max(h, w * 1.5);
    w = h / 1.5;
    const double cx = (x0 + x1) * 0.5, cy = (y0 + y1) * 0.5;
    const double sx0 = cx - w * 0.5, sy0 = cy - h * 0.5;
    gsr::Image out;
    out.width = cw * kStudioFrames;
    out.height = ch;
    out.rgba.assign(size_t(out.width) * out.height * 4, 0);
    for (int k = 0; k < kStudioFrames; ++k)
        for (int y = 0; y < ch; ++y)
            for (int x = 0; x < cw; ++x) {
                const int sx = static_cast<int>(sx0 + (x + 0.5) * w / cw),
                          sy = static_cast<int>(sy0 + (y + 0.5) * h / ch);
                if (sx < int(zx0) || sy < 0 || sx >= int(W) || sy >= int(H))
                    continue;
                // 2 x 2 source samples; the alpha is the change against the empty shot
                int a = 0, r = 0, g = 0, b = 0;
                for (int j = 0; j < 2; ++j)
                    for (int i = 0; i < 2; ++i) {
                        const unsigned px = std::min<unsigned>(sx + i, W - 1), py = std::min<unsigned>(sy + j, H - 1);
                        const int d = diff(fr[k], px, py);
                        const int al = std::clamp((d - 24) * 12, 0, 255);
                        const uint8_t* p = fr[k].rgba.data() + (size_t(py) * W + px) * 4;
                        a += al;
                        r += p[0] * al;
                        g += p[1] * al;
                        b += p[2] * al;
                    }
                uint8_t* o = out.rgba.data() + (size_t(y) * out.width + size_t(k) * cw + x) * 4;
                if (a > 0) {
                    o[0] = static_cast<uint8_t>(r / a);
                    o[1] = static_cast<uint8_t>(g / a);
                    o[2] = static_cast<uint8_t>(b / a);
                    o[3] = static_cast<uint8_t>(a / 4);
                }
            }
    std::error_code ec;
    // Holes where the character's colours match the ground behind it: close the cut-out (alpha follows the closed
    // mask).
    for (int k = 0; k < kStudioFrames; ++k) {
        std::vector<uint8_t> m(size_t(cw) * ch), t(size_t(cw) * ch);
        const auto cell = [&](int x, int y) -> uint8_t* {
            return out.rgba.data() + (size_t(y) * out.width + size_t(k) * cw + x) * 4;
        };
        for (int y = 0; y < ch; ++y)
            for (int x = 0; x < cw; ++x)
                m[size_t(y) * cw + x] = cell(x, y)[3] > 24 ? 255 : 0;
        const auto filt = [&](std::vector<uint8_t>& a, int r, bool grow) {
            for (int pass = 0; pass < 2; ++pass) {
                for (int y = 0; y < ch; ++y)
                    for (int x = 0; x < cw; ++x) {
                        uint8_t v = grow ? 0 : 255;
                        for (int d = -r; d <= r; ++d) {
                            const int xx = pass == 0 ? std::clamp(x + d, 0, cw - 1) : x;
                            const int yy = pass == 0 ? y : std::clamp(y + d, 0, ch - 1);
                            const uint8_t u = a[size_t(yy) * cw + xx];
                            v = grow ? std::max(v, u) : std::min(v, u);
                        }
                        t[size_t(y) * cw + x] = v;
                    }
                a = t;
            }
        };
        filt(m, 4, true);
        filt(m, 4, false);
        // Colour of a filled pixel: the nearest cut-out pixel to its left or right (same row).
        for (int y = 0; y < ch; ++y)
            for (int x = 0; x < cw; ++x) {
                uint8_t* o = cell(x, y);
                if (!m[size_t(y) * cw + x] || o[3] > 24)
                    continue;
                for (int d = 1; d < 12; ++d) {
                    const int xs[2] = {x - d, x + d};
                    bool got = false;
                    for (int xx : xs)
                        if (xx >= 0 && xx < cw && cell(xx, y)[3] > 24) {
                            std::copy(cell(xx, y), cell(xx, y) + 3, o);
                            o[3] = 255;
                            got = true;
                            break;
                        }
                    if (got)
                        break;
                }
            }
    }
    const bool ok = gsr::writePng((dir + hex4(id) + ".png").c_str(), out);
    for (int k = 0; k < kStudioFrames; ++k)
        std::filesystem::remove(dir + "raw/" + hex4(id) + "_" + std::to_string(k) + ".png", ec);
    return ok;
}

void studioTick(drakan_input::IGuest& guest, uint8_t* rdram) {
    (void)guest;
    Studio& s = g_studio;
    if (s.done)
        return;
    if (!s.wanted) {
        const char* e = std::getenv("DRAKAN_CHAR_PREVIEWS");
        if (!e || !*e || std::strcmp(e, "0") == 0) {
            s.done = true;
            return;
        }
        s.wanted = true;
        const std::string v = e;
        if (v != "1" && v != "all") {
            s.all = false;
            for (size_t p = 0; p < v.size();) {
                s.ids.push_back(static_cast<uint16_t>(std::strtoul(v.c_str() + p, nullptr, 16)));
                p = v.find(',', p);
                if (p == std::string::npos)
                    break;
                ++p;
            }
        }
    }
    const uint32_t player = controlledActor(rdram);
    if (!ramPtr(player) || !drakan_ghost::ready() || !drakan_ghost::enabled())
        return;
    const std::string dir = studioDir();
    ++s.ticks;
    if (s.phase == 0) {
        if (s.ticks < 900) // the level has settled, the follow camera holds still
            return;
        std::error_code ec;
        std::filesystem::create_directories(dir + "raw", ec);
        if (s.all) {
            const char* mx = std::getenv("DRAKAN_CHAR_PREVIEW_MAX");
            const size_t max = mx ? std::strtoul(mx, nullptr, 10) : 1000;
            for (const auto& e : drakan_coop_character::roster())
                if (s.ids.size() < max)
                    s.ids.push_back(e.id);
        }
        std::erase_if(s.ids, [&](uint16_t id) { return std::filesystem::exists(dir + hex4(id) + ".png"); });
        if (s.ids.empty()) {
            arenaLog("character previews: nothing to do");
            s.done = true;
            return;
        }
        arenaLog("character previews: %zu characters into %s", s.ids.size(), dir.c_str());
        for (int i = 0; i < 3; ++i)
            s.base[i] = rdf(rdram, player + 0x14 + 4u * static_cast<uint32_t>(i));
        for (int i = 0; i < 9; ++i)
            s.rot[i] = rdf(rdram, player + 0x20 + 4u * static_cast<uint32_t>(i));
        // Beside her, to one side of the camera's view of her.
        const uint32_t camH = rd32(rdram, engineCtx(rdram) + 0x4b4);
        if (ramPtr(camH)) {
            float dx = s.base[0] - rdf(rdram, camH + 0x14), dz = s.base[2] - rdf(rdram, camH + 0x1c);
            const float n = std::max(1.0f, std::hypot(dx, dz));
            dx /= n;
            dz /= n;
            const float side = std::getenv("DRAKAN_CHAR_PREVIEW_LEFT") ? 1.0f : -1.0f; // -1: the right of the picture
            s.base[0] += side * dz * 520.0f - dx * 60.0f;
            s.base[2] += side * -dx * 520.0f - dz * 60.0f;
        }
        gsr::requestWindowShot(dir + "raw/bg.png");
        s.at = s.ticks + kStudioWrite + 20;
        s.phase = 1;
        return;
    }
    if (s.phase == 1) {
        if (s.ticks < s.at)
            return;
        s.phase = 2;
    }
    if (s.phase == 2 && s.next >= s.ids.size()) {
        if (s.ticks < s.at)
            return;
        for (auto& j : s.jobs)
            j.wait();
        arenaLog("character previews: finished");
        s.done = true;
        return;
    }
    const auto place = [&](float angle) {
        const float c = std::cos(angle), sn = std::sin(angle);
        float r[9]; // R * Ry(angle), rows of the 3 x 3
        for (int row = 0; row < 3; ++row) {
            const float a = s.rot[row * 3], b = s.rot[row * 3 + 1], d = s.rot[row * 3 + 2];
            r[row * 3] = a * c - d * sn;
            r[row * 3 + 1] = b;
            r[row * 3 + 2] = a * sn + d * c;
        }
        drakan_ghost::pinTransform(s.ghost, s.base, r);
    };
    if (s.phase == 2) {
        s.cur = s.ids[s.next];
        drakan_ghost::Transform t;
        std::copy(s.base, s.base + 3, t.pos);
        std::copy(s.rot, s.rot + 9, t.rot);
        s.ghost = drakan_ghost::spawn(&t, s.cur);
        if (s.ghost == 0) { // refused (not ready yet): try again shortly
            s.at = s.ticks + 30;
            s.phase = 5;
            return;
        }
        s.frame = 0;
        s.at = s.ticks + kStudioSettle;
        s.phase = 3;
        return;
    }
    if (s.phase == 5) {
        if (s.ticks >= s.at)
            s.phase = 2;
        return;
    }
    if (s.phase == 3) {
        place(static_cast<float>(s.frame) * 6.2831853f / kStudioFrames);
        if (s.ticks < s.at)
            return;
        gsr::requestWindowShot(dir + "raw/" + hex4(s.cur) + "_" + std::to_string(s.frame) + ".png");
        ++s.frame;
        s.at = s.ticks + kStudioWrite + kStudioTurn;
        if (s.frame >= kStudioFrames) {
            s.phase = 4;
            s.at = s.ticks + kStudioWrite + 10;
        }
        return;
    }
    if (s.phase == 4) {
        if (s.ticks < s.at)
            return;
        drakan_ghost::destroy(s.ghost);
        const uint16_t id = s.cur;
        s.jobs.push_back(std::async(std::launch::async, [dir, id] {
            const bool ok = studioAssemble(dir, id);
            arenaLog("character previews: %04x %s", id, ok ? "written" : "no usable cut");
        }));
        ++s.next;
        s.at = s.ticks + (s.next >= s.ids.size() ? 120 : 60);
        s.phase = 2;
    }
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
        if (s.take) {
            // The aid takes the present pickup nearest to the player (the server only accepts a claim from within 4000
            // units of the map's position), walking her next to it when none is close: she is put 800 units from it,
            // never on it (the engine would pick it up itself), and the item vanishes once the placement and the
            // server's view of her position have settled.
            if (!s.killAt) {
                int best = -1;
                float bestD = 1e30f;
                for (size_t k = 0; k < g_picks.size(); ++k) {
                    if (g_picks[k].state != PState::Present)
                        continue;
                    const float d = std::hypot(rdf(rdram, player + 0x14) - g_picks[k].def.pos[0],
                                               rdf(rdram, player + 0x1c) - g_picks[k].def.pos[2]);
                    if (d < bestD) {
                        bestD = d;
                        best = static_cast<int>(k);
                    }
                }
                if (best < 0)
                    continue;
                s.pickup = best;
                const Pick& p = g_picks[static_cast<size_t>(best)];
                if (bestD > 3000.0f) {
                    float pos[3] = {p.def.pos[0] + 800.0f, p.def.pos[1] - kSpawnLift + 60.0f, p.def.pos[2]};
                    teleport(guest, player, pos, -2);
                }
                s.killAt = 1;
                s.killTime = std::chrono::steady_clock::now() + std::chrono::seconds(4);
                arenaLog("test: taking pickup %d (%s), the player is %.0f units away", best, p.def.label.c_str(),
                         static_cast<double>(bestD));
                continue;
            }
            const Pick& p = g_picks[static_cast<size_t>(s.pickup)];
            if (g_place.active || std::chrono::steady_clock::now() < s.killTime)
                continue; // real time: the server must have seen the position (state packets)
            if (p.state != PState::Present)
                continue; // taken by something else meanwhile: the claim path runs by itself
            s.done = true;
            arenaShot("pickup" + std::to_string(s.pickup) + "_before");
            arenaLog("test: pickup %d (%s, actor %x) vanishes the way it does when the engine picks it up", s.pickup,
                     p.def.label.c_str(), p.handle);
            killActor(guest, const_cast<uint8_t*>(rdram), p.handle);
            continue;
        }
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
    g_spell = SpellState();
    g_calls.clear();
    g_pendingStow.clear();
    g_removed = 0;
    g_place.active = false;
    g_assignedSpawn = -1;
    g_placeHoldUntil = 0;
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
        m.game = a.game;
        m.deathmatch = a.deathmatch;
        m.teamDeathmatch = a.teamDeathmatch;
        m.maxPlayers = a.maxPlayers;
        m.spawns = static_cast<int>(a.spawns.size());
        m.pickups = static_cast<int>(a.pickups.size());
        m.hasBounds = a.hasBounds;
        std::copy(a.bounds, a.bounds + 4, m.bounds);
        m.hasPreview = a.hasPreview;
        std::copy(a.preview, a.preview + 5, m.preview);
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

size_t addMaps(const std::string& text, const std::string& source, const std::string& defaultGame) {
    loadDefaults();
    std::vector<std::string> errors;
    size_t n;
    {
        std::lock_guard<std::mutex> l(g_catM);
        n = g_catalog.add(text, source, &errors, defaultGame);
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

bool floorHint(float& y) {
    std::lock_guard<std::mutex> l(g_sessM);
    if (!g_active.load() || !g_hasDef || g_def.spawns.empty())
        return false;
    y = 1e30f;
    for (const auto& s : g_def.spawns)
        y = std::min(y, s.pos[1]);
    return true;
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

bool takeQuickSlot() {
    return g_quickRequest.exchange(false);
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
    teleport(guest, player, s.pos, index, spawnYawDeg(def, s));
    g_assignedSpawn = index;
    g_outsideTicks = 0;
    arenaLog("player placed at spawn %d: %.0f %.0f %.0f", index, static_cast<double>(s.pos[0]),
             static_cast<double>(s.pos[1]), static_cast<double>(s.pos[2]));
    armLoadout(rdram, player);
    return true;
}

void requestPreviewShot(const std::string& file) {
    if (file.empty() || g_shotStage.load())
        return;
    {
        std::lock_guard<std::mutex> l(g_shotM);
        g_shotFile = file;
    }
    g_shotAt = 0;
    g_shotDone = 0;
    g_shotStage = 1;
}

std::string charPreviewDir() {
    return studioDir();
}

int previewShotState() {
    return g_shotStage.load() ? 1 : (g_shotDone.load() ? 2 : 0);
}

bool spawnEffect(uint8_t* rdram, uint32_t resId, const float pos[3], const float rot[9], unsigned lifeTicks) {
    const uint32_t idx = findResourceIndex(rdram, resId);
    if (idx == 0xffffffffu || g_spawnQueue.size() > 1)
        return false;
    g_effectLife = lifeTicks ? lifeTicks : 40;
    SpawnReq r;
    r.resIndex = idx;
    r.pickup = kEffectSpawn;
    std::memcpy(r.pos, pos, sizeof r.pos);
    std::memcpy(r.rot, rot, sizeof r.rot);
    g_spawnQueue.push_back(r);
    return true;
}

void install(PS2Runtime& rt) {
    (void)rt;
    if (const char* f = std::getenv("DRAKAN_ARENA_PREVIEW_SHOT"))
        g_envShotFile = f;
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
    for (const char* var : {"DRAKAN_ARENA_TEST_GOTO", "DRAKAN_ARENA_TEST_TAKE"}) {
        const char* t = std::getenv(var);
        if (!t)
            continue;
        for (const char* p = t; *p;) {
            int idx = 0;
            unsigned at = 0;
            if (std::sscanf(p, "%d@%u", &idx, &at) == 2 && idx >= 0)
                g_goto.push_back({idx, at, std::strstr(var, "TAKE") != nullptr});
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
        g_spell = SpellState();
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
    studioTick(guest, rdram);
    if (!g_active.load())
        return;
    const uint32_t player = controlledActor(rdram);
    {
        static bool lastReady = true;
        const bool ready = drakan_ghost::ready();
        if (ready != lastReady) {
            lastReady = ready;
            arenaLog("ghost layer %s (arena ticks %s)", ready ? "ready again" : "not ready",
                     ready ? "resume" : "pause");
        }
    }
    if (!ramPtr(player) || !drakan_ghost::ready())
        return;
    ++g_ticks;
    if (g_filteredGen != g_loadGen) {
        if (g_ticks >= kSettleTicks)
            firstPass(guest, rdram, player);
        return;
    }
    applyPickupEvents(guest, rdram);
    while (!g_effects.empty() && g_ticks >= g_effects.front().dieTick) { // breath flames past their life
        killActor(guest, rdram, g_effects.front().handle);
        g_effects.pop_front();
    }
    const unsigned since = g_ticks - g_passTick;
    if (since < kSweepTicks ? since % 20 == 0 : since % 120 == 0) {
        // A newly created loadout item is briefly a loose world actor until the native stow completes.
        const bool stowing = g_loadoutArmed && g_loadoutPhase < 2;
        if (snapshot(rdram)) {
            const bool sweepLoose = since < kSweepTicks || g_deferredLooseSweep;
            g_removed += removeCampaign(guest, rdram, sweepLoose && !stowing);
            g_deferredLooseSweep = sweepLoose && stowing;
        }
    }
    if (g_dumpWanted && since == kSweepTicks + 120 && snapshot(rdram))
        dumpWorld("after");
    if (std::getenv("DRAKAN_ARENA_RESDUMP") && since == kSweepTicks + 60) { // test aid: the level's resource records
        const uint32_t ctx = engineCtx(rdram);
        const uint32_t res = ramPtr(ctx) ? rd32(rdram, ctx + 0x320) : 0;
        const uint32_t table = ramPtr(res) ? rd32(rdram, res + 8) : 0,
                       count = ramPtr(res) ? rd32(rdram, res + 0x10) : 0;
        arenaLog("resdump: %u records", count);
        for (uint32_t i = 0; ramPtr(table) && i <= count && i < 0x20000u; ++i) {
            const uint32_t r = table + i * 0x24u;
            arenaLog("resdump %u id %x type %u class %x props %x model %x", i, rd32(rdram, r), rd32(rdram, r + 4),
                     rd32(rdram, r + 8), rd32(rdram, r + 0xc), rd32(rdram, r + 0x10));
        }
    }
    if (g_ticks % 3 == 0)
        pickupTick(guest, rdram);
    pumpSpawns(guest, rdram);
    loadoutTick(guest, rdram, player);
    armTick(guest, rdram, player);
    spellTick(guest, rdram, player);
    placementTick(guest, rdram, player);
    gotoTick(guest, rdram, player);
    testFallTick(guest, rdram, player, since);
    testSaveTick(guest, since);
    boundsTick(guest, rdram, player);
    tourTick(guest, rdram, player);
    previewShotTick(guest, rdram, player);
}

} // namespace drakan_arena
