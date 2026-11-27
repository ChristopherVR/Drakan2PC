// Drakan: the modern Save / Load screen (gameui SaveLoadScreen), drawn over its parent (the pause or the title
// menu). EE side: drakan_menu_screens.inl.
#if defined(PS2X_HAVE_DRAKAN_SETTINGS)
#include "drakan_settings_host_internal.h"

namespace drakan_settings_host::detail {

namespace {
struct SlotsUi {
    gameui::SaveLoadScreen screen;
    gameui::DrawList last;
    uint32_t session = 0, reactivated = 0;
    float fade = 0;
    double lastTime = -1;
};
SlotsUi g_slotsUi;
} // namespace

bool slotsShown() {
    return drakan_menu_screens::shared().slotsActive.load() || g_slotsUi.fade > 0.0f;
}

void slotsDraw(const dsettings::SettingsOverlay::ScreenFrame& f) {
    auto& sh = drakan_menu_screens::shared();
    SlotsUi& ui = g_slotsUi;
    TabUi& tu = g_tabUi;
    const double now = ImGui::GetTime();
    const float dt = ui.lastTime < 0 ? 0.0f : std::clamp(static_cast<float>(now - ui.lastTime), 0.0f, 0.1f);
    ui.lastTime = now;
    // DRAKAN_DEBUG_SLOTS_HIDE=1 (test aid): the host slot screen never draws, so the EE watchdog's fallback runs.
    static const bool debugHide = [] {
        const char* e = std::getenv("DRAKAN_DEBUG_SLOTS_HIDE");
        return e && std::atoi(e) > 0;
    }();
    const bool active = sh.slotsActive.load(), leaving = sh.slotsLeaving.load();
    const bool show = active && !debugHide;
    const bool visible = show && !sh.slotsChild.load() && !leaving;
    ui.fade = visible ? std::min(1.0f, ui.fade + dt * 8.0f) : std::max(0.0f, ui.fade - dt * 10.0f);
    tu.renderer.setFonts(*f.fonts);
    tu.renderer.setSkin(f.skin);
    if (show && sh.slotsSession.load() != ui.session) {
        ui.session = sh.slotsSession.load();
        ui.reactivated = sh.slotsReactivated.load();
        gameui::SaveLoadConfig cfg;
        cfg.screen.measure = tu.renderer.measure();
        ui.screen = gameui::SaveLoadScreen(cfg);
        gameui::SaveLoadMode mode;
        gameui::SaveLoadContext ctx;
        {
            std::lock_guard<std::mutex> l(sh.m);
            mode = sh.mode;
            ctx = sh.context;
        }
        ui.screen.open(mode, ctx);
        tu.gate.reset();
        ui.fade = std::max(ui.fade, 0.5f);
        std::fprintf(stderr, "[menu-screens] overlay: slot session %u opened\n", ui.session);
    }
    if (const uint32_t r = sh.slotsReactivated.load(); r != ui.reactivated) {
        ui.reactivated = r;
        if (!ui.screen.isOpen()) { // Back was not taken: the screen again
            std::lock_guard<std::mutex> l(sh.m);
            ui.screen.open(sh.mode, sh.context);
        }
        ui.screen.reactivate();
    }
    if (ui.fade <= 0.0f) {
        if (!active && ui.screen.isOpen())
            ui.screen.close();
        tu.took = false;
        (void)drakan_input::module().drainHostScreenEvents();
        return;
    }
    updatePreviewTextures();
    const gameui::Viewport vp{0, 0, f.w, f.h};
    const bool take = f.input && visible;
    if (take && !tu.took)
        tu.gate.reset();
    tu.took = take;
    const gameui::UiInput in = hostScreenInput(take, false, f.w, f.h);
    tu.renderer.setGamepadPrompts(tu.device == gameui::InputDevice::Gamepad);
    if (visible && ui.screen.isOpen()) {
        const gameui::SaveSlotsState st = sh.slotsState();
        gameui::SaveLoadFrameOutput out = ui.screen.frame(st, in, vp);
        for (const gameui::SaveLoadAction& a : out.actions) {
            std::fprintf(stderr, "[menu-screens] overlay t=%.3f: slot action %s(%d)\n",
                         drakan_menu_screens::clockSeconds(), gameui::saveLoadActionName(a.kind), a.value);
            sh.slotActions.push(a);
        }
        if (out.wantsBack)
            sh.slotsLeaving.store(true);
        hostUiSound(out.sound, out.wantsBack); // Back is START natively: the screen's own back sound
        ui.last = std::move(out.draw);
    }
    drawHostList(f, ui.last, now, ui.fade, &previewCrop);
    if (visible && ui.last.size() > 0)
        sh.slotsDrawn.fetch_add(1); // the EE watchdog (drakan_menu_screens.inl slotsTick)
    if (in.pointerValid && tu.device == gameui::InputDevice::KeyboardMouse && f.input && visible)
        dsettings::drawMenuCursor(in.pointerX, in.pointerY, std::clamp(f.h / 1080.0f, 0.62f, 3.0f));
}
} // namespace drakan_settings_host::detail
#endif // PS2X_HAVE_DRAKAN_SETTINGS
