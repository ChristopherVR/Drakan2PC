// Level change from the host (drakan_levelchange.h). See docs/journal/2026-10-31/level-change.md for the mechanism.
#include "drakan_levelchange.h"
#include "drakan_dragon_arrival.h"
#include "drakan_exit_guard.h"
#include "drakan_game.h"
#include "drakan_lifecycle.h"
#include "drakan_mount_arrival.h"
#include "drakan_pause.h"
#include "drakan_region.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"

#if defined(PS2X_HAVE_GSR)
#include "imgui.h"
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <map>
#include <mutex>
#if defined(_WIN32)
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

void ps2xNoPreemptEnter() noexcept;
void ps2xNoPreemptLeave() noexcept;

namespace drakan_levelchange {
namespace {

// SCES-500.06 (identity addresses only; install() refuses other profiles).
constexpr uint32_t kPadMgrUpdate = 0x1e3970u;
constexpr uint32_t kTransition = 0x362d38u;    // the level transition on the main thread
constexpr uint32_t kArrivalLookup = 0x362a60u; // (old level name): the new level's arrival marker, 0 when none
// Where a substituted old-level name is put for the lookup: the speech controller's idle text buffer, clear of the
// level-path rewrite at 0x5b7d20 (drakan_lifecycle) and the scratch words at 0x5b8480 (drakan_pvp / drakan_coop).
constexpr uint32_t kArrivalName = 0x5b7f20u;
constexpr uint32_t kContextPtr = 0x5565d4u;
constexpr uint32_t kExitClass = 0x10037u;    // "Level Transition"
constexpr uint32_t kPendingExit = 0x4fbba4u; // handle of the trigger the main loop is about to process (0 = none)
constexpr uint32_t kBusy = 0x4fb6fcu;        // cleared by the trigger when it arms the transition
constexpr uint32_t kRescanTicks = 30;

struct Ram {
    uint8_t* base;
    static bool ok(uint32_t a, uint32_t n = 4) {
        const uint32_t p = a & 0x1fffffffu;
        return p >= 0x100000u && p < PS2_RAM_SIZE && n <= PS2_RAM_SIZE - p;
    }
    uint32_t u32(uint32_t a) const {
        uint32_t v = 0;
        if (ok(a))
            std::memcpy(&v, base + (a & 0x1fffffffu), 4);
        return v;
    }
    uint32_t u16(uint32_t a) const {
        uint16_t v = 0;
        if (ok(a, 2))
            std::memcpy(&v, base + (a & 0x1fffffffu), 2);
        return v;
    }
    void w32(uint32_t a, uint32_t v) const {
        if (ok(a))
            std::memcpy(base + (a & 0x1fffffffu), &v, 4);
    }
};

uint32_t addr(uint32_t pal) {
    return drakan_region::guestAddress(pal);
}

std::mutex g_m;
std::vector<ExitInfo> g_exits;
std::atomic<bool> g_installed{false}, g_scanWanted{false};
std::atomic<uint32_t> g_fire{0};      // exit handle to fire at the next tick
std::atomic<int64_t> g_fireIndex{-1}; // exit index to fire at the next tick
std::atomic<uint64_t> g_framesInLevel{0};
std::atomic<bool> g_loading{false};
PS2Runtime::RecompiledFunction g_origTick = nullptr;
uint32_t g_scanTicks = 0, g_playerRef = 0;
std::mutex g_wm;
std::function<void(const float*)> g_exitListener; // guarded by g_wm
std::atomic<bool> g_following{false}, g_firedByHost{false};
bool g_haveFollow = false; // guarded by g_wm
float g_followPos[3] = {};
uint32_t g_followTicks = 0;
std::string g_wantLevel;    // level name to reach through a matching exit (guarded by g_wm)
std::string g_currentLevel; // stem of the level the engine reported last

// ---- test aid: DRAKAN_LEVELCHANGE=<index|level name>[@ticks][,repeat] ----
struct Probe {
    bool armed = false;
    std::vector<std::string> steps; // index or level name, one per level change (comma separated in the env var)
    size_t next = 0;
    uint32_t after = 600;
} g_probe;

// Destination names. Property 0 is a level-local string id: 0x80000 + the id of a string of the level's own XDB (the
// dependency named like LEVEL1ON.XDB), whose text is the destination ("Levels\Level 1B - Kragmor\Level 1B -
// Kragmor.lvl"). Resolving it through the string manager from the host blocks on guest semaphores (a nested call froze
// the guest), and the manager's cache only holds what the game has read, so the texts are kept in tables read from the
// disc's XDBs (level names only; journal 2026-10-31/level-change.md section 6). Levels that share an XDB share a group.
// The text "portal" is not a level: such an exit is the arrival marker of a portal pair and the game's own transition
// hangs on it.
struct LevelGroup {
    const char* level;
    const char* group;
};
struct DestText {
    const char* group;
    uint32_t id;
    const char* to;
};
constexpr LevelGroup kLevelGroups[] = {
    {"Level 0 - Intro", "LEVEL0RO"},
    {"Level 1A - Surdana", "LEVEL1ON"},
    {"Level 1B - Kragmor", "LEVEL1ON"},
    {"Level 1C - Catacombs", "LEVEL1ON"},
    {"Level 1D - City of the Damned", "LEVEL1ON"},
    {"Level 1E - Inquisitor's Lair", "LEVEL1ON"},
    {"Level 2 - Shadowmire", "LEVEL2RE"},
    {"Level 3A - Andrellian Isles", "LEVEL3ON"},
    {"Level 3B - Crystal Mine", "LEVEL3ON"},
    {"Level 4A - Ravenshold", "LEVEL4ON"},
    {"Level 4B - Northern Tundra", "LEVEL4ON"},
    {"Level 5 - Shiverbane", "LEVEL5NE"},
    {"Level 6 - Valley of the Fallen", "LEVEL6EN"},
    {"Level 8 - Stratos", "LEVEL8OS"},
};
constexpr DestText kDestTexts[] = {
    {"LEVEL0RO", 0x80001, "Level 1A - Surdana"},
    {"LEVEL1ON", 0x80001, "Level 2 - Shadowmire"},
    {"LEVEL1ON", 0x80005, "Level 2 - Shadowmire"},
    {"LEVEL1ON", 0x80006, "Level 3A - Andrellian Isles"},
    {"LEVEL1ON", 0x80007, "Level 6 - Valley of the Fallen"},
    {"LEVEL1ON", 0x80008, "Level 4A - Ravenshold"},
    {"LEVEL1ON", 0x80009, "Level 5 - Shiverbane"},
    {"LEVEL1ON", 0x8000a, "Level 8 - Stratos"},
    {"LEVEL1ON", 0x8000c, "Level 1A - Surdana"},
    {"LEVEL1ON", 0x8000d, "Level 1B - Kragmor"},
    {"LEVEL1ON", 0x8000e, "Level 1C - Catacombs"},
    {"LEVEL1ON", 0x8000f, "Level 1D - City of the Damned"},
    {"LEVEL1ON", 0x80010, "Level 1E - Inquisitor's Lair"},
    {"LEVEL1ON", 0x80011, "portal"},
    {"LEVEL2RE", 0x80004, "Level 1A - Surdana"},
    {"LEVEL3ON", 0x80004, "Level 1 - Aachen (A)"},
    {"LEVEL3ON", 0x80005, "Level 4A - Ravenshold"},
    {"LEVEL3ON", 0x80006, "Level 3B - Crystal Mine"},
    {"LEVEL3ON", 0x80009, "Level 3A - Andrellian Isles"},
    {"LEVEL4ON", 0x80009, "portal"},
    {"LEVEL4ON", 0x8000a, "Level 4 Portal"},
    {"LEVEL4ON", 0x8000b, "Level 1A - Surdana"},
    {"LEVEL4ON", 0x8000c, "Level 3A - Andrellian Isles"},
    {"LEVEL4ON", 0x8000d, "Level 4B - Northern Tundra"},
    {"LEVEL4ON", 0x8000e, "Level 5 - Shiverbane"},
    {"LEVEL4ON", 0x80010, "Level 4A - Ravenshold"},
    {"LEVEL5NE", 0x80005, "Level 1A - Surdana"},
    {"LEVEL5NE", 0x80006, "portal"},
    {"LEVEL5NE", 0x80007, "Level 4B - Northern Tundra"},
    {"LEVEL6EN", 0x80001, "ruined village"},
    {"LEVEL6EN", 0x80002, "mines"},
    {"LEVEL6EN", 0x80004, "Level 1A - Surdana"},
    {"LEVEL6EN", 0x80005, "portal"},
    {"LEVEL6EN", 0x80006, "Level 8 - Stratos"},
    {"LEVEL8OS", 0x80004, "Level 6 - Transition to Desert"},
    {"LEVEL8OS", 0x80005, "Out"},
};
// Destinations learned in this run: (level, string id) -> level the exit led to (set when a level change through an
// exit completes), so exits whose text the string manager has not cached yet get a name after they were used once.
std::map<std::pair<std::string, uint32_t>, std::string> g_learned;   // guarded by g_m
std::function<std::string(const std::string&, uint32_t)> g_destHook; // guarded by g_m
std::map<std::string, std::string>
    g_copies;                               // level copy -> source level (mod packs, registerLevelCopy), guarded by g_m
std::pair<std::string, uint32_t> g_fireKey; // exit being taken (level, string id), EE thread
std::string levelNameOf(std::string t);
std::string knownName(const std::string& level, uint32_t id) {
    {
        std::lock_guard<std::mutex> l(g_m);
        if (const auto it = g_learned.find({level, id}); it != g_learned.end())
            return it->second;
    }
    std::string src = level; // a mod pack's level copy shares the destination strings of the level it was copied from
    {
        std::lock_guard<std::mutex> l(g_m);
        if (const auto it = g_copies.find(level); it != g_copies.end())
            src = it->second;
    }
    for (const LevelGroup& g : kLevelGroups)
        if (src == g.level) {
            // A mod pack's override of the destination string beats the disc's text in the table below.
            std::function<std::string(const std::string&, uint32_t)> hook;
            {
                std::lock_guard<std::mutex> l(g_m);
                hook = g_destHook;
            }
            if (hook)
                if (std::string t = hook(g.group, id); !t.empty())
                    return levelNameOf(t);
            for (const DestText& d : kDestTexts)
                if (id == d.id && std::strcmp(d.group, g.group) == 0)
                    return d.to;
        }
    return {};
}

// The level-local text of a destination is sometimes the level's name ("Level 1B - Kragmor") and sometimes its file
// path ("Levels\\Level 1A - Surdana\\Level 1A - Surdana.lvl"): reduce both to the name.
std::string levelNameOf(std::string t) {
    if (const size_t slash = t.find_last_of("\\/"); slash != std::string::npos)
        t.erase(0, slash + 1);
    if (t.size() > 4 && t.compare(t.size() - 4, 4, ".lvl") == 0)
        t.resize(t.size() - 4);
    return t;
}

bool isPortalName(const std::string& name) {
    return name.size() == 6 && strncasecmp(name.c_str(), "portal", 6) == 0;
}

// Destination texts of exits whose level is not on the disc (cut content): Stratos' "Transition to Desert". The game's
// load fails (result -4) and the title screen comes up with a "Save Failed!" box, so the host does not fire them.
bool isMissingLevel(const std::string& name) {
    return name == "Level 6 - Transition to Desert";
}

uint32_t callGuest(uint8_t* rdram, R5900Context* c, PS2Runtime* rt, uint32_t fn, uint32_t a0, uint32_t a1 = 0,
                   uint32_t a2 = 0) {
    drakan_lifecycle::Event e;
    e.rdram = rdram;
    e.runtime = rt;
    e.ctx = c;
    return drakan_lifecycle::callGuest(e, fn, a0, a1, a2);
}

// Property `index` of a handle's property block: the pointer to its u32 value (0 when unavailable). Same walk as the
// exit trigger's own code: block = *(handle+0x54), vtable = *(block+8), accessor = vtable[+0x1c], this = block +
// s16(vtable+0x18).
uint32_t propertyPtr(uint8_t* rdram, R5900Context* c, PS2Runtime* rt, uint32_t handle, uint32_t index) {
    const Ram r{rdram};
    const uint32_t block = r.u32(handle + 0x54);
    if (!Ram::ok(block, 12))
        return 0;
    const uint32_t vt = r.u32(block + 8);
    if (!Ram::ok(vt, 0x20))
        return 0;
    const uint32_t fn = r.u32(vt + 0x1c);
    if (!fn || !rt->hasFunction(fn))
        return 0;
    const int16_t adj = static_cast<int16_t>(r.u16(vt + 0x18));
    return callGuest(rdram, c, rt, fn, block + static_cast<uint32_t>(adj), index, 0xffffffffu);
}

// Destination text of a string id, read from the string manager's cache without calling it (a nested call of the
// accessor 0x1b9fb0 froze the guest: on a miss it loads the string and waits on semaphores). The manager (*(ctx+0x344),
// this = sm + s16(vt+0x90)) keeps entries in 128 hash buckets at this+0x20 (bucket (id * 0x59) & 0x7f, chain through
// entry+0x14, entry+0 = id, entry+8 & 3 = state, 3 = text pointer at entry+0x1c, 1 = placeholder). Empty when the
// string is not cached (the game has not read it yet).
std::string readString(uint8_t* rdram, uint32_t id) {
    const Ram r{rdram};
    const uint32_t eng = r.u32(addr(kContextPtr));
    const uint32_t sm = Ram::ok(eng, 0x350) ? r.u32(eng + 0x344) : 0;
    const uint32_t vt = Ram::ok(sm, 4) ? r.u32(sm) : 0;
    if (!Ram::ok(vt, 0x98))
        return {};
    const uint32_t self = sm + static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(r.u16(vt + 0x90))));
    uint32_t e = r.u32(self + 0x20 + ((id * 0x59u) & 0x7fu) * 4u);
    for (int guard = 0; guard < 64 && Ram::ok(e, 0x20) && r.u32(e) != id; ++guard)
        e = r.u32(e + 0x14);
    if (!Ram::ok(e, 0x20) || r.u32(e) != id || (r.u32(e + 8) & 3u) != 3u)
        return {};
    const uint32_t t = r.u32(e + 0x1c);
    if (!Ram::ok(t, 2))
        return {};
    const char* text = reinterpret_cast<const char*>(rdram + (t & 0x1fffffffu));
    const size_t room = std::min<size_t>(0x100, PS2_RAM_SIZE - (t & 0x1fffffffu));
    return std::string(text, strnlen(text, room));
}

