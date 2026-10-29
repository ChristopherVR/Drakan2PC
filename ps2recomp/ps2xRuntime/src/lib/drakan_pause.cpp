#include "drakan_pause.h"
#include "drakan_config.h"
#include "drakan_settings_host.h"
#include <spu2/AudioOutput.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>

#if defined(PS2X_HAVE_GSR)
#include "drakan_window.h"
#endif

namespace drakan_pause {
namespace {
std::atomic<spu2::AudioOutput*> g_audio{nullptr};
std::atomic<bool> g_focusPaused{false};
std::atomic<bool> g_overlayPaused{false};
std::atomic<bool> g_overlayHold{false}; // overlay open AND the game had no menu of its own when it opened
std::atomic<int> g_guiScreen{-1};
std::atomic<bool> g_gameplay{false};
std::atomic<bool> g_pauseReq{false};
std::atomic<int> g_cfg{-1}; // pause_when_unfocused, read once

bool wantPauseUnfocused() {
    int v = g_cfg.load();
    if (v < 0) {
        v = drakan_cfg::getBool("general", "pause_when_unfocused", "DRAKAN_PAUSE_UNFOCUSED", false) ? 1 : 0;
        g_cfg.store(v);
        if (v)
            std::fprintf(stderr, "[drakan] pause_when_unfocused = 1: the game (and its audio) pauses while the window "
                                 "is not in the foreground\n");
    }
    return v != 0;
}

// DRAKAN_DEBUG_HOLD: test hook, see header.
bool debugHold() {
    static const char* e = std::getenv("DRAKAN_DEBUG_HOLD");
    if (!e)
        return false;
    static const auto t0 = std::chrono::steady_clock::now();
    double a = 0, b = 0;
    if (std::sscanf(e, "%lf,%lf", &a, &b) != 2)
        return false;
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return t >= a && t < a + b;
}
} // namespace

void setAudio(spu2::AudioOutput* audio) {
    g_audio.store(audio);
}

void hostTick(void* hostWindow) {
    bool unfocused = false;
#if defined(PS2X_HAVE_GSR)
    if (hostWindow)
        unfocused = !drakan_win::focused() || drakan_win::minimized();
#else
    (void)hostWindow;
#endif
    const bool fp = unfocused && wantPauseUnfocused();
    const bool ov = drakan_settings_host::isOpen() && g_overlayHold.load();
    g_focusPaused.store(fp);
    g_overlayPaused.store(ov);
    if (spu2::AudioOutput* a = g_audio.load())
        a->setPaused(fp || ov);
}

bool holdRequested() {
    return g_focusPaused.load() || (drakan_settings_host::isOpen() && g_overlayHold.load()) || debugHold();
}

namespace {
std::atomic<bool> g_titleSeen{false};
std::atomic<bool> g_flowOk{false};
std::atomic<int> g_flowScr{-1};
std::atomic<bool> g_flowPlay{false}, g_flowScripted{false};
std::atomic<long long> g_flowAtMs{0};

long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
// GUI screens in which the player works with a menu (title, options, pause, load / save, inventory, message boxes ...)
// or watches the credits.
bool menuScreen(int id) {
    switch (id) {
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 8:
        case 9:
        case 10:
        case 13:
        case 0x13:
        case 0x15:
        case 0x17:
        case 0x1b:
        case 0x1c:
            return true;
        default:
            return false;
    }
}
} // namespace

void noteFlow(int guiScreen, bool gameplay, bool scriptedInput) {
    if (guiScreen == 8)
        g_titleSeen.store(true);
    const bool ok = g_titleSeen.load() && (menuScreen(guiScreen) || (guiScreen < 0 && gameplay && !scriptedInput));
    g_flowOk.store(ok);
    g_flowScr.store(guiScreen);
    g_flowPlay.store(gameplay);
    g_flowScripted.store(scriptedInput);
    g_flowAtMs.store(nowMs());
}
bool f1Allowed() {
    static const bool off = std::getenv("DRAKAN_F1_ALWAYS") != nullptr; // debugging aid: no gating
    if (off)
        return true;
    return g_flowOk.load() && nowMs() - g_flowAtMs.load() < 700;
}
bool flowBlockedFresh() {
    return g_titleSeen.load() && !g_flowOk.load() && nowMs() - g_flowAtMs.load() < 700;
}
std::string flowDescription() {
    char b[160];
    const long long age = g_flowAtMs.load() ? nowMs() - g_flowAtMs.load() : -1;
    std::snprintf(b, sizeof b, "screen %d, camera %d, scripted %d, title seen %d, last sample %lld ms ago",
                  g_flowScr.load(), g_flowPlay.load() ? 1 : 0, g_flowScripted.load() ? 1 : 0,
                  g_titleSeen.load() ? 1 : 0, age);
    return b;
}

void setGameState(int guiScreen, bool gameplay) {
    g_guiScreen.store(guiScreen);
    g_gameplay.store(gameplay);
}
int guiScreen() {
    return g_guiScreen.load();
}
bool gameplay() {
    return g_gameplay.load();
}
void overlayOpened() {
    g_overlayHold.store(g_guiScreen.load() < 0);
}
void overlayClosed() {
    g_overlayHold.store(false);
}
bool overlayHolds() {
    return g_overlayHold.load();
}
void requestPauseThenOpen() {
    g_pauseReq.store(true);
}
bool takePauseRequest() {
    return g_pauseReq.exchange(false);
}
bool focusPaused() {
    return g_focusPaused.load();
}
} // namespace drakan_pause
