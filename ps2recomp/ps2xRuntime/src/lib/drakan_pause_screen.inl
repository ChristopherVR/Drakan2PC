// Included inside drakan_input_glue.cpp's anonymous namespace after drakan_tab_screen.inl.
// Modern Esc (pause) menu, phase 1 of docs/journal/2026-10-25/modern-pause-menu.md (reference:
// docs/reference/mouse-menus.md section 14). With [general] pause_menu = modern the pause screen 0x17 still opens
// natively (handler 431f00: volume ducking, game logic paused, enable states) and stays the active screen; the host
// draws gameui PauseScreen over the dimmed, blurred world:
//   * bound at the handler's message 3, unbound at message 6 or when the manager's pause slot (*0x4fb66c) no longer
//     holds it;
//   * its page (dim panel, brass frame, rows) is not drawn: the screen draw 2bd1b0 runs with +0x08 = 0 and every child
//     widget's +0x3c = 0, restored right after (no preemption in between);
//   * rows are driven natively: Up / Down taps until the native focus (+0x84) is the row, then a Cross tap; Resume is a
//     START tap; Save, Load, Options and the Quit box stay native, the host fades out while one is on top;
//   * input: InputModule host-screen mode while the pause itself is the active screen.
// Stage 2 / 3 (runtime/gameui/README.md "Pause menu actions", mouse-menus.md section 14): the host Options / Audio /
// Controller pages run over the native screens 9 / 13 / 10, which the glue opens and keeps open but hidden (the same
// 2bd1b0 wrapper), as it hides the Yes / No box it answers for Default Settings and Quit to Title. Those screens are
// "owned": host input stays on, no fade. Each action is executed on the EE side, one at a time, and confirmed by
// reading the native fields back.

struct PauseGlue {
    uint32_t screen = 0;
    bool child = false;    // a native child screen / box is the active screen (not one the host owns)
    bool fallBack = false; // the native page is back for this opening (watchdog / switched off)
    gameui::ReaderState reader;
    std::optional<gameui::PauseAction> action;
    int wait = 0;
    bool pressed = false; // the row's Cross was tapped
    int taps = 0;
    int frames = 0;
    // stage 2 executor state (per action)
    int phase = 0;
    int pressAt = -100;           // `wait` of the last Cross tap
    int focusAt = -100;           // `wait` of the last focus change
    int closeWait = 0;            // Resume: frames spent closing owned screens first
    int32_t mark = 0;             // the field's value at the last press (scheme cycling)
    bool ownBox = false;          // the Yes / No box on top was opened by the host (hidden, answered by the glue)
    uint16_t optionsRow = 0x2313; // EnterPage(Options): the row of `screen` that opens screen 9 (title: 0x2305)
    int noScreenFrames = 0;       // frames without an active screen (a save closed every screen)
    uint64_t drawnSeen = 0;
    int undrawnFrames = 0;
    std::chrono::steady_clock::time_point undrawnSince{};
};
PauseGlue g_pause;
// The stage 2 executor (pauseExecuteStage2 and its helpers) works on this state: the pause's, or the modern title
// menu's Options pages (drakan_menu_screens.inl), whose root screen is the title instead of the pause.
PauseGlue* g_exec = &g_pause;
// drakan_menu_screens.inl: the modern title / Save / Load screens' part of the screen draw, and whether the modern
// Save / Load screen owns the input now (over the pause, whose native child it is).
bool menuScreenDraw(uint8_t* ram, R5900Context* c, PS2Runtime* rt, uint32_t screen);
bool menuSlotsHosted();
PS2Runtime::RecompiledFunction g_origPauseHandler = nullptr, g_origScreenDraw = nullptr;
// Quit to Title answered the native box Yes: the box (1) -> title (8) change must not be turned into an exit
// (quit_exits, hookPadMgrUpdate). One shot, armed for 30 s.
std::atomic<bool> g_pauseQuitToTitle{false};
std::chrono::steady_clock::time_point g_pauseQuitToTitleAt{};

constexpr uint32_t kPauseSlot = 0x4fb66cu; // screen manager 0x4fb610 + 4 * 0x17
constexpr int kPauseActionFrames = 240;    // time-out of one step of an action (EE frames)

void pauseLog(const char* fmt, ...) {
    static unsigned lines = 0;
    if (lines++ > 400)
        return;
    va_list ap;
    va_start(ap, fmt);
    char buf[512];
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    ilog("[pause-screen] frame=%d %s\n", module().frameNo(), buf);
}

bool pauseHosted() {
    return g_pause.screen && !g_pause.fallBack;
}

uint32_t pauseWidget(RecompGuest& g, uint32_t screen, uint16_t id) {
    uint32_t w = g.read32(screen + 0x7c);
    for (int guard = 0; guard < 64 && w >= 0x100000u && w < PS2_RAM_SIZE; ++guard) {
        if (g.read16(w + 0x14) == id)
            return w;
        w = g.read32(w + 4);
    }
    return 0;
}
// A row takes part in input (MenuMouse's rule: +0x3c and +0x40 both non-zero; 432518 disables through vt+0x6c).
bool pauseRowEnabled(RecompGuest& g, uint32_t screen, uint16_t id) {
    const uint32_t w = pauseWidget(g, screen, id);
    return w && g.read32(w + 0x3c) != 0 && g.read32(w + 0x40) != 0;
}