uint32_t classOf(const Ram& r, uint32_t engine, uint32_t handle) {
    const uint32_t resources = r.u32(engine + 0x320);
    if (!Ram::ok(resources, 0x14))
        return 0;
    const uint32_t index = r.u16(handle + 0x10);
    if (index == 0xffffu || index > r.u32(resources + 0x10))
        return 0;
    const uint32_t table = r.u32(resources + 8);
    if (!Ram::ok(table, index * 0x24u + 0x24u))
        return 0;
    return r.u32(table + index * 0x24u + 8);
}

float f32(const Ram& r, uint32_t a) {
    const uint32_t v = r.u32(a);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

void scan(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const Ram r{rdram};
    std::vector<ExitInfo> out;
    const uint32_t eng = r.u32(addr(kContextPtr));
    if (Ram::ok(eng, 0x4b8)) {
        const uint32_t table = r.u32(eng + 0x7c), top = r.u32(eng + 0x84);
        const uint32_t player = r.u32(eng + 0x4b0);
        for (uint32_t i = 0; i < top + 1 && i < 0x4000; ++i) {
            const uint32_t h = r.u32(table + i * 8);
            if (!Ram::ok(h, 0x120) || r.u32(h) != addr(0x51b8b0u) || r.u16(h + 0x12) != i + 1)
                continue;
            if (classOf(r, eng, h) != kExitClass)
                continue;
            ExitInfo e;
            e.handle = h;
            e.ref = i + 1;
            for (int k = 0; k < 3; ++k)
                e.pos[k] = f32(r, h + 0x14 + 4u * k);
            for (uint32_t k = 0; k < 8; ++k) {
                const uint32_t p = propertyPtr(rdram, c, rt, h, k);
                e.prop[k] = Ram::ok(p) ? r.u32(p) : 0xdeadbeefu;
            }
            e.dest = e.prop[0];
            // The live text wins: a mod pack may have pointed the exit at a level copy, the tables describe the disc.
            e.destName = levelNameOf(readString(rdram, e.prop[0]));
            if (e.destName.empty())
                e.destName = knownName(g_currentLevel, e.prop[0]);
            e.direct = e.prop[6] != 0 && e.prop[6] != 0xdeadbeefu;
            e.portal = isPortalName(e.destName);
            {
                const uint32_t inner = r.u32(h + 0xfc);
                const uint32_t inst = Ram::ok(inner, 0x14) ? r.u32(inner + 0x10) : 0;
                e.state = Ram::ok(inst, 4) ? r.u16(inst) : 0xffffu;
                e.handleFlags = r.u32(h + 4);
            }
            out.push_back(e);
        }
        if (player && Ram::ok(player, 0x20)) { // nearest first
            const float px = f32(r, player + 0x14), py = f32(r, player + 0x18), pz = f32(r, player + 0x1c);
            std::sort(out.begin(), out.end(), [&](const ExitInfo& a, const ExitInfo& b) {
                auto d = [&](const ExitInfo& e) {
                    const float dx = e.pos[0] - px, dy = e.pos[1] - py, dz = e.pos[2] - pz;
                    return dx * dx + dy * dy + dz * dz;
                };
                return d(a) < d(b);
            });
        }
    }
    std::lock_guard<std::mutex> l(g_m);
    g_exits = std::move(out);
}

void setPos(const Ram& r, uint32_t o, const float* p) {
    for (uint32_t k = 0; k < 3; ++k) {
        uint32_t bits;
        std::memcpy(&bits, &p[k], 4);
        r.w32(o + 0x14 + 4 * k, bits);
    }
}

// Camera offset from the player at the time an exit was fired (the transition keeps it, see transitionEntry()).
float g_camOff[3] = {};
PS2Runtime::RecompiledFunction g_origTransition = nullptr;

// Arrival out of a level copy. The lookup 0x362a60 picks the new level's arrival marker whose destination text equals
// the old level's name. A copy is known to the engine by its own title ("Surdana Copy"), but the levels it leads to
// still name its source ("Level 1A - Surdana") in their way back, so nothing matched and the player landed at the
// fallback point (docs/journal/2026-11-02/level-copy-direct-play.md). At the lookup's entry: when the old level is a
// registered copy, no exit of the new level names the copy and one names its source, the lookup is given the source's
// name. An author who pointed the destination's way back at the copy itself keeps that marker. The decision is made
// before the call (the lookup is resumable, a retry after it returned could not be wrapped safely).
PS2Runtime::RecompiledFunction g_origArrival = nullptr;
void arrivalEntry(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == addr(kArrivalLookup)) {
        const uint32_t a0 = getRegU32(c, 4);
        std::string old;
        if (Ram::ok(a0, 1)) {
            const char* text = reinterpret_cast<const char*>(rdram + (a0 & 0x1fffffffu));
            old.assign(text, strnlen(text, std::min<size_t>(0x100, PS2_RAM_SIZE - (a0 & 0x1fffffffu))));
        }
        static const bool trace = std::getenv("DRAKAN_LEVELCHANGE_TRACE") != nullptr;
        if (trace)
            std::fprintf(stderr, "[levelchange] arrival lookup for '%s'\n", old.c_str());
        // The argument is the old level's path as the engine keeps it, lower case
        // ("cdrom0:/english/levels/surdana copy/surdana copy.lvl"): its file stem names the level.
        const auto lower = [](std::string t) {
            for (char& ch : t)
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            return t;
        };
        const size_t slash = old.find_last_of("/\\");
        std::string stem = slash == std::string::npos ? old : old.substr(slash + 1);
        if (const size_t dot = stem.rfind('.'); dot != std::string::npos)
            stem.resize(dot);
        std::string copy, source;
        {
            std::lock_guard<std::mutex> l(g_m);
            for (const auto& [k, v] : g_copies)
                if (lower(k) == lower(stem)) {
                    copy = k;
                    source = v;
                }
        }
        if (!source.empty()) {
            scan(rdram, c, rt); // the new level's exits (Level_Load has returned, g_currentLevel is the new level)
            bool copyNamed = false, sourceNamed = false;
            {
                std::lock_guard<std::mutex> l(g_m);
                for (const ExitInfo& e : g_exits) {
                    copyNamed = copyNamed || lower(e.destName) == lower(copy);
                    sourceNamed = sourceNamed || lower(e.destName) == lower(source);
                }
            }
            // Same path with the source's name for the copy's (folder and file), lower case like the engine's own.
            std::string path;
            const std::string from = lower(copy), to = lower(source), low = lower(old);
            for (size_t i = 0; i < low.size();) {
                if (low.compare(i, from.size(), from) == 0) {
                    path += to;
                    i += from.size();
                } else
                    path += low[i++];
            }
            if (!copyNamed && sourceNamed && path.size() < 0x100) {
                std::memcpy(rdram + addr(kArrivalName), path.c_str(), path.size() + 1);
                SET_GPR_U32(c, 4, addr(kArrivalName));
                std::fprintf(stderr, "[levelchange] arrival from copy '%s': using the marker of its source '%s'\n",
                             copy.c_str(), source.c_str());
            } else if (trace)
                std::fprintf(stderr, "[levelchange] arrival from copy '%s': kept (copy named %d, source named %d)\n",
                             copy.c_str(), copyNamed ? 1 : 0, sourceNamed ? 1 : 0);
        }
    }
    g_origArrival(rdram, c, rt);
}

