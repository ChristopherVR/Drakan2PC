#pragma once
// Hooks on the PAL cell lookup 0x14b6b8, the actor refile 0x1496a0 and the refile "from no layer" 0x1499c8 that apply
// drakan_layer_seam.h to the controlled actor and to the view layer.
// DRAKAN_LAYER_SEAM_FIX=0 keeps the native behaviour (A/B test aid); DRAKAN_LAYER_SEAM_TRACE=1 logs every native
// "no layer" result, =2 also every cell change inside DRAKAN_LAYER_SEAM_BOX="x0 x1 z0 z1" with its caller.
#include "drakan_layer_seam.h"
#include "drakan_region.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_no_preempt.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace drakan_layer_seam {

inline PS2Runtime::RecompiledFunction lookupOriginal = nullptr, refileOriginal = nullptr, fileOriginal = nullptr,
                                      viewResetOriginal = nullptr, terrainDrawOriginal = nullptr;
inline std::atomic<uint32_t> recoveredCount{0}, tracedCount{0}, keptCount{0}, refusedCount{0}, rewindCount{0};
// State, only touched on the EE thread under ScopedEeNoPreempt.
inline bool filingControlled = false; // inside 0x1499c8 for the controlled actor (the "from no layer" refile)
inline uint32_t controlledActor = 0;
inline int32_t lastActorCell = kNoCell; // the controlled actor's layer before it lost it
inline bool actorLost = false;          // its last refile ended without a layer
inline bool actorRefused = false;       // rule 1 refused a search for it since it last had a layer
inline bool haveFramePos = false;       // framePos: the controlled actor's position at the last view update
inline float framePos[3] = {0.0f, 0.0f, 0.0f};
inline int32_t viewLostFrom = kNoCell; // the view layer whose own lookup last found nothing (beyond kEdgeSlack)
inline uint32_t knownLayers = 0;       // world +8 when the state above was recorded (a new level resets it)
inline bool recoverEnabled = true;     // DRAKAN_LAYER_SEAM_FIX=0: native result kept
inline bool trace = false;             // DRAKAN_LAYER_SEAM_TRACE=1: log every native "no layer" result (diagnosis)
inline bool traceCallers = false;      // DRAKAN_LAYER_SEAM_TRACE=2: also every cell change inside DRAKAN_LAYER_SEAM_BOX
inline float traceBox[4] = {-1e30f, 1e30f, -1e30f, 1e30f};

constexpr uint32_t kCellLookup = 0x14b6b8u; // (f12 x, f13 y, f14 z, a0 world, a1 old cell, a2 flags) -> v0 cell
constexpr uint32_t kLayerStride = 0x164u;   // runtime layer record; world +8 array, +0x10 count
// The actor refile (records the controlled actor's layer).
constexpr uint32_t kRefile = 0x1496a0u;
// The refile "from no layer" 0x1499c8 (a0 world, a1 actor, a2 hint layer) and its two lookups: from the hint, then
// (when that fails and the hint was a layer) from -1.
constexpr uint32_t kFile = 0x1499c8u, kFileCall = 0x149ab4u, kFileCallGlobal = 0x149b00u;
// The view layer update 0x112488 (each frame, from 0x139010): lookup from the view layer 0x4dade4 at a point 32 units
// behind the camera; the terrain draw walks that layer's `visible` list.
constexpr uint32_t kViewCellCall = 0x112550u;
constexpr uint32_t kViewReset = 0x1435c8u;   // stores -1 into the view layer 0x4dade4 (once per level load)
constexpr uint32_t kTerrainDraw = 0x152330u; // draws the view layer's `visible` list
// The player update 0x321258: when the player's actor has no layer it rewinds her to the position saved at the end of
// the last frame (player +0xbe4) minus a step from 0x325d30, then files her again through the world's vtable
// (0x1499c8); this is the return address of that call.
constexpr uint32_t kPlayerRewindFile = 0x321548u;
constexpr uint32_t kEngineContext = 0x5565d4u; // engine object; +0x4b0 the controlled actor

