#pragma once
#include <cstdint>

namespace Vu1SelectedNormalized {
// Lane bits use DEST ordering. Entry values and memory/integer writes remain
// unknown and keep runtime normalization. Only selected destination lanes
// are consumed: FMAC arithmetic and classification have no cross-lane operations. Arithmetic FMAC writes are normalized
// by the existing arithmetic path; normalizing those lanes again is an identity.
struct State {
    uint8_t vf[32]{};
    uint8_t acc = 0;
    constexpr State() { vf[0] = 15; }
    template <class Dec>
    constexpr uint8_t inputs(const Dec& d) const {
        if (d.fmKind < 1 || d.fmKind > 5)
            return 0;
        const uint32_t fs = (d.upper >> 11) & 31u;
        const uint32_t ft = (d.upper >> 16) & 31u;
        const uint8_t need = uint8_t((d.upper >> 21) & 15u);
        uint8_t result = (vf[fs] & need) == need ? 1 : 0;
        if (d.fmB <= 3) {
            if (vf[ft] & (8u >> d.fmB))
                result |= 2;
        } else if (d.fmB > 5 && (vf[ft] & need) == need)
            result |= 2;
        if ((acc & need) == need)
            result |= 4;
        return result;
    }
    template <class Dec>
    constexpr void apply(const Dec& d) {
        const auto lower = d.lowerUsage.vfWrite;
        if (!d.iBit && lower.reg != 0)
            vf[lower.reg] &= ~lower.lanes;
        const auto upper = d.upperUsage.vfWrite;
        if (upper.reg != 0)
            vf[upper.reg] &= ~upper.lanes;
        acc &= ~d.upperUsage.accWrite;
        if (d.fmKind >= 1 && d.fmKind <= 5) {
            if (upper.reg != 0)
                vf[upper.reg] |= upper.lanes;
            acc |= d.upperUsage.accWrite;
        }
    }
};
} // namespace Vu1SelectedNormalized
