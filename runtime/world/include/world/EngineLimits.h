// PS2-era budgets of the engine that the PC lifts (pure rules, unit tested in world_engine_limits_test).
//
// Character shadow distance ([video] shadow_distance). The level sets three squared distances for the projected
// character shadows (PAL 0x1431e8 writes them from the level's numbers): full shadow up to `start`, fading to nothing
// at `end`, and no shadow work at all past `cutoff` (end x 1.1). Every user compares them with a squared distance, so a
// distance factor f multiplies each of them by f^2.
//
// Model and animation caches ([general] model_cache_mb / animation_cache_mb). The engine keeps loaded models and
// animations in two resource caches whose budgets the level sets (5.4 MiB and 2 MiB on the disc); past the budget the
// least recently used unreferenced entries are dropped and loaded again from the disc when next needed. The options
// raise a budget; they never lower one.
//
// AI run limit ([general] ai_run_max). The level's "AI Run Max" (4 on the disc levels) is how many creatures that the
// AI limiter controls may run their AI at once; a farther one is paused until a slot frees. The option raises it; it
// never lowers it, and leaves a level value of 0 (none) or below 0 (no limit) as it is.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace world::shadow_distance {

inline constexpr float kOff = 1.0f;
inline constexpr float kMaxFactor = 4.0f;
inline constexpr float kDefault = 3.0f; // the option's default; kOff (1) is the original distance

inline float clampFactor(float factor) {
    return std::isfinite(factor) ? std::clamp(factor, kOff, kMaxFactor) : kOff;
}

// A squared distance of the level, moved out by the distance factor. Factor 1, or a stored value that is not a plain
// positive number, returns the stored value itself.
inline float scaledSquared(float stored, float factor) {
    if (!(factor > kOff) || !std::isfinite(stored) || !(stored > 0.0f) || !(stored < 1.0e30f))
        return stored;
    return stored * factor * factor;
}

} // namespace world::shadow_distance

namespace world::resource_cache {

inline constexpr int kMaxMb = 64;

// The budget an option value asks for: 0 (or less) = keep the level's own budget, otherwise 1 .. kMaxMb MiB.
inline uint32_t budgetBytes(int mb) {
    if (mb <= 0)
        return 0;
    return static_cast<uint32_t>(std::min(mb, kMaxMb)) * 1024u * 1024u;
}

// "auto" (the default): with the extended game heap (drakan_extended_heap.h) the caches get these budgets, whose extra
// bytes the heap takes above the retail 32 MB when its retail part is full; without it the level's own (0).
inline constexpr int kAutoModelMb = 16, kAutoAnimationMb = 8;

// The MiB an option value asks for: "auto" or empty = the automatic budget above, a number = that (0 = the level's own).
inline int optionMb(const char* value, bool heapExtends, int autoMb) {
    if (!value || !*value || (value[0] == 'a' || value[0] == 'A'))
        return heapExtends ? autoMb : 0;
    int mb = 0;
    for (const char* c = value; *c >= '0' && *c <= '9'; ++c)
        mb = std::min(mb * 10 + (*c - '0'), 100000);
    return mb;
}

// The value to write over a cache's budget word, or 0 to leave it. Only ever raises a positive budget.
inline uint32_t raisedBudget(uint32_t current, uint32_t target) {
    return current > 0 && target > current ? target : 0;
}

} // namespace world::resource_cache

namespace world::ai_run_max {

inline constexpr int kDefault = 8;
inline constexpr int kMax = 64;

// The run limit to use for a level value and an option value: 0 (or less) = the level's own, otherwise the larger of
// the two, at most kMax. Only a positive level value is raised.
inline int raised(int levelValue, int option) {
    if (option <= 0 || levelValue <= 0)
        return levelValue;
    return std::max(levelValue, std::min(option, kMax));
}

} // namespace world::ai_run_max
