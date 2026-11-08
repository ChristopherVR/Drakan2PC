// Drakan: environmental deaths in a multiplayer session (drakan_mp_death.h).
#include "drakan_mp_death.h"
#include "drakan_arena.h"
#include "drakan_netplay.h"
#include "drakan_region.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace drakan_mp_death {
namespace {

constexpr uint32_t kHpSetter = 0x3d5710u;    // (stats block, new HP): the leaf of the damage method 0x314ab0
constexpr uint32_t kStatsBlock = 0x4fbac8u;  // the local player's stats
constexpr uint32_t kHpKey = 0x4fe86cu;       // HP is stored as rotl8(v) ^ key
constexpr uint32_t kHpField = 0xcu;

PS2Runtime::RecompiledFunction g_origSetter = nullptr;
std::atomic<bool> g_pending{false};

uint32_t rd32(const uint8_t* ram, uint32_t a) {
    a &= 0x1ffffffu;
    uint32_t v = 0;
    if (a + 4 <= PS2_RAM_SIZE)
        std::memcpy(&v, ram + a, 4);
    return v;
}

int32_t storedHp(const uint8_t* rdram) {
    const uint32_t raw = rd32(rdram, drakan_region::guestAddress(kStatsBlock) + kHpField) ^
                         rd32(rdram, drakan_region::guestAddress(kHpKey));
    return static_cast<int32_t>((raw >> 8) | (raw << 24));
}

bool sessionActive() {
    return drakan_arena::active() || drakan_netplay::coopLink().active;
}

void hookHpSetter(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t stats = getRegU32(c, 4);
    const int32_t newHp = static_cast<int32_t>(getRegU32(c, 5));
    if (newHp <= 0 && interceptsDeath(sessionActive(), stats == drakan_region::guestAddress(kStatsBlock), newHp,
                                      storedHp(rdram))) {
        std::fprintf(stderr, "[mpdeath] lethal damage in a session (HP %d -> %d): kept at %d, respawn queued\n",
                     storedHp(rdram), newHp, kSurviveHp);
        SET_GPR_U32(c, 5, static_cast<uint32_t>(kSurviveHp));
        g_pending.store(true);
    }
    g_origSetter(rdram, c, rt);
}

} // namespace

void install(PS2Runtime& rt) {
    const uint32_t a = drakan_region::guestAddress(kHpSetter);
    if (!a || !drakan_region::guestAddress(kStatsBlock) || !rt.hasFunction(a) || !(g_origSetter = rt.lookupFunction(a))) {
        std::fprintf(stderr, "[mpdeath] disabled: no HP setter for this disc profile\n");
        return;
    }
    rt.replaceFunction(a, &hookHpSetter);
}

bool takeDeath() {
    return g_pending.exchange(false);
}

} // namespace drakan_mp_death
