#pragma once
// Pure policy of drakan_texture_budget (no runtime dependencies, unit tested in texture_budget_policy_tests.cpp).
#include <cstdint>
#include <cstdlib>

namespace drakan_texture_budget {

// The native texture cache budget of the PAL executable (texture manager 0x4db280 + 4): 5,662,310 bytes.
inline constexpr uint32_t kNativeBudget = 0x566666u;
// Budget used while a campaign pack (Order of the Flame or a custom campaign) is played: 8 MiB. Converted levels
// keep more and larger textures resident than the disc levels were built for (see
// docs/journal/2026-11-06/issue-5-village-scene-timing.md); 8 MiB leaves several MiB of guest heap free.
inline constexpr uint32_t kCampaignBudget = 8u * 1024u * 1024u;
// Bounds for DRAKAN_TEX_CACHE_KB.
inline constexpr uint32_t kMinBudgetKb = 1024u, kMaxBudgetKb = 16u * 1024u;

// The budget to apply, or 0 to keep the native one. `campaign`: a campaign pack is the current game.
// `env`: DRAKAN_TEX_CACHE_KB (null = unset): "0" keeps the native budget everywhere, a number of KiB (clamped to
// kMinBudgetKb..kMaxBudgetKb) applies to every game, anything else is ignored.
inline uint32_t budgetFor(bool campaign, const char* env) {
    if (env && *env) {
        char* end = nullptr;
        const long kb = std::strtol(env, &end, 10);
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
    return campaign ? kCampaignBudget : 0;
}

// The value to write over the manager's budget word, or 0 to leave it. Only ever raises a positive budget.
inline uint32_t raisedBudget(uint32_t current, uint32_t target) {
    return current > 0 && target > current ? target : 0;
}

} // namespace drakan_texture_budget
