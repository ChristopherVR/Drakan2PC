// Campaigns without spells (manifest `campaign_spells = false`, Drakan: Order of the Flame): the guest-side part.
// Included by drakan_input_glue.cpp after the inventory view hooks; campaignSpellsOff() is defined there. While it is
// true (drakan_game::spellsEnabled, switched with the game on the title card; multiplayer keeps spells):
//   * casting: the Circle command's "can a summon start" test 38f370(player+0x1024) answers no, so Circle with empty
//     hands takes its parry path instead (Game_OnInputCommand 23ce18, command 0x4007), and the two summon entries,
//     3895c0 (preparation, from 23ce18 and 38bd48) and 38bd48(spellbook, spell + 1) (a given spell, from the
//     inventory's Summon 342478 and the debug console), start nothing;
//   * the mana orb: the right orb of the gameplay HUD 3ac540 shows mana when it has nothing else to show (arrows of a
//     drawn bow, an item's charge). Its mana branch alone calls the stats getter 3d5838 (return 3adf9c); there the
//     orb's slide-in offset (hud+0xc, -90 = out of the picture, 20 = in) is put back to -90 before the orb's draws, so
//     they land outside the picture and the orb stays out. Arrows and charges still slide the orb in as on the disc.
// The Spells sections of the native screens are skipped and their tab not drawn in drakan_inventory_views.inl; the
// modern screens hide their Spells page and mana bars from gameui::Snapshot::spellsEnabled.
// docs/reference/modding.md "Campaign packs", journal docs/journal/2026-11-14/ootf-no-spells.md.

namespace {

PS2Runtime::RecompiledFunction g_origSpellHud = nullptr, g_origManaMax = nullptr, g_origCanSummon = nullptr,
                               g_origSummonPrepare = nullptr, g_origSummonSpell = nullptr;
uint32_t g_spellHudObj = 0;                    // a0 of the HUD draw 3ac540 (0x510150 on the PAL disc)
constexpr uint32_t kHudManaReturn = 0x3adf9cu; // 3ac540's call of 3d5838 in its mana branch
constexpr uint32_t kHudRightOrbSlide = 0xcu;   // hud+0xc: the right orb's slide offset (float)
constexpr float kHudOrbOut = -90.0f;           // the native "slid out" end of that offset

std::atomic<int> g_spellRefusals{0}; // log budget

void spellLog(const char* what) {
    if (g_spellRefusals.fetch_add(1) < 16)
        std::fprintf(stderr, "[spells] %s: this campaign has no spells\n", what);
}

void hookSpellHud(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x3ac540u)
        g_spellHudObj = getRegU32(c, 4);
    g_origSpellHud(ram, c, rt);
}

void hookManaMax(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t ra = getRegU32(c, 31);
    g_origManaMax(ram, c, rt);
    if (ra == kHudManaReturn && campaignSpellsOff() && g_spellHudObj >= 0x100000u &&
        g_spellHudObj + kHudRightOrbSlide + 4 <= PS2_RAM_SIZE) {
        float out = kHudOrbOut;
        std::memcpy(ram + g_spellHudObj + kHudRightOrbSlide, &out, 4);
    }
}

void hookCanSummon(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x38f370u && campaignSpellsOff()) {
        spellLog("Circle: no cast");
        SET_GPR_U32(c, 2, 0);
        c->pc = getRegU32(c, 31);
        return;
    }
    g_origCanSummon(ram, c, rt);
}

void hookSummonPrepare(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x3895c0u && campaignSpellsOff()) {
        spellLog("summon refused");
        SET_GPR_U32(c, 2, 0);
        c->pc = getRegU32(c, 31);
        return;
    }
    g_origSummonPrepare(ram, c, rt);
}

void hookSummonSpell(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x38bd48u && campaignSpellsOff()) {
        spellLog("spell summon refused");
        SET_GPR_U32(c, 2, 0);
        c->pc = getRegU32(c, 31);
        return;
    }
    g_origSummonSpell(ram, c, rt);
}

} // namespace

// After installInventoryViewHooks (the HUD hook wraps the views' 3ac540 hook). PAL addresses, like the view hooks.
void installCampaignSpellHooks(PS2Runtime& rt) {
    struct Hook {
        uint32_t address;
        PS2Runtime::RecompiledFunction* original;
        PS2Runtime::RecompiledFunction hook;
    };
    const Hook hooks[] = {
        {0x3ac540u, &g_origSpellHud, &hookSpellHud},       {0x3d5838u, &g_origManaMax, &hookManaMax},
        {0x38f370u, &g_origCanSummon, &hookCanSummon},     {0x3895c0u, &g_origSummonPrepare, &hookSummonPrepare},
        {0x38bd48u, &g_origSummonSpell, &hookSummonSpell},
    };
    for (const auto& h : hooks)
        if ((*h.original = rt.lookupFunction(h.address)))
            rt.replaceFunction(h.address, h.hook);
        else
            std::fprintf(stderr, "[spells] %x missing: a campaign without spells keeps that part\n", h.address);
}
