// Drakan: the modern Tab screen, the I strip and the M map's objectives panel (runtime/gameui), drawn by the
// overlay's screen layer on the present thread. EE side: drakan_tab_screen.inl.
#if defined(PS2X_HAVE_DRAKAN_SETTINGS)
#include "drakan_settings_host_internal.h"

namespace drakan_settings_host::detail {

TabUi g_tabUi;

namespace {
// Native GUI units (W x H frame) <-> back buffer pixels of the inventory page, which the renderer centres and
// compresses by hudScaleX (CentredPage, hud::inventoryMap) inside the game image rectangle.
struct PageMap {
    float vx = 0, vy = 0, vw = 1, vh = 1, hs = 1, W = 512, H = 512;
    float x(float gx) const { return vx + vw * (0.5f + (gx / W - 0.5f) * hs); }
    float y(float gy) const { return vy + vh * gy / H; }
    float gx(float x) const { return (0.5f + ((x - vx) / vw - 0.5f) / hs) * W; }
    float gy(float y) const { return (y - vy) / vh * H; }
};
PageMap tabPageMap(const gsr::OverlayContext& oc, float w, float h, unsigned frameW, unsigned frameH) {
    PageMap m;
    m.vx = oc.viewW > 0 ? oc.viewX : 0;
    m.vy = oc.viewH > 0 ? oc.viewY : 0;
    m.vw = oc.viewW > 0 ? oc.viewW : w;
    m.vh = oc.viewH > 0 ? oc.viewH : h;
    m.hs = oc.hudScaleX > 0.2f && oc.hudScaleX <= 1.0f ? oc.hudScaleX : 1.0f;
    m.W = float(frameW ? frameW : 512);
    m.H = float(frameH ? frameH : 512);
    return m;
}

// The bound host screen, whichever it is.
bool hostOpen(const TabUi& ui) {
    switch (ui.kind) {
        case drakan_tab::HostKind::Strip:
            return ui.strip.isOpen();
        case drakan_tab::HostKind::Map:
            return ui.map.isOpen();
        default:
            return ui.screen.isOpen();
    }
}
bool hostClosePending(const TabUi& ui) {
    switch (ui.kind) {
        case drakan_tab::HostKind::Strip:
            return ui.strip.closePending();
        case drakan_tab::HostKind::Map:
            return false;
        default:
            return ui.screen.closePending();
    }
}
void hostOpenScreen(TabUi& ui, const gameui::Snapshot& s) {
    switch (ui.kind) {
        case drakan_tab::HostKind::Strip:
            ui.strip.open(s);
            break;
        case drakan_tab::HostKind::Map:
            ui.map.open(s);
            break;
        default:
            ui.screen.open(s);
            break;
    }
}
void hostCloseScreen(TabUi& ui) {
    ui.screen.close();
    ui.strip.close();
    ui.map.close();
}
// Where the native grid goes (under the host grid: Inventory / Character page, the strip's bar), where the native
// map goes (Map page, the M view left of the objectives panel) and where Rynn stands (Tab screen). The EE glue
// draws them there, and the host screen crops its item icons and map picture from those spots of the frame.
void publishNativeTargets(TabUi& ui, const dsettings::SettingsOverlay::ScreenFrame& f, const gameui::Viewport& vp,
                          const PageMap& pm, const drakan_tab::MapCapture& mc) {
    using drakan_tab::HostKind;
    auto& sh = drakan_tab::shared();
    const gameui::LayoutFrame lf = gameui::computeFrame(vp);
    drakan_tab::GridTarget t;
    drakan_tab::MapTarget mt;
    drakan_tab::PreviewTarget p;
    const bool placed = hostOpen(ui) || hostClosePending(ui); // kept through the close animation
    // Targets stay inside the native window, which the GS front end clips: a letterboxed game picture (21:9 /
    // 32:9 on a 16:9 output) put the host's design rectangles partly above / beside it (the Map page lost the
    // map's left part, the 32:9 grid its icons). The grid is moved / shrunk whole (the crops come from the cells
    // where it was drawn, NativeGrid), the map area clipped (the map is fitted into it).
    constexpr float kWindowMargin = 8.0f;
    const auto gridBox = [&](const gameui::Rect& a, const gameui::Rect& b) {
        const drakan_views::Box k = drakan_views::keepInWindow(
            {pm.gx(a.x), pm.gy(a.y), pm.gx(b.right()), pm.gy(b.bottom())}, pm.W, pm.H, kWindowMargin);
        return drakan_tab::GridTarget{true, k.x0, k.y0, k.x1, k.y1};
    };
    if (ui.kind == HostKind::Tab) {
        // The native grid stays under the host grid on the Inventory page, and under the opaque centre column on
        // the Character page (its equipped gear shows the same crops).
        const gameui::Page page = ui.screen.page();
        if (placed && (page == gameui::Page::Inventory || page == gameui::Page::Character))
            t = gridBox(lf.map(gameui::design::gridCell(0)), lf.map(gameui::design::gridCell(23)));
        if (placed && page == gameui::Page::Map) {
            const gameui::Rect a = lf.map(gameui::design::mapImageArea());
            const drakan_views::Box k = drakan_views::clipToWindow(
                {pm.gx(a.x), pm.gy(a.y), pm.gx(a.right()), pm.gy(a.bottom())}, pm.W, pm.H, kWindowMargin);
            if (k.w() > 32 && k.h() > 32)
                mt = {true, k.x0, k.y0, k.x1, k.y1};
        }
        // Rynn's centre: the column's, kept inside the game picture (a 4:3 pillarbox cut her in half); her
        // on-screen half width follows the picture height.
        const gameui::Rect pr = ui.screen.previewRect();
        if (!pr.empty())
            p = {true, drakan_tab::detail::previewCentreInPicture(pr.cx(), pm.vx, pm.vw, 0.2f * pm.vh), 1.0f};
    } else if (ui.kind == HostKind::Strip) {
        // At about the Tab screen's scale, not at the (smaller) host cells': the native mesh draw skips an item
        // whose texture is not resident at a small size (the elixir never appeared at the cells' scale); the
        // crops are fitted into the host cells. With the renderer's grid capture (d3d11) the grid is kept whole
        // inside the native window, which the GS front end clips (a 21:9 / 32:9 picture on a 16:9 output put the
        // plate below the picture: the dagger row, at 32:9 every icon, was cut), and the renderer removes it from
        // the presented frame (it showed over the open world where the plate does not cover it). Without the
        // capture it stays under the strip's opaque plate.
        const gameui::Rect a = gameui::StripScreen::layoutFor(vp).nativeGrid;
        if (g_overlayView.gridCapture)
            t = gridBox(a, a);
        else
            t = {true, pm.gx(a.x), pm.gy(a.y), pm.gx(a.right()), pm.gy(a.bottom())};
    } else {
        // The M view: the native map fitted into the part of the screen left of the objectives panel (inside the
        // native window, which the GS front end clips at its edges).
        const PageMap mm = tabPageMap(g_overlayView, f.w, f.h, mc.frameW, mc.frameH);
        const gameui::Rect panel = gameui::MapScreen::panelRectFor(vp);
        const float margin = 12.0f * f.h / 1080.0f, top = 40.0f * f.h / 1080.0f;
        const float x0 = std::max(mm.gx(vp.x + margin), 8.0f);
        const float x1 = std::min(mm.gx(panel.x - 2 * margin), mm.W - 8);
        const float y0 = std::max(mm.gy(vp.y + top), 8.0f), y1 = std::min(mm.gy(vp.h - top), mm.H - 8);
        if (x1 > x0 + 32 && y1 > y0 + 32)
            mt = {true, x0, y0, x1, y1};
    }
    std::lock_guard<std::mutex> l(sh.gridM);
    sh.gridTarget = t;
    sh.mapTarget = mt;
    sh.preview = p;
}

// A native help / confirmation box over the host screen: its text and buttons drawn with the host renderer when
// the EE published them (dialog.content), else the box's own pixels cropped from the frame. Native widgets keep
// the input either way.
void drawNativeDialog(ImDrawList* dl, TabUi& ui, const dsettings::SettingsOverlay::ScreenFrame& f,
                      const drakan_tab::NativeDialog& dialog, const gameui::Viewport& vp,
                      const dsettings::GameUiCanvas& canvas, const dsettings::IconSource& icons, double now) {
    const PageMap dm = tabPageMap(g_overlayView, f.w, f.h, dialog.frameW, dialog.frameH);
    // Use the rendered page's anchor: a box can be on top while the inventory remains the drawing screen.
    const float anchor = g_overlayView.hudAnchorX;
    const auto x = [&](float gx) { return dm.vx + dm.vw * (anchor + (gx / dm.W - anchor) * dm.hs); };
    const float x0 = std::clamp(x(dialog.x0), 0.0f, f.frameW);
    const float y0 = std::clamp(dm.y(dialog.y0), 0.0f, f.frameH);
    const float x1 = std::clamp(x(dialog.x1), 0.0f, f.frameW);
    const float y1 = std::clamp(dm.y(dialog.y1), 0.0f, f.frameH);
    if (x1 > x0 && y1 > y0) {
        if (dialog.content.valid) {
            auto content = dialog.content;
            for (auto& b : content.buttons) {
                const gameui::Rect r = b.rect;
                b.rect = {x(r.x), dm.y(r.y), x(r.right()) - x(r.x), dm.y(r.bottom()) - dm.y(r.y)};
            }
            const auto draw = gameui::drawDialog(content, {x0, y0, x1 - x0, y1 - y0}, vp, ui.renderer.measure());
            ui.renderer.draw(dl, draw, canvas, icons, now, 1.0f);
        } else {
            if (f.opaque)
                dl->AddCallback(f.opaque, nullptr);
            dl->AddImage(f.frame, ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(x0 / f.frameW, y0 / f.frameH),
                         ImVec2(x1 / f.frameW, y1 / f.frameH));
            if (f.opaque)
                dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
        }
    }
}
} // namespace

void tabDraw(const dsettings::SettingsOverlay::ScreenFrame& f) {
    using drakan_tab::HostKind;
    auto& sh = drakan_tab::shared();
    TabUi& ui = g_tabUi;
    const double now = ImGui::GetTime();
    const float dt = ui.lastTime < 0 ? 0.0f : std::clamp(static_cast<float>(now - ui.lastTime), 0.0f, 0.1f);
    ui.lastTime = now;
    const auto snap = sh.model.acquire();
    const bool active = sh.active.load();
    // DRAKAN_DEBUG_TAB_HIDE=1 (test aid): the host screen never draws, so the EE watchdog's native fallback runs.
    static const bool debugHide = [] {
        const char* e = std::getenv("DRAKAN_DEBUG_TAB_HIDE");
        return e && std::atoi(e) > 0;
    }();
    // Closing: the host screen stays up (last picture, no input) until screen 0x15 is gone, which the views (Tab
    // screen, strip, map) make take 2 EE frames by finishing its close animation at once (drakan_inventory_views.inl
    // hookViewDraw), then fades out quickly (~40 ms); the HUD switches in the middle of that fade (Shared::hostFade,
    // hookViewHud).
    const bool closing = sh.closing.load();
    const bool show = active && snap && snap->native.valid && !debugHide;
    // Keep the modern menu behind native help / confirmation boxes. Present their text and buttons with the menu's
    // own renderer (or reapply the native crop for unsupported controls); native widgets still own input. Until bounds
    // arrive, step aside so an unavailable crop can never cover the native buttons.
    const bool box = sh.boxUp.load();
    const drakan_tab::NativeDialog dialog = sh.nativeDialog();
    const bool compositeDialog = box && dialog.valid && f.frame && f.frameW > 0 && f.frameH > 0;
    const bool visible = show && (!box || compositeDialog);
    constexpr float fadeOut = 24.0f;
    ui.fade = visible ? std::min(1.0f, ui.fade + dt * 7.0f) : std::max(0.0f, ui.fade - dt * fadeOut);
    sh.hostFade.store(ui.fade);

    ui.renderer.setFonts(*f.fonts);
    ui.renderer.setSkin(f.skin);
    if (show && !closing && sh.session.load() != ui.session) {
        ui.session = sh.session.load();
        ui.kind = sh.kind.load();
        gameui::ScreenConfig cfg;
        cfg.measure = ui.renderer.measure();
        hostCloseScreen(ui);
        ui.screen = gameui::InventoryScreen(cfg);
        ui.strip = gameui::StripScreen(cfg);
        ui.map = gameui::MapScreen(cfg);
        hostOpenScreen(ui, *snap);
        ui.gridSerial = 0;
        ui.gridTime = -10;
        ui.mapSerial = 0;
        ui.mapTime = -10;
        ui.gate.reset(); // the key that opened the screen is held over: it counts only after its release
        // Start mostly opaque: the first frames under the screen are the native open (the page window cleared, the
        // world crop settling), which a slow fade-in would show.
        ui.fade = std::max(ui.fade, 0.6f);
        sh.hostFade.store(ui.fade);
        std::fprintf(stderr, "[tab-screen] overlay: session %u opened (host screen %d)\n", ui.session,
                     static_cast<int>(ui.kind));
    }
    while (auto r = sh.results.pop()) {
        if (r->kind == gameui::ActionKind::Close && r->outcome != gameui::Outcome::Confirmed)
            std::fprintf(stderr, "[tab-screen] overlay: close %s, screen back\n", gameui::outcomeName(r->outcome));
        if (ui.kind == HostKind::Strip)
            ui.strip.onResult(*r);
        else if (ui.kind == HostKind::Tab)
            ui.screen.onResult(*r);
    }
    if (show && !closing && snap->native.inventoryOpen && !hostOpen(ui) && !hostClosePending(ui)) {
        // Bound and not closing, but the host screen is closed (it must never stay invisible over the hidden page).
        std::fprintf(stderr, "[tab-screen] overlay: session %u reopened\n", ui.session);
        hostOpenScreen(ui, *snap);
    }
    if (ui.fade <= 0.0f) {
        if (!active && hostOpen(ui))
            hostCloseScreen(ui);
        ui.took = false;
        (void)drakan_input::module().drainHostScreenEvents();
        return;
    }

    const gameui::Viewport vp{0, 0, f.w, f.h};
    const bool take = f.input && visible && !box && !closing;
    if (take && !ui.took)
        ui.gate.reset(); // input back (F1 menu, native box, fade-in): keys held meanwhile wait for their release
    ui.took = take;
    const gameui::UiInput in = hostScreenInput(take, false, f.w, f.h);
    ui.renderer.setGamepadPrompts(ui.device == gameui::InputDevice::Gamepad);
    const drakan_tab::MapCapture mc = sh.mapCapture();
    if (visible && hostOpen(ui)) {
        gameui::FrameOutput out;
        if (ui.kind == HostKind::Strip) {
            out = ui.strip.frame(*snap, in, vp);
        } else if (ui.kind == HostKind::Map) {
            // The markers the EE drew (GUI units of the centred page) -> back-buffer pixels.
            gameui::MapInput mi;
            mi.location = sh.locationName();
            const PageMap mm = tabPageMap(g_overlayView, f.w, f.h, mc.frameW, mc.frameH);
            for (const drakan_tab::MapMarkerGui& m : sh.mapMarkers())
                mi.markers.push_back({m.quest, mm.x(m.x), mm.y(m.y)});
            out = ui.map.frame(*snap, mi, in, vp);
            if (out.wantsClose) { // the native view's own close (Select)
                gameui::UiAction close;
                close.kind = gameui::ActionKind::Close;
                out.actions.push_back(close);
            }
        } else {
            out = ui.screen.frame(*snap, in, vp);
        }
        // With a native box up, input is empty. Passive selection requests can queue here; the EE executor
        // waits for the box to close before applying them.
        for (const gameui::UiAction& a : out.actions)
            sh.actions.push(a);
        if (!box && out.sound != gameui::UiSound::None)
            sh.sound.store(0x2au); // the game's focus tick ("Next Option"), as the native menus play on a move
        if (out.wantsClose)
            sh.closing.store(true);
        else
            ui.last = std::move(out.draw);
    }

    const drakan_tab::NativeGrid ng = sh.nativeGrid();
    const PageMap pm = tabPageMap(g_overlayView, f.w, f.h, ng.frameW, ng.frameH);
    publishNativeTargets(ui, f, vp, pm, mc);
    if (mc.serial != ui.mapSerial) {
        ui.mapSerial = mc.serial;
        ui.mapTime = now;
        // On-screen aspect of the drawn map: GUI units are not square (the page is compressed by hudScaleX).
        const PageMap mm = tabPageMap(g_overlayView, f.w, f.h, mc.frameW, mc.frameH);
        const float cw = mm.x(mc.x1) - mm.x(mc.x0), ch = mm.y(mc.y1) - mm.y(mc.y0);
        ui.screen.setMapImageSize(mc.imageW, mc.imageH, ch > 0 ? cw / ch : 0.0f);
    }
    const bool frozen = closing || !active; // the frame copy is held (ScreenLayer::freezeFrame): its crops stay valid
    const bool mapFresh = (frozen && ui.mapTime > 0) || now - ui.mapTime < 0.3;
    if (ng.serial != ui.gridSerial) {
        ui.gridSerial = ng.serial;
        ui.gridTime = now;
    }
    const bool gridFresh = (frozen && ui.gridTime > 0) || now - ui.gridTime < 0.3;
    const dsettings::IconSource icons = [&](const gameui::ImageRef& ref, dsettings::IconCrop& c) {
        if (ref.kind == gameui::ImageKind::MapBackground) {
            // The native map image, drawn by the EE where the host's map area is (Map page).
            if (!mapFresh || f.frameW <= 0)
                return false;
            const PageMap mm = tabPageMap(g_overlayView, f.w, f.h, mc.frameW, mc.frameH);
            const float x0 = mm.x(mc.x0), y0 = mm.y(mc.y0), x1 = mm.x(mc.x1), y1 = mm.y(mc.y1);
            if (!(x1 > x0 && y1 > y0))
                return false;
            c.uv0 = ImVec2(x0 / f.frameW, y0 / f.frameH);
            c.uv1 = ImVec2(x1 / f.frameW, y1 / f.frameH);
            c.w = x1 - x0;
            c.h = y1 - y0;
            c.opaque = true;
            return true;
        }
        if (ref.kind != gameui::ImageKind::ItemIcon || !gridFresh || !snap || ref.index >= 24 || f.frameW <= 0)
            return false;
        const int slot = static_cast<int>(ref.index);
        const gameui::ItemEntry* it = snap->itemAtCell(slot);
        const int iw = it ? std::max(1, it->width) : 1, ih = it ? std::max(1, it->height) : 1;
        const int last = std::min(23, slot + (ih - 1) * 8 + (iw - 1));
        const drakan_tab::NativeCell& c0 = ng.cells[size_t(slot)];
        const drakan_tab::NativeCell& c1 = ng.cells[size_t(last)];
        const gsr::OverlayContext& oc = g_overlayView;
        if (ui.kind == HostKind::Strip && oc.gridSrv && oc.gridFrameW > 0 && oc.gridFrameH > 0) {
            // The renderer's off-screen copy of the grid (the presented frame no longer shows it): native target
            // pixels through the inventory page map, normalised to the copy.
            const float fw = float(oc.gridFrameW), fh = float(oc.gridFrameH);
            const float kx = ng.frameW ? fw / float(ng.frameW) : 1.0f, ky = ng.frameH ? fh / float(ng.frameH) : 1.0f;
            const float hs = oc.gridHudScale > 0.2f && oc.gridHudScale <= 1.0f ? oc.gridHudScale : 1.0f;
            const float anchor = oc.gridCentred ? 0.5f * fw : 0.0f;
            const auto u = [&](float gx) { return (anchor + (gx * kx - anchor) * hs) / fw; };
            const float u0 = u(c0.x0), u1 = u(c1.x1), v0 = c0.y0 * ky / fh, v1 = c1.y1 * ky / fh;
            if (!(u1 > u0 && v1 > v0))
                return false;
            c.uv0 = ImVec2(u0, v0);
            c.uv1 = ImVec2(u1, v1);
            // Its on-screen size for the aspect fit: the same cells through the presented page.
            c.w = pm.x(c1.x1) - pm.x(c0.x0);
            c.h = pm.y(c1.y1) - pm.y(c0.y0);
            c.texture = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(oc.gridSrv));
            return c.w > 0 && c.h > 0;
        }
        if (ui.kind == HostKind::Strip && oc.gridCapture)
            return false; // not captured yet: the frame copy does not hold the grid
        const float x0 = pm.x(c0.x0), y0 = pm.y(c0.y0), x1 = pm.x(c1.x1), y1 = pm.y(c1.y1);
        if (!(x1 > x0 && y1 > y0))
            return false;
        c.uv0 = ImVec2(x0 / f.frameW, y0 / f.frameH);
        c.uv1 = ImVec2(x1 / f.frameW, y1 / f.frameH);
        c.w = x1 - x0;
        c.h = y1 - y0;
        return true;
    };
    dsettings::GameUiCanvas canvas;
    canvas.frame = f.frame;
    canvas.frameW = f.frameW;
    canvas.frameH = f.frameH;
    canvas.additive = f.additive;
    canvas.opaque = f.opaque;
    // DRAKAN_DEBUG_TAB_PEEK=1 (test aid): the host screen runs (input, grid / map targets, watchdog) but draws nothing,
    // so screenshots show the native frame its icon and map crops are taken from.
    static const bool debugPeek = [] {
        const char* e = std::getenv("DRAKAN_DEBUG_TAB_PEEK");
        return e && std::atoi(e) > 0;
    }();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!debugPeek)
        ui.renderer.draw(dl, ui.last, canvas, icons, now, ui.fade);
    if (!debugPeek && compositeDialog)
        drawNativeDialog(dl, ui, f, dialog, vp, canvas, icons, now);
    if (visible && ui.last.size() > 0)
        sh.hostDrawn.fetch_add(1); // the EE watchdog (drakan_tab_screen.inl tabWatchdog)

    if (in.pointerValid && ui.device == gameui::InputDevice::KeyboardMouse && f.input && !sh.boxUp.load())
        dsettings::drawMenuCursor(in.pointerX, in.pointerY, std::clamp(f.h / 1080.0f, 0.62f, 3.0f));
}
} // namespace drakan_settings_host::detail
#endif // PS2X_HAVE_DRAKAN_SETTINGS
