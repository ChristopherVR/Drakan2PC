// Image uploads applied to a render target: which rectangles to copy, and whether the GPU replicates them to the
// internal scale. Shared by D3D11Backend::Impl and WgpuBackend::Impl (completeTargetUpload, uploadTargetRect); a
// backend only reads memory and issues the copy.
#pragma once
#include "gs_internal.h"
#include "gs_mem.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace gsr::rt_upload {

// GSR_RT_UPLOAD_GPU=0 replicates every refreshed rectangle on the CPU, as before.
inline bool gpuReplicationEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("GSR_RT_UPLOAD_GPU");
        return !value || value[0] != '0';
    }();
    return enabled;
}

// Above scale 1 the GPU replicates the native pixels of a refreshed rectangle; tiny rectangles are not worth a pass.
inline bool gpuReplication(uint32_t scale, size_t scaledPixels) {
    return gpuReplicationEnabled() && scale > 1 && scaledPixels >= 16384;
}

// The blocks an upload covers inside the target rows [y0, y1), as runs of covered blocks per block row.
//   covers(block)            the upload rewrote that VRAM block
//   run(x, y, w, h, xEnd)    every run, for the caller's bookkeeping (clean blocks, row stamps); xEnd is unclamped
//   rect(x, y, w, h)         with `apply`: the rectangles to copy to the target. A run directly below the previous one
//                            with the same columns joins it, so a 16 x 16 movie macroblock (two block rows) is one
//                            rectangle and not two.
// Returns true when the upload touched the target at all.
template <class RT, class Covers, class Run, class Rect>
bool uploadedRuns(const RT& rt, uint32_t y0, uint32_t y1, bool apply, Covers&& covers, Run&& run, Rect&& rect) {
    const PsmGeom g = geomOf(rt.psm);
    bool touched = false;
    struct {
        uint32_t x = 0, y = 0, w = 0, h = 0;
    } pending;
    const auto flushPending = [&] {
        if (pending.h)
            rect(pending.x, pending.y, pending.w, pending.h);
        pending.h = 0;
    };
    for (uint32_t by = y0; by < y1; by += g.blockH)
        for (uint32_t bx = 0; bx < rt.w;) {
            if (!covers(blockOf(rt.psm, rt.base(), rt.fbw, bx, by))) {
                bx += g.blockW;
                continue;
            }
            uint32_t end = bx + g.blockW; // run of covered blocks in this block row
            while (end < rt.w && covers(blockOf(rt.psm, rt.base(), rt.fbw, end, by)))
                end += g.blockW;
            const uint32_t runW = std::min(end, rt.w) - bx, runH = std::min(by + g.blockH, rt.h) - by;
            touched = true;
            run(bx, by, runW, runH, end);
            if (apply) {
                if (pending.h && pending.x == bx && pending.w == runW && pending.y + pending.h == by)
                    pending.h += runH;
                else {
                    flushPending();
                    pending = {bx, by, runW, runH};
                }
            }
            bx = end;
        }
    flushPending();
    return touched;
}

} // namespace gsr::rt_upload
