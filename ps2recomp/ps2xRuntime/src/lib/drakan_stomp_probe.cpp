// Bounded, read-only observations of native cinematic readiness. No guest calls, readiness overrides or timing writes.
#include "drakan_stomp_probe.h"
#include "drakan_region.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <tuple>

namespace drakan_stomp_probe {
namespace {
using Clock = std::chrono::steady_clock;
using Fn = PS2Runtime::RecompiledFunction;
constexpr uint32_t kPreload = 0x1a49f0, kActorReady = 0x17a540, kResourceReady = 0x12c3f0, kTick = 0x1a26f0;
Fn g_preload = nullptr, g_actorReady = nullptr, g_resourceReady = nullptr, g_tick = nullptr;
constexpr uint32_t kPose = 0x1a61e8, kCamera = 0x1a68d8, kModel = 0x17a828;
Fn g_pose = nullptr, g_camera = nullptr, g_model = nullptr;
uint32_t g_sequence = 0, g_actor = 0;
using Owner = std::pair<int, size_t>;
Owner g_sequenceOwner{}, g_actorOwner{};
constexpr uint32_t kFrame = 0x1e3970;
Fn g_frame = nullptr;
Owner owner(PS2Runtime* rt) {
    const auto* thread = rt->eeScheduler().currentThread();
    return {rt->eeScheduler().currentThreadId(), thread ? thread->invocations.size() : 0};
}
int g_lines = 0, g_limit = 512;
Clock::time_point g_start, g_lastTick;
struct Ram {
    uint8_t* data;
    static bool valid(uint32_t a, uint32_t n = 4) {
        a &= 0x1fffffff;
        return a >= 0x100000 && a < PS2_RAM_SIZE && n <= PS2_RAM_SIZE - a;
    }
    uint32_t u32(uint32_t a) const {
        uint32_t v = 0;
        if (valid(a))
            std::memcpy(&v, data + (a & 0x1fffffff), 4);
        return v;
    }
    uint16_t u16(uint32_t a) const {
        uint16_t v = 0;
        if (valid(a, 2))
            std::memcpy(&v, data + (a & 0x1fffffff), 2);
        return v;
    }
    float f32(uint32_t a) const {
        const uint32_t bits = u32(a);
        float v;
        std::memcpy(&v, &bits, 4);
        return v;
    }
};
void log(const char* format, ...) {
    if (g_lines >= g_limit)
        return;
    ++g_lines;
    const double seconds = std::chrono::duration<double>(Clock::now() - g_start).count();
    std::fprintf(stderr, "[stomp-probe %.3f] ", seconds);
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc('\n', stderr);
    if (g_lines == g_limit)
        std::fprintf(stderr, "[stomp-probe] log limit reached; further diagnostics suppressed\n");
}
// At most one observation per key per second, and no unbounded diagnostic state.
bool due(uint32_t kind, uint32_t a, uint32_t b) {
    static std::map<std::tuple<uint32_t, uint32_t, uint32_t>, Clock::time_point> seen;
    if (g_lines >= g_limit)
        return false;
    const auto key = std::make_tuple(kind, a, b);
    const auto now = Clock::now();
    const auto it = seen.find(key);
    if (it != seen.end() && now - it->second < std::chrono::seconds(1))
        return false;
    if (it == seen.end() && seen.size() >= 512)
        return false;
    seen[key] = now;
    return true;
}
uint32_t cached(const Ram& r, uint32_t manager, uint32_t id) {
    uint32_t p = r.u32(manager + 4 * ((id * 0x59u & 127u) + 8));
    for (int i = 0; p && i < 512; ++i) {
        if (!Ram::valid(p, 0x1c))
            return 0;
        if (r.u32(p) == id)
            return p;
        p = r.u32(p + 0x14);
    }
    return 0;
}
uint32_t activeSequence(const Ram& r) {
    const uint32_t count = r.u32(0x4dba68 + 0x23c), list = r.u32(0x4dba68 + 0x234);
    return count > 0 && count <= 8 ? r.u32(list) : 0;
}
bool loadingScene(const Ram& r, uint32_t sequence) {
    return sequence && r.u32(0x4fb60c) && r.u32(r.u32(sequence + 0xc)) == 0x8003c;
}
uint32_t objectId(const Ram& r, uint32_t sequence, uint32_t actor) {
    if (!actor)
        return 0;
    const uint32_t resource = r.u32(sequence + 0xc), table = r.u32(resource + 0x24);
    const uint32_t count = std::min(r.u32(resource + 0x2c), 256u);
    const uint16_t ref = r.u16(actor + 0x12);
    if (!ref || !Ram::valid(table, count * 32))
        return 0;
    for (uint32_t i = 0; i < count; ++i)
        if (r.u16(table + i * 32 + 0xc) == ref)
            return r.u32(table + i * 32 + 8);
    return 0;
}
void preload(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc != kPreload || g_lines >= g_limit) {
        g_preload(ram, c, rt);
        return;
    }
    const uint32_t old = g_sequence, sequence = getRegU32(c, 4), ra = getRegU32(c, 31);
    const Owner oldOwner = g_sequenceOwner;
    g_sequenceOwner = owner(rt);
    g_sequence = sequence;
    g_preload(ram, c, rt);
    g_sequence = old;
    g_sequenceOwner = oldOwner;
    // A guest yield returns at an internal PC; only inspect a completed invocation.
    if (c->pc == ra && getRegU32(c, 2) == 2 && due(0, sequence, 0)) {
        const Ram r{ram};
        log("preload pending seq=%08x ref=%08x elapsed=%.4f pause=%u action=%u preload=%u count=%u dt=%.6f", sequence,
            r.u32(r.u32(sequence + 0xc)), r.f32(sequence + 0x18), r.u32(sequence + 0x1c), r.u32(sequence + 0x10),
            r.u32(sequence + 0x14), r.u32(sequence + 0x48), r.f32(0x4de604));
    }
}
void actorReady(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc != kActorReady || !g_sequence || g_sequenceOwner != owner(rt) || g_lines >= g_limit) {
        g_actorReady(ram, c, rt);
        return;
    }
    const uint32_t old = g_actor, actor = getRegU32(c, 4), sequence = g_sequence, ra = getRegU32(c, 31);
    const Owner oldOwner = g_actorOwner;
    g_actorOwner = owner(rt);
    g_actor = actor;
    g_actorReady(ram, c, rt);
    g_actor = old;
    g_actorOwner = oldOwner;
    if (c->pc == ra && getRegU32(c, 2) == 2 && due(1, sequence, actor)) {
        const Ram r{ram};
        log("actor pending seq=%08x object=%u actor=%08x resource=%08x controller=%08x", sequence,
            objectId(r, sequence, actor), actor, r.u32(actor + 8), r.u32(actor + 0x50));
    }
}
void resourceReady(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t ra = getRegU32(c, 31);
    const Ram current{ram};
    const Owner currentOwner = owner(rt);
    const bool scoped = g_sequence && g_sequenceOwner == currentOwner;
    const uint32_t observed = scoped ? g_sequence : activeSequence(current);
    const bool cinematicCaller = (ra >= 0x1a1900 && ra < 0x1a7500) || loadingScene(current, observed);
    if (c->pc != kResourceReady || (!scoped && !cinematicCaller) || g_lines >= g_limit) {
        g_resourceReady(ram, c, rt);
        return;
    }
    const uint32_t manager = getRegU32(c, 4), id = getRegU32(c, 5);
    const uint32_t actor = g_actorOwner == currentOwner ? g_actor : 0, sequence = observed;
    g_resourceReady(ram, c, rt);
    if (c->pc == ra && getRegU32(c, 2) == 2 && due(2, manager, id)) {
        const Ram r{ram};
        const uint32_t descriptor = cached(r, manager, id);
        log("resource pending seq=%08x object=%u actor=%08x manager=%08x id=%08x descriptor=%08x flags=%08x "
            "caller=%08x thread=%d invocation=%zu scoped=%d",
            sequence, objectId(r, sequence, actor), actor, manager, id, descriptor, r.u32(descriptor + 8), ra,
            currentOwner.first, currentOwner.second, scoped ? 1 : 0);
    }
}
void tick(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == kTick && g_lines < g_limit && Clock::now() - g_lastTick >= std::chrono::seconds(1)) {
        g_lastTick = Clock::now();
        const Ram r{ram};
        const uint32_t manager = getRegU32(c, 4), table = r.u32(manager + 0x234);
        const uint32_t count = std::min(r.u32(manager + 0x23c), 8u);
        if (!count)
            log("tick manager=%08x sequences=0 dt=%.6f gui=%08x", manager, r.f32(0x4de604), r.u32(0x4fb60c));
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t s = r.u32(table + 4 * i);
            log("tick seq=%08x ref=%08x elapsed=%.4f pause=%u flags=%08x action=%u preload=%u count=%u dt=%.6f "
                "gui=%08x",
                s, r.u32(r.u32(s + 0xc)), r.f32(s + 0x18), r.u32(s + 0x1c), r.u32(s + 8), r.u32(s + 0x10),
                r.u32(s + 0x14), r.u32(s + 0x48), r.f32(0x4de604), r.u32(0x4fb60c));
        }
    }
    g_tick(ram, c, rt);
}
// Independent frame sampling remains available when the cinematic manager stops being scheduled.
void frame(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    static Clock::time_point last;
    const Ram r{ram};
    const uint32_t sequence = activeSequence(r), ref = r.u32(r.u32(sequence + 0xc));
    if (c->pc == kFrame && ref >= 0x8003b && ref <= 0x8003d && g_lines < g_limit &&
        Clock::now() - last >= std::chrono::seconds(5)) {
        last = Clock::now();
        const auto state = rt->eeScheduler().snapshot();
        log("kernel seq=%08x ref=%08x elapsed=%.4f gui=%08x running=%d snapshot=%llu", sequence, ref,
            r.f32(sequence + 0x18), r.u32(0x4fb60c), state.runningThreadId,
            static_cast<unsigned long long>(state.sequence));
        size_t count = 0;
        for (const auto& thread : state.threads) {
            if (++count > 16)
                break;
            log("thread id=%d pc=%08x ra=%08x sp=%08x state=%u wait=%u waitId=%d priority=%d invocation=%u", thread.id,
                thread.pc, thread.ra, thread.sp, static_cast<unsigned>(thread.status),
                static_cast<unsigned>(thread.waitReason), thread.waitId, thread.currentPriority,
                thread.invocationDepth);
            for (const auto& sema : state.semaphores)
                if (thread.waitReason == EeWaitReason::Semaphore && thread.waitId == sema.id)
                    log("semaphore id=%d count=%d maximum=%d waiters=%u", sema.id, sema.count, sema.maxCount,
                        sema.waiters);
        }
    }
    g_frame(ram, c, rt);
}
// Entry/return/resume observations distinguish a stalled nested call from a manager that is no longer scheduled.
// These wrappers never redirect guest returns; a yield is reported with its actual suspended PC.
template <uint32_t Entry>
void phase(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    Fn original = Entry == kPose ? g_pose : Entry == kCamera ? g_camera : g_model;
    const Ram r{ram};
    const uint32_t sequence = activeSequence(r), pc = c->pc, arg = getRegU32(c, 4), ra = getRegU32(c, 31);
    const bool observe = loadingScene(r, sequence) && g_lines < g_limit;
    if (observe && due(Entry, pc, arg))
        log("phase %s function=%08x pc=%08x arg=%08x ra=%08x seq=%08x elapsed=%.4f", pc == Entry ? "enter" : "resume",
            Entry, pc, arg, ra, sequence, r.f32(sequence + 0x18));
    original(ram, c, rt);
    if (observe && due(Entry + 1, c->pc, arg))
        log("phase %s function=%08x pc=%08x entryArg=%08x ra=%08x seq=%08x", c->pc == ra ? "return" : "yield", Entry,
            c->pc, arg, ra, sequence);
}

} // namespace
void install(PS2Runtime& rt) {
    const char* enabled = std::getenv("DRAKAN_STOMP_PROBE");
    if (!enabled || std::strcmp(enabled, "1") != 0)
        return;
    if (!drakan_region::build().identityAddresses) {
        std::fprintf(stderr, "[stomp-probe] PAL-only probe not installed\n");
        return;
    }
    if (g_preload)
        return;
    if (!rt.hasFunction(kPreload) || !rt.hasFunction(kActorReady) || !rt.hasFunction(kResourceReady) ||
        !rt.hasFunction(kTick) || !rt.hasFunction(kPose) || !rt.hasFunction(kCamera) || !rt.hasFunction(kModel) ||
        !rt.hasFunction(kFrame)) {
        std::fprintf(stderr, "[stomp-probe] required guest function missing; no hooks installed\n");
        return;
    }
    if (const char* n = std::getenv("DRAKAN_STOMP_PROBE_LINES"))
        g_limit = std::clamp(std::atoi(n), 1, 4096);
    g_start = Clock::now();
    g_lastTick = g_start;
    g_preload = rt.lookupFunction(kPreload);
    g_actorReady = rt.lookupFunction(kActorReady);
    g_resourceReady = rt.lookupFunction(kResourceReady);
    g_tick = rt.lookupFunction(kTick);
    rt.replaceFunction(kPreload, preload);
    rt.replaceFunction(kActorReady, actorReady);
    rt.replaceFunction(kResourceReady, resourceReady);
    rt.replaceFunction(kTick, tick);
    g_pose = rt.lookupFunction(kPose);
    g_camera = rt.lookupFunction(kCamera);
    g_model = rt.lookupFunction(kModel);
    rt.replaceFunction(kPose, phase<kPose>);
    rt.replaceFunction(kCamera, phase<kCamera>);
    rt.replaceFunction(kModel, phase<kModel>);
    g_frame = rt.lookupFunction(kFrame);
    rt.replaceFunction(kFrame, frame);
    log("installed maxLines=%d (completed readiness calls only; yielded calls are not inferred)", g_limit);
}
} // namespace drakan_stomp_probe
