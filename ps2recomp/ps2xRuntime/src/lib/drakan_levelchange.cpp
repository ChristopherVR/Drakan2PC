// Level change from the host (drakan_levelchange.h). See docs/journal/2026-10-31/level-change.md for the mechanism.
#include "drakan_levelchange.h"
#include "drakan_lifecycle.h"
#include "drakan_region.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"

#if defined(PS2X_HAVE_GSR)
#include "imgui.h"
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>

void ps2xNoPreemptEnter() noexcept;
void ps2xNoPreemptLeave() noexcept;

namespace drakan_levelchange {
namespace {

// SCES-500.06 (identity addresses only; install() refuses other profiles).
constexpr uint32_t kPadMgrUpdate = 0x1e3970u;
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
std::atomic<bool> g_following{false};
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

// Destination names. Property 0 is a level-local string id (0x8000n); resolving it through the string manager blocks on
// guest semaphores (a nested call froze the guest), so the names seen in game are kept in a table instead. Observed by
// firing every Surdana exit (journal 2026-10-31/level-change.md); other levels fall back to "?" and the exit list shows
// the raw id.
struct KnownDest {
    const char* from;
    uint32_t id;
    const char* to;
};
constexpr KnownDest kKnownDests[] = {
    {"Level 1A - Surdana", 0x80005, "Level 2 - Shadowmire"},
    {"Level 1A - Surdana", 0x80006, "Level 3A - Andrellian Isles"},
    {"Level 1A - Surdana", 0x8000d, "Level 1B - Kragmor"},
    {"Level 1A - Surdana", 0x8000e, "Level 1C - Catacombs"},
    {"Level 1A - Surdana", 0x8000f, "Level 1D - City of the Damned"},
    {"Level 1A - Surdana", 0x80010, "Level 1E - Inquisitor's Lair"},
};
std::string knownName(const std::string& level, uint32_t id) {
    for (const KnownDest& k : kKnownDests)
        if (id == k.id && level == k.from)
            return k.to;
    return {};
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
            e.destName = knownName(g_currentLevel, e.prop[0]);
            e.direct = e.prop[6] != 0 && e.prop[6] != 0xdeadbeefu;
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

bool fire(uint8_t* rdram, PS2Runtime* rt, uint32_t handle) {
    const Ram r{rdram};
    const uint32_t eng = r.u32(addr(kContextPtr));
    if (!Ram::ok(eng, 0x4b8) || drakan_lifecycle::transitionActive())
        return false;
    const uint32_t inner = r.u32(handle + 0xfc);
    const uint32_t inst = Ram::ok(inner, 0x14) ? r.u32(inner + 0x10) : 0;
    if (!Ram::ok(inst, 0x30)) { // the class instance data: what the trigger's own handlers work on
        std::fprintf(stderr, "[levelchange] handle %x has no instance data\n", handle);
        return false;
    }
    std::fprintf(stderr, "[levelchange] firing exit %x\n", handle);
    if (!std::getenv("DRAKAN_LEVELCHANGE_NOMOVE")) {
        // The transition keeps the player's offset from the trigger ("Maintain Player Position"): arriving 100 000
        // units from the destination's arrival marker puts Rynn outside the level. Stand on the trigger first
        // (player and camera move together).
        const uint32_t pl = r.u32(eng + 0x4b0), cam = r.u32(eng + 0x4b4);
        float d[3];
        for (uint32_t k = 0; k < 3; ++k)
            d[k] = f32(r, handle + 0x14 + 4 * k) - f32(r, pl + 0x14 + 4 * k);
        for (const uint32_t o : {pl, cam}) {
            if (!Ram::ok(o, 0x20))
                continue;
            for (uint32_t k = 0; k < 3; ++k) {
                const float v = f32(r, o + 0x14 + 4 * k) + d[k];
                uint32_t bits;
                std::memcpy(&bits, &v, 4);
                r.w32(o + 0x14 + 4 * k, bits);
            }
        }
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
        g_currentLevel = e.level;
        g_following = false;
    }
}

void hookTick(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == addr(kPadMgrUpdate) && !g_loading.load() && !drakan_lifecycle::transitionActive()) {
        const Ram r{rdram};
        const uint32_t eng = r.u32(addr(kContextPtr));
        const bool live = Ram::ok(eng, 0x4b8) && r.u32(eng + 0x4b0) != 0;
        if (live) {
            ++g_framesInLevel;
            const bool inGame = !g_currentLevel.empty() && g_currentLevel.find("Intro") == std::string::npos;
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
                g_currentLevel.find("Intro") == std::string::npos) {
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
            if (h)
                fire(rdram, rt, h);
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
                    e.destName.empty() ? "?" : e.destName.c_str(), e.direct ? " (direct)" : "");
        ImGui::SameLine();
        if (ImGui::SmallButton("Go"))
            requestExit(e.handle);
        ImGui::PopID();
    }
#endif
}

} // namespace drakan_levelchange
