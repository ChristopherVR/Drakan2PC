// Included inside drakan_input_glue.cpp's anonymous namespace after drakan_inventory_views.inl.
// Modern Tab screen, EE side (docs/reference/mouse-menus.md section 13; runtime/gameui is the platform-free core, the
// overlay in drakan_settings_host.cpp draws it). With [general] inventory_screen = modern, Tab still opens inventory
// screen 0x15 natively and its per-frame draw 42fd18 keeps running (open / close animation, camera, close path), bound
// as view View::Modern of drakan_inventory_views.inl, which removes the page art, the HUD and the world crop. Here:
//   * every frame at the screen's draw message (430d68 msg 10): the gameui snapshot (Reader over rdram) is published
//     for the overlay, and queued UiActions are executed through the native routes (selection substitution at
//     33eaf8 / 3422c8 plus pad taps), one at a time, verified by gameui::ActionTracker;
//   * the native Equipment grid is scaled and moved under the host grid (the overlay publishes where), drawn over a
//     native black fill, and its cells are captured, so the overlay can show the real 3D icons by cropping its copy
//     of the frame; the details text, cell frames, gold and the large item preview are not drawn;
//   * the world crop and zoom put Rynn where the host layout leaves room for her;
//   * input: InputModule host-screen mode (neutral pad, taps only) while no native message box is on top.

class RdramMemory final : public gameui::IGuestMemory {
public:
    explicit RdramMemory(const uint8_t* ram) : ram_(ram) {}
    uint8_t read8(uint32_t a) const override {
        a &= 0x1ffffffu;
        return a < PS2_RAM_SIZE ? ram_[a] : 0;
    }
    uint32_t read32(uint32_t a) const override {
        a &= 0x1ffffffu;
        if (a + 4 > PS2_RAM_SIZE)
            return 0;
        uint32_t v;
        std::memcpy(&v, ram_ + a, 4);
        return v;
    }

private:
    const uint8_t* ram_;
};

struct TabState {
    uint32_t screen = 0;
    gameui::ReaderState reader;
    gameui::ActionTracker tracker;
    std::optional<gameui::UiAction> action; // being executed (steering or waiting for its effect)
    int wait = 0;                           // frames spent on the current action's steering
    int selectedAt = 0;                     // `wait` when the native selection reached the action's cell (0 = not yet)
    int animWait = 0;                       // frames an item action waited for the open animation
    bool pressed = false;                   // the action's button was tapped (the tracker runs)
    // Selection steering consumed by the native selectors (33eaf8 cell, 3422c8 spell).
    int steerCell = -1;
    uint32_t steerHandle = 0;
    int steerSpell = -1;
    // SelectMarker: the map selection seen last frame and for how many frames it has not moved.
    int markerSeen = -1;
    int markerStall = 0;
    uint32_t idleStreak = 0; // snapshots in a row: controller idle, no action in flight
    bool boxUp = false;
    uint32_t boxScreen = 0;
    int frames = 0;
    // The native details of the selection came up empty (the builder ran while its labels were loading): the selector
    // 33eaf8(ctl, cell) is invoked again on the EE thread (rebuildCell >= 0 asks for it).
    int rebuildCell = -1;
    uint32_t rebuildCtl = 0;
    int rebuildFrame = -100;
    int rebuilds = 0;
    // Rynn's preview: her skeleton's eased view direction (snaps on open) and the fractional part of the crop offset
    // (132f60 takes whole frame pixels; the rest is added to its projection centre afterwards).
    drakan_tab::detail::Follow previewFollow;
    std::chrono::steady_clock::time_point previewTime{};
    float previewFraction = 0;
    int previewCalls = 0;
    // Close (gameui::CloseExecutor): deferred while the open animation runs, tapped when the screen takes Select.
    gameui::CloseExecutor close;
    const char* fallBack = nullptr; // set by the executor: tabFrame restores the native page
    // EE frames per 60 Hz tick (eased): the tracker's time-outs count snapshots and assume ~60 per second, while
    // the native effects run on game time; an uncapped EE takes ~250 frames per second.
    std::chrono::steady_clock::time_point frameTime{};
    float framesPerTick = 1.0f;
    // Watchdog: the overlay's draw count (Shared::hostDrawn) when last seen moving, and since when it stands still.
    uint64_t drawnSeen = 0;
    int undrawnFrames = 0;
    std::chrono::steady_clock::time_point undrawnSince{};
};
TabState g_tab;
uint32_t g_tabGridFrame = 0; // frame of the last native grid capture

void tabLog(const char* fmt, ...) {
    static unsigned lines = 0;
    if (lines++ > 600)
        return;
    va_list ap;
    va_start(ap, fmt);
    char buf[512];
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    ilog("[tab-screen] frame=%d t=%.3f %s\n", module().frameNo(), nowS(), buf);
}

// The level's display name from the context's level path (ctx+0x98, "cdrom0:/English/Levels/Level 1A - Surdana/
// Level 1A - Surdana.lvl"; RAM dump build/harness/tabreal-series9/ram_h1): the location of the map / pause screens.
std::string readLevelName(const uint8_t* ram) {
    uint32_t ctx = 0;
    std::memcpy(&ctx, ram + 0x5565d4u, 4);
    ctx &= 0x1ffffffu;
    if (ctx < 0x100000u || ctx >= PS2_RAM_SIZE - 0x200u)
        return {};
    std::string path;
    for (uint32_t i = 0; i < 128 && ram[ctx + 0x98 + i]; ++i)
        path.push_back(char(ram[ctx + 0x98 + i]));
    return drakan_tab::levelDisplayName(path);
}

