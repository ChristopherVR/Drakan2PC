// Drakan: level change from the host side (docs/reference/world-model.md section 10, journal
// 2026-10-31/level-change.md).
//
// The game changes level through exit triggers (class 0x10037, "Level Transition"); this module lists them and fires
// one by writing the state the trigger writes (pending handle DAT_004fbba4) at a frame boundary. The game's own
// transition function (0x362d38, wrapped here so the player is put onto the trigger as its first act) then runs on the
// main thread: level-change save, unload / load, respawn of the player at the destination's arrival marker. No guest
// call is made. The older shortcut of calling the level-change entry 0x13b7c0 directly hangs, because the world keeps
// running on the main thread while the load tears it down.
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
    // The destination text is "portal": the exit is the arrival marker of a portal pair, not a way to another level.
    // The game's own transition hangs on it (it cuts the text at the first '.', which "portal" does not have), so
    // fire() refuses it.
    bool portal = false;
    // The trigger instance's state word (bit 1: set by message 0x10a3, bit 2: set by 0x1002; with bit 2 set the touch
    // event 0x1001 is ignored) and the handle's flag word (bits 2/3 set while a transition from it is pending).
    uint32_t state = 0, handleFlags = 0;
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
// A level a mod pack added as a copy of a disc level: its exits name their destinations like the source's do.
void registerLevelCopy(const std::string& copy, const std::string& source);
// Text of a level-local destination string as a mod pack replaced it (group = the level XDB's stem, e.g. "LEVEL1ON";
// id = 0x80000 + string id), empty when no pack touches it. Lets the exit list name an exit that a pack points at a
// copy.
void setDestinationTextHook(std::function<std::string(const std::string& group, uint32_t id)> hook);

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
