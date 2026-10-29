// Region address tables (docs/reference/regions.md). Every row is a PAL SCES_500.06 address the runtime uses and its
// equivalent in the other executable, checked against analysis/regions/<region>/function_map.csv / data_map.csv
// (written by analysis/scripts/region_map.py) and, for "callsite" functions, by reading both bodies.
#include "drakan_region.h"

#include <algorithm>
#include <iterator>

namespace drakan_region {
namespace {

struct AddressPair {
    uint32_t pal;
    uint32_t region;
};

// USA SCUS_971.28. Keep sorted by PAL address.
constexpr AddressPair kUsa[] = {
    {0x00101710u, 0x001016b0u}, // LoadIopModules (function_map: exact)
    {0x004b0948u, 0x004b0080u}, // scePadInit (config stub, libpad 2.2)
    {0x004b0ba8u, 0x004b02e0u}, // scePadPortOpen
    {0x004b0ef8u, 0x004b0630u}, // scePadRead
    {0x004b0f78u, 0x004b06b0u}, // scePadGetState
    {0x004b1490u, 0x004b0bc8u}, // scePadSetMainMode
    {0x004b1548u, 0x004b0c80u}, // scePadSetActDirect
    {0x004b91d8u, 0x004ba570u}, // sceSdRemote (callsite; libsdr 2.2 body adds fno 0x8110 / 0x8120)
    {0x004c3798u, 0x004c45e8u}, // sceSifLoadModuleBuffer wrapper (exact)
    {0x004ce028u, 0x004cee98u}, // newlib rand (exact)
    {0x004fb60cu, 0x004fc48cu}, // current GUI screen object pointer, read by the host pad (data_map, 10 refs)
    {0x00516f80u, 0x00516360u}, // sceSdRemote no-wait end-function slot (lw in both bodies)
    {0x00517000u, 0x005163e0u}, // libipu IQ matrix table (data_map, 1 ref, no conflict)
    {0x00517050u, 0x00516430u}, // libipu VQ CLUT table (data_map)
    {0x00518e94u, 0x0051826cu}, // _impure_ptr (data_map, 35 refs)
    {0x0056df00u, 0x0056e6c0u}, // sceSdRemote static RPC / reply buffer (lui 0x57 + lo in both bodies)
};

constexpr bool sorted() {
    for (size_t i = 1; i < std::size(kUsa); ++i)
        if (kUsa[i - 1].pal >= kUsa[i].pal)
            return false;
    return true;
}
static_assert(sorted(), "keep the region tables sorted by PAL address");

} // namespace

uint32_t translateGuestAddress(uint32_t palAddress) {
    if constexpr (kBuildRegion == Region::Pal) {
        return palAddress;
    } else {
        const auto it = std::lower_bound(std::begin(kUsa), std::end(kUsa), palAddress,
                                         [](const AddressPair& p, uint32_t a) { return p.pal < a; });
        return it != std::end(kUsa) && it->pal == palAddress ? it->region : 0u;
    }
}

} // namespace drakan_region
