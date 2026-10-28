# World-triangle packing cache — 21 October 2026

The full ray-effects path was rebuilding material attributes for approximately 229,000 world triangles each frame. Opt-in CPU profiling identified about 11 ms/frame in this operation in the pond scene. These timings include CPU/driver waits where applicable and are not standalone GPU timestamp measurements.

`ray_world_cache.h` keeps exact source snapshots and grouped vertices/material attributes in 256-triangle blocks. Changed geometry, lighting, UVs, alpha flags, opacity, group membership, or texture-index validity rebuild the affected blocks. Camera-relative positions are still updated every frame. Group ordering and shader inputs remain unchanged. Texture-table growth only invalidates blocks whose valid/invalid index classification changes. Storage follows the existing 500,000-triangle scene budget and adds CPU memory; it does not add GPU readbacks.

`GSR_RAY_WORLD_CACHE=0` selects original world packing. `GSR_RAY_CPU_CACHE_CHECK=1` compares cached world positions, attributes, group counts and textured counts byte-for-byte against the original packing path, alongside the existing model-material checks. Validation must be disabled for performance measurements.

`GSR_PROFILE=2` now splits guide preparation, ray effects, model guides, Super Resolution, optional neural evaluation and composition. Ray sub-stages separate triangle packing, model attributes, uploads, API handoffs, acceleration-structure command recording and completion waits. Disabled profiling does not sample the clock.

## Validation

Six focused tests pass: scene geometry, scene models, model motion, temporal presentation, ray water and neural guides. Hardware ray tests run with the D3D12 debug layer and cache comparison enabled. New cache regressions cover block boundaries, unchanged camera-relative reuse, opacity/UV/lighting/alpha changes, reordering, texture-table validity, shrink/grow and empty scenes.

Three 350-frame movement routes pass finite SR/neural output, exact swapchain presentation, independent model-motion vectors, shadows in all 349 evaluated frames, and unchanged original/source/private saves. The pond and camp runs also record zero original-packing mismatches. The river's final screenshot faces the bank; it alone is not evidence of reflection appearance across the whole route. Hardware reflection and shoreline-shadow regressions remain passing.

## Performance and remaining limits

Diagnostic last-five-window triangle packing fell from approximately 11.2 ms to 2.7 ms per call. Some diagnostic runs overlapped other work, so this is a targeted timing observation, not a clean whole-game speedup claim. The retained full-frame measurements and process-overlap evidence are in `build/dlss-world-cache-20261021/performance.json` and `stage-summary.json`.

Feature-isolation runs with no recorded game/compiler overlap at 1920x1080 output and scale 3 measured about 78.1 FPS with temporal/ray features off and 70.95 FPS with Quality SR alone. This is one fixed view and demonstrates remaining overhead; it does not establish universal performance. All comparisons disable validation readbacks. Quality SR plus ray effects, with optional neural rendering disabled, measured 36.48 FPS in another run without recorded game/compiler overlap. Optional neural rendering adds further cost.

The original coarse terrain, limited low-frequency offscreen sky observations and incomplete whole-game coverage remain. Neural reconstruction does not replace meshes. The goal of consistently high performance with all effects remains open; this change removes repeated CPU work without reducing effect quality.

## Provenance

Candidate and baseline hashes, replaced object/source hashes and frozen link inputs are recorded in `build/dlss-world-cache-20261021/build.json`. Only the ray renderer and D3D11 backend objects are replaced in the previously validated renderer archive. Other runtime/menu objects remain frozen. Deployment records and the installed smoke test are recorded separately after validation.

Installed candidate `1ca391ce9cd11a97f6f6179034b07df8ad6066678990de4eb5ccfadf9de26e89` passed the deployed-game smoke test with finite neural output, exact presentation and unchanged saves. The original build is backed up in the evidence directory.

A final pair measured 20.76 FPS before and 24.54 FPS after caching. However, additional system sampling found 83% total CPU load with 28% GPU utilization, plus active Java, Node and Android-build processes outside the original overlap probe. These results are **not a clean causal FPS gain**. `background-load.json` retains the observation. Quiet-machine validation and full-feature performance remain unresolved.
