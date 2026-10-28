// Included inside drakan_input_glue.cpp's anonymous namespace after RecompGuest, the Map and the content hooks.
// Order-of-the-Flame style views of inventory screen 0x15 (docs/reference/mouse-menus.md section 12):
//   * VIEW_MAP (M): the screen opens through the native SELECT path, jumps to section 3 (Map) through the native
//     section switch 4309d8, and draws the map scaled to fit the screen over the uncropped world.
//   * VIEW_STRIP (I): the screen opens on section 0 (Equipment) and draws the 8 x 3 grid centred along the
//     bottom in a bar (the game's own 2D draws: dark fill, slot frames, a brass frame) with the item details in a
//     framed panel on its left end, without the page art, tabs, the large item preview, the gold or the HUD orbs.
// Every hook chains to the original function; a draw is only steered while the view's own screen draws.
// drakan_input_glue.cpp includes drakan_inventory_views.h and drakan_inventory_views_policy.h at file scope.

struct InventoryView {
    drakan_views::View view = drakan_views::View::None;
    uint32_t screen = 0; // the inventory screen object the view was opened on
    bool layout = false; // custom layout (false: the normal screen, only opened on the view's section)
    bool jump = false;   // the section jump is still to be done
    // Strip / Map: drawn by the host (gameui StripScreen / MapScreen through the overlay, drakan_tab_screen.inl)
    // instead of the native-drawn bar / full-screen map; the Modern view is always host-drawn.
    bool host = false;
    int boundFrame = 0; // frame of the activation (msg 3)
};
InventoryView g_invView;
drakan_views::View g_invViewRequest = drakan_views::View::None;
int g_invViewRequestAge = 0;
// The view's screen is inside its per-frame draw 42fd18 (EE thread). Frame-stamped so a yield cannot leave it set.
uint32_t g_invViewDrawing = 0;
int g_invViewDrawFrame = -1;
drakan_views::MapLayout g_invMap;
int g_invMapFrame = -1;
PS2Runtime::RecompiledFunction g_origViewHandler = nullptr, g_origViewDraw = nullptr, g_origViewViewport = nullptr,
                               g_origViewPanel = nullptr, g_origViewHints = nullptr, g_origViewGold = nullptr,
                               g_origViewTabs = nullptr, g_origViewTabGlow = nullptr, g_origViewSprite = nullptr,
                               g_origViewMarkerFrame = nullptr, g_origViewText = nullptr,
                               g_origViewObjectScale = nullptr, g_origViewObjectTurn = nullptr,
                               g_origViewEquipment = nullptr, g_origViewTextOrigin = nullptr,
                               g_origViewItemPreview = nullptr, g_origViewGrid = nullptr, g_origViewHud = nullptr,
                               g_origViewEquipmentGold = nullptr;

constexpr uint32_t kInvScreenVar = 0x4fb60cu, kInvRendererVar = 0x5565c8u;

bool invViewDrawing(drakan_views::View v) {
    return g_invViewDrawing && g_invViewDrawFrame == module().frameNo() && g_invView.layout && g_invView.view == v;
}
bool invViewDrawingAny() {
    return g_invViewDrawing && g_invViewDrawFrame == module().frameNo() && g_invView.layout;
}
// A host-drawn screen (Tab screen, host strip, host map) is bound to the open inventory screen.
bool invHostView() {
    return g_invView.layout && g_invView.screen && (g_invView.view == drakan_views::View::Modern || g_invView.host);
}
bool invHostDrawing() {
    return invViewDrawingAny() && (g_invView.view == drakan_views::View::Modern || g_invView.host);
}
bool invHostMapDrawing() {
    return invHostDrawing() && g_invView.view == drakan_views::View::Map;
}
void invViewSkip(R5900Context* c) {
    c->pc = getRegU32(c, 31);
}
unsigned invRendererWidth(RecompGuest& g) {
    const uint32_t r = g.read32(kInvRendererVar);
    const uint32_t w = r >= 0x100000u && r < PS2_RAM_SIZE ? g.read32(r + 0xf8) : 0;
    return w >= 64 && w <= 4096 ? w : 512;
}
unsigned invRendererHeight(RecompGuest& g) {
    const uint32_t r = g.read32(kInvRendererVar);
    const uint32_t h = r >= 0x100000u && r < PS2_RAM_SIZE ? g.read32(r + 0xfc) : 0;
    return h >= 64 && h <= 4096 ? h : 512;
}
void invViewLog(const char* fmt, ...) {
    static unsigned lines = 0;
    if (lines++ > 400)
        return;
    va_list ap;
    va_start(ap, fmt);
    char buf[512];
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    ilog("[inv-view] frame=%d %s\n", module().frameNo(), buf);
}