void tabBegin(const uint8_t* ram, uint32_t screen, drakan_tab::HostKind kind) {
    g_tab = {};
    g_tabGrid = {}; // a placement left from an earlier opening belongs to a freed section: never written back
    g_tab.screen = screen;
    auto& sh = drakan_tab::shared();
    sh.actions.clear();
    while (sh.results.pop())
        ;
    sh.closing.store(false);
    sh.boxUp.store(false);
    {
        const std::string location = readLevelName(ram);
        std::lock_guard<std::mutex> l(sh.gridM);
        sh.location = location;
        sh.markers.clear();
        sh.mapTarget = {};
        sh.gridTarget = {};
        sh.dialog = {};
    }
    sh.kind.store(kind);
    sh.session.fetch_add(1);
    g_tab.drawnSeen = sh.hostDrawn.load();
    g_tab.undrawnSince = std::chrono::steady_clock::now();
    sh.active.store(true);
    module().setHostScreen(true);
    tabLog("host screen %d bound to %x (location '%s')", int(kind), screen, sh.locationName().c_str());
}
void tabEnd(const char* why) {
    if (!g_tab.screen && !drakan_tab::shared().active.load())
        return;
    tabLog("modern screen ended (%s)", why);
    g_tab = {};
    if (g_tabGrid.moved) // the screen (and its sections) may be gone: never written back after this
        tabLog("grid placement of section %x dropped at the end", g_tabGrid.section);
    g_tabGrid = {};
    auto& sh = drakan_tab::shared();
    sh.active.store(false);
    sh.closing.store(false);
    sh.boxUp.store(false);
    sh.actions.clear();
    module().setHostScreen(false);
}
// A host screen (Tab screen, host strip / map) is bound.
bool tabModern() {
    return invHostView();
}
bool tabModernDrawing() {
    return invHostDrawing();
}
// The Map section draws under the modern screen's Map page or the host M view: the map hooks place the native map
// where the host wants it.
bool tabMapDrawing() {
    return invHostDrawing() && drakan_tab::shared().mapPlace().valid;
}
// Host M view: the markers drawn this frame (native GUI units), published complete at the next frame's map draw.
std::vector<drakan_tab::MapMarkerGui> g_tabMarkers;
int g_tabMarkerFrame = -1;
void tabNoteMarker(int quest, float x, float y) {
    if (g_tabMarkerFrame != module().frameNo()) {
        g_tabMarkers.clear();
        g_tabMarkerFrame = module().frameNo();
    }
    if (quest >= 0 && quest < 256 && g_tabMarkers.size() < 256)
        g_tabMarkers.push_back({quest, x, y});
}
void tabPublishMarkers() {
    auto& sh = drakan_tab::shared();
    std::lock_guard<std::mutex> l(sh.gridM);
    sh.markers = g_tabMarkers;
    ++sh.markerSerial;
    static int logged = 0;
    if (logged++ < 3 && !g_tabMarkers.empty())
        tabLog("map markers: %zu (first record %d at %.1f,%.1f)", g_tabMarkers.size(), g_tabMarkers[0].quest,
               g_tabMarkers[0].x, g_tabMarkers[0].y);
}
// Map background on the Map page: fitted into the host's map area (any scale), published for the crop.
drakan_views::MapLayout tabMapLayout(float x, float y, float w, float h, unsigned frameW, unsigned frameH) {
    drakan_views::MapLayout m;
    m.ox = x;
    m.oy = y;
    m.tw = w;
    m.th = h;
    const drakan_tab::MapTarget t = drakan_tab::shared().mapPlace();
    if (!t.valid || !(w > 0 && h > 0))
        return m;
    m.fit = drakan_views::fitCentred(w, h, t.x0, t.y0, t.x1, t.y1);
    if (!(m.fit.scale > 0.25f))
        return m;
    m.iconScale = drakan_views::markerScale(m.fit.scale);
    m.valid = true;
    auto& sh = drakan_tab::shared();
    std::lock_guard<std::mutex> l(sh.gridM);
    sh.map = {sh.map.serial + 1,         frameW, frameH, m.fit.x, m.fit.y, m.fit.x + w * m.fit.scale,
              m.fit.y + h * m.fit.scale, w,      h};
    return m;
}

// The open screen goes back to its native page for the rest of this opening (it stays open; Tab / Select close it
// natively; the next Tab opens the modern screen again): the view's removals, the world crop and the host-screen input
// mode end, the overlay fades out.
void tabFallBack(RecompGuest& g, const char* why) {
    if (!tabModern())
        return;
    tabRestoreGrid(g);
    ilog("[tab-screen] frame=%d native page restored: %s\n", module().frameNo(), why);
    if (g_invView.host) {
        // The strip / map keep their native-drawn view for the rest of this opening.
        g_invView.host = false;
        tabEnd(why);
        return;
    }
    g_invView = {};
    drakan_views::g_centredScreen.store(0, std::memory_order_relaxed);
    tabEnd(why);
}

// EE thread, every PadMgr update (from inventoryViewsTick): native boxes over the modern screen get the input.
void tabTick(RecompGuest& g) {
    if (!tabModern()) {
        if (g_tab.screen)
            tabEnd("view gone");
        return;
    }
    const bool wanted = g_invView.view == drakan_views::View::Map     ? drakan_tab::mapModern()
                        : g_invView.view == drakan_views::View::Strip ? drakan_tab::stripModern()
                                                                      : drakan_tab::enabled();
    if (!wanted || !drakan_tab::shared().layerReady.load()) {
        tabFallBack(g, "the host screen was switched to native while it was open");
        return;
    }
    const uint32_t active = g.read32(kInvScreenVar);
    const bool box = active != g_invView.screen;
    auto& sh = drakan_tab::shared();
    const uint32_t boxScreen = box ? active : 0;
    if (boxScreen != g_tab.boxScreen) {
        g_tab.boxScreen = boxScreen;
        std::lock_guard<std::mutex> l(sh.gridM);
        sh.dialog = {}; // a closed / replaced dialog must never reuse the previous one's crop
    }
    if (box != g_tab.boxUp) {
        g_tab.boxUp = box;
        tabLog("native box %s (active %x)", box ? "up" : "closed", active);
    }
    sh.boxUp.store(box);
    module().setHostScreen(!box);
}

PS2Runtime::RecompiledFunction g_origTabDialogFrame = nullptr;
void hookTabDialogFrame(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    // GUI screen frame (2bd388) or frame widget (427fc8). Other users of the same art (e.g. Skills) are not dialogs.
    const uint32_t caller = getRegU32(c, 31);
    if (c->pc == 0x2c94a8u && tabModern() && g_tab.boxUp && (caller == 0x2bd388u || caller == 0x427fc8u)) {
        RecompGuest g(ram, c, rt);
        const float x = float(int32_t(getRegU32(c, 4))), y = float(int32_t(getRegU32(c, 5)));
        const float w = float(int32_t(getRegU32(c, 6))), h = float(int32_t(getRegU32(c, 7)));
        if (w > 0 && h > 0) {
            auto content = gameui::readDialog(RdramMemory(ram), g_tab.boxScreen);
            for (auto& b : content.buttons) {
                // MenuMouse's centred-text hit region is at least 66 GUI units wide (text + glyph + margins).
                // Keep the modern card within that region without changing any native widget or hit test.
                const float bw = std::min(b.rect.w, 66.0f);
                b.rect.x = b.rect.cx() - bw * 0.5f;
                b.rect.w = bw;
            }
            auto& sh = drakan_tab::shared();
            std::lock_guard<std::mutex> l(sh.gridM);
            // 2c94a8 draws the left / right ornaments at x-28 / x+w-1 (width 27),
            // the top at y-10 and the bottom at y+h-17 (height 28). Include a two-pixel guard.
            sh.dialog = {true,   invRendererWidth(g), invRendererHeight(g), x - 30,
                         y - 12, x + w + 28,          y + h + 13,           std::move(content)};
        }
    }
    g_origTabDialogFrame(ram, c, rt); // retain native rendering and scheduler continuations
}

