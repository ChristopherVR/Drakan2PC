#include "drakan_region.h"

#include <algorithm>

namespace drakan_region {
namespace {
constexpr bool sorted() {
    for (size_t i = 1; i < kAddressPairs.size(); ++i)
        if (kAddressPairs[i - 1].pal >= kAddressPairs[i].pal)
            return false;
    return true;
}
static_assert(sorted(), "keep the region tables sorted by PAL address");
} // namespace

uint32_t translateGuestAddress(uint32_t palAddress) {
    if constexpr (build().identityAddresses) {
        return palAddress;
    } else {
        const auto it = std::lower_bound(kAddressPairs.begin(), kAddressPairs.end(), palAddress,
                                         [](const AddressPair& p, uint32_t a) { return p.pal < a; });
        return it != kAddressPairs.end() && it->pal == palAddress ? it->region : 0u;
    }
}

} // namespace drakan_region
