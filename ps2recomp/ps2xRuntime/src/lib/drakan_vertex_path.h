// Which path the vertex work of the game takes ([video] vertex_processing = cpu | gpu | auto, env
// DRAKAN_VERTEX_PROCESSING): the host side of gsr/vertex_path.h.
//
//   cpu   the VU1 microprogram does all vertex work on the CPU, as the game always did (the default)
//   gpu   recognised runs are handed to the renderer and drawn by a vertex shader that repeats the arithmetic of the
//         microprogram bit for bit; whatever the renderer cannot draw exactly stays on the CPU
//   auto  gpu where the renderer and graphics adapter are proven identical, cpu everywhere else
// Both paths give the same picture; the setting can change while the game runs and takes effect at the next object.
// Diagnosis overrides (docs/reference/env-flags.md): DRAKAN_VERTEX_EVAL, DRAKAN_VERTEX_STAGES, DRAKAN_VERTEX_TOGGLE,
// DRAKAN_VERTEX_LOG.
#pragma once

#include <string>

namespace gsr {
class Renderer;
}

namespace drakan_vertex_path {

// Reads the setting and the overrides and applies them against what `renderer` can do. Call once the renderer exists.
void attach(gsr::Renderer* renderer);
// The setting changed (settings page): cpu | gpu | auto. Unknown text counts as cpu.
void configure(const std::string& choice);
// What is in effect, for the settings page and the log: "cpu", "gpu" or "records" (a diagnosis mode).
const char* activePath();
// True when `gpu` / `auto` can do anything with the attached renderer (the settings page says why not otherwise).
bool gpuAvailable();
// One line with the counters of gsr/vertex_path.h (runs taken per stage family, vertices per path, fallbacks per
// reason). Empty while nothing was counted.
std::string countersLine();
// Called once per presented frame by the host: prints countersLine() every few seconds under DRAKAN_VERTEX_LOG=1.
void frameTick();

} // namespace drakan_vertex_path