void tabFinish(gameui::Outcome o) {
    if (g_tab.action) {
        auto& sh = drakan_tab::shared();
        sh.results.push({g_tab.action->id, g_tab.action->kind, g_tab.action->target, o});
        // A close that did not happen gives the screen back to the host (it reopens on the result); pushed first, so
        // the overlay sees the result when it sees `closing` drop.
        if (g_tab.action->kind == gameui::ActionKind::Close && o != gameui::Outcome::Confirmed)
            sh.closing.store(false);
        tabLog("action %s(%d) -> %s", gameui::actionName(g_tab.action->kind), g_tab.action->target,
               gameui::outcomeName(o));
    }
    g_tab.action.reset();
    g_tab.wait = 0;
    g_tab.selectedAt = 0;
    g_tab.animWait = 0;
    g_tab.pressed = false;
    g_tab.steerCell = -1;
    g_tab.steerHandle = 0;
    g_tab.steerSpell = -1;
}

uint16_t tabButton(gameui::ActionKind k) {
    using gameui::ActionKind;
    switch (k) {
        case ActionKind::UseOrEquip:
            return Pad::SQUARE;
        case ActionKind::Drop:
            return Pad::TRIANGLE;
        case ActionKind::ToggleHotSlot:
            return Pad::CROSS;
        case ActionKind::Sell:
            return Pad::CIRCLE;
        case ActionKind::CastOrSummon:
            return Pad::SQUARE;
        default:
            return 0;
    }
}

