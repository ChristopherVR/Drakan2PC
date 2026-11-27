// Drakan: the modern Esc (pause) menu (gameui PauseScreen), drawn by the overlay's screen layer. EE side:
// drakan_pause_screen.inl.
#if defined(PS2X_HAVE_DRAKAN_SETTINGS)
#include "drakan_settings_host_internal.h"

namespace drakan_settings_host::detail {

namespace {
// ---- the modern Esc (pause) menu (gameui PauseScreen; EE side drakan_pause_screen.inl) ----
struct PauseUi {
    gameui::PauseScreen screen;
    gameui::DrawList last;
    uint32_t session = 0, reactivated = 0;
    float fade = 0;
    double lastTime = -1;
};
PauseUi g_pauseUi;
} // namespace

bool pauseShown() {
    return drakan_pause_screen::shared().active.load() || g_pauseUi.fade > 0.0f;
}

// `background`: another host screen is on top this frame (the modern Save / Load screen over the pause or the title):
// this one only fades and draws, it reads no input (the events are the top screen's).
void pauseDraw(const dsettings::SettingsOverlay::ScreenFrame& f, bool background) {
    auto& sh = drakan_pause_screen::shared();
    PauseUi& ui = g_pauseUi;
    TabUi& tu = g_tabUi; // shared renderer, key gate and device
    const double now = ImGui::GetTime();
    const float dt = ui.lastTime < 0 ? 0.0f : std::clamp(static_cast<float>(now - ui.lastTime), 0.0f, 0.1f);
    ui.lastTime = now;
    const auto snap = sh.model.acquire();
    const bool active = sh.active.load();
    // DRAKAN_DEBUG_PAUSE_HIDE (test aid): 1 = the host pause never draws, so the EE watchdog's native fallback runs;
    // 2 = it stops drawing once a host page (Options, Audio, Controller) is open, i.e. while an owned native options
    // screen is up under it.
    static const int debugHide = [] {
        const char* e = std::getenv("DRAKAN_DEBUG_PAUSE_HIDE");
        return e ? std::atoi(e) : 0;
    }();
    const bool child = sh.childUp.load(), resuming = sh.resuming.load();
    const bool hidden = debugHide == 1 || (debugHide == 2 && ui.session == sh.session.load() && ui.screen.isOpen() &&
                                           ui.screen.page() != gameui::PausePage::Root);
    const bool show = active && snap && !hidden;
    const bool visible = show && !child && !resuming;
    ui.fade = visible ? std::min(1.0f, ui.fade + dt * 8.0f) : std::max(0.0f, ui.fade - dt * 10.0f);
    sh.hostFade.store(ui.fade);
    tu.renderer.setFonts(*f.fonts);
    tu.renderer.setSkin(f.skin);
    if (show && sh.session.load() != ui.session) {
        ui.session = sh.session.load();
        ui.reactivated = sh.reactivated.load();
        gameui::PauseConfig cfg;
        cfg.screen.measure = tu.renderer.measure();
        // Stage 2 / 3: host Options / Audio / Controller pages over the hidden native screens 9 / 13 / 10, the host
        // Default Settings and Quit cards (drakan_pause_screen.inl executes their actions).
        cfg.hostPages = true;
        cfg.quitCard = true;
        // One Options screen: the PC settings pages with the game options folded in (F1 stays the overlay).
        cfg.pcSettingsPages = cfg.unifiedOptions = g_pcs != nullptr;
        ui.screen = gameui::PauseScreen(cfg);
        ui.screen.open();
        tu.gate.reset(); // Esc / START that opened the pause count only after their release
        ui.fade = std::max(ui.fade, 0.5f);
        std::fprintf(stderr, "[pause-screen] overlay: session %u opened\n", ui.session);
    }
    if (const uint32_t r = sh.reactivated.load(); r != ui.reactivated) {
        ui.reactivated = r;
        ui.screen.reactivate(); // back from a native child (or its row was refused)
    }
    if (show && !resuming && !ui.screen.isOpen()) {
        std::fprintf(stderr, "[pause-screen] overlay: session %u reopened\n", ui.session);
        ui.screen.open();
    }
    if (ui.fade <= 0.0f) {
        if (!active && ui.screen.isOpen())
            ui.screen.close();
        if (!background) {
            tu.took = false;
            (void)drakan_input::module().drainHostScreenEvents();
        }
        return;
    }
    updateMapPreviews(); // the help panel's setting pictures (the pause menu shows the PC Settings pages too)
    const gameui::Viewport vp{0, 0, f.w, f.h};
    const bool take = !background && f.input && visible;
    const bool capture = ui.screen.pcSettingsOpen() && ui.screen.pcSettings().capturing();
    gameui::UiInput in;
    if (!background) {
        if (take && !tu.took)
            tu.gate.reset(); // input back (F1 menu, native child closed): held keys wait for their release
        tu.took = take;
        in = hostScreenInput(take, capture, f.w, f.h);
    }
    tu.renderer.setGamepadPrompts(tu.device == gameui::InputDevice::Gamepad);
    if (visible && ui.screen.isOpen() && snap) {
        gameui::PauseState st = sh.pauseState();
        st.pcSettings = pcSettingsSchema(ui.screen.pcSettingsOpen(), now);
        st.now = now;
        st.capturedKey = take && capture ? g_capturedKey : std::string();
        ui.screen.setIssueReportingVisible(CommonSettingsHost::issueReportingVisible());
        gameui::PauseFrameOutput out = ui.screen.frame(*snap, st, in, vp);
        for (const gameui::PauseAction& a : out.actions) {
            if (a.kind == gameui::PauseActionKind::ReportIssue)
                drakan_issue::report("Pause menu", drakan_game::currentName(),
                                     gameui::locationName(*snap, st.location));
            else
                sh.actions.push(a);
        }
        hostUiSound(out.sound, nativeSound(out.actions));
        if (out.wantsResume)
            sh.resuming.store(true);
        if (g_pcs && !out.settings.empty())
            g_pcs->applyAll(out.settings, ui.screen.pcSettings());
        ui.last = std::move(out.draw);
    }
    dsettings::GameUiCanvas canvas;
    canvas.frame = f.frame;
    canvas.frameW = f.frameW;
    canvas.frameH = f.frameH;
    canvas.additive = f.additive;
    canvas.opaque = f.opaque;
    const dsettings::IconSource pics = [](const gameui::ImageRef& ref, dsettings::IconCrop& c) {
        return ref.kind == gameui::ImageKind::LevelPreview && previewCrop(ref, c); // a mod pack's picture
    };
    tu.renderer.draw(ImGui::GetBackgroundDrawList(), ui.last, canvas, pics, now, ui.fade);
    if (visible && ui.last.size() > 0)
        sh.hostDrawn.fetch_add(1); // the EE watchdog (drakan_pause_screen.inl pauseTick)
    if (in.pointerValid && tu.device == gameui::InputDevice::KeyboardMouse && f.input && visible)
        dsettings::drawMenuCursor(in.pointerX, in.pointerY, std::clamp(f.h / 1080.0f, 0.62f, 3.0f));
}
} // namespace drakan_settings_host::detail
#endif // PS2X_HAVE_DRAKAN_SETTINGS
