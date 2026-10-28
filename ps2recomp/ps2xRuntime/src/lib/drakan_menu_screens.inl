// Included inside drakan_input_glue.cpp's anonymous namespace after drakan_pause_screen.inl.
// Modern title menu (screen 8) and Save / Load screens (Load 4, Save 5): docs/reference/mouse-menus.md section 15,
// runtime/gameui/README.md ("Save / Load and title screens"), research in
// docs/journal/2026-10-25/modern-save-load-title.md, integration notes in
// docs/journal/2026-10-25/modern-save-title-settings-integration.md.
//
// As with the modern pause menu, the native screens open natively and stay the active screens; the overlay draws the
// gameui screens over them and this glue drives them through their own paths:
//   * binding: polled every PadMgr update (the active screen *0x4fb60c): the title while it or one of its children is
//     up, a slot screen while it or its box / Saving screen is up;
//   * hiding: the screen draw 2bd1b0 runs with the page hidden (drakan_pause_screen.inl HiddenDraw). The title keeps
//     its art 0x2301 (and the music, which is the sound manager's), the slot screens keep the grid image 0x2331, whose
//     draw sends the per-frame message 10 (rescan, card check, the deferred load); the grid itself (43bfb0) only
//     makes its thumbnail requests. Both draws do memory card work, so they are not made no-preempt sections;
//   * the title's Options pages run the pause stage 2 executor with the title as the root screen (0x2305 opens 9);
//   * Save into an empty slot / Load from the title: Cross (message 2), as the native mouse does. Overwrite and Load in
//     game: the host card asked, so the glue arms what the native box's Yes arms (the screen timer whose message 9
//     runs 443ac0; the "load confirmed" bit whose next message 10 runs 43b4d0) without the box, and those run inside
//     the screen's own draw exactly as after the native Yes. Not posted as separate guest calls: an HLE-posted call
//     runs on the dispatcher thread at priority 6 and preempts the GUI thread at any checkpoint, and both routines
//     close screens / free the slot list that the GUI thread may be walking;
//   * input: InputModule host-screen mode while the host screen is on top; native boxes the host did not ask for (no
//     card, card changed, save failed) are shown natively with the host faded out.

namespace saveui = gameui::addr::saveui;
namespace titleui = gameui::addr::title;

constexpr uint32_t kGameCtxVar = 0x5565d4u;
// Time-outs are in milliseconds: the EE runs at very different rates in these menus (~1000 updates per second on the
// title, ~250 in a paused level), so frame counts do not make a usable time-out here.
constexpr int64_t kMenuActionMs = 4000;

int64_t menuNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void menuLog(const char* fmt, ...) {
    static unsigned lines = 0;
    if (lines++ > 600)
        return;
    va_list ap;
    va_start(ap, fmt);
    char buf[512];
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    ilog("[menu-screens] frame=%d t=%.3f %s\n", module().frameNo(), nowS(), buf);
}

bool titleBound(); // below

// Runs the stage 2 executor helpers (pausePress, pauseFinish, pauseExecuteStage2) on another PauseGlue.
struct ExecScope {
    PauseGlue* prev;
    explicit ExecScope(PauseGlue& p) : prev(g_exec) { g_exec = &p; }
    ~ExecScope() { g_exec = prev; }
    ExecScope(const ExecScope&) = delete;
    ExecScope& operator=(const ExecScope&) = delete;
};

void menuWrite16(RecompGuest& g, uint32_t a, uint16_t v) {
    g.write8(a, static_cast<uint8_t>(v & 0xff));
    g.write8(a + 1, static_cast<uint8_t>(v >> 8));
}

bool menuIsBox(int id) {
    return id == 1 || id == 2 || id == 3 || id == 0x1b;
}

gameui::DateOrder menuDateOrder(RecompGuest& g, bool& clock24) {
    const uint32_t ctx = g.read32(kGameCtxVar);
    clock24 = true;
    if (!pauseValidPtr(ctx))
        return gameui::DateOrder::DayMonthYear;
    clock24 = g.read32(ctx + 0x4f4) != 0;
    switch (g.read32(ctx + 0x4f8)) {
        case 0:
            return gameui::DateOrder::MonthDayYear;
        case 2:
            return gameui::DateOrder::YearMonthDay;
        default:
            return gameui::DateOrder::DayMonthYear;
    }
}

// ---- watchdog (as the pause's): the page is hidden, so the overlay must be drawing the screen ----
struct Watchdog {
    uint64_t seen = 0;
    int frames = 0;
    std::chrono::steady_clock::time_point since{};
    void reset(uint64_t drawn) {
        seen = drawn;
        frames = 0;
        since = std::chrono::steady_clock::now();
    }
    // True once the overlay has not drawn for 90 EE frames and 2.5 s while it should.
    bool expired(uint64_t drawn, bool exempt, char* why, size_t n) {
        const auto now = std::chrono::steady_clock::now();
        if (drawn != seen || exempt || drakan_settings_host::isOpen()) {
            seen = drawn;
            frames = 0;
            since = now;
            return false;
        }
        if (++frames < 90 || now - since < std::chrono::milliseconds(2500))
            return false;
        std::snprintf(why, n, "watchdog: host UI not drawn for %d frames (%.1f s)", frames,
                      std::chrono::duration<double>(now - since).count());
        return true;
    }
};

// =====================================================================================================================
// Save / Load (screens 4 / 5)
// =====================================================================================================================

