#pragma once
// Hook on the PAL cell lookup 0x14b6b8. The camera caller uses drakan_view_layer.h. The actor reinsertion caller
// handles the stacked floors under the Ruined Village tomb landing during both save and restore.
#include "drakan_config.h"
#include "drakan_guest_layout.h"
#include "drakan_game.h"
#include "drakan_region.h"
#include "drakan_view_layer.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_no_preempt.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>

namespace drakan_view_layer {

inline PS2Runtime::RecompiledFunction lookupOriginal = nullptr;
inline std::atomic<uint32_t> changedCount{0};
inline bool traceAll = false;
inline bool fixView = false;
// Only touched on the EE thread under ScopedEeNoPreempt: the controlled actor and where it stood at the last view
// update, and the count that keeps the rule aside after a placement.
inline uint32_t settleActor = 0, settleLayers = 0;
inline float settlePosition[3] = {0.0f, 0.0f, 0.0f};
inline Settle settle;

constexpr uint32_t kCellLookup = 0x14b6b8u;       // (f12 x, f13 y, f14 z, a0 world, a1 old layer, a2 flags) -> v0 layer
constexpr uint32_t kViewLookupReturn = 0x112550u; // the view layer update 0x112488 calls it from here
constexpr uint32_t kActorInsertReturn = 0x149ab4u;
constexpr float kLandingHeight = 22890.0f, kLandingHeadroom = 1024.0f;
constexpr uint32_t kLayerStride = 0x164u; // runtime layer record: world +8 array, +0x10 count
constexpr uint32_t kLayerVisible = 0x70u, kLayerVisibleCount = 0x78u; // u16 layer indices
constexpr uint32_t kEngineContext = drakan_layout::kEngineContextPtr; // engine object; +0x4b0 the controlled actor
constexpr uint32_t kActorPosition = 0x14u, kActorLayer = 0x7cu;

enum class LookupMode { Off, On, Verify };
inline LookupMode lookupMode = LookupMode::Off;
inline bool traceLookup = false;
constexpr uint32_t kNeighborReturn = 0x14a05cu, kNeighborRetryReturn = 0x14a0a8u;
constexpr uint32_t kLayerSelfRank = 0x68u, kLayerRankCutoff = 0x6eu;
constexpr uint32_t kLayerOrigin = 0x14u, kLayerCells = 0x24u, kLayerHeights = 0x28u, kLayerType = 0x4cu;
constexpr uint32_t kLayerFormat = 0x50u, kSingleHeight = 1u << 22;
constexpr uint32_t kTerrainCellSize = 2048u;
constexpr uint32_t kLookupBackEdgeCycles = 32u;
constexpr uint32_t kLookupHeightCycles = 4u * 8u; // four calls to the native height layout test

inline uint64_t neighborCalls = 0, retryCalls = 0, modeledNeighbors = 0, modeledRetries = 0, verifiedLookups = 0;
inline uint64_t lookupDifferences = 0, verifyYields = 0, modeledCycles = 0;

inline void reportTerrainLookup() {
    std::fprintf(stderr,
                 "[terrain-lookup] neighbors=%llu retries=%llu modeledNeighbors=%llu modeledRetries=%llu "
                 "verified=%llu differences=%llu verifyYields=%llu modeledCycles=%llu\n",
                 static_cast<unsigned long long>(neighborCalls), static_cast<unsigned long long>(retryCalls),
                 static_cast<unsigned long long>(modeledNeighbors), static_cast<unsigned long long>(modeledRetries),
                 static_cast<unsigned long long>(verifiedLookups), static_cast<unsigned long long>(lookupDifferences),
                 static_cast<unsigned long long>(verifyYields), static_cast<unsigned long long>(modeledCycles));
}

inline uint32_t guestWord(const uint8_t* ram, uint32_t address) {
    uint32_t v = 0;
    std::memcpy(&v, ram + address, sizeof(v));
    return v;
}
inline float guestFloat(const uint8_t* ram, uint32_t address) {
    float f = 0.0f;
    std::memcpy(&f, ram + address, sizeof(f));
    return f;
}
inline bool guestPointer(uint32_t a, uint32_t size, uint32_t limit = PS2_RAM_SIZE) {
    return a >= 0x100000u && a < limit && size <= limit - a;
}

inline uint16_t guestHalf(const uint8_t* ram, uint32_t address) {
    uint16_t value;
    std::memcpy(&value, ram + address, sizeof(value));
    return value;
}

struct LayerBounds {
    uint32_t x, z, width, depth;