// The game's own 2D draws, queued from inside a view hook as guest invocations of the current thread
// (EeScheduler::invokeCurrentSequence, as the HLE IPU init does): they run as ordinary guest calls, so a draw that
// blocks (its GIF buffer kicked and waited for, its texture loaded on first use) is suspended and resumed by the
// scheduler. Running them as nested host calls hung the game (the block unwound through the nested call). After the
// sequence the thread resumes its own context at the hooked function's entry, i.e. the hook runs again and then
// chains to the original (the caller records the frame as done). The calls run on the thread's stack below the
// hook's stack pointer (the thread is suspended meanwhile); one 16-byte colour block per call sits above them.
//   2c8e98(x, y, w, h, t0 = 4 packed colours): the native 32 x 32 white tile stretched over the rectangle (the dark
//          fill of an equipped item's cell, 435b00 flag 4). Colour 0xAABBGGRR, alpha 0x80 = opaque.
//   2c8ff8(x, y, w, h, t0 = 4 packed colours): the thin slot frame (the selection frame, 435b00 flag 2).
// The bar and panel get a double slot frame (bright brass outside, dim inside, drakan_views::kFrameOut).
class InvDrawBatch {
public:
    InvDrawBatch(uint8_t* ram, R5900Context* c, PS2Runtime* rt)
        : ram_(ram), c_(c), rt_(rt), sp_(getRegU32(c, 29)), next_((getRegU32(c, 29) - kBlocks) & ~15u) {}
    void fill(const drakan_views::Rect& r, uint32_t colour) { boxed(0x2c8e98u, r, colour); }
    void slot(const drakan_views::Rect& r, uint32_t colour) { boxed(0x2c8ff8u, r, colour); }
    void frame(const drakan_views::Rect& r, uint32_t outer, uint32_t inner) {
        const float k = drakan_views::kFrameOut;
        slot({r.x0 - k, r.y0 - k, r.x1 + k, r.y1 + k}, outer);
        slot(r, inner);
    }
    size_t size() const { return calls_.size(); }
    // Queues the calls and unwinds to the scheduler (does not return); returns only when there is nothing to run.
    void run() {
        if (calls_.empty())
            return;
        EeScheduler& ee = rt_->eeScheduler();
        ee.bindMainContextForSyscall(*c_, ram_);
        ee.invokeCurrentSequence(std::move(calls_));
    }

private:
    static constexpr uint32_t kBlocks = 0x500u; // colour blocks between sp - 0x500 and sp - 0x100
    static bool pixels(const drakan_views::Rect& r, int& x, int& y, int& w, int& h) {
        x = int(std::lround(r.x0));
        y = int(std::lround(r.y0));
        w = int(std::lround(r.x1)) - x;
        h = int(std::lround(r.y1)) - y;
        return w > 0 && h > 0;
    }
    void boxed(uint32_t fn, const drakan_views::Rect& r, uint32_t colour) {
        int x, y, w, h;
        if (!pixels(r, x, y, w, h) || !rt_->hasFunction(fn) || next_ + 16 > sp_ - 0x100u)
            return;
        const uint32_t block = next_;
        next_ += 16;
        RecompGuest g(ram_, c_, rt_);
        for (uint32_t i = 0; i < 4; ++i)
            g.write32(block + i * 4, colour);
        GuestInvocation call{};
        call.kind = GuestInvocationKind::HleCall;
        call.context = *c_;
        const uint32_t args[5] = {uint32_t(x), uint32_t(y), uint32_t(w), uint32_t(h), block};
        for (int i = 0; i < 5; ++i) {
            const int reg = 4 + i; // SET_GPR_U32 evaluates its register index twice
            SET_GPR_U32(&call.context, reg, args[i]);
        }
        SET_GPR_U32(&call.context, 29, ((sp_ - kBlocks) & ~15u) - 0x10u);
        SET_GPR_U32(&call.context, 31, 0u);
        call.context.pc = fn;
        call.context.in_delay_slot = false;
        calls_.push_back(std::move(call));
    }
    uint8_t* ram_;
    R5900Context* c_;
    PS2Runtime* rt_;
    uint32_t sp_, next_;
    std::vector<GuestInvocation> calls_;
};
// Colours (0xAABBGGRR; GS vertex colour, 0x80 = full intensity / opaque): the bar and panel fills, each cell's own
// shade and its slot frame (dim brass), the world shade behind the map.
constexpr uint32_t kViewBarShade = 0x48000000u, kViewCellShade = 0x16000000u, kViewSlotFrame = 0x60284858u,
                   kViewPanelShade = 0x50000000u, kViewMapShade = 0x26000000u, kViewFrameOuter = 0x80306880u,
                   kViewFrameInner = 0x70204050u;

// EE thread, every PadMgr update after beginFrame: view keys, request timeout, end of a view, MenuMouse mode.
void inventoryViewsTick(RecompGuest& g, int scr, bool gameplay) {
    using namespace drakan_views;
    InputModule& m = module();
    const uint32_t active = g.read32(kInvScreenVar);
    if (g_invView.screen) {
        const bool valid = active >= 0x100000u && active < PS2_RAM_SIZE;
        const uint32_t parent = valid ? g.read32(active + 0x34) : 0;
        if (!viewScreenUp(g_invView.screen, valid ? active : 0, scr, parent)) {
            invViewLog("view %d ended (screen %x gone, active %d)", int(g_invView.view), g_invView.screen, scr);
            const bool modern = invHostView();
            g_invView = {};
            if (modern)
                tabEnd("screen closed");
            g_centredScreen.store(0, std::memory_order_relaxed);
        }
    }
    if (g_invViewRequest != View::None && ++g_invViewRequestAge > 90) {
        invViewLog("view %d request dropped: the inventory did not open", int(g_invViewRequest));
        g_invViewRequest = View::None;
    }
    if (invHostView()) {
        tabTick(g);
        return;
    }
    View key = View::None;
    const FrameInput& f = m.frame();
    for (const Binding& b : m.config().game) {
        if (b.act != Binding::View)
            continue;
        const bool pressed = (b.src == Binding::Key && f.keyPressed[size_t(b.code) & 255]) ||
                             (b.src == Binding::Mouse && (f.mousePressed & b.code));
        if (pressed)
            key = View(b.value);
    }
    const bool free = gameplay && scr < 0 && !m.suspended() && f.focused && g_invViewRequest == View::None;
    switch (viewKeyAction(key != View::None, free, g_invView.view)) {
        case KeyAction::Open:
            g_invViewRequest = key;
            g_invViewRequestAge = 0;
            m.injectPadTap(Pad::SELECT, 2); // the game's own Inventory / Map command (0x4030)
            invViewLog("view %d requested: SELECT injected", int(key));
            break;
        case KeyAction::Close:
            // Only the inventory itself takes SELECT as "close"; a help box on top keeps its own keys.
            if (scr == 0x15 && active == g_invView.screen) {
                m.injectPadTap(Pad::SELECT, 2);
                invViewLog("view %d: close key, SELECT injected", int(g_invView.view));
            }
            break;
        case KeyAction::None:
            break;
    }
    m.menuMouseRw().setInventoryView(g_invView.layout ? int(g_invView.view) : 0);
    g_centredScreen.store(g_invView.layout ? g_invView.screen : 0, std::memory_order_relaxed);
}

