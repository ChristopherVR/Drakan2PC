// Arokh's breath while Rynn rides him (drakan_dragon_breath.h). PAL addresses.
#include "drakan_dragon_breath.h"
#include "drakan_arena.h"
#include "drakan_game.h"
#include "drakan_lifecycle.h"
#include "drakan_region.h"
#include "input/GuestInterface.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace drakan_dragon_breath {
namespace {

constexpr uint32_t kContextPtr = 0x5565d4u;
constexpr uint32_t kDragonClass = 0x1001cu, kRynnClass = 0x10009u;
constexpr uint32_t kProjectileClass = 0x10008u, kStreamClass = 0x10015u; // breath ball / breath stream engines
constexpr uint16_t kFireBall = 0x340au, kFlameBreath = 0x29e4u;          // RESOURES 13322 / 10724
constexpr uint32_t kDragonHandler = 0x27d0e0u; // (dragon instance, message {sender, u16* payload})
constexpr uint16_t kAddBreath = 0x1028u;       // payload: id, then (weapon, alternate weapon, 0, 0) at +4
constexpr uint32_t kRider = 0x4d8u;            // dragon instance: u16 registry reference of the rider (0x3d2518)
constexpr uint32_t kFlags2 = 0x94u, kMounted = 0x1u, kAttackBusy = 0x400000u;
constexpr uint32_t kBreathChosen = 0x6a4u, kBreathList = 0x6a8u, kBreathCount = 0x6b0u;
constexpr uint32_t kFrame = 0x60u; // trampoline stack frame: message at +0x10, payload at +0x20

bool g_enabled = false, g_log = true, g_give = false;
uint32_t g_slot1 = 0, g_slot2 = 0;
std::atomic<int> g_logBudget{64};

struct Flight {
    uint32_t dragon = 0, weapon = 0, alt = 0, savedSp = 0, savedRa = 0;
} g_flight;
std::atomic<uint32_t> g_serial{0};
bool g_inFlight = false;
uint32_t g_inflightSerial = 0, g_ticks = 0, g_postTick = 0;
uint32_t g_tried = 0;       // dragon handle the stand-in was given to (or refused for) in this level
uint32_t g_riderLogged = 0; // rider handle whose cleared state was logged

void logf(const char* fmt, ...) {
    if (!g_log || g_logBudget.fetch_sub(1) <= 0)
        return;
    va_list ap;
    va_start(ap, fmt);
    std::fputs("[dragon-breath] ", stderr);
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
    va_end(ap);
}

bool ramPtr(uint32_t p, uint32_t n = 4) {
    const uint32_t a = p & 0x1fffffffu;
    return a >= 0x100000u && a + n <= PS2_RAM_SIZE;
}
uint32_t rd32(const uint8_t* ram, uint32_t a) {
    if (!ramPtr(a))
        return 0;
    uint32_t v;
    std::memcpy(&v, ram + (a & 0x1fffffffu), 4);
    return v;
}
uint16_t rd16(const uint8_t* ram, uint32_t a) {
    if (!ramPtr(a, 2))
        return 0;
    uint16_t v;
    std::memcpy(&v, ram + (a & 0x1fffffffu), 2);
    return v;
}
void wr32(uint8_t* ram, uint32_t a, uint32_t v) {
    if (ramPtr(a))
        std::memcpy(ram + (a & 0x1fffffffu), &v, 4);
}

// The resource record of an actor handle (handle +0x10 = u16 record index into ctx+0x320).
uint32_t classOf(const uint8_t* ram, uint32_t handle) {
    const uint32_t ctx = rd32(ram, kContextPtr);
    const uint32_t res = ramPtr(ctx) ? rd32(ram, ctx + 0x320u) : 0;
    if (!ramPtr(handle, 0x100) || !ramPtr(res, 0x14))
        return 0;
    const uint16_t idx = rd16(ram, handle + 0x10u);
    if (idx == 0xffffu || idx > rd32(ram, res + 0x10u))
        return 0;
    return rd32(ram, rd32(ram, res + 8u) + idx * 0x24u + 8u);
}
uint32_t dataOf(const uint8_t* ram, uint32_t handle) {
    const uint32_t holder = ramPtr(handle, 0x100) ? rd32(ram, handle + 0xfcu) : 0;
    return ramPtr(holder, 0x14) ? rd32(ram, holder + 0x10u) : 0;
}

// The resource index of the one class with this id (low 16 bits) and engine, or -1 (none, or more than one).
uint32_t findResource(const uint8_t* ram, uint16_t id, uint32_t engine) {
    const uint32_t ctx = rd32(ram, kContextPtr);
    const uint32_t res = ramPtr(ctx) ? rd32(ram, ctx + 0x320u) : 0;
    if (!ramPtr(res, 0x14))
        return 0xffffffffu;
    const uint32_t table = rd32(ram, res + 8u), count = rd32(ram, res + 0x10u);
    if (!ramPtr(table) || count > 0x20000u)
        return 0xffffffffu;
    uint32_t found = 0xffffffffu;
    for (uint32_t i = 0; i <= count; ++i) {
        const uint32_t rec = table + i * 0x24u;
        if ((rd32(ram, rec) & 0xffffu) == id && rd32(ram, rec + 8u) == engine) {
            if (found != 0xffffffffu)
                return 0xffffffffu;
            found = i;
        }
    }
    return found;
}

// Posted guest call (four arguments, no stack data) -> this trampoline: the message and its payload go on the
// dispatcher thread's stack and the dragon's handler returns to stage 2.
void stage1(uint8_t* rdram, R5900Context* c, PS2Runtime*) {
    g_flight.savedSp = getRegU32(c, 29);
    g_flight.savedRa = getRegU32(c, 31);
    const uint32_t sp = g_flight.savedSp - kFrame, msg = sp + 0x10u, pay = sp + 0x20u;
    const uint32_t m[2] = {0u, pay};
    const uint32_t p[5] = {kAddBreath, g_flight.weapon, g_flight.alt, 0u, 0u};
    std::memcpy(rdram + (msg & PS2_RAM_MASK), m, sizeof m);
    std::memcpy(rdram + (pay & PS2_RAM_MASK), p, sizeof p);
    SET_GPR_U32(c, 29, sp);
    SET_GPR_U32(c, 4, g_flight.dragon);
    SET_GPR_U32(c, 5, msg);
    SET_GPR_U32(c, 31, g_slot2);
    c->pc = kDragonHandler;
}

void stage2(uint8_t*, R5900Context* c, PS2Runtime*) {
    g_serial.fetch_add(1);
    SET_GPR_U32(c, 29, g_flight.savedSp);
    SET_GPR_U32(c, 31, g_flight.savedRa);
    c->pc = g_flight.savedRa;
}

void releaseRider(uint8_t* rdram, uint32_t ctx, uint32_t dragon) {
    const uint16_t ref = rd16(rdram, dragon + kRider);
    if (!ref || ref > rd32(rdram, ctx + 0x84u) + 1u)
        return;
    const uint32_t rider = rd32(rdram, rd32(rdram, ctx + 0x7cu) + (ref - 1u) * 8u);
    if (classOf(rdram, rider) != kRynnClass)
        return;
    const uint32_t data = dataOf(rdram, rider);
    const uint32_t f = rd32(rdram, data + kFlags2);
    if (!ramPtr(data, kFlags2 + 4) || (f & (kMounted | kAttackBusy)) != (kMounted | kAttackBusy))
        return;
    wr32(rdram, data + kFlags2, f & ~kAttackBusy);
    if (g_riderLogged != rider) {
        g_riderLogged = rider;
        logf("rider %x: stale attack state cleared (+0x94 %x -> %x), the breath key reaches the dragon", rider, f,
             f & ~kAttackBusy);
    }
}

} // namespace

