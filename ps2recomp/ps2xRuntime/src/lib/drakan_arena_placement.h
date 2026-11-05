// Native motion bookkeeping for an explicit arena relocation, not an ordinary physics tick.
#pragma once

#include <cmath>
#include <cstdint>

namespace drakan_arena::detail {

inline bool placementPointer(uint32_t address, uint32_t bytes) {
    return address >= 0x100000u && bytes <= 0x2000000u && address <= 0x2000000u - bytes;
}

// The native velocity query reads handle+0x50 (skeleton), previous position +0x174,
// and collision-resolved tick displacement +0xc8. A host teleport must rebase both
// before the next query can interpret the relocation as a physical impact.
// PAL Rynn also caches speed at player+8 and landing impact speed at player+0xb9c.
// No health, airborne state, gravity, damage thresholds or later physics writes change.
template <class Guest>
bool resetPlacementMotion(Guest& guest, uint32_t actor, const float (&position)[3], bool palRynnLayout) {
    if (!placementPointer(actor, 0x100) || !std::isfinite(position[0]) || !std::isfinite(position[1]) ||
        !std::isfinite(position[2]))
        return false;
    const uint32_t skeleton = guest.read32(actor + 0x50);
    if (!placementPointer(skeleton, 0x180) || guest.read32(skeleton + 0xd4) != actor)
        return false;
    for (uint32_t i = 0; i < 3; ++i) {
        guest.writeF(skeleton + 0x174 + 4 * i, position[i]);
        guest.writeF(skeleton + 0xc8 + 4 * i, 0.0f);
    }
    if (palRynnLayout) {
        const uint32_t inner = guest.read32(actor + 0xfc);
        if (placementPointer(inner, 0x14)) {
            const uint32_t player = guest.read32(inner + 0x10);
            if (placementPointer(player, 0xba0) && guest.read32(player + 0x80) == actor) {
                guest.writeF(player + 8, 0.0f);
                guest.writeF(player + 0xb9c, 0.0f);
            }
        }
    }
    return true;
}

} // namespace drakan_arena::detail
