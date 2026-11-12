#pragma once
// Diagnosis only, off unless DRAKAN_MELEE_TRACE is set (docs/reference/env-flags.md): logs the player's melee swings
// as the engine sees them, to stderr. A swing is the span in which a blade (weapon data object) is live (flags +0x1c
// bit 3, set and cleared by the attack clip through 0x49a538); every tick of it the blade sweep 0x4979a8 asks the
// collision query for the actors its weapon model touches and hands each contact to 0x498020, which applies one hit
// per victim and swing (the blade's hit list, count at +0x1a0).
//   DRAKAN_MELEE_TRACE=1  one line per swing (ticks, contacts, hits, the weapon's path) and one per contact
//   DRAKAN_MELEE_TRACE=2  also one line per live tick (weapon position and axes)
// Nothing is changed: both hooks run the original first and only read guest memory afterwards. PAL addresses.
#include "drakan_region.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace drakan_melee_trace {

constexpr uint32_t kSweep = 0x4979a8u;   // (a0 blade)
constexpr uint32_t kHit = 0x498020u;     // (a0 blade, a1 contact)
constexpr uint32_t kContext = 0x5565d4u; // engine object; +0x4b0 controlled actor, +0x7c registry, +0x320 classes
constexpr uint32_t kBladeModel = 0xcu, kBladeFlags = 0x1cu, kBladeOwner = 0x60u, kBladeHits = 0x1a0u;
constexpr uint32_t kBladeLive = 8u;
constexpr int kMaxBlades = 4;
constexpr double kSwingGap = 0.12; // seconds without a live tick that end a swing

struct Swing {
    uint32_t blade = 0;
    bool live = false;
    int ticks = 0, contacts = 0, hits = 0;
    float first[3] = {}, low[3] = {}, high[3] = {}, last[3] = {};
    double started = 0, seen = 0; // seconds(): first and latest live tick
};

inline PS2Runtime::RecompiledFunction sweepOriginal = nullptr, hitOriginal = nullptr;
inline int level = 0;
inline Swing swings[kMaxBlades]; // EE game thread only

