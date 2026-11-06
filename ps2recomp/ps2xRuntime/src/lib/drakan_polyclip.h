// Near-plane clip for the engine's own projected polygons (opt-out, default on: [video] poly_near_clip, env
// DRAKAN_POLY_NEAR_CLIP=0 disables). The polygon draw FUN_00146378 only checks that one vertex is in front of the camera
// and sends the whole polygon to the GIF emitter; ribbons / beams crossing the camera plane then reach the GS with a
// negative q on some vertices and show as starburst fans (docs/journal/2026-11-06/stratos-garbled-effects.md). The hook
// on the fan drawer FUN_00146880 clips such polygons (math: runtime/world/include/world/PolyClip.h) and draws the
// remainder; every other polygon takes the original path untouched. PAL addresses only.
#pragma once

#include <cstdint>

class PS2Runtime;

namespace drakan_polyclip {

void install(PS2Runtime& runtime);
bool available();
void configure(bool enabled);
bool enabled();

} // namespace drakan_polyclip
