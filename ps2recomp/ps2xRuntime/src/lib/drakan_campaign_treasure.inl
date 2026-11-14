// Campaigns without gold and trinket drops (manifest `campaign_treasure = false`, Drakan: Order of the Flame): the
// guest-side part. Included by drakan_input_glue.cpp after drakan_campaign_spells.inl.
//
// How the disc deals out treasure (PAL addresses): a level's Level Properties ("Level Gold Distribution": total gold,
// allocation sizes, "Types of Treasure Available", a list of treasure classes 0x1006d such as the Scepter, Goblet or
// SackoGold) shares its gold at level load among the objects whose class has a "Gold Generation Weight". The shares sit
// in the gold generator's table (0x4fbee0, entries {actor, gold, ?} at +0x18, count +0x20). One function hands an
// object its share: 2eeb98(generator, actor, slots), `slots` being the class's "Number of Gold Slots". It takes the
// actor's entry out of the table, splits the gold into up to `slots` pieces, makes a treasure object for each
// (2ef558) and lists them as the actor's children; it returns 1 when it made any. Its three callers are the creature
// death 2d7120 (field 171), the breakable's destruction 214488 (field 1) and the chest's spew 227ec8 (field 1); each
// flings the new children out only when it returned 1. A class with no slots (several disc barrels have 0) takes the
// entry out and returns 0, and its caller goes on as for an object without gold.
//
// While the current game is such a campaign (drakan_game::treasureEnabled false; PvP arenas keep the disc rules), the
// hook calls 2eeb98 with `slots` = 0: the disc's own "no gold slots" path, so nothing is made and the table stays
// consistent. Everything else the dying creature does is untouched: its "Death Items Discarded" (the potions the Order
// of the Flame data gives orcs) are made and thrown before 2eeb98 by 2d7120 itself, a chest still spews what the level
// put in it, and Rynn's own gold (0 in such a campaign) is dropped at her death by 25f668 through 2ef728 as before.
// Journal docs/journal/2026-11-14/ootf-no-drops.md.

namespace {

PS2Runtime::RecompiledFunction g_origGoldShare = nullptr;
constexpr uint32_t kGoldShareFn = 0x2eeb98u; // PAL; other discs through drakan_region (none known: the hook stays off)
uint32_t g_goldShareAt = 0;
std::atomic<int> g_treasureRefusals{0}; // log budget

// The current game is a campaign without gold and trinket drops. A PvP arena keeps the disc's rules (the players may
// have chosen different games); co-op plays the campaign, so both sides follow it.
inline bool campaignTreasureOff() {
    return !drakan_game::treasureEnabled() && !arenaWorldMode();
}

void hookGoldShare(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == g_goldShareAt && campaignTreasureOff() && getRegU32(c, 6) != 0) {
        if (g_treasureRefusals.fetch_add(1) < 32)
            std::fprintf(stderr, "[treasure] actor %08x: no gold or trinkets (%u slots, caller %08x)\n",
                         getRegU32(c, 5), getRegU32(c, 6), getRegU32(c, 31));
        SET_GPR_U32(c, 6, 0);
        g_origGoldShare(ram, c, rt);
        return;
    }
    const uint32_t actor = getRegU32(c, 5), slots = getRegU32(c, 6), ra = getRegU32(c, 31);
    g_origGoldShare(ram, c, rt);
    if (c->pc == ra && getRegU32(c, 2) != 0 && g_treasureRefusals.fetch_add(1) < 32) // the disc's rules: say so once
        std::fprintf(stderr, "[treasure] actor %08x: gold share made (%u slots, caller %08x)\n", actor, slots, ra);
}

} // namespace

void installCampaignTreasureHooks(PS2Runtime& rt) {
    g_goldShareAt = drakan_region::guestAddress(kGoldShareFn);
    if (g_goldShareAt && (g_origGoldShare = rt.lookupFunction(g_goldShareAt)))
        rt.replaceFunction(g_goldShareAt, &hookGoldShare);
    else
        std::fprintf(stderr, "[treasure] %x missing: a campaign without treasure keeps its drops\n", kGoldShareFn);
}