bool pauseValidPtr(uint32_t a) {
    return a >= 0x100000u && a < PS2_RAM_SIZE - 0x100u;
}
uint32_t pauseScreenObject(RecompGuest& g, uint32_t id) {
    return g.read32(gameui::addr::gui::kScreenTable + 4u * id);
}
int pauseScreenId(RecompGuest& g, uint32_t screen) {
    return pauseValidPtr(screen) ? static_cast<int>(g.read32(screen + 0x18)) : -1;
}
uint32_t pauseActive(RecompGuest& g) {
    return g.read32(kInvScreenVar);
}
// Depth of an owned native screen over the pause: Options 1, Audio / Controller 2, the box the host answers 3 (0 =
// not owned).
int pauseOwnedDepth(RecompGuest& g, uint32_t screen) {
    namespace gui = gameui::addr::gui;
    const int id = pauseScreenId(g, screen);
    if (id == static_cast<int>(gui::kScreenOptions))
        return 1;
    if (id == static_cast<int>(gui::kScreenAudio) || id == static_cast<int>(gui::kScreenController))
        return 2;
    if (id == static_cast<int>(gui::kScreenBox) && g_exec->ownBox)
        return 3;
    return 0;
}
int pausePageScreen(int32_t page) {
    switch (static_cast<gameui::PausePage>(page)) {
        case gameui::PausePage::Options:
            return static_cast<int>(gameui::addr::gui::kScreenOptions);
        case gameui::PausePage::Audio:
            return static_cast<int>(gameui::addr::gui::kScreenAudio);
        case gameui::PausePage::Controller:
            return static_cast<int>(gameui::addr::gui::kScreenController);
        default:
            return -1;
    }
}

void pauseBegin(uint8_t* ram, uint32_t screen) {
    g_pause = {};
    g_pause.screen = screen;
    auto& sh = drakan_pause_screen::shared();
    sh.actions.clear();
    sh.childUp.store(false);
    sh.resuming.store(false);
    {
        std::lock_guard<std::mutex> l(sh.m);
        sh.state = {};
        sh.state.location = readLevelName(ram);
    }
    g_pause.drawnSeen = sh.hostDrawn.load();
    g_pause.undrawnSince = std::chrono::steady_clock::now();
    sh.session.fetch_add(1);
    sh.active.store(true);
    module().setHostScreen(true);
    pauseLog("modern pause bound to %x", screen);
}
void pauseEnd(const char* why) {
    if (!g_pause.screen)
        return;
    pauseLog("modern pause ended (%s)", why);
    g_pause = {};
    auto& sh = drakan_pause_screen::shared();
    sh.active.store(false);
    sh.childUp.store(false);
    sh.resuming.store(false);
    sh.actions.clear();
    module().setHostScreen(false);
}
void pauseFallBack(const char* why) {
    if (!pauseHosted())
        return;
    pauseLog("native pause page restored: %s", why);
    g_pause.fallBack = true;
    auto& sh = drakan_pause_screen::shared();
    sh.active.store(false);
    sh.resuming.store(false);
    sh.actions.clear();
    module().setHostScreen(false);
}

// 431f00(screen, widget, msg): the pause handler. Message 3 opens (binds the host screen), 6 closes.
void hookPauseHandler(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x431f00u) {
        const uint32_t screen = getRegU32(c, 4), msg = getRegU32(c, 6);
        if (msg == 3) {
            if (g_pause.screen)
                pauseEnd("reopened");
            if (drakan_pause_screen::enabled() && drakan_tab::shared().layerReady.load())
                pauseBegin(ram, screen);
        } else if (msg == 6 && screen == g_pause.screen) {
            pauseEnd("closed");
        }
    }
    g_origPauseHandler(ram, c, rt);
}

// A screen draw with the page hidden (+0x08 = 0, every child +0x3c = 0, cursor +0x40 = 0), restored right after.
// The edits live only for this call. A draw that blocks (first texture / sound resource, a level teardown run from
// a timer message) unwinds the host stack with EeDispatcherTransfer: the guard then still leaves the no-preempt
// section (a leaked count disables preemption on this thread for good) and restores the fields at once, so no
// write outlives the call; the rest of that one draw, resumed later, shows the page (one frame at most).
// `keep`: a widget id that stays drawable (the slot screens' grid image, whose draw sends the per-frame message 10;
// the title art). `preempt`: for screens whose draw does memory card work (the slot screens' scans, card checks,
// thumbnail loads and the deferred load / overwrite; the title's card check) the draw is not made a no-preempt
// section: that work runs as it does natively.
// A draw that does not finish in this call (a time-slice preemption or a block returns out of the host stack, normally
// or through EeDispatcherTransfer, and the rest of the draw resumes later from its resume point) keeps the page
// hidden: the fields are restored at the next frame boundary (hiddenDrawRestorePending, from pauseTick) or when the
// screen is drawn again, so the resumed remainder does not show the native page (it did before: one frame of the
// native title rows behind the modern title now and then, 2026-10-25).
struct HiddenFields {
    uint32_t screen = 0, panel = 0, cursor = 0;
    static constexpr int kMax = 32;
    uint32_t widgets[kMax] = {}, shown[kMax] = {};
    int n = 0;
    void restore(RecompGuest& g) const {
        g.write32(screen + 0x08, panel);
        g.write32(screen + 0x40, cursor);
        for (int i = 0; i < n; ++i)
            g.write32(widgets[i] + 0x3c, shown[i]);
    }
};
std::vector<HiddenFields> g_hiddenPending;
std::vector<HiddenFields*> g_hiddenActive; // the hidden draws on the host stack now (innermost last)

