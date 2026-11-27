// Drakan: the modern title menu (gameui TitleScreen, EE side drakan_menu_screens.inl), the Switch Campaigns button
// over the Original title menu, and the title backdrop picture of the chosen game.
#if defined(PS2X_HAVE_DRAKAN_SETTINGS)
#include "drakan_settings_host_internal.h"

#if defined(_WIN32)
#include "drakan_update.h"
#include "drakan_update_installer_win.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__) && !defined(__EMSCRIPTEN__)
#include "drakan_update_linux.h"
#endif

namespace drakan_settings_host::detail {

namespace {
struct TitleUi {
    gameui::TitleScreen screen;
    gameui::DrawList last;
    uint32_t session = 0, reactivated = 0;
    float fade = 0;
    double lastTime = -1;
    const gameui::UiSkin* skin = nullptr; // the skin the screen was built with (a campaign switch replaces it)
};
TitleUi g_titleUi;

bool g_modsNoticeDone = false; // it was shown and answered / closed (overlay thread)

// The update notice of the title's main page: the installer (Windows) or drakan_update_linux. True when an update
// is being installed and the game has been asked to close.
bool pollTitleUpdate(gameui::TitleState& st) {
#if defined(_WIN32)
    using drakan_update_installer::State;
    const State installer = drakan_update_installer::poll();
    if (installer == State::Launched) {
        drakan_win::requestClose();
        return true;
    }
    st.updateVersion = drakan_update::availableVersion();
    if (!st.updateVersion.empty() && drakan_update_installer::eligible()) {
        st.updateInstall = gameui::TitleUpdateInstall::Available;
        if (installer == State::Downloading)
            st.updateInstall = gameui::TitleUpdateInstall::Downloading;
        else if (installer == State::Failed)
            st.updateInstall = gameui::TitleUpdateInstall::Failed;
    }
#elif defined(__linux__) && !defined(__EMSCRIPTEN__)
    using drakan_update_linux::Install;
    const auto notice = drakan_update_linux::poll();
    if (notice.install == Install::Ready) {
        drakan_win::requestClose();
        return true;
    }
    st.updateVersion = notice.version;
    if (notice.install == Install::Available)
        st.updateInstall = gameui::TitleUpdateInstall::Available;
    else if (notice.install == Install::Downloading)
        st.updateInstall = gameui::TitleUpdateInstall::Downloading;
    else if (notice.install == Install::Failed)
        st.updateInstall = gameui::TitleUpdateInstall::Failed;
#else
    (void)st;
#endif
    return false;
}

// A game's cover: previews/campaign-<id>.png (a mod pack's campaign, or a pack / the player replacing the
// cover of "d2" = The Ancients' Gates, "ootf" = Order of the Flame), else previews/game-<id>.png (the logo
// picture the player drops in for a built-in game). Nothing from the disc is stored in the repository.
void setGamePictures(gameui::TitleState& st, float viewW, float viewH) {
    for (gameui::TitleState::Game& g : st.games) {
        uint32_t pic = mapPreviewId("campaign-" + g.id);
        if (pic == 0)
            pic = mapPreviewId("game-" + g.id);
        if (pic != 0)
            g.cover = {gameui::ImageKind::LevelPreview, pic};
        // The classic game choice's full-screen background: previews/background-<id>.png (a pack's, or the player's
        // picture of "d2" / "ootf" next to the exe); none = plain black. The disc game shows the picture its title
        // menu shows, the texture pack's wide title art, and keeps background-d2.png (the 1280 x 720 cover, soft
        // on a large screen) for a pack without one or a file that does not decode.
        uint32_t bg = mapPreviewId("background-" + g.id);
        if (g.id == "d2") {
            const uint32_t art = titleArtPictureId(viewH > 0 ? viewW / viewH : 16.0f / 9);
            if (drakan_title_picture::choose(pictureLoad(art), pictureLoad(bg)) ==
                drakan_title_picture::Source::TitleArt)
                bg = art;
        }
        if (bg != 0)
            g.background = {gameui::ImageKind::LevelPreview, bg};
    }
}

// Why a joined session ended (host left / connection lost): shown on the main page for 12 s, dropped once
// the player opens the Multiplayer page (its Join screen carries the same text).
void setConnectionNote(gameui::TitleState& st, bool multiplayerPage, double now) {
    static double s_noticeSince = -1;
    const std::string& en = st.multiplayer.endNotice;
    if (!en.empty() && multiplayerPage) {
        drakan_netplay::dismissNotice();
        s_noticeSince = -1;
    } else if (!en.empty()) {
        if (s_noticeSince < 0)
            s_noticeSince = now;
        if (now - s_noticeSince > 12.0) {
            drakan_netplay::dismissNotice();
            s_noticeSince = -1;
        } else {
            st.connectionNote = en;
        }
    } else {
        s_noticeSince = -1;
    }
}

// The update card's buttons (none pressed: nothing).
void runTitleUpdateAction(const std::optional<gameui::TitleUpdateAction>& action, const std::string& version) {
#if defined(_WIN32)
    if (action == gameui::TitleUpdateAction::ViewChanges)
        drakan_update::viewChanges();
    else if (action == gameui::TitleUpdateAction::Dismiss)
        drakan_update::dismiss();
    else if (action == gameui::TitleUpdateAction::DownloadAndRebuild)
        drakan_update_installer::start(version);
#elif defined(__linux__) && !defined(__EMSCRIPTEN__)
    (void)version;
    if (action == gameui::TitleUpdateAction::ViewChanges)
        drakan_update_linux::viewChanges();
    else if (action == gameui::TitleUpdateAction::Dismiss)
        drakan_update_linux::dismiss();
    else if (action == gameui::TitleUpdateAction::DownloadAndRebuild)
        drakan_update_linux::start();
#else
    (void)action;
    (void)version;
#endif
}

// Hands this frame's actions on: the EE glue carries out the title / option actions (drakan_menu_screens.inl), the
// netplay API the multiplayer ones; the host handles the mod packs card itself. Then the frame's menu sound.
void dispatchTitleActions(const gameui::TitleFrameOutput& out, gameui::TitleScreen& screen) {
    auto& sh = drakan_menu_screens::shared();
    for (const gameui::TitleAction& a : out.actions) {
        std::fprintf(stderr, "[menu-screens] overlay t=%.3f: title action %s(%d)\n",
                     drakan_menu_screens::clockSeconds(), gameui::titleActionName(a.kind), a.value);
        if (a.kind == gameui::TitleActionKind::ReportIssue)
            drakan_issue::report("Title menu", drakan_game::currentName());
        else if (a.kind == gameui::TitleActionKind::ModsEnable || a.kind == gameui::TitleActionKind::ModsKeepOff ||
                 a.kind == gameui::TitleActionKind::OpenMods)
            titleModsAction(a); // host only: no native press
        else {
            if (a.kind == gameui::TitleActionKind::SelectGame)
                previewTitleBackdrop(a.value); // this thread closes the chooser now; the EE applies the choice later
            sh.titleActions.push(a);
        }
    }
    for (const gameui::PauseAction& a : out.optionActions)
        sh.titleOptionActions.push(a);
    for (const gameui::mp::MpAction& a : out.mpActions)
        mpApply(a, screen.multiplayer());
    { // A match started: begin a New Game with the match level standing in for the first one (the intro is skipped
        // by START taps, drakan_input_glue.cpp).
        std::string matchLevel;
        if (drakan_netplay::takeLaunchRequest(matchLevel)) {
            std::fprintf(stderr, "[menu-screens] multiplayer launch: New Game with '%s'\n", matchLevel.c_str());
            sh.titleActions.push({0, gameui::TitleActionKind::NewGame, 0});
        } else if (drakan_playtest::takeTitleNewGame()) { // the editor's Play (DRAKAN_FAST_START)
            std::fprintf(stderr, "[menu-screens] play test: New Game at once\n");
            sh.titleActions.push({0, gameui::TitleActionKind::NewGame, 0});
        }
    }
    // Actions that press a native row use its own click sound.
    bool titleNative = nativeSound(out.optionActions);
    for (const gameui::TitleAction& a : out.actions)
        titleNative =
            titleNative ||
            (a.kind != gameui::TitleActionKind::QuitToDesktop && a.kind != gameui::TitleActionKind::SelectGame &&
             a.kind != gameui::TitleActionKind::ModsEnable && a.kind != gameui::TitleActionKind::ModsKeepOff &&
             a.kind != gameui::TitleActionKind::OpenMods && a.kind != gameui::TitleActionKind::ReportIssue);
    hostUiSound(out.sound, titleNative);
}
} // namespace

bool titleShown() {
    return drakan_menu_screens::shared().titleActive.load() || g_titleUi.fade > 0.0f;
}

void titleDraw(const dsettings::SettingsOverlay::ScreenFrame& f, bool background) {
    auto& sh = drakan_menu_screens::shared();
    TitleUi& ui = g_titleUi;
    TabUi& tu = g_tabUi;
    const double now = ImGui::GetTime();
    const float dt = ui.lastTime < 0 ? 0.0f : std::clamp(static_cast<float>(now - ui.lastTime), 0.0f, 0.1f);
    ui.lastTime = now;
    // DRAKAN_DEBUG_TITLE_HIDE=1 (test aid): the host title never draws, so the EE watchdog's native fallback runs.
    static const bool debugHide = [] {
        const char* e = std::getenv("DRAKAN_DEBUG_TITLE_HIDE");
        return e && std::atoi(e) > 0;
    }();
    const bool active = sh.titleActive.load();
    const bool show = active && !debugHide;
    const bool visible = show && !sh.titleChild.load() && !sh.titleCovered.load();
    ui.fade = visible ? std::min(1.0f, ui.fade + dt * 8.0f) : std::max(0.0f, ui.fade - dt * 10.0f);
    tu.renderer.setFonts(*f.fonts);
    tu.renderer.setSkin(f.skin);
    // DRAKAN_DEBUG_TITLE_PC_PAGES=0 (test aid): the title behaves as if the modern PC Settings pages were missing, so
    // its PC Settings and Open Mods menu rows take the F1 overlay fallback (the pages always exist in this build).
    static const bool debugNoPages = [] {
        const char* e = std::getenv("DRAKAN_DEBUG_TITLE_PC_PAGES");
        return e && e[0] == '0';
    }();
    const auto titleConfig = [&tu] {
        gameui::TitleConfig cfg; // its Theme reads the active skin
        cfg.screen.measure = tu.renderer.measure();
        cfg.pcSettingsPages = cfg.unifiedOptions = g_pcs != nullptr && !debugNoPages;
        cfg.showMultiplayer = drakan_netplay::wanted();
        cfg.showSwitchCampaigns = true;     // shown only while more than one game is installed
        cfg.showCoop = cfg.showMultiplayer; // New Game asks Solo / Co-op (Co-op is not in the Multiplayer menu)
        return cfg;
    };
    if (show && sh.titleSession.load() == ui.session && ui.skin != &gameui::activeUiSkin() &&
        ui.screen.page() == gameui::TitlePage::Main) {
        // The game card switched to a campaign with its own menu skin (or back): the theme colours are read when the
        // screen is built, so build it again, on the main page (the choice was just made).
        ui.skin = &gameui::activeUiSkin();
        ui.screen = gameui::TitleScreen(titleConfig());
        ui.screen.open();
        ui.screen.skipGameChoice();
        std::fprintf(stderr, "[menu-screens] title: rebuilt for the menu skin of %s\n",
                     drakan_game::currentName().c_str());
    }
    if (show && sh.titleSession.load() != ui.session) {
        ui.session = sh.titleSession.load();
        ui.reactivated = sh.titleReactivated.load();
        ui.skin = &gameui::activeUiSkin();
        ui.screen = gameui::TitleScreen(titleConfig());
        ui.screen.open();
        { // Back from a match with the session still up: the lobby page again (the next match starts from there).
            const drakan_netplay::State ns = drakan_netplay::state();
            // (Not while a match is loading or running: the shortcut launches, DRAKAN_NET_HOST / _CONNECT, bring the
            // title up under a live session and the lobby page would sit over the level, holding the world.)
            const bool matchOver =
                ns.phase == drakan_netplay::Phase::Lobby || ns.phase == drakan_netplay::Phase::Results;
            if (drakan_netplay::policy::resumeLobby(ns.inSession, ns.conn == drakan_netplay::Conn::Rejected,
                                                    ns.conn == drakan_netplay::Conn::Connected, matchOver)) {
                // A lost session returns here with its error retained. Reopen the lobby so its rejection path
                // shows the reason and issues Leave, completing network/launch/co-op cleanup before another join.
                ui.screen.resumeMultiplayer(mpState(false));
                std::fprintf(stderr, "[menu-screens] title: resumed the multiplayer lobby\n");
            }
        }
        tu.gate.reset();
        ui.fade = std::max(ui.fade, 0.5f);
        std::fprintf(stderr, "[menu-screens] overlay: title session %u opened\n", ui.session);
    }
    if (const uint32_t r = sh.titleReactivated.load(); r != ui.reactivated) {
        ui.reactivated = r;
        ui.screen.reactivate();
    }
    if (show && !ui.screen.isOpen())
        ui.screen.open();
    if (ui.fade <= 0.0f) {
        if (!active && ui.screen.isOpen())
            ui.screen.close();
        if (!background) {
            tu.took = false;
            (void)drakan_input::module().drainHostScreenEvents();
        }
        return;
    }
    const gameui::Viewport vp{0, 0, f.w, f.h};
    const bool take = !background && f.input && visible;
    const bool capture = ui.screen.pcSettingsOpen() && ui.screen.pcSettings().capturing();
    gameui::UiInput in;
    if (!background) {
        if (take && !tu.took)
            tu.gate.reset();
        tu.took = take;
        in = hostScreenInput(take, capture, f.w, f.h, ui.screen.wantsText());
    }
    tu.renderer.setGamepadPrompts(tu.device == gameui::InputDevice::Gamepad);
    if (visible && ui.screen.isOpen()) {
        gameui::TitleState st = sh.titleState();
        if (!background && !st.classicGameChoice && ui.screen.page() == gameui::TitlePage::Main && pollTitleUpdate(st))
            return; // the game closes for the update: nothing more to draw
        setGamePictures(st, f.w, f.h);
        st.multiplayer = mpState(ui.screen.page() == gameui::TitlePage::Multiplayer &&
                                 ui.screen.multiplayer().page() == gameui::mp::MpPage::Join);
        st.pcSettings = pcSettingsSchema(ui.screen.pcSettingsOpen(), now);
        if (modsNoticeWanted() && !g_modsNoticeDone) { // the new packs card: once per run
            const dsettings::ModNoticeContent mc = dsettings::modNoticeContent(harnessMods());
            st.newMods = mc.names;
            st.newModCampaigns = mc.campaigns;
        }
        st.gameChoiceNote =
            dsettings::gameChoiceNoteText(dsettings::gameChoiceNote(harnessMods(), harnessMods().gameChoice()), true);
        st.modsNote = dsettings::unreadablePacksText(harnessMods(), true);
        st.missingModsNote = dsettings::missingPacksText(harnessMods(), true);
        setConnectionNote(st, ui.screen.page() == gameui::TitlePage::Multiplayer, now);
        if ((!st.gameChoiceNote.empty() || !st.modsNote.empty() || !st.missingModsNote.empty() ||
             !st.connectionNote.empty()) &&
            ui.screen.page() == gameui::TitlePage::Main)
            drakan_fpsmeter::setAvoidTop(true); // the note plate sits in the top left corner
        st.now = now;
        st.capturedKey = take && capture ? capturedKey() : std::string();
        const bool noticeBefore = ui.screen.page() == gameui::TitlePage::ModsNotice;
        ui.screen.setIssueReportingVisible(CommonSettingsHost::issueReportingVisible());
        gameui::TitleFrameOutput out = ui.screen.frame(st, in, vp);
        runTitleUpdateAction(out.updateAction, st.updateVersion);
        if (noticeBefore && ui.screen.page() != gameui::TitlePage::ModsNotice)
            g_modsNoticeDone = true; // answered or closed (Esc: asked again on the next start, mods.ini keeps `new`)
        if (ui.screen.page() == gameui::TitlePage::Multiplayer)
            drakan_fpsmeter::setAvoidTop(true); // the multiplayer pages use the top of the window for their titles
        dispatchTitleActions(out, ui.screen);
        if (g_pcs && !out.settings.empty())
            g_pcs->applyAll(out.settings, ui.screen.pcSettings());
        ui.last = std::move(out.draw);
    }
    updateMapPreviews(); // the Multiplayer pages' map pictures (ImageKind::LevelPreview)
    drawHostList(f, ui.last, now, ui.fade, &previewCrop);
    if (visible && ui.last.size() > 0)
        sh.titleDrawn.fetch_add(1); // the EE watchdog (drakan_menu_screens.inl titleTick)
    if (in.pointerValid && tu.device == gameui::InputDevice::KeyboardMouse && f.input && visible)
        dsettings::drawMenuCursor(in.pointerX, in.pointerY, std::clamp(f.h / 1080.0f, 0.62f, 3.0f));
}

// The Original title menu is the game's own page: with several games installed the host draws a Switch Campaigns button
// over it (mouse click, G or the pad's Back button). It only sets a request; the EE glue (drakan_menu_screens.inl
// titleTick) brings the plain game list back, the same path as at start-up. It takes no input from the game.
bool switchButtonShown() {
    return drakan_menu_screens::shared().switchShown.load();
}
void switchButtonDraw(const dsettings::SettingsOverlay::ScreenFrame& f) {
    auto& sh = drakan_menu_screens::shared();
    TabUi& tu = g_tabUi;
    tu.renderer.setFonts(*f.fonts);
    tu.renderer.setSkin(f.skin);
    const gameui::Viewport vp{0, 0, f.w, f.h};
    const gameui::Rect r = gameui::TitleScreen::switchButtonRect(vp);
    // ImGui is not fed the mouse while the native title owns the input (OverlayEvents: only a screen that wants input
    // is), so the pointer is read from the OS (client area scaled to the back buffer) and the click is the physical
    // left button's press edge.
    ImVec2 mp = ImGui::GetIO().MousePos;
    bool pointerOk = ImGui::IsMousePosValid(&mp);
#ifdef _WIN32
    if (HWND hw = static_cast<HWND>(drakan_win::surface().window)) {
        POINT pt{};
        RECT rc{};
        if (GetCursorPos(&pt) && ScreenToClient(hw, &pt) && GetClientRect(hw, &rc) && rc.right > 0 && rc.bottom > 0) {
            mp = ImVec2(static_cast<float>(pt.x) * f.w / static_cast<float>(rc.right),
                        static_cast<float>(pt.y) * f.h / static_cast<float>(rc.bottom));
            pointerOk = true;
        }
    }
#endif
    const bool hover = pointerOk && r.contains(mp.x, mp.y);
    static bool lHeld = false;
    const bool lDown = drakan_win::focused() && drakan_win::keyDown(drakan_win::vk::LeftButton);
    const bool click = lDown && !lHeld;
    lHeld = lDown;
    static bool gHeld = false;
    const bool gDown = drakan_win::focused() && drakan_win::keyDown('G');
    const bool key = gDown && !gHeld;
    gHeld = gDown;
    const bool pad = ImGui::IsKeyPressed(ImGuiKey_GamepadBack, false);
    if ((hover && click) || key || pad) {
        sh.switchRequest.store(true);
        std::fprintf(stderr, "[menu-screens] Switch Campaigns pressed (%s) at (%.0f, %.0f) of %.0fx%.0f\n",
                     hover && click ? "click"
                     : key          ? "G"
                                    : "pad",
                     mp.x, mp.y, f.w, f.h);
    }
    { // Test aid: DRAKAN_DEBUG_SWITCH_CLICK=<seconds> presses the button once when the log clock passes it.
        static const double at = [] {
            const char* e = std::getenv("DRAKAN_DEBUG_SWITCH_CLICK");
            return e ? std::atof(e) : -1.0;
        }();
        static bool done = false;
        if (at >= 0 && !done && drakan_menu_screens::clockSeconds() >= at) {
            done = true;
            sh.switchRequest.store(true);
        }
    }
    gameui::ScreenConfig cfg;
    cfg.measure = tu.renderer.measure();
    drawHostList(f, gameui::TitleScreen::drawSwitchButton(vp, hover, cfg), ImGui::GetTime(), 1.0f, {});
}

namespace {
// The title and the loading screen after it show the chosen game's own picture (previews/background-<id>.png, else its
// cover campaign-<id> / game-<id>, the same files as the game card) instead of the disc's title art, which names The
// Ancients' Gates. The disc game keeps its art. Both menu styles: the renderer swaps the art under the menus.
// The picture of one game card entry ("" for the disc game or when the game has none).
std::string titlePictureOf(const drakan_game::GameInfo& g) {
    if (g.kind == drakan_game::Game::Drakan)
        return {};
    for (const char* kind : {"background-", "campaign-", "game-"})
        if (const uint32_t id = mapPreviewId(kind + g.id); id != 0)
            return previewPath(id);
    return {};
}

// The overlay thread closes the classic chooser in the frame of the choice, but the EE applies the choice
// (drakan_game::select) a frame or two later. Until then the native title would draw the disc art, so the choice binds
// its picture first and syncTitleBackdrop follows the chosen index until the game index catches up (or a second passes:
// a refused choice).
std::mutex g_backdropM;
int g_backdropPending = -1;
std::chrono::steady_clock::time_point g_backdropPendingUntil;
} // namespace

void syncTitleBackdrop() {
    // The overlay (every present) and the game thread (the choice, syncTitleBackdropNow) both call this.
    std::lock_guard<std::mutex> lock(g_backdropM);
    static int s_game = -2;
    static std::string s_path;
    static std::chrono::steady_clock::time_point s_checked;
    static bool s_preloaded = false;
    const std::vector<drakan_game::GameInfo> games = drakan_game::games();
    if (!s_preloaded) { // decode every campaign's picture now, so choosing one on the game card swaps the art at once
        s_preloaded = true;
        for (const drakan_game::GameInfo& g : games)
            if (const std::string p = titlePictureOf(g); !p.empty())
                gsr::preloadTitleBackdropImage(p);
    }
    const auto now = std::chrono::steady_clock::now();
    int game = drakan_game::currentIndex();
    if (g_backdropPending >= 0) {
        if (g_backdropPending == game || now >= g_backdropPendingUntil)
            g_backdropPending = -1;
        else
            game = g_backdropPending; // chosen, not yet applied by the EE
    }
    if (game == s_game && (!s_path.empty() || now - s_checked < std::chrono::seconds(3)))
        return; // no picture yet: look again every 3 s (mapPreviewId's own retry)
    s_game = game;
    s_checked = now;
    const std::string path =
        game >= 0 && game < static_cast<int>(games.size()) ? titlePictureOf(games[static_cast<size_t>(game)]) : "";
    if (path == s_path)
        return;
    s_path = path;
    gsr::setTitleBackdropImage(path);
    std::fprintf(stderr, "[menu-screens] title art: %s\n", path.empty() ? "the disc's" : path.c_str());
}

void previewTitleBackdrop(int game) {
    {
        std::lock_guard<std::mutex> lock(g_backdropM);
        g_backdropPending = game;
        g_backdropPendingUntil = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    }
    syncTitleBackdrop();
}
} // namespace drakan_settings_host::detail
#endif // PS2X_HAVE_DRAKAN_SETTINGS