// Rynn on foot takes an exit while her dragon is parked far away (Surdana's end-of-level stage): the transition would
// carry him that far from her into the next level (drakan_dragon_arrival.h). Put him beside her before it reads him.
// Rynn's dragon: her data (*(*(handle + 0xfc) + 0x10)) + 0x4d8 holds his registry reference (0x3d2658).
// Only for the disc game's own levels: a level of a campaign pack or an editor level may park the dragon far away on
// purpose, so those keep the author's placement (the 100000 unit stray rule in drakan_dragon_arrival.h is a heuristic
// for the base game's Surdana stage). The target is not collision checked (no cheap query exists at this point); the
// dragon is put 3000 units above Rynn so he drops onto open ground rather than being embedded beside a wall.
void bringDragon(const Ram& r, uint32_t eng) {
    if (!Ram::ok(eng, 0x4b8) || std::getenv("DRAKAN_LEVELCHANGE_KEEP_DRAGON"))
        return;
    if (drakan_game::isCampaignPackLevel(g_currentLevel))
        return;
    const uint32_t pl = r.u32(eng + 0x4b0);
    if (!Ram::ok(pl, 0x100) || classOf(r, eng, pl) != 0x10009u)
        return;
    const uint32_t inner = r.u32(pl + 0xfc);
    const uint32_t data = Ram::ok(inner, 0x14) ? r.u32(inner + 0x10) : 0;
    if (!Ram::ok(data, 0x4e0))
        return;
    const uint32_t ref = r.u16(data + 0x4d8), table = r.u32(eng + 0x7c);
    if (!ref || ref > r.u32(eng + 0x84) + 1 || !Ram::ok(table, ref * 8))
        return;
    const uint32_t dh = r.u32(table + (ref - 1) * 8);
    if (!Ram::ok(dh, 0x30) || r.u32(dh) != addr(0x51b8b0u) || classOf(r, eng, dh) != 0x1001cu)
        return;
    const float rynn[3] = {f32(r, pl + 0x14), f32(r, pl + 0x18), f32(r, pl + 0x1c)};
    const float dragon[3] = {f32(r, dh + 0x14), f32(r, dh + 0x18), f32(r, dh + 0x1c)};
    const drakan_dragon_arrival::Placement p = drakan_dragon_arrival::decide(true, rynn, dragon);
    if (!p.moved)
        return;
    setPos(r, dh, p.pos);
    std::fprintf(stderr, "[levelchange] dragon was parked at %.0f %.0f %.0f, put beside Rynn at %.0f %.0f %.0f\n",
                 static_cast<double>(dragon[0]), static_cast<double>(dragon[1]), static_cast<double>(dragon[2]),
                 static_cast<double>(p.pos[0]), static_cast<double>(p.pos[1]), static_cast<double>(p.pos[2]));
}

