#include "drakan_view_distance.h"

#include "drakan_config.h"
#include "drakan_lifecycle.h"
#include "drakan_region.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include "runtime/ps2_memory.h"
#include "world/ViewDistance.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace drakan_view_distance {
namespace {

namespace rules = world::view_distance;

constexpr uint32_t kSetFog = 0x142f70u;         // a0 = world, f12 = far (cells), f13 = fog range (cells)
constexpr uint32_t kTerrainView = 0x153828u;    // a0 = terrain, a1 = underwater pass: grid radius from world +0x388
constexpr uint32_t kObjectActivity = 0x15cba0u; // a0 = layer: objects within min(+0xf8, far) of the camera
constexpr uint32_t kWorld = 0x4de5e0u;          // the world (the only one the setter is called with in play)
constexpr uint32_t kFar = 0x388u;               // f32 far, cells
constexpr uint32_t kFarSquared = 0x38cu;        // f32 (far * 2048)^2, read by the sound code
constexpr uint32_t kRecord = 0x394u;            // 8 x f32 fog record
constexpr uint32_t kRamSize = 32u * 1024u * 1024u;

// Far scenery: the terrain draw walks the view layer's visible list (layer +0x70 u16 list, +0x78 count).
constexpr uint32_t kTerrainDraw = 0x152330u;       // a0 = terrain (layers at +8, count at +0x10)
constexpr uint32_t kCellLookup = 0x14b6b8u;        // cell lookup: searches the old layer's list (actors, the view)
constexpr uint32_t kViewLayer = 0x4dad58u + 0x8cu; // index of the layer the camera is in, -1 for none
constexpr uint32_t kLayerStride = 0x164u;
constexpr uint32_t kLayerList = 0x70u;
constexpr uint32_t kLayerListCount = 0x78u;
constexpr uint32_t kMalloc = 0x4ac628u;       // a0 = size
constexpr uint32_t kFree = 0x4acc40u;         // a0 = block
constexpr uint32_t kSignalSema = 0x4bc180u;   // a0 = semaphore
constexpr uint32_t kHeapSemaWord = 0x515260u; // the semaphore the game holds round its heap calls
constexpr size_t kMaxEntries = 4096;

std::atomic<bool> g_installed{false};
std::atomic<float> g_factor{rules::kOff};
PS2Runtime::RecompiledFunction g_origSetFog = nullptr;
PS2Runtime::RecompiledFunction g_origTerrainView = nullptr;
PS2Runtime::RecompiledFunction g_origActivity = nullptr;
bool g_log = false;

std::mutex g_mutex;
// What the setter was last called with, and the factor its fields were written for.
bool g_known = false;
rules::FogArgs g_args{};
float g_applied = rules::kOff;

// The activity loop's call that runs with the original far distance (it does not nest; a suspended call keeps its
// scope until it returns to its caller).
struct Scope {
    bool active = false;
    int thread = 0;
    uint32_t returnPc = 0;
    float original = 0, scaled = 0;
};
Scope g_scope;

// Far scenery state (g_mutex).
std::atomic<bool> g_sceneryOn{true};
bool g_sceneryAllowed = false; // heap_extend on: more layers drawn means more draw blocks resident at once
PS2Runtime::RecompiledFunction g_origDraw = nullptr;
PS2Runtime::RecompiledFunction g_origLookup = nullptr;
uint32_t g_cacheBase = 0, g_cacheCount = 0;
float g_cacheReach = 0;
std::unordered_map<uint32_t, std::vector<uint16_t>> g_lists;
uint32_t g_buf = 0, g_bufEntries = 0; // guest block for the widened list, from the game's heap
uint32_t g_bufView = 0xffffffffu, g_bufBase = 0;
uint32_t g_allocFailures = 0;
uint32_t g_sceneryTypes = 7u;
float g_sceneryReachCap = 0.0f;
struct Swap {
    bool active = false;
    int thread = 0;
    uint32_t layer = 0, origList = 0, origCount = 0, count = 0;
};
Swap g_swap;

inline bool inRam(uint32_t a, uint32_t bytes) {
    return a >= 0x100000u && bytes <= kRamSize && a <= kRamSize - bytes;
}
// Anywhere in guest RAM, the extended heap included (level data and the list block can live there).
inline bool inGuest(uint32_t a, uint32_t bytes) {
    return a >= 0x100000u && bytes <= PS2_RAM_SIZE && a <= PS2_RAM_SIZE - bytes;
}
inline uint32_t u32(const uint8_t* ram, uint32_t a) {
    uint32_t v;
    std::memcpy(&v, ram + a, 4);
    return v;
}
inline void putU32(uint8_t* ram, uint32_t a, uint32_t v) {
    std::memcpy(ram + a, &v, 4);
}
inline float f32(const uint8_t* ram, uint32_t a) {
    float v;
    std::memcpy(&v, ram + a, 4);
    return v;
}
inline void putF32(uint8_t* ram, uint32_t a, float v) {
    std::memcpy(ram + a, &v, 4);
}

// Writes the setter's fields for the current factor; the sound distance keeps the original value.
void applyLocked(uint8_t* ram, float factor) {
    if (!g_known || !inRam(kWorld + kFar, kRecord - kFar + 32))
        return;
    const rules::FogFields orig = rules::fields(g_args);
    const rules::FogFields now = rules::fields(rules::scaled(g_args, factor));
    putF32(ram, kWorld + kFar, now.farCells);
    putF32(ram, kWorld + kFarSquared, orig.farSquared);
    std::memcpy(ram + kWorld + kRecord, now.record.data(), sizeof(now.record));
    g_applied = factor;
    if (g_log)
        std::fprintf(stderr, "[view-distance] far %.2f cells, fog range %.2f, factor %.2f -> far %.2f\n",
                     g_args.farCells, g_args.rangeCells, factor, now.farCells);
}

void hookSetFog(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const bool entry = c->pc == kSetFog;
    const uint32_t world = GPR_U32(c, 4) & 0x1fffffffu;
    const rules::FogArgs args{c->f[12], c->f[13]};
    g_origSetFog(ram, c, rt);
    // The setter is a leaf: it always returns at once.
    if (!entry || world != kWorld || !rules::valid(args))
        return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_known = true;
    g_args = args;
    g_applied = rules::kOff; // the original just wrote the unscaled fields
    const float factor = g_factor.load(std::memory_order_relaxed);
    if (factor > rules::kOff)
        applyLocked(ram, factor);
}

// Puts the view layer's own list back (g_mutex), only when the fields still hold what the swap wrote.
void unswapLocked(uint8_t* ram) {
    if (!g_swap.active)
        return;
    if (u32(ram, g_swap.layer + kLayerList) == g_buf) {
        putU32(ram, g_swap.layer + kLayerList, g_swap.origList);
        putU32(ram, g_swap.layer + kLayerListCount, g_swap.origCount);
    }
    g_swap.active = false;
}

// A guest block of at least `entries` u16 from the game's heap, taken under the game's heap semaphore (g_mutex). The
// semaphore is polled, because a nested guest call runs without preemption and must not wait: when the heap is busy
// this frame draws the level's own list and a later frame tries again.
bool ensureBuffer(uint8_t* ram, R5900Context* c, PS2Runtime* rt, uint32_t entries) {
    if (g_buf && g_bufEntries >= entries)
        return true;
    if (g_allocFailures > 64)
        return false;
    const int sema = static_cast<int>(u32(ram, kHeapSemaWord));
    if (rt->eeScheduler().pollSemaphore(sema) != sema)
        return false;
    drakan_lifecycle::Event e;
    e.rdram = ram;
    e.runtime = rt;
    e.ctx = c;
    if (g_buf) {
        drakan_lifecycle::callGuest(e, kFree, g_buf);
        g_buf = 0;
        g_bufEntries = 0;
    }
    const uint32_t want = std::max<uint32_t>(1024u, (entries + 255u) & ~255u);
    const uint32_t block = drakan_lifecycle::callGuest(e, kMalloc, want * 2u) & 0x1fffffffu;
    drakan_lifecycle::callGuest(e, kSignalSema, static_cast<uint32_t>(sema));
    g_bufView = 0xffffffffu;
    if (!block || (block & 1u) || !inGuest(block, want * 2u)) {
        ++g_allocFailures;
        return false;
    }
    g_buf = block;
    g_bufEntries = want;
    if (g_log)
        std::fprintf(stderr, "[view-distance] far scenery list block 0x%x (%u entries)\n", g_buf, want);
    return true;
}

// The widened list of a view layer (g_mutex), cached per level and reach.
const std::vector<uint16_t>& sceneryList(const uint8_t* ram, uint32_t base, uint32_t count, uint32_t view,
                                         float reach) {
    if (base != g_cacheBase || count != g_cacheCount || reach != g_cacheReach) {
        g_lists.clear();
        g_cacheBase = base;
        g_cacheCount = count;
        g_cacheReach = reach;
        g_bufView = 0xffffffffu;
    }
    if (auto it = g_lists.find(view); it != g_lists.end())
        return it->second;
    auto boxOf = [&](uint32_t i, rules::LayerBox& b) {
        const uint32_t a = base + i * kLayerStride;
        const float x = f32(ram, a + 8) / rules::kCell, z = f32(ram, a + 16) / rules::kCell;
        const uint32_t w = u32(ram, a + 0x1c), h = u32(ram, a + 0x20);
        if (!std::isfinite(x) || !std::isfinite(z) || !w || !h || w > 4096 || h > 4096)
            return false;
        b = {x, z, x + float(w), z + float(h)};
        return true;
    };
    auto listOf = [&](uint32_t i, std::vector<uint16_t>& out) {
        const uint32_t a = base + i * kLayerStride;
        const uint32_t list = u32(ram, a + kLayerList) & 0x1fffffffu, n = u32(ram, a + kLayerListCount);
        out.clear();
        if (n > kMaxEntries || (n && !inGuest(list, n * 2u)))
            return false;
        out.resize(n);
        if (n)
            std::memcpy(out.data(), ram + list, n * 2u);
        return true;
    };
    auto& slot = g_lists[view];
    // Diagnostic: which layer types are added (bit 1 floor, 2 ceiling, 4 water; DRAKAN_FAR_SCENERY_TYPES).
    auto keep = [&](uint32_t i) {
        const uint32_t type = u32(ram, base + i * kLayerStride + 0x4c);
        return type < 3 && (g_sceneryTypes >> type & 1u) != 0;
    };
    slot = rules::widenedList(view, count, reach, std::min<size_t>(kMaxEntries, count), boxOf, listOf, keep);
    if (g_log)
        std::fprintf(stderr, "[view-distance] far scenery: layer %u lists %u, draws %zu (reach %.1f cells)\n", view,
                     u32(ram, base + view * kLayerStride + kLayerListCount), slot.size(), reach);
    return slot;
}

void hookTerrainDraw(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t returnPc = GPR_U32(c, 31);
    bool swapped = false;
    if (c->pc == kTerrainDraw) {
        std::lock_guard<std::mutex> lock(g_mutex);
        unswapLocked(ram); // a draw that never came back through here
        const float factor = g_factor.load(std::memory_order_relaxed);
        const uint32_t terrain = GPR_U32(c, 4) & 0x1fffffffu;
        if (g_sceneryAllowed && g_sceneryOn.load(std::memory_order_relaxed) && factor > rules::kOff && g_known &&
            inGuest(terrain, 0x14)) {
            const uint32_t base = u32(ram, terrain + 8) & 0x1fffffffu, count = u32(ram, terrain + 0x10);
            const uint32_t view = u32(ram, kViewLayer);
            if (count && count <= kMaxEntries && view < count && inGuest(base, count * kLayerStride)) {
                float reach = rules::scenicReach(g_args.farCells, factor);
                if (g_sceneryReachCap > 0.0f) // diagnostic: DRAKAN_FAR_SCENERY_REACH (cells)
                    reach = std::min(reach, g_sceneryReachCap);
                const uint32_t layer = base + view * kLayerStride;
                const uint32_t origList = u32(ram, layer + kLayerList), origCount = u32(ram, layer + kLayerListCount);
                const std::vector<uint16_t>& list = sceneryList(ram, base, count, view, reach);
                if (list.size() > origCount && ensureBuffer(ram, c, rt, uint32_t(list.size()))) {
                    if (g_bufView != view || g_bufBase != base) {
                        std::memcpy(ram + g_buf, list.data(), list.size() * 2u);
                        g_bufView = view;
                        g_bufBase = base;
                    }
                    g_swap = {
                        true, rt->eeScheduler().currentThreadId(), layer, origList, origCount, uint32_t(list.size())};
                    putU32(ram, layer + kLayerList, g_buf);
                    putU32(ram, layer + kLayerListCount, uint32_t(list.size()));
                    swapped = true;
                }
            }
        }
    }
    g_origDraw(ram, c, rt);
    if (swapped && c->pc == returnPc) {
        std::lock_guard<std::mutex> lock(g_mutex);
        unswapLocked(ram);
    }
}

// Every other reader of the list sees the level's own list: the widened list starts with it, so only the count is put
// back for the call (a draw in progress keeps the count it read at its start).
void hookCellLookup(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    bool narrowed = false;
    uint32_t layer = 0, count = 0;
    if (c->pc == kCellLookup) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_swap.active && u32(ram, g_swap.layer + kLayerList) == g_buf) {
            layer = g_swap.layer;
            count = g_swap.count;
            putU32(ram, layer + kLayerListCount, g_swap.origCount);
            narrowed = true;
        }
    }
    g_origLookup(ram, c, rt);
    if (narrowed) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_swap.active && g_swap.layer == layer && u32(ram, layer + kLayerList) == g_buf)
            putU32(ram, layer + kLayerListCount, count);
    }
}