// 2bea48 / 2be9a8(screen, id, on): the native show / enable setters of a widget. The screen's own code calls them
// during its draw (the slot screens' message 10 shows or hides the "no memory card" label 0x2332 every frame): the
// value it sets is the one to restore, and the widget stays hidden until then.
PS2Runtime::RecompiledFunction g_origWidgetShow = nullptr, g_origWidgetEnable = nullptr;
void widgetSetterFollow(RecompGuest& g, uint32_t screen, uint16_t id) {
    const auto follow = [&](HiddenFields& f) {
        if (f.screen != screen)
            return;
        for (int i = 0; i < f.n; ++i)
            if (g.read16(f.widgets[i] + 0x14) == id) {
                f.shown[i] = g.read32(f.widgets[i] + 0x3c);
                g.write32(f.widgets[i] + 0x3c, 0);
            }
    };
    for (HiddenFields* f : g_hiddenActive)
        follow(*f);
    for (HiddenFields& f : g_hiddenPending)
        follow(f);
}
template <int Which>
void hookWidgetSetter(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t screen = getRegU32(c, 4) & 0x1fffffffu, ra = getRegU32(c, 31);
    const uint16_t id = static_cast<uint16_t>(getRegU32(c, 5));
    (Which == 0 ? g_origWidgetShow : g_origWidgetEnable)(ram, c, rt);
    if (c->pc == ra && (!g_hiddenActive.empty() || !g_hiddenPending.empty())) {
        RecompGuest g(ram, c, rt);
        widgetSetterFollow(g, screen, id);
    }
}

// Restores the fields of unfinished hidden draws (of `screen`, or all with 0).
void hiddenDrawRestorePending(RecompGuest& g, uint32_t screen = 0) {
    for (auto it = g_hiddenPending.begin(); it != g_hiddenPending.end();) {
        if (screen && it->screen != screen) {
            ++it;
            continue;
        }
        it->restore(g);
        it = g_hiddenPending.erase(it);
    }
}

struct HiddenDraw {
    RecompGuest& g;
    R5900Context* c;
    uint32_t ra = 0;
    bool noPreempt = true;
    HiddenFields f;
    HiddenDraw(RecompGuest& guest, R5900Context* ctx, uint32_t s, uint16_t keep = 0, bool preempt = false)
        : g(guest), c(ctx), ra(getRegU32(ctx, 31)), noPreempt(!preempt) {
        hiddenDrawRestorePending(g, s); // its previous draw has finished: the real values first
        f.screen = s;
        for (uint32_t w = g.read32(s + 0x7c); f.n < HiddenFields::kMax && w >= 0x100000u && w < PS2_RAM_SIZE;
             w = g.read32(w + 4)) {
            if (keep && g.read16(w + 0x14) == keep)
                continue;
            f.widgets[f.n] = w;
            f.shown[f.n] = g.read32(w + 0x3c);
            g.write32(w + 0x3c, 0);
            ++f.n;
        }
        f.panel = g.read32(s + 0x08);
        f.cursor = g.read32(s + 0x40);
        g.write32(s + 0x08, 0);
        g.write32(s + 0x40, 0); // the native menu cursor sprite
        g_hiddenActive.push_back(&f);
        if (noPreempt)
            ps2xNoPreemptEnter();
    }
    HiddenDraw(const HiddenDraw&) = delete;
    HiddenDraw& operator=(const HiddenDraw&) = delete;
    ~HiddenDraw() {
        if (noPreempt)
            ps2xNoPreemptLeave();
        g_hiddenActive.erase(std::remove(g_hiddenActive.begin(), g_hiddenActive.end(), &f), g_hiddenActive.end());
        const bool unwound = std::uncaught_exceptions() > 0;
        if (!unwound && c->pc == ra) {
            f.restore(g);
            return;
        }
        // Not finished: the remainder resumes later and must stay hidden.
        g_hiddenPending.push_back(f);
        static std::atomic<unsigned> logged{0};
        if (logged.fetch_add(1) < 8)
            ilog("[pause-screen] hidden draw of %x interrupted (%s): kept hidden until the next frame\n", f.screen,
                 unwound ? "blocked inside" : "preempted");
    }
};

