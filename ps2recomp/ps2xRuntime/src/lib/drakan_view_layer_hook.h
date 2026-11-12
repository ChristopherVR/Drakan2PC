#pragma once
// Hook on the PAL cell lookup 0x14b6b8 for the one call that updates the view layer (from 0x112488, return address
// 0x112550): applies drakan_view_layer.h. Every other caller (actor refiles, queries) reaches the original untouched.
// On by default; [video] camera_layer_fix = 0 or DRAKAN_VIEW_LAYER_FIX=0 turns it off; DRAKAN_VIEW_LAYER_TRACE=1 logs
// every decision instead of the first 16.
#include "drakan_config.h"
#include "drakan_region.h"
#include "drakan_view_layer.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_no_preempt.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace drakan_view_layer {

inline PS2Runtime::RecompiledFunction lookupOriginal = nullptr;
inline std::atomic<uint32_t> changedCount{0};
inline bool traceAll = false;
// Only touched on the EE thread under ScopedEeNoPreempt: the controlled actor and where it stood at the last view
// update, and the count that keeps the rule aside after a placement.
inline uint32_t settleActor = 0, settleLayers = 0;
inline float settlePosition[3] = {0.0f, 0.0f, 0.0f};
inline Settle settle;

constexpr uint32_t kCellLookup = 0x14b6b8u;       // (f12 x, f13 y, f14 z, a0 world, a1 old layer, a2 flags) -> v0 layer
constexpr uint32_t kViewLookupReturn = 0x112550u; // the view layer update 0x112488 calls it from here
constexpr uint32_t kLayerStride = 0x164u;         // runtime layer record: world +8 array, +0x10 count
constexpr uint32_t kLayerVisible = 0x70u, kLayerVisibleCount = 0x78u; // u16 layer indices
constexpr uint32_t kEngineContext = 0x5565d4u;                        // engine object; +0x4b0 the controlled actor
constexpr uint32_t kActorPosition = 0x14u, kActorLayer = 0x7cu;

inline uint32_t guestWord(const uint8_t* ram, uint32_t address) {
    uint32_t v = 0;
    std::memcpy(&v, ram + (address & (PS2_RAM_SIZE - 1)), 4);
    return v;
}
inline float guestFloat(const uint8_t* ram, uint32_t address) {
    float f = 0.0f;
    std::memcpy(&f, ram + (address & (PS2_RAM_SIZE - 1)), 4);
    return f;
}
inline bool guestPointer(uint32_t a, uint32_t size) {
    return a >= 0x100000u && a < PS2_RAM_SIZE && size <= PS2_RAM_SIZE - a;
}

// `a` lists `b` as visible.
inline bool lists(const uint8_t* ram, uint32_t layers, int32_t count, int32_t a, int32_t b) {
    if (a < 0 || a >= count)
        return false;
    const uint32_t record = layers + static_cast<uint32_t>(a) * kLayerStride;
    const uint32_t list = guestWord(ram, record + kLayerVisible);
    const int32_t n = static_cast<int32_t>(guestWord(ram, record + kLayerVisibleCount));
    if (n <= 0 || n > count || !guestPointer(list, static_cast<uint32_t>(n) * 2u))
        return false;
    for (int32_t i = 0; i < n; ++i) {
        uint16_t v = 0;
        std::memcpy(&v, ram + ((list + 2u * static_cast<uint32_t>(i)) & (PS2_RAM_SIZE - 1)), 2);
        if (v == static_cast<uint16_t>(b))
            return true;
    }
    return false;
}

