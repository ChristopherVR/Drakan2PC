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
//   * the native Skills section (vtable 0x54f1f0): its Magic row, the last of three, is neither drawn nor reachable.
//     The row loops of the key handler 445898 (Up / Down wrap, Left / Right on the current row), the text pass 445b78
//     and the first-row choice 447150 read the skill count *0x4fbad0; during those calls it reads 2. The row frames
//     and bars of 446290 (a fixed loop of three, each row's objects row+0x148 / +0x16c / +0x190 drawn by 4316c8) skip
//     the third row's draws;
//   * the native tab row: the Spells tab is not drawn (drakan_inventory_views.inl) and the drawn tabs spread evenly
//     over the old span (drakan_views::tabSlotShift): after the tab placement 42b0a8 the icon and ring nodes of a tab
//     right of the gap move left (hookTabPlace), so the glow (42faa0, placed from the ring) and the menu mouse boxes
//     (projected from the icon's rendered box) follow.
// The Spells sections of the native screens are skipped and their tab not drawn in drakan_inventory_views.inl; the
// modern screens hide their Spells page, the Magic skill and the mana bars from gameui::Snapshot::spellsEnabled.
// docs/reference/modding.md "Campaign packs", journal docs/journal/2026-11-14/ootf-no-spells.md.

namespace {

PS2Runtime::RecompiledFunction g_origSpellHud = nullptr, g_origManaMax = nullptr, g_origCanSummon = nullptr,
                               g_origSummonPrepare = nullptr, g_origSummonSpell = nullptr, g_origSkillKeys = nullptr,
                               g_origSkillText2 = nullptr, g_origSkillFirstRow = nullptr, g_origSkillGraphics = nullptr,
                               g_origRecordDraw = nullptr, g_origTabPlace = nullptr;
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

// ---- native Skills section ----
constexpr uint32_t kSkillCountVar = 0x4fbad0u; // stats block skill count (0x4fbac8 + 8)
constexpr uint32_t kSkillsVtable = 0x54f1f0u;  // section+0x18 of the Skills section
constexpr uint32_t kSkillRowsField = 0xfcu, kSkillRowField = 0x58u, kSkillRowStride = 0x1b4u, kSkillRowLit = 0x24u;
uint32_t g_skillGraphicsSection = 0; // a0 of the last 446290 (its hidden rows' objects are skipped)

uint32_t spellRead32(const uint8_t* ram, uint32_t a) {
    uint32_t v = 0;
    a &= 0x1fffffffu;
    if (a >= 0x100000u && a + 4 <= PS2_RAM_SIZE)
        std::memcpy(&v, ram + a, 4);
    return v;
}
void spellWrite32(uint8_t* ram, uint32_t a, uint32_t v) {
    a &= 0x1fffffffu;
    if (a >= 0x100000u && a + 4 <= PS2_RAM_SIZE)
        std::memcpy(ram + a, &v, 4);
}

// A section that was built with the Magic row current (it had the highest level) moves to the first row.
void skillRowIntoRange(uint8_t* ram, uint32_t section, int visible) {
    const uint32_t rows = spellRead32(ram, section + kSkillRowsField);
    const int row = int(spellRead32(ram, section + kSkillRowField));
    if (!rows || row < visible || row >= drakan_views::kSkillRows)
        return;
    spellWrite32(ram, rows + uint32_t(row) * kSkillRowStride + kSkillRowLit, 0);
    spellWrite32(ram, section + kSkillRowField, 0);
    spellWrite32(ram, rows + kSkillRowLit, 1);
}

// Runs a Skills function with the skill count lowered to the visible rows.
void withVisibleSkills(uint8_t* ram, R5900Context* c, PS2Runtime* rt, PS2Runtime::RecompiledFunction orig,
                       bool section) {
    const uint32_t count = spellRead32(ram, kSkillCountVar);
    const int visible = drakan_views::visibleSkillRows(int(count), campaignSpellsOff());
    if (visible == int(count)) {
        orig(ram, c, rt);
        return;
    }
    if (section)
        skillRowIntoRange(ram, getRegU32(c, 4), visible);
    withTemporaryGuestState(
        [&] {
            spellWrite32(ram, kSkillCountVar, uint32_t(visible));
            orig(ram, c, rt);
        },
        [&] { spellWrite32(ram, kSkillCountVar, count); });
}

void hookSkillKeys(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc != 0x445898u)
        return g_origSkillKeys(ram, c, rt);
    withVisibleSkills(ram, c, rt, g_origSkillKeys, true);
}
void hookSkillText2(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc != 0x445b78u)
        return g_origSkillText2(ram, c, rt);
    withVisibleSkills(ram, c, rt, g_origSkillText2, true);
}
void hookSkillFirstRow(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc != 0x447150u)
        return g_origSkillFirstRow(ram, c, rt);
    withVisibleSkills(ram, c, rt, g_origSkillFirstRow, false);
}
// 446290(section, transform) draws the rows' frames, level orbs and skill icons. It is long and the scheduler can
// suspend it between its calls (it resumes past this hook), so the section is remembered, not scoped to the call.
void hookSkillGraphics(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x446290u)
        g_skillGraphicsSection = getRegU32(c, 4);
    g_origSkillGraphics(ram, c, rt);
}
// 4316c8(object, transform, list) from 446290: the hidden rows' frame, orb and icon (row+0x148 / +0x16c / +0x190)
// are not drawn.
void hookRecordDraw(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t section = g_skillGraphicsSection;
    const uint32_t ra = getRegU32(c, 31);
    if (c->pc == 0x4316c8u && section && ra > 0x446290u && ra < 0x4468e0u && campaignSpellsOff() &&
        spellRead32(ram, section + 0x18u) == kSkillsVtable) {
        const uint32_t rows = spellRead32(ram, section + kSkillRowsField);
        const uint32_t obj = getRegU32(c, 4);
        const int visible = drakan_views::visibleSkillRows(int(spellRead32(ram, kSkillCountVar)), true);
        if (rows && obj >= rows + uint32_t(visible) * kSkillRowStride &&
            obj < rows + uint32_t(drakan_views::kSkillRows) * kSkillRowStride) {
            static int logged = 0;
            if (logged < 3 && ++logged)
                std::fprintf(stderr, "[spells] Skills row object %x not drawn (row %u)\n", obj,
                             (obj - rows) / kSkillRowStride);
            c->pc = ra;
            return;
        }
    }
    g_origRecordDraw(ram, c, rt);
}

