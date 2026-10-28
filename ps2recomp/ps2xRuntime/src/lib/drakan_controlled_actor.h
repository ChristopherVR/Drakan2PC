// SCES_500.06: native 0x3d2798 identifies dragon control from the current
// context actor's resource class, not from the last optional camera-look call.
#pragma once
#include <cstdint>

namespace drakan_controlled_actor {
inline bool ram(uint32_t address, uint32_t bytes) {
    const uint32_t p = address & 0x1fffffffu;
    return p >= 0x100000u && p < 0x2000000u && bytes <= 0x2000000u - p;
}
struct State {
    bool dragon = false;
    bool dragonGameplay = false;
};
template <class Guest>
State read(Guest& g) {
    State result;
    const uint32_t context = g.read32(0x5565d4u);
    if (!ram(context, 0x4b8))
        return result;
    const uint32_t handle = g.read32(context + 0x4b0);
    if (!ram(handle, 0x100))
        return result;
    const uint32_t resources = g.read32(context + 0x320);
    if (!ram(resources, 0x14))
        return result;
    const uint32_t index = g.read16(handle + 0x10);
    // Native resource count is an inclusive limit (0x3d2798).
    if (index == 0xffffu || index > g.read32(resources + 0x10))
        return result;
    const uint32_t table = g.read32(resources + 8);
    const uint32_t offset = index * 0x24u;
    if (!ram(table, offset + 0x24u))
        return result;
    result.dragon = g.read32(table + offset + 8) == 0x1001cu;
    const uint32_t inner = g.read32(handle + 0xfc);
    const uint32_t data = ram(inner, 0x14) ? g.read32(inner + 0x10) : 0;
    const uint32_t cameraHandle = g.read32(context + 0x4b4);
    const uint32_t cameraInner = ram(cameraHandle, 0x100) ? g.read32(cameraHandle + 0xfc) : 0;
    const uint32_t camera = ram(cameraInner, 0x14) ? g.read32(cameraInner + 0x10) : 0;
    if (!result.dragon || !ram(data, 0x98) || !ram(camera, 0x150))
        return result;
    const uint32_t cameraMode = g.read32(camera + 0x14c);
    result.dragonGameplay =
        (cameraMode == 1 || cameraMode == 6) && g.read32(0x4fbafcu) == 0 && g.read32(0x4fb730u) != 0;
    return result;
}
} // namespace drakan_controlled_actor
