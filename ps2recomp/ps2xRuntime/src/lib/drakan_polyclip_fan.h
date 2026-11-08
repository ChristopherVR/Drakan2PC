// Clipped fan loop of the polyclip hook (header only, no guest state, testable with a stub draw).
//
// The original fan drawer reaches the GIF emitter through PS2_JAL, whose checkpoint can unwind the guest in the middle
// of a draw (the frames return upward and the scheduler re-enters at the resume address later). After such a draw the
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

// Guest pc after a clipped fan that completed: the caller's return address, as the original's `jr ra` leaves it. The
// hook can be entered from PS2_JAL (pc == entry) or by the scheduler resuming a call that unwound at its pre-call
// checkpoint; leaving pc at the entry makes the scheduler call the hook again, endlessly.
constexpr uint32_t pcAfterClippedFan(uint32_t ra) {
    return ra;
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

// Diagnostic guard of the scratch area (DRAKAN_POLY_NEAR_CLIP_GUARD): the hook keeps its own image of what it wrote
// there. Guest RAM differing from that image means other code wrote into the reserved area. A slot that a preempted
// draw may still read (its original resumes later and reads the vertices from it) is pinned to the stack that owns it
// until that stack enters the hook again; a different stack mapping to a pinned slot is a possible collision.
class ScratchGuard {
public:
    static constexpr uint32_t kBytes = kScratchSlots * kScratchSlotBytes;
    void start(const uint8_t* ram) {
        std::memcpy(image, ram + kScratchBase, kBytes);
        std::memset(pinned, 0, sizeof pinned);
        started = true;
    }
    bool active() const { return started; }
    // Offset (from kScratchBase) of the first byte that differs from the hook's image, or -1.
    int64_t verify(const uint8_t* ram) const {
        if (!started)
            return -1;
        const uint8_t* live = ram + kScratchBase;
        if (std::memcmp(live, image, kBytes) == 0)
            return -1;
        for (uint32_t i = 0; i < kBytes; ++i)
            if (live[i] != image[i])
                return i;
        return -1;
    }
    // Re-learn the area after a reported difference, so one foreign write is reported once.
    void resync(const uint8_t* ram) { std::memcpy(image, ram + kScratchBase, kBytes); }
    void wrote(uint32_t scratch, const void* bytes, size_t n) {
        if (started && scratch >= kScratchBase && scratch - kScratchBase + n <= kBytes)
            std::memcpy(image + (scratch - kScratchBase), bytes, n);
    }
    // Entry of a clipped draw from stack `sp`: false when the slot is pinned by a preempted draw of another stack.
    bool claim(uint32_t sp) {
        uint32_t& owner = pinned[slotOf(sp)];
        if (owner == 0 || owner == sp) {
            owner = 0;
            return true;
        }
        return false;
    }
    void pin(uint32_t sp) { pinned[slotOf(sp)] = sp; }

private:
    static uint32_t slotOf(uint32_t sp) { return (scratchFor(sp) - kScratchBase) / kScratchSlotBytes; }
    uint8_t image[kBytes] = {};
    uint32_t pinned[kScratchSlots] = {};
    bool started = false;
};

} // namespace drakan_polyclip