void hookTerrainView(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == kTerrainView) {
        // The draw calls the terrain view after its layer loop: the list is not read again in that draw.
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_swap.active && g_swap.thread == rt->eeScheduler().currentThreadId())
            unswapLocked(ram);
    }
    if (c->pc == kTerrainView) {
        const float factor = g_factor.load(std::memory_order_relaxed);
        if (factor != g_applied) {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_known && factor != g_applied)
                applyLocked(ram, factor);
        }
    }
    g_origTerrainView(ram, c, rt);
}

void restoreLocked(uint8_t* ram) {
    // Not when something else wrote the value in between.
    if (g_scope.active && f32(ram, kWorld + kFar) == g_scope.original)
        putF32(ram, kWorld + kFar, g_scope.scaled);
    g_scope.active = false;
}

void hookActivity(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == kObjectActivity) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_scope.active) // a call that never returned (its thread ended inside it)
            restoreLocked(ram);
        if (g_known && g_applied > rules::kOff) {
            const float scaled = f32(ram, kWorld + kFar);
            if (scaled != g_args.farCells) {
                g_scope.active = true;
                g_scope.thread = rt->eeScheduler().currentThreadId();
                g_scope.returnPc = GPR_U32(c, 31);
                g_scope.original = g_args.farCells;
                g_scope.scaled = scaled;
                putF32(ram, kWorld + kFar, g_args.farCells);
            }
        }
    }
    g_origActivity(ram, c, rt);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_scope.active && g_scope.thread == rt->eeScheduler().currentThreadId() && c->pc == g_scope.returnPc)
        restoreLocked(ram);
}

} // namespace