inline uint32_t word(const uint8_t* ram, uint32_t a) {
    uint32_t v = 0;
    std::memcpy(&v, ram + (a & (PS2_RAM_SIZE - 1)), 4);
    return v;
}
inline uint16_t half(const uint8_t* ram, uint32_t a) {
    uint16_t v = 0;
    std::memcpy(&v, ram + (a & (PS2_RAM_SIZE - 1)), 2);
    return v;
}
inline float real(const uint8_t* ram, uint32_t a) {
    float f = 0;
    std::memcpy(&f, ram + (a & (PS2_RAM_SIZE - 1)), 4);
    return f;
}
inline bool pointer(uint32_t a, uint32_t size) {
    return a >= 0x100000u && a < PS2_RAM_SIZE && size <= PS2_RAM_SIZE - a;
}
inline double seconds() {
    using namespace std::chrono;
    static const steady_clock::time_point start = steady_clock::now();
    return duration<double>(steady_clock::now() - start).count();
}
inline uint32_t controlled(const uint8_t* ram) {
    const uint32_t engine = word(ram, kContext);
    return pointer(engine, 0x4b4) ? word(ram, engine + 0x4b0) : 0;
}
// The registry object a 16 bit reference names (1 based; 0 = none).
inline uint32_t registryObject(const uint8_t* ram, uint32_t ref) {
    const uint32_t engine = word(ram, kContext);
    if (!ref || !pointer(engine, 0x88))
        return 0;
    const uint32_t table = word(ram, engine + 0x7c), count = word(ram, engine + 0x84);
    if (ref > count + 1 || !pointer(table, ref * 8))
        return 0;
    return word(ram, table + (ref - 1) * 8);
}
inline uint32_t classOf(const uint8_t* ram, uint32_t handle) {
    const uint32_t engine = word(ram, kContext);
    const uint32_t classes = pointer(engine, 0x324) ? word(ram, engine + 0x320) : 0;
    if (!pointer(handle, 0x14) || !pointer(classes, 0x14))
        return 0;
    const uint32_t index = half(ram, handle + 0x10);
    if (index == 0xffffu || index > word(ram, classes + 0x10))
        return 0;
    const uint32_t record = word(ram, classes + 8) + index * 0x24;
    return pointer(record, 0x24) ? word(ram, record + 8) : 0;
}
// The data object of a registry object (handle +0xfc -> +0x10).
inline uint32_t dataOf(const uint8_t* ram, uint32_t handle) {
    const uint32_t holder = pointer(handle, 0x100) ? word(ram, handle + 0xfc) : 0;
    return pointer(holder, 0x14) ? word(ram, holder + 0x10) : 0;
}
// A blade the player swings: the weapon in her right or left hand holder (player data +0xeb4 / +0xe90, item reference
// at +0x1c), or one that names her as its owner (+0x60; the engine clears that reference at times, so it is not
// enough).
inline bool playersBlade(const uint8_t* ram, uint32_t blade) {
    if (!pointer(blade, 0x1a4))
        return false;
    const uint32_t me = controlled(ram);
    if (!me)
        return false;
    if (registryObject(ram, half(ram, blade + kBladeOwner)) == me)
        return true;
    const uint32_t player = dataOf(ram, me);
    if (!pointer(player, 0xed4))
        return false;
    return blade == dataOf(ram, registryObject(ram, half(ram, player + 0xeb4u + 0x1cu))) ||
           blade == dataOf(ram, registryObject(ram, half(ram, player + 0xe90u + 0x1cu)));
}
// Hit points of an NPC (data +0x24c when data +0x23c names the object), else 0.
inline int32_t hitPoints(const uint8_t* ram, uint32_t handle) {
    const uint32_t data = dataOf(ram, handle);
    if (!pointer(data, 0x250) || word(ram, data + 0x23c) != handle)
        return 0;
    return static_cast<int32_t>(word(ram, data + 0x24c));
}
inline Swing* swingOf(uint32_t blade, bool make) {
    for (Swing& s : swings)
        if (s.blade == blade)
            return &s;
    if (!make)
        return nullptr;
    for (Swing& s : swings)
        if (!s.live) {
            s = Swing{};
            s.blade = blade;
            return &s;
        }
    return nullptr;
}
inline void endSwing(const uint8_t* ram, Swing& s) {
    const uint32_t me = controlled(ram);
    const float y = pointer(me, 0x20) ? real(ram, me + 0x18) : 0.0f;
    std::fprintf(stderr,
                 "[melee] swing blade %x: %.2f s, %d ticks, %d contacts, %d hits; weapon height above the player's "
                 "origin %.0f .. %.0f, first (%.0f %.0f %.0f) last (%.0f %.0f %.0f)\n",
                 s.blade, s.seen - s.started, s.ticks, s.contacts, s.hits, static_cast<double>(s.low[1] - y),
                 static_cast<double>(s.high[1] - y), static_cast<double>(s.first[0]), static_cast<double>(s.first[1]),
                 static_cast<double>(s.first[2]), static_cast<double>(s.last[0]), static_cast<double>(s.last[1]),
                 static_cast<double>(s.last[2]));
    s.live = false;
    s.blade = 0;
}

// The player's own state, once per sweep call of one of her blades: hit points lost (the stats block, decoded as
// drakan_pvp.cpp does) and the attack flag (player data +0x94 bit 7). An attack that ends without its blade ever having
// been live was cancelled before it could hit.
struct Attack {
    bool on = false, armed = false, struck = false;
    double started = 0;
    int32_t hp = -1;
};
inline Attack attack; // EE game thread only
inline int32_t playerHitPoints(const uint8_t* ram) {
    const uint32_t x = word(ram, 0x4fbac8u + 0xcu) ^ word(ram, 0x4fe86cu);
    return static_cast<int32_t>((x >> 8) | (x << 24));
}
inline void watchPlayer(const uint8_t* ram, bool bladeLive, double now) {
    const uint32_t player = dataOf(ram, controlled(ram));
    if (!pointer(player, 0x98))
        return;
    const uint32_t state = word(ram, player + 0x94);
    const bool on = (state & 0x80u) != 0;
    const int32_t hp = playerHitPoints(ram);
    if (attack.hp >= 0 && hp < attack.hp && hp >= -1000) {
        std::fprintf(stderr, "[melee] player hit %.3f: %d -> %d, attacking %d, blade live %d, state %x\n", now,
                     attack.hp, hp, on ? 1 : 0, bladeLive ? 1 : 0, state);
        if (attack.on)
            attack.struck = true;
    }
    attack.hp = hp;
    if (on && !attack.on) {
        attack.started = now;
        attack.armed = attack.struck = false;
    }
    if (on && bladeLive)
        attack.armed = true;
    if (!on && attack.on)
        std::fprintf(stderr, "[melee] attack %.3f: flag held %.2f s, blade %s%s\n", now, now - attack.started,
                     attack.armed ? "went live" : "never live (cancelled)",
                     attack.struck ? ", player hit during it" : "");
    attack.on = on;
}