inline void lookupHook(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    const uint32_t ra = GPR_U32(ctx, 31);
    if (ctx->pc != kCellLookup || ra != kViewLookupReturn) { // another caller, or a resumed call
        lookupOriginal(ram, ctx, rt);
        return;
    }
    const float x = ctx->f[12], y = ctx->f[13], z = ctx->f[14];
    const uint32_t world = GPR_U32(ctx, 4), flags = GPR_U32(ctx, 6);
    Lookup l;
    l.oldLayer = static_cast<int32_t>(GPR_U32(ctx, 5));
    const ScopedEeNoPreempt noPreempt; // the repeated lookup must not interleave with another guest thread
    lookupOriginal(ram, ctx, rt);
    if (ctx->pc != ra || !guestPointer(world, 0x14))
        return;
    l.native = static_cast<int32_t>(GPR_U32(ctx, 2));
    const uint32_t layers = guestWord(ram, world + 8);
    const int32_t count = static_cast<int32_t>(guestWord(ram, world + 0x10));
    if (count <= 0 || count > 0x4000 || !guestPointer(layers, static_cast<uint32_t>(count) * kLayerStride))
        return;
    const uint32_t engine = guestWord(ram, kEngineContext);
    const uint32_t actor = guestPointer(engine, 0x4b4) ? guestWord(ram, engine + 0x4b0) : 0;
    if (!guestPointer(actor, 0x100)) {
        settleActor = 0;
        return;
    }
    const float position[3] = {guestFloat(ram, actor + kActorPosition), guestFloat(ram, actor + kActorPosition + 4),
                               guestFloat(ram, actor + kActorPosition + 8)};
    const bool placed =
        actor != settleActor || layers != settleLayers ||
        teleported(position[0] - settlePosition[0], position[1] - settlePosition[1], position[2] - settlePosition[2]);
    settleActor = actor;
    settleLayers = layers;
    std::memcpy(settlePosition, position, sizeof(position));
    if (settle.update(placed))
        return;
    l.actorLayer = static_cast<int32_t>(guestWord(ram, actor + kActorLayer));
    if (l.actorLayer < 0 || l.actorLayer >= count)
        l.actorLayer = kNoLayer;
    l.cameraNearActor = nearActor(x - position[0], y - position[1], z - position[2]);
    l.nativeRelated = l.native == l.actorLayer || lists(ram, layers, count, l.native, l.actorLayer) ||
                      lists(ram, layers, count, l.actorLayer, l.native);
    const Action action = decide(l);
    if (action == Action::Native)
        return;
    int32_t fromActor = kNoLayer;
    if (action == Action::FromActor) {
        ctx->f[12] = x;
        ctx->f[13] = y;
        ctx->f[14] = z;
        SET_GPR_U32(ctx, 4, world);
        SET_GPR_U32(ctx, 5, static_cast<uint32_t>(l.actorLayer));
        SET_GPR_U32(ctx, 6, flags);
        SET_GPR_U32(ctx, 31, ra);
        ctx->pc = kCellLookup;
        lookupOriginal(ram, ctx, rt);
        if (ctx->pc == ra)
            fromActor = static_cast<int32_t>(GPR_U32(ctx, 2));
    }
    const int32_t result = resolve(action, l, fromActor);
    // The call's outcome: v0 = the layer, the argument registers as the caller left them, return to ra.
    ctx->f[12] = x;
    ctx->f[13] = y;
    ctx->f[14] = z;
    SET_GPR_U32(ctx, 4, world);
    SET_GPR_U32(ctx, 5, static_cast<uint32_t>(l.oldLayer));
    SET_GPR_U32(ctx, 6, flags);
    SET_GPR_U32(ctx, 31, ra);
    SET_GPR_S32(ctx, 2, result);
    ctx->pc = ra;
    if (result != l.native && (changedCount.fetch_add(1) < 16 || traceAll))
        std::fprintf(stderr,
                     "[view-layer] camera point (%.0f %.0f %.0f) from layer %d: engine %d, actor layer %d -> %d\n",
                     static_cast<double>(x), static_cast<double>(y), static_cast<double>(z), l.oldLayer, l.native,
                     l.actorLayer, result);
}

inline void install(PS2Runtime& rt) {
    if (!drakan_region::build().identityAddresses) // SCES-500.06 addresses only
        return;
    if (!drakan_cfg::getBool("video", "camera_layer_fix", "DRAKAN_VIEW_LAYER_FIX", true) ||
        !rt.hasFunction(kCellLookup)) {
        std::fprintf(stderr, "[view-layer] off\n");
        return;
    }
    if (const char* e = std::getenv("DRAKAN_VIEW_LAYER_TRACE"); e && e[0] == '1')
        traceAll = true;
    lookupOriginal = rt.lookupFunction(kCellLookup);
    rt.replaceFunction(kCellLookup, lookupHook);
    std::fprintf(stderr, "[view-layer] installed\n");
}

} // namespace drakan_view_layer