// Wrapper of the transition function 0x362d38 (runs on the main thread when the main loop sees the pending trigger).
// The transition keeps the player's offset from the trigger ("Maintain Player Position") and places the respawned
// player at the destination's arrival marker plus that offset, so for a fired exit the player has to stand on the
// trigger. The position at +0x14 is overwritten from the physics copies every frame (a write at the frame boundary is
// gone when the main loop gets here, and the player ends up wherever the engine threw it), so it is written here, at
// the entry, where the function reads it a few calls later.
void transitionEntry(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const Ram r{rdram};
    const uint32_t eng = r.u32(addr(kContextPtr));
    const uint32_t h = r.u32(addr(kPendingExit));
    if (g_firedByHost.exchange(false) && Ram::ok(eng, 0x4b8) && Ram::ok(h, 0x20) &&
        !std::getenv("DRAKAN_LEVELCHANGE_NOMOVE")) {
        const uint32_t pl = r.u32(eng + 0x4b0), cam = r.u32(eng + 0x4b4);
        float t[3], cp[3];
        for (uint32_t k = 0; k < 3; ++k) {
            t[k] = f32(r, h + 0x14 + 4 * k);
            cp[k] = t[k] + g_camOff[k];
        }
        std::fprintf(
            stderr, "[levelchange] transition entry: player was at %.0f %.0f %.0f, put on the trigger %.0f %.0f %.0f\n",
            static_cast<double>(f32(r, pl + 0x14)), static_cast<double>(f32(r, pl + 0x18)),
            static_cast<double>(f32(r, pl + 0x1c)), static_cast<double>(t[0]), static_cast<double>(t[1]),
            static_cast<double>(t[2]));
        if (Ram::ok(pl, 0x60))
            setPos(r, pl, t);
        if (Ram::ok(cam, 0x20))
            setPos(r, cam, cp);
    }
    bringDragon(r, eng);
    if (Ram::ok(h, 0x20) && r.u32(h) == addr(0x51b8b0u)) {
        std::lock_guard<std::mutex> l(g_m);
        for (const ExitInfo& e : g_exits)
            if (e.handle == h)
                g_fireKey = {g_currentLevel, e.dest};
    }
    g_origTransition(rdram, c, rt);
}