// One executor step per frame on the fresh snapshot `s`.
void tabExecute(const gameui::Snapshot& s) {
    using gameui::ActionKind;
    using gameui::Outcome;
    auto& sh = drakan_tab::shared();
    InputModule& m = module();
    // Snapshots in a row with an idle controller and no action in flight (an arrow tap of a steer may still be on its
    // way): what a close may count as already settled (CloseExecutor::begin).
    const bool settled = !s.native.controllerBusy && !g_tab.action && !g_tab.pressed;
    g_tab.idleStreak = settled ? std::min(g_tab.idleStreak + 1, 1000u) : 0u;
    if (g_tab.pressed) {
        if (auto r = g_tab.tracker.update(s)) {
            g_tab.pressed = false;
            tabFinish(r->outcome);
        }
        return;
    }
    const auto selection = [](ActionKind k) { return k == ActionKind::SelectItem || k == ActionKind::SelectMarker; };
    if (g_tab.action && selection(g_tab.action->kind) && sh.closing.load()) {
        tabLog("%s(%d) dropped: the screen is closing", gameui::actionName(g_tab.action->kind), g_tab.action->target);
        return tabFinish(Outcome::Aborted);
    }
    if (!g_tab.action) {
        if (g_tab.boxUp)
            return;
        auto a = sh.actions.pop();
        if (!a)
            return;
        if (sh.closing.load() && selection(a->kind)) {
            // The screen is closing: a selection change now would only overlap the teardown.
            sh.results.push({a->id, a->kind, a->target, Outcome::Aborted});
            return;
        }
        g_tab.action = *a;
        g_tab.wait = 0;
        tabLog("action %s(%d) handle %x", gameui::actionName(a->kind), a->target, a->handle);
    }
    const gameui::UiAction a = *g_tab.action;
    if (g_tab.boxUp)
        return; // a native box has the input: the action waits for it
    const int wait = ++g_tab.wait;
    if (a.kind == ActionKind::Close) {
        // The screen's own close (Select: 42f640 -> 431d10 -> 42fd18 -> 42f758 restores the camera), deferred while the
        // open animation runs (Select is ignored then), repeated when a tap was ignored.
        if (wait == 1) {
            sh.closing.store(true);
            // A settled controller: the Select tap goes at once (this snapshot is counted by update itself).
            g_tab.close.begin(g_tab.idleStreak > 0 ? g_tab.idleStreak - 1 : 0);
        }
        using Step = gameui::CloseExecutor::Step;
        switch (g_tab.close.update(s)) {
            case Step::Wait:
                if (wait == 1 && s.native.animation > 0)
                    tabLog("close deferred: the open animation is running");
                return;
            case Step::Tap:
                tabLog("close: Select tap %d (deferred %u frames, waited %u for the controller)", g_tab.close.taps(),
                       g_tab.close.deferredFrames(), g_tab.close.busyFrames());
                m.injectPadTap(Pad::SELECT, 2);
                return;
            case Step::Done:
                return tabFinish(Outcome::Confirmed);
            case Step::Failed:
                g_tab.fallBack = "the screen did not close";
                return tabFinish(Outcome::Refused);
        }
        return;
    }
    if (!s.native.valid)
        return tabFinish(Outcome::Aborted);
    switch (a.kind) {
        case ActionKind::SelectItem:
        case ActionKind::UseOrEquip:
        case ActionKind::Drop:
        case ActionKind::ToggleHotSlot:
        case ActionKind::Sell: {
            const gameui::ItemEntry* it = s.itemAtCell(a.target);
            if (!it || it->originCell != a.target || (a.handle && it->handle != a.handle) || s.native.section != 0 ||
                s.native.pane != 0)
                return tabFinish(Outcome::Refused);
            if (s.native.animation > 0 && ++g_tab.animWait < 600) {
                // The screen's open animation ignores the arrows: wait for it (on the Tab screen it outlasted the
                // steering budget, so the first selection after opening was refused).
                g_tab.wait = 0;
                return;
            }
            if (s.native.selection != a.target || s.native.controllerBusy) {
                if (wait > 60)
                    return tabFinish(Outcome::Refused);
                g_tab.steerCell = a.target;
                g_tab.steerHandle = it->handle;
                // A native arrow event runs the selector 33eaf8, whose destination the hook replaces. Alternate axes
                // in case a row holds only the current multi-cell item.
                if (!s.native.controllerBusy && wait % 3 == 1)
                    m.injectPadTap(((wait / 3) % 2) ? Pad::DOWN : Pad::RIGHT, 1);
                return;
            }
            g_tab.steerCell = -1;
            if (!g_tab.selectedAt)
                g_tab.selectedAt = wait;
            const uint16_t button = tabButton(a.kind);
            if (!button) {
                // Selected, but its details came up empty (labels still loading when the screen opened): the
                // selector is run again (tabFrame), a few times at most. The window counts from the moment the
                // selection arrived: steering right after the open takes ~45 EE frames (the strip's bow came up
                // with no stat lines when it counted from the action's start).
                if (!s.native.detailsValid && g_tab.rebuilds < 3 && wait - g_tab.selectedAt < 40) {
                    if (g_tab.rebuildCell < 0 && g_tab.frames - g_tab.rebuildFrame > 8) {
                        g_tab.rebuildCell = a.target;
                        ++g_tab.rebuilds;
                    }
                    return;
                }
                return tabFinish(Outcome::Confirmed);
            }
            m.injectPadTap(button, 1);
            g_tab.tracker.begin(a, s, g_tab.framesPerTick);
            g_tab.pressed = true;
            return;
        }
        case ActionKind::SelectSpell:
        case ActionKind::CastOrSummon: {
            if (s.native.section != 1 || a.target < 0 || a.target >= int(s.spells.entries.size()) ||
                s.spells.entries[size_t(a.target)].knownLevel <= 0 || s.spells.summonBusy())
                return tabFinish(Outcome::Refused);
            if (s.spells.selected != a.target) {
                if (wait > 60)
                    return tabFinish(Outcome::Refused);
                g_tab.steerSpell = a.target;
                if (wait % 3 == 1)
                    m.injectPadTap(((wait / 3) % 2) ? Pad::DOWN : Pad::RIGHT, 1);
                return;
            }
            g_tab.steerSpell = -1;
            if (a.kind == ActionKind::SelectSpell)
                return tabFinish(Outcome::Confirmed);
            m.injectPadTap(Pad::SQUARE, 1);
            g_tab.tracker.begin(a, s, g_tab.framesPerTick);
            g_tab.pressed = true;
            return;
        }
        case ActionKind::SwitchSection: {
            if (s.native.section == a.target)
                return tabFinish(Outcome::Confirmed);
            if (wait > 90 || a.target < 0 || a.target >= drakan_views::kSectionCount)
                return tabFinish(Outcome::Refused);
            if (wait % 4 == 1) {
                const int n = drakan_views::kSectionCount, cur = std::max(0, s.native.section);
                const int forward = (a.target - cur + n) % n;
                m.injectPadTap(forward <= n - forward ? Pad::R1 : Pad::L1, 1);
            }
            return;
        }
        case ActionKind::SelectMarker: {
            // The map's selection (section +0x6c, a quest record) follows the host's focused location: the native
            // arrow handler 43a588 steps it to the next marker of the shown region, so RIGHT is tapped every third
            // frame until it is the target (the map mouse's route, MenuMouse::updateMap). Only taps: nothing is written
            // into the screen, so nothing can outlive it. Bounded: one lap over the records plus a margin, and a
            // selection that stops moving ends it.
            if (s.native.section != drakan_views::kMapSection || s.native.mapSelection < 0 || a.target < 0)
                return tabFinish(Outcome::Refused);
            if (s.native.mapSelection == a.target)
                return tabFinish(Outcome::Confirmed);
            if (s.native.animation != 0 && ++g_tab.animWait < 600) {
                g_tab.wait = 0; // the open animation ignores the arrows
                return;
            }
            if (wait == 1) {
                g_tab.markerSeen = s.native.mapSelection;
                g_tab.markerStall = 0;
            }
            g_tab.markerStall = s.native.mapSelection == g_tab.markerSeen ? g_tab.markerStall + 1 : 0;
            g_tab.markerSeen = s.native.mapSelection;
            const int lap = 3 * (int(s.quests.size()) + 2);
            if (wait > lap || g_tab.markerStall > 12)
                return tabFinish(Outcome::Refused);
            if (wait % 3 == 1)
                m.injectPadTap(Pad::RIGHT, 1);
            return;
        }
        case ActionKind::Close:
            return; // handled above
        case ActionKind::SpendSkillPoint:
        case ActionKind::ReturnSkillPoint: {
            // Skills (section 2): the row is the section's +0x58, stepped by Up / Down taps (three rows, wrapping;
            // rows only move while points are unspent); Right spends a point, Left takes one back (445898).
            const bool spend = a.kind == ActionKind::SpendSkillPoint;
            if (s.native.section != 2 || s.native.skillRow < 0 || a.target < 0 || a.target > 2 ||
                (spend && s.skills.unspentPoints <= 0))
                return tabFinish(Outcome::Refused);
            if (s.native.skillRow != a.target) {
                if (wait > 60)
                    return tabFinish(Outcome::Refused);
                if (wait % 3 == 1)
                    m.injectPadTap((a.target - s.native.skillRow + 3) % 3 == 1 ? Pad::DOWN : Pad::UP, 1);
                return;
            }
            m.injectPadTap(spend ? Pad::RIGHT : Pad::LEFT, 1);
            g_tab.tracker.begin(a, s, g_tab.framesPerTick);
            g_tab.pressed = true;
            return;
        }
        case ActionKind::ShopBrowse:
        case ActionKind::Buy:
            // Shops stay native (three-section screen; docs/reference/mouse-menus.md section 13).
            return tabFinish(Outcome::Refused);
    }
}

// Runs the native selector 33eaf8(ctl, cell) as a guest invocation of the current thread (like InvDrawBatch: the
// hook unwinds, the scheduler runs it, the thread resumes at the hooked entry, 430d68 msg 10, which runs tabFrame
// again). It rebuilds the details buffers (342f90) for the cell.
void tabInvokeSelector(uint8_t* ram, R5900Context* c, PS2Runtime* rt, uint32_t ctl, int cell) {
    if (!rt->hasFunction(0x33eaf8u))
        return;
    GuestInvocation call{};
    call.kind = GuestInvocationKind::HleCall;
    call.context = *c;
    SET_GPR_U32(&call.context, 4, ctl);
    SET_GPR_U32(&call.context, 5, uint32_t(cell));
    SET_GPR_U32(&call.context, 29, ((getRegU32(c, 29) - 0x500u) & ~15u) - 0x10u);
    SET_GPR_U32(&call.context, 31, 0u);
    call.context.pc = 0x33eaf8u;
    call.context.in_delay_slot = false;
    std::vector<GuestInvocation> calls;
    calls.push_back(std::move(call));
    EeScheduler& ee = rt->eeScheduler();
    ee.bindMainContextForSyscall(*c, ram);
    ee.invokeCurrentSequence(std::move(calls)); // does not return
}

