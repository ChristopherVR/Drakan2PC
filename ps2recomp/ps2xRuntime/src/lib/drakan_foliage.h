// Living foliage (experimental): a travelling wind with gusts in the vegetation. [video] foliage_wind (env
// DRAKAN_FOLIAGE_WIND): off | grass | all | classic, off by default; 1 / on in an older ini mean all.
//
// * Grass, flowers, ferns and crops (the plants of the vegetation table drakan_foliage_ids.inc,
//   analysis/scripts/foliage_ids.py) bend as a whole: the engine's per-object render method (0x189b38, `this` = the
//   registered handle) reads the object's 3x3 at handle +0x20 to build the model matrix; for the duration of the call
//   the hook adds a horizontal shear to the image of the model's up axis (the base stays, the tip moves) and puts the
//   matrix back. Grass also bends away from whoever the player controls.
// * Trees (all): trunks and branches stay still and only the leaves move. A leaf is a vertex of a cut-out material
//   (texture with transparent texels); one that a solid face shares is an anchor and stays. The packets of an object
//   reference its position arrays and the DMA reads them after the render call, so the hook gives each tree its own
//   copy of its leaf arrays in one block of the game's heap, refills it every frame with the leaves moved, and points
//   the draw at it during the call: the inline draw groups (0x1ed8f0, group +8) and the indexed path's position source
//   (0x1ec160 / 0x1ec8f0, renderer +0xc). Trees come from the vegetation table, or are static props whose model looks
//   like a tree (world::foliage::looksLikeTree), which also covers converted Order of the Flame levels.
// * classic: the first version, whole trees lean with the same shear as the grass.
// Nothing outside the render call sees a change: saves, physics, collision and the ray tracing scene export keep the
// true transforms and the model's own arrays. PAL only (the addresses are PAL values; other disc profiles never
// install it). Wind model and leaf rules: runtime/world/include/world/Foliage.h (world_foliage_test).
#pragma once

#include <cstdint>

class PS2Runtime;

namespace drakan_foliage {

// Installs the render hooks when the disc profile has PAL addresses and the functions exist. Safe to call more than
// once.
void install(PS2Runtime& runtime);
// True when the hook is installed (the settings page greys the options out otherwise).
bool available();

// What moves: nothing; grass, flowers and ferns; grass and the leaves of trees (trunks and branches still); or the
// first version (grass and whole trees lean).
enum class Mode : int { Off = 0, Grass = 1, All = 2, Classic = 3 };
// ini / env text: off | 0, grass, all | 1 | on | leaves, classic. Anything else is Off.
Mode modeFromText(const char* text);

// Live settings (any thread). strength is clamped to 0.25 .. 2.
void configure(Mode mode, float strength);
Mode mode();
bool enabled();
float strength();

} // namespace drakan_foliage
