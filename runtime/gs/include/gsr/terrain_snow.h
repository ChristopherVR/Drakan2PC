#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace gsr::snow {
constexpr size_t kMaxRanges = 4;
struct Range {
    // Outer minimum x/z and core minimum x/z; core maximum x/z and outer maximum x/z.
    float enter[4], leave[4];
    float snowline, verticalScale;
};
struct Profile { std::string_view level; std::span<const Range> ranges; };
// Add measured ranges here, then add a level/profile row below. IDs are one-based table positions.
inline constexpr Range wartok[] = {{{-20000, 50000, 0, 68000}, {140000, 195000, 170000, 220000}, 33200, 1}};
inline constexpr Profile profiles[] = {{"OotF 03 - Wartok Canyons", wartok}};
inline uint32_t profileForLevel(std::string_view level) {
    auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c; };
    for (size_t i = 0; i < std::size(profiles); ++i) {
        auto name = profiles[i].level;
        if (name.size() != level.size()) continue;
        size_t j = 0;
        while (j < name.size() && lower(name[j]) == lower(level[j])) ++j;
        if (j == name.size()) return uint32_t(i + 1);
    }
    return 0;
}
inline std::span<const Range> rangesForProfile(uint32_t id) {
    if (!id || id > std::size(profiles)) return {};
    auto ranges = profiles[id - 1].ranges;
    if (ranges.empty() || ranges.size() > kMaxRanges) return {};
    for (const auto& range : ranges)
        if (!(range.verticalScale > 0)) return {};
    return ranges;
}
} // namespace gsr::snow