// Rynn's respawn in the new level (0x362310, called by the transition for Rynn and then for her dragon): a mounted
// arrival in a level that gives no dragon hands control to Rynn (drakan_mount_arrival.h). At the Rynn call (return
// address kRiderRespawnRet) the transition holds the dragon it will respawn next in s7 (0 = none) and its words
// "Rynn controlled" / "dragon controlled" at sp + kRiderWord / kDragonWord; the respawn takes the Rynn word as its
// 7th argument (t2). The words are re-read by the rest of the transition (Rynn's input setup, the camera placement), so
// they are rewritten together with the argument. Decided at the entry only (the function is resumable).
constexpr uint32_t kRespawn = 0x362310u;
constexpr uint32_t kRiderRespawnRet = 0x363decu;
constexpr uint32_t kRiderWord = 0xbfcu;  // sp+3068 in 0x362d38
constexpr uint32_t kDragonWord = 0xc1cu; // sp+3100
// Rynn's input setup 0x44d6c8, called by the transition for her when she is controlled (return address
// kRiderInputRet). Her carried state is still the rider's: data+0x94 bit 0 is set on the actor that hands control
// over (0x44d3b0, the mount) and the word is cleared by the one that takes it (0x456de0, the end of her dismount,
// and 0x44d3b0 for the new controlled actor). A rider who keeps it neither moves nor reads the pad, so after a foot
// arrival it is cleared here the way 0x456de0 does.
constexpr uint32_t kRiderInput = 0x44d6c8u;
constexpr uint32_t kRiderInputRet = 0x3640b8u;
constexpr uint32_t kControlWord = 0x94u;
bool g_footArrival = false; // EE thread (the transition)
PS2Runtime::RecompiledFunction g_origRespawn = nullptr, g_origRiderInput = nullptr;
void riderInputEntry(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == addr(kRiderInput) && g_footArrival && getRegU32(c, 31) == addr(kRiderInputRet)) {
        g_footArrival = false;
        const Ram r{rdram};
        const uint32_t data = getRegU32(c, 4);
        if (Ram::ok(data, kControlWord + 4)) {
            std::fprintf(stderr, "[levelchange] Rynn takes control on foot (control word %x cleared)\n",
                         r.u32(data + kControlWord));
            r.w32(data + kControlWord, 0);
        }
    }
    g_origRiderInput(rdram, c, rt);
}
void respawnEntry(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == addr(kRespawn) && getRegU32(c, 31) == addr(kRiderRespawnRet)) {
        const Ram r{rdram};
        const uint32_t sp = getRegU32(c, 29);
        const bool rider = getRegU32(c, 10) != 0, dragonFollows = getRegU32(c, 23) != 0;
        const drakan_mount_arrival::Control ctl = drakan_mount_arrival::decide(rider, dragonFollows);
        if (ctl.changed && Ram::ok(sp + kDragonWord)) {
            SET_GPR_U32(c, 10, ctl.rider ? 1u : 0u);
            r.w32(sp + kRiderWord, ctl.rider ? 1u : 0u);
            r.w32(sp + kDragonWord, ctl.dragon ? 1u : 0u);
            g_footArrival = true;
            std::fprintf(stderr,
                         "[levelchange] arrived on a mount in a level without a dragon: Rynn arrives on foot\n");
        } else if (std::getenv("DRAKAN_LEVELCHANGE_TRACE"))
            std::fprintf(stderr, "[levelchange] rider respawn: rider controlled %d, dragon follows %d\n", rider ? 1 : 0,
                         dragonFollows ? 1 : 0);
    }
    g_origRespawn(rdram, c, rt);
}

// Trace of what the engine delivers to the exit triggers (DRAKAN_LEVELCHANGE_TRACE=1): the touch handler 0x361440
// (inst, msg; event id = u16 at msg+4) and the class handler 0x365138 (handle, msg), to see whether a walk reaches
// them.
constexpr uint32_t kTouchHandler = 0x361440u;
constexpr uint32_t kExitHandler = 0x365138u;
PS2Runtime::RecompiledFunction g_origTouch = nullptr, g_origExitHandler = nullptr;
std::atomic<int> g_traceCount{0};
void touchEntry(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const Ram r{rdram};
    const uint32_t inst = getRegU32(c, 4), msg = getRegU32(c, 5);
    if (g_traceCount.fetch_add(1) < 400) {
        // 0x361440: the event id is the u16 behind the pointer at msg+4 (0x1001 touch, 0x1002 / 0x10a3 set state bits);
        // a touch queues the exit unless the game mode (0x4fbac8+0x70) is 1 or 2 or the exit handle's flags (+4) have
        // bit 2 or 3 set (already queued).
        const uint32_t handle = r.u32(inst + 0x28);
        std::fprintf(
            stderr, "[levelchange] trace touch handler inst %x msg %x event %x state %x handle %x flags %x mode %x\n",
            inst, msg, r.u16(r.u32(msg + 4)), r.u16(inst), handle, r.u32(handle + 4), r.u32(addr(0x4fbac8u) + 0x70));
    }
    g_origTouch(rdram, c, rt);
}
bool fire(uint8_t* rdram, PS2Runtime* rt, uint32_t handle);
std::atomic<bool> g_traceHandlers{false};
// The exit class handler (0x365138). Message 0x20 is the exit's "touched" message. In the Order of the Flame campaign
// the converter puts a Trigger Detector box on every exit, so a player who walks in is within a few cells of it; the
// same message arriving while she is far away comes from something else linked to the exit, in practice the Timer that
// ends a cut-scene level (the Intro and the Ruined Village Book have no player to walk anywhere:
// docs/journal/2026-11-01/ ootf-campaign-walkthrough.md). The engine's own touch handler ignores it (no toucher), so
// take the exit the way the host-fired aid does: straight to the transition, no save prompt.
// Only an event other than the touch (0x1001) is taken this way: a detector sends ON, which the engine's touch handler
// turns into the normal queued change with its save prompt; a Timer sends 5, which that handler only notes.
constexpr float kRemoteExitDist = 8000.0f;
constexpr uint32_t kTouchEvent = 0x1001u;
constexpr int kLoadingScreen = 14; // gui screen of the loading screen (level load, level change, load game)
// Converted levels: no exit touch before the player has walked off her start / arrival spot (drakan_exit_guard.h).
std::mutex g_guardMutex;
drakan_exit_guard::ArrivalGuard g_guard;
void exitHandlerEntry(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const Ram r{rdram};
    const uint32_t h = getRegU32(c, 4), msg = getRegU32(c, 5);
    if (g_traceHandlers.load() && g_traceCount.fetch_add(1) < 400)
        std::fprintf(stderr, "[levelchange] trace exit handler handle %x msg %x id %x\n", h, msg, r.u16(msg + 4));
    // 0x365138 passes the engine's current message (ctx+0x4e0) to the touch handler; its event id is the u16 behind +4.
    const uint32_t ctxp = r.u32(addr(kContextPtr));
    const uint32_t event = msg == 0x20 && Ram::ok(ctxp, 0x4e4) ? r.u16(r.u32(r.u32(ctxp + 0x4e0) + 4)) : 0;
    if (event == kTouchEvent) {
        bool blocked;
        {
            std::lock_guard<std::mutex> l(g_guardMutex);
            blocked = g_guard.blocks();
        }
        if (blocked) {
            static std::atomic<int> told{0};
            if (told.fetch_add(1) < 20)
                std::fprintf(stderr,
                             "[levelchange] exit %x touched before the player walked off her arrival spot: ignored\n",
                             h);
            return;
        }
    }
    if (msg == 0x20 && event != kTouchEvent && drakan_game::current() == drakan_game::Game::Ootf && Ram::ok(h, 0x20)) {
        const uint32_t eng = r.u32(addr(kContextPtr));
        const uint32_t pl = Ram::ok(eng, 0x4b8) ? r.u32(eng + 0x4b0) : 0;
        if (Ram::ok(pl, 0x20)) {
            const float dx = f32(r, pl + 0x14) - f32(r, h + 0x14), dz = f32(r, pl + 0x1c) - f32(r, h + 0x1c);
            const float dist = std::sqrt(dx * dx + dz * dz);
            // The Ruined Village Book's player stands beside its exit (the PC level has her there), so the end-of-scene
            // Timer's message arrives with her a few hundred units away; the level has nothing else to do, take it.
            const bool bookExit = g_currentLevel.find("Ruined Village Book") != std::string::npos;
            if (dist > kRemoteExitDist || bookExit) {
                std::fprintf(stderr,
                             "[levelchange] exit %x got its touch message %.0f units from the player: taking it\n", h,
                             static_cast<double>(dist));
                fire(rdram, rt, h);
            }
        }
    }
    g_origExitHandler(rdram, c, rt);
}

