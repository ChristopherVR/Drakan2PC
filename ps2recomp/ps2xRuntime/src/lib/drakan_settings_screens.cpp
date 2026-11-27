// Drakan: the overlay's screen layer: which host screen draws this frame (hostScreenDraw) and what the host screens
// share (the modern PC Settings pages, menu sounds, font sizes, drawing a gameui::DrawList).
#if defined(PS2X_HAVE_DRAKAN_SETTINGS)
#include "drakan_settings_host_internal.h"

namespace drakan_settings_host::detail {

// ---- the modern PC Settings pages (inside the modern pause / title menus): dsettings::ModernSettings shares the F1
// overlay's host and store (runtime/gameui/README.md, "PC Settings glue contract") ----
std::unique_ptr<dsettings::ModernSettings> g_pcs;

namespace {
bool g_pcsOpen = false, g_pcsOverlayOpen = false;
} // namespace

// The schema for this frame's PauseState / TitleState (null without the pages): the files are read again when the
// pages open and after the F1 overlay was closed over them (it may have saved).
const gameui::SettingsSchema* pcSettingsSchema(bool pagesOpen, double now) {
    if (!g_pcs)
        return nullptr;
    const bool overlay = g_overlay && g_overlay->isOpen();
    if (pagesOpen && (!g_pcsOpen || (g_pcsOverlayOpen && !overlay)))
        g_pcs->open();
    g_pcsOpen = pagesOpen;
    g_pcsOverlayOpen = overlay;
    g_pcs->tick(now);
    return &g_pcs->schema();
}

dsettings::ScreenFontSizes tabFontSizes(float w, float h) {
    const gameui::LayoutFrame lf = gameui::computeFrame({0, 0, w, h});
    const gameui::Theme th;
    dsettings::ScreenFontSizes s;
    const float design[] = {th.sizeBody, th.sizeHeading, th.sizeTitle, th.sizeNumeric, th.sizeSmall};
    for (int i = 0; i < dsettings::UiFonts::kScreenFonts; ++i)
        s.px[i] = std::round(design[i] * lf.scale);
    return s;
}

// Feedback sounds of the host-drawn menus (title, pause, Options pages, Save / Load) through the game's own UI sounds
// (drakan_menu_screens::Shared::uiSound, played by the EE glue). A frame whose actions make the glue press or tap a
// native widget leaves the sound to that widget (its click, its check box, START's back sound), so nothing plays
// twice.
void hostUiSound(gameui::UiSound s, bool nativeSound) {
    using S = gameui::UiSound;
    if (s == S::None || nativeSound)
        return;
    const uint32_t property = s == S::Confirm  ? 0x24u
                              : s == S::Back   ? 0x2du
                              : s == S::Slider ? 0x27u
                              : s == S::Toggle ? 0x28u
                                               : 0x2au; // Move, Page: the focus tick
    drakan_menu_screens::shared().uiSound.store(property);
}
// Pause / Options actions the glue carries out with a native press or tap (drakan_pause_screen.inl).
bool nativeSound(const std::vector<gameui::PauseAction>& actions) {
    using K = gameui::PauseActionKind;
    for (const gameui::PauseAction& a : actions)
        switch (a.kind) {
            case K::Resume:
            case K::OpenSave:
            case K::OpenLoad:
            case K::OpenOptions:
            case K::Quit:
            case K::OpenController:
            case K::OpenAudio:
            case K::DefaultSettings:
            case K::EnterPage:
            case K::LeavePage:
            case K::SetSubtitles:
            case K::SetStereo:
            case K::SetVibration:
            case K::SetReversePitch:
            case K::SetControllerScheme:
            case K::RestoreDefaults:
            case K::QuitToTitle:
                return true;
            default:
                break;
        }
    return false;
}

void drawHostList(const dsettings::SettingsOverlay::ScreenFrame& f, const gameui::DrawList& list, double now,
                  float fade, const dsettings::IconSource& icons) {
    dsettings::GameUiCanvas canvas;
    canvas.frame = f.frame;
    canvas.frameW = f.frameW;
    canvas.frameH = f.frameH;
    canvas.additive = f.additive;
    canvas.opaque = f.opaque;
    g_tabUi.renderer.draw(ImGui::GetBackgroundDrawList(), list, canvas, icons, now, fade);
}

// The overlay has one screen layer. The modern Save / Load screen is drawn over its parent (the pause or the title,
// fading out under it), else the title, the pause menu, or the inventory hosts.
void hostScreenDraw(const dsettings::SettingsOverlay::ScreenFrame& f) {
    if (const int quick = drakan_menu_screens::shared().quickOperation.load()) {
        // Keep the native pause, scan and card screens behind one progress card, from the shortcut until gameplay.
        auto& ui = g_tabUi;
        ui.renderer.setFonts(*f.fonts);
        ui.renderer.setSkin(f.skin);
        gameui::SaveLoadConfig cfg;
        cfg.screen.measure = ui.renderer.measure();
        gameui::SaveLoadScreen progress(cfg);
        progress.open(quick == 1 ? gameui::SaveLoadMode::Save : gameui::SaveLoadMode::Load,
                      gameui::SaveLoadContext::Pause);
        gameui::SaveSlotsState st;
        st.quickOperation = quick;
        st.busy = true;
        const auto out = progress.frame(st, {}, {0, 0, f.w, f.h});
        drawHostList(f, out.draw, ImGui::GetTime(), 1.0f, {});
        drakan_menu_screens::shared().slotsDrawn.fetch_add(1);
        drakan_pause_screen::shared().hostDrawn.fetch_add(1);
        return;
    }
    { // Test aid: DRAKAN_MENU_SHOTS=<ms> writes a window shot every <ms> while a title / Save / Load screen is up
      // and for 4 s after (<DRAKAN_SHOT_DIR>/ms_<milliseconds of the log clock>.png): frame series of the menus.
        static const int every = [] {
            const char* e = std::getenv("DRAKAN_MENU_SHOTS");
            return e ? std::atoi(e) : 0;
        }();
        static double next = 0, until = 0;
        if (every > 0) {
            const double t = drakan_menu_screens::clockSeconds();
            if (slotsShown() || titleShown())
                until = t + 4.0;
            if (t < until && t >= next) {
                next = t + every / 1000.0;
                const char* dir = std::getenv("DRAKAN_SHOT_DIR");
                char nm[600];
                std::snprintf(nm, sizeof nm, "%s/ms_%07.0f.png", dir ? dir : ".", t * 1000.0);
                gsr::requestWindowShot(nm);
            }
        }
    }
    if (switchButtonShown() && !titleShown() && !slotsShown() && !pauseShown())
        switchButtonDraw(f);
    if (slotsShown()) {
        if (pauseShown())
            pauseDraw(f, true);
        else if (titleShown())
            titleDraw(f, true);
        slotsDraw(f);
    } else if (titleShown()) {
        titleDraw(f);
    } else if (pauseShown()) {
        pauseDraw(f);
    } else if (drakan_tab::shared().active.load() || g_tabUi.fade > 0.0f) {
        tabDraw(f);
    } else if (mpHudActive()) {
        mpHudDraw(f);
    } else {
        tabDraw(f);
    }
}
} // namespace drakan_settings_host::detail
#endif // PS2X_HAVE_DRAKAN_SETTINGS