void configureScenery(bool on) {
    g_sceneryOn.store(on, std::memory_order_relaxed);
}

bool scenery() {
    return g_sceneryOn.load(std::memory_order_relaxed);
}

bool sceneryWidens() {
    return g_sceneryAllowed && g_sceneryOn.load(std::memory_order_relaxed) &&
           g_factor.load(std::memory_order_relaxed) > rules::kOff;
}

void configure(float factor) {
    g_factor.store(rules::clampFactor(factor), std::memory_order_relaxed);
}

float factor() {
    return g_factor.load(std::memory_order_relaxed);
}

bool available() {
    return g_installed.load();
}

void install(PS2Runtime& rt) {
    if (g_installed.load())
        return;
    if (!drakan_region::build().identityAddresses || !rt.hasFunction(kSetFog) || !rt.hasFunction(kTerrainView) ||
        !rt.hasFunction(kObjectActivity))
        return;
    configure(static_cast<float>(drakan_cfg::getFloat("video", "view_distance", "DRAKAN_VIEW_DISTANCE", 1.0)));
    g_log = std::getenv("DRAKAN_VIEW_DISTANCE_LOG") != nullptr;
    g_origSetFog = rt.lookupFunction(kSetFog);
    g_origTerrainView = rt.lookupFunction(kTerrainView);
    g_origActivity = rt.lookupFunction(kObjectActivity);
    rt.replaceFunction(kSetFog, &hookSetFog);
    rt.replaceFunction(kTerrainView, &hookTerrainView);
    rt.replaceFunction(kObjectActivity, &hookActivity);
    configureScenery(drakan_cfg::getBool("video", "far_scenery", "DRAKAN_FAR_SCENERY", true));
    if (const char* types = std::getenv("DRAKAN_FAR_SCENERY_TYPES"))
        g_sceneryTypes = static_cast<uint32_t>(std::strtoul(types, nullptr, 0)) & 7u;
    if (const char* cap = std::getenv("DRAKAN_FAR_SCENERY_REACH"))
        g_sceneryReachCap = static_cast<float>(std::atof(cap));
    g_sceneryAllowed = drakan_cfg::getBool("general", "heap_extend", "DRAKAN_HEAP_EXTEND", true) &&
                       rt.hasFunction(kTerrainDraw) && rt.hasFunction(kCellLookup) && rt.hasFunction(kMalloc) &&
                       rt.hasFunction(kFree) && rt.hasFunction(kSignalSema);
    if (g_sceneryAllowed) {
        g_origDraw = rt.lookupFunction(kTerrainDraw);
        g_origLookup = rt.lookupFunction(kCellLookup);
        rt.replaceFunction(kTerrainDraw, &hookTerrainDraw);
        rt.replaceFunction(kCellLookup, &hookCellLookup);
    }
    g_installed = true;
    std::fprintf(stderr, "[view-distance] installed (view distance x%.2f, far scenery %s)\n", g_factor.load(),
                 !g_sceneryAllowed    ? "unavailable: needs heap_extend"
                 : g_sceneryOn.load() ? "on"
                                      : "off");
}

} // namespace drakan_view_distance