// 2bd1b0(screen): a screen's draw (open transition, dim panel + brass frame when +0x08 / +0x0c > 0, the child widgets
// whose +0x3c is set, the cursor). The bound pause draws nothing of its own while the host shows it; the transition and
// timers keep running. Also reached as the parent of a native child (2cc8e0 recursion): it stays hidden then too.
// With stage 2 the owned native screens (Options 9, Audio 13, Controller 10 and the Yes / No box the host answers) are
// drawn the same way: open, active, invisible.
void hookScreenDraw(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t screen = getRegU32(c, 4) & 0x1fffffffu;
    if (c->pc == 0x2bd1b0u && menuScreenDraw(ram, c, rt, screen))
        return; // a modern title / Save / Load screen's page (or the title's Options screens)
    if (c->pc != 0x2bd1b0u || !pauseHosted()) {
        g_origScreenDraw(ram, c, rt);
        return;
    }
    RecompGuest g(ram, c, rt);
    if (screen != g_pause.screen && pauseOwnedDepth(g, screen) == 0) {
        g_origScreenDraw(ram, c, rt);
        return;
    }
    // The status strip's objective texts (see tabRequestStrings); returns only when nothing was queued.
    if (screen == g_pause.screen && g_pause.frames > 2 && !g_pause.child)
        if (const auto s = drakan_pause_screen::shared().model.acquire())
            tabRequestStrings(ram, c, rt, *s);
    static const bool trace = std::getenv("DRAKAN_DEBUG_PAUSE_DRAWLOG") != nullptr;
    static unsigned traced = 0;
    const bool tr = trace && traced < 400 && (screen != g_pause.screen || g.read32(screen + 0x48) != 0);
    if (tr) {
        ++traced;
        pauseLog("draw enter %x id %d timer %d", screen, pauseScreenId(g, screen), int(g.read32(screen + 0x48)));
    }
    {
        HiddenDraw hidden(g, c, screen);
        g_origScreenDraw(ram, c, rt);
    }
    if (tr)
        pauseLog("draw leave %x pc %x", screen, c->pc);
}

void pauseFinish(gameui::Outcome o) {
    PauseGlue& ex = *g_exec;
    if (ex.action)
        pauseLog("action %s -> %s", gameui::pauseActionName(ex.action->kind), gameui::outcomeName(o));
    ex.action.reset();
    ex.wait = 0;
    ex.pressed = false;
    ex.taps = 0;
    ex.phase = 0;
    ex.pressAt = ex.focusAt = -100;
    ex.closeWait = 0;
    ex.ownBox = false;
}

// Focus widget `id` of `screen` (2be528, a plain setter: no yields, no allocation) and tap Cross once the focus has
// held for two frames and the screen's open transition is done (+0x14). True on the frame of the tap.
bool pausePress(RecompGuest& g, uint32_t screen, uint16_t id) {
    PauseGlue& ex = *g_exec;
    const uint32_t w = pauseWidget(g, screen, id);
    if (!w)
        return false;
    if (g.read32(screen + 0x84) != w) {
        g.call(GuestCall(0x2be528u).arg(screen).arg(w));
        ex.focusAt = ex.wait;
        return false;
    }
    if (ex.wait - ex.focusAt < 2 || ex.wait - ex.pressAt < 3)
        return false;
    if (g.read32(screen + 0x14) == 0 && ex.wait < 60)
        return false; // still opening
    module().injectPadTap(Pad::CROSS, 1);
    ex.pressAt = ex.wait;
    ++ex.taps;
    return true;
}

