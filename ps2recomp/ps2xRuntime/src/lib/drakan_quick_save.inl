// Included after drakan_menu_screens.inl. Drive the real pause and slot screens at the PadMgr boundary;
// never post a load/save call on a thread that might interrupt a live GUI/card operation.
struct QuickSaveGlue {
    int phase = 0;
    int64_t since = 0, tapAt = 0;
    bool mapped = false;
    bool fromTitle = false;
};
QuickSaveGlue g_quickSave;

bool quickSaveExists() {
    const auto& io = PS2Runtime::getIoPaths();
    const auto root = io.mcRoot.empty() ? io.elfDirectory / "mc0" : io.mcRoot;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(root / "BESCES-50006Slot33", ec), end; !ec && it != end;
         it.increment(ec)) {
        if (it->path().extension() == ".rsg" && it->is_regular_file(ec) && !ec)
            return true;
    }
    return false;
}

void quickSaveTick(uint8_t* ram, R5900Context* c, PS2Runtime* rt, RecompGuest& g, int id, bool gameplay) {
    auto& m = module();
    const auto now = menuNowMs();
    const uint32_t active = g.read32(kInvScreenVar);
    const auto finish = [&](const char* why) {
        menuLog("quick %s: %s", g_quickOperation == 1 ? "save" : "load", why);
        if (g_quickSave.mapped) {
            const auto slotScreen = id == 4 || id == 5 ? active : menuIsBox(id) ? g.read32(active + 0x34) : 0u;
            if (pauseValidPtr(slotScreen) && (pauseScreenId(g, slotScreen) == 4 || pauseScreenId(g, slotScreen) == 5)) {
                // Cancel deferred work before restoring folder names: a late timer must never write a manual slot.
                g.write32(slotScreen + 0x48, 0);
                menuWrite16(g, slotScreen + saveui::kFlags,
                            static_cast<uint16_t>(g.read16(slotScreen + saveui::kFlags) & ~0x10u));
                drakan_menus::endQuickPage(ram, c, rt, slotScreen);
            } else {
                drakan_menus::endQuickPage(ram, c, rt, 0);
            }
        }
        g_quickSave = {};
        g_quickOperation = 0;
        drakan_menu_screens::shared().quickOperation.store(0);
        if (id < 0 && g_slots.screen)
            slotsEnd("quick operation finished"); // native save/load already freed the slot list
        if (!slotsHosted() && !pauseHosted() && !titleBound())
            m.setHostScreen(false);
    };
    if (!g_quickOperation) {
        const bool title = id == 8 && !m.suspended();
        if (!m.frame().focused || (!title && (!gameplay || id >= 0 || !m.frame().captured || m.hostOwned())))
            return;
        int request = title && m.titleQuickLoadPressed() ? 2 : 0;
        for (const auto& b : m.config().game) {
            if (b.act != Binding::Save)
                continue;
            FrameInput edge = m.frame();
            edge.keyDown = edge.keyPressed;
            edge.mouseDown = edge.mousePressed;
            if (sourceLevel(b, edge, true))
                request = static_cast<int>(b.value);
        }
        if (!request)
            return;
        if (title && request != 2)
            return;
        if (request == 2 && !quickSaveExists()) {
            menuLog("quick load ignored: no quicksave exists");
            return;
        }
        g_quickOperation = request;
        drakan_menu_screens::shared().quickOperation.store(request);
        g_quickSave.phase = 1;
        g_quickSave.fromTitle = title;
        g_quickSave.since = g_quickSave.tapAt = now;
        if (!title)
            m.injectPadTap(Pad::START, 1);
        menuLog("quick %s requested", request == 1 ? "save" : "load");
        return;
    }
    const int wanted = g_quickOperation == 1 ? 5 : 4;
    if (now - g_quickSave.since > 12000) {
        finish("timed out; native screen restored");
        return;
    }
    // Native error boxes remain visible and interactive. Do not report a failed write as a successful quicksave.
    if (menuIsBox(id)) {
        finish("native error dialog; operation stopped");
        return;
    }
    m.setHostScreen(true); // neutral user input throughout; injected native taps still run
    if (g_quickSave.phase == 1 && id < 0 && now - g_quickSave.tapAt > 750) {
        m.injectPadTap(Pad::START, 1);
        g_quickSave.tapAt = now;
    }
    if (g_quickSave.phase == 1 && id == (g_quickSave.fromTitle ? 8 : kPauseScreenId)) {
        const auto row = static_cast<uint16_t>(g_quickSave.fromTitle   ? titleui::kWidgetLoad
                                               : g_quickOperation == 1 ? 0x2311
                                                                       : 0x2312);
        const auto widget = pauseWidget(g, active, row);
        if (!widget || !pauseRowEnabled(g, active, row)) {
            finish("unavailable in this game state");
            return;
        }
        if (g.read32(active + 0x84) != widget) {
            g.call(GuestCall(0x2be528u).arg(active).arg(widget));
            return;
        }
        if (!drakan_menus::beginQuickPage(ram, c, rt)) {
            finish("slot table unavailable");
            return;
        }
        g_quickSave.mapped = true;
        g_quickSave.phase = 2;
        m.injectPadTap(Pad::CROSS, 1);
        return;
    }
    if (g_quickSave.phase == 2 && id == wanted) {
        // Wait for the actual native scan and fade. All four entries describe the reserved page now.
        if (!g.read32(active + 0x14) || (g.read16(active + saveui::kFlags) & 0xc) ||
            g.read32(active + saveui::kCount) < 4)
            return;
        const auto entries = g.read32(active + saveui::kEntries);
        if (!pauseValidPtr(entries))
            return;
        g.write32(active + saveui::kSelection, 0);
        if (g_quickOperation == 2) {
            if (!g.read8(entries)) {
                finish("quicksave could not be read");
                return;
            }
            if (g_quickSave.fromTitle)
                m.injectPadTap(Pad::CROSS, 1);
            else
                menuWrite16(g, active + saveui::kFlags,
                            static_cast<uint16_t>(g.read16(active + saveui::kFlags) | 0x10));
        } else if (g.read8(entries)) {
            g.write32(active + 0x44, 0);
            g.write32(active + 0x48, 1); // native overwrite Yes -> next draw's message 9
        } else {
            m.injectPadTap(Pad::CROSS, 1); // native empty-slot save
        }
        g_quickSave.phase = 3;
        menuLog("quick %s: native operation armed", g_quickOperation == 1 ? "save" : "load");
        return;
    }
    if (g_quickSave.phase == 3 && id == kPauseScreenId) {
        // Saving returned to its parent. Restore the normal page only after the native writer has finished.
        drakan_menus::endQuickPage(ram, c, rt, active);
        g_quickSave.mapped = false;
        g_quickSave.phase = 4;
        g_quickSave.tapAt = now;
        m.injectPadTap(Pad::START, 1);
        return;
    }
    if (g_quickSave.phase >= 3 && gameplay && id < 0) {
        finish("returned to gameplay");
        return;
    }
    if (g_quickSave.phase == 4 && id == kPauseScreenId && now - g_quickSave.tapAt > 750) {
        m.injectPadTap(Pad::START, 1);
        g_quickSave.tapAt = now;
    }
}