bool fire(uint8_t* rdram, PS2Runtime* rt, uint32_t handle) {
    const Ram r{rdram};
    const uint32_t eng = r.u32(addr(kContextPtr));
    if (!Ram::ok(eng, 0x4b8) || drakan_lifecycle::transitionActive())
        return false;
    {
        std::lock_guard<std::mutex> l(g_m);
        for (const ExitInfo& e : g_exits) {
            if (e.handle == handle && isMissingLevel(e.destName)) {
                std::fprintf(stderr,
                             "[levelchange] exit %x leads to '%s', a level that is not on the disc (the game's own "
                             "load fails and drops to the title screen with \"Save Failed!\"): not fired\n",
                             handle, e.destName.c_str());
                return false;
            }
            if (e.handle == handle && e.portal) {
                std::fprintf(stderr,
                             "[levelchange] exit %x leads to '%s', an arrival marker and not a level (the game's "
                             "transition hangs on it): not fired\n",
                             handle, e.destName.c_str());
                return false;
            }
        }
    }
    const uint32_t inner = r.u32(handle + 0xfc);
    const uint32_t inst = Ram::ok(inner, 0x14) ? r.u32(inner + 0x10) : 0;
    if (!Ram::ok(inst, 0x30)) { // the class instance data: what the trigger's own handlers work on
        std::fprintf(stderr, "[levelchange] handle %x has no instance data\n", handle);
        return false;
    }
    {
        std::string m;
        char tmp[32];
        for (uint32_t k = 0; k < 13; ++k) {
            std::snprintf(tmp, sizeof tmp, " %.3f", static_cast<double>(f32(r, handle + 0x14 + 4 * k)));
            m += tmp;
        }
        std::fprintf(stderr, "[levelchange] firing exit %x block:%s\n", handle, m.c_str());
    }
    {
        const uint32_t pl = r.u32(eng + 0x4b0), cam = r.u32(eng + 0x4b4);
        for (uint32_t k = 0; k < 3; ++k)
            g_camOff[k] =
                Ram::ok(cam, 0x20) && Ram::ok(pl, 0x20) ? f32(r, cam + 0x14 + 4 * k) - f32(r, pl + 0x14 + 4 * k) : 0.f;
        g_firedByHost = true;
    }
    // The trigger's own state change (0x361440, branch of an exit with property 6 set, which the other branch reaches
    // after its save screen): the main loop's per-frame function sees the pending handle and runs the whole transition
    // on the main thread. The save-slot name stays empty, so the level-change save lands in the card's root.
    r.w32(addr(kBusy), 0);
    r.w32(addr(kPendingExit), handle);
    return true;
}

void onLevelEvent(const drakan_lifecycle::Event& e) {
    // The level-change save ("<level>.rlt", written first) is the moment the pending trigger handle is still set.
    const bool rlt = e.kind == drakan_lifecycle::Kind::BeforeSaveWrite && e.path.size() > 4 &&
                     e.path.compare(e.path.size() - 4, 4, ".rlt") == 0;
    if (rlt) {
        // A level change through an exit (walked into or fired here) has the trigger's handle in the pending slot.
        if (!g_following.load() && e.rdram) {
            const Ram r{e.rdram};
            const uint32_t h = r.u32(addr(kPendingExit));
            if (Ram::ok(h, 0x20) && r.u32(h) == addr(0x51b8b0u)) {
                const float pos[3] = {f32(r, h + 0x14), f32(r, h + 0x18), f32(r, h + 0x1c)};
                std::function<void(const float*)> cb;
                {
                    std::lock_guard<std::mutex> l(g_wm);
                    cb = g_exitListener;
                }
                {
                    const uint32_t engp = r.u32(addr(kContextPtr)), pl = r.u32(engp + 0x4b0);
                    std::fprintf(stderr, "[levelchange] at the save the player %08x is at %.0f %.0f %.0f body %08x\n",
                                 pl, static_cast<double>(f32(r, pl + 0x14)), static_cast<double>(f32(r, pl + 0x18)),
                                 static_cast<double>(f32(r, pl + 0x1c)), r.u32(pl + 0x50));
                }
                std::fprintf(stderr, "[levelchange] exit taken in '%s' at %.0f %.0f %.0f\n", e.level.c_str(),
                             static_cast<double>(pos[0]), static_cast<double>(pos[1]), static_cast<double>(pos[2]));
                if (cb)
                    cb(pos);
            }
        }
    } else if (e.kind == drakan_lifecycle::Kind::BeforeLevelLoad) {
        g_loading = true;
        g_framesInLevel = 0;
        std::lock_guard<std::mutex> l(g_m);
        g_exits.clear();
    } else if (e.kind == drakan_lifecycle::Kind::AfterLevelLoad) {
        g_loading = false;
        if (!g_fireKey.first.empty() && g_fireKey.first != e.level) {
            std::lock_guard<std::mutex> l(g_m);
            g_learned[g_fireKey] = e.level;
        }
        g_fireKey = {};
        g_currentLevel = e.level;
        g_following = false;
        {
            // A load (title Load / Continue, quick load, a level change) can put the player on an exit and touch it
            // before the first live tick re-takes the arrival spot: hold the exits from the level's start.
            std::lock_guard<std::mutex> gl(g_guardMutex);
            g_guard.levelStart(drakan_game::isCampaignPackLevel(e.level), 0.0f, 0.0f);
        }
    }
}