// Stage 2 / 3 actions (runtime/gameui/README.md, "Pause menu actions"). One step per frame.
void pauseExecuteStage2(RecompGuest& g, const gameui::PauseAction& a, int wait) {
    PauseGlue& ex = *g_exec;
    using K = gameui::PauseActionKind;
    using gameui::PausePage;
    namespace gui = gameui::addr::gui;
    namespace opt = gameui::addr::options;
    auto& sh = drakan_pause_screen::shared();
    const uint32_t active = pauseActive(g);
    const int activeId = pauseScreenId(g, active);
    const auto refuse = [&](const char* why) {
        pauseLog("%s(%d) refused: %s (active screen %d, step %d)", gameui::pauseActionName(a.kind), a.value, why,
                 activeId, ex.phase);
        pauseFinish(gameui::Outcome::Refused);
    };
    switch (a.kind) {
        case K::EnterPage: {
            const auto page = static_cast<PausePage>(a.value);
            const int want = pausePageScreen(a.value);
            if (want < 0)
                return refuse("no native screen for the page");
            if (activeId == want) {
                pauseLog("page %s: screen %d open (hidden) after %d frames", gameui::pausePageName(page), want, wait);
                return pauseFinish(gameui::Outcome::Confirmed);
            }
            if (wait > kPauseActionFrames)
                return refuse("the screen did not open");
            const bool options = page == PausePage::Options;
            const uint32_t from = options ? ex.screen : pauseScreenObject(g, gui::kScreenOptions);
            if (active != from)
                return; // the previous screen is still closing
            const uint16_t row = options                    ? ex.optionsRow
                                 : page == PausePage::Audio ? gui::kOptionsAudio
                                                            : gui::kOptionsController;
            if (options && !pauseRowEnabled(g, from, row))
                return refuse("Options is disabled");
            if (ex.taps == 0 || (wait - ex.pressAt > 90 && ex.taps < 3))
                pausePress(g, from, row);
            return;
        }
        case K::LeavePage: {
            // Close owned screens from the top down until the page's level is gone (Audio / Controller -> Options,
            // Options -> the pause); nothing to do when it is not open (its EnterPage was refused).
            const int depth = static_cast<PausePage>(a.value) == PausePage::Options ? 1 : 2;
            const int cur = active == ex.screen ? 0 : pauseOwnedDepth(g, active);
            if (cur < depth || cur == 3) {
                if (cur == 3)
                    pauseLog("leave: a box is on top, not closed");
                else if (ex.taps)
                    pauseLog("page %s left after %d frames", gameui::pausePageName(static_cast<PausePage>(a.value)),
                             wait);
                return pauseFinish(gameui::Outcome::Confirmed);
            }
            if (wait > kPauseActionFrames)
                return refuse("the screen did not close");
            if (wait - ex.pressAt >= 45 && ex.taps < 6) {
                module().injectPadTap(Pad::START, 2); // key 1 = back on 9 / 10 / 13
                ex.pressAt = wait;
                ++ex.taps;
            }
            return;
        }
        case K::SetMasterVolume:
        case K::SetMusicVolume: {
            const bool master = a.kind == K::SetMasterVolume;
            const uint32_t field = master ? opt::kMasterVolume : opt::kMusicVolume;
            const int32_t v = std::clamp<int32_t>(a.value, 0, 100);
            if (static_cast<int32_t>(g.read32(field)) == v) {
                if (ex.phase)
                    pauseLog("%s %d stored after %d frames", gameui::pauseActionName(a.kind), v, wait);
                return pauseFinish(gameui::Outcome::Confirmed);
            }
            if (ex.phase == 0) {
                if (activeId != static_cast<int>(gui::kScreenAudio))
                    return refuse("Audio is not the active screen");
                if (g.postBusy())
                    return; // one posted guest call at a time (the apply may wait for a sound resource)
                const uint32_t w =
                    pauseWidget(g, active, static_cast<uint16_t>(master ? gui::kAudioMaster : gui::kAudioMusic));
                if (!w)
                    return refuse("no slider widget");
                // 441440 stores int(value * 100): a little above v / 100 so the float truncates to v (1.0 clamps).
                g.call(GuestCall(gui::kFnSliderSet).argf((static_cast<float>(v) + 0.25f) / 100.0f).arg(w));
                g.post(GuestCall(gui::kFnPostMessage).arg(active).arg(w).arg(1));
                ex.phase = 1;
                return;
            }
            if (wait > kPauseActionFrames)
                return refuse("the volume field did not change");
            return;
        }
        case K::SetSubtitles:
        case K::SetStereo:
        case K::SetVibration:
        case K::SetReversePitch: {
            const bool audio = a.kind == K::SetSubtitles || a.kind == K::SetStereo;
            const uint32_t field = a.kind == K::SetSubtitles   ? opt::kSubtitles
                                   : a.kind == K::SetStereo    ? opt::kStereo
                                   : a.kind == K::SetVibration ? opt::kVibration
                                                               : opt::kReversePitch;
            const uint32_t id = a.kind == K::SetSubtitles   ? gui::kAudioSubtitles
                                : a.kind == K::SetStereo    ? gui::kAudioStereo
                                : a.kind == K::SetVibration ? gui::kControllerVibration
                                                            : gui::kControllerReversePitch;
            const int32_t v = a.value ? 1 : 0;
            if (static_cast<int32_t>(g.read32(field)) == v) {
                if (ex.taps > 0)
                    pauseLog("%s %d stored %d frames after the press", gameui::pauseActionName(a.kind), v,
                             wait - ex.pressAt);
                return pauseFinish(gameui::Outcome::Confirmed);
            }
            if (activeId != static_cast<int>(audio ? gui::kScreenAudio : gui::kScreenController))
                return refuse("its screen is not the active screen");
            if (wait > kPauseActionFrames) {
                const uint32_t w = pauseWidget(g, active, static_cast<uint16_t>(id));
                pauseLog("%s: widget flags %x, field %d", gameui::pauseActionName(a.kind), w ? g.read32(w + 0x10) : 0u,
                         static_cast<int32_t>(g.read32(field)));
                return refuse("the field did not change");
            }
            if (ex.taps == 0)
                pausePress(g, active, static_cast<uint16_t>(id)); // the press flips the box; message 1 stores it
            return;
        }
        case K::SetControllerScheme: {
            const int32_t v = std::clamp<int32_t>(a.value, 0, opt::kSchemeCount - 1);
            const int32_t cur = static_cast<int32_t>(g.read32(opt::kControllerScheme));
            if (cur == v) {
                if (ex.taps)
                    pauseLog("scheme %d after %d presses", v, ex.taps);
                return pauseFinish(gameui::Outcome::Confirmed);
            }
            if (activeId != static_cast<int>(gui::kScreenController))
                return refuse("Controller is not the active screen");
            if (wait > 3 * kPauseActionFrames)
                return refuse("the scheme did not reach the value");
            // The button cycles 0 -> 1 -> 2 -> 0: press, wait for the field to move, press again.
            if (!ex.pressed || cur != ex.mark) {
                ex.pressed = false;
                if (pausePress(g, active, static_cast<uint16_t>(gui::kControllerScheme))) {
                    ex.pressed = true;
                    ex.mark = cur;
                }
            }
            return;
        }
        case K::RestoreDefaults: {
            // 4424e0's message 1 on Default Settings (0x2344) restores the defaults at once when the screen's "asked"
            // flag (+0x9c) is 0, else it opens the Yes / No box, whose Yes clears the flag and arms a timer (+0x48)
            // that re-sends message 1 from the screen's draw (message 9). The host already asked on its card, so the
            // glue clears the flag and presses once: the defaults (442328, 441198, 445380) apply on the GUI's own
            // dispatch, not inside the hidden draw (which runs without preemption), and no box opens.
            const uint32_t options = pauseScreenObject(g, gui::kScreenOptions);
            const bool restored = g.read32(opt::kMasterVolume) == 100 && g.read32(opt::kMusicVolume) == 80 &&
                                  g.read32(opt::kSubtitles) == 1 && g.read32(opt::kStereo) == 1 &&
                                  g.read32(opt::kControllerScheme) == 0 && g.read32(opt::kVibration) == 1 &&
                                  g.read32(opt::kReversePitch) == 0;
            if (ex.phase == 1 && restored) {
                pauseLog("defaults restored %d frames after the press: volume 100 / 80, subtitles 1, stereo 1, "
                         "scheme 0, vibration 1, pitch 0",
                         wait - ex.pressAt);
                return pauseFinish(gameui::Outcome::Confirmed);
            }
            if (active != options) {
                if (activeId == static_cast<int>(gui::kScreenBox))
                    pauseLog("defaults: a box opened (the flag was set again)");
                return refuse("Options is not the active screen");
            }
            if (wait > kPauseActionFrames) {
                if (g.read32(options + 0x9c) == 0)
                    g.write32(options + 0x9c, 1); // not taken: as the screen keeps it
                return refuse("the defaults were not restored");
            }
            if (ex.phase == 0) {
                const uint32_t w = pauseWidget(g, active, static_cast<uint16_t>(gui::kOptionsDefaults));
                if (w && g.read32(active + 0x84) == w && wait - ex.focusAt >= 2)
                    g.write32(options + 0x9c, 0); // "asked": the press restores directly
                if (pausePress(g, active, static_cast<uint16_t>(gui::kOptionsDefaults)))
                    ex.phase = 1;
            }
            return;
        }
        case K::QuitToTitle: {
            // Done when the pause is gone (pauseTick ends the binding); the title opens as screen 8.
            if (wait > 3 * kPauseActionFrames) {
                g_pauseQuitToTitle.store(false);
                sh.reactivated.fetch_add(1);
                return refuse("the title did not open");
            }
            if (ex.phase == 0) {
                if (active != ex.screen) {
                    sh.reactivated.fetch_add(1);
                    return refuse("the pause is not the active screen");
                }
                if (!pauseRowEnabled(g, active, static_cast<uint16_t>(gui::kPauseQuit))) {
                    sh.reactivated.fetch_add(1);
                    return refuse("Quit is disabled");
                }
                ex.ownBox = true;
                if (pausePress(g, active, static_cast<uint16_t>(gui::kPauseQuit)))
                    ex.phase = 1;
                return;
            }
            if (ex.phase == 1) {
                if (activeId == static_cast<int>(gui::kScreenBox)) {
                    if (pausePress(g, active, static_cast<uint16_t>(gui::kBoxYes))) {
                        g_pauseQuitToTitleAt = std::chrono::steady_clock::now();
                        g_pauseQuitToTitle.store(true);
                        ex.phase = 2;
                        pauseLog("quit to title: box Yes");
                    }
                } else if (active == ex.screen && wait - ex.pressAt > 90 && ex.taps < 3) {
                    pausePress(g, active, static_cast<uint16_t>(gui::kPauseQuit));
                }
            }
            return;
        }
        case K::QuitToDesktop:
            pauseLog("quit to desktop: closing the window");
            g.requestQuit();
            return pauseFinish(gameui::Outcome::Confirmed);
        case K::OpenPcSettings:
        case K::OpenKeyBindings: {
            // The F1 overlay (on its Controls page for Key Bindings); the screen is suspended until it closes.
            if (ex.phase == 0) {
                if (a.kind == K::OpenKeyBindings)
                    drakan_settings_host::requestOpenControls();
                else
                    drakan_settings_host::requestOpen();
                ex.phase = 1;
                return;
            }
            const bool open = drakan_settings_host::isOpen();
            if (ex.phase == 1) {
                if (open) {
                    ex.phase = 2;
                    pauseLog("settings overlay open");
                } else if (wait > kPauseActionFrames) {
                    sh.reactivated.fetch_add(1);
                    return refuse("the settings overlay did not open");
                }
                return;
            }
            if (!open) {
                sh.reactivated.fetch_add(1);
                pauseLog("settings overlay closed");
                return pauseFinish(gameui::Outcome::Confirmed);
            }
            return;
        }
        default:
            sh.reactivated.fetch_add(1);
            return refuse("not a stage 2 action");
    }
}

