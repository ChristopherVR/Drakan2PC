// Drakan: level change from the host side (docs/reference/world-model.md section 10, journal
// 2026-10-31/level-change.md).
//
// The game changes level through exit triggers (class 0x10037, "Level Transition"); this module lists them and fires
// one by sending the trigger the same event it receives when the player touches it (event 0x1001 to its message
// handler's worker 0x361440), from a guest call posted at a frame boundary. Everything after that (save screen, loading
// screen, the unload / load on the main thread, the arrival at the destination's "portal" object) is the game's own
// code. The older shortcut of calling the level-change entry 0x13b7c0 directly hangs, because the world keeps running
// on the main thread while the load tears it down.
//
// PAL (SCES-500.06) only. Test aids: DRAKAN_LEVELCHANGE=<spec> (see install()).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class PS2Runtime;

namespace drakan_levelchange {

struct ExitInfo {
    uint32_t handle = 0; // registry handle of the exit trigger
    uint32_t ref = 0;
    float pos[3] = {0, 0, 0};
    uint32_t dest = 0; // property 0 "Transition To...": string id of the destination name
    uint32_t prop[8] = {};
    std::string destName; // text of the destination, e.g. "Level 1B - Kragmor" (empty when unreadable)
    bool direct = false;  // property 6 != 0: the trigger sets the transition state itself (no save screen)
};

// Installed from applyDrakanOverrides after drakan_lifecycle::install (chains PadMgr_Update).
void install(PS2Runtime& runtime);
bool installed();

// Exits of the current level as of the last scan (about once a second while someone asks, see requestScan()). Thread
// safe copy; empty before the first scan or while a level is loading.
std::vector<ExitInfo> exits();
void requestScan();

// Fires the exit trigger `handle` at the next frame boundary (any thread). false when nothing is installed.
bool requestExit(uint32_t handle);
// Fires the exit with the given index in exits() (0 = nearest to the player when the list is sorted by distance).
bool requestExitIndex(size_t index);
// Level change to a named level (case-insensitive substring of the destination text, e.g. "kragmor"): fires the exit
// of the current level that leads there. false when this level has no such exit (a level only connects to its
// neighbours; use several hops).
bool requestLevel(const std::string& level);

// Called on the EE thread when the game starts a level change through an exit (walked into, or fired here), with the
// trigger's world position. Not called for loads from a save or for changes made by followExit().
void setExitListener(std::function<void(const float* pos3)> listener);
// Fires the exit of this level at that world position (same level data on both machines). Waits for a level to be up.
bool followExit(float x, float y, float z);

// Frames since the last level finished loading (0 while loading). For harnesses / panel.
uint64_t framesInLevel();

// F1 World panel section (UI thread). No-op outside GSR builds.
void drawControls();

} // namespace drakan_levelchange
