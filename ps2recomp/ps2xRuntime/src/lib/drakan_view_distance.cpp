#include "drakan_view_distance.h"

#include "drakan_config.h"
#include "drakan_lifecycle.h"
#include "drakan_region.h"
#include "drakan_scene_geometry.h"
#include "drakan_texture_budget.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include "runtime/ps2_memory.h"
#include "world/FarLand.h"
#include "world/ViewDistance.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace drakan_view_distance {
namespace {

namespace rules = world::view_distance;
namespace farland = world::far_land;

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
uint64_t g_bufKey = ~0ull;            // what the block holds: a view layer (closure) or a far land key
uint32_t g_bufBase = 0;
uint32_t g_allocFailures = 0;
uint32_t g_sceneryTypes = 7u;
float g_sceneryReachCap = 0.0f;
struct Swap {
    bool active = false;
    int thread = 0;
    uint32_t layer = 0, origList = 0, origCount = 0, count = 0;
};
Swap g_swap;

// Far land at factor 1 in converted levels (g_mutex): a snapshot of the level's terrain, taken once per terrain layout.
bool g_farLandOn = true; // DRAKAN_FAR_LAND=0 turns it off alone (diagnostic)
// Diagnostics (env DRAKAN_FAR_LAND_NEAR / _CUSHION / _RISE / _REACH): candidate distances in cells, observer spread.
float g_farNear = farland::kNearCells, g_farCushion = farland::kCushionCells, g_farRise = farland::kRise;
float g_farReachCap = 0.0f;
uint64_t g_farIdentity = 0;
std::vector<farland::Layer> g_farLayers;
std::unique_ptr<farland::Ground> g_farGround;
std::vector<std::vector<farland::Point>> g_farTargets;
// Lists per view layer and camera cell (x, z: 2048 units, height: 1024), bounded.
std::unordered_map<uint64_t, std::vector<uint16_t>> g_farLists;
constexpr size_t kFarListsMax = 1024;
constexpr uint32_t kCameraPos = 0x4dad58u + 0x6cu; // camera position x, y, z (the object distance test reads it)

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
    g_bufKey = ~0ull;
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
        g_bufKey = ~0ull;
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

// The level's terrain as farland::Layer (g_mutex), read again when the layout changes (a level load). Layer fields as
// drakan_scene_geometry reads them: origin +8 / +12 / +16 (world units), size +0x1c / +0x20, cell words +0x24 (bits
// 0x50 / 0xa0 hide the two triangles), heights +0x28 (u16, y = origin y + 2 * h - 65536; one height with flag
// 0x400000), type +0x4c, flags +0x50 (bit 0: not drawn).
void farLandSnapshotLocked(const uint8_t* ram, uint32_t ramBytes) {
    const uint64_t identity = drakan_scene_geometry::layoutIdentity(ram, ramBytes);
    if (identity == g_farIdentity && g_farGround)
        return;
    g_farIdentity = identity;
    g_farLayers.clear();
    g_farTargets.clear();
    g_farLists.clear();
    g_bufKey = ~0ull;
    const uint32_t base = u32(ram, 0x4dc118) & 0x1fffffffu, count = u32(ram, 0x4dc120);
    if (identity && count <= kMaxEntries && inGuest(base, count * kLayerStride)) {
        g_farLayers.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t a = base + i * kLayerStride, type = u32(ram, a + 0x4c), flags = u32(ram, a + 0x50);
            const uint32_t w = u32(ram, a + 0x1c), h = u32(ram, a + 0x20);
            const uint32_t heights = u32(ram, a + 0x28) & 0x1fffffffu, cells = u32(ram, a + 0x24) & 0x1fffffffu;
            const float ox = f32(ram, a + 8), oy = f32(ram, a + 12), oz = f32(ram, a + 16);
            if (type > 2 || (flags & 1u) || !w || !h || w > 512 || h > 512)
                continue;
            const uint32_t vertices = (w + 1) * (h + 1);
            const uint32_t heightCount = (flags & 0x400000u) ? 1u : vertices;
            if (!inGuest(heights, heightCount * 2u) || !inGuest(cells, w * h * 4u) || !std::isfinite(ox) ||
                !std::isfinite(oy) || !std::isfinite(oz) || std::fabs(ox) > 1e8f || std::fabs(oz) > 1e8f)
                continue;
            farland::Layer& L = g_farLayers[i];
            L.type = int(type);
            L.x0 = int(std::lround(ox / rules::kCell));
            L.z0 = int(std::lround(oz / rules::kCell));
            L.w = int(w);
            L.h = int(h);
            L.heights.resize(vertices);
            for (uint32_t v = 0; v < vertices; ++v) {
                uint16_t raw;
                std::memcpy(&raw, ram + heights + ((flags & 0x400000u) ? 0u : v * 2u), 2);
                L.heights[v] = oy + float(raw) * 2.0f - 65536.0f;
            }
            L.drawn.resize(size_t(w) * h);
            for (uint32_t q = 0; q < w * h; ++q) {
                uint16_t bits;
                std::memcpy(&bits, ram + cells + q * 4u, 2);
                L.drawn[q] = !(bits & 0x50u) || !(bits & 0xa0u);
            }
        }
    }
    g_farGround = std::make_unique<farland::Ground>(g_farLayers);
}

