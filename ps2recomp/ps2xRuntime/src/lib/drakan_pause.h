// Drakan: host pause gate (window focus / settings overlay) shared by the host loop, the PadMgr_Update hook and the
// SPU2 output.
//   * pause_when_unfocused (drakan.ini [general], env DRAKAN_PAUSE_UNFOCUSED=0|1, default 0): when the game window is
//   not the
//     foreground window (or is minimised) the guest is held at the next frame boundary and the SPU2 output is paused.
//   * the settings overlay (F1) holds the guest the same way and pauses the SPU2 output too.
//   * holdRequested() is polled by the EE thread (PadMgr_Update hook, drakan_input_glue.cpp).
#pragma once
#include <string>

namespace spu2 {
class AudioOutput;
}

namespace drakan_pause {
void setAudio(spu2::AudioOutput* audio);
// Host thread, once per host frame: evaluates focus / minimised state for `hostWindow` (HWND) and the overlay, and
// pauses / resumes the audio output. Cheap; also callable while the window is minimised.
void hostTick(void* hostWindow);
// True while the guest must not advance (unfocused with pause_when_unfocused, or the settings overlay is open and the
// game has no menu of its own to sit in: see overlayOpened()). Thread safe.
bool holdRequested();
// Game state published by the EE thread (PadMgr_Update hook) every frame, read by the window thread:
//   guiScreen  = id of the active GUI screen (8 title, 9 options, 0x17 pause ...), -1 = none
//   gameplay   = the player camera is running and no GUI screen is open (F1 can ask the game to pause itself)
void setGameState(int guiScreen, bool gameplay);
int guiScreen();
bool gameplay();
// The settings overlay opened / closed (window thread). The EE is held (and the audio paused) only when the overlay
// opened while the game was not in one of its own menus (cut scene, loading ...): over a game menu the game keeps
// running (menu, music) and only its input is swallowed (drakan_input suspended).
void overlayOpened();
void overlayClosed();
bool overlayHolds();
// F1 in gameplay: the EE thread injects START once and waits until the pause menu is up, then opens the overlay.
// requestPauseThenOpen() is called by the window thread, take() by the EE thread (returns true once per request).
void requestPauseThenOpen();
bool takePauseRequest();
// Flow state for gating F1 (EE thread, every PadMgr_Update): which game screen is up, whether the player camera runs,
// whether the game scripts the input (cut scene flag 0x4fbafc). F1 is allowed only in the title menu / game menus /
// gameplay, and never before the title menu was seen (logos, intro).
void noteFlow(int guiScreen, bool gameplay, bool scriptedInput);
bool f1Allowed(); // window thread; false when blocked or when the EE has not reported for a while (movie / loading: the
                  // hook does not run)
bool flowBlockedFresh(); // a recent sample says "movie / cut scene / loading" (used to close an overlay that is already
                         // open)
std::string flowDescription();
// True when only the focus rule asks for the hold (used for diagnostics).
bool focusPaused();
// Debug aid (DRAKAN_DEBUG_HOLD="<start_s>,<len_s>"): holds the guest for <len_s> seconds starting <start_s> after
// launch WITHOUT pausing the audio, to exercise the SPU2 stall watchdog. Evaluated inside holdRequested().
} // namespace drakan_pause