// ---- native tab row ----
// Each tab record holds two scene nodes: +0x1c the icon (its model carries the slot offset, its frame +0x14 is the
// screen's page frame, copied from screen+0x16c by 42b0a8 every frame before the draw) and +0x20 the ring behind it
// (42b0a8 puts its position +0x14 at the icon's box centre). Both positions are in the page's camera space.
constexpr uint32_t kNodePosition = 0x14u;

bool tabNodeX(const uint8_t* ram, uint32_t record, uint32_t which, uint32_t& node, float& x) {
    node = spellRead32(ram, record + which);
    if (node < 0x100000u || node + 0x120u > PS2_RAM_SIZE)
        return false;
    std::memcpy(&x, ram + node + kNodePosition, 4);
    return std::isfinite(x);
}

// 42b0a8(record, scale) places one tab every frame, before the glows (42faa0) and the draws (42b308). After it a tab
// right of the hidden Spells tab moves left by its tabSlotShift: the icon's page frame (reset by the next placement)
// gets the shift, the ring is placed from the first ring. The glow is placed from the ring and the menu mouse projects
// the icon's rendered box, so both follow.
void hookTabPlace(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    using namespace drakan_views;
    if (c->pc != 0x42b0a8u || !campaignSpellsOff() || !g_invTabScreen)
        return g_origTabPlace(ram, c, rt);
    const uint32_t screen = g_invTabScreen, record = getRegU32(c, 4); // read before the call clobbers a0
    g_origTabPlace(ram, c, rt);
    const uint32_t base = spellRead32(ram, screen + 0x19c);
    const int count = int(spellRead32(ram, screen + 0x1a4));
    if (!base || record < base || (record - base) % 0x24u || count != kSectionCount ||
        int((record - base) / 0x24u) >= count)
        return;
    const int index = int((record - base) / 0x24u);
    const float shift = tabSlotShift(index, count, disabledSections(count, false, true));
    uint32_t icon = 0, ring = 0, ring0 = 0, ringLast = 0;
    float iconX = 0, ringX = 0, first = 0, last = 0;
    // The rings of the first and the last tab never move: a quarter of their distance is one slot (the last ring is
    // the one placed last frame). Accepted only near the slot the icons' own width implies (the first frames of a
    // new screen have no last ring yet).
    if (shift == 0.0f || !tabNodeX(ram, record, 0x1c, icon, iconX) || !tabNodeX(ram, record, 0x20, ring, ringX) ||
        !tabNodeX(ram, base, 0x20, ring0, first) ||
        !tabNodeX(ram, base + uint32_t(count - 1) * 0x24u, 0x20, ringLast, last))
        return;
    float width = 0;
    std::memcpy(&width, ram + icon + 0xc4u, 4); // the icon's box: x edge
    const float slot = (last - first) / float(count - 1);
    if (!(width > 0) || !(slot > 0.8f * width && slot < 2.0f * width))
        return;
    const float movedIcon = iconX + shift * slot, movedRing = first + (float(index) + shift) * slot;
    std::memcpy(ram + icon + kNodePosition, &movedIcon, 4);
    std::memcpy(ram + ring + kNodePosition, &movedRing, 4);
    static int logged = 0;
    if (logged < 4 && ++logged)
        std::fprintf(stderr, "[spells] tab %d moved by %.2f slots of %.1f\n", index, shift, slot);
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
        {0x3ac540u, &g_origSpellHud, &hookSpellHud},           {0x3d5838u, &g_origManaMax, &hookManaMax},
        {0x38f370u, &g_origCanSummon, &hookCanSummon},         {0x3895c0u, &g_origSummonPrepare, &hookSummonPrepare},
        {0x38bd48u, &g_origSummonSpell, &hookSummonSpell},     {0x445898u, &g_origSkillKeys, &hookSkillKeys},
        {0x445b78u, &g_origSkillText2, &hookSkillText2},       {0x447150u, &g_origSkillFirstRow, &hookSkillFirstRow},
        {0x446290u, &g_origSkillGraphics, &hookSkillGraphics}, {0x4316c8u, &g_origRecordDraw, &hookRecordDraw},
        {0x42b0a8u, &g_origTabPlace, &hookTabPlace},
    };
    for (const auto& h : hooks)
        if ((*h.original = rt.lookupFunction(h.address)))
            rt.replaceFunction(h.address, h.hook);
        else
            std::fprintf(stderr, "[spells] %x missing: a campaign without spells keeps that part\n", h.address);
}