// Strings the map and pause panels show (objective texts, marker captions) are only resident once something asked the
// string manager for them (the native Journal page does, the map only for the selected marker's caption), so outside
// the Journal the objectives fell back to the region name. The missing ids are requested once each (per level) through
// the game's own lookup 1b9fb0(mgr = ctx+0x344, id, buf, 1, max): with flag 1 it starts an asynchronous load and
// returns (docs/journal/2026-10-24/tab-rework-data-model.md). The calls run as guest invocations of the current thread
// (they may wait on the manager's semaphore), so this does not return when anything was queued; the hooked function
// is entered again afterwards and finds nothing left to ask.
std::vector<uint32_t> g_stringsAsked;
std::string g_stringsLevel;
void tabRequestStrings(uint8_t* ram, R5900Context* c, PS2Runtime* rt, const gameui::Snapshot& s) {
    constexpr uint32_t kLookup = 0x1b9fb0u;
    if (!rt->hasFunction(kLookup))
        return;
    if (const std::string level = readLevelName(ram); level != g_stringsLevel) {
        g_stringsLevel = level;
        g_stringsAsked.clear();
    }
    std::vector<uint32_t> ids;
    const auto want = [&](uint32_t id) {
        if (id && ids.size() < 12 &&
            std::find(g_stringsAsked.begin(), g_stringsAsked.end(), id) == g_stringsAsked.end())
            ids.push_back(id);
    };
    for (const gameui::QuestEntry& q : s.quests) {
        if (q.listed() && !q.textResident)
            want(q.textId);
        if (q.enabled && q.marker() && !q.captionResident)
            want(q.captionId);
    }
    if (ids.empty() || g_stringsAsked.size() > 512)
        return;
    RecompGuest g(ram, c, rt);
    const uint32_t ctx = g.read32(0x5565d4u);
    const uint32_t mgr = ctx >= 0x100000u && ctx < PS2_RAM_SIZE - 0x400u ? g.read32(ctx + 0x344) : 0;
    if (mgr < 0x100000u || mgr >= PS2_RAM_SIZE - 0x400u)
        return;
    const uint32_t sp = getRegU32(c, 29);
    const uint32_t buf = (sp - 0x300u) & ~15u; // 0x100 bytes below the hook's frame, above the calls' stack
    std::vector<GuestInvocation> calls;
    for (uint32_t id : ids) {
        g_stringsAsked.push_back(id);
        GuestInvocation call{};
        call.kind = GuestInvocationKind::HleCall;
        call.context = *c;
        const uint32_t args[5] = {mgr, id, buf, 1u, 0xffu};
        for (int i = 0; i < 5; ++i) {
            const int reg = 4 + i; // SET_GPR_U32 evaluates its register index twice
            SET_GPR_U32(&call.context, reg, args[i]);
        }
        SET_GPR_U32(&call.context, 29, ((sp - 0x500u) & ~15u) - 0x10u);
        SET_GPR_U32(&call.context, 31, 0u);
        call.context.pc = kLookup;
        call.context.in_delay_slot = false;
        calls.push_back(std::move(call));
    }
    static int logged = 0;
    if (logged++ < 8)
        ilog("[tab-screen] frame=%d %zu panel string(s) requested (first %x)\n", module().frameNo(), ids.size(),
             ids[0]);
    EeScheduler& ee = rt->eeScheduler();
    ee.bindMainContextForSyscall(*c, ram);
    ee.invokeCurrentSequence(std::move(calls)); // does not return
}

// UI feedback sound of the host screen through the game's own path (MenuMouse::playUiSound): the property lookups
// (UI entry 2bec48, the property table method) as nested calls, the play itself (sound manager ctx+0x32c, method
// +0x114) as a guest invocation of the current thread, so a play that waits on the sound manager's semaphore runs on
// the scheduler (a nested call that blocks would strand the hooked thread). Does not return when a play is queued.
void tabPlaySound(uint8_t* ram, R5900Context* c, PS2Runtime* rt, uint32_t property) {
    RecompGuest g(ram, c, rt);
    const auto ok = [](uint32_t a) { return a >= 0x100000u && a < PS2_RAM_SIZE - 0x200u; };
    const auto code = [](uint32_t a) { return a >= 0x100000u && a < 0x600000u; };
    const uint32_t entry = g.call(GuestCall(module().config().addrUiEntry));
    const uint32_t obj = ok(entry) ? g.read32(entry + 0xc) : 0;
    const uint32_t vt = ok(obj) ? g.read32(obj + 8) : 0;
    if (!ok(vt) || !code(g.read32(vt + 0x1c)))
        return;
    const auto lookup = [&](uint32_t prop) {
        GuestCall pc(g.read32(vt + 0x1c));
        pc.arg(obj + uint32_t(int32_t(g.readS16(vt + 0x18)))).arg(prop).arg(0xffffffffu);
        const uint32_t idp = g.call(pc);
        return ok(idp) ? g.read32(idp) : 0u;
    };
    uint32_t id = lookup(property);
    if (!id && (property == 0x27u || property == 0x28u)) // no slider / check box sound in the UI entry: the click
        id = lookup(property == 0x27u ? 0x2au : 0x24u);
    const uint32_t ctx = g.read32(0x5565d4u);
    const uint32_t snd = ok(ctx) ? g.read32(ctx + 0x32c) : 0;
    const uint32_t svt = ok(snd) ? g.read32(snd) : 0;
    if (!id || !ok(svt) || !code(g.read32(svt + 0x114)) || !rt->hasFunction(g.read32(svt + 0x114)))
        return;
    GuestInvocation call{};
    call.kind = GuestInvocationKind::HleCall;
    call.context = *c;
    call.context.f[12] = 0.0f;
    SET_GPR_U32(&call.context, 4, snd + uint32_t(int32_t(g.readS16(svt + 0x110))));
    SET_GPR_U32(&call.context, 5, id);
    SET_GPR_U32(&call.context, 6, 0x4e8c28u); // the 3-vector the game passes to its UI sounds
    SET_GPR_U32(&call.context, 7, 4u);
    SET_GPR_U32(&call.context, 29, ((getRegU32(c, 29) - 0x500u) & ~15u) - 0x10u);
    SET_GPR_U32(&call.context, 31, 0u);
    call.context.pc = g.read32(svt + 0x114);
    call.context.in_delay_slot = false;
    static int logged = 0;
    if (logged++ < 64)
        ilog("[ui-sound] frame=%d property %x: sound id %x\n", module().frameNo(), property, id);
    std::vector<GuestInvocation> calls;
    calls.push_back(std::move(call));
    EeScheduler& ee = rt->eeScheduler();
    ee.bindMainContextForSyscall(*c, ram);
    ee.invokeCurrentSequence(std::move(calls)); // does not return
}

