// drakan_arena_power_policy.h: every multiplayer (arena) character starts at the maximum level with every spell
// unlocked, whatever the campaign save behind the match looks like. Pure decisions and the stat word encoding; the
// guest reads and writes live in drakan_arena.cpp (docs/reference/netcode.md "Starting power").
#pragma once

#include <cstdint>

namespace drakan_arena_power {

constexpr int32_t kMaxLevel = 12;       // "All Experience" debug option: level 1 to 12
constexpr int32_t kMaxXp = 219623;      // the experience that option reaches (level 12)
constexpr uint32_t kMaxSkill = 8;       // skill record maximum (melee, archery, magic), plain ints
constexpr uint32_t kSkillRecord = 0x4c; // skill table record size: {id, level, max, name...}
constexpr uint32_t kSpellRecord = 0x14; // spell table entry size: +0 known level (0 = not learned)
constexpr uint32_t kMaxSpellSlots = 32; // sanity bound on the spell count
constexpr int kMaxShamanCalls = 8;      // the console's `shaman` raises every spell by one level per call

// The stats block keeps numbers as rotl32(v, 8) ^ key (rotr8 undoes it); the skill levels are plain.
constexpr uint32_t encodeStat(int32_t v, uint32_t key) {
    const uint32_t u = static_cast<uint32_t>(v);
    return ((u << 8) | (u >> 24)) ^ key;
}
constexpr int32_t decodeStat(uint32_t raw, uint32_t key) {
    const uint32_t x = raw ^ key;
    return static_cast<int32_t>((x >> 8) | (x << 24));
}

// True when the level or the experience is below the maximum (a stale or implausible value is raised too).
constexpr bool needsLevel(int32_t level, int32_t xp) {
    return level < kMaxLevel || xp < kMaxXp;
}

// A skill record is raised to its own maximum (clamped to kMaxSkill); returns the level it should have.
constexpr uint32_t skillTarget(uint32_t recordMax) {
    return recordMax == 0 || recordMax > kMaxSkill ? kMaxSkill : recordMax;
}

// Whether a skill record reading `level` / `recordMax` needs a write.
constexpr bool needsSkill(uint32_t level, uint32_t recordMax) {
    return level < skillTarget(recordMax);
}

// The spell unlock loop: `sum` is the total of the known levels after the last `shaman` call, `previousSum` the total
// before it, `calls` how many calls have been made. Another call while the last one still raised something.
constexpr bool callShamanAgain(uint32_t previousSum, uint32_t sum, int calls) {
    return calls < kMaxShamanCalls && (calls == 0 || sum > previousSum);
}

} // namespace drakan_arena_power