// 430d68(screen, widget, msg): msg 3 activate (42c9a0), 10 per-frame draw (42fd18), 0xb key (4309d8, key at +0x50).
void hookViewHandler(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    using namespace drakan_views;
    if (c->pc != 0x430d68u) {
        g_origViewHandler(ram, c, rt);
        return;
    }
    RecompGuest g(ram, c, rt);
    const uint32_t screen = getRegU32(c, 4), msg = getRegU32(c, 6);
    if (msg == 3) {
        // A new activation: bound to a pending view request, otherwise an ordinary open (Tab) without a view.
        if (g_invViewRequest != View::None) {
            const auto& cfg = module().config();
            g_invView.view = g_invViewRequest;
            g_invView.screen = screen;
            g_invView.layout = g_invViewRequest == View::Map ? cfg.mapView : cfg.inventoryStrip;
            g_invView.jump = targetSection(g_invViewRequest) != kEquipmentSection;
            g_invView.boundFrame = module().frameNo();
            g_invViewRequest = View::None;
            // Host-drawn strip / map (gameui StripScreen / MapScreen) when the overlay's screen layer is up.
            const bool hostWanted = g_invView.view == View::Map ? drakan_tab::mapModern() : drakan_tab::stripModern();
            g_invView.host = g_invView.layout && hostWanted && drakan_tab::shared().layerReady.load();
            invViewLog("view %d bound to screen %x (layout %d, host %d)", int(g_invView.view), screen,
                       g_invView.layout ? 1 : 0, g_invView.host ? 1 : 0);
            g_centredScreen.store(g_invView.layout ? screen : 0, std::memory_order_relaxed);
            if (g_invView.host)
                tabBegin(ram, screen,
                         g_invView.view == View::Map ? drakan_tab::HostKind::Map : drakan_tab::HostKind::Strip);
        } else if (drakan_tab::enabled() && drakan_tab::shared().layerReady.load()) {
            // Tab with inventory_screen = modern: the host-drawn screen over this one (drakan_tab_screen.inl).
            g_invView = {};
            g_invView.view = View::Modern;
            g_invView.screen = screen;
            g_invView.layout = true;
            g_centredScreen.store(screen, std::memory_order_relaxed);
            tabBegin(ram, screen, drakan_tab::HostKind::Tab);
        } else {
            if (invHostView())
                tabEnd("native open");
            g_invView = {};
            g_centredScreen.store(0, std::memory_order_relaxed);
        }
    } else if (msg == 10 && g_invView.screen == screen && g_invView.jump) {
        // Section jump through the native switch: preset the section after the target and deliver "previous" (0xd3)
        // instead of this frame's draw, so only the target's enter handler runs (no intermediate help boxes).
        g_invView.jump = false;
        const int target = targetSection(g_invView.view);
        if (g.read32(screen + 0x1b0) == uint32_t(kSectionCount) && g.read32(screen + 0x154) == 0) {
            const uint32_t savedKey = g.read32(screen + 0x50);
            g.write32(screen + 0x154, uint32_t(jumpPreset(target, kSectionCount)));
            g.write32(screen + 0x50, 0xd3u);
            SET_GPR_U32(c, 6, 0xbu);
            const uint32_t ra = getRegU32(c, 31);
            g_origViewHandler(ram, c, rt);
            if (c->pc == ra)
                g.write32(screen + 0x50, savedKey);
            invViewLog("section jump -> %u", g.read32(screen + 0x154));
            return;
        }
        invViewLog("section jump skipped: sections=%u current=%u", g.read32(screen + 0x1b0), g.read32(screen + 0x154));
        if (g_invView.host)
            tabEnd("not the five-section inventory");
        g_invView.layout = false; // not the 5-section inventory (e.g. a shop): leave it native
        g_invView.host = false;
        g_centredScreen.store(0, std::memory_order_relaxed);
    } else if (msg == 10 && g_invView.screen == screen && invHostView()) {
        tabFrame(ram, c, rt, screen);
    } else if (msg == 0xb && g_invView.screen == screen && g_invView.layout && g_invView.view != View::Modern) {
        const uint32_t key = g.read32(screen + 0x50);
        if (key == 0xd1u || key == 0xd3u) { // R1 / L1, PgDn / PgUp, wheel: a view keeps its section
            SET_GPR_U32(c, 2, 0);
            invViewSkip(c);
            return;
        }
    }
    g_origViewHandler(ram, c, rt);
}

void invViewWholeWorld(RecompGuest& g, uint32_t screen);
// 42fd18(screen): the inventory's per-frame draw. In a view the hooks below keep the page offset at 0, drop the page
// art and steer the section's own draw; the world crop it sets for the next frame is overridden at its use.
void hookViewDraw(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    using namespace drakan_views;
    const uint32_t screen = getRegU32(c, 4);
    if (c->pc != 0x42fd18u || !g_invView.layout || screen != g_invView.screen) {
        g_origViewDraw(ram, c, rt);
        return;
    }
    RecompGuest g(ram, c, rt);
    const int section = int(g.read32(screen + 0x154));
    const bool sectionOk = g_invView.view == View::Modern || section == targetSection(g_invView.view);
    if (g.read32(screen + 0x1b0) != uint32_t(kSectionCount) || !sectionOk) {
        // Not the view's section (a failed jump): the normal page from the next frame on.
        invViewLog("view %d: section %d of %u, layout off", int(g_invView.view), section, g.read32(screen + 0x1b0));
        if (g_invView.view == View::Modern) {
            g_invView = {}; // e.g. a shop (three sections): the native screen
            tabEnd("not the five-section inventory");
        } else if (g_invView.host) {
            g_invView.host = false;
            tabEnd("not the view's section");
        }
        g_invView.layout = false;
        g_centredScreen.store(0, std::memory_order_relaxed);
        g_origViewDraw(ram, c, rt);
        return;
    }
    // The I / M views open and close without the native animation: their content does not slide (the native page art
    // is not drawn), so the close animation only kept the view up (the map stayed ~18 frames after the close key), and
    // during the open the 3D "you are here" arrow sat 3-4 px off its place on the scaled map for ~8 frames (the jitter
    // when the map opens; markers and the settled view do not move). The progress +0x158 is put next to its end, so
    // this frame's native step finishes the animation on its own path: the open goes idle (+0x158 = 1, +0x15c = 0), the
    // close calls 42f758 (camera restore, screen closed). The open is finished only from the third frame after the
    // activation: Select is ignored while it runs, which keeps the opening key's two-frame Select tap from closing the
    // view again.
    // The modern Tab screen skips the close animation the same way (its host screen stayed up, without input, through
    // the ~0.5 s native close: the "delay before the inventory closes"); its open animation is kept, it zooms the world
    // camera in on Rynn for her preview. 42f758 restores the camera saved at activation either way.
    {
        const float speed = g.readF(screen + 0x15c), t = g.readF(screen + 0x158);
        if (g_invView.view != View::Modern && speed > 0 && t < 0.999f && module().frameNo() - g_invView.boundFrame >= 3)
            g.writeF(screen + 0x158, 0.999f);
        else if (speed < 0 && t > 0.001f) {
            g.writeF(screen + 0x158, 0.001f);
            // The modern screen zoomed the world camera in on Rynn: back to the camera saved at activation at once,
            // so the first gameplay frames are not drawn at the preview zoom.
            if (g_invView.view == View::Modern)
                invViewWholeWorld(g, screen);
            invViewLog("view %d: close animation skipped (t %.3f)", int(g_invView.view), t);
        }
    }
    const uint32_t ra = getRegU32(c, 31);
    g_invViewDrawing = screen;
    g_invViewDrawFrame = module().frameNo();
    g_origViewDraw(ram, c, rt);
    if (c->pc == ra)
        g_invViewDrawing = 0;
}

