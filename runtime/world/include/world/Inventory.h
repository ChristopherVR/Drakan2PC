#pragma once

#include "world/Addresses.h"

#include <cstdint>

namespace world {

// Read-only ownership check before exposing a dynamic actor to world collision queries.
template <typename Read>
bool inventoryReferencesItem(uint32_t actorClass, uint32_t playerData, uint32_t handle, Read read) {
    if (actorClass != kClassRynn || !playerData || !handle)
        return false;
    for (uint32_t i = 0; i < 24; ++i)
        if (read(playerData + 0x584u + 0x30u + i * 16u + 8u) == handle)
            return true;
    for (uint32_t i = 0; i < 4; ++i)
        if (read(playerData + 0xa48u + i * 16u) == handle)
            return true;
    return false;
}

// Remove references before a replicated world item is queued for destruction. A peer may
// already have collected its local copy while the authoritative removal was in flight.
// Only the native Rynn class owns this layout; mounted dragons and cosmetic NPCs do not.
// playerData is validated by the caller; accessors operate on guest addresses.
template <typename Read, typename Write>
unsigned detachInventoryItem(uint32_t actorClass, uint32_t playerData, uint32_t handle, Read read, Write write) {
    if (actorClass != kClassRynn || !playerData || !handle)
        return 0;
    const uint32_t controller = playerData + 0x584u;
    unsigned removed = 0;
    for (uint32_t i = 0; i < 24; ++i) {
        const uint32_t cell = controller + 0x30u + i * 16u;
        if (read(cell + 8) == handle) {
            for (uint32_t offset = 0; offset < 16; offset += 4)
                write(cell + offset, 0u);
            ++removed;
        }
    }
    for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t slot = playerData + 0xa48u + i * 16u;
        if (read(slot) == handle) {
            write(slot, 0u);
            ++removed;
        }
    }
    if (removed)
        write(controller + 0x238u, 1u);
    return removed;
}

} // namespace world
