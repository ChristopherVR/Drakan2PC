// Drakan: editor play test (docs/reference/modding.md "Play test"). The level editor's "Play from here" starts the game
// on the open level with DRAKAN_START_LEVEL and asks for the player to stand somewhere else:
//   DRAKAN_PLAYTEST_POS="x y z [yaw]"   game units; yaw in radians (0 faces -z, -pi/2 faces +x; measured 2026-11-01),
//   optional.
// Once the level is up and the player is under control the player is moved there at a frame boundary (the same write
// the arena's spawn placement uses: player+0x14.., then the engine's own re-filing into its collision cell). The engine
// puts a player back when no floor is resident at a far target, so the placement is checked and repeated a few times.
// PAL only (uses the generic player pointer at engine+0x4b0); compiled with the GS renderer like drakan_arena.
#pragma once

#include <cstdint>
#include <string>

namespace drakan_input {
class IGuest;
}

namespace drakan_playtest {

// EE thread, every PadMgr_Update (after drakan_arena::beforePadMgr). Does nothing unless DRAKAN_PLAYTEST_POS (or
// DRAKAN_START_LEVEL with DRAKAN_START_LEVEL_FROM="Level 0 - Intro", see introToLevel) is set.
void beforePadMgr(drakan_input::IGuest& guest, uint8_t* rdram);

// Pure parser for the environment value (unit tested): up to four floats separated by spaces or commas.
bool parsePose(const char* text, float pos[3], float& yaw, bool& hasYaw);

// The path New Game's opening level load (`.../Level 0 - Intro/Level 0 - Intro.lvl`) is turned into for `level`
// (`.../<level>/<level>.lvl`, same folder layout); empty for any other file (the intro folder's Out.lvl included).
std::string introToLevel(const std::string& path, const std::string& level);

} // namespace drakan_playtest