struct SlotsGlue {
    uint32_t screen = 0;
    int id = 0;
    gameui::SaveLoadContext context = gameui::SaveLoadContext::Pause;
    bool fallBack = false;
    bool child = false;
    int awayFrames = 0;
    std::optional<gameui::SaveLoadAction> action;
    int phase = 0, taps = 0;
    int64_t startMs = 0, pressMs = 0; // the action's start and its last tap
    bool kick = false; // a thumbnail request was made: the card manager's vt+0xac follows (as the native grid does)
    int frames = 0;
    std::string message;
    int messageFrames = 0;
    int fadeSkips = 0;
    uint32_t listState = ~0u;
    struct Seen {
        int number = 0;
        std::string file;
        gameui::SaveTimestamp time;
        std::string hd;
        uint64_t key = 0;
        uint32_t image = 0;
    };
    std::vector<Seen> seen; // the pictures published, by slot number
    Watchdog watch;
};
SlotsGlue g_slots;
PS2Runtime::RecompiledFunction g_origGridDraw = nullptr;

bool slotsHosted() {
    return g_slots.screen && !g_slots.fallBack;
}
bool menuSlotsHosted() {
    return slotsHosted() && !g_slots.child;
}

void slotsBegin(RecompGuest& g, uint32_t screen, int id, bool fromTitle) {
    auto& sh = drakan_menu_screens::shared();
    g_slots = {};
    g_slots.screen = screen;
    g_slots.id = id;
    const bool levelChange = id == static_cast<int>(saveui::kScreenSave) && g.read32(screen + saveui::kSaveMode) == 1;
    g_slots.context = fromTitle     ? gameui::SaveLoadContext::Title
                      : levelChange ? gameui::SaveLoadContext::LevelChange
                                    : gameui::SaveLoadContext::Pause;
    sh.slotActions.clear();
    sh.slotsChild.store(false);
    sh.slotsLeaving.store(false);
    {
        std::lock_guard<std::mutex> l(sh.m);
        sh.mode = id == static_cast<int>(saveui::kScreenSave) ? gameui::SaveLoadMode::Save : gameui::SaveLoadMode::Load;
        sh.context = g_slots.context;
        sh.slots = {};
        sh.slots.card = gameui::CardState::Reading;
        sh.pictures.clear();
        ++sh.picturesSerial;
    }
    g_slots.watch.reset(sh.slotsDrawn.load());
    drakan_menus::setSlotsHosted(true);
    sh.slotsSession.fetch_add(1);
    sh.slotsActive.store(true);
    module().setHostScreen(true);
    menuLog("modern %s screen bound to %x (%s)", id == 5 ? "save" : "load", screen,
            g_slots.context == gameui::SaveLoadContext::Title         ? "title"
            : g_slots.context == gameui::SaveLoadContext::LevelChange ? "level change"
                                                                      : "pause");
}
void slotsEnd(const char* why) {
    if (!g_slots.screen)
        return;
    if (g_slots.action)
        menuLog("slot action %s -> confirmed (%s)", gameui::saveLoadActionName(g_slots.action->kind), why);
    menuLog("modern slot screen ended (%s)", why);
    g_slots = {};
    auto& sh = drakan_menu_screens::shared();
    sh.slotsActive.store(false);
    sh.slotsChild.store(false);
    sh.slotsLeaving.store(false);
    sh.slotActions.clear();
    drakan_menus::setSlotsHosted(false);
    // Back to the modern pause / title: they keep the host input (a held key must not reach the native page).
    if (!pauseHosted() && !titleBound())
        module().setHostScreen(false);
}
void slotsFallBack(const char* why) {
    if (!slotsHosted())
        return;
    menuLog("native slot page restored: %s", why);
    g_slots.fallBack = true;
    g_slots.action.reset();
    auto& sh = drakan_menu_screens::shared();
    sh.slotsActive.store(false);
    sh.slotsLeaving.store(false);
    sh.slotActions.clear();
    drakan_menus::setSlotsHosted(false);
    module().setHostScreen(false);
}

void slotsFinish(gameui::Outcome o, bool reactivate = false) {
    if (g_slots.action)
        menuLog("slot action %s(%d) -> %s", gameui::saveLoadActionName(g_slots.action->kind), g_slots.action->value,
                gameui::outcomeName(o));
    g_slots.action.reset();
    g_slots.phase = g_slots.taps = 0;
    auto& sh = drakan_menu_screens::shared();
    if (reactivate) {
        sh.slotsLeaving.store(false);
        sh.slotsReactivated.fetch_add(1); // the host screen takes input again
    }
}

// 43bfb0(screen): the 2 x 2 grid draw. While the host owns the screen it only makes the grid's thumbnail requests
// (the first block of its loop: card manager vt+0x1a4(entry, 0, 0, caption, 0x28, &image), then vt+0xac), one per
// call as a tail call on the real context (the request reads the save from the card and may block); nothing is drawn.
void hookGridDraw(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t screen = getRegU32(c, 4) & 0x1fffffffu;
    if (c->pc != saveui::kFnGridDraw || !slotsHosted() || screen != g_slots.screen) {
        g_origGridDraw(ram, c, rt);
        return;
    }
    RecompGuest g(ram, c, rt);
    const uint32_t ctx = g.read32(kGameCtxVar);
    const uint32_t vt = pauseValidPtr(ctx) ? g.read32(ctx + 0x520) : 0;
    const auto tail = [&](uint32_t fn, std::initializer_list<uint32_t> args) {
        int r = 4;
        for (uint32_t a : args) {
            const int reg = r++;
            SET_GPR_U32(c, reg, a);
        }
        c->pc = fn; // $ra is the grid draw's caller: the request returns there
    };
    if (pauseValidPtr(vt)) {
        if (g_slots.kick) {
            g_slots.kick = false;
            const uint32_t fn = g.read32(vt + 0xac);
            if (pauseValidPtr(fn)) {
                tail(fn, {ctx + static_cast<uint32_t>(static_cast<int16_t>(g.read16(vt + 0xa8)))});
                return;
            }
        }
        const int count = static_cast<int32_t>(g.read32(screen + saveui::kCount));
        const uint32_t entries = g.read32(screen + saveui::kEntries);
        const uint32_t fn = g.read32(vt + 0x1a4);
        for (int i = 0; i < std::min(count, saveui::kSlotsPerPage) && pauseValidPtr(entries) && pauseValidPtr(fn);
             ++i) {
            const uint32_t e = entries + static_cast<uint32_t>(i) * saveui::kEntrySize;
            if (g.read32(e + saveui::kEntryThumbRequested) != 0 || g.read32(e + saveui::kEntryThumb) != 0)
                continue;
            g.write32(e + saveui::kEntryThumbRequested, 1);
            g.write32(e + saveui::kEntryThumbRequested + 4, 1);
            g_slots.kick = true;
            tail(fn, {ctx + static_cast<uint32_t>(static_cast<int16_t>(g.read16(vt + 0x1a0))), e, 0u, 0u,
                      e + saveui::kEntryCaption, 0x28u, e + saveui::kEntryThumb});
            return;
        }
    }
    c->pc = getRegU32(c, 31); // the grid is the host's
}

