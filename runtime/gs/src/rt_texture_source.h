// May a texture be sampled straight from a render target that covers its memory? (pure policy, unit tested)
#pragma once
#include <cstdint>
#include <utility>

namespace gsr::rt_source {

// The game renders shadows, distant characters and the HUD orb liquid into small targets it places anywhere in its
// texture memory, and later reuses that memory for ordinary uploaded textures. A target object outlives that reuse
// (and is allocated with at least 512 rows), so a later texture often starts on a page inside an old target. Sampling
// it from the target is only right while the target holds something memory does not: draws. Rows that merely mirror
// an upload are a native resolution copy without mip levels or the HD image, and rows past the target do not exist.
//
// Does page row r (ph pixel rows high) intersect the pixel rows [y0, y1)? (a target's dirty range)
inline bool rowInRange(uint32_t r, uint32_t ph, uint32_t y0, uint32_t y1) {
    return y0 < y1 && y0 < (r + 1) * ph && y1 > r * ph;
}

// rows: page rows of the texture inside the target {total, filled from memory}; a row drawn since the last readback,
// or holding draws that were read back, does not count as filled from memory.
// offset: the texture does not start at the target's base. py / h: first texture row inside the target and the
// texture height; targetH: allocated target rows.
inline bool targetOwnsTexture(std::pair<uint32_t, uint32_t> rows, bool offset, uint32_t py, uint32_t h,
                              uint32_t targetH) {
    // A texture that runs past the last target row would sample nothing there (the HUD atlas lost the orb liquid).
    // A texture at the base may be larger than the target: a frame buffer sampled with a power of two size.
    if (offset && py + h > targetH)
        return false;
    if (rows.first == 0)
        return false;
    // Mostly an uploaded image (the HUD atlas with the orb liquid drawn into one row band, any texture on a stale
    // target): decode it from memory like every other texture. Mostly drawn: the target is the better source.
    return rows.second * 2 <= rows.first;
}

} // namespace gsr::rt_source