// Damage is counted from hit points: the victims the player's blades touched are remembered, and every sweep call
// compares their hit points with the last value seen (the hit application does not always change them inside
// 0x498020, and the blade's hit list does not grow for a victim it already holds).
struct Victim {
    uint32_t handle = 0, blade = 0;
    int32_t hp = 0;
};
inline Victim victims[8]; // EE game thread only
inline void noteVictim(const uint8_t* ram, uint32_t handle, uint32_t blade, int32_t hpBefore) {
    Victim* slot = nullptr;
    for (Victim& v : victims)
        if (v.handle == handle)
            slot = &v;
    if (!slot)
        for (Victim& v : victims)
            if (!v.handle || hitPoints(ram, v.handle) <= 0) {
                slot = &v;
                slot->hp = hpBefore;
                break;
            }
    if (!slot)
        return;
    slot->handle = handle;
    slot->blade = blade;
}
inline void watchVictims(const uint8_t* ram, double now) {
    for (Victim& v : victims) {
        if (!v.handle)
            continue;
        const int32_t hp = hitPoints(ram, v.handle);
        if (hp < v.hp) {
            std::fprintf(stderr, "[melee] damage %.3f victim %x class %x: %d -> %d (blade %x)\n", now, v.handle,
                         classOf(ram, v.handle), v.hp, hp, v.blade);
            for (Swing& s : swings)
                if (s.blade == v.blade)
                    ++s.hits;
        }
        v.hp = hp;
        if (hp <= 0)
            v.handle = 0;
    }
}

inline void sweepHook(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    if (ctx->pc != kSweep) {
        sweepOriginal(ram, ctx, rt);
        return;
    }
    const uint32_t blade = GPR_U32(ctx, 4), ra = GPR_U32(ctx, 31);
    const bool mine = playersBlade(ram, blade);
    const bool live = mine && (word(ram, blade + kBladeFlags) & kBladeLive);
    const double now = seconds();
    if (mine) {
        watchPlayer(ram, live, now);
        watchVictims(ram, now);
    }
    if (level >= 2 && !mine && pointer(blade, 0x1a4) && (word(ram, blade + kBladeFlags) & kBladeLive)) {
        static int lines = 0; // other owners' live blades (enemy weapons), a sample only
        if (lines++ < 40)
            std::fprintf(stderr, "[melee] other live blade %x: owner ref %u object %x, controlled %x\n", blade,
                         half(ram, blade + kBladeOwner), registryObject(ram, half(ram, blade + kBladeOwner)),
                         controlled(ram));
    }
    for (Swing& old : swings) // the sweep is not called for every dead blade: a gap in the live ticks ends a swing
        if (old.live && now - old.seen > kSwingGap)
            endSwing(ram, old);
    Swing* s = mine ? swingOf(blade, live) : nullptr;
    if (s && live && !s->live) {
        s->live = true;
        s->started = s->seen = now;
    }
    sweepOriginal(ram, ctx, rt);
    if (ctx->pc != ra || !s)
        return;
    if (!live) {
        if (s->live)
            endSwing(ram, *s);
        return;
    }
    const uint32_t model = word(ram, blade + kBladeModel);
    if (!pointer(model, 0x44))
        return;
    float p[3], m[9];
    for (uint32_t i = 0; i < 3; ++i)
        p[i] = real(ram, model + 0x14 + 4 * i);
    for (uint32_t i = 0; i < 9; ++i)
        m[i] = real(ram, model + 0x20 + 4 * i);
    if (s->ticks == 0)
        for (int i = 0; i < 3; ++i)
            s->first[i] = s->low[i] = s->high[i] = p[i];
    for (int i = 0; i < 3; ++i)
        s->last[i] = p[i];
    if (p[1] < s->low[1])
        for (int i = 0; i < 3; ++i)
            s->low[i] = p[i];
    if (p[1] > s->high[1])
        for (int i = 0; i < 3; ++i)
            s->high[i] = p[i];
    ++s->ticks;
    s->seen = now;
    if (level >= 2)
        std::fprintf(
            stderr,
            "[melee] tick %.3f blade %x at (%.0f %.0f %.0f) axes (%.2f %.2f %.2f) (%.2f %.2f %.2f) (%.2f %.2f %.2f)\n",
            seconds(), blade, static_cast<double>(p[0]), static_cast<double>(p[1]), static_cast<double>(p[2]),
            static_cast<double>(m[0]), static_cast<double>(m[1]), static_cast<double>(m[2]), static_cast<double>(m[3]),
            static_cast<double>(m[4]), static_cast<double>(m[5]), static_cast<double>(m[6]), static_cast<double>(m[7]),
            static_cast<double>(m[8]));
}