// The native thumbnails and HD companions of the shown page, for the overlay (every 10 frames).
void slotsPictures(const uint8_t* ram, RecompGuest& g, const gameui::SaveSlotsState& st) {
    if (++g_slots.frames % 10 != 1 || st.card != gameui::CardState::Ready)
        return;
    const uint32_t entries = g.read32(g_slots.screen + saveui::kEntries);
    bool changed = false;
    std::vector<std::pair<int, drakan_menu_screens::SlotPicture>> fresh;
    for (size_t i = 0; i < st.slots.size() && i < 4; ++i) {
        const gameui::SaveSlot& s = st.slots[i];
        SlotsGlue::Seen* seen = nullptr;
        for (SlotsGlue::Seen& v : g_slots.seen)
            if (v.number == s.number)
                seen = &v;
        if (!seen) {
            g_slots.seen.push_back({s.number});
            seen = &g_slots.seen.back();
        }
        drakan_menu_screens::SlotPicture pic;
        if (s.state == gameui::SlotState::Used) {
            const uint32_t image =
                g.read32(entries + static_cast<uint32_t>(i) * saveui::kEntrySize + saveui::kEntryThumb);
            if (pauseValidPtr(image) && g.read32(image + 0x24) == 128 && g.read32(image + 0x28) == 128 &&
                g.read32(image + 0x38) == 16) {
                const uint32_t data = g.read32(image + 0x50);
                if (data >= 0x100000u && data <= PS2_RAM_SIZE - 128u * 128u * 2u) {
                    pic.pixels.resize(128 * 128);
                    uint64_t h = 1469598103934665603ull;
                    for (uint32_t n = 0; n < pic.pixels.size(); ++n) {
                        uint16_t v;
                        std::memcpy(&v, ram + data + n * 2, 2);
                        h = (h ^ v) * 1099511628211ull;
                        pic.pixels[n] =
                            ((v & 31u) << 3) | (((v >> 5) & 31u) << 11) | (((v >> 10) & 31u) << 19) | 0xff000000u;
                    }
                    pic.key = h | 1;
                }
            }
            if (seen->file != s.file || !(seen->time == s.time)) { // a new save in the slot: look for its companion
                seen->hd = drakan_save_previews::previewPath(s.number);
                if (!seen->hd.empty())
                    menuLog("slot %d: HD preview %s", s.number, seen->hd.c_str());
            }
            pic.hdPath = seen->hd;
        } else {
            seen->hd.clear();
        }
        if (seen->file != s.file || !(seen->time == s.time) || seen->key != pic.key) {
            seen->file = s.file;
            seen->time = s.time;
            seen->key = pic.key;
            changed = true;
        }
        fresh.emplace_back(s.number, std::move(pic));
    }
    if (!changed)
        return;
    auto& sh = drakan_menu_screens::shared();
    std::lock_guard<std::mutex> l(sh.m);
    for (auto& f : fresh) {
        bool found = false;
        for (auto& p : sh.pictures)
            if (p.first == f.first) {
                p.second = std::move(f.second);
                found = true;
            }
        if (!found)
            sh.pictures.push_back(std::move(f));
    }
    ++sh.picturesSerial;
}

// Title Continue: once a slot screen is up (modern or native), show the save's page, select it and load it (Cross).
struct ContinueGlue {
    int slot = 0; // slot number to load; 0 = none
    int phase = 0;
    int64_t phaseMs = 0; // when the current phase began
};
ContinueGlue g_continue;