// Watchdog, every screen draw message: the native page is hidden while the modern screen is bound, so the overlay must
// be drawing it. When it has not drawn for kWatchFrames EE frames and kWatchTime (not ready, faded out, closing that
// never finished, an invalid snapshot ...), the native page comes back. A native box on top is expected to hide it.
void tabWatchdog(RecompGuest& g) {
    constexpr int kWatchFrames = 90;
    constexpr auto kWatchTime = std::chrono::milliseconds(2500);
    auto& sh = drakan_tab::shared();
    const auto now = std::chrono::steady_clock::now();
    const uint64_t drawn = sh.hostDrawn.load();
    if (drawn != g_tab.drawnSeen || g_tab.boxUp) {
        g_tab.drawnSeen = drawn;
        g_tab.undrawnFrames = 0;
        g_tab.undrawnSince = now;
        return;
    }
    if (++g_tab.undrawnFrames < kWatchFrames || now - g_tab.undrawnSince < kWatchTime)
        return;
    char why[160];
    std::snprintf(why, sizeof why, "watchdog: host UI not drawn for %d frames (%.1f s; closing %d, layer %d)",
                  g_tab.undrawnFrames, std::chrono::duration<double>(now - g_tab.undrawnSince).count(),
                  sh.closing.load() ? 1 : 0, sh.layerReady.load() ? 1 : 0);
    tabFallBack(g, why);
}

// 430d68 msg 10 on the modern screen: snapshot, publish, execute.
void tabFrame(uint8_t* ram, R5900Context* c, PS2Runtime* rt, uint32_t screen) {
    if (!tabModern() || screen != g_invView.screen)
        return;
    if (g_tab.screen != screen)
        tabBegin(ram, screen,
                 g_invView.view == drakan_views::View::Map     ? drakan_tab::HostKind::Map
                 : g_invView.view == drakan_views::View::Strip ? drakan_tab::HostKind::Strip
                                                               : drakan_tab::HostKind::Tab);
    if (const uint32_t sound = drakan_tab::shared().sound.exchange(0); sound && !g_tab.boxUp)
        tabPlaySound(ram, c, rt, sound); // returns only when nothing was queued
    // The M view and the Tab screen's Map section: its locations list read "Location n" for every marker caption that
    // nothing had asked for yet (only the selected marker's is loaded natively).
    const bool mapStrings = g_invView.view == drakan_views::View::Map ||
                            (g_invView.view == drakan_views::View::Modern &&
                             int32_t(RecompGuest(ram, c, rt).read32(screen + 0x154)) == drakan_views::kMapSection);
    if (mapStrings && g_tab.frames > 2 && !g_tab.boxUp)
        if (const auto s = drakan_tab::shared().model.acquire())
            tabRequestStrings(ram, c, rt, *s); // returns only when nothing was queued
    if (g_tab.rebuildCell >= 0) {
        RecompGuest g(ram, c, rt);
        const uint32_t player = g.read32(screen + 0x14c);
        const uint32_t ctl = player >= 0x100000u && player < PS2_RAM_SIZE ? player + 0x584 : 0;
        const int cell = g_tab.rebuildCell;
        g_tab.rebuildCell = -1;
        g_tab.rebuildFrame = g_tab.frames;
        if (ctl && g.read32(ctl) == 0 && int(g.read32(ctl + 0x20)) == cell && g.read32(ctl + 0x24) == 0) {
            tabLog("details of cell %d empty: selector run again", cell);
            tabInvokeSelector(ram, c, rt, ctl, cell);
        }
    }
    {
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - g_tab.frameTime).count();
        if (g_tab.frameTime.time_since_epoch().count() != 0 && dt > 0 && dt < 0.25f)
            g_tab.framesPerTick += 0.1f * (std::clamp((1.0f / 60.0f) / dt, 1.0f, 16.0f) - g_tab.framesPerTick);
        g_tab.frameTime = now;
    }
    auto& sh = drakan_tab::shared();
    RdramMemory mem(ram);
    gameui::Snapshot& back = sh.model.back();
    const gameui::ReadStatus st = gameui::readSnapshot(mem, g_tab.reader, back);
    if (++g_tab.frames <= 2 || st != gameui::ReadStatus::Ok)
        tabLog("snapshot %s: %zu items, section %d, selection %d", gameui::readStatusName(st), back.items.size(),
               back.native.section, back.native.selection);
    { // details of each new native selection (verification aid)
        static int lastSel = -2, lastCount = -1;
        const int count = int(back.details.size());
        if (back.native.selection != lastSel || count != lastCount) {
            lastSel = back.native.selection;
            lastCount = count;
            std::string lines;
            for (const std::string& l : back.details)
                lines += "[" + l + "]";
            tabLog("selection %d valid=%d busy=%d details %d: %s", lastSel, back.native.detailsValid ? 1 : 0,
                   back.native.controllerBusy ? 1 : 0, count, lines.c_str());
        }
    }
    sh.model.publish();
    if (auto s = sh.model.acquire())
        tabExecute(*s);
    RecompGuest g(ram, c, rt);
    if (g_tab.fallBack)
        return tabFallBack(g, g_tab.fallBack);
    tabWatchdog(g);
}

// 33eaf8(ctl, cell) from the native arrow handler: an outstanding steer replaces the destination.
int tabSteerCell(RecompGuest& g, uint32_t ctl) {
    if (g_tab.steerCell < 0 || !tabModern() || g.read32(ctl + 0x24) != 0 ||
        g.read32(ctl + 0x38 + uint32_t(g_tab.steerCell) * 16) != g_tab.steerHandle)
        return -1;
    return g_tab.steerCell;
}
int tabSteerSpell() {
    return tabModern() ? g_tab.steerSpell : -1;
}