inline uint32_t guestWord(const uint8_t* ram, uint32_t address) {
    uint32_t v = 0;
    std::memcpy(&v, ram + (address & (PS2_RAM_SIZE - 1)), 4);
    return v;
}
inline uint16_t guestHalf(const uint8_t* ram, uint32_t address) {
    uint16_t v = 0;
    std::memcpy(&v, ram + (address & (PS2_RAM_SIZE - 1)), 2);
    return v;
}
inline bool guestPointer(uint32_t a, uint32_t size) {
    return a >= 0x100000u && a < PS2_RAM_SIZE && size <= PS2_RAM_SIZE - a;
}
inline bool isControlled(const uint8_t* ram, uint32_t actor) {
    const uint32_t engine = guestWord(ram, kEngineContext);
    return guestPointer(engine, 0x4b4) && guestPointer(actor, 0x100) && guestWord(ram, engine + 0x4b0) == actor;
}
// The controlled actor's current layer (-1 when it has none or there is no controlled actor).
inline int32_t controlledCell(const uint8_t* ram) {
    const uint32_t engine = guestWord(ram, kEngineContext);
    if (!guestPointer(engine, 0x4b4))
        return kNoCell;
    const uint32_t actor = guestWord(ram, engine + 0x4b0);
    return guestPointer(actor, 0x100) ? static_cast<int32_t>(guestWord(ram, actor + 0x7c)) : kNoCell;
}
inline float guestFloat(const uint8_t* ram, uint32_t address) {
    const uint32_t bits = guestWord(ram, address);
    float f = 0.0f;
    std::memcpy(&f, &bits, 4);
    return f;
}
// After the controlled actor was (re)filed: remember its layer, or that it lost one.
inline void noteActorCell(uint32_t actor, int32_t before, int32_t after) {
    if (actor != controlledActor) { // another controlled actor (Arokh mounted, a new level)
        controlledActor = actor;
        lastActorCell = kNoCell;
        actorLost = false;
    }
    if (after != kNoCell) {
        lastActorCell = after;
        actorLost = false;
        actorRefused = false;
    } else if (before != kNoCell) {
        lastActorCell = before;
        actorLost = true;
    }
}