void hookTick(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == addr(kPadMgrUpdate) && !g_loading.load() && !drakan_lifecycle::transitionActive()) {
        const Ram r{rdram};
        const uint32_t eng = r.u32(addr(kContextPtr));
        const bool live = Ram::ok(eng, 0x4b8) && r.u32(eng + 0x4b0) != 0;
        if (live) {
            ++g_framesInLevel;
            {
                const uint32_t gp = r.u32(eng + 0x4b0);
                const float px = f32(r, gp + 0x14), pz = f32(r, gp + 0x1c);
                std::lock_guard<std::mutex> l(g_guardMutex);
                if (g_framesInLevel == 1)
                    g_guard.levelStart(drakan_game::isCampaignPackLevel(g_currentLevel), px, pz);
                else if (g_guard.tick(drakan_pause::gameplay(), drakan_pause::guiScreen() == kLoadingScreen, px, pz))
                    std::fprintf(stderr, "[levelchange] the player walked off her arrival spot: exits armed\n");
            }
            if (g_probe.steps.size() && (g_framesInLevel <= 3 || g_framesInLevel == 60 || g_framesInLevel % 100 == 0)) {
                const uint32_t pl = r.u32(eng + 0x4b0);
                std::fprintf(stderr, "[levelchange] arrival report '%s' frame %llu player %08x pos %.0f %.0f %.0f\n",
                             g_currentLevel.c_str(), static_cast<unsigned long long>(g_framesInLevel), pl,
                             static_cast<double>(f32(r, pl + 0x14)), static_cast<double>(f32(r, pl + 0x18)),
                             static_cast<double>(f32(r, pl + 0x1c)));
                if (g_framesInLevel % 100 == 0 || g_framesInLevel <= 3)
                    std::fprintf(stderr, "[levelchange]   lock state: scripted %x player flags %08x %08x ctx+4c4 %x\n",
                                 r.u32(addr(0x4fbafcu)), r.u32(pl + 0x90), r.u32(pl + 0x94), r.u32(eng + 0x4c4));
                if (g_framesInLevel % 100 == 0 || g_framesInLevel <= 3) {
                    std::string m;
                    char tmp[16];
                    for (uint32_t k = 0x490; k < 0x4e0; k += 4) {
                        std::snprintf(tmp, sizeof tmp, " %x", r.u32(eng + k));
                        m += tmp;
                    }
                    std::fprintf(stderr, "[levelchange]   ctx+490:%s pl vt %x +c %x +10 %x +fc %x\n", m.c_str(),
                                 r.u32(pl), r.u32(pl + 0xc), r.u32(pl + 0x10), r.u32(pl + 0xfc));
                }
                if (g_framesInLevel == 3) {
                    {
                        std::string m;
                        char tmp[32];
                        for (uint32_t k = 0; k < 13; ++k) {
                            std::snprintf(tmp, sizeof tmp, " %.3f", static_cast<double>(f32(r, pl + 0x14 + 4 * k)));
                            m += tmp;
                        }
                        std::fprintf(stderr, "[levelchange]   player block:%s\n", m.c_str());
                    }
                    scan(rdram, c, rt);
                    for (const ExitInfo& e : exits())
                        std::fprintf(
                            stderr,
                            "[levelchange]   exit %x pos %.0f %.0f %.0f dest %x '%s' p1 %x p2 %x state %x hflags %x\n",
                            e.handle, static_cast<double>(e.pos[0]), static_cast<double>(e.pos[1]),
                            static_cast<double>(e.pos[2]), e.dest, e.destName.c_str(), e.prop[1], e.prop[2], e.state,
                            e.handleFlags);
                }
            }
            const bool inGame = !g_currentLevel.empty() && !drakan_game::isDiscCutsceneLevel(g_currentLevel);
            const bool wanted = inGame && (g_scanWanted.load() || g_probe.armed);
            if (wanted && ++g_scanTicks >= kRescanTicks) {
                g_scanTicks = 0;
                scan(rdram, c, rt);
            }
            uint32_t h = g_fire.exchange(0);
            const int64_t idx = g_fireIndex.exchange(-1);
            if (!h && idx >= 0) {
                std::lock_guard<std::mutex> l(g_m);
                if (static_cast<size_t>(idx) < g_exits.size())
                    h = g_exits[static_cast<size_t>(idx)].handle;
            }
            if (!h && g_probe.armed && g_framesInLevel >= g_probe.after && g_probe.next < g_probe.steps.size() &&
                !drakan_game::isDiscCutsceneLevel(g_currentLevel)) {
                scan(rdram, c, rt);
                std::vector<ExitInfo> list = exits();
                for (size_t i = 0; i < list.size(); ++i)
                    std::fprintf(stderr,
                                 "[levelchange] exit %zu: handle %x pos %.0f %.0f %.0f dest %x '%s' direct %d props %x "
                                 "%x %x %x %x %x %x %x\n",
                                 i, list[i].handle, static_cast<double>(list[i].pos[0]),
                                 static_cast<double>(list[i].pos[1]), static_cast<double>(list[i].pos[2]), list[i].dest,
                                 list[i].destName.c_str(), list[i].direct ? 1 : 0, list[i].prop[0], list[i].prop[1],
                                 list[i].prop[2], list[i].prop[3], list[i].prop[4], list[i].prop[5], list[i].prop[6],
                                 list[i].prop[7]);
                const std::string step = g_probe.steps[g_probe.next++];
                g_probe.armed = g_probe.next < g_probe.steps.size();
                if (!std::isdigit(static_cast<unsigned char>(step[0])))
                    requestLevel(step);
                else if (const size_t idx = static_cast<size_t>(std::strtoul(step.c_str(), nullptr, 10));
                         idx < list.size())
                    h = list[idx].handle;
                else
                    std::fprintf(stderr, "[levelchange] step '%s': no such exit\n", step.c_str());
            }
            std::string want;
            {
                std::lock_guard<std::mutex> l(g_wm);
                want.swap(g_wantLevel);
            }
            if (!inGame)
                want.clear();
            bool follow = false;
            float fp[3] = {};
            {
                std::lock_guard<std::mutex> l(g_wm);
                if (g_haveFollow && inGame && g_framesInLevel >= 120) {
                    follow = true;
                    std::memcpy(fp, g_followPos, sizeof fp);
                    g_haveFollow = false;
                }
            }
            if (follow && !h) {
                scan(rdram, c, rt);
                for (const ExitInfo& e : exits())
                    if (std::fabs(e.pos[0] - fp[0]) < 2.f && std::fabs(e.pos[1] - fp[1]) < 2.f &&
                        std::fabs(e.pos[2] - fp[2]) < 2.f) {
                        h = e.handle;
                        break;
                    }
                std::fprintf(stderr, "[levelchange] follow exit at %.0f %.0f %.0f: %s\n", static_cast<double>(fp[0]),
                             static_cast<double>(fp[1]), static_cast<double>(fp[2]), h ? "found" : "no such exit here");
                g_following = h != 0;
            }
            if (!h && !want.empty()) {
                scan(rdram, c, rt);
                auto low = [](std::string s) {
                    for (char& ch : s)
                        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    return s;
                };
                const std::string w = low(want);
                for (const ExitInfo& e : exits())
                    if (low(e.destName).find(w) != std::string::npos) {
                        h = e.handle;
                        std::fprintf(stderr, "[levelchange] '%s' -> exit %x (%s)\n", want.c_str(), h,
                                     e.destName.c_str());
                        break;
                    }
                if (!h)
                    std::fprintf(stderr, "[levelchange] no exit in this level leads to '%s'\n", want.c_str());
            }
            if (h && !fire(rdram, rt, h) && g_probe.next > 0) {
                g_probe.armed =
                    false; // a refused exit ends the scripted chain (the steps after it assume it was taken)
                g_probe.next = g_probe.steps.size();
            }
        }
    }
    g_origTick(rdram, c, rt);
}

} // namespace

