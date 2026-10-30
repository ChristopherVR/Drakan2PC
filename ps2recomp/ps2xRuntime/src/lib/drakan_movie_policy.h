// Disc-independent movie timing and listener validation. No platform or guest CPU dependencies.
#pragma once

#include "drakan_region.h"

#include <chrono>
#include <cstring>
#include <span>

namespace drakan_movie {

inline std::chrono::nanoseconds framePeriod(const drakan_region::MovieLayout& layout) {
    if (layout.fpsNumerator == 0)
        return {};
    return std::chrono::nanoseconds(1000000000ull * layout.fpsDenominator / layout.fpsNumerator);
}

inline bool liveListener(std::span<const uint8_t> memory, uint32_t manager, uint32_t listener,
                         const drakan_region::MovieLayout& layout) {
    if (manager == 0 || listener == 0 || listener >= memory.size())
        return false;
    const uint64_t begin = uint64_t(manager) + layout.screenEntries;
    const uint64_t end = begin + uint64_t(layout.screenCount) * sizeof(uint32_t);
    if (end > memory.size())
        return false;
    for (uint32_t id = 0; id < layout.screenCount; ++id) {
        uint32_t entry = 0;
        std::memcpy(&entry, memory.data() + begin + sizeof(uint32_t) * id, sizeof(entry));
        if (entry == listener)
            return true;
    }
    return false;
}

} // namespace drakan_movie
