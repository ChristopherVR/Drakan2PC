// drakan_texture_budget_policy.h: which texture cache budget a game gets.
#include "../src/lib/drakan_texture_budget_policy.h"

#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    using namespace drakan_texture_budget;
    // The disc game keeps the native budget; a campaign pack gets the larger one.
    assert(budgetFor(false, nullptr) == 0);
    assert(budgetFor(true, nullptr) == kCampaignBudget);
    assert(kCampaignBudget > kNativeBudget);
    // An empty or malformed variable is ignored.
    assert(budgetFor(true, "") == kCampaignBudget);
    assert(budgetFor(true, "8m") == kCampaignBudget);
    assert(budgetFor(false, "-5") == 0);
    // "0" turns it off everywhere; a number applies to every game, clamped.
    assert(budgetFor(true, "0") == 0);
    assert(budgetFor(false, "6144") == 6144u * 1024u);
    assert(budgetFor(true, "10") == kMinBudgetKb * 1024u);
    assert(budgetFor(true, "999999") == kMaxBudgetKb * 1024u);
    // Only raises, never lowers, and never touches an unset (0) budget.
    assert(raisedBudget(kNativeBudget, kCampaignBudget) == kCampaignBudget);
    assert(raisedBudget(kCampaignBudget, kCampaignBudget) == 0);
    assert(raisedBudget(kCampaignBudget, kNativeBudget) == 0);
    assert(raisedBudget(0, kCampaignBudget) == 0);
    assert(raisedBudget(kNativeBudget, 0) == 0);
    // The ini words: "auto" is the default rule, "native" keeps the native budget everywhere.
    assert(budgetFor(true, "auto") == kCampaignBudget);
    assert(budgetFor(false, "AUTO") == 0);
    assert(budgetFor(true, "native") == 0);
    assert(budgetFor(false, "16384") == 16u * 1024u * 1024u);
    // Pack levels: by name, or the level a direct start replaces.
    auto packs = [](const char* n) { return std::strcmp(n, "OotF 03 - Wartok Canyons") == 0; };
    assert(packLevel("OotF 03 - Wartok Canyons", packs, nullptr, nullptr));
    assert(!packLevel("Level 1A - Surdana", packs, nullptr, nullptr));
    assert(packLevel("Level 1A - Surdana", packs, "OotF 03 - Wartok Canyons", nullptr));
    assert(packLevel("level 0 - intro", packs, "OotF 03 - Wartok Canyons", "Level 0 - Intro"));
    assert(!packLevel("Level 1A - Surdana", packs, "OotF 03 - Wartok Canyons", "Level 0 - Intro"));
    assert(!packLevel("Level 1A - Surdana", packs, "Level 4A - Ravenshold", nullptr));
    assert(!packLevel("", packs, nullptr, nullptr));
    std::puts("texture_budget_policy: ok");
    return 0;
}