// Renderer slot 0x8c (132f60, f12 = scale, a1 = x offset). 42fd18 moves the page left by W/2 / 3 here (return
// 0x430230). A view keeps the native window (offset 0); the renderer centres its page on the screen instead
// (SceneGuideCamera::CentredPage on the inventory draw packet, hud_page.h inventoryMap).
//
// The same slot applies the world crop for the next world draw (2c4400, return 2c4444: f12 = DAT_004fb608 scale,
// a1 = DAT_004fb604 offset, natively 1 - 2/3 t + 0.1 and W/2 * 2/3 t). While a view is up the world is whole: scale 1,
// offset 0. The open animation also zooms the world camera in on Rynn (cam+0x74/+0x78 = screen+0x160 * (1 - 0.6 t),
// +0x70) for the page's world window; the values saved at activation (42c9a0: +0x160 / +0x164 / +0x168) are put back
// so the world behind a view is the ordinary view. cam = 21ec20's camera (ctx+0x4b4 -> +0xfc -> +0x10).
void invViewWholeWorld(RecompGuest& g, uint32_t screen) {
    const auto ok = [](uint32_t a) { return a >= 0x100000u && a < PS2_RAM_SIZE - 0x200u; };
    const uint32_t ctx = g.read32(0x5565d4u);
    const uint32_t handle = ok(ctx) ? g.read32(ctx + 0x4b4) : 0, holder = ok(handle) ? g.read32(handle + 0xfc) : 0;
    const uint32_t cam = ok(holder) ? g.read32(holder + 0x10) : 0;
    const float zx = g.readF(screen + 0x160), zy = g.readF(screen + 0x164), zz = g.readF(screen + 0x168);
    if (ok(cam) && std::isfinite(zx) && std::isfinite(zy) && std::isfinite(zz) && zx > 0 && zy > 0) {
        g.writeF(cam + 0x74, zx);
        g.writeF(cam + 0x78, zy);
        g.writeF(cam + 0x70, zz);
    }
}
// A view with its own layout is up: its screen is active, or a message box stacked on it.
bool invViewShown(RecompGuest& g) {
    if (!g_invView.layout || !g_invView.screen)
        return false;
    const uint32_t active = g.read32(kInvScreenVar);
    const bool valid = active >= 0x100000u && active < PS2_RAM_SIZE;
    const int id = valid ? int(g.read32(active + 0x18)) : -1;
    return drakan_views::viewScreenUp(g_invView.screen, valid ? active : 0, id, valid ? g.read32(active + 0x34) : 0);
}
void hookViewViewport(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t ra = getRegU32(c, 31);
    if (c->pc == 0x132f60u && ra == 0x430230u && invViewDrawingAny())
        SET_GPR_U32(c, 5, 0);
    float fraction = 0;
    uint32_t cropObj = 0;
    if (c->pc == 0x132f60u && ra == 0x2c4444u && g_invView.layout && g_invView.screen) {
        RecompGuest g(ram, c, rt);
        // The modern screen's crop applies from its activation on (the first frames, before the screen is the active
        // one, would otherwise show the native open crop: the world cut off where the hidden page would be).
        if (g_invView.view == drakan_views::View::Modern || invViewShown(g)) {
            if (g_invView.view == drakan_views::View::Modern) {
                fraction = tabWorldCrop(g, c);
                cropObj = getRegU32(c, 4) & 0x1fffffffu;
            } else {
                c->f[12] = 1.0f;
                SET_GPR_U32(c, 5, 0);
                invViewWholeWorld(g, g_invView.screen);
            }
        }
    }
    g_origViewViewport(ram, c, rt);
    // 132f60 takes the offset in whole frame pixels: add the modern preview's fraction to the projection centre
    // (+0x100) and the crop edges (+0x50 / +0x54) it just wrote, so the eased follow moves smoothly.
    if (fraction != 0 && c->pc == ra && cropObj >= 0x100000u && cropObj < PS2_RAM_SIZE - 0x120u) {
        for (uint32_t off : {0x50u, 0x54u, 0x100u}) {
            float v;
            std::memcpy(&v, ram + cropObj + off, 4);
            v += fraction;
            std::memcpy(ram + cropObj + off, &v, 4);
        }
    }
}

// Gameplay HUD 3ac540(hud = 0x510150, frame), from the HUD screen's draw 3b2178: the health orb and its gauge
// (3ac008 pieces, native x 28..110, y 379..494), the mana orb (x 417..481) and the counters. The inventory draws over
// it; a view hides it (the orbs would sit under the map and over the strip's bar). Returns 0. The modern screen hides
// it while the host screen is at least half opaque, so it switches in the middle of the host's fade in and out instead
// of popping at the native open / close (the host screen stays up through the close animation).
void hookViewHud(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x3ac540u) {
        RecompGuest g(ram, c, rt);
        const bool host = invHostView();
        if ((invViewShown(g) && !host) || drakan_tab::shared().hostFade.load() >= 0.5f ||
            drakan_pause_screen::shared().hostFade.load() >= 0.5f) {
            SET_GPR_U32(c, 2, 0);
            return invViewSkip(c);
        }
    }
    g_origViewHud(ram, c, rt);
}