// One executor step per frame.
void pauseExecute(RecompGuest& g) {
    using K = gameui::PauseActionKind;
    auto& sh = drakan_pause_screen::shared();
    InputModule& m = module();
    if (!g_pause.action) {
        if (g_pause.child)
            return;
        auto a = sh.actions.pop();
        if (!a)
            return;
        g_pause.action = *a;
        g_pause.wait = 0;
        pauseLog("action %s", gameui::pauseActionName(a->kind));
    }
    const gameui::PauseAction a = *g_pause.action;
    const int wait = ++g_pause.wait;
    if (a.kind == K::Resume) {
        // START = the GUI's key 1 on the pause page = 432708 (resume). The screen goes at once; a tap that was not
        // taken (open transition) is repeated.
        sh.resuming.store(true);
        if (const uint32_t active = pauseActive(g); active != g_pause.screen && pauseOwnedDepth(g, active) > 0) {
            // Owned screens still open (a LeavePage did not finish): close them first, START = back on each.
            if (++g_pause.closeWait % 30 == 1)
                m.injectPadTap(Pad::START, 2);
            g_pause.wait = 0;
            if (g_pause.closeWait > 400) {
                sh.resuming.store(false);
                pauseFinish(gameui::Outcome::Refused);
            }
            return;
        }
        if (wait == 1 || (wait % 45 == 0 && g_pause.taps < 3)) {
            m.injectPadTap(Pad::START, 2);
            ++g_pause.taps;
            pauseLog("resume: START tap %d", g_pause.taps);
        } else if (wait > 150) {
            sh.resuming.store(false); // the overlay opens the screen again
            pauseFinish(gameui::Outcome::Refused);
        }
        return; // done when the screen is gone (pauseTick ends the binding)
    }
    switch (a.kind) {
        case K::OpenSave:
        case K::OpenLoad:
        case K::OpenOptions:
        case K::Quit:
        case K::OpenController:
        case K::OpenAudio:
        case K::DefaultSettings:
            break;
        default:
            return pauseExecuteStage2(g, a, wait);
    }
    if (g_pause.child) { // the row's child is up
        if (g_pause.pressed)
            return pauseFinish(gameui::Outcome::Confirmed);
        return;
    }
    uint16_t row = 0;
    switch (a.kind) {
        case K::OpenSave:
            row = 0x2311;
            break;
        case K::OpenLoad:
            row = 0x2312;
            break;
        case K::OpenOptions:
            row = 0x2313;
            break;
        case K::Quit:
            row = 0x2315;
            break;
        default: // OpenController / OpenAudio / DefaultSettings: only without hostPages (the overlay always sets it)
            sh.reactivated.fetch_add(1);
            return pauseFinish(gameui::Outcome::Refused);
    }
    const uint32_t screen = g_pause.screen;
    const uint32_t target = pauseWidget(g, screen, row);
    if (!target || !pauseRowEnabled(g, screen, row) || wait > 120) {
        sh.reactivated.fetch_add(1); // the host screen takes input again
        return pauseFinish(gameui::Outcome::Refused);
    }
    if (g_pause.pressed)
        return; // waiting for the child (pauseTick sees it) or the time-out above
    const uint32_t focus = g.read32(screen + 0x84);
    if (focus != target) {
        // Focus the row with the GUI's plain setter (2be528, as pausePress does), not with UP / DOWN taps: the taps
        // played the native focus tick for every row on the way, after the host menu's own tick for the move.
        g.call(GuestCall(0x2be528u).arg(screen).arg(target));
        return;
    }
    if (wait % 3 != 1)
        return; // let the last focus step settle
    m.injectPadTap(Pad::CROSS, 1);
    g_pause.pressed = true;
    pauseLog("row %x focused, Cross", row);
}

