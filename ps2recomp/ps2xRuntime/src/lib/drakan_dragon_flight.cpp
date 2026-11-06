#include "drakan_dragon_flight.h"
#include "drakan_dragon_flight_policy.h"
#include "drakan_region.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace drakan_dragon_flight {
namespace {
using Fn = PS2Runtime::RecompiledFunction;
// 0x372d38 (cell, from, delta, hit out, hit object out, polygons, terrain triangle out, ...): world ray, 1 on a hit.
constexpr uint32_t kWorldRay = 0x372d38u;
constexpr uint32_t kLookAheadBegin = 0x274250u, kLookAheadEnd = 0x274710u; // the dragon's look-ahead (the caller)
constexpr uint32_t kWorldBox = 0xb8u; // object + 0xb8: its world box (minimum corner, three scaled edges)
Fn g_origRay = nullptr;
bool g_trace = false;
std::atomic<int> g_lines{0}, g_dropped{0};
int g_limit = 400;

struct Ram {
    uint8_t* data;
    static bool ok(uint32_t a, uint32_t n = 4) {
        a &= 0x1fffffffu;
        return a >= 0x100000u && a < PS2_RAM_SIZE && n <= PS2_RAM_SIZE - a;
    }
    uint32_t u32(uint32_t a) const {
        uint32_t v = 0;
        if (ok(a))
            std::memcpy(&v, data + (a & 0x1fffffffu), 4);
        return v;
    }
    float f32(uint32_t a) const {
        const uint32_t b = u32(a);
        float v;
        std::memcpy(&v, &b, 4);
        return v;
    }
};

void rayEntry(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t ra = getRegU32(c, 31);
    if (ra < drakan_region::guestAddress(kLookAheadBegin) || ra >= drakan_region::guestAddress(kLookAheadEnd)) {
        g_origRay(rdram, c, rt);
        return;
    }
    const Ram r{rdram};
    const uint32_t fromAt = getRegU32(c, 5), deltaAt = getRegU32(c, 6), hitAt = getRegU32(c, 7),
                   objAt = getRegU32(c, 8);
    const float from[3] = {r.f32(fromAt), r.f32(fromAt + 4), r.f32(fromAt + 8)};
    const float delta[3] = {r.f32(deltaAt), r.f32(deltaAt + 4), r.f32(deltaAt + 8)};
    g_origRay(rdram, c, rt);
    if (c->pc != ra) // preempted inside the ray (it waits on a semaphore): leave the result to the engine
        return;
    const bool hit = getRegU32(c, 2) != 0;
    const uint32_t obj = r.u32(objAt);
    float box[12] = {};
    if (obj && Ram::ok(obj + kWorldBox, 48))
        for (int k = 0; k < 12; ++k)
            box[k] = r.f32(obj + kWorldBox + 4u * static_cast<uint32_t>(k));
    const bool drop = dropLookAheadHit(hit, obj != 0, box, box + 3, from);
    if (drop) {
        SET_GPR_U32(c, 2, 0u);
        if (g_dropped.fetch_add(1) % 600 == 0)
            std::fprintf(stderr, "[dragon-ray] look-ahead starts inside object %x (at %.0f %.0f %.0f): hit ignored\n",
                         obj, static_cast<double>(r.f32(obj + 0x14)), static_cast<double>(r.f32(obj + 0x18)),
                         static_cast<double>(r.f32(obj + 0x1c)));
    }
    if (g_trace && g_lines.fetch_add(1) < g_limit)
        std::fprintf(
            stderr,
            "[dragon-ray] from %.0f %.0f %.0f delta %.0f %.0f %.0f -> %d hit %.0f %.0f %.0f object %x (at %.0f "
            "%.0f %.0f)%s\n",
            static_cast<double>(from[0]), static_cast<double>(from[1]), static_cast<double>(from[2]),
            static_cast<double>(delta[0]), static_cast<double>(delta[1]), static_cast<double>(delta[2]), hit ? 1 : 0,
            static_cast<double>(r.f32(hitAt)), static_cast<double>(r.f32(hitAt + 4)),
            static_cast<double>(r.f32(hitAt + 8)), obj, static_cast<double>(r.f32(obj + 0x14)),
            static_cast<double>(r.f32(obj + 0x18)), static_cast<double>(r.f32(obj + 0x1c)),
            drop ? " inside: ignored" : "");
}
} // namespace

void install(PS2Runtime& rt) {
    if (!drakan_region::build().identityAddresses || !rt.hasFunction(drakan_region::guestAddress(kWorldRay)))
        return;
    const char* e = std::getenv("DRAKAN_DRAGON_RAY_TRACE");
    g_trace = e && *e && *e != '0';
    if (const char* n = std::getenv("DRAKAN_DRAGON_RAY_LINES"); n && *n)
        g_limit = std::atoi(n);
    g_origRay = rt.lookupFunction(drakan_region::guestAddress(kWorldRay));
    rt.replaceFunction(drakan_region::guestAddress(kWorldRay), &rayEntry);
}

} // namespace drakan_dragon_flight
