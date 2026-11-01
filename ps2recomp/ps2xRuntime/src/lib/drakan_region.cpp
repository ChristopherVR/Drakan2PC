#include "drakan_region.h"

#include <algorithm>
#include <cstdio>

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

void reportMultiplayerAvailability() {
    if constexpr (!build().identityAddresses) {
        static bool reported = false;
        if (reported)
            return;
        reported = true;
        std::fprintf(stderr,
                     "[region] multiplayer, co-op, arena, level change and the lifecycle hooks are disabled for disc "
                     "profile %s (%s): they use SCES-500.06 guest addresses and object layouts that are not verified "
                     "for this disc. See docs/reference/regions.md (\"Multiplayer arenas on USA\") and "
                     "analysis/scripts/mp_region_find.py to fill configs/regions/%s.json.\n",
                     build().id, build().serial, build().id);
    }
}

} // namespace drakan_region