// EE thread, every PadMgr update: binding, child screens, input mode, snapshot, executor, watchdog.
void pauseTick(uint8_t* ram, RecompGuest& g) {
    hiddenDrawRestorePending(g); // a frame boundary: unfinished hidden draws (of any host screen) are done
    if (!g_pause.screen)
        return;
    auto& sh = drakan_pause_screen::shared();
    if (g.read32(kPauseSlot) != g_pause.screen) {
        if (g_pause.action && g_pause.action->kind == gameui::PauseActionKind::Resume)
            pauseFinish(gameui::Outcome::Confirmed);
        pauseEnd("screen gone");
        return;
    }
    const uint32_t active = g.read32(kInvScreenVar);
    // A save closes every screen (GUI event 0x18: the game resumes), while the manager's pause slot keeps the object.
    g_pause.noScreenFrames = pauseValidPtr(active) ? 0 : g_pause.noScreenFrames + 1;
    if (g_pause.noScreenFrames >= 10) {
        pauseEnd("no screen open (all screens closed)");
        return;
    }
    if (pauseScreenId(g, active) == static_cast<int>(gameui::addr::gui::kScreenTitle)) {
        // Quit Yes (the host's Quit to Title, or the native box in a fallback): the pause closed itself and the title
        // is the active screen, while the manager's pause slot keeps the old object.
        if (g_pause.action && g_pause.action->kind == gameui::PauseActionKind::QuitToTitle)
            pauseFinish(gameui::Outcome::Confirmed);
        pauseEnd("title menu");
        return;
    }
    if (g_pause.fallBack)
        return;
    if (!drakan_pause_screen::enabled() || !drakan_tab::shared().layerReady.load()) {
        pauseFallBack("pause_menu switched to native while the pause was open");
        return;
    }
    // Owned screens (the hidden Options / Audio / Controller, the box the host answers) are not a native child.
    const bool child = active != g_pause.screen && pauseOwnedDepth(g, active) == 0;
    if (child != g_pause.child) {
        g_pause.child = child;
        const uint32_t id = active >= 0x100000u && active < PS2_RAM_SIZE ? g.read32(active + 0x18) : 0xffffffffu;
        pauseLog("native child %s (active %x, screen %d)", child ? "up" : "closed", active, int(id));
        if (!child)
            sh.reactivated.fetch_add(1);
    }
    sh.childUp.store(child);
    module().setHostScreen(!child || menuSlotsHosted()); // the modern Save / Load screen over it takes the input
    {                                                    // enable states (432518) and the status strip's snapshot
        gameui::PauseState st;
        st.canSave = pauseRowEnabled(g, g_pause.screen, 0x2311);
        st.canLoad = pauseRowEnabled(g, g_pause.screen, 0x2312);
        st.canOptions = pauseRowEnabled(g, g_pause.screen, 0x2313);
        st.canQuit = pauseRowEnabled(g, g_pause.screen, 0x2315);
        st.canQuitToTitle = st.canQuit;
        gameui::readPauseOptions(RdramMemory(ram), st.options); // the Audio / Controller pages' values
        std::lock_guard<std::mutex> l(sh.m);
        st.location = sh.state.location;
        if (++g_pause.frames <= 1 || st.canSave != sh.state.canSave || st.canLoad != sh.state.canLoad)
            pauseLog("rows: save %d load %d options %d quit %d", st.canSave ? 1 : 0, st.canLoad ? 1 : 0,
                     st.canOptions ? 1 : 0, st.canQuit ? 1 : 0);
        sh.state = st;
    }
    RdramMemory mem(ram);
    gameui::Snapshot& back = sh.model.back();
    const gameui::ReadStatus rs = gameui::readSnapshot(mem, g_pause.reader, back);
    if (g_pause.frames <= 1)
        pauseLog("snapshot %s: %zu quests, hp %d / %d", gameui::readStatusName(rs), back.quests.size(), back.stats.hp,
                 back.stats.hpMax);
    sh.model.publish();
    if (!g_quickOperation)
        pauseExecute(g);
    // Watchdog (as the Tab screen's): the page is hidden, so the overlay must be drawing the screen.
    constexpr int kWatchFrames = 90;
    constexpr auto kWatchTime = std::chrono::milliseconds(2500);
    const auto now = std::chrono::steady_clock::now();
    const uint64_t drawn = sh.hostDrawn.load();
    if (drawn != g_pause.drawnSeen || child || sh.resuming.load() || drakan_settings_host::isOpen()) {
        g_pause.drawnSeen = drawn;
        g_pause.undrawnFrames = 0;
        g_pause.undrawnSince = now;
    } else if (++g_pause.undrawnFrames >= kWatchFrames && now - g_pause.undrawnSince >= kWatchTime) {
        char why[128];
        std::snprintf(why, sizeof why, "watchdog: host UI not drawn for %d frames (%.1f s)", g_pause.undrawnFrames,
                      std::chrono::duration<double>(now - g_pause.undrawnSince).count());
        pauseFallBack(why);
    }
}

void installPauseScreenHooks(PS2Runtime& rt) {
    g_origPauseHandler = rt.lookupFunction(0x431f00u);
    g_origScreenDraw = rt.lookupFunction(0x2bd1b0u);
    if (!g_origPauseHandler || !g_origScreenDraw) {
        ilog("[pause-screen] 431f00 / 2bd1b0 missing: modern pause menu off\n");
        g_origPauseHandler = g_origScreenDraw = nullptr;
        return;
    }
    rt.replaceFunction(0x431f00u, &hookPauseHandler);
    rt.replaceFunction(0x2bd1b0u, &hookScreenDraw);
    g_origWidgetShow = rt.lookupFunction(0x2bea48u);
    g_origWidgetEnable = rt.lookupFunction(0x2be9a8u);
    if (g_origWidgetShow && g_origWidgetEnable) {
        rt.replaceFunction(0x2bea48u, &hookWidgetSetter<0>);
        rt.replaceFunction(0x2be9a8u, &hookWidgetSetter<1>);
    } else {
        ilog("[pause-screen] 2bea48 / 2be9a8 missing: widget changes during a hidden draw are not followed\n");
    }
}
