// Arokh's breath while Rynn rides him (drakan_dragon_breath.h). PAL addresses.
#include "drakan_dragon_breath.h"
#include "drakan_arena.h"
#include "drakan_game.h"
#include "drakan_lifecycle.h"
#include "drakan_region.h"
#include "input/GuestInterface.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/guest_access.h"

#include <atomic>
#include <bit>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>

namespace drakan_dragon_breath {
namespace {

constexpr uint32_t kContextPtr = 0x5565d4u;
constexpr uint32_t kDragonClass = 0x1001cu, kRynnClass = 0x10009u;
constexpr uint32_t kProjectileClass = 0x10008u, kStreamClass = 0x10015u; // breath ball / breath stream engines
constexpr uint16_t kFireBall = 0x340au, kFlameBreath = 0x29e4u;          // RESOURES 13322 / 10724
constexpr uint32_t kFactory = 0x181ad8u;
constexpr uint32_t kNpcDeath = 0x47a8b0u;
constexpr uint32_t kDamageHandlerReturn = 0x476b0cu, kDamageDeathReturn = 0x474308u;
constexpr uint16_t kBurnDamage = 0x10d7u;
constexpr uint32_t kFlameBurnEffect = 0x42874u, kBurnEffectClass = 0x10005u;
constexpr uint32_t kWartokClass = 0x10031u, kWartokPieces = 0x3472au, kGeneratorClass = 0x1003fu;
constexpr uint32_t kSpawnGeneratorChild = 0x2d1124u;
constexpr uint32_t kFirstWartokPiece = 0x3481fu, kLastWartokPiece = 0x34822u;
constexpr uint32_t kNpcActor = 0x23cu, kNpcHealth = 0x24cu;
constexpr uint32_t kSetActorEnabled = 0x17e418u;
constexpr uint32_t kActorEnabled = 1u, kActorDying = 0x80000000u;
constexpr float kPieceOriginLift = 256.0f;
struct ImpactEffect {
    uint32_t caller;
    uint16_t resource;
    uint32_t engine;
};
constexpr ImpactEffect kFireBallImpactEffects[] = {
    {0x39c8f8u, 0x2b54u, 0x1003fu}, // FireBall Explosion Main
    {0x39c998u, 0x29ddu, 0x10086u}, // FireBall Shrapnel
    {0x39c7ecu, 0x3470u, 0x10001u}, // Fire Ball3D Fire Ring
};
constexpr uint32_t kCameraPosition = 0x4dad58u + 0x6cu;
constexpr float kNearBurstDistance = 6.0f * 2048.0f;
constexpr uint32_t kDragonHandler = 0x27d0e0u; // (dragon instance, message {sender, u16* payload})
constexpr uint16_t kAddBreath = 0x1028u;       // payload: id, then (weapon, alternate weapon, 0, 0) at +4
constexpr uint32_t kRider = 0x4d8u;            // dragon instance: u16 registry reference of the rider (0x3d2518)
constexpr uint32_t kFlags2 = 0x94u, kMounted = 0x1u, kAttackBusy = 0x400000u;
constexpr uint32_t kBreathChosen = 0x6a4u, kBreathList = 0x6a8u, kBreathCount = 0x6b0u;
constexpr uint32_t kFrame = 0x60u; // trampoline stack frame: message at +0x10, payload at +0x20

bool g_enabled = false, g_log = true, g_give = false;
uint32_t g_slot1 = 0, g_slot2 = 0;
std::atomic<int> g_logBudget{64};
PS2Runtime::RecompiledFunction g_factory = nullptr;
PS2Runtime::RecompiledFunction g_npcDeath = nullptr;
uint32_t g_breakupStart = 0, g_breakupResult = 0;

struct Breakup {
    uint32_t actor = 0, data = 0, resource = 0;
    uint16_t registryRef = 0;
};

std::deque<Breakup> g_breakups;
R5900Context* g_breakupContext = nullptr;
bool g_breakupSpawnedPiece = false;

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

// This file follows pointer chains through objects that may not exist yet: pointers are accepted through KSEG
// too, and a null / low pointer reads 0 instead of low RAM (the strict guest accessors).
bool ramPtr(uint32_t p, uint32_t n = 4) {
    return guest::isHeapPtr(guest::phys(p), n);
}

uint32_t resourceRecord(const uint8_t* ram, uint32_t ctx, uint32_t index) {
    const uint32_t res = ramPtr(ctx) ? guest::readHeap32(ram, ctx + 0x320u) : 0;
    if (!ramPtr(res, 0x14) || index == 0xffffu || index > guest::readHeap32(ram, res + 0x10u))
        return 0;
    return guest::readHeap32(ram, res + 8u) + index * 0x24u;
}

uint32_t actorResource(const uint8_t* ram, uint32_t handle) {
    if (!ramPtr(handle, 0x100))
        return 0;
    return resourceRecord(ram, guest::readHeap32(ram, kContextPtr), guest::readHeap16(ram, handle + 0x10u));
}

uint32_t classOf(const uint8_t* ram, uint32_t handle) {
    return guest::readHeap32(ram, actorResource(ram, handle) + 8u);
}

bool isCloseMountedFireBallBurst(const uint8_t* ram, const R5900Context* c) {
    const uint32_t caller = getRegU32(c, 31);
    const ImpactEffect* visual = nullptr;
    for (const auto& effect : kFireBallImpactEffects)
        if (effect.caller == caller)
            visual = &effect;
    if (!visual || drakan_game::current() != drakan_game::Game::Ootf || drakan_arena::active())
        return false;
    const uint32_t game = guest::readHeap32(ram, kContextPtr);
    if (classOf(ram, guest::readHeap32(ram, game + 0x4b0u)) != kDragonClass)
        return false;
    const uint32_t projectile = actorResource(ram, getRegU32(c, 5));
    const uint32_t effect = resourceRecord(ram, game, getRegU32(c, 9));
    if ((guest::readHeap32(ram, projectile) & 0xffffu) != kFireBall ||
        guest::readHeap32(ram, projectile + 8u) != kProjectileClass ||
        (guest::readHeap32(ram, effect) & 0xffffu) != visual->resource ||
        guest::readHeap32(ram, effect + 8u) != visual->engine)
        return false;
    const uint32_t impact = getRegU32(c, 6);
    if (!ramPtr(impact, 12))
        return false;
    float distanceSq = 0;
    for (uint32_t i = 0; i < 12; i += 4) {
        const float d = std::bit_cast<float>(guest::readHeap32(ram, impact + i)) -
                        std::bit_cast<float>(guest::readHeap32(ram, kCameraPosition + i));
        distanceSq += d * d;
    }
    return distanceSq < kNearBurstDistance * kNearBurstDistance;
}

void spawnEffect(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (isCloseMountedFireBallBurst(ram, c)) {
        // Close impact visuals can overlap Arokh. Collision and damage are handled separately by the projectile.
        logf("suppressed a fireball impact visual near the mounted camera (caller %x)", getRegU32(c, 31));
        SET_GPR_U32(c, 2, 0);
        c->pc = getRegU32(c, 31);
        return;
    }
    const bool spawningPiece = c == g_breakupContext && c->pc == kFactory && getRegU32(c, 31) == kSpawnGeneratorChild;
    const uint32_t piece =
        spawningPiece ? guest::readHeap32(ram, resourceRecord(ram, getRegU32(c, 4), getRegU32(c, 9))) : 0;
    g_factory(ram, c, rt);
    if (piece >= kFirstWartokPiece && piece <= kLastWartokPiece && c->pc == kSpawnGeneratorChild && getRegU32(c, 2))
        g_breakupSpawnedPiece = true;
}
uint32_t dataOf(const uint8_t* ram, uint32_t handle) {
    const uint32_t holder = ramPtr(handle, 0x100) ? guest::readHeap32(ram, handle + 0xfcu) : 0;
    return ramPtr(holder, 0x14) ? guest::readHeap32(ram, holder + 0x10u) : 0;
}

// A class ID can omit the database number. Multiple matches are ambiguous.
uint32_t findResource(const uint8_t* ram, uint32_t id, uint32_t engine) {
    const uint32_t ctx = guest::readHeap32(ram, kContextPtr);
    const uint32_t res = ramPtr(ctx) ? guest::readHeap32(ram, ctx + 0x320u) : 0;
    if (!ramPtr(res, 0x14))
        return 0xffffffffu;
    const uint32_t table = guest::readHeap32(ram, res + 8u), count = guest::readHeap32(ram, res + 0x10u);
    if (!ramPtr(table) || count > 0x20000u)
        return 0xffffffffu;
    const uint32_t mask = id <= 0xffffu ? 0xffffu : 0xffffffffu;
    uint32_t found = 0xffffffffu;
    for (uint32_t i = 0; i <= count; ++i) {
        const uint32_t rec = table + i * 0x24u;
        if ((guest::readHeap32(ram, rec) & mask) == id && guest::readHeap32(ram, rec + 8u) == engine) {
            if (found != 0xffffffffu)
                return 0xffffffffu;
            found = i;
        }
    }
    return found;
}

bool canBreakUpWartok() {
    return drakan_game::current() == drakan_game::Game::Ootf && !drakan_arena::active() &&
           !drakan_lifecycle::transitionActive();
}

bool isWartok(uint32_t resource) {
    switch (resource) {
        case 0x50000u | 4251u: // Chain center
        case 0x50000u | 5067u: // Plate center
        case 0x50000u | 5174u: // Leather center
        case 0x50000u | 5784u: // Chain flank
        case 0x50000u | 5786u: // Leather flank
        case 0x50000u | 5788u: // Plate flank
            return true;
        default:
            return false;
    }
}

void queueFlameBreakup(const uint8_t* ram, const R5900Context* c) {
    if (c->pc != kNpcDeath || getRegU32(c, 31) != kDamageDeathReturn || !canBreakUpWartok())
        return;
    // At this call site the native damage handler has applied damage. Its frame keeps the message payload at +0x44,
    // the return address at +0xf0, the source actor in s4 and the health before the hit in fp.
    const uint32_t sp = getRegU32(c, 29), data = getRegU32(c, 4);
    if (guest::readHeap32(ram, sp + 0xf0u) != kDamageHandlerReturn ||
        guest::readHeap16(ram, guest::readHeap32(ram, sp + 0x44u)) != kBurnDamage ||
        std::bit_cast<int32_t>(getRegU32(c, 30)) <= 0 ||
        std::bit_cast<int32_t>(guest::readHeap32(ram, data + kNpcHealth)) > 0)
        return;
    const uint32_t source = actorResource(ram, getRegU32(c, 20));
    if (guest::readHeap32(ram, source) != kFlameBurnEffect || guest::readHeap32(ram, source + 8u) != kBurnEffectClass)
        return;
    const uint32_t actor = guest::readHeap32(ram, data + kNpcActor), record = actorResource(ram, actor),
                   resource = guest::readHeap32(ram, record);
    if (isWartok(resource) && guest::readHeap32(ram, record + 8u) == kWartokClass && dataOf(ram, actor) == data &&
        g_breakups.size() < 16)
        g_breakups.push_back({actor, data, resource, guest::readHeap16(ram, actor + 0x12u)});
}

void npcDeath(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    queueFlameBreakup(ram, c);
    g_npcDeath(ram, c, rt);
}

bool isSameDeadWartok(const uint8_t* ram, const Breakup& victim) {
    return canBreakUpWartok() && victim.registryRef != 0 &&
           guest::readHeap16(ram, victim.actor + 0x12u) == victim.registryRef &&
           guest::readHeap32(ram, actorResource(ram, victim.actor)) == victim.resource &&
           dataOf(ram, victim.actor) == victim.data &&
           guest::readHeap32(ram, victim.data + kNpcActor) == victim.actor &&
           (guest::readHeap32(ram, victim.actor + 4u) & (kActorEnabled | kActorDying)) == kActorEnabled &&
           std::bit_cast<int32_t>(guest::readHeap32(ram, victim.data + kNpcHealth)) <= 0;
}

// The dispatcher can run the one-shot generator inline. In that case its factory return is zero even when it
// produced pieces. The spawn hook records those child creations so we can still retire the original corpse.
void startBreakup(uint8_t* ram, R5900Context* c, PS2Runtime*) {
    const Breakup victim{getRegU32(c, 4), getRegU32(c, 5), getRegU32(c, 6), uint16_t(getRegU32(c, 7))};
    c->pc = getRegU32(c, 31);
    if (!isSameDeadWartok(ram, victim))
        return;
    const uint32_t generator = findResource(ram, kWartokPieces, kGeneratorClass);
    if (generator == 0xffffffffu)
        return;

    const uint32_t sp = getRegU32(c, 29) - kFrame, transform = sp + 0x10u;
    std::memcpy(ram + (transform & PS2_RAM_MASK), ram + ((victim.actor + 0x14u) & PS2_RAM_MASK), 0x30);
    const float y = std::bit_cast<float>(guest::readHeap32(ram, transform + 4u)) + kPieceOriginLift;
    guest::writeHeap32(ram, transform + 4u, std::bit_cast<uint32_t>(y));
    guest::writeHeap32(ram, sp + 0x40u, getRegU32(c, 31));
    guest::writeHeap32(ram, sp + 0x44u, victim.actor);
    guest::writeHeap32(ram, sp + 0x48u, victim.data);
    guest::writeHeap32(ram, sp + 0x4cu, victim.resource);
    guest::writeHeap32(ram, sp + 0x50u, victim.registryRef);
    SET_GPR_U32(c, 29, sp);
    SET_GPR_U32(c, 4, guest::readHeap32(ram, kContextPtr));
    SET_GPR_U32(c, 5, victim.actor);
    SET_GPR_U32(c, 6, transform);
    SET_GPR_U32(c, 7, 0u);
    SET_GPR_U32(c, 8, 0u);
    SET_GPR_U32(c, 9, generator);
    SET_GPR_U32(c, 31, g_breakupResult);
    g_breakupContext = c;
    g_breakupSpawnedPiece = false;
    c->pc = kFactory;
}

void finishBreakup(uint8_t* ram, R5900Context* c, PS2Runtime*) {
    const uint32_t sp = getRegU32(c, 29), ra = guest::readHeap32(ram, sp + 0x40u);
    SET_GPR_U32(c, 29, sp + kFrame);
    SET_GPR_U32(c, 31, ra);
    c->pc = ra;
    const Breakup victim{guest::readHeap32(ram, sp + 0x44u), guest::readHeap32(ram, sp + 0x48u),
                         guest::readHeap32(ram, sp + 0x4cu), guest::readHeap16(ram, sp + 0x50u)};
    const bool generated = getRegU32(c, 2) || (g_breakupContext == c && g_breakupSpawnedPiece);
    g_breakupContext = nullptr;
    g_breakupSpawnedPiece = false;
    if (generated && isSameDeadWartok(ram, victim)) {
        logf("Wartok %x broke apart after fatal secondary flame damage", victim.actor);
        SET_GPR_U32(c, 4, victim.actor);
        SET_GPR_U32(c, 5, 0u);
        c->pc = kSetActorEnabled;
    }
}

void installWartokBreakup(PS2Runtime& rt) {
    if (!g_factory || !rt.hasFunction(kSetActorEnabled) || !rt.hasFunction(kNpcDeath))
        return;
    for (uint32_t addr = 0x100500u; addr < 0x100600u && !g_breakupResult; addr += 4) {
        if (rt.hasFunction(addr))
            continue;
        if (!g_breakupStart && rt.registerFunction(addr, &startBreakup))
            g_breakupStart = addr;
        else if (g_breakupStart && rt.registerFunction(addr, &finishBreakup))
            g_breakupResult = addr;
    }
    if (!g_breakupResult)
        return;
    g_npcDeath = rt.lookupFunction(kNpcDeath);
    rt.replaceFunction(kNpcDeath, &npcDeath);
    const auto clear = [](const drakan_lifecycle::Event&) { g_breakups.clear(); };
    drakan_lifecycle::subscribe(drakan_lifecycle::Kind::BeforeLevelLoad, clear);
    drakan_lifecycle::subscribe(drakan_lifecycle::Kind::BeforeSaveRestore, clear);
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
    const uint16_t ref = guest::readHeap16(rdram, dragon + kRider);
    if (!ref || ref > guest::readHeap32(rdram, ctx + 0x84u) + 1u)
        return;
    const uint32_t rider = guest::readHeap32(rdram, guest::readHeap32(rdram, ctx + 0x7cu) + (ref - 1u) * 8u);
    if (classOf(rdram, rider) != kRynnClass)
        return;
    const uint32_t data = dataOf(rdram, rider);
    const uint32_t f = guest::readHeap32(rdram, data + kFlags2);
    if (!ramPtr(data, kFlags2 + 4) || (f & (kMounted | kAttackBusy)) != (kMounted | kAttackBusy))
        return;
    guest::writeHeap32(rdram, data + kFlags2, f & ~kAttackBusy);
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
        if (rt.hasFunction(kFactory)) {
            g_factory = rt.lookupFunction(kFactory);
            rt.replaceFunction(kFactory, &spawnEffect);
        }
        installWartokBreakup(rt);
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
        logf("dragon %x: stand-in breath given, list count %u, entry %x / %x", g_tried,
             guest::readHeap32(rdram, d + kBreathCount),
             guest::readHeap32(rdram, guest::readHeap32(rdram, d + kBreathList)),
             guest::readHeap32(rdram, guest::readHeap32(rdram, d + kBreathList) + 4u));
    }
    if (g_inFlight && !guest.postBusy() && g_ticks > g_postTick + 4)
        g_inFlight = false;
    if (drakan_lifecycle::transitionActive()) {
        g_tried = 0; // handles are reused by the next level or load
        return;
    }
    if (!gameplay)
        return;
    if (!g_breakups.empty() && !guest.postBusy()) {
        const auto& victim = g_breakups.front();
        drakan_input::GuestCall call(g_breakupStart);
        call.arg(victim.actor).arg(victim.data).arg(victim.resource).arg(victim.registryRef);
        if (guest.post(call))
            g_breakups.pop_front();
    }
    const uint32_t ctx = guest::readHeap32(rdram, kContextPtr);
    const uint32_t h = ramPtr(ctx, 0x4b4) ? guest::readHeap32(rdram, ctx + 0x4b0u) : 0;
    if (classOf(rdram, h) != kDragonClass)
        return;
    const uint32_t dragon = dataOf(rdram, h);
    if (!ramPtr(dragon, kBreathCount + 4))
        return;
    releaseRider(rdram, ctx, dragon);

    if (!g_give || g_inFlight || guest.postBusy() || h == g_tried ||
        drakan_game::current() != drakan_game::Game::Ootf || drakan_arena::active())
        return;
    if (guest::readHeap32(rdram, dragon + kBreathCount) != 0 || guest::readHeap32(rdram, dragon + kBreathList) != 0 ||
        guest::readHeap32(rdram, dragon + kBreathChosen) != 0xffffffffu)
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