    bool contains(uint32_t cellX, uint32_t cellZ) const { return cellX - x < width && cellZ - z < depth; }
};

inline LayerBounds layerBounds(const uint8_t* ram, uint32_t layer) {
    LayerBounds bounds;
    std::memcpy(&bounds, ram + layer + kLayerOrigin, sizeof(bounds));
    return bounds;
}

struct CellCenter {
    uint32_t x, z;
    float y;
};

enum class SurfaceRelation { Unsupported, None, AbovePoint, AtOrBelowPoint };

struct NeighborResult {
    int32_t layer = -1;
    uint32_t cycles = 0;
};

struct LookupTerrain {
    const uint8_t* ram;
    uint32_t layers, count, limit;

    SurfaceRelation surface(uint32_t index, const CellCenter& point, uint32_t& cycles) const {
        if (index >= count)
            return SurfaceRelation::Unsupported;
        const uint32_t layer = layers + index * kLayerStride;
        const LayerBounds bounds = layerBounds(ram, layer);
        if (!bounds.contains(point.x, point.z))
            return SurfaceRelation::None;
        const uint32_t type = guestWord(ram, layer + kLayerType);
        if (type == 2)
            return SurfaceRelation::None;
        if (type > 2 || bounds.width > 8192u || bounds.depth > 8192u)
            return SurfaceRelation::Unsupported;

        const uint32_t x = point.x - bounds.x, z = point.z - bounds.z;
        const uint32_t cells = guestWord(ram, layer + kLayerCells);
        const uint32_t cell = (z * bounds.width + x) * 4u;
        if (!guestPointer(cells, cell + 2u, limit))
            return SurfaceRelation::Unsupported;
        const uint16_t cellFlags = guestHalf(ram, cells + cell);
        // The center lies on either diagonal. Both native ties choose the triangle with hole bit 0x40.
        if (cellFlags & 0x40u)
            return SurfaceRelation::None;

        const uint32_t heights = guestWord(ram, layer + kLayerHeights);
        const bool flat = (guestWord(ram, layer + kLayerFormat) & kSingleHeight) != 0;
        const uint32_t stride = bounds.width + 1u;
        const uint32_t vertex = flat ? 0u : z * stride + x;
        const uint32_t lastVertex = flat ? 0u : vertex + stride + 1u;
        const float base = guestFloat(ram, layer + 0x0cu);
        if (!guestPointer(heights, lastVertex * 2u + 2u, limit) || !std::isfinite(base))
            return SurfaceRelation::Unsupported;
        cycles += kLookupHeightCycles;
        const auto heightAt = [&](uint32_t i) {
            const int32_t offset = 2 * static_cast<int32_t>(guestHalf(ram, heights + i * 2u)) - 65536;
            return FPU_ADD_S(static_cast<float>(offset), base);
        };
        const float h0 = heightAt(vertex);
        bool above = FPU_C_OLT_S(point.y, h0);
        if (!flat) {
            const float h1 = heightAt(vertex + 1u), h2 = heightAt(vertex + stride), h3 = heightAt(lastVertex);
            // The native search classifies the whole quad when all four corners agree. Mixed corners use the
            // selected triangle, in the guest's order of operations, to keep equality at the surface exact.
            if (above != FPU_C_OLT_S(point.y, h1) || above != FPU_C_OLT_S(point.y, h2) ||
                above != FPU_C_OLT_S(point.y, h3)) {
                const bool diagonal = (cellFlags & 1u) != 0;
                const float corner = diagonal ? h2 : h0;
                const float dx = FPU_MUL_S(FPU_SUB_S(diagonal ? h3 : h1, corner), 0.5f);
                const float dz = FPU_MUL_S(FPU_SUB_S(diagonal ? h0 : h2, corner), 0.5f);
                const float center = FPU_ADD_S(FPU_ADD_S(corner, dx), dz);
                above = FPU_C_OLT_S(point.y, center);
            }
        }
        if (above)
            return SurfaceRelation::AbovePoint;
        return type == 0 ? SurfaceRelation::AtOrBelowPoint : SurfaceRelation::None;
    }
};

inline std::optional<NeighborResult> neighborLayer(const uint8_t* ram, R5900Context* ctx, uint32_t limit, bool retry) {
    const uint32_t world = GPR_U32(ctx, 4);
    const int32_t old = GPR_S32(ctx, 5);
    if (!guestPointer(world, 0x14, limit) || (retry ? old != -1 : old < 0))
        return std::nullopt;
    const uint32_t width = guestWord(ram, world), depth = guestWord(ram, world + 4);
    const float x = ctx->f[12], z = ctx->f[14];
    if (width > 8192u || depth > 8192u ||
        !(x >= 0 && x < float(width * kTerrainCellSize) && z >= 0 && z < float(depth * kTerrainCellSize)) ||
        !std::isfinite(ctx->f[13]))
        return std::nullopt;
    const CellCenter point{static_cast<uint32_t>(x / kTerrainCellSize), static_cast<uint32_t>(z / kTerrainCellSize),
                           ctx->f[13]};
    if (x != float(point.x * kTerrainCellSize + kTerrainCellSize / 2) ||
        z != float(point.z * kTerrainCellSize + kTerrainCellSize / 2))
        return std::nullopt;

    const uint32_t layers = guestWord(ram, world + 8), count = guestWord(ram, world + 0x10);
    if (count == 0 || count > 0x4000u || !guestPointer(layers, count * kLayerStride, limit))
        return std::nullopt;
    const LookupTerrain terrain{ram, layers, count, limit};
    NeighborResult result;
    if (retry) {
        // With no old layer the native order is descending by layer index.
        uint32_t lastSurfaceCycles = 0;
        for (uint32_t i = count; i != 0;) {
            const uint32_t index = --i;
            const uint32_t before = result.cycles;
            const auto relation = terrain.surface(index, point, result.cycles);
            if (relation == SurfaceRelation::Unsupported)
                return std::nullopt;
            if (relation == SurfaceRelation::AtOrBelowPoint)
                return NeighborResult{static_cast<int32_t>(index), result.cycles};
            if (relation == SurfaceRelation::AbovePoint)
                result.layer = static_cast<int32_t>(index);
            lastSurfaceCycles = result.cycles - before;
            result.cycles += kLookupBackEdgeCycles;
        }
        // The final native iteration reuses index zero. It has no back edge and cannot change the answer.
        result.cycles += lastSurfaceCycles;
        return result;
    }
    if (static_cast<uint32_t>(old) >= count)
        return std::nullopt;

    const uint32_t layer = layers + static_cast<uint32_t>(old) * kLayerStride;
    const LayerBounds bounds = layerBounds(ram, layer);
    if (bounds.width > 8192u || bounds.depth > 8192u)
        return std::nullopt;
    const uint32_t list = guestWord(ram, layer + kLayerVisible), length = guestWord(ram, layer + kLayerVisibleCount);
    const uint32_t self = guestWord(ram, layer + kLayerSelfRank);
    if (length > count || self >= length || !guestPointer(list, length * 2u, limit) ||
        guestHalf(ram, list + self * 2u) != static_cast<uint16_t>(old))
        return std::nullopt;
    const uint32_t cutoff = bounds.contains(point.x, point.z) ? guestHalf(ram, layer + kLayerRankCutoff) : 0u;
    if (self < cutoff)
        return result;

    // Search the authored list from the old layer forward. A higher surface ends a run of floor candidates.
    // If it comes first, or the forward search finds nothing, go back from the old layer to the cutoff.
    uint32_t rank = self;
    for (; rank < length; ++rank) {
        const uint32_t index = guestHalf(ram, list + rank * 2u);
        const auto relation = terrain.surface(index, point, result.cycles);
        if (relation == SurfaceRelation::Unsupported)
            return std::nullopt;
        if (relation == SurfaceRelation::AtOrBelowPoint)
            result.layer = static_cast<int32_t>(index);
        if (relation == SurfaceRelation::AbovePoint) {
            if (result.layer >= 0)
                return result;
            result.layer = static_cast<int32_t>(index);
            break;
        }
        result.cycles += kLookupBackEdgeCycles;
    }
    if (rank == length && result.layer >= 0)
        return result;
    // The native counter restarts when it reverses, then takes a back edge even if the next rank hits the cutoff.
    result.cycles += kLookupBackEdgeCycles;
    for (rank = self; rank > cutoff;) {
        const uint32_t index = guestHalf(ram, list + --rank * 2u);
        const auto relation = terrain.surface(index, point, result.cycles);
        if (relation == SurfaceRelation::Unsupported)
            return std::nullopt;
        if (relation == SurfaceRelation::AtOrBelowPoint)
            return NeighborResult{static_cast<int32_t>(index), result.cycles};
        if (relation == SurfaceRelation::AbovePoint)
            result.layer = static_cast<int32_t>(index);
        result.cycles += kLookupBackEdgeCycles;
    }
    return result;
}

inline void lookupNeighbor(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt, uint32_t ra) {
    const bool retry = ra == kNeighborRetryReturn;
    if (traceLookup) {
        if (retry)
            ++retryCalls;
        else
            ++neighborCalls;
        if (((neighborCalls + retryCalls) & 65535u) == 0)
            reportTerrainLookup();
    }
    const auto candidate = neighborLayer(ram, ctx, rt->memory().guestRamInUse(), retry);
    if (!candidate) {
        lookupOriginal(ram, ctx, rt);
        return;
    }
    if (traceLookup) {
        if (retry)
            ++modeledRetries;
        else
            ++modeledNeighbors;
        modeledCycles += candidate->cycles;
    }
    if (lookupMode == LookupMode::On) {
        // Advance the guest timers for the completed search. Its native intermediate resume points do not exist
        // in this path, so leave dispatch to the caller's next checkpoint.
        if (candidate->cycles != 0) {
            const ScopedEeNoPreempt noPreempt;
            (void)rt->cpTick(candidate->cycles);
        }
        SET_GPR_S32(ctx, 2, candidate->layer);
        ctx->pc = ra;
        return;
    }

    const ScopedEeNoPreempt noPreempt;
    lookupOriginal(ram, ctx, rt);
    if (ctx->pc != ra) {
        ++verifyYields;
        return;
    }
    ++verifiedLookups;
    const int32_t native = GPR_S32(ctx, 2);
    if (native != candidate->layer && ++lookupDifferences <= 4)
        std::fprintf(stderr, "[terrain-lookup] neighbor %x differed: fast=%d native=%d\n", ra, candidate->layer,
                     native);
}

inline int32_t tombLandingLayer(const uint8_t* ram, uint32_t world, int32_t native, float x, float z) {
    if (native < 0 || !guestPointer(world, 0x14))
        return native;
    const uint32_t count = guestWord(ram, world + 0x10);
    const uint32_t layers = guestWord(ram, world + 8);
    if (count > 0x4000 || static_cast<uint32_t>(native) >= count || !guestPointer(layers, count * kLayerStride))
        return native;
    const uint32_t lower = layers + static_cast<uint32_t>(native) * kLayerStride;
    // The converted Ruined Village push-block floor has source ID 451. It lies below both upper cave landing tiles.
    if (guestWord(ram, lower + 4) != 451u || guestFloat(ram, lower + 0x150u) != 7680.0f ||
        drakan_game::current() != drakan_game::Game::Ootf)
        return native;

    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t layer = layers + i * kLayerStride;
        const uint32_t id = guestWord(ram, layer + 4);
        if (id != 269u && id != 325u)
            continue;
        const float floor = guestFloat(ram, layer + 0x150u);
        if (floor != kLandingHeight || guestFloat(ram, layer + 0x15cu) != floor ||
            !(x >= guestFloat(ram, layer + 0x14cu) && x < guestFloat(ram, layer + 0x158u) &&
              z >= guestFloat(ram, layer + 0x154u) && z < guestFloat(ram, layer + 0x160u)))
            continue;
        return static_cast<int32_t>(i);
    }
    return native;
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
    if (lookupMode != LookupMode::Off && ctx->pc == kCellLookup && GPR_U32(ctx, 6) == 5 &&
        (ra == kNeighborReturn || ra == kNeighborRetryReturn)) {
        lookupNeighbor(ram, ctx, rt, ra);
        return;
    }
    const float y = ctx->f[13];
    if (ctx->pc == kCellLookup && ra == kActorInsertReturn && GPR_S32(ctx, 5) == -1 && GPR_U32(ctx, 6) == 5 &&
        y >= kLandingHeight && y - kLandingHeight <= kLandingHeadroom) {
        const uint32_t world = GPR_U32(ctx, 4);
        const float x = ctx->f[12], z = ctx->f[14];
        const ScopedEeNoPreempt noPreempt; // apply the layer before the reinsertion caller consumes it
        lookupOriginal(ram, ctx, rt);
        if (ctx->pc == ra) {
            const int32_t native = GPR_S32(ctx, 2);
            const int32_t landing = tombLandingLayer(ram, world, native, x, z);
            if (landing != native) {
                std::fprintf(stderr, "[tomb-landing] file %.0f %.0f %.0f on layer %d instead of %d (caller %x)\n", x, y,
                             z, landing, native, ra);
                SET_GPR_S32(ctx, 2, landing);
            }
        }
        return;
    }
    if (ctx->pc != kCellLookup || ra != kViewLookupReturn || !fixView) { // another caller, or a resumed call
        lookupOriginal(ram, ctx, rt);
        return;
    }
    const float x = ctx->f[12], z = ctx->f[14];
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
    const uint32_t actor =
        guestPointer(engine, 0x4b4) ? guestWord(ram, engine + drakan_layout::engine::kControlledActor) : 0;
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
    if (!rt.hasFunction(kCellLookup)) {
        std::fprintf(stderr, "[view-layer] off\n");
        return;
    }
    if (const char* e = std::getenv("DRAKAN_VIEW_LAYER_TRACE"); e && e[0] == '1')
        traceAll = true;
    fixView = drakan_cfg::getBool("video", "camera_layer_fix", "DRAKAN_VIEW_LAYER_FIX", true);
    lookupOriginal = rt.lookupFunction(kCellLookup);
    rt.replaceFunction(kCellLookup, lookupHook);
    const char* value = std::getenv("DRAKAN_TERRAIN_LOOKUP");
    lookupMode = !value || std::strcmp(value, "1") == 0 ? LookupMode::On
                 : std::strcmp(value, "verify") == 0    ? LookupMode::Verify
                                                        : LookupMode::Off;
    traceLookup = value && lookupMode != LookupMode::Off;
    if (lookupMode != LookupMode::Off)
        std::fprintf(stderr, "[terrain-lookup] %s\n", lookupMode == LookupMode::On ? "on" : "verify");
    std::fprintf(stderr, "[view-layer] %s\n", fixView ? "installed" : "camera adjustment off");
}

} // namespace drakan_view_layer