// Page art: 42a2c0 (the section's 3D panels, from the base draw 42b5c8), 431a10 (the hint bar), 42b988 (the gold
// count), 42b308 (tab icons) and 42faa0 (tab glow). Skipped while a view draws.
void hookViewPanel(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t ra = getRegU32(c, 31);
    if (c->pc == 0x42a2c0u && ra >= 0x42b5c8u && ra < 0x42b738u && invViewDrawingAny())
        return invViewSkip(c);
    g_origViewPanel(ram, c, rt);
}
void hookViewHints(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x431a10u && invViewDrawingAny())
        return invViewSkip(c);
    g_origViewHints(ram, c, rt);
}
void hookViewGold(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x42b988u && invViewDrawingAny())
        return invViewSkip(c);
    g_origViewGold(ram, c, rt);
}
void hookViewTabs(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x42b308u && invViewDrawingAny())
        return invViewSkip(c);
    g_origViewTabs(ram, c, rt);
}
void hookViewTabGlow(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x42faa0u && invViewDrawingAny())
        return invViewSkip(c);
    g_origViewTabGlow(ram, c, rt);
}

// The map hooks run for the M view and for the modern screen's Map page (drakan_tab_screen.inl).
bool invMapDrawing() {
    return invViewDrawing(drakan_views::View::Map) || tabMapDrawing();
}

// Map section draw 439b28. Sprite method 1c1e90(f12 = x scale, f13 = y scale (-1: f12), a0 = image, a1 = x, a2 = y,
// t1 / t2 = width / height). Return 439d80 = the map background, 43a330 = a marker, 43a498 = the caption icon.
void hookViewSprite(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    using namespace drakan_views;
    const uint32_t ra = getRegU32(c, 31);
    if (c->pc == 0x1c1e90u && (ra == 0x439d80u || ra == 0x43a330u || ra == 0x43a498u) && invHostDrawing() &&
        !tabMapDrawing()) {
        // The modern screen without a host map area (another page, or the screen closing: its open / close animation
        // would slide the native map across the picture): no native map pieces.
        return invViewSkip(c);
    }
    if (c->pc == 0x1c1e90u && ra == 0x43a498u && invHostMapDrawing())
        return invViewSkip(c); // host map: the selected marker's caption is the host's name tag
    const bool modern = c->pc == 0x1c1e90u && ra == 0x439d80u && tabMapDrawing();
    if (modern) {
        // The modern screen's Map page / the host M view: the map where the host shows it (the Tab screen dims with
        // its backdrop; the M view keeps the native world shade, queued once per frame like the native view's).
        RecompGuest g(ram, c, rt);
        const float x = float(int32_t(getRegU32(c, 5))), y = float(int32_t(getRegU32(c, 6)));
        const float w = float(int32_t(getRegU32(c, 9))), h = float(int32_t(getRegU32(c, 10)));
        if (invHostMapDrawing()) {
            static int hostShadeFrame = -1;
            if (hostShadeFrame != module().frameNo()) {
                hostShadeFrame = module().frameNo();
                tabPublishMarkers(); // the previous frame's markers, complete
                InvDrawBatch shade(ram, c, rt);
                shade.fill({0, 0, float(invRendererWidth(g)), float(invRendererHeight(g))}, kViewMapShade);
                shade.run();
            }
        }
        g_invMap = tabMapLayout(x, y, w, h, invRendererWidth(g), invRendererHeight(g));
        g_invMapFrame = module().frameNo();
        if (g_invMap.valid) {
            c->f[12] = c->f[13] = g_invMap.fit.scale;
            SET_GPR_U32(c, 5, uint32_t(int32_t(std::lround(g_invMap.fit.x))));
            SET_GPR_U32(c, 6, uint32_t(int32_t(std::lround(g_invMap.fit.y))));
        }
    } else if (c->pc == 0x1c1e90u && (ra == 0x439d80u || ra == 0x43a330u || ra == 0x43a498u) && invMapDrawing()) {
        RecompGuest g(ram, c, rt);
        const float x = float(int32_t(getRegU32(c, 5))), y = float(int32_t(getRegU32(c, 6)));
        const float w = float(int32_t(getRegU32(c, 9))), h = float(int32_t(getRegU32(c, 10)));
        if (ra == 0x439d80u) {
            // The world around the map is dimmed: one solid full-window sprite before the map (left unmapped by the
            // renderer, hud::inventoryViewShade, so it covers the whole output). Queued once per frame; the sprite
            // call is entered again after it.
            static int shadeFrame = -1;
            if (shadeFrame != module().frameNo()) {
                shadeFrame = module().frameNo();
                InvDrawBatch shade(ram, c, rt);
                shade.fill({0, 0, float(invRendererWidth(g)), float(invRendererHeight(g))}, kViewMapShade);
                shade.run();
            }
            g_invMap = mapLayout(x, y, w, h, invRendererWidth(g), invRendererHeight(g));
            g_invMapFrame = module().frameNo();
            static int logged = 0;
            if (logged++ < 4)
                invViewLog("map image %gx%g at %g,%g -> scale %.3f at %.1f,%.1f icons %.2f", w, h, x, y,
                           g_invMap.fit.scale, g_invMap.fit.x, g_invMap.fit.y, g_invMap.iconScale);
            if (g_invMap.valid) {
                c->f[12] = c->f[13] = g_invMap.fit.scale;
                SET_GPR_U32(c, 5, uint32_t(int32_t(std::lround(g_invMap.fit.x))));
                SET_GPR_U32(c, 6, uint32_t(int32_t(std::lround(g_invMap.fit.y))));
            }
        } else if (g_invMap.valid && g_invMapFrame == module().frameNo()) {
            float nx = x, ny = y;
            if (ra == 0x43a330u) {
                const float k = g_invMap.iconScale;
                placeCentred(g_invMap, x, y, w, h, w * k, h * k, nx, ny);
                c->f[12] = k;            // f13 stays -1: height follows f12
                if (invHostMapDrawing()) // s3 = the record index (as the map mouse capture reads it)
                    tabNoteMarker(int(getRegU32(c, 19)), nx + 0.5f * w * k, ny + 0.5f * h * k);
            } else {
                nx = g_invMap.captionX(x);
                ny = g_invMap.captionY(y);
            }
            SET_GPR_U32(c, 5, uint32_t(int32_t(std::lround(nx))));
            SET_GPR_U32(c, 6, uint32_t(int32_t(std::lround(ny))));
        }
    }
    g_origViewSprite(ram, c, rt);
}
// 4398f8(section, x, y, w, h, selected): the frame around the selected marker (marker size + 16), return 43a22c.
void hookViewMarkerFrame(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    // Host map (M view) and the Tab screen's Map page: the host rings the focused record's marker. On the Map page the
    // native frame (a bright halo) stayed on the native selection, which the host's list does not move, so two
    // different markers were highlighted.
    if (c->pc == 0x4398f8u && getRegU32(c, 31) == 0x43a22cu && (invHostMapDrawing() || tabMapDrawing()))
        return invViewSkip(c);
    if (c->pc == 0x4398f8u && getRegU32(c, 31) == 0x43a22cu && invMapDrawing() && g_invMap.valid &&
        g_invMapFrame == module().frameNo()) {
        const float x = float(int32_t(getRegU32(c, 5))), y = float(int32_t(getRegU32(c, 6)));
        const float w = float(int32_t(getRegU32(c, 7))), h = float(int32_t(getRegU32(c, 8)));
        const float k = g_invMap.iconScale;
        const float nw = (w - 16) * k + 16, nh = (h - 16) * k + 16;
        float nx = x, ny = y;
        drakan_views::placeCentred(g_invMap, x, y, w, h, nw, nh, nx, ny);
        SET_GPR_U32(c, 5, uint32_t(int32_t(std::lround(nx))));
        SET_GPR_U32(c, 6, uint32_t(int32_t(std::lround(ny))));
        SET_GPR_U32(c, 7, uint32_t(int32_t(std::lround(nw))));
        SET_GPR_U32(c, 8, uint32_t(int32_t(std::lround(nh))));
    }
    g_origViewMarkerFrame(ram, c, rt);
}
// 286170(x, y, text, ...): the selected marker's caption, return 43a4e0.
void hookViewText(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x286170u && getRegU32(c, 31) == 0x43a4e0u && invHostMapDrawing())
        return invViewSkip(c);
    if (c->pc == 0x286170u && getRegU32(c, 31) == 0x43a4e0u && invMapDrawing() && g_invMap.valid &&
        g_invMapFrame == module().frameNo()) {
        const float x = float(int32_t(getRegU32(c, 4))), y = float(int32_t(getRegU32(c, 5)));
        SET_GPR_U32(c, 4, uint32_t(int32_t(std::lround(g_invMap.captionX(x)))));
        SET_GPR_U32(c, 5, uint32_t(int32_t(std::lround(g_invMap.captionY(y)))));
    }
    g_origViewText(ram, c, rt);
}
// The "you are here" object (section+0x68): 13f6a8(f12, f13, f14 = scale, a0 = matrix), return 439f00, then its
// position is written from the map pixel (+0x58/+0x5c + origin) before 13edd8(a0 = matrix, ...) returns to 439fbc.
void hookViewObjectScale(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x13f6a8u && getRegU32(c, 31) == 0x439f00u && invMapDrawing() && g_invMap.valid &&
        g_invMapFrame == module().frameNo()) {
        const float k = g_invMap.iconScale;
        c->f[12] *= k;
        c->f[13] *= k;
        c->f[14] *= k;
    }
    g_origViewObjectScale(ram, c, rt);
}
void hookViewObjectTurn(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    using namespace drakan_views;
    if (c->pc == 0x13edd8u && getRegU32(c, 31) == 0x439fbcu && invMapDrawing() && g_invMap.valid &&
        g_invMapFrame == module().frameNo()) {
        RecompGuest g(ram, c, rt);
        const uint32_t m = getRegU32(c, 4), r = g.read32(kInvRendererVar);
        if (r >= 0x100000u && r < PS2_RAM_SIZE && m >= 0x100000u && m < PS2_RAM_SIZE) {
            const float kx = g.readF(r + 0x40) * g.readF(r + 0x48), ky = g.readF(r + 0x44) * g.readF(r + 0x4c);
            const unsigned w = invRendererWidth(g), h = invRendererHeight(g);
            if (std::isfinite(kx) && std::isfinite(ky) && std::fabs(kx) > 1e-6f && std::fabs(ky) > 1e-6f) {
                const float px = objectToPixelX(g.readF(m), kx, w), py = objectToPixelY(g.readF(m + 4), ky, h);
                g.writeF(m, pixelToObjectX(g_invMap.x(px), kx, w));
                g.writeF(m + 4, pixelToObjectY(g_invMap.y(py), ky, h));
            }
        }
    }
    g_origViewObjectTurn(ram, c, rt);
}