bool continueStep(uint8_t* ram, R5900Context* c, PS2Runtime* rt, RecompGuest& g, uint32_t active, int id) {
    if (!g_continue.slot)
        return false;
    const int64_t now = menuNowMs(), wait = now - g_continue.phaseMs;
    if (id != static_cast<int>(saveui::kScreenLoad)) {
        if (g_continue.phase >= 2) { // the Load screen went (the load started, or Back)
            g_continue = {};
            return false;
        }
        if (wait > 3 * kMenuActionMs) {
            menuLog("continue: the load screen did not open");
            drakan_menu_screens::shared().titleReactivated.fetch_add(1); // the title menu takes input again
            g_continue = {};
        }
        return false;
    }
    const int n = g_continue.slot, page = (n - 1) / 4, row = (n - 1) % 4;
    const int count = static_cast<int32_t>(g.read32(active + saveui::kCount));
    const bool rescanning = (g.read16(active + saveui::kFlags) & 0xc) != 0;
    if (g_continue.phase == 0) { // the page
        if (drakan_menus::page() != page) {
            drakan_menus::showPage(ram, c, rt, active, page);
            menuLog("continue: page %d", page + 1);
        }
        g_continue.phase = 1;
        g_continue.phaseMs = now;
        return true;
    }
    if (g_continue.phase == 1) { // the list of that page
        if (count < 4 || rescanning || drakan_menus::page() != page) {
            if (wait > kMenuActionMs) {
                menuLog("continue: the slot list was not read");
                drakan_menu_screens::shared().slotsReactivated.fetch_add(1); // the list takes input again
                g_continue = {};
            }
            return true;
        }
        const uint32_t e = g.read32(active + saveui::kEntries) + static_cast<uint32_t>(row) * saveui::kEntrySize;
        if (g.read8(e + saveui::kEntryPath) == 0) {
            menuLog("continue: slot %d holds no save (left on the Load screen)", n);
            drakan_menu_screens::shared().slotsReactivated.fetch_add(1); // the list takes input again
            g_continue = {};
            return false;
        }
        g.write32(active + saveui::kSelection, static_cast<uint32_t>(row));
        g_continue.phase = 2;
        g_continue.phaseMs = now;
        return true;
    }
    if (g_continue.phase == 2 && wait >= 50) { // the selection has been seen by a frame
        module().injectPadTap(Pad::CROSS, 1);  // 43d028 -> 43b4d0 (no level runs on the title: no question)
        menuLog("continue: slot %d, Cross", n);
        g_continue.phase = 3;
        g_continue.phaseMs = now;
    } else if (g_continue.phase == 3 && wait > 1500) {
        g_continue = {};
    }
    return true;
}

void slotsExecute(uint8_t* ram, R5900Context* c, PS2Runtime* rt, RecompGuest& g, uint32_t active) {
    using K = gameui::SaveLoadActionKind;
    auto& sh = drakan_menu_screens::shared();
    if (!g_slots.action) {
        if (g_slots.child)
            return;
        auto a = sh.slotActions.pop();
        if (!a)
            return;
        g_slots.action = *a;
        g_slots.phase = g_slots.taps = 0;
        g_slots.startMs = g_slots.pressMs = menuNowMs();
        if (a->kind != K::SelectSlot)
            menuLog("slot action %s(%d)", gameui::saveLoadActionName(a->kind), a->value);
    }
    const gameui::SaveLoadAction a = *g_slots.action;
    const int64_t now = menuNowMs(), wait = now - g_slots.startMs, sincePress = now - g_slots.pressMs;
    const uint32_t s = g_slots.screen;
    const int count = static_cast<int32_t>(g.read32(s + saveui::kCount));
    const auto refuse = [&](const char* why, const char* message = nullptr) {
        menuLog("slot action %s(%d) refused: %s", gameui::saveLoadActionName(a.kind), a.value, why);
        if (message) {
            g_slots.message = message;
            g_slots.messageFrames = 300;
        }
        slotsFinish(gameui::Outcome::Refused, true);
    };
    // Confirm* and SelectSlot name a slot of the shown page.
    const int row = (a.value - 1) % 4, page = (a.value - 1) / 4;
    const auto select = [&]() {
        if (page != drakan_menus::page() || row < 0 || row >= count)
            return false;
        if (static_cast<int>(g.read32(s + saveui::kSelection)) != row)
            g.write32(s + saveui::kSelection, static_cast<uint32_t>(row));
        return true;
    };
    switch (a.kind) {
        case K::SelectSlot:
            if (count <= 0 && wait < 1000)
                return; // the list is being read
            if (!select())
                return refuse("not on the shown page");
            return slotsFinish(gameui::Outcome::Confirmed);
        case K::ChangePage:
            if (g_slots.phase == 0) {
                if (!drakan_menus::showPage(ram, c, rt, s, a.value))
                    return refuse("save pages are off");
                g_slots.phase = 1;
                return;
            }
            if (drakan_menus::page() == a.value && count >= 4 && (g.read16(s + saveui::kFlags) & 0xc) == 0)
                return slotsFinish(gameui::Outcome::Confirmed);
            if (wait > kMenuActionMs)
                return refuse("the page was not read");
            return;
        case K::ConfirmSave:
        case K::ConfirmOverwrite:
        case K::ConfirmLoad: {
            if (active != s) // the Saving screen, a box or the loading screen took over: done here
                return slotsFinish(gameui::Outcome::Confirmed);
            if (g_slots.phase == 0) {
                if (!select())
                    return refuse("not on the shown page");
                if (a.kind == K::ConfirmOverwrite) {
                    // What the overwrite box's Yes arms (443360 message 0x10): the screen timer, whose message 9 the
                    // screen's draw sends at once and which runs 443ac0 there.
                    g.write32(s + 0x44, 0);
                    g.write32(s + 0x48, 1);
                    menuLog("overwrite slot %d: the save timer is armed (as the box's Yes)", a.value);
                } else if (a.kind == K::ConfirmLoad && g_slots.context != gameui::SaveLoadContext::Title) {
                    // What the load question's Yes sets (43ac40 message 0x10): the next message 10 runs 43b4d0.
                    menuWrite16(g, s + saveui::kFlags, static_cast<uint16_t>(g.read16(s + saveui::kFlags) | 0x10));
                    menuLog("load slot %d in game: the load is armed (as the box's Yes)", a.value);
                } else {
                    module().injectPadTap(Pad::CROSS, 1); // message 2: 444460 (save) / 43d028 -> 43b4d0 (title load)
                    g_slots.pressMs = now;
                    ++g_slots.taps;
                    menuLog("%s slot %d: Cross", a.kind == K::ConfirmSave ? "save" : "load", a.value);
                }
                g_slots.phase = 1;
                return;
            }
            if (a.kind == K::ConfirmOverwrite && g.read32(s + 0x44) != 0 && g.read32(s + 0x48) != 0)
                g.write32(s + 0x48, 0); // message 9 was sent: once (the native screen is gone after it anyway)
            if (wait > kMenuActionMs) {
                g.write32(s + 0x48, 0);
                menuWrite16(g, s + saveui::kFlags, static_cast<uint16_t>(g.read16(s + saveui::kFlags) & ~0x10u));
                return refuse("the screen stayed open", a.kind == K::ConfirmLoad
                                                            ? "The game could not be loaded."
                                                            : "The game could not be saved in this slot.");
            }
            if (g_slots.taps > 0 && sincePress > 1500 && g_slots.taps < 2) {
                module().injectPadTap(Pad::CROSS, 1); // the first tap was not taken
                g_slots.pressMs = now;
                ++g_slots.taps;
            }
            return;
        }
        case K::Back:
            if (active != s)
                return slotsFinish(gameui::Outcome::Confirmed);
            if (g_slots.taps == 0 || (sincePress > 750 && g_slots.taps < 3)) {
                module().injectPadTap(Pad::START, 2); // key 1: back to the parent (GUI event 0x12)
                g_slots.pressMs = now;
                ++g_slots.taps;
            } else if (wait > 3000) {
                return refuse("the screen did not close");
            }
            return;
    }
}

