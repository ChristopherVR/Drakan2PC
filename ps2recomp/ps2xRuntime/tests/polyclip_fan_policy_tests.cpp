#include "../src/lib/drakan_polyclip_fan.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

using namespace drakan_polyclip;
using world::polyclip::Params;
using world::polyclip::Vertex;

namespace {
Vertex vtx(float x, float y, float depth) {
    Vertex v{};
    v.q = 1.0f / depth;
    v.x = x;
    v.y = y;
    v.z = 8.6e9f * v.q;
    v.w = 255.0f;
    return v;
}
} // namespace

int main() {
    std::vector<uint8_t> ram(0x100000, 0);
    // A fan of four triangles whose far edge is behind the camera (negative depth): every triangle needs clipping.
    const Vertex fan[6] = {vtx(2048, 2048, 100), vtx(2400, 2048, 100), vtx(2400, 2400, -50),
                           vtx(2048, 2400, -50), vtx(1700, 2400, -50), vtx(1700, 2048, 100)};
    const Params params;
    const uint32_t scratch = scratchFor(0x01ff1000u);
    assert(scratch >= kScratchBase && scratch + kScratchSlotBytes <= kScratchBase + kScratchSlots * kScratchSlotBytes);

    // Normal run: every non-empty triangle is drawn, the vertices handed over are in the scratch slot.
    int calls = 0;
    FanResult r = drawClippedFan(ram.data(), scratch, fan, 6, params, [&](uint32_t at, int m) {
        assert(at == scratch && m >= 3);
        ++calls;
        return true;
    });
    assert(!r.preempted && r.drawn == calls && calls > 0);

    // Preemption (the original unwound at a checkpoint) on the second draw: the loop must stop at once, with no further
    // original call (a second entry into the live frame would corrupt the guest stack), and report it.
    int seen = 0;
    r = drawClippedFan(ram.data(), scratch, fan, 6, params, [&](uint32_t, int) {
        ++seen;
        return seen < 2;
    });
    assert(r.preempted && seen == 2 && r.drawn == 1);

    // Nothing clipped away: no draw at all and no preemption.
    const Vertex behind[3] = {vtx(2048, 2048, -50), vtx(2100, 2048, -50), vtx(2048, 2100, -50)};
    r = drawClippedFan(ram.data(), scratch, behind, 3, params, [&](uint32_t, int) {
        assert(false);
        return true;
    });
    assert(!r.preempted && r.drawn == 0);
    std::cout << "polyclip fan policy ok\n";
    return 0;
}
