// Which page rows of a render target must be refreshed from memory before a use? (pure policy, unit tested)
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace gsr::rt_sync {

// A target object is allocated with at least 512 rows, far more than the 64 or 128 rows the game draws into its
// scratch targets (shadows, distant characters, the HUD orb liquid). The rows below belong to ordinary textures the
// game uploads every frame, and refreshing them on the GPU costs width * 32 * scale^2 pixels per page row without any
// draw or sample ever looking at them. A use that names its pixel rows refreshes those only; the others keep their
// old row stamp, so the next use that does need them (or any whole-target use) still finds them stale and refreshes
// them then.
struct Rows {
    uint32_t first = 0, end = 0; // page rows [first, end)
};

// Page rows covering the pixel rows [y0, y1) of a target with `pageRows` page rows of `ph` pixel rows each.
inline Rows pageRows(uint32_t y0, uint32_t y1, uint32_t ph, uint32_t pageRowCount) {
    if (y1 <= y0 || ph == 0)
        return {};
    const uint32_t first = std::min(y0 / ph, pageRowCount);
    const uint32_t end = std::min((y1 + ph - 1) / ph, pageRowCount);
    return {first, std::max(first, end)};
}

// A draw with the scissor rows [sy0, sy1] (inclusive) into a target whose rows [dy0, dy1) are already dirty (GPU
// newer than memory, dirty == true). The dirty range is one interval and a readback writes all of it to memory, so
// every row that the draw can pull into the interval must be current first: the hull of the scissor and the old
// dirty range.
inline Rows drawRows(uint32_t sy0, uint32_t sy1, bool dirty, uint32_t dy0, uint32_t dy1, uint32_t ph,
                     uint32_t pageRowCount) {
    uint32_t y0 = sy0, y1 = sy1 + 1;
    if (dirty && dy1 > dy0) {
        y0 = std::min(y0, dy0);
        y1 = std::max(y1, dy1);
    }
    return pageRows(y0, y1, ph, pageRowCount);
}

// A texture of h rows sampled from the target at pixel row py. Filtering at the first and last texture row can
// reach the neighbouring target row, so one page row on each side is kept current as well.
inline Rows sampleRows(uint32_t py, uint32_t h, uint32_t ph, uint32_t pageRowCount) {
    Rows rows = pageRows(py, py + std::max(h, 1u), ph, pageRowCount);
    if (rows.end == rows.first)
        return rows;
    if (rows.first)
        --rows.first;
    rows.end = std::min(rows.end + 1, pageRowCount);
    return rows;
}

// ---- Which targets may be refreshed by row range
// --------------------------------------------------------------------- GSR_RT_LAZY_SYNC=0 refreshes every target
// whole, as before.
inline bool lazySyncEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("GSR_RT_LAZY_SYNC");
        return !value || value[0] != '0';
    }();
    return enabled;
}

// The game's small scratch targets (shadows, distant characters, HUD liquid): never shown, never part of a world
// pass. Only their uses may refresh a row range; every other target is always refreshed whole.
inline bool lazyTarget(uint32_t width, bool displayed, bool worldState) {
    return width < 480 && !displayed && !worldState;
}

// The same question asked of a backend's render target (both backends keep these fields under the same names).
template <class RT>
bool lazySyncTarget(const RT& rt) {
    return lazySyncEnabled() && lazyTarget(rt.w, rt.displaySeenFrame != ~0ull,
                                           rt.wstate || rt.neuralState || rt.temporalEpoch ||
                                               rt.surfaceSubmittedEpoch || rt.neuralOutputEpoch || rt.worldEpoch);
}

} // namespace gsr::rt_sync
