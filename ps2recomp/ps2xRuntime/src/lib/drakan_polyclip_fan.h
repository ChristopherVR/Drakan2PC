// Clipped fan loop of the polyclip hook (header only, no guest state, testable with a stub draw).
//
// The original fan drawer reaches the GIF emitter through PS2_JAL, whose checkpoint can unwind the guest in the middle of
// a draw (the frames return upward and the scheduler re-enters at the resume address later). After such a draw the
// original's own frame is live, so the hook must not touch registers or issue another call: the loop below stops at the
// first draw that did not complete and reports it. The remaining triangles of that fan are dropped (one frame, rare).
#pragma once

#include "world/PolyClip.h"

#include <cstdint>
#include <cstring>

namespace drakan_polyclip {

struct FanResult {
    int drawn = 0;
    bool preempted = false; // a draw did not return normally: leave guest state exactly as the callee left it
};

// Scratch slots in reserved low RDRAM (below the ELF image), keyed by the caller's stack so that a draw resumed after a
// preemption keeps its vertices even if another guest thread clips meanwhile.
constexpr uint32_t kScratchBase = 0x000f0000u;
constexpr uint32_t kScratchSlots = 64u;
constexpr uint32_t kScratchSlotBytes = 16u * 32u; // kMaxClipped vertices of 32 bytes
static_assert(world::polyclip::kMaxClipped * sizeof(world::polyclip::Vertex) <= kScratchSlotBytes, "slot size");
constexpr uint32_t scratchFor(uint32_t sp) {
    return kScratchBase + ((sp >> 8) & (kScratchSlots - 1u)) * kScratchSlotBytes;
}

// draw(scratchAddr, vertexCount) -> true when the original returned normally (pc == return address).
template <class Draw>
FanResult drawClippedFan(uint8_t* ram, uint32_t scratch, const world::polyclip::Vertex* v, uint32_t n,
                         const world::polyclip::Params& params, Draw&& draw) {
    FanResult r;
    for (uint32_t i = 1; i + 1 < n; ++i) {
        const world::polyclip::Vertex tri[3] = {v[0], v[i], v[i + 1]};
        world::polyclip::Vertex out[world::polyclip::kMaxClipped];
        const int m = world::polyclip::clipPolygon(tri, 3, params, out);
        if (m < 3)
            continue;
        std::memcpy(ram + scratch, out, static_cast<size_t>(m) * sizeof(world::polyclip::Vertex));
        if (!draw(scratch, m)) {
            r.preempted = true;
            return r;
        }
        ++r.drawn;
    }
    return r;
}

} // namespace drakan_polyclip