inline void lookupHook(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    if (ctx->pc != kCellLookup) { // a resumed call: leave it to the original
        lookupOriginal(ram, ctx, rt);
        return;
    }
    const float x = ctx->f[12], y = ctx->f[13], z = ctx->f[14];
    const uint32_t world = GPR_U32(ctx, 4), flags = GPR_U32(ctx, 6), ra = GPR_U32(ctx, 31);
    const int32_t oldCell = static_cast<int32_t>(GPR_U32(ctx, 5));
    const ScopedEeNoPreempt noPreempt; // the extra lookups below must not interleave with another guest thread
    lookupOriginal(ram, ctx, rt);
    if (ctx->pc != ra)
        return;
    const int32_t native = static_cast<int32_t>(GPR_U32(ctx, 2));
    // Diagnosis (DRAKAN_LAYER_SEAM_TRACE=2): every lookup whose result differs from the old cell, inside a box
    // (DRAKAN_LAYER_SEAM_BOX="x0 x1 z0 z1"), with the caller.
    if (traceCallers && native != oldCell && x >= traceBox[0] && x <= traceBox[1] && z >= traceBox[2] &&
        z <= traceBox[3]) {
        static std::atomic<uint32_t> lines{0};
        if (lines.fetch_add(1) < 20000)
            std::fprintf(stderr, "[layer-seam] call ra %x flags %x from %d -> %d at (%.0f %.0f %.0f) view %d\n", ra,
                         flags, oldCell, native, static_cast<double>(x), static_cast<double>(y), static_cast<double>(z),
                         static_cast<int32_t>(guestWord(ram, 0x4dade4u)));
    }
    if (trace && native == kNoCell && oldCell >= 0 && tracedCount.fetch_add(1) < 2000)
        std::fprintf(stderr, "[layer-seam] native lookup from cell %d: no layer at (%.1f %.1f %.1f) flags %x ra %x\n",
                     oldCell, static_cast<double>(x), static_cast<double>(y), static_cast<double>(z), flags, ra);
    // Only the controlled actor's refiles and the view layer; other actors and queries keep the engine's handling.
    const bool view = ra == kViewCellCall;
    const bool file = filingControlled && (ra == kFileCall || ra == kFileCallGlobal);
    if (!recoverEnabled || flags != 5u || !(view || file) || !guestPointer(world, 0x14))
        return;
    const uint32_t layers = guestWord(ram, world + 8);
    const int32_t count = static_cast<int32_t>(guestWord(ram, world + 0x10));
    if (count <= 0 || count > 0x4000 || !guestPointer(layers, static_cast<uint32_t>(count) * kLayerStride))
        return;
    if (layers != knownLayers) { // another level: nothing known from the last one applies
        knownLayers = layers;
        viewLostFrom = kNoCell;
        lastActorCell = kNoCell;
        actorLost = false;
    }
    if (view) { // once a frame: where the controlled actor stands, to tell a teleport from a step next frame
        const uint32_t engine = guestWord(ram, kEngineContext);
        const uint32_t actor = guestPointer(engine, 0x4b4) ? guestWord(ram, engine + 0x4b0) : 0;
        haveFramePos = guestPointer(actor, 0x100);
        for (uint32_t k = 0; haveFramePos && k < 3; ++k)
            std::memcpy(&framePos[k], ram + ((actor + 0x14 + 4 * k) & (PS2_RAM_SIZE - 1)), 4);
    }
    const auto record = [&](int32_t cell) { return layers + static_cast<uint32_t>(cell) * kLayerStride; };
    const auto rectOf = [&](int32_t cell) -> std::optional<LayerRect> {
        if (cell < 0 || cell >= count)
            return std::nullopt;
        const uint32_t r = record(cell);
        return LayerRect{static_cast<int32_t>(guestWord(ram, r + 0x14)), static_cast<int32_t>(guestWord(ram, r + 0x18)),
                         static_cast<int32_t>(guestWord(ram, r + 0x1c)),
                         static_cast<int32_t>(guestWord(ram, r + 0x20))};
    };
    const auto visibleCount = [&](int32_t cell) {
        const uint32_t r = record(cell);
        const uint32_t list = guestWord(ram, r + 0x70);
        const int32_t n = static_cast<int32_t>(guestWord(ram, r + 0x78));
        return n > 0 && n <= count && guestPointer(list, static_cast<uint32_t>(n) * 2u) ? n : 0;
    };
    const auto visibleAt = [&](int32_t cell, int32_t i) {
        return static_cast<int32_t>(
            guestHalf(ram, guestWord(ram, record(cell) + 0x70) + 2u * static_cast<uint32_t>(i)));
    };
    const auto lookupFrom = [&](int32_t from) {
        ctx->f[12] = x;
        ctx->f[13] = y;
        ctx->f[14] = z;
        SET_GPR_U32(ctx, 4, world);
        SET_GPR_U32(ctx, 5, static_cast<uint32_t>(from));
        SET_GPR_U32(ctx, 6, flags);
        SET_GPR_U32(ctx, 31, ra);
        ctx->pc = kCellLookup;
        lookupOriginal(ram, ctx, rt);
        return ctx->pc == ra ? static_cast<int32_t>(GPR_U32(ctx, 2)) : kNoCell;
    };
    int32_t result = native;
    const char* what = nullptr;
    int32_t via = kNoCell;
    std::atomic<uint32_t>* counter = &keptCount;
    if (view && native == kNoCell && oldCell >= 0) {
        if (nearListedLayers(oldCell, x, z, visibleCount, visibleAt, rectOf)) {
            result = oldCell; // a camera swinging into a wall: keep drawing from the layer it came from
            what = "kept";
        } else {
            viewLostFrom = oldCell; // further out: the next frame searches from -1; remember where it came from
        }
    } else if (view && oldCell == kNoCell && native != kNoCell &&
               foreignLayer(viewReferenceLayer(viewLostFrom, controlledCell(ram), actorLost ? lastActorCell : kNoCell),
                            native, x, z, visibleCount, visibleAt, rectOf)) {
        // That search ended in a layer not adjacent to the one the view just lost, or (the engine reset the view) to
        // the controlled actor's layer: the outdoor layer above the cellar. Look from that layer instead, or keep it.
        const int32_t ref = viewReferenceLayer(viewLostFrom, controlledCell(ram), actorLost ? lastActorCell : kNoCell);
        const int32_t local = lookupFrom(ref);
        result = local != kNoCell ? local : ref;
        what = viewLostFrom >= 0
                   ? (local != kNoCell ? "refused, looked from the lost layer" : "refused, lost layer kept")
                   : (local != kNoCell ? "refused, looked from the actor's layer" : "refused, actor's layer kept");
        via = native;
        counter = &refusedCount;
    } else if (file && oldCell == kNoCell && native != kNoCell && actorLost &&
               !(haveFramePos && teleported(x - framePos[0], y - framePos[1], z - framePos[2])) &&
               foreignLayer(lastActorCell, native, x, z, visibleCount, visibleAt, rectOf)) {
        // The search over all layers for the controlled actor after a step (not a teleport: a placement, a script, a
        // level arrival moves her further than kNearLast in one frame and keeps the engine's search): refuse the
        // outdoor layer above the cellar. She stays without a layer for this tick, as the engine leaves her at walls,
        // and is refiled from the next lookup.
        result = kNoCell;
        actorRefused = true;
        what = "refused, stays without a layer";
        via = native;
        counter = &refusedCount;
    }
    if (view && result != kNoCell)
        viewLostFrom = kNoCell;
    if (result == native)
        return;
    // Restore the call's outcome: v0 = the layer (or -1), argument registers as the caller left them, return to ra.
    ctx->f[12] = x;
    ctx->f[13] = y;
    ctx->f[14] = z;
    SET_GPR_U32(ctx, 4, world);
    SET_GPR_U32(ctx, 5, static_cast<uint32_t>(oldCell));
    SET_GPR_U32(ctx, 6, flags);
    SET_GPR_U32(ctx, 31, ra);
    SET_GPR_S32(ctx, 2, result);
    ctx->pc = ra;
    if (what && (counter->fetch_add(1) < 64 || trace))
        std::fprintf(stderr, "[layer-seam] %s lookup from %d at (%.0f %.0f %.0f): native %d, %s -> %d (via %d)\n",
                     view ? "view" : "actor", oldCell, static_cast<double>(x), static_cast<double>(y),
                     static_cast<double>(z), native, what, result, via);
}

