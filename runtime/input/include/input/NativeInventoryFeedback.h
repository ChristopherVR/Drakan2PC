#pragma once
#include "input/GuestInterface.h"
#include <cmath>

namespace drakan_input::native_feedback {
inline bool span(uint32_t p, uint32_t n) {
    return !(p & 3) && p >= 0x100000 && p <= 0x2000000 - n;
}
// SCES_500.06: 435f94 calls 2c8ff8(x,y,w,h,t0=packedColours[4]).
// 4372d0..e4 sets flag 2 only for controller+20's selected item.
// 435f24..40 chooses property CB/CC using flag 0x10; preserve CC semantics.
inline bool selectionBorder(IGuest& g, uint32_t caller, uint32_t colours, uint32_t flags) {
    if (caller != 0x435f9c || (flags & 0x12) != 2 || !span(colours, 16))
        return false;
    for (unsigned i = 0; i < 4; ++i) {
        const uint32_t native = g.read32(colours + i * 4);
        // Warm brass with a light edge; keep the native PS2 alpha byte intact.
        g.write32(colours + i * 4, (native & 0xff000000u) | 0x0070d6f4u);
    }
    return true;
}
// 42b3d0..494 builds the native nine-float block; +18/+1c/+20
// are initialized to 0.8. 42a528..554 copies the two RGB triplets.
// The first three floats and middle triplet are deliberately untouched.
inline bool categoryTint(IGuest& g, uint32_t caller, uint32_t params, bool hovered) {
    if (caller != 0x42b4a0 || !hovered || !span(params, 36))
        return false;
    for (unsigned i = 0; i < 3; ++i)
        if (std::fabs(g.readF(params + 0x18 + i * 4) - 0.8f) > 0.0001f ||
            !std::isfinite(g.readF(params + 0x18 + i * 4)))
            return false;
    g.writeF(params + 0x18, 1.0f);
    g.writeF(params + 0x1c, 0.90f);
    g.writeF(params + 0x20, 0.66f);
    return true;
}
} // namespace drakan_input::native_feedback
