// View distance ([video] view_distance, env DRAKAN_VIEW_DISTANCE): the world is drawn this many times further out
// and its distance fog starts and ends that many times further away.
//
// How: the world's fog setter (0x142f70, a0 = world, f12 = far in cells, f13 = fog range in cells; 75 / 48.75 in every
// level seen) is called with both numbers times the factor. The terrain grid radius, the frustum, the object draw cap,
// particles and the VU1 fog constants all read what it writes. Two readers keep the original numbers: the sound
// distance (+0x38c, put back after every setter call) and the per-layer object activity loop 0x15cba0, which runs
// with the original far distance for the length of its call, so no creature or object wakes up further away than on
// the PS2. The underwater fog (0x142fe0) is not touched. A live change is applied at the next terrain view set-up
// (0x153828, once per frame). Factor 1 calls the original functions with the guest untouched.
// Rules: runtime/world/include/world/ViewDistance.h (world_view_distance_test). PAL only (the addresses are PAL
// values).
#pragma once

class PS2Runtime;

namespace drakan_view_distance {

// Installs the hooks when the disc profile has PAL addresses and the functions exist. Safe to call more than once.
void install(PS2Runtime& runtime);
// True when the hooks are installed (the settings page greys the option out otherwise).
bool available();
// Live setting (any thread). Clamped to 1 .. 3; 1 = original.
void configure(float factor);
float factor();

} // namespace drakan_view_distance