// Diagnosis (DRAKAN_LAYER_SEAM_TRACE): a jump of the controlled actor's position (+0x14) by more than 1000 units
// between two (re)files, with the caller of the refile that saw it.
inline void noteActorMove(const uint8_t* ram, uint32_t actor, const char* where, uint32_t ra, int32_t before,
                          int32_t after) {
    static uint32_t lastActor = 0;
    static float lastX = 0.0f, lastZ = 0.0f;
    float x = 0.0f, z = 0.0f;
    const uint32_t bx = guestWord(ram, actor + 0x14), bz = guestWord(ram, actor + 0x1c);
    std::memcpy(&x, &bx, 4);
    std::memcpy(&z, &bz, 4);
    if (actor == lastActor) {
        const float dx = x - lastX, dz = z - lastZ;
        if (dx * dx + dz * dz > 1000.0f * 1000.0f)
            std::fprintf(stderr,
                         "[layer-seam] actor moved %.0f units: (%.0f %.0f) -> (%.0f %.0f), seen by %s called from %x, "
                         "layer %d -> %d\n",
                         static_cast<double>(std::sqrt(dx * dx + dz * dz)), static_cast<double>(lastX),
                         static_cast<double>(lastZ), static_cast<double>(x), static_cast<double>(z), where, ra, before,
                         after);
    }
    lastActor = actor;
    lastX = x;
    lastZ = z;
}

// Diagnosis (DRAKAN_LAYER_SEAM_TRACE): 0x1435c8 stores -1 into the view layer; count its callers.
inline void viewResetHook(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    const uint32_t ra = GPR_U32(ctx, 31);
    static uint32_t calls = 0, lastRa = 0;
    ++calls;
    if (ra != lastRa || calls % 1000 == 0)
        std::fprintf(stderr, "[layer-seam] view reset 0x1435c8 from %x (call %u, view was %d)\n", ra, calls,
                     static_cast<int32_t>(guestWord(ram, 0x4dade4u)));
    lastRa = ra;
    viewResetOriginal(ram, ctx, rt);
}

// Diagnosis (DRAKAN_LAYER_SEAM_TRACE): terrain draws that find the view layer at -1 (nothing drawn).
inline void terrainDrawHook(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    static uint32_t blank = 0;
    if (ctx->pc == kTerrainDraw && static_cast<int32_t>(guestWord(ram, 0x4dade4u)) == kNoCell &&
        (++blank <= 10 || blank % 100 == 0))
        std::fprintf(stderr, "[layer-seam] terrain draw with view layer -1 (%u so far)\n", blank);
    terrainDrawOriginal(ram, ctx, rt);
}

// The actor refile 0x1496a0 (a0 world, a1 actor): records the controlled actor's layer, or that it lost it.
inline void refileHook(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    if (ctx->pc != kRefile || (!recoverEnabled && !trace)) {
        refileOriginal(ram, ctx, rt);
        return;
    }
    const uint32_t actor = GPR_U32(ctx, 5), ra = GPR_U32(ctx, 31);
    const bool controlled = isControlled(ram, actor);
    const ScopedEeNoPreempt noPreempt;
    const int32_t before = controlled ? static_cast<int32_t>(guestWord(ram, actor + 0x7c)) : kNoCell;
    refileOriginal(ram, ctx, rt);
    if (controlled && ctx->pc == ra) {
        const int32_t after = static_cast<int32_t>(guestWord(ram, actor + 0x7c));
        noteActorCell(actor, before, after);
        if (trace)
            noteActorMove(ram, actor, "refile 0x1496a0", ra, before, after);
    }
}

