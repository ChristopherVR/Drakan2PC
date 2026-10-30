// EE-thread settings/pause handshake; no platform, input or renderer dependencies.
#pragma once
#include <cstdint>

namespace drakan_pause {
struct InputActions {
    bool tapStart = false;
    bool open = false;
};
class InputPolicy {
public:
    InputActions tick(bool request, int screen, bool gameplay, bool scripted, bool overlayOpen, bool resumeOnClose,
                      int pauseScreen, int64_t nowMs) {
        InputActions action;
        if (overlayWasOpen_ && !overlayOpen) {
            action.tapStart = autoPaused_ && resumeOnClose && screen == pauseScreen;
            autoPaused_ = false;
        }
        overlayWasOpen_ = overlayOpen;
        if (scripted) {
            waiting_ = autoPaused_ = false;
            action.tapStart = false;
            return action;
        }
        if (request && !waiting_ && !overlayOpen) {
            if (gameplay && screen < 0) {
                action.tapStart = true;
                waiting_ = true;
                startedMs_ = nowMs;
            } else {
                action.open = true;
            }
        } else if (waiting_) {
            if (screen == pauseScreen) {
                waiting_ = false;
                autoPaused_ = true;
                action.open = true;
            } else if (nowMs - startedMs_ > 3000) {
                waiting_ = false;
                action.open = gameplay && screen < 0;
            }
        }
        return action;
    }

private:
    bool waiting_ = false, autoPaused_ = false, overlayWasOpen_ = false;
    int64_t startedMs_ = 0;
};
} // namespace drakan_pause