// The view layer's own list plus the far land in sight from the camera (g_mutex), cached per view layer and camera
// cell. `key` receives the cache key (bit 63 set, so it never equals a closure list's view index).
const std::vector<uint16_t>& farLandList(const uint8_t* ram, uint32_t ramBytes, uint32_t base, uint32_t count,
                                         uint32_t view, float reach, uint64_t& key) {
    static const std::vector<uint16_t> kNone;
    farLandSnapshotLocked(ram, ramBytes);
    const float cx = f32(ram, kCameraPos), cy = f32(ram, kCameraPos + 4), cz = f32(ram, kCameraPos + 8);
    if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(cz) || std::fabs(cx) > 1e8f ||
        std::fabs(cy) > 1e8f || std::fabs(cz) > 1e8f)
        return kNone;
    const auto cell = [](float v, float size) { return uint64_t(uint16_t(int32_t(std::floor(v / size)))); };
    key = (1ull << 63) | uint64_t(view & 0x7fffu) << 48 | cell(cx, rules::kCell) << 32 | cell(cz, rules::kCell) << 16 |
          cell(cy, 1024.0f);
    if (auto it = g_farLists.find(key); it != g_farLists.end())
        return it->second;
    if (g_farLists.size() >= kFarListsMax) {
        g_farLists.clear();
        g_bufKey = ~0ull;
    }
    auto& slot = g_farLists[key];
    const uint32_t layer = base + view * kLayerStride;
    const uint32_t list = u32(ram, layer + kLayerList) & 0x1fffffffu, n = u32(ram, layer + kLayerListCount);
    if (n > kMaxEntries || (n && !inGuest(list, n * 2u)))
        return slot;
    slot.resize(n);
    if (n)
        std::memcpy(slot.data(), ram + list, n * 2u);
    if (g_farLayers.size() != count || !g_farGround)
        return slot;
    const auto t0 = std::chrono::steady_clock::now();
    const std::vector<uint16_t> add =
        farland::farLayers(view, g_farLayers, *g_farGround, slot, reach,
                           farland::cameraObservers(cx, cy, cz, g_farCushion, g_farRise), g_farTargets, g_farNear);
    for (uint16_t j : add) {
        if (slot.size() >= kMaxEntries)
            break;
        slot.push_back(j);
    }
    if (g_log)
        std::fprintf(stderr, "[view-distance] far land: layer %u at (%.0f, %.0f, %.0f) lists %u, draws %zu (%.1f ms)\n",
                     view, cx, cy, cz, n, slot.size(),
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
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
        // Factor 1: far land, in converted levels only (their lists are the PC's, made for a shorter fog).
        const bool farLand = !(factor > rules::kOff) && g_farLandOn && drakan_texture_budget::convertedLevel();
        if (g_sceneryAllowed && g_sceneryOn.load(std::memory_order_relaxed) && (factor > rules::kOff || farLand) &&
            g_known && inGuest(terrain, 0x14)) {
            const uint32_t base = u32(ram, terrain + 8) & 0x1fffffffu, count = u32(ram, terrain + 0x10);
            const uint32_t view = u32(ram, kViewLayer);
            if (count && count <= kMaxEntries && view < count && inGuest(base, count * kLayerStride)) {
                float reach = rules::scenicReach(g_args.farCells, factor);
                if (g_sceneryReachCap > 0.0f) // diagnostic: DRAKAN_FAR_SCENERY_REACH (cells)
                    reach = std::min(reach, g_sceneryReachCap);
                const uint32_t layer = base + view * kLayerStride;
                const uint32_t origList = u32(ram, layer + kLayerList), origCount = u32(ram, layer + kLayerListCount);
                uint64_t key = view;
                float farReach = g_args.farCells + farland::kReachMargin;
                if (g_farReachCap > 0.0f) // diagnostic: DRAKAN_FAR_LAND_REACH (cells)
                    farReach = std::min(farReach, g_farReachCap);
                const std::vector<uint16_t>& list =
                    farLand ? farLandList(ram, PS2_RAM_SIZE, base, count, view, farReach, key)
                            : sceneryList(ram, base, count, view, reach);
                if (list.size() > origCount && ensureBuffer(ram, c, rt, uint32_t(list.size()))) {
                    if (g_bufKey != key || g_bufBase != base) {
                        std::memcpy(ram + g_buf, list.data(), list.size() * 2u);
                        g_bufKey = key;
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
           (g_factor.load(std::memory_order_relaxed) > rules::kOff || g_farLandOn);
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
    if (const char* fl = std::getenv("DRAKAN_FAR_LAND"))
        g_farLandOn = std::strcmp(fl, "0") != 0;
    const auto envFloat = [](const char* name, float& out) {
        if (const char* v = std::getenv(name))
            out = static_cast<float>(std::atof(v));
    };
    envFloat("DRAKAN_FAR_LAND_NEAR", g_farNear);
    envFloat("DRAKAN_FAR_LAND_CUSHION", g_farCushion);
    envFloat("DRAKAN_FAR_LAND_RISE", g_farRise);
    envFloat("DRAKAN_FAR_LAND_REACH", g_farReachCap);
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