// The refile "from no layer" 0x1499c8 (a0 world, a1 actor, a2 hint): marks it for lookupHook, records the result.
inline void fileHook(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    if (ctx->pc != kFile || (!recoverEnabled && !trace)) {
        fileOriginal(ram, ctx, rt);
        return;
    }
    const uint32_t actor = GPR_U32(ctx, 5), ra = GPR_U32(ctx, 31);
    const bool controlled = isControlled(ram, actor);
    const ScopedEeNoPreempt noPreempt;
    const int32_t before = controlled ? static_cast<int32_t>(guestWord(ram, actor + 0x7c)) : kNoCell;
    // The player update's rewind of an actor without a layer (saved end-of-frame position player +0xbe4 minus a step
    // from 0x325d30) can throw her 1000 to 12500 units: when the step is that large, rewind to the saved position only.
    // The caller's s17 is the player object (its +0x80 the actor). Only while she is without a layer because rule 1
    // refused a search; a teleport or the engine's own losses keep the engine's rewind.
    const uint32_t player = GPR_U32(ctx, 17);
    if (controlled && recoverEnabled && actorRefused && ra == kPlayerRewindFile && guestPointer(player, 0xbf0) &&
        guestWord(ram, player + 0x80) == actor && static_cast<int32_t>(guestWord(ram, actor + 0x7c)) == kNoCell) {
        const float x = guestFloat(ram, actor + 0x14), z = guestFloat(ram, actor + 0x1c);
        const float sx = guestFloat(ram, player + 0xbe4), sz = guestFloat(ram, player + 0xbec);
        if (rewindTooFar(x - sx, z - sz)) {
            std::memcpy(ram + ((actor + 0x14) & (PS2_RAM_SIZE - 1)), ram + ((player + 0xbe4) & (PS2_RAM_SIZE - 1)), 12);
            if (rewindCount.fetch_add(1) < 64 || trace)
                std::fprintf(stderr, "[layer-seam] rewind to (%.0f %.0f) refused, back to the saved (%.0f %.0f)\n",
                             static_cast<double>(x), static_cast<double>(z), static_cast<double>(sx),
                             static_cast<double>(sz));
        }
    }
    filingControlled = controlled;
    fileOriginal(ram, ctx, rt);
    filingControlled = false;
    if (controlled && ctx->pc == ra) {
        const int32_t after = static_cast<int32_t>(guestWord(ram, actor + 0x7c));
        if (after != kNoCell || before != kNoCell)
            noteActorCell(actor, before, after);
        if (trace)
            noteActorMove(ram, actor, "file 0x1499c8", ra, before, after);
    }
}

inline void install(PS2Runtime& rt) {
    if (!drakan_region::build().identityAddresses) // SCES-500.06 addresses only
        return;
    if (const char* e = std::getenv("DRAKAN_LAYER_SEAM_FIX"); e && e[0] == '0')
        recoverEnabled = false;
    if (const char* e = std::getenv("DRAKAN_LAYER_SEAM_TRACE"); e && (e[0] == '1' || e[0] == '2')) {
        trace = true;
        traceCallers = e[0] == '2';
        if (const char* b = std::getenv("DRAKAN_LAYER_SEAM_BOX"))
            std::sscanf(b, "%f %f %f %f", &traceBox[0], &traceBox[1], &traceBox[2], &traceBox[3]);
    }
    if (!rt.hasFunction(kCellLookup) || (!recoverEnabled && !trace)) {
        std::fprintf(stderr, "[layer-seam] off (DRAKAN_LAYER_SEAM_FIX=0)\n");
        return;
    }
    lookupOriginal = rt.lookupFunction(kCellLookup);
    rt.replaceFunction(kCellLookup, lookupHook);
    if (trace && rt.hasFunction(kViewReset)) {
        viewResetOriginal = rt.lookupFunction(kViewReset);
        rt.replaceFunction(kViewReset, viewResetHook);
    }
    if (trace && rt.hasFunction(kTerrainDraw)) {
        terrainDrawOriginal = rt.lookupFunction(kTerrainDraw);
        rt.replaceFunction(kTerrainDraw, terrainDrawHook);
    }
    if ((recoverEnabled || trace) && rt.hasFunction(kRefile) && rt.hasFunction(kFile)) {
        refileOriginal = rt.lookupFunction(kRefile);
        rt.replaceFunction(kRefile, refileHook);
        fileOriginal = rt.lookupFunction(kFile);
        rt.replaceFunction(kFile, fileHook);
    }
    std::fprintf(stderr, "[layer-seam] installed (recovery %s%s)\n", recoverEnabled ? "on" : "off",
                 trace ? ", trace" : "");
}

} // namespace drakan_layer_seam