// Grid draw 437170(section) on the modern screen, after the grid was placed: the cells for the icon crops.
void tabCaptureGrid(RecompGuest& g, uint32_t section) {
    if (!tabModern() || !g_invView.screen)
        return;
    const uint32_t ctl = g.read32(section + 0x70), transform = g.read32(section + 0xc);
    const auto ok = [](uint32_t a) { return a >= 0x100000u && a < PS2_RAM_SIZE - 0x500u; };
    if (!ok(ctl) || !ok(transform) || g.read32(g_invView.screen + 0x154) != 0 || g.read32(ctl + 0x24) != 0)
        return;
    const float cw = g.readF(section + 0x60), ch = g.readF(section + 0x64);
    const float ox = g.readF(section + 0x68), oy = g.readF(section + 0x6c);
    if (!(cw > 0 && ch > 0) || !std::isfinite(ox) || !std::isfinite(oy))
        return;
    drakan_tab::NativeGrid grid;
    grid.frameW = invRendererWidth(g);
    grid.frameH = invRendererHeight(g);
    for (int cell = 0; cell < 24; ++cell) {
        float x[2], y[2];
        const float u0 = ox + (float(cell % 8) - 0.5f) * cw, v0 = oy - (float(cell / 8) - 0.5f) * ch;
        if (!invProject(g, transform, u0, v0, x[0], y[0]) || !invProject(g, transform, u0 + cw, v0 - ch, x[1], y[1]))
            return;
        grid.cells[size_t(cell)] = {std::min(x[0], x[1]), std::min(y[0], y[1]), std::max(x[0], x[1]),
                                    std::max(y[0], y[1])};
    }
    auto& sh = drakan_tab::shared();
    std::lock_guard<std::mutex> l(sh.gridM);
    grid.serial = sh.grid.serial + 1;
    sh.grid = grid;
    g_tabGridFrame = uint32_t(module().frameNo());
}

// Equipment draw 433610 on the modern screen: scale the grid (cell spacing +0x60 / +0x64, which also sizes the icons,
// 4346d0) and move its centre (+0x68 / +0x6c) so its projection covers the host grid's cells; restored after the draw.
// A draw that yields (texture load, GIF kick) unwinds the hook, so the restore then happens at the section's next
// draw. Only ever into that live section (`section` = the one being drawn, 0 = the bound screen is known to be open):
// the inventory's sections are freed when the screen closes, and writing the saved floats into the old address after
// a close corrupted whatever had been allocated there (an item object's texture list at +0x60: the game died in the
// item destructor 178f28 -> 17a030 after a Tab close, drakan.log of the user's session, 2026-10-25).
void tabRestoreGrid(RecompGuest& g, uint32_t section) {
    if (!g_tabGrid.moved)
        return;
    if (section == 0 || section == g_tabGrid.section) {
        g.writeF(g_tabGrid.section + 0x60, g_tabGrid.cw);
        g.writeF(g_tabGrid.section + 0x64, g_tabGrid.ch);
        g.writeF(g_tabGrid.section + 0x68, g_tabGrid.x0);
        g.writeF(g_tabGrid.section + 0x6c, g_tabGrid.y0);
    } else {
        tabLog("grid of section %x left moved (screen gone): dropped, not written", g_tabGrid.section);
    }
    g_tabGrid.moved = false;
}
void tabPlaceGrid(RecompGuest& g, uint32_t section) {
    using namespace drakan_views;
    tabRestoreGrid(g, section); // a yielded draw left them moved
    g_tabGrid = {};
    g_tabGrid.section = section;
    const drakan_tab::GridTarget t = drakan_tab::shared().target();
    if (!t.valid) {
        g_tabGrid.hidden = true;
        return;
    }
    const uint32_t transform = g.read32(section + 0xc);
    const float x0 = g.readF(section + 0x68), y0 = g.readF(section + 0x6c);
    const float cw = g.readF(section + 0x60), ch = g.readF(section + 0x64);
    if (!(transform >= 0x100000u && transform < PS2_RAM_SIZE && cw > 0 && ch > 0 && std::isfinite(x0) &&
          std::isfinite(y0)))
        return;
    // Current projected grid size -> uniform scale for the cell spacing.
    float u = gridCentrePlaneX(x0, cw), v = gridCentrePlaneY(y0, ch);
    float sx = 0, sy = 0, ax = 0, ay = 0, bx = 0, by = 0;
    if (!invProject(g, transform, u, v, sx, sy) || !invProject(g, transform, u + cw, v, ax, ay) ||
        !invProject(g, transform, u, v + ch, bx, by))
        return;
    const float gridW = std::fabs(ax - sx) * 8, gridH = std::fabs(by - sy) * 3;
    const float kx = (t.x1 - t.x0) / std::max(gridW, 1.0f), ky = (t.y1 - t.y0) / std::max(gridH, 1.0f);
    const float k = std::clamp(0.5f * (kx + ky), 0.25f, 4.0f);
    const float ncw = cw * k, nch = ch * k;
    const float tx = 0.5f * (t.x0 + t.x1), ty = 0.5f * (t.y0 + t.y1);
    u = gridCentrePlaneX(x0, ncw);
    v = gridCentrePlaneY(y0, nch);
    bool ok = true;
    for (int step = 0; ok && step < 3; ++step) {
        ok = invProject(g, transform, u, v, sx, sy) && invProject(g, transform, u + ncw, v, ax, ay) &&
             invProject(g, transform, u, v + nch, bx, by);
        if (!ok)
            break;
        const float dxdu = (ax - sx) / ncw, dydu = (ay - sy) / ncw, dxdv = (bx - sx) / nch, dydv = (by - sy) / nch;
        ok = solvePlane(u, v, sx, sy, dxdu, dxdv, dydu, dydv, tx, ty, u, v);
    }
    if (!ok)
        return;
    g_tabGrid.cw = cw;
    g_tabGrid.ch = ch;
    g_tabGrid.x0 = x0;
    g_tabGrid.y0 = y0;
    g_tabGrid.moved = true;
    g_tabGrid.target = {t.x0, t.y0, t.x1, t.y1};
    g.writeF(section + 0x60, ncw);
    g.writeF(section + 0x64, nch);
    g.writeF(section + 0x68, x0 + (u - gridCentrePlaneX(x0, ncw)));
    g.writeF(section + 0x6c, y0 + (v - gridCentrePlaneY(y0, nch)));
    static int logged = 0;
    if (logged++ < 3)
        tabLog("grid scale %.3f, centre -> %.1f,%.1f (target %.1f,%.1f..%.1f,%.1f)", k, tx, ty, t.x0, t.y0, t.x1, t.y1);
}