void install(PS2Runtime& rt) {
    if (const char* v = std::getenv("DRAKAN_DRAGON_BREATH"); v && v[0] == '0') {
        std::fprintf(stderr, "[dragon-breath] off (DRAKAN_DRAGON_BREATH=0)\n");
        return;
    }
    if (const char* v = std::getenv("DRAKAN_DRAGON_BREATH_LOG"))
        g_log = v[0] != '0';
    if constexpr (!drakan_region::build().identityAddresses) {
        std::fprintf(stderr, "[dragon-breath] off: PAL addresses only\n");
        return;
    } else {
        g_enabled = true;
        if (!rt.hasFunction(kDragonHandler)) {
            std::fprintf(stderr, "[dragon-breath] stand-in breath off: %x missing\n", kDragonHandler);
            return;
        }
        for (uint32_t a = 0x100500u; a < 0x100600u && !(g_slot1 && g_slot2); a += 4) {
            if (rt.hasFunction(a))
                continue;
            if (!g_slot1 && rt.registerFunction(a, &stage1))
                g_slot1 = a;
            else if (g_slot1 && !g_slot2 && rt.registerFunction(a, &stage2))
                g_slot2 = a;
        }
        g_give = g_slot1 && g_slot2;
        if (!g_give)
            std::fprintf(stderr, "[dragon-breath] stand-in breath off: no free trampoline slots\n");
    }
}