inline void hitHook(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    if (ctx->pc != kHit) {
        hitOriginal(ram, ctx, rt);
        return;
    }
    const uint32_t blade = GPR_U32(ctx, 4), contact = GPR_U32(ctx, 5), ra = GPR_U32(ctx, 31);
    const bool mine = playersBlade(ram, blade);
    Swing* s = mine ? swingOf(blade, false) : nullptr;
    const uint32_t before = mine ? word(ram, blade + kBladeHits) : 0;
    // Read the contact before the call: the record is the sweep's stack and result list.
    uint32_t victim = 0, cls = 0;
    int32_t damage = 0;
    float at[3] = {}, vp[3] = {};
    int32_t hpBefore = 0;
    if (mine && pointer(contact, 0x2c)) {
        damage = static_cast<int32_t>(word(ram, contact));
        victim = word(ram, contact + 0x20);
        const uint32_t slot = word(ram, contact + 0x24);
        const uint32_t node = pointer(slot, 4) ? word(ram, slot) : 0;
        if (pointer(node, 0x14))
            for (uint32_t i = 0; i < 3; ++i)
                at[i] = real(ram, node + 8 + 4 * i);
        if (pointer(victim, 0x20)) {
            hpBefore = hitPoints(ram, victim);
            cls = classOf(ram, victim);
            for (uint32_t i = 0; i < 3; ++i)
                vp[i] = real(ram, victim + 0x14 + 4 * i);
        }
    }
    if (mine && victim && hitPoints(ram, victim) > 0)
        noteVictim(ram, victim, blade, hpBefore);
    hitOriginal(ram, ctx, rt);
    if (ctx->pc != ra || !mine)
        return;
    // A hit: the victim lost hit points in the call, or the blade's hit list (+0x1a0) grew. The list alone is not
    // enough: it does not grow for a victim it still holds from an earlier swing, though that swing's first contact
    // does damage.
    const uint32_t after = word(ram, blade + kBladeHits);
    const int32_t hpAfter = hitPoints(ram, victim);
    const bool fresh = after > before || hpAfter < hpBefore;
    if (s) {
        ++s->contacts;
    }
    if (fresh || level >= 2)
        std::fprintf(stderr,
                     "[melee] contact %.3f blade %x victim %x class %x damage %d %s; contact (%.0f %.0f %.0f), victim "
                     "origin (%.0f %.0f %.0f), height above it %.0f; hit points %d -> %d, hit list %u -> %u\n",
                     seconds(), blade, victim, cls, damage, fresh ? "HIT" : "(no new hit)", static_cast<double>(at[0]),
                     static_cast<double>(at[1]), static_cast<double>(at[2]), static_cast<double>(vp[0]),
                     static_cast<double>(vp[1]), static_cast<double>(vp[2]), static_cast<double>(at[1] - vp[1]),
                     hpBefore, hpAfter, before, after);
}

inline void install(PS2Runtime& rt) {
    const char* e = std::getenv("DRAKAN_MELEE_TRACE");
    if (!e || !*e || e[0] == '0' || !drakan_region::build().identityAddresses) // SCES-500.06 addresses only
        return;
    if (!rt.hasFunction(kSweep) || !rt.hasFunction(kHit))
        return;
    level = e[0] == '2' ? 2 : 1;
    sweepOriginal = rt.lookupFunction(kSweep);
    rt.replaceFunction(kSweep, sweepHook);
    hitOriginal = rt.lookupFunction(kHit);
    rt.replaceFunction(kHit, hitHook);
    std::fprintf(stderr, "[melee] trace level %d installed\n", level);
}

} // namespace drakan_melee_trace
