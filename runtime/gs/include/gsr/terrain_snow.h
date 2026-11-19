#pragma once
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <string_view>

namespace gsr::snow {
constexpr size_t kMaxRanges = 8;
struct Range {
    // Outer minimum x/z and core minimum x/z; core maximum x/z and outer maximum x/z.
    float enter[4], leave[4];
    float snowline, verticalScale;
    bool blueSnow = true;
};
struct Profile {
    std::string_view level;
    std::span<const Range> ranges;
};

inline constexpr Range wartok[] = {
    {{-20000, 50000, 0, 68000}, {140000, 195000, 170000, 220000}, 33200, 1, false}, // First rope bridge
    {{-6000, -6000, 8000, 8000}, {145000, 68000, 155000, 76500}, 33200, 1},         // North rim
    {{30000, 185000, 42000, 194000}, {186000, 403000, 195000, 417000}, 48000, 1},   // West range
    {{46000, 416000, 55000, 430000}, {190000, 498000, 200000, 512000}, 54000, 1},   // Far west
    {{200000, 132000, 212000, 145000}, {363000, 302000, 375000, 316000}, 47000, 1}, // East range
    {{413000, 464000, 425000, 477000}, {469000, 543000, 482000, 558000}, 33200, 1}, // Southeast ridge
};
inline constexpr Range grimstone[] = {
    {{133120, 73728, 143360, 83968}, {286720, 276480, 296960, 288768}, 100000, 0.95f},
    {{186500, 164000, 191000, 170000}, {225000, 194000, 231000, 198000}, 90000, 0.55f},
    {{200704, 237568, 206848, 247808}, {290816, 282624, 296960, 288768}, 75000, 0.92f},
};
inline constexpr Range grotto[] = {
    {{163840, 45056, 172032, 57344}, {387072, 415744, 395264, 425984}, 79000, 0.9f},
    {{169984, 49152, 176128, 57344}, {278528, 137216, 286720, 145408}, 67000, 0.9f},
    {{249856, 284672, 258048, 294912}, {382976, 413696, 391168, 423936}, 73000, 0.92f},
};
inline constexpr Range arenaOfDeath[] = {
    {{43008, 36864, 51200, 45056}, {188416, 182272, 196608, 190464}, 50000, 0.9f},
};
inline constexpr Range grottoOfDeath[] = {
    {{40960, 12288, 49152, 20480}, {225280, 147456, 233472, 155648}, 112000, 0.95f},
};

// The camera carries the one-based table position.
inline constexpr Profile profiles[] = {
    {"OotF 03 - Wartok Canyons", wartok},
    {"OotF 04 - Grimstone Mines", grimstone},
    {"OotF 05 - Grotto", grotto},
    {"OotF - Arena of Death (air)", arenaOfDeath},
    {"OotF - Grotto of Death (air)", grottoOfDeath},
};

static_assert(
    [] {
        for (const Profile& profile : profiles) {
            if (profile.ranges.empty() || profile.ranges.size() > kMaxRanges)
                return false;
            for (const Range& range : profile.ranges) {
                if (!(range.verticalScale > 0))
                    return false;
                for (size_t axis = 0; axis < 2; ++axis)
                    if (!(range.enter[axis] < range.enter[axis + 2] && range.enter[axis + 2] < range.leave[axis] &&
                          range.leave[axis] < range.leave[axis + 2]))
                        return false;
            }
        }
        return true;
    }(),
    "Snow ranges need positive scales and room for the fades on each side");

inline uint32_t profileForLevel(std::string_view level) {
    auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c; };
    for (size_t i = 0; i < std::size(profiles); ++i) {
        auto name = profiles[i].level;
        if (name.size() != level.size())
            continue;
        size_t j = 0;
        while (j < name.size() && lower(name[j]) == lower(level[j]))
            ++j;
        if (j == name.size())
            return uint32_t(i + 1);
    }
    return 0;
}
inline std::span<const Range> rangesForProfile(uint32_t id) {
    if (!id || id > std::size(profiles))
        return {};
    return profiles[id - 1].ranges;
}
} // namespace gsr::snow
