// drakan_texture_budget_policy.h: which texture cache budget a game gets.
#include "../src/lib/drakan_texture_budget_policy.h"

#include <cassert>
#include <cstdio>

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
    std::puts("texture_budget_policy: ok");
    return 0;
}