// Rynn's view direction for the preview: the mean view x / depth of her animated joints (player+0x480, 12-byte
// positions, count +0x488; ctx+0x4b0 -> +0xfc -> +0x10 = player) through the world camera object (ctx+0x4b4 -> +0xfc
// -> +0x10 -> +0x148). Her root (player+0x80 transform) stays put while she idles; the turns, steps and bends that
// moved her off the column are all in the joints. False when anything is missing.
bool tabPreviewViewX(RecompGuest& g, float& viewX) {
    const auto ok = [](uint32_t a) { return a >= 0x100000u && a < PS2_RAM_SIZE - 0x200u; };
    const uint32_t ctx = g.read32(0x5565d4u);
    if (!ok(ctx))
        return false;
    const auto inner = [&](uint32_t handleOffset) {
        const uint32_t handle = g.read32(ctx + handleOffset), holder = ok(handle) ? g.read32(handle + 0xfc) : 0;
        return ok(holder) ? g.read32(holder + 0x10) : 0;
    };
    const uint32_t player = inner(0x4b0), cam = inner(0x4b4);
    const uint32_t camObj = ok(cam) ? g.read32(cam + 0x148) : 0;
    if (!ok(player) || !ok(camObj))
        return false;
    const uint32_t joints = g.read32(player + 0x480), count = g.read32(player + 0x488);
    constexpr uint32_t kMaxJoints = 64;
    if (count == 0 || count > kMaxJoints || joints < 0x100000u || joints + count * 12 > PS2_RAM_SIZE)
        return false;
    drakan_tab::detail::CameraFrame frame;
    frame.pos = {g.readF(camObj + 0x14), g.readF(camObj + 0x18), g.readF(camObj + 0x1c)};
    for (uint32_t i = 0; i < 9; ++i)
        frame.m[i] = g.readF(camObj + 0x20 + 4 * i);
    drakan_tab::detail::Vec3 pts[kMaxJoints];
    for (uint32_t i = 0; i < count; ++i)
        pts[i] = {g.readF(joints + 12 * i), g.readF(joints + 12 * i + 4), g.readF(joints + 12 * i + 8)};
    return drakan_tab::detail::skeletonViewX(pts, count, frame, viewX);
}

// World crop for the modern screen (132f60 from 2c4444): Rynn stands at the host's preview centre, at the native
// preview zoom scaled by the host's factor (1 = native). 132f60 keeps the projection centre at W/2 + offset and the
// character size fixed; the crop must cover the screen: s >= 1 + 2|offset| / W. The offset also follows her skeleton
// (eased, drakan_tab_preview_policy.h), so her idle turns and steps do not walk her off the column; without a usable
// skeleton or world projection it is the fixed offset. Returns the offset's fraction of a frame pixel (applied by the
// caller after 132f60).
float tabWorldCrop(RecompGuest& g, R5900Context* c) {
    const drakan_tab::PreviewTarget p = drakan_tab::shared().previewTarget();
    const float w = float(invRendererWidth(g));
    const float cx = p.valid ? p.centreX : 0.5f;
    const auto now = std::chrono::steady_clock::now();
    float dt = -1; // snap
    if (g_tab.previewFollow.valid)
        dt = std::chrono::duration<float>(now - g_tab.previewTime).count();
    if (dt > 0.5f) // the crop was not applied for a while (a box was up): start from her current pose
        dt = -1;
    g_tab.previewTime = now;
    float viewX = 0;
    const float projX = drakan_ws::worldProjectionX();
    float shown = 0;
    if (p.valid && projX > 0 && tabPreviewViewX(g, viewX)) {
        constexpr float kSmoothTime = 0.35f; // seconds
        shown = drakan_tab::detail::followStep(g_tab.previewFollow, viewX, dt, kSmoothTime);
    } else {
        g_tab.previewFollow = {};
    }
    constexpr float kMaxShift = 0.2f; // of the frame width
    const drakan_tab::detail::Crop crop = drakan_tab::detail::previewCrop(cx, shown, projX, w, kMaxShift);
    const float whole = std::round(crop.offset);
    g_tab.previewFraction = crop.offset - whole;
    c->f[12] = crop.scale + 2.0f / w; // the fraction moves the centre by up to half a pixel
    SET_GPR_U32(c, 5, uint32_t(int32_t(whole)));
    static const bool log = [] {
        const char* e = std::getenv("DRAKAN_TAB_PREVIEW_LOG");
        return e && std::atoi(e) > 0;
    }();
    if (log && g_tab.previewCalls++ % 15 == 0)
        tabLog("preview: viewX %.4f eased %.4f projX %.2f offset %.2f scale %.3f", viewX, shown, projX, crop.offset,
               c->f[12]);
    if (!p.valid || std::fabs(p.zoom - 1.0f) < 0.01f)
        return g_tab.previewFraction;
    // The open animation's zoom (42fd18: cam+0x74/+0x78 = saved * (1 - 0.6 t)), eased towards the saved camera.
    const auto ok = [](uint32_t a) { return a >= 0x100000u && a < PS2_RAM_SIZE - 0x200u; };
    const uint32_t ctx = g.read32(0x5565d4u);
    const uint32_t handle = ok(ctx) ? g.read32(ctx + 0x4b4) : 0, holder = ok(handle) ? g.read32(handle + 0xfc) : 0;
    const uint32_t cam = ok(holder) ? g.read32(holder + 0x10) : 0;
    const uint32_t screen = g_invView.screen;
    const float zx = g.readF(screen + 0x160), zy = g.readF(screen + 0x164);
    if (!ok(cam) || !(zx > 0 && zy > 0) || !std::isfinite(zx) || !std::isfinite(zy))
        return g_tab.previewFraction;
    const float f = g.readF(cam + 0x74) / zx;
    if (!(f > 0.05f && f <= 1.0f))
        return g_tab.previewFraction;
    const float nf = 1.0f - (1.0f - f) * std::clamp(p.zoom, 0.0f, 1.5f);
    g.writeF(cam + 0x74, zx * nf);
    g.writeF(cam + 0x78, zy * nf);
    return g_tab.previewFraction;
}
