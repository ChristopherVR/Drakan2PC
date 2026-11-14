#include "drakan_view_distance.h"

#include "drakan_config.h"
#include "drakan_region.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include "world/ViewDistance.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

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

inline bool inRam(uint32_t a, uint32_t bytes) {
    return a >= 0x100000u && bytes <= kRamSize && a <= kRamSize - bytes;
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

void hookTerrainView(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
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
    g_installed = true;
    std::fprintf(stderr, "[view-distance] installed (view distance x%.2f)\n", g_factor.load());
}

} // namespace drakan_view_distance
