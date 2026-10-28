// Drakan port: HD movie pack (<exe dir>/movies, built by analysis/scripts/fmv_hd_pack.py). Identifies every frame the
// IPU decodes, registers its macroblock signature with the renderer and serves the matching HD frame, decoded ahead
// on a worker thread. docs/reference/fmv.md section 9.
#pragma once
namespace gsr {
class Renderer;
}
namespace drakan_fmv_hd {
// Loads the pack ([video] hd_movies, env DRAKAN_HD_MOVIES) and hooks the IPU; nullptr detaches.
void attach(gsr::Renderer* renderer);
} // namespace drakan_fmv_hd