void slotsTick(uint8_t* ram, R5900Context* c, PS2Runtime* rt, RecompGuest& g, uint32_t titleScreen) {
    auto& sh = drakan_menu_screens::shared();
    const uint32_t active = g.read32(kInvScreenVar);
    const int id = pauseScreenId(g, active);
    const bool autoContinue = continueStep(ram, c, rt, g, active, id);
    const bool slotScreen = id == static_cast<int>(saveui::kScreenLoad) || id == static_cast<int>(saveui::kScreenSave);
    if (g_slots.screen && slotScreen && active != g_slots.screen)
        slotsEnd("another slot screen");
    if (!g_slots.screen) {
        if (slotScreen && drakan_menu_screens::saveModern() && drakan_tab::shared().layerReady.load()) {
            const uint32_t parent = g.read32(active + 0x34); // the screen it returns to (title 8 / pause 0x17)
            const bool fromTitle =
                titleScreen != 0 ||
                (pauseValidPtr(parent) && pauseScreenId(g, parent) == static_cast<int>(titleui::kScreenTitle));
            slotsBegin(g, active, id, fromTitle);
        }
        if (!g_slots.screen)
            return;
    }
    if (active == g_slots.screen) {
        g_slots.awayFrames = 0;
    } else if (pauseValidPtr(active) && (menuIsBox(id) || id == static_cast<int>(saveui::kScreenSaving))) {
        g_slots.awayFrames = 0; // its box / the Saving screen
    } else if (id == static_cast<int>(saveui::kScreenLoading)) {
        slotsEnd("loading");
        return;
    } else if (++g_slots.awayFrames >= (pauseValidPtr(active) ? 3 : 10)) {
        char why[64];
        std::snprintf(why, sizeof why, "left (active screen %d)", id);
        slotsEnd(why);
        return;
    }
    if (g_slots.fallBack)
        return;
    if (!drakan_menu_screens::saveModern() || !drakan_tab::shared().layerReady.load()) {
        slotsFallBack("save_menu switched to native while the screen was open");
        return;
    }
    const bool child = active != g_slots.screen;
    if (child != g_slots.child) {
        g_slots.child = child;
        menuLog("slot screen: native child %s (screen %d)", child ? "up" : "closed", id);
        if (!child)
            sh.slotsReactivated.fetch_add(1);
    }
    sh.slotsChild.store(child);
    module().setHostScreen(!child);
    // The screen's fade-in (2bd5f0, from its draw while +0x54 == 2): the timer +0x6c counts the frame time down and
    // sets "shown" (+0x14) at 0; message 10 does nothing until then, so the list scan, the card check and the
    // deferred load wait for it (about 1.2 s after every open and page change). The page is hidden while the host
    // draws the screen, so the fade is not seen: end it at once (the value the timer reaches anyway).
    if (!child && g.read32(g_slots.screen + 0x54) == 2 && g.read32(g_slots.screen + 0x14) == 0) {
        const uint32_t bits = g.read32(g_slots.screen + 0x6c);
        float left;
        std::memcpy(&left, &bits, 4);
        if (left > 0.0f) {
            g.write32(g_slots.screen + 0x6c, 0);
            if (g_slots.fadeSkips++ < 8)
                menuLog("slot screen fade-in ended early (%.2f s left: hidden page)", left);
        }
    }
    { // the list's state, on change (latency checks: open -> shown -> scan -> list)
        const uint32_t v = (g.read32(g_slots.screen + 0x14) ? 1u : 0u) |
                           (uint32_t(g.read16(g_slots.screen + saveui::kFlags)) << 4) |
                           (uint32_t(g.read32(g_slots.screen + saveui::kCount) & 0xff) << 20);
        if (v != g_slots.listState) {
            g_slots.listState = v;
            menuLog("slot screen: shown %u, flags %x, entries %u", v & 1, (v >> 4) & 0xffff, v >> 20);
        }
    }
    // The list and the pictures.
    gameui::SaveReadOptions opt;
    opt.page = drakan_menus::page();
    opt.pageCount = drakan_menus::pageCount();
    opt.dateOrder = menuDateOrder(g, opt.clock24);
    gameui::SaveSlotsState st;
    gameui::readSaveSlots(RdramMemory(ram), g_slots.screen, opt, st);
    if ((g.read16(g_slots.screen + saveui::kFlags) & 0xc) != 0 && st.card == gameui::CardState::Ready)
        st.card = gameui::CardState::Reading; // a page change is rescanning
    // 43ac40 shows its label (and an OK box) when the card is missing. The label is also up from the layout until the
    // first message 10 has run, so it counts only once the list was built (scan flags clear) and came out empty.
    if (const uint32_t noCard = pauseWidget(g, g_slots.screen, static_cast<uint16_t>(saveui::kWidgetReading));
        noCard && g.read32(noCard + 0x3c) != 0 && (g.read16(g_slots.screen + saveui::kFlags) & 0xc) == 0 &&
        static_cast<int32_t>(g.read32(g_slots.screen + saveui::kCount)) <= 0)
        st.card = gameui::CardState::NoCard;
    st.inGame = g_slots.context != gameui::SaveLoadContext::Title;
    const bool confirming = g_slots.action && g_slots.action->kind != gameui::SaveLoadActionKind::SelectSlot &&
                            g_slots.action->kind != gameui::SaveLoadActionKind::ChangePage;
    st.busy = child || autoContinue || confirming || g_quickOperation;
    st.loadingSlot = autoContinue ? g_continue.slot : 0; // the host shows "Loading..." from the first frame
    st.quickOperation = g_quickOperation;
    if (g_slots.messageFrames > 0) {
        --g_slots.messageFrames;
        st.message = g_slots.message;
    }
    if (st.card == gameui::CardState::Ready || g_slots.frames == 0)
        slotsPictures(ram, g, st);
    {
        std::lock_guard<std::mutex> l(sh.m);
        sh.slots = st;
    }
    if (!autoContinue && !g_quickOperation)
        slotsExecute(ram, c, rt, g, active);
    char why[128];
    if (g_slots.watch.expired(sh.slotsDrawn.load(), child || sh.slotsLeaving.load() || autoContinue, why, sizeof why))
        slotsFallBack(why);
}