// Strip view, Equipment section draw 433610(section, frame): the grid's cells are at (+0x68 + col * cw,
// +0x6c - row * ch) on the section plane (+0xc transform), cw / ch at +0x60 / +0x64. For the draw the grid centre is
// moved so its projection lands centred at the bottom of the window (Newton steps through the live UI camera), then
// restored. The grid draw 437170 and the menu mouse's capture read the moved fields, so hit testing follows.
struct StripState {
    uint32_t section = 0;
    float x0 = 0, y0 = 0;
    bool moved = false;
    drakan_views::StripTarget target;
    int frame = -1;
    drakan_views::StripPanel panel; // this frame's details panel (empty: no item selected)
    int panelFrame = -1;
    int decorFrame = -1; // the bar was queued this frame (the grid draw is entered again after it)
};
StripState g_strip;
bool invProject(RecompGuest& g, uint32_t transform, float u, float v, float& x, float& y) {
    return module().menuMouse().projectInventoryPlane(g, transform, u, v, x, y);
}
void hookViewEquipment(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    using namespace drakan_views;
    if (c->pc == 0x433610u && invHostDrawing()) {
        RecompGuest g(ram, c, rt);
        const uint32_t section = getRegU32(c, 4);
        tabPlaceGrid(g, section);
        const uint32_t ra = getRegU32(c, 31);
        g_origViewEquipment(ram, c, rt);
        if (c->pc == ra)
            tabRestoreGrid(g, section);
        return;
    }
    if (c->pc != 0x433610u || !invViewDrawing(View::Strip)) {
        g_origViewEquipment(ram, c, rt);
        return;
    }
    RecompGuest g(ram, c, rt);
    const uint32_t section = getRegU32(c, 4);
    if (g_strip.moved && g_strip.section == section) { // a yielded draw left them moved
        g.writeF(section + 0x68, g_strip.x0);
        g.writeF(section + 0x6c, g_strip.y0);
    }
    g_strip = {};
    const uint32_t transform = g.read32(section + 0xc);
    const float x0 = g.readF(section + 0x68), y0 = g.readF(section + 0x6c);
    const float cw = g.readF(section + 0x60), ch = g.readF(section + 0x64);
    bool ok = transform >= 0x100000u && transform < PS2_RAM_SIZE && cw > 0 && ch > 0 && std::isfinite(x0) &&
              std::isfinite(y0);
    float u = gridCentrePlaneX(x0, cw), v = gridCentrePlaneY(y0, ch);
    StripTarget target;
    for (int step = 0; ok && step < 3; ++step) {
        float sx = 0, sy = 0, ax = 0, ay = 0, bx = 0, by = 0;
        ok = invProject(g, transform, u, v, sx, sy) && invProject(g, transform, u + cw, v, ax, ay) &&
             invProject(g, transform, u, v + ch, bx, by);
        if (!ok)
            break;
        const float dxdu = (ax - sx) / cw, dydu = (ay - sy) / cw, dxdv = (bx - sx) / ch, dydv = (by - sy) / ch;
        if (step == 0)
            target = stripTarget(std::fabs(dxdu) * 8 * cw, std::fabs(dydv) * 3 * ch, invRendererWidth(g),
                                 invRendererHeight(g));
        if (step == 2)
            break;
        ok = solvePlane(u, v, sx, sy, dxdu, dxdv, dydu, dydv, target.gridCentreX, target.gridCentreY, u, v);
    }
    if (ok) {
        const float nx = x0 + (u - gridCentrePlaneX(x0, cw)), ny = y0 + (v - gridCentrePlaneY(y0, ch));
        g.writeF(section + 0x68, nx);
        g.writeF(section + 0x6c, ny);
        g_strip = {section, x0, y0, true, target, module().frameNo()};
        static int logged = 0;
        if (logged++ < 3)
            invViewLog("strip grid plane %.1f,%.1f -> %.1f,%.1f (cell %.1fx%.1f) target %.1f,%.1f text %.1f,%.1f", x0,
                       y0, nx, ny, cw, ch, target.gridCentreX, target.gridCentreY, target.textLeft, target.textTop);
    }
    const uint32_t ra = getRegU32(c, 31);
    g_origViewEquipment(ram, c, rt);
    if (c->pc == ra && g_strip.moved) {
        g.writeF(section + 0x68, g_strip.x0);
        g.writeF(section + 0x6c, g_strip.y0);
        g_strip.moved = false;
    }
}
// 4314a0(out, section, point) from the details text draw 433850 (return 433a2c): the text block's origin. The lines
// follow 0x12 apart; the block goes above the moved grid.
void hookViewTextOrigin(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t ra = getRegU32(c, 31), out = getRegU32(c, 4);
    const bool steer = c->pc == 0x4314a0u && ra == 0x433a2cu && invViewDrawing(drakan_views::View::Strip) &&
                       g_strip.frame == module().frameNo();
    g_origViewTextOrigin(ram, c, rt);
    if (steer && c->pc == ra) {
        RecompGuest g(ram, c, rt);
        const bool panel = g_strip.panelFrame == module().frameNo() && !g_strip.panel.fill.empty();
        g.writeF(out, panel ? g_strip.panel.textLeft : g_strip.target.textLeft);
        g.writeF(out + 4, panel ? g_strip.panel.textTop : g_strip.target.textTop);
    }
}
// Grid draw 437170(section, frame), from the Equipment draw with the grid moved: before the items, the bar and the
// details panel are drawn with the game's own 2D draws (see InvDrawBatch). The panel is sized to this frame's details
// text (controller section+0x70: +0x4ac = an item is selected, its text lines as 433850 draws them).
// Length up to the last visible character: the game fills unused lines with a blank (" "), which takes no room.
int invStringLength(RecompGuest& g, uint32_t a) {
    int n = 0, visible = 0;
    while (n < 96) {
        const uint8_t ch = g.read8(a + uint32_t(n));
        if (!ch)
            break;
        ++n;
        if (ch > ' ')
            visible = n;
    }
    return visible;
}
void hookViewGrid(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    using namespace drakan_views;
    if (c->pc == 0x437170u && invHostDrawing() && g_tabGrid.section == getRegU32(c, 4)) {
        if (g_tabGrid.hidden) // another host page: no native icons
            return invViewSkip(c);
        // A native box over the strip (first-visit help) is drawn after the grid, inside a capture scope: the grid is
        // not drawn meanwhile (the host strip steps aside while the box is up).
        if (g_invView.view == View::Strip && drakan_tab::shared().boxUp.load())
            return invViewSkip(c);
        // The icons are drawn over a native black fill of the placed grid, so the overlay's crops of them are clean.
        // The host strip has no opaque plate where its grid fits inside the native window: the renderer keeps the
        // fill and the icons in an off-screen copy, which the overlay crops, and restores the frame under the grid's
        // box (drakan_scene_camera::gridCapture). The scope opens here, before the fill; it ends in the renderer when
        // that frame is displayed (the item draws reach the GS after this call returns).
        static int fillFrame = -1;
        if (g_tabGrid.moved && fillFrame != module().frameNo()) {
            fillFrame = module().frameNo();
            const Rect& t = g_tabGrid.target;
#if defined(PS2X_HAVE_GSR)
            if (g_invView.view == View::Strip)
                drakan_scene_camera::gridCapture(*rt, true, t.x0 - 2, t.y0 - 2, t.x1 + 2, t.y1 + 2);
#endif
            InvDrawBatch d(ram, c, rt);
            d.fill({t.x0 - 2, t.y0 - 2, t.x1 + 2, t.y1 + 2}, 0x80000000u);
            d.run(); // does not return when anything was queued
        }
    }
    if (c->pc == 0x437170u && invViewDrawing(View::Strip) && g_strip.moved && g_strip.frame == module().frameNo() &&
        g_strip.section == getRegU32(c, 4) && g_strip.decorFrame != module().frameNo()) {
        RecompGuest g(ram, c, rt);
        const StripTarget& t = g_strip.target;
        const uint32_t ctl = g.read32(g_strip.section + 0x70);
        int len[7] = {};
        if (ctl >= 0x100000u && ctl < PS2_RAM_SIZE - 0x500u && g.read32(ctl + 0x4ac) != 0) {
            const uint32_t offsets[7] = {0x244, 0x2c4, 0x284, 0x304, 0x450, 0x344, 0x384};
            for (int i = 0; i < 7; ++i)
                len[i] = invStringLength(g, ctl + offsets[i]);
            len[6] += len[6] ? 10 : 0; // 433850 indents the last line 45 pixels (an icon goes before it)
        }
        const int lines = detailsLines(len);
        int stat = 0;
        for (int i = 1; i < 7; ++i)
            stat = std::max(stat, len[i]);
        g_strip.panel = stripPanel(t, lines, len[0], stat);
        g_strip.panelFrame = module().frameNo();
        InvDrawBatch d(ram, c, rt);
        const Rect bar = stripBar(t);
        if (!g_strip.panel.fill.empty()) {
            d.fill(g_strip.panel.fill, kViewPanelShade);
            d.frame(g_strip.panel.fill, kViewFrameOuter, kViewFrameInner);
        }
        d.fill(bar, kViewBarShade);
        for (int row = 0; row < kStripRows; ++row)
            for (int col = 0; col < kStripColumns; ++col) {
                const Rect cell = stripCell(t, col, row);
                d.fill({cell.x0 + 1, cell.y0 + 1, cell.x1 - 1, cell.y1 - 1}, kViewCellShade);
                d.slot(cell, kViewSlotFrame);
            }
        d.frame(bar, kViewFrameOuter, kViewFrameInner);
        static unsigned logged = 0;
        if (logged++ < 3)
            invViewLog("strip bar %.0f,%.0f..%.0f,%.0f panel %.0f,%.0f..%.0f,%.0f lines %d chars %d/%d calls %zu",
                       bar.x0, bar.y0, bar.x1, bar.y1, g_strip.panel.fill.x0, g_strip.panel.fill.y0,
                       g_strip.panel.fill.x1, g_strip.panel.fill.y1, lines, len[0], stat, d.size());
        g_strip.decorFrame = module().frameNo();
        d.run(); // does not return when anything was queued
    }
    g_origViewGrid(ram, c, rt);
}

