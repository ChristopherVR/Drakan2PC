#include "drakan_ponytail.h"
#include "drakan_ponytail_policy.h"
#include "drakan_region.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace drakan_ponytail {
namespace {
using Fn = PS2Runtime::RecompiledFunction;
// PAL. The step (cloth state, object) and the three passes it calls: rebase the particles into the body's moved frame
// (and measure the anchors' velocities), the length constraints, the body collisions. Each pass is called only from
// the step.
constexpr uint32_t kClothStep = 0x11d368u, kRebase = 0x11fd58u, kConstraints = 0x11ead8u, kCollide = 0x11f1a0u;
constexpr uint32_t kWorldDt = 0x4de604u, kWorldInvDt = 0x4de608u;
// Cloth state: +0 flags (bit 0 started, 0x10 teleported), +0xc the body's position and matrix of the last step
// (12 words, the frame the particles are kept in), +0x3c the resource. Resource +0x2c: air drag. Object +0x14:
// position.
constexpr uint32_t kStateFrame = 0xcu, kStateFrameBytes = 48u, kStateResource = 0x3cu, kResDrag = 0x2cu,
                   kObjPos = 0x14u;
Fn g_orig = nullptr, g_origRebase = nullptr, g_origConstraints = nullptr, g_origCollide = nullptr;
bool g_enabled = true;
bool g_hold = false; // inside a held step: the three passes return at once (game thread only)
float g_softSpeed = kDefaultSoftSpeed;
int g_log = 0;
std::atomic<int> g_lines{0};

struct Gathered {
    uint32_t state = 0;
    float time = 0.0f;
};
Gathered g_gathered[8]; // per cloth (the player, co-op characters, NPCs that have one)
int g_nextGathered = 0;

float& gatheredFor(uint32_t state) {
    for (Gathered& g : g_gathered)
        if (g.state == state)
            return g.time;
    Gathered& g = g_gathered[g_nextGathered++ % 8];
    g = Gathered{state, 0.0f};
    return g.time;
}

struct Ram {
    uint8_t* data;
    static bool ok(uint32_t a, uint32_t n = 4) {
        a &= 0x1fffffffu;
        return a >= 0x100000u && a < PS2_RAM_SIZE && n <= PS2_RAM_SIZE - a;
    }
    uint8_t* at(uint32_t a) const { return data + (a & 0x1fffffffu); }
    uint32_t u32(uint32_t a) const {
        uint32_t v = 0;
        if (ok(a))
            std::memcpy(&v, at(a), 4);
        return v;
    }
    float f32(uint32_t a) const {
        const uint32_t b = u32(a);
        float v;
        std::memcpy(&v, &b, 4);
        return v;
    }
    void putF32(uint32_t a, float v) const {
        if (ok(a))
            std::memcpy(at(a), &v, 4);
    }
};

struct Stats {
    int steps = 0, held = 0, skipped = 0;
    float vmax = 0.0f, smin = 1.0f;
} g_stats;

void pass(Fn orig, uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    if (g_hold) {
        ++g_stats.skipped;
        c->pc = getRegU32(c, 31);
        return;
    }
    orig(rdram, c, rt);
}
void rebase(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    pass(g_origRebase, rdram, c, rt);
}
void constraints(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    pass(g_origConstraints, rdram, c, rt);
}
void collide(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    pass(g_origCollide, rdram, c, rt);
}

void logStep(const Ram& r, uint32_t state, float dt, float stepDt, float speed, float scale, bool held) {
    if (!g_log)
        return;
    held ? ++g_stats.held : ++g_stats.steps;
    g_stats.vmax = std::max(g_stats.vmax, speed);
    g_stats.smin = std::min(g_stats.smin, scale);
    if (g_log >= 2 && g_lines.fetch_add(1) < 4000)
        std::fprintf(stderr, "[ponytail] state %x flags %x dt %.4f step %.4f v %.0f drag scale %.3f%s\n", state,
                     r.u32(state) & 0xffffu, double(dt), double(stepDt), double(speed), double(scale),
                     held ? " held" : "");
    if (g_stats.steps + g_stats.held >= 200) {
        std::fprintf(stderr, "[ponytail] %d steps, %d held (%d passes skipped), max v %.0f, min drag scale %.3f\n",
                     g_stats.steps, g_stats.held, g_stats.skipped, double(g_stats.vmax), double(g_stats.smin));
        g_stats = Stats{};
    }
}

void clothStep(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const Ram r{rdram};
    const uint32_t state = getRegU32(c, 4), obj = getRegU32(c, 5);
    const uint32_t res = r.u32(state + kStateResource);
    if (!g_enabled || !Ram::ok(state, 0x50) || !Ram::ok(obj, 0x60) || !Ram::ok(res + kResDrag)) {
        g_orig(rdram, c, rt);
        return;
    }
    const uint32_t dtAt = drakan_region::guestAddress(kWorldDt), invAt = drakan_region::guestAddress(kWorldInvDt);
    const float dt = r.f32(dtAt), inv = r.f32(invAt);
    float& gathered = gatheredFor(state);
    const bool running = (r.u32(state) & 0x11u) == 0x1u; // otherwise the step starts or resets the cloth itself
    if (!running) {
        gathered = 0.0f;
        g_orig(rdram, c, rt);
        return;
    }
    const float stepDt = cadenceStep(gathered, dt);
    if (stepDt == 0.0f) {
        // Hold: no integration (dt 0) and none of the three passes; the step still writes the particles, unchanged in
        // the body's frame, into the model's vertices. The frame of the last real step is kept for the next one.
        uint8_t frame[kStateFrameBytes];
        std::memcpy(frame, r.at(state + kStateFrame), sizeof frame);
        r.putF32(dtAt, 0.0f);
        g_hold = true;
        g_orig(rdram, c, rt);
        g_hold = false;
        r.putF32(dtAt, dt);
        std::memcpy(r.at(state + kStateFrame), frame, sizeof frame);
        logStep(r, state, dt, 0.0f, 0.0f, 1.0f, true);
        return;
    }
    // A real step over `stepDt`: the body moved since the last real step (the kept frame), so 1/dt is 1/stepDt.
    const float stepInv = stepDt != dt ? 1.0f / stepDt : inv;
    float d[3];
    for (uint32_t k = 0; k < 3; ++k)
        d[k] = r.f32(obj + kObjPos + 4u * k) - r.f32(state + kStateFrame + 4u * k);
    const float distance = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    const float drag = r.f32(res + kResDrag);
    float scale = dragScale(distance, stepDt, stepInv, g_softSpeed);
    if (!(scale < 1.0f) || !(drag > 0.0f))
        scale = 1.0f;
    if (scale < 1.0f)
        r.putF32(res + kResDrag, drag * scale);
    if (stepDt != dt) {
        r.putF32(dtAt, stepDt);
        r.putF32(invAt, stepInv);
    }
    g_orig(rdram, c, rt);
    // Restored also when the step was preempted (it can wait on the heap semaphore); the rest then runs as the
    // engine's.
    if (stepDt != dt) {
        r.putF32(dtAt, dt);
        r.putF32(invAt, inv);
    }
    if (scale < 1.0f)
        r.putF32(res + kResDrag, drag);
    logStep(r, state, dt, stepDt, engineSpeed(distance, stepDt, stepInv), scale, false);
}
} // namespace

