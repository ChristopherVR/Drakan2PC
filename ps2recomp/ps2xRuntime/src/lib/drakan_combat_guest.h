#pragma once

#include "drakan_region.h"
#include "input/GuestInterface.h"

namespace drakan_combat_guest {

inline constexpr uint32_t kPlayerDamage = 0x314ab0u;

// playerData is the live on-foot player's instance data. Keep the scratch word intact until the call returns.
// The native method loads effects and sounds, so run it on the guest scheduler.
inline bool postPlayerDamage(drakan_input::IGuest& guest, uint32_t playerData, uint32_t damage) {
    const uint32_t scratch = drakan_region::guestAddress(0x5b8480u);
    const uint32_t method = drakan_region::guestAddress(kPlayerDamage);
    if (!playerData || !scratch || !method || guest.postBusy())
        return false;
    guest.write32(scratch, damage);
    return guest.post(drakan_input::GuestCall(method).arg(playerData).arg(scratch).arg(0).arg(0).arg(0).arg(1));
}

} // namespace drakan_combat_guest
