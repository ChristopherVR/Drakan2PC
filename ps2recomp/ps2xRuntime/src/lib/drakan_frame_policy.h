#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace drakan_frame_rate {
// Only the graphics worker's SleepThread call participates. Other threads,
// resumed syscalls, counter overflow and the original cadence are unchanged.
constexpr uint32_t cadenceCount(uint32_t count, bool higher, uint32_t pc, uint32_t ra) {
    return higher && pc == 0x4bc080u && ra == 0x1d7a24u && count >= 2 && count < 4 ? 4 : count;
}

// Scene objects (vtable 51b8b0) keep their per-vertex colours at +78. The pass 189288 frees the colours of objects
// far from the camera (18ba28) through vtable +11c (17e478, called at 18938c); a later draw (189b38) rebuilds them
// from the lights it gathers. An object with no layer (+7c = -1) and no light link (*(+a0) or its slot 1 empty) gathers
// none, so the draw clears its lighting flags +74 for good and it is drawn with the default colour 0x7f. The inventory
// page grid is such an object: 13af38 lights it once when the page is built. At the faster cadence the pass can run
// between that and the grid's first draw, so the grid lost its lines on some opens.
inline constexpr uint32_t kFarLightingRelease = 0x18938cu; // return address of the vtable +11c call in 189288
inline constexpr uint32_t kSceneObjectVtable = 0x51b8b0u;

struct ObjectLighting {
    uint32_t vtable = 0, layer = 0, lightSource = 0, colours = 0; // +0, +7c, (*(+a0))[1] or 0, +78
};

// True when the far-object pass is about to free colours that no draw can rebuild.
constexpr bool keepUnrebuildableLighting(uint32_t ra, const ObjectLighting& object) {
    return ra == kFarLightingRelease && object.vtable == kSceneObjectVtable && object.layer == 0xffffffffu &&
           object.lightSource == 0 && object.colours != 0;
}

// Engine tick time while presentation is vsync-locked. Every shown frame lasts whole refreshes, but 1c3028 reads the
// 1 ms game clock wherever the EE happens to be after the previous present, so release jitter moved the world 14 then
// 19 ms per 16.7 ms frame. Give the tick whole refreshes and carry the difference into the next one (total game time
// stays the clock's within half a refresh). A tick shorter than half a refresh (a catch-up after a stall) is off the
// grid: it passes through and drops the carry. `carry`: seconds measured but not yet given, may be negative.
inline double snapTickToRefresh(double measured, double refresh, double& carry) {
    if (!(measured >= 0.0) || !(refresh > 0.0)) {
        carry = 0.0;
        return measured;
    }
    const double due = measured + carry;
    if (due < refresh * 0.5) {
        carry = 0.0;
        return measured;
    }
    const double given = std::round(due / refresh) * refresh;
    carry = due - given;
    return given;
}
} // namespace drakan_frame_rate