void install(PS2Runtime& rt) {
    if (!drakan_region::build().identityAddresses)
        return;
    for (uint32_t a : {kClothStep, kRebase, kConstraints, kCollide})
        if (!rt.hasFunction(drakan_region::guestAddress(a)))
            return;
    if (const char* e = std::getenv("DRAKAN_PONYTAIL"); e && *e)
        g_enabled = *e != '0';
    if (const char* e = std::getenv("DRAKAN_PONYTAIL_SOFT_SPEED"); e && *e)
        g_softSpeed = static_cast<float>(std::atof(e));
    if (const char* e = std::getenv("DRAKAN_PONYTAIL_LOG"); e && *e)
        g_log = std::atoi(e);
    g_orig = rt.lookupFunction(drakan_region::guestAddress(kClothStep));
    g_origRebase = rt.lookupFunction(drakan_region::guestAddress(kRebase));
    g_origConstraints = rt.lookupFunction(drakan_region::guestAddress(kConstraints));
    g_origCollide = rt.lookupFunction(drakan_region::guestAddress(kCollide));
    rt.replaceFunction(drakan_region::guestAddress(kClothStep), &clothStep);
    rt.replaceFunction(drakan_region::guestAddress(kRebase), &rebase);
    rt.replaceFunction(drakan_region::guestAddress(kConstraints), &constraints);
    rt.replaceFunction(drakan_region::guestAddress(kCollide), &collide);
    std::fprintf(stderr, "[ponytail] cloth step cadence and drag easing %s (soft speed %.0f)\n",
                 g_enabled ? "on" : "off (DRAKAN_PONYTAIL=0)", double(g_softSpeed));
}

} // namespace drakan_ponytail
