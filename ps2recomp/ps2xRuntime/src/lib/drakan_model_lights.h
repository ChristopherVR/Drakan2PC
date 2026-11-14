// Point lights per model (PAL). Rules: runtime/world/include/world/ModelLights.h.
//
//   * [video] model_lights (env DRAKAN_MODEL_LIGHTS), 4 .. 16, 4 = original: how many lights (the sun included) light
//     one model. The light set builder 0x1c4148 keeps the sun and the 3 nearest point lights for a model (its callers
//     pass a limit of 4, because the model upload 0x1e9fa0 writes exactly four slots into VU1 [13..20]). The wrapper
//     runs it unchanged, then evaluates the candidates it cut off (the sorted list at 0x56b6c0) the way its accept
//     loop does and adds up to (setting - 4) of them: into a free slot, else folded into the slot whose direction is
//     closest. The set never holds more than four slots, so the upload and the microprogram are untouched. Objects
//     that reuse their cached lights (no full search this frame) get the lights the last full search cut off.
// DRAKAN_MODEL_LIGHTS_LOG=1 counts, every 5 s, the calls with a limit of 4: candidates after the sort, the lights the
// engine accepted and the lights it would have accepted without the limit (also with the option at 4).
#pragma once

class PS2Runtime;

namespace drakan_model_lights {

// Installs the wrapper when the disc profile has PAL addresses and the function exists. Safe to call more than once.
void install(PS2Runtime& runtime);

} // namespace drakan_model_lights
