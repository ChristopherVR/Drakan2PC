#pragma once
#include <cmath>
#include <cstdint>

namespace drakan_ws::detail {
// Message boxes (1, 2, 3, 0x1b) stack on the screen below them (parent at
// screen+0x34). Over the inventory, e.g. each section's first-visit help, the
// world behind them is still the inventory's cropped world.
inline int inventoryCropScreen(int active, int parent) {
    const bool box = active == 1 || active == 2 || active == 3 || active == 0x1b;
    return box && parent == 21 ? 21 : active;
}

// 2c4400 applies the screen manager's crop through renderer vtable slot8c
// (132f60). Inventory's own grid/preview calls use different return PCs.
inline bool inventoryWorldCrop(uint32_t pc, uint32_t caller, int screen, uint32_t object, uint32_t renderer,
                               uint32_t frameWidth, uint32_t viewportWidth, int32_t offset, float hudScale,
                               float& scale) {
    if (pc != 0x132f60u || caller != 0x2c4444u || screen != 21 || !renderer || object != renderer || frameWidth < 2 ||
        frameWidth > 2048 || viewportWidth != frameWidth || offset < 0 || offset > int32_t(frameWidth) ||
        !std::isfinite(hudScale) || hudScale <= 0 || hudScale >= 1 || !std::isfinite(scale) || scale <= 0 || scale >= 1)
        return false;
    const float half = float(frameWidth) * .5f;
    // In framebuffer coordinates: centre = W/2 + offset, left = centre-W*s/2.
    // Left-anchored GUI is compressed by hudScale. Extend the crop to the
    // mapped native left edge (with the same native overlap behind the GUI).
    const float left = half + float(offset) - half * scale;
    if (!(left > 0 && left < float(frameWidth)))
        return false;
    const float expanded = scale + left * (1 - hudScale) / half;
    if (!std::isfinite(expanded) || expanded <= scale)
        return false;
    // 132f60 changes extent by s and projectionX by 1/s. Their product and
    // +100 centre stay fixed, so this reveals geometry without stretching or
    // translating the character. CPU/VU clipping consume the expanded extent.
    scale = expanded;
    return true;
}
} // namespace drakan_ws::detail