// =====================================================================================================================
// Title (screen 8)
// =====================================================================================================================

struct TitleGlue {
    uint32_t screen = 0;
    bool fallBack = false, child = false, covered = false;
    int awayFrames = 0, frames = 0;
    std::optional<gameui::TitleAction> action;
    PauseGlue exec;    // the title actions' press / wait counters (pausePress)
    PauseGlue options; // the Options pages: pause stage 2 executor with the title as its root screen
    int overlayPhase = 0;
    int64_t startMs = 0, pressMs = 0;
    Watchdog watch;
    gameui::TitleState saves; // the newest save (the card scan), refreshed on bind and on return
    bool scanned = false;
};
TitleGlue g_title;

bool titleHosted() {
    return g_title.screen && !g_title.fallBack;
}
bool titleBound() {
    return g_title.screen != 0;
}

// The newest save on the card (Continue): the slot folders' .rsg files (the private / configured card root).
void titleScanSaves(gameui::TitleState& st) {
    namespace fs = std::filesystem;
    st.continueSlot = 0;
    st.continueLocation.clear();
    st.continueTime = {};
    const auto& io = PS2Runtime::getIoPaths();
    const fs::path root = io.mcRoot.empty() ? io.elfDirectory / "mc0" : io.mcRoot;
    fs::file_time_type best{};
    for (int n = 1; n <= drakan_menus::pageCount() * 4; ++n) {
        std::error_code ec;
        for (fs::directory_iterator it(root / ("BESCES-50006Slot" + std::to_string(n)), ec), end; !ec && it != end;
             it.increment(ec)) {
            if (it->path().extension() != ".rsg")
                continue;
            std::error_code tec;
            const fs::file_time_type t = fs::last_write_time(it->path(), tec);
            if (tec || (st.continueSlot && t <= best))
                continue;
            best = t;
            st.continueSlot = n;
            st.continueLocation = drakan_tab::levelDisplayName(it->path().filename().string());
#if defined(_WIN32)
            const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(t);
#else
            const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                std::filesystem::file_time_type::clock::to_sys(t));
#endif
            const std::time_t tt = std::chrono::system_clock::to_time_t(sys);
            std::tm tm{};
#if defined(_WIN32)
            localtime_s(&tm, &tt);
#else
            localtime_r(&tt, &tm);
#endif
            st.continueTime = {tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec};
        }
    }
}

void titleBegin(RecompGuest& g, uint32_t screen) {
    auto& sh = drakan_menu_screens::shared();
    g_title = {};
    g_title.screen = screen;
    g_title.exec.screen = g_title.options.screen = screen;
    g_title.options.optionsRow = static_cast<uint16_t>(titleui::kWidgetOptions);
    sh.titleActions.clear();
    sh.titleOptionActions.clear();
    sh.titleChild.store(false);
    sh.titleCovered.store(false);
    g_title.watch.reset(sh.titleDrawn.load());
    sh.titleSession.fetch_add(1);
    sh.titleActive.store(true);
    module().setHostScreen(true);
    (void)g;
    menuLog("modern title bound to %x", screen);
}
void titleEnd(const char* why) {
    if (!g_title.screen)
        return;
    if (g_title.action)
        menuLog("title action %s -> confirmed (%s)", gameui::titleActionName(g_title.action->kind), why);
    menuLog("modern title ended (%s)", why);
    g_title = {};
    auto& sh = drakan_menu_screens::shared();
    sh.titleActive.store(false);
    sh.titleChild.store(false);
    sh.titleCovered.store(false);
    sh.titleActions.clear();
    sh.titleOptionActions.clear();
    if (!g_slots.screen)
        module().setHostScreen(false);
}
void titleFallBack(const char* why) {
    if (!titleHosted())
        return;
    menuLog("native title page restored: %s", why);
    g_title.fallBack = true;
    g_title.action.reset();
    g_title.options.action.reset();
    auto& sh = drakan_menu_screens::shared();
    sh.titleActive.store(false);
    sh.titleActions.clear();
    sh.titleOptionActions.clear();
    module().setHostScreen(false);
}

