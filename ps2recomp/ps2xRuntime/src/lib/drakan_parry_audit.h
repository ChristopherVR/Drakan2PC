// Private SCES_500.06 observer. No guest calls, writes, timing or input changes.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>

namespace drakan_parry_audit {
struct Ram {
    const uint8_t* bytes;
    bool valid(uint32_t a, uint32_t n = 4) const {
        a &= 0x1fffffffu;
        return a >= 0x100000u && a < 0x2000000u && n <= 0x2000000u - a;
    }
    template <class T>
    T read(uint32_t a) const {
        T v{};
        if (valid(a, sizeof(T)))
            std::memcpy(&v, bytes + (a & 0x1fffffffu), sizeof(T));
        return v;
    }
    uint32_t u(uint32_t a) const { return read<uint32_t>(a); }
    uint16_t h(uint32_t a) const { return read<uint16_t>(a); }
    float f(uint32_t a) const {
        const float v = read<float>(a);
        return std::isfinite(v) ? v : 0.f;
    }
    uint32_t data(uint32_t handle) const {
        if (!valid(handle, 0x100))
            return 0;
        const auto inner = u(handle + 0xfc);
        return valid(inner, 0x14) ? u(inner + 0x10) : 0;
    }
    uint32_t resource(uint32_t handle) const {
        const auto c = u(0x5565d4), r = valid(c, 0x324) ? u(c + 0x320) : 0;
        if (!valid(handle, 0x12) || !valid(r, 0x14))
            return 0;
        const auto ix = h(handle + 0x10);
        if (ix == 0xffff || ix > u(r + 0x10))
            return 0;
        const uint64_t a = uint64_t(u(r + 8)) + uint64_t(ix) * 0x24;
        return a <= 0xffffffffu && valid(uint32_t(a), 0x24) ? u(uint32_t(a) + 8) : 0;
    }
    uint32_t reference(uint16_t ref) const {
        const auto c = u(0x5565d4);
        if (!valid(c, 0x88) || !ref || ref > u(c + 0x84))
            return 0;
        const uint64_t a = uint64_t(u(c + 0x7c)) + uint64_t(ref - 1) * 8;
        return a <= 0xffffffffu && valid(uint32_t(a), 8) ? u(uint32_t(a)) : 0;
    }
    // Native getter 0x192e48: props+4 -> word array. Only decode this known
    // getter, zero this-adjustment, and in-range index; never guess another ABI.
    uint32_t property(uint32_t handle, uint32_t index) const {
        if (!valid(handle, 0x58))
            return 0;
        const auto p = u(handle + 0x54), vt = valid(p, 12) ? u(p + 8) : 0;
        if (!valid(vt, 0x20) || h(vt + 0x18) || u(vt + 0x1c) != 0x192e48 || index >= u(p))
            return 0;
        const uint64_t a = uint64_t(u(p + 4)) + uint64_t(index) * 4;
        return a <= 0xffffffffu && valid(uint32_t(a)) ? uint32_t(a) : 0;
    }
    int hp() const {
        const uint32_t v = u(0x4fbad4) ^ u(0x4fe86c);
        return int32_t((v >> 8) | (v << 24));
    }
    uint32_t animation(uint32_t p) const {
        if (!valid(p, 0xaf0))
            return 0;
        const auto handle = u(p + 0x80), slot = u(p + 0xaec);
        if (!valid(handle, 0x54) || slot > 4096)
            return 0;
        const auto model = u(handle + 0x50);
        if (!valid(model, 0xec))
            return 0;
        const auto map = u(model + 0xdc), rows = u(model + 0xe8);
        if (!valid(map, (slot + 1) * 2))
            return 0;
        const int ix = read<int16_t>(map + slot * 2);
        if (ix < 0 || ix > 4096)
            return 0;
        const uint64_t a = uint64_t(rows) + uint64_t(ix) * 0x80;
        return a <= 0xffffffffu && valid(uint32_t(a), 0x80) ? uint32_t(a) : 0;
    }
    uint32_t source(uint32_t attacker) const {
        if (!valid(attacker, 0x14))
            return 0;
        if (u(attacker + 0xc) != 10)
            return attacker;
        const auto d = data(attacker);
        return valid(d, 0x62) ? reference(h(d + 0x60)) : 0;
    }
    float facing(uint32_t p, uint32_t source) const {
        const auto own = valid(p, 0x84) ? u(p + 0x80) : 0;
        if (!valid(own, 0x50) || !valid(source, 0x20))
            return -2.f;
        const float dx = f(source + 0x14) - f(own + 0x14), dy = f(source + 0x18) - f(own + 0x18),
                    dz = f(source + 0x1c) - f(own + 0x1c);
        const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (len <= 0 || !std::isfinite(len))
            return -2.f;
        return -(dx * f(own + 0x28) + dy * f(own + 0x34) + dz * f(own + 0x40)) * f(own + 0x4c) / len;
    }
};

struct PendingReduction {
    unsigned id = 0;
    uint32_t player = 0, attacker = 0, stack = 0;
    int incoming = 0, result = 0;
};
inline bool hpUpdateMatches(const Ram& r, const PendingReduction& saved, uint32_t ra, uint32_t stats, int newHp,
                            uint32_t player, uint32_t stack, int amount, uint32_t damagePtr) {
    return saved.id && ra == 0x314ed0u && stats == 0x4fbac8u && saved.player == player && saved.stack == stack &&
           saved.result == amount && r.valid(damagePtr) && saved.result == int32_t(r.u(damagePtr)) &&
           newHp == r.hp() - saved.result;
}

// Installed only for DRAKAN_PARRY_AUDIT=1. Hooks use the real native routines
// and log their observed result, including incomplete/yielded returns.
inline int frame = 0;
inline bool enabled = false;
inline unsigned rows = 0, serial = 0;
inline constexpr unsigned maxRows = 10000;
inline void state(const Ram& r, const char* kind, uint32_t p, unsigned id = 0, unsigned hit = 0, uint32_t attacker = 0,
                  int incoming = 0, int result = 0, int beforeHp = 0, bool complete = true) {
    if (!enabled || !r.valid(p, 0x1108) || rows >= maxRows)
        return;
    const auto own = r.u(p + 0x80);
    if (r.resource(own) != 0x10009 || r.data(own) != p)
        return;
    ++rows;
    const auto anim = r.animation(p), src = r.source(attacker);
    const auto start = r.property(own, (r.u(p + 0x94) & 0x400) ? 0x157 : 0x155);
    const auto duration = r.property(own, (r.u(p + 0x94) & 0x400) ? 0x158 : 0x156);
    const auto reduction = r.property(own, 0x159);
    std::fprintf(stderr,
                 "[parry] "
                 "{\"kind\":\"%s\",\"f\":%d,\"id\":%u,\"hit\":%u,\"complete\":%s,\"player\":%u,\"hp\":%d,\"hp_before\":"
                 "%d,\"incoming\":%d,\"result\":%d,\"attacker\":%u,\"source\":%u,\"source_class\":%u,\"facing\":%.8g,"
                 "\"flags90\":%u,\"flags94\":%u,\"ab0\":%u,\"b28\":%u,\"e90\":%u,\"eb4\":%u,\"ed0\":%u,\"ed0_class\":%"
                 "u,\"shield_class\":%u,\"anim\":%u,\"anim_flags\":%u,\"anim_time\":%.8g,\"anim_speed\":%.8g,\"props_"
                 "valid\":%s,\"start\":%.8g,\"duration\":%.8g,\"percent\":%d}\n",
                 kind, frame, id, hit, complete ? "true" : "false", p, r.hp(), beforeHp, incoming, result, attacker,
                 src, r.resource(src), r.facing(p, src), r.u(p + 0x90), r.u(p + 0x94), r.u(p + 0xab0), r.u(p + 0xb28),
                 r.u(p + 0xe90), r.u(p + 0xeb4), r.h(p + 0xed0), r.resource(r.reference(r.h(p + 0xed0))),
                 r.resource(r.u(p + 0xa68)), anim, r.u(anim), r.f(anim + 0x58), r.f(anim + 0x54),
                 start && duration && reduction ? "true" : "false", r.f(start), r.f(duration), int32_t(r.u(reduction)));
    if (rows == maxRows)
        std::fprintf(stderr, "[parry] {\"kind\":\"truncated\"}\n");
}
} // namespace drakan_parry_audit
