#include "drakan_foliage.h"

#include "drakan_config.h"
#include "drakan_region.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "world/Foliage.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace drakan_foliage {
namespace {

constexpr uint32_t kRenderObject = 0x189b38u; // per-object render method, a0 = the registered handle
constexpr uint32_t kContextPtr = 0x5565d4u;   // word holding the engine object
constexpr uint32_t kHandleVtable = 0x51b8b0u;
constexpr uint32_t kRamMask = 0x1fffffffu;
constexpr uint32_t kRamSize = PS2_RAM_SIZE;

struct IdKind {
    uint32_t id;
    uint8_t kind;
};
constexpr IdKind kIds[] = {
#include "drakan_foliage_ids.inc"
};

std::atomic<bool> g_installed{false};
std::atomic<bool> g_enabled{false};
std::atomic<float> g_strength{1.0f};
PS2Runtime::RecompiledFunction g_orig = nullptr;

// Statistics for DRAKAN_FOLIAGE_LOG.
std::atomic<uint32_t> g_hits[2]{};
bool g_log = false;

inline bool inRam(uint32_t a, uint32_t bytes) {
    return a >= 0x100000u && bytes <= kRamSize && a <= kRamSize - bytes;
}
inline uint32_t u32(const uint8_t* ram, uint32_t a) {
    uint32_t v;
    std::memcpy(&v, ram + a, 4);
    return v;
}
inline float f32(const uint8_t* ram, uint32_t a) {
    float v;
    std::memcpy(&v, ram + a, 4);
    return v;
}

// The clock is read once per 32 drawn plants / trees (well under a millisecond apart), so a frame shares a few time
// stamps and the wind direction cache in world/Foliage.h hits.
double nowSeconds() {
    static const auto t0 = std::chrono::steady_clock::now();
    thread_local double cached = 0;
    thread_local uint32_t n = 0;
    if ((n++ & 31u) == 0)
        cached = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return cached;
}

// 0 tree, 1 plant, -1 not vegetation.
int kindOf(uint32_t resourceId) {
    const IdKind* end = std::end(kIds);
    const IdKind* it =
        std::lower_bound(std::begin(kIds), end, resourceId, [](const IdKind& e, uint32_t id) { return e.id < id; });
    return it != end && it->id == resourceId ? it->kind : -1;
}

// Resource id of a handle through the resource db (record +0), 0 when anything is out of range.
uint32_t resourceId(const uint8_t* ram, uint32_t handle) {
    uint16_t idx;
    std::memcpy(&idx, ram + handle + 0x10, 2);
    if (idx == 0xffffu)
        return 0;
    const uint32_t ctx = u32(ram, kContextPtr) & kRamMask;
    if (!inRam(ctx, 0x4c0))
        return 0;
    const uint32_t res = u32(ram, ctx + 0x320) & kRamMask;
    if (!inRam(res, 0x14) || idx > u32(ram, res + 0x10))
        return 0;
    const uint32_t table = u32(ram, res + 8) & kRamMask;
    const uint32_t rec = table + static_cast<uint32_t>(idx) * 0x24u;
    return inRam(rec, 0x24) ? u32(ram, rec) : 0;
}

void hookRenderObject(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (!g_enabled.load(std::memory_order_relaxed)) {
        g_orig(ram, c, rt);
        return;
    }
    const uint32_t h = GPR_U32(c, 4) & kRamMask;
    int kind = -1;
    if (inRam(h, 0x120) && u32(ram, h) == kHandleVtable)
        kind = kindOf(resourceId(ram, h));
    if (kind < 0) {
        g_orig(ram, c, rt);
        return;
    }
    const float strength = g_strength.load(std::memory_order_relaxed);
    const float px = f32(ram, h + 0x14), pz = f32(ram, h + 0x1c);
    // Column 1 of the 3x3 at +0x20 (floats at +0x24, +0x30, +0x3c) is where the model's up axis ends up in the world.
    constexpr uint32_t kCol1[3] = {0x24, 0x30, 0x3c};
    float up[3];
    for (int i = 0; i < 3; ++i)
        up[i] = f32(ram, h + kCol1[i]);
    const float len = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
    if (!std::isfinite(px) || !std::isfinite(pz) || !std::isfinite(len) || len < 1e-6f || len > 1e6f) {
        g_orig(ram, c, rt);
        return;
    }
    world::foliage::Lean lean = world::foliage::windLean(kind, px, pz, h, nowSeconds(), strength);
    if (kind == world::foliage::Plant) {
        // Grass bends away from whoever the player controls (Rynn, or Arokh when mounted).
        const uint32_t ctx = u32(ram, kContextPtr) & kRamMask;
        const uint32_t actor = inRam(ctx, 0x4c0) ? u32(ram, ctx + 0x4b0) & kRamMask : 0;
        if (inRam(actor, 0x20) && u32(ram, actor) == kHandleVtable) {
            const world::foliage::Lean push =
                world::foliage::pushLean(px - f32(ram, actor + 0x14), pz - f32(ram, actor + 0x1c), strength);
            lean.x += push.x;
            lean.z += push.z;
        }
    }
    lean = world::foliage::clampLean(lean);
    float add[3];
    world::foliage::shearAdd(lean, len, add);
    for (int i = 0; i < 3; ++i) {
        const float v = up[i] + add[i];
        std::memcpy(ram + h + kCol1[i], &v, 4);
    }
    if (g_log)
        g_hits[kind].fetch_add(1, std::memory_order_relaxed);
    g_orig(ram, c, rt);
    // The change lives for this draw call only: the true transform goes back before anything else can read it.
    for (int i = 0; i < 3; ++i)
        std::memcpy(ram + h + kCol1[i], &up[i], 4);
    if (g_log) {
        static double next = 0;
        const double t = nowSeconds();
        if (t >= next) {
            next = t + 5.0;
            std::fprintf(stderr, "[foliage] drawn since last report: trees %u plants %u (strength %.2f)\n",
                         g_hits[0].exchange(0), g_hits[1].exchange(0), strength);
        }
    }
}

} // namespace

void configure(bool on, float s) {
    g_strength.store(std::clamp(s, world::foliage::kMinStrength, world::foliage::kMaxStrength),
                     std::memory_order_relaxed);
    g_enabled.store(on, std::memory_order_relaxed);
}

bool enabled() {
    return g_enabled.load(std::memory_order_relaxed);
}

float strength() {
    return g_strength.load(std::memory_order_relaxed);
}

bool available() {
    return g_installed.load();
}

void install(PS2Runtime& rt) {
    if (g_installed.load())
        return;
    if (!drakan_region::build().identityAddresses || !rt.hasFunction(kRenderObject))
        return;
    configure(
        drakan_cfg::getBool("video", "foliage_wind", "DRAKAN_FOLIAGE_WIND", false),
        static_cast<float>(drakan_cfg::getFloat("video", "foliage_wind_strength", "DRAKAN_FOLIAGE_STRENGTH", 1.0)));
    g_log = std::getenv("DRAKAN_FOLIAGE_LOG") != nullptr;
    g_orig = rt.lookupFunction(kRenderObject);
    rt.replaceFunction(kRenderObject, &hookRenderObject);
    g_installed = true;
    std::fprintf(stderr, "[foliage] installed (%s, strength %.2f)\n", g_enabled ? "on" : "off", g_strength.load());
}

} // namespace drakan_foliage
