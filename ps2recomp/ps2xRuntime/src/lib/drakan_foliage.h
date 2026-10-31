// Living foliage (experimental): grass, flowers, ferns, crops and trees sway in a travelling wind with gusts, and the
// grass bends away from whoever the player controls. Off by default ([video] foliage_wind, env DRAKAN_FOLIAGE_WIND).
//
// How: the engine's per-object render method (0x189b38, `this` = the registered handle) reads the object's position and
// 3x3 rotation at handle +0x14 / +0x20 to build the model matrix. For a handle whose resource id is in the vegetation
// table (drakan_foliage_ids.inc, analysis/scripts/foliage_ids.py) the hook adds a world-space horizontal shear to the
// rotation (the image of the model's up axis leans), calls the original and puts the rotation back. Nothing outside the
// render call ever sees the change: saves, physics, collision and the ray tracing scene export keep the true transform.
// PAL only (the addresses are PAL values; other disc profiles never install it).
#pragma once

#include <cstdint>

class PS2Runtime;

namespace drakan_foliage {

// Installs the render hook when the disc profile has PAL addresses and the function exists. Safe to call more than once.
void install(PS2Runtime& runtime);
// True when the hook is installed (the settings page greys the options out otherwise).
bool available();

// Live settings (any thread). strength is clamped to 0.25 .. 2.
void configure(bool enabled, float strength);
bool enabled();
float strength();

// The wind model itself is runtime/world/include/world/Foliage.h (pure functions, unit tested in world_foliage_test).

} // namespace drakan_foliage
