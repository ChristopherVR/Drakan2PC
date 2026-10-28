// Included inside drakan_input_glue.cpp's anonymous namespace after RecompGuest and the Map hooks.
// Inventory section contents for the menu mouse (docs/reference/mouse-menus.md, section 11): read-only captures at the
// native draws while their camera is installed, and the native selectors that a pending mouse request steers. Every
// hook chains to the original function.
PS2Runtime::RecompiledFunction g_origSpellCells = nullptr, g_origSpellSelect = nullptr, g_origSkillText = nullptr,
                               g_origJournalDraw = nullptr, g_origJournalText = nullptr, g_origFontDraw = nullptr,
                               g_origShopDraw = nullptr, g_origShopIcon = nullptr, g_origShopIconAlt = nullptr,
                               g_origShopSelect = nullptr;
bool contentMouseAllowed(uint8_t* ram) {
    return activeScreenId(ram) == 0x15 && module().frame().focused && !module().suspended();
}
void logContent(const char* what) {
    const auto& d = module().menuMouse().debug();
    static unsigned logged[8]{};
    const unsigned kind = unsigned(d.contentKind) & 7;
    if (logged[kind] < 3 || d.contentCaptures % 300 == 0) {
        ++logged[kind];
        ilog("[in] inventory %s items=%u box=%.1f,%.1f..%.1f,%.1f capture=%u frame=%d\n", what, d.contentItems,
             d.contentLeft, d.contentTop, d.contentRight, d.contentBottom, d.contentCaptures, module().frameNo());
    }
}
// 4481e8(section): the Spells cells, with the page's camera installed.
void hookSpellCells(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x4481e8u && contentMouseAllowed(ram)) {
        RecompGuest g(ram, c, rt);
        module().menuMouseRw().captureInventorySpells(g, getRegU32(c, 4));
        logContent("spells");
    }
    g_origSpellCells(ram, c, rt);
}
// 3422c8(controller, index, commit): reached from the arrow handler 344de0 with commit 0.
void hookSpellSelect(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t ra = getRegU32(c, 31);
    // The modern Tab screen steers the spell selection while the input is the host's (drakan_tab_screen.inl).
    if (c->pc == 0x3422c8u && ra >= 0x344de0u && ra < 0x345290u && getRegU32(c, 6) == 0 && tabSteerSpell() >= 0) {
        RecompGuest g(ram, c, rt);
        ilog("[tab-screen] spell selection old=%u target=%d\n", g.read32(getRegU32(c, 4) + 0x4b4), tabSteerSpell());
        SET_GPR_U32(c, 5, static_cast<uint32_t>(tabSteerSpell()));
        g_origSpellSelect(ram, c, rt);
        return;
    }
    if (c->pc == 0x3422c8u && ra >= 0x344de0u && ra < 0x345290u && contentMouseAllowed(ram)) {
        RecompGuest g(ram, c, rt);
        const int target = module().menuMouseRw().takeSpellSelection(g, getRegU32(c, 4), getRegU32(c, 6));
        if (target >= 0) {
            ilog("[in] inventory mouse spell selection old=%u target=%d\n", g.read32(getRegU32(c, 4) + 0x4b4), target);
            SET_GPR_U32(c, 5, static_cast<uint32_t>(target));
        }
    }
    g_origSpellSelect(ram, c, rt);
}
// 445b78(section): the Skills text pass, under the same camera as the native label projection (42ae00).
void hookSkillText(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x445b78u && contentMouseAllowed(ram)) {
        RecompGuest g(ram, c, rt);
        module().menuMouseRw().captureInventorySkills(g, getRegU32(c, 4));
        logContent("skills");
    }
    g_origSkillText(ram, c, rt);
}
// Journal: 438be8(section) draws the page; its 4387b0 call returning to 438e40 passes the page clip rectangle, and its
// 286170 glyph draws return to 438d64 / 438d90 (previous / next region) and 438ee8 / 438f10 (scroll up / down).
void hookJournalDraw(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x438be8u && contentMouseAllowed(ram)) {
        RecompGuest g(ram, c, rt);
        module().menuMouseRw().beginInventoryJournal(g, getRegU32(c, 4));
    }
    g_origJournalDraw(ram, c, rt);
    if (contentMouseAllowed(ram) && module().menuMouse().debug().contentKind == 3)
        logContent("journal");
}
void hookJournalText(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x4387b0u && getRegU32(c, 31) == 0x438e40u && contentMouseAllowed(ram)) {
        RecompGuest g(ram, c, rt);
        module().menuMouseRw().captureJournalPage(g, getRegU32(c, 6) & 0x1fffffffu);
    }
    g_origJournalText(ram, c, rt);
}
void hookFontDraw(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x286170u) {
        const uint32_t ra = getRegU32(c, 31);
        const int glyph = ra == 0x438d64u ? 0 : ra == 0x438d90u ? 1 : ra == 0x438ee8u ? 2 : ra == 0x438f10u ? 3 : -1;
        if (glyph >= 0 && contentMouseAllowed(ram)) {
            RecompGuest g(ram, c, rt);
            module().menuMouseRw().captureJournalGlyph(g, unsigned(glyph), static_cast<int32_t>(getRegU32(c, 4)),
                                                       static_cast<int32_t>(getRegU32(c, 5)));
        }
    }
    g_origFontDraw(ram, c, rt);
}
// Shop list: 44a678(section) lays out the stock; each item icon goes through 434e80 (return 44aa94) or 4380b0 (return
// 44aa0c) with a0 = section and a3 -> {drawn column, row} floats. The caller's frame (sp at callee entry) holds the
// list index (+0xd4) and the item's cell size (+0xc0 width, +0xc4 height).
void hookShopDraw(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x44a678u && contentMouseAllowed(ram)) {
        RecompGuest g(ram, c, rt);
        module().menuMouseRw().beginInventoryShop(g, getRegU32(c, 4));
    }
    g_origShopDraw(ram, c, rt);
    if (contentMouseAllowed(ram) && module().menuMouse().debug().contentKind == 4)
        logContent("shop");
}
void captureShopIcon(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    RecompGuest g(ram, c, rt);
    const uint32_t sp = getRegU32(c, 29) & 0x1fffffffu, cell = getRegU32(c, 7) & 0x1fffffffu;
    module().menuMouseRw().captureShopItem(g, getRegU32(c, 4), g.read32(sp + 0xd4), g.read32(sp + 0xc0),
                                           g.read32(sp + 0xc4), g.readF(cell), g.readF(cell + 4));
}
void hookShopIcon(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x434e80u && getRegU32(c, 31) == 0x44aa94u && contentMouseAllowed(ram))
        captureShopIcon(ram, c, rt);
    g_origShopIcon(ram, c, rt);
}
void hookShopIconAlt(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x4380b0u && getRegU32(c, 31) == 0x44aa0cu && contentMouseAllowed(ram))
        captureShopIcon(ram, c, rt);
    g_origShopIconAlt(ram, c, rt);
}
// 33e960(controller, index, buy): the arrow handler's call returns to 34525c with buy 0.
void hookShopSelect(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x33e960u && getRegU32(c, 31) == 0x34525cu && contentMouseAllowed(ram)) {
        RecompGuest g(ram, c, rt);
        const int target = module().menuMouseRw().takeShopSelection(g, getRegU32(c, 4), getRegU32(c, 6));
        if (target >= 0) {
            ilog("[in] inventory mouse shop selection old=%u target=%d\n", g.read32(getRegU32(c, 4) + 0x1c), target);
            SET_GPR_U32(c, 5, static_cast<uint32_t>(target));
        }
    }
    g_origShopSelect(ram, c, rt);
}
void installInventoryContentHooks(PS2Runtime& rt) {
    struct Hook {
        uint32_t address;
        PS2Runtime::RecompiledFunction* original;
        PS2Runtime::RecompiledFunction hook;
    };
    const Hook hooks[] = {
        {0x4481e8u, &g_origSpellCells, &hookSpellCells},   {0x3422c8u, &g_origSpellSelect, &hookSpellSelect},
        {0x445b78u, &g_origSkillText, &hookSkillText},     {0x438be8u, &g_origJournalDraw, &hookJournalDraw},
        {0x4387b0u, &g_origJournalText, &hookJournalText}, {0x286170u, &g_origFontDraw, &hookFontDraw},
        {0x44a678u, &g_origShopDraw, &hookShopDraw},       {0x434e80u, &g_origShopIcon, &hookShopIcon},
        {0x4380b0u, &g_origShopIconAlt, &hookShopIconAlt}, {0x33e960u, &g_origShopSelect, &hookShopSelect},
    };
    // A missing function leaves only its own section without content capture (clicks there stay inert).
    for (const auto& h : hooks)
        if ((*h.original = rt.lookupFunction(h.address)))
            rt.replaceFunction(h.address, h.hook);
}
