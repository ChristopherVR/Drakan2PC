#pragma once
// Pure policy of drakan_texture_budget (no runtime dependencies, unit tested in texture_budget_policy_tests.cpp).
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace drakan_texture_budget {

// The native texture cache budget of the PAL executable (texture manager 0x4db280 + 4): 5,662,310 bytes.
inline constexpr uint32_t kNativeBudget = 0x566666u;
// Budget used while a campaign pack (Order of the Flame or a custom campaign) is played, or a level from a pack is
// loaded in any game (a direct start, the editor's play test, a multiplayer map from a pack): 8 MiB. Converted levels
// keep more and larger textures resident than the disc levels were built for (see
// docs/journal/2026-11-06/issue-5-village-scene-timing.md); 8 MiB leaves several MiB of guest heap free.
inline constexpr uint32_t kCampaignBudget = 8u * 1024u * 1024u;
// Bounds of a number of KiB in [video] game_texture_cache_kb / DRAKAN_TEX_CACHE_KB.
inline constexpr uint32_t kMinBudgetKb = 1024u, kMaxBudgetKb = 16u * 1024u;

inline bool equalsNoCase(const char* a, const char* b) {
    for (;; ++a, ++b) {
        const char ca = (*a >= 'A' && *a <= 'Z') ? char(*a - 'A' + 'a') : *a;
        const char cb = (*b >= 'A' && *b <= 'Z') ? char(*b - 'A' + 'a') : *b;
        if (ca != cb)
            return false;
        if (!ca)
            return true;
    }
}

// The budget to apply, or 0 to keep the native one. `raised`: a campaign pack is the current game or the loaded level
// comes from a pack. `setting`: [video] game_texture_cache_kb or DRAKAN_TEX_CACHE_KB (null = unset). "auto" or empty =
// kCampaignBudget when `raised`, else native; "native" or "0" keeps the native budget everywhere; a number of KiB
// (clamped to kMinBudgetKb..kMaxBudgetKb) applies to every game and level; anything else counts as "auto".
inline uint32_t budgetFor(bool raised, const char* setting) {
    if (setting && *setting && !equalsNoCase(setting, "auto")) {
        if (equalsNoCase(setting, "native"))
            return 0;
        char* end = nullptr;
        const long kb = std::strtol(setting, &end, 10);
        if (end && *end == '\0' && kb >= 0) {
            if (kb == 0)
                return 0;
            const uint32_t clamped =
                kb < static_cast<long>(kMinBudgetKb)
                    ? kMinBudgetKb
                    : (kb > static_cast<long>(kMaxBudgetKb) ? kMaxBudgetKb : static_cast<uint32_t>(kb));
            return clamped * 1024u;
        }
    }
    return raised ? kCampaignBudget : 0;
}

// The value to write over the manager's budget word, or 0 to leave it. Only ever raises a positive budget.
inline uint32_t raisedBudget(uint32_t current, uint32_t target) {
    return current > 0 && target > current ? target : 0;
}

// Is the level about to load one of a pack's? `level`: the name the load event carries; `packLevels`: the pack levels
// (LEVELLST.TXT titles). A direct start (DRAKAN_START_LEVEL = `startLevel`) replaces `startFrom`
// (DRAKAN_START_LEVEL_FROM, empty = "Level 1A - Surdana") at the file layer, so that load still carries the replaced
// level's name.
template <class Contains>
bool packLevel(const char* level, Contains&& packLevels, const char* startLevel, const char* startFrom) {
    if (!level || !*level)
        return false;
    if (packLevels(level))
        return true;
    if (startLevel && *startLevel && packLevels(startLevel)) {
        const char* from = startFrom && *startFrom ? startFrom : "Level 1A - Surdana";
        return equalsNoCase(level, from);
    }
    return false;
}

} // namespace drakan_texture_budget
