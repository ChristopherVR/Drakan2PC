// Ground texture distance ([video] terrain_distance, env DRAKAN_TERRAIN_DISTANCE): terrain keeps its ground textures
// further out than the PS2 budget allowed, so mid-range hills and mountains are drawn from the tiles instead of from
// the level's 4 texel per cell far colour map.
//
// How: the guest's chunk draw 0x158148 (a0 = layer, a1 = draw page) decides per ground texture whether the chunk is
// within the material's splat fade distance and uploads that distance to the vertex program for the fade. Both use
// one view float, s at *(0x5565c8) + 0x3c (1.0 in play): cut-off fade^2 > (distance * s)^2, vertex fade = fade / s.
// The hook divides s by the factor for the length of that call and puts it back when the call returns. No code, no
// level data and no material is changed, and nothing outside the call sees another value.
// Factor 1 calls the original function with the guest untouched. Rules: runtime/world/include/world/TerrainDistance.h
// (unit tested in world_terrain_distance_test). PAL only (the addresses are PAL values).
#pragma once

class PS2Runtime;

namespace drakan_terrain_distance {

// Installs the hook when the disc profile has PAL addresses and the function exists. Safe to call more than once.
void install(PS2Runtime& runtime);
// True when the hook is installed (the settings page greys the option out otherwise).
bool available();
// Live setting (any thread). Clamped to 1 .. 4; 1 = original.
void configure(float factor);
float factor();

} // namespace drakan_terrain_distance