// 4330b0(section, frame), from the Equipment draw: the gold amount (text box 42ae00) and the spinning gold bag
// (4316c8). Hidden in the strip like the orbs.
void hookViewEquipmentGold(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x4330b0u && (invViewDrawing(drakan_views::View::Strip) || invViewDrawing(drakan_views::View::Modern)))
        return invViewSkip(c);
    g_origViewEquipmentGold(ram, c, rt);
}

// 436b78(section, frame, 0): the large spinning preview of the selected item, from the grid draw 437170.
void hookViewItemPreview(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t ra = getRegU32(c, 31);
    if (c->pc == 0x436b78u && ra >= 0x437170u && ra < 0x437760u &&
        (invViewDrawing(drakan_views::View::Strip) || invViewDrawing(drakan_views::View::Modern)))
        return invViewSkip(c);
    g_origViewItemPreview(ram, c, rt);
}

// Modern screen only: the Equipment details text 433850(section, frame) (the host draws the details) and the cell
// decoration 435b00 (the equipped cell's dark fill and the selection frame; the host draws its own).
PS2Runtime::RecompiledFunction g_origViewDetails = nullptr, g_origViewCellDecor = nullptr;
void hookViewDetails(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x433850u && invHostDrawing())
        return invViewSkip(c);
    g_origViewDetails(ram, c, rt);
}
void hookViewCellDecor(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x435b00u && invHostDrawing())
        return invViewSkip(c);
    g_origViewCellDecor(ram, c, rt);
}