void beforePadMgr(drakan_input::IGuest& guest, uint8_t* rdram, bool gameplay) {
    if (!g_enabled)
        return;
    ++g_ticks;
    if (g_inFlight && g_serial.load() != g_inflightSerial) {
        g_inFlight = false;
        const uint32_t d = dataOf(rdram, g_tried);
        logf("dragon %x: stand-in breath given, list count %u, entry %x / %x", g_tried, rd32(rdram, d + kBreathCount),
             rd32(rdram, rd32(rdram, d + kBreathList)), rd32(rdram, rd32(rdram, d + kBreathList) + 4u));
    }
    if (g_inFlight && !guest.postBusy() && g_ticks > g_postTick + 4)
        g_inFlight = false;
    if (drakan_lifecycle::transitionActive()) {
        g_tried = 0; // handles are reused by the next level or load
        return;
    }
    if (!gameplay)
        return;
    const uint32_t ctx = rd32(rdram, kContextPtr);
    const uint32_t h = ramPtr(ctx, 0x4b4) ? rd32(rdram, ctx + 0x4b0u) : 0;
    if (classOf(rdram, h) != kDragonClass)
        return;
    const uint32_t dragon = dataOf(rdram, h);
    if (!ramPtr(dragon, kBreathCount + 4))
        return;
    releaseRider(rdram, ctx, dragon);

    if (!g_give || g_inFlight || guest.postBusy() || h == g_tried ||
        drakan_game::current() != drakan_game::Game::Ootf || drakan_arena::active())
        return;
    if (rd32(rdram, dragon + kBreathCount) != 0 || rd32(rdram, dragon + kBreathList) != 0 ||
        rd32(rdram, dragon + kBreathChosen) != 0xffffffffu)
        return;
    g_tried = h;
    const uint32_t weapon = findResource(rdram, kFireBall, kProjectileClass);
    const uint32_t alt = findResource(rdram, kFlameBreath, kStreamClass);
    if (weapon == 0xffffffffu || alt == 0xffffffffu) {
        logf("dragon %x has no breath, but the stand-in classes are not in this level (%x / %x)", h, weapon, alt);
        return;
    }
    g_flight.dragon = dragon;
    g_flight.weapon = weapon;
    g_flight.alt = alt;
    g_inflightSerial = g_serial.load();
    logf("dragon %x has no breath (an older pack's empty power-up): giving the stand-in (resources %x / %x)", h, weapon,
         alt);
    guest.post(drakan_input::GuestCall(g_slot1));
    g_inFlight = true;
    g_postTick = g_ticks;
}

} // namespace drakan_dragon_breath