void install(PS2Runtime& rt) {
    if (g_installed.load())
        return;
    if (!drakan_region::build().identityAddresses || !drakan_lifecycle::installed() ||
        !rt.hasFunction(addr(kPadMgrUpdate)))
        return;
    g_origTick = rt.lookupFunction(addr(kPadMgrUpdate));
    rt.replaceFunction(addr(kPadMgrUpdate), &hookTick);
    if (rt.hasFunction(addr(kTransition))) {
        g_origTransition = rt.lookupFunction(addr(kTransition));
        rt.replaceFunction(addr(kTransition), &transitionEntry);
    }
    if (rt.hasFunction(addr(kRespawn))) {
        g_origRespawn = rt.lookupFunction(addr(kRespawn));
        rt.replaceFunction(addr(kRespawn), &respawnEntry);
    }
    if (rt.hasFunction(addr(kRiderInput))) {
        g_origRiderInput = rt.lookupFunction(addr(kRiderInput));
        rt.replaceFunction(addr(kRiderInput), &riderInputEntry);
    }
    if (rt.hasFunction(addr(kArrivalLookup))) {
        g_origArrival = rt.lookupFunction(addr(kArrivalLookup));
        rt.replaceFunction(addr(kArrivalLookup), &arrivalEntry);
    }
    if (std::getenv("DRAKAN_LEVELCHANGE_TRACE")) {
        g_traceHandlers = true;
        if (rt.hasFunction(addr(kTouchHandler))) {
            g_origTouch = rt.lookupFunction(addr(kTouchHandler));
            rt.replaceFunction(addr(kTouchHandler), &touchEntry);
        }
    }
    if (rt.hasFunction(addr(kExitHandler))) {
        g_origExitHandler = rt.lookupFunction(addr(kExitHandler));
        rt.replaceFunction(addr(kExitHandler), &exitHandlerEntry);
    }
    drakan_lifecycle::subscribe(drakan_lifecycle::Kind::BeforeSaveWrite, onLevelEvent);
    drakan_lifecycle::subscribe(drakan_lifecycle::Kind::BeforeLevelLoad, onLevelEvent);
    drakan_lifecycle::subscribe(drakan_lifecycle::Kind::AfterLevelLoad, onLevelEvent);
    if (const char* x = std::getenv("DRAKAN_LEVELCHANGE"); x && *x) {
        std::string s = x;
        if (const size_t at = s.find('@'); at != std::string::npos) {
            g_probe.after = static_cast<uint32_t>(std::strtoul(s.c_str() + at + 1, nullptr, 10));
            s.resize(at);
        }
        for (size_t from = 0; from <= s.size();) {
            const size_t comma = std::min(s.find(',', from), s.size());
            if (comma > from)
                g_probe.steps.push_back(s.substr(from, comma - from));
            from = comma + 1;
        }
        g_probe.armed = !g_probe.steps.empty();
    }
    g_installed = true;
    std::fprintf(stderr, "[levelchange] installed%s\n", g_probe.armed ? " (probe armed)" : "");
}

bool installed() {
    return g_installed.load();
}

std::vector<ExitInfo> exits() {
    std::lock_guard<std::mutex> l(g_m);
    return g_exits;
}

void requestScan() {
    g_scanWanted = true;
}

bool requestExit(uint32_t handle) {
    if (!g_installed.load() || !handle)
        return false;
    g_fire = handle;
    return true;
}

bool requestExitIndex(size_t index) {
    if (!g_installed.load())
        return false;
    g_fireIndex = static_cast<int64_t>(index);
    return true;
}

void setDestinationTextHook(std::function<std::string(const std::string& group, uint32_t id)> hook) {
    std::lock_guard<std::mutex> l(g_m);
    g_destHook = std::move(hook);
}

void registerLevelCopy(const std::string& copy, const std::string& source) {
    std::lock_guard<std::mutex> l(g_m);
    g_copies[copy] = source;
}

bool requestLevel(const std::string& level) {
    if (!g_installed.load() || level.empty())
        return false;
    g_wantLevel = level;
    return true;
}

void setExitListener(std::function<void(const float*)> listener) {
    std::lock_guard<std::mutex> l(g_wm);
    g_exitListener = std::move(listener);
}

bool followExit(float x, float y, float z) {
    if (!g_installed.load())
        return false;
    std::lock_guard<std::mutex> l(g_wm);
    g_followPos[0] = x;
    g_followPos[1] = y;
    g_followPos[2] = z;
    g_haveFollow = true;
    return true;
}

uint64_t framesInLevel() {
    return g_framesInLevel.load();
}

void drawControls() {
#if defined(PS2X_HAVE_GSR)
    if (!g_installed.load())
        return;
    requestScan();
    ImGui::Separator();
    ImGui::TextUnformatted("Level exits");
    const std::vector<ExitInfo> list = exits();
    if (list.empty())
        ImGui::TextDisabled("no exit triggers (loading, or none in this level)");
    for (size_t i = 0; i < list.size(); ++i) {
        const ExitInfo& e = list[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::Text("%08x  %.0f %.0f %.0f  -> %s%s", e.handle, static_cast<double>(e.pos[0]),
                    static_cast<double>(e.pos[1]), static_cast<double>(e.pos[2]),
                    e.destName.empty() ? "?" : e.destName.c_str(),
                    e.portal ? " (portal marker)" : (e.direct ? " (direct)" : ""));
        ImGui::SameLine();
        ImGui::BeginDisabled(e.portal);
        if (ImGui::SmallButton("Go"))
            requestExit(e.handle);
        ImGui::EndDisabled();
        ImGui::PopID();
    }
#endif
}

} // namespace drakan_levelchange
