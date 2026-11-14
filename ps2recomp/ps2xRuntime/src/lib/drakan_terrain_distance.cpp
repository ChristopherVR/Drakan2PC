#include "drakan_terrain_distance.h"

#include "drakan_config.h"
#include "drakan_region.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include "world/TerrainDistance.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace drakan_terrain_distance {
namespace {

namespace rules = world::terrain_distance;

constexpr uint32_t kChunkDraw = 0x158148u;   // a0 = layer, a1 = draw page: emits the base pass and the splat passes
constexpr uint32_t kViewPointer = 0x5565c8u; // pointer to the view the terrain draw reads
constexpr uint32_t kViewScale = 0x3cu;       // view: f32 the chunk distance is multiplied with (1.0 in play)
constexpr uint32_t kRamMask = 0x1fffffffu;
constexpr uint32_t kRamSize = PS2_RAM_SIZE;

std::atomic<bool> g_installed{false};
std::atomic<float> g_factor{rules::kOff};
PS2Runtime::RecompiledFunction g_orig = nullptr;
bool g_log = false;

// The one call whose view scale is changed. The chunk draw does not nest; a call that was suspended by the scheduler
// keeps its scope until it returns to its caller.
struct Scope {
    bool active = false;
    int thread = 0;
    uint32_t returnPc = 0, address = 0;
    float stored = 0, scaled = 0;
};
std::mutex g_mutex;
Scope g_scope;
uint64_t g_calls = 0, g_scaledCalls = 0;

inline bool inRam(uint32_t a, uint32_t bytes) {
    return a >= 0x100000u && bytes <= kRamSize && a <= kRamSize - bytes;
}

void restoreLocked(uint8_t* ram) {
    // Not when something else wrote the value in between.
    if (g_scope.active && inRam(g_scope.address, 4) && std::memcmp(ram + g_scope.address, &g_scope.scaled, 4) == 0)
        std::memcpy(ram + g_scope.address, &g_scope.stored, 4);
    g_scope.active = false;
}

void enter(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    std::lock_guard<std::mutex> lock(g_mutex);
    ++g_calls;
    if (g_scope.active) // a call that never returned (its thread ended inside it)
        restoreLocked(ram);
    const float factor = g_factor.load(std::memory_order_relaxed);
    if (!(factor > rules::kOff))
        return;
    uint32_t view;
    std::memcpy(&view, ram + kViewPointer, 4);
    view &= kRamMask;
    if (!inRam(view, kViewScale + 4))
        return;
    float stored;
    std::memcpy(&stored, ram + view + kViewScale, 4);
    const float scaled = rules::viewScale(stored, factor);
    if (std::memcmp(&stored, &scaled, 4) == 0)
        return;
    g_scope.active = true;
    g_scope.thread = rt->eeScheduler().currentThreadId();
    g_scope.returnPc = GPR_U32(c, 31);
    g_scope.address = view + kViewScale;
    g_scope.stored = stored;
    g_scope.scaled = scaled;
    std::memcpy(ram + g_scope.address, &scaled, 4);
    ++g_scaledCalls;
    if (g_log && g_scaledCalls % 20000 == 1)
        std::fprintf(stderr, "[terrain-distance] chunk draws %llu, scaled %llu, view scale %g -> %g (factor %.2f)\n",
                     (unsigned long long)g_calls, (unsigned long long)g_scaledCalls, stored, scaled, factor);
}

void leave(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_scope.active && g_scope.thread == rt->eeScheduler().currentThreadId() && c->pc == g_scope.returnPc)
        restoreLocked(ram);
}

void hookChunkDraw(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == kChunkDraw)
        enter(ram, c, rt);
    g_orig(ram, c, rt);
    leave(ram, c, rt);
}

} // namespace

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
    if (!drakan_region::build().identityAddresses || !rt.hasFunction(kChunkDraw))
        return;
    configure(static_cast<float>(drakan_cfg::getFloat("video", "terrain_distance", "DRAKAN_TERRAIN_DISTANCE", 1.0)));
    g_log = std::getenv("DRAKAN_TERRAIN_DISTANCE_LOG") != nullptr;
    g_orig = rt.lookupFunction(kChunkDraw);
    rt.replaceFunction(kChunkDraw, &hookChunkDraw);
    g_installed = true;
    std::fprintf(stderr, "[terrain-distance] installed (ground texture distance x%.2f)\n", g_factor.load());
}

} // namespace drakan_terrain_distance
