# Modern water: per-frame CPU spikes on Direct3D 11 and WebGPU (31 October 2026)

Report: since the WebGPU renderer landed, modern water is very heavy on both renderers.

## What the profile showed

`play_run.py` in Surdana with a still / turn / settle recipe, `PS2X_GS=d3d11 GSR_MODERN_WATER=1 GSR_PROFILE=4`.
The D3D11 source barely changed in the WebGPU port (renames; `packAttributes` moved to `ray_scene.cpp`), so the cost
was in shared code paths that the port's work made easy to hit:

| Stage (per pass) | Before | After |
| --- | --- | --- |
| `rayTriangles` (pass thread, static world) while turning | 25-70 ms | 1-3 ms |
| `rayModelAttributes` with a collapsed weld cache | ~54 ms (whole session) | ~2 ms |
| `rayScene` (GS thread) while turning | 3-10 ms, peaks 70 ms | 0.5-2 ms |

Frame rates in these runs are not comparable: other sessions were building and running tests at the time (CPU at
100%). The stage timings above are per pass.

## Causes and fixes

1. **Material projections changing every frame.** While the camera moves, the surface builder keeps finding larger
   reference triangles for terrain layers (`ResidentScene::observe`), so the attribute version changes most frames.
   Each change re-projected all ~185k resident triangles into a new 27 MB vector on the GS thread, and the D3D11 pass
   repacked the whole static material buffer, welding the normals again, and uploaded all of it (~15 MB).
   - `ResidentScene` (`resident_scene.h`) keeps triangle lists per layer and the version at which each layer last
     changed. A new projection reuses a buffer that no holder keeps any more and re-projects only the layers changed
     since that buffer was built (or copies the current one). Triangles are byte copies of the resident ones, so
     unchanged triangles compare equal byte for byte.
   - `RayWater` (`ray_water.cpp`) keeps welded normals and slot order per geometry version, repacks only the
     triangles that differ from the last packed snapshot (as `wgpu_ray.cpp` already did), and uploads only those slot
     ranges. With `GSR_RAY_CPU_CACHE_CHECK=1` the result is compared with a full repack.
2. **Model attributes rebuilt when the frame texture count changes.** Native model attributes hold
   `nativeTextureBase + material`; any change of the frame's texture count (frequent while turning) rebuilt every
   cached mesh, normals included, on both renderers. `rebaseModelAttributes` (`ray_scene.cpp`) now rewrites only the
   texture field.
3. **Collapsed poses poisoning the weld cache.** Welds are cached per topology from the first pose seen. A model first
   seen with every vertex on one point (Rynn's 2,492-triangle mesh in the run above: one group of 7,476 corners) made
   `smoothNormals`, which is quadratic per group, cost ~50 ms for every later pose. `weldCorners` leaves groups larger
   than `CornerWelds::kMaxGroup` (64) unwelded and sets `collapsed`; both passes weld again from a later pose.

## Validation

- `scene_geometry` (new: incremental projections match a fresh projection, held projections stay unchanged; a
  collapsed pose is flagged and re-welded), `surface_builder`, `ray_water`, `ray_trace` pass.
- Game, D3D11 with `GSR_RAY_CPU_CACHE_CHECK=1`: 540 checked passes, zero mismatches (world, models, static repack).
- Game, WebGPU on Vulkan with modern water: the pass traces as before (`[modern-water] first ray-traced world`). Its
  remaining cost is the compute-shader traversal itself ([webgpu-ray-tracing.md](../2026-10-30/webgpu-ray-tracing.md)).