void titleFinish(gameui::Outcome o, bool reactivate = false) {
    if (g_title.action)
        menuLog("title action %s -> %s", gameui::titleActionName(g_title.action->kind), gameui::outcomeName(o));
    g_title.action.reset();
    {
        ExecScope scope(g_title.exec);
        pauseFinish(o);
    }
    g_title.overlayPhase = 0;
    if (reactivate)
        drakan_menu_screens::shared().titleReactivated.fetch_add(1);
}

// The F1 overlay for a row of the title or of its Options pages (no modern PC Settings pages): wait for it to open and
// close, then the title takes input again. True while it runs.
bool titleOverlayStep(int& phase, int64_t wait, bool controls) {
    if (phase == 0) {
        if (controls)
            drakan_settings_host::requestOpenControls();
        else
            drakan_settings_host::requestOpen();
        phase = 1;
        return true;
    }
    const bool open = drakan_settings_host::isOpen();
    if (phase == 1) {
        if (open)
            phase = 2;
        return open || wait <= kMenuActionMs;
    }
    return open;
}

void titleExecute(RecompGuest& g, uint32_t active, int id) {
    using K = gameui::TitleActionKind;
    auto& sh = drakan_menu_screens::shared();
    // The Options pages' actions first (only while those pages are open).
    PauseGlue& op = g_title.options;
    if (!op.action) {
        if (auto a = sh.titleOptionActions.pop()) {
            op.action = *a;
            op.wait = 0;
            op.phase = 0;
            g_title.startMs = menuNowMs();
            menuLog("options action %s(%d)", gameui::pauseActionName(a->kind), a->value);
        }
    }
    if (op.action) {
        const gameui::PauseAction a = *op.action;
        const int wait = ++op.wait;
        ExecScope scope(op);
        if (a.kind == gameui::PauseActionKind::OpenPcSettings || a.kind == gameui::PauseActionKind::OpenKeyBindings) {
            if (!titleOverlayStep(op.phase, menuNowMs() - g_title.startMs,
                                  a.kind == gameui::PauseActionKind::OpenKeyBindings)) {
                pauseFinish(gameui::Outcome::Confirmed);
                sh.titleReactivated.fetch_add(1);
            }
            return;
        }
        pauseExecuteStage2(g, a, wait);
        return;
    }
    if (!g_title.action) {
        if (g_title.child || g_title.covered)
            return;
        auto a = sh.titleActions.pop();
        if (!a)
            return;
        g_title.action = *a;
        g_title.exec.wait = 0;
        g_title.startMs = menuNowMs();
        g_title.pressMs = 0;
        menuLog("title action %s(%d)", gameui::titleActionName(a->kind), a->value);
    }
    const gameui::TitleAction a = *g_title.action;
    ++g_title.exec.wait; // pausePress counts its focus hold in frames
    const int64_t now = menuNowMs(), wait = now - g_title.startMs;
    const uint32_t t = g_title.screen;
    const auto press = [&](uint16_t row) {
        ExecScope scope(g_title.exec);
        if (active == t && (g_title.exec.taps == 0 || (now - g_title.pressMs > 1500 && g_title.exec.taps < 3)))
            if (pausePress(g, t, row))
                g_title.pressMs = now;
    };
    const auto refuse = [&](const char* why) {
        menuLog("title action %s refused: %s (active screen %d)", gameui::titleActionName(a.kind), why, id);
        titleFinish(gameui::Outcome::Refused, true);
    };
    switch (a.kind) {
        case K::LoadGame:
        case K::Continue:
            if (id == static_cast<int>(saveui::kScreenLoad)) {
                if (a.kind == K::Continue) {
                    g_continue = {};
                    g_continue.slot = a.value;
                    g_continue.phaseMs = now;
                }
                return titleFinish(gameui::Outcome::Confirmed);
            }
            if (!pauseRowEnabled(g, t, static_cast<uint16_t>(titleui::kWidgetLoad)))
                return refuse("Load Game is disabled");
            if (wait > kMenuActionMs)
                return refuse("the load screen did not open");
            return press(static_cast<uint16_t>(titleui::kWidgetLoad));
        case K::NewGame:
            if (id == static_cast<int>(saveui::kScreenLoading) || id == static_cast<int>(titleui::kScreenIntroMovie))
                return titleFinish(gameui::Outcome::Confirmed);
            if (wait > kMenuActionMs)
                return refuse("the game did not start");
            return press(static_cast<uint16_t>(titleui::kWidgetNew));
        case K::Credits:
            if (id == static_cast<int>(titleui::kScreenCredits))
                return titleFinish(gameui::Outcome::Confirmed); // a native child: reactivated when the title is back
            if (wait > kMenuActionMs)
                return refuse("the credits did not open");
            return press(static_cast<uint16_t>(titleui::kWidgetCredits));
        case K::OpenPcSettings:
            if (!titleOverlayStep(g_title.overlayPhase, wait, false))
                titleFinish(gameui::Outcome::Confirmed, true);
            return;
        case K::QuitToDesktop:
            menuLog("quit to desktop: closing the window");
            g.requestQuit();
            return titleFinish(gameui::Outcome::Confirmed);
    }
}