// Installed before the Map mouse, content and inventory hooks, so theirs wrap these: they observe the native
// arguments (marker f12 == 1) and the submitted, already moved vertices.
void installInventoryViewHooks(PS2Runtime& rt) {
    struct Hook {
        uint32_t address;
        PS2Runtime::RecompiledFunction* original;
        PS2Runtime::RecompiledFunction hook;
    };
    const Hook hooks[] = {
        {0x430d68u, &g_origViewHandler, &hookViewHandler},
        {0x42fd18u, &g_origViewDraw, &hookViewDraw},
        {0x132f60u, &g_origViewViewport, &hookViewViewport},
        {0x42a2c0u, &g_origViewPanel, &hookViewPanel},
        {0x431a10u, &g_origViewHints, &hookViewHints},
        {0x42b988u, &g_origViewGold, &hookViewGold},
        {0x42b308u, &g_origViewTabs, &hookViewTabs},
        {0x42faa0u, &g_origViewTabGlow, &hookViewTabGlow},
        {0x1c1e90u, &g_origViewSprite, &hookViewSprite},
        {0x4398f8u, &g_origViewMarkerFrame, &hookViewMarkerFrame},
        {0x286170u, &g_origViewText, &hookViewText},
        {0x13f6a8u, &g_origViewObjectScale, &hookViewObjectScale},
        {0x13edd8u, &g_origViewObjectTurn, &hookViewObjectTurn},
        {0x433610u, &g_origViewEquipment, &hookViewEquipment},
        {0x4314a0u, &g_origViewTextOrigin, &hookViewTextOrigin},
        {0x436b78u, &g_origViewItemPreview, &hookViewItemPreview},
        {0x437170u, &g_origViewGrid, &hookViewGrid},
        {0x3ac540u, &g_origViewHud, &hookViewHud},
        {0x4330b0u, &g_origViewEquipmentGold, &hookViewEquipmentGold},
    };
    // The views need all of them (a missing draw hook would show the normal page half rearranged).
    for (const auto& h : hooks)
        if (!rt.lookupFunction(h.address)) {
            ilog("[inv-view] %x missing: inventory views off\n", h.address);
            return;
        }
    for (const auto& h : hooks) {
        *h.original = rt.lookupFunction(h.address);
        rt.replaceFunction(h.address, h.hook);
    }
    // Optional for the modern screen (a missing one leaves that native draw visible under it).
    const Hook modern[] = {{0x433850u, &g_origViewDetails, &hookViewDetails},
                           {0x435b00u, &g_origViewCellDecor, &hookViewCellDecor}};
    for (const auto& h : modern)
        if ((*h.original = rt.lookupFunction(h.address)))
            rt.replaceFunction(h.address, h.hook);
        else
            ilog("[inv-view] %x missing: the modern screen shows that native draw\n", h.address);
}
