#include "world/SpatialRegistration.h"

#include <array>
#include <cstdio>

int main() {
    auto check = [](bool ok) {
        if (!ok)
            std::fprintf(stderr, "spatial registration regression failed\n");
        return ok;
    };
    world::SpatialRegistrationRetry first, second;
    bool used = false;
    int attempts = 0;
    int32_t terrainCell = -1;
    auto nativeRegistration = [&] {
        ++attempts;
        return terrainCell;
    };
    // Terrain is unavailable at birth: try once, then leave the frame budget to no other actor.
    if (!check(!first.service(0, true, -1, used, nativeRegistration) && attempts == 1 && used))
        return 1;
    if (!check(!second.service(0, true, -1, used, nativeRegistration) && attempts == 1))
        return 1;
    used = false;
    if (!check(!second.service(1, true, -1, used, nativeRegistration) && attempts == 2))
        return 1;
    // Neither an inventory/claimed/dead actor nor a validly filed actor may consume native work.
    used = false;
    if (!check(!first.service(240, false, -1, used, nativeRegistration) && !used && attempts == 2))
        return 1;
    if (!check(!first.service(240, true, 4, used, nativeRegistration) && !used && attempts == 2))
        return 1;
    if (!check(!first.service(239, true, -1, used, nativeRegistration) && !used && attempts == 2))
        return 1;
    // The same object becomes discoverable once terrain arrives; successful cell state stops retries.
    terrainCell = 22;
    if (!check(first.service(240, true, -1, used, nativeRegistration) && used && attempts == 3))
        return 1;
    used = false;
    if (!check(!first.service(480, true, terrainCell, used, nativeRegistration) && !used && attempts == 3))
        return 1;
    // All 64 map entries get a turn before failed early entries retry (scan every third boundary).
    std::array<world::SpatialRegistrationRetry, 64> crowd;
    std::array<unsigned, 64> counts{};
    for (uint64_t tick = 0; tick < 192; tick += 3) {
        bool budget = false;
        unsigned boundaryCalls = 0;
        for (size_t i = 0; i < crowd.size(); ++i)
            crowd[i].service(tick, true, -1, budget, [&] {
                ++counts[i];
                ++boundaryCalls;
                return int32_t{-1};
            });
        if (!check(boundaryCalls == 1))
            return 1;
    }
    for (unsigned count : counts)
        if (!check(count == 1))
            return 1;
    return 0;
}