void titleTick(const uint8_t* ram, RecompGuest& g) {
    namespace gui = gameui::addr::gui;
    auto& sh = drakan_menu_screens::shared();
    const uint32_t active = g.read32(kInvScreenVar);
    const int id = pauseScreenId(g, active);
    const int titleId = static_cast<int>(titleui::kScreenTitle);
    if (g_title.screen && id == titleId && active != g_title.screen)
        titleEnd("another title screen");
    if (!g_title.screen) {
        if (id == titleId && drakan_menu_screens::titleModern() && drakan_tab::shared().layerReady.load())
            titleBegin(g, active);
        if (!g_title.screen)
            return;
    }
    if (id == static_cast<int>(saveui::kScreenLoading) || id == static_cast<int>(titleui::kScreenIntroMovie) ||
        id == static_cast<int>(gui::kScreenPause) || id == 0x15) {
        titleEnd("the game started");
        return;
    }
    g_title.awayFrames = pauseValidPtr(active) ? 0 : g_title.awayFrames + 1;
    if (g_title.awayFrames >= 10) {
        titleEnd("no screen open");
        return;
    }
    if (g_title.fallBack)
        return;
    if (!drakan_menu_screens::titleModern() || !drakan_tab::shared().layerReady.load()) {
        titleFallBack("title_menu switched to native while the title was open");
        return;
    }
    const bool owned = id == static_cast<int>(gui::kScreenOptions) || id == static_cast<int>(gui::kScreenAudio) ||
                       id == static_cast<int>(gui::kScreenController) ||
                       (id == static_cast<int>(gui::kScreenBox) && g_title.options.ownBox);
    const bool covered = g_slots.screen != 0 && !g_slots.fallBack;
    const bool child = active != g_title.screen && !owned && !covered;
    if (child != g_title.child || covered != g_title.covered) {
        const bool back = (g_title.child || g_title.covered) && !child && !covered;
        g_title.child = child;
        g_title.covered = covered;
        menuLog("title: %s (active %x, screen %d)",
                child     ? "native child up"
                : covered ? "modern slot screen up"
                          : "back",
                active, id);
        if (back) {
            sh.titleReactivated.fetch_add(1);
            g_title.scanned = false; // a save may have been loaded / the card changed
        }
    }
    sh.titleChild.store(child);
    sh.titleCovered.store(covered);
    if (!covered)
        module().setHostScreen(!child);
    {
        gameui::TitleState st;
        if (!g_title.scanned) {
            titleScanSaves(g_title.saves);
            g_title.scanned = true;
            menuLog("title: newest save slot %d (%s)", g_title.saves.continueSlot,
                    g_title.saves.continueLocation.c_str());
        }
        st.canLoad = pauseRowEnabled(g, g_title.screen, static_cast<uint16_t>(titleui::kWidgetLoad));
        if (st.canLoad) {
            st.continueSlot = g_title.saves.continueSlot;
            st.continueLocation = g_title.saves.continueLocation;
            st.continueTime = g_title.saves.continueTime;
        }
        st.canQuit = drakan_menus::titleQuit();
        st.dateOrder = menuDateOrder(g, st.clock24);
        gameui::readPauseOptions(RdramMemory(ram), st.options);
        if (++g_title.frames <= 1)
            menuLog("title rows: load %d continue %d quit %d", st.canLoad ? 1 : 0, st.continueSlot, st.canQuit ? 1 : 0);
        std::lock_guard<std::mutex> l(sh.m);
        sh.title = st;
    }
    titleExecute(g, active, id);
    char why[128];
    if (g_title.watch.expired(sh.titleDrawn.load(), child || covered, why, sizeof why))
        titleFallBack(why);
}

// The modern screens' part of the screen draw 2bd1b0 (drakan_pause_screen.inl hookScreenDraw).
bool menuScreenDraw(uint8_t* ram, R5900Context* c, PS2Runtime* rt, uint32_t screen) {
    if (!slotsHosted() && !titleHosted())
        return false;
    RecompGuest g(ram, c, rt);
    if (slotsHosted() && screen == g_slots.screen) {
        HiddenDraw hidden(g, c, screen, static_cast<uint16_t>(saveui::kWidgetGrid), true);
        g_origScreenDraw(ram, c, rt);
        return true;
    }
    if (!titleHosted())
        return false;
    if (screen == g_title.screen) {
        HiddenDraw hidden(g, c, screen, static_cast<uint16_t>(titleui::kWidgetArt), true);
        g_origScreenDraw(ram, c, rt);
        return true;
    }
    namespace gui = gameui::addr::gui;
    const int id = pauseScreenId(g, screen);
    if (id == static_cast<int>(gui::kScreenOptions) || id == static_cast<int>(gui::kScreenAudio) ||
        id == static_cast<int>(gui::kScreenController) ||
        (id == static_cast<int>(gui::kScreenBox) && g_title.options.ownBox)) {
        HiddenDraw hidden(g, c, screen); // the title's Options pages: as the pause's (no card work in these draws)
        g_origScreenDraw(ram, c, rt);
        return true;
    }
    return false;
}

// EE thread, every PadMgr update (after pauseTick).
void menuScreensTick(uint8_t* ram, R5900Context* c, PS2Runtime* rt, RecompGuest& g) {
    if (!g_origScreenDraw)
        return; // the pause hooks (2bd1b0) are missing: no hiding, nothing modern
    slotsTick(ram, c, rt, g, g_title.screen);
    titleTick(ram, g);
}

void installMenuScreenHooks(PS2Runtime& rt) {
    g_origGridDraw = rt.lookupFunction(saveui::kFnGridDraw);
    if (!g_origGridDraw || !g_origScreenDraw) {
        ilog("[menu-screens] 43bfb0 / 2bd1b0 missing: modern title and Save / Load screens off\n");
        g_origGridDraw = nullptr;
        return;
    }
    rt.replaceFunction(saveui::kFnGridDraw, &hookGridDraw);
}
