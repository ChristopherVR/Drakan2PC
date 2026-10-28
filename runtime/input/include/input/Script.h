// Scripted host input for automated tests (DRAKAN_INPUT_SCRIPT=<file>). Frame numbers count PadMgr_Update calls
// (1 = first call since process start). Lines run in file order; <frame> is relative to the last `sync`.
// Optional header `clock milliseconds` (once, before events) changes numeric event times to elapsed milliseconds
// since the first timestamped advance, or the sample satisfying the last `sync play`. Events run on the first
// sample at/after their time; overdue events run in file order. This does not synthesize samples between calls.
//
//   <frame> key    down|up|tap <KEYNAME>      DirectInput-style key names of the ini (W, SPACE, ESC, ENTER, ...)
//   <frame> button down|up|tap <1..5>         mouse button (1 = left)
//   <frame> mouse  <dx> <dy> [frames]         raw mouse counts, applied on each of `frames` consecutive frames (default
//   1) <frame> wheel  <notches> <frame> pad    down|up|tap <PADBUTTON>    DualShock button OR-ed into the sample
//   (CROSS, START, DPAD_UP ...) <frame> cursor <x> <y>                    host cursor in a virtual 640x512 client area
//   (menu cursor tests) <frame> dump   <label>                    asks the integration to print the player/camera state
//   <frame> shot   <label>                    screenshot of the game window via the integration (renderer PNG
//                                             <label>.png + desktop BMP)
//   <frame> log    on|off [every]             per-frame state log (every N frames)
//   <frame> menu   on|off|auto                override the menu/gameplay detection
//   <frame> sync   play <n>                   wait until the integration reports n consecutive gameplay frames; frame
//   numbers
//                                             of the lines after it are relative to that moment
//   <frame> sync   screen <id> [n]            wait until GUI screen <id> has been the active screen for n samples
//                                             (default 1; -1 = no screen); later frames are relative to that moment
//   <frame> quit                              ask the integration to stop the process
//   <frame> dual_wield on|off                 apply the live runtime setting (test only; does not save the INI)
// `sync play <n>` still counts consecutive gameplay samples in either clock mode.
// `tap` holds the key/button/pad bit for exactly one input sample in either mode.
// Mouse [frames] repetitions and log [every] remain input sample counts, NOT milliseconds; mouse counts are
// applied once per sample, including while waiting for sync. Use timed one-shot mouse events for fixed totals.
#pragma once
#include "input/FrameInput.h"
#include <bitset>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace drakan_input {

struct ScriptEvents {
    std::vector<std::string> dumps;
    std::vector<std::string> shots;
    bool quit = false;
    int logOn = -1;     // -1 unchanged, 0 off, >0 = every N frames
    int menu = -2;      // -2 unchanged, -1 auto, 0 gameplay, 1 menu
    int dualWield = -1; // -1 unchanged; live runtime setting, without changing the user's INI
};

class InputScript {
public:
    // Returns false (and fills *err) when the file cannot be read; bad lines are reported in *err but skipped.
    bool load(const std::string& path, std::string* err);
    bool loaded() const { return loaded_; }
    // Active GUI screen id of the coming sample (-1 none), for `sync screen`. Call before advance().
    void setScreen(int id) {
        screenRun_ = id == screen_ ? screenRun_ + 1 : 1;
        screen_ = id;
    }
    // Produces the host snapshot for `frame` (edges computed against the previous call) and the side events.
    // monotonicMs is an arbitrary-origin monotonic timestamp in milliseconds, ignored by default frame scripts.
    // In millisecond mode omission freezes event scheduling (including sync); taps/repetitions still advance.
    // Repeated/backwards timestamps cannot rewind elapsed time or replay events.
    void advance(int frame, int playRun, FrameInput& out, uint16_t& padButtons, ScriptEvents& ev,
                 std::optional<uint64_t> monotonicMs = std::nullopt);

private:
    struct Ev {
        int frame = 0;
        enum Kind { Key, Button, Mouse, Wheel, Pad, Cursor, Dump, Shot, Log, Menu, Sync, Quit, DualWield } kind = Key;
        int mode = 0; // 0 up, 1 down, 2 tap
        int code = 0;
        int a = 0, b = 0, n = 1;
        std::string text;
    };
    std::vector<Ev> evs_;
    size_t next_ = 0;
    int segBase_ = 0;
    bool milliseconds_ = false;
    std::optional<uint64_t> timeBase_;
    uint64_t lastMs_ = 0;
    bool waiting_ = false;
    bool loaded_ = false;
    int screen_ = -1, screenRun_ = 0;
    std::bitset<256> keys_, prevKeys_, tapKeys_;
    uint8_t mouse_ = 0, prevMouse_ = 0, tapMouse_ = 0;
    uint16_t pad_ = 0, tapPad_ = 0;
    int curX_ = 320, curY_ = 256;
    struct Rep {
        int dx, dy, left;
    };
    std::vector<Rep> reps_;
};

} // namespace drakan_input
