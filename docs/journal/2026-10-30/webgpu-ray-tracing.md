# WebGPU renderer: ray-traced water and shadows in compute (phase 6)

Plan: [webgpu-renderer.md](../../proposals/webgpu-renderer.md). Previous step:
[renderer-selection.md](renderer-selection.md).

The D3D11 renderer's modern water, water reflections, soft shadows and ambient occlusion are one DXR 1.1 inline
ray-query shader (`ray_water.hlsl`) over a TLAS / BLAS built through D3D12 interop. `webgpu.h` exposes no ray queries,
so the WebGPU renderer traces the same scene in WGSL compute over BVHs built on the CPU. Same scene, same shading, a
different tracer; it runs on Windows (Vulkan / D3D12), Linux and, in principle, the browser.

## Pieces

- `gsr::rhi` gained compute: `Buffer` (storage, `writeBuffer` ordered with draws), compute programs
  (`ProgramDesc::computeEntry`, storage buffers at `@binding(20+)`, write-only storage textures at `@binding(28+)`),
  `setBuffer` / `setStorageTexture` / `dispatch`, the `Storage` texture usage, and `compute()` to ask whether the
  adapter can. The browser proxy forwards all of it.
- Shared with D3D11: `ray_scene.h/.cpp` (the triangle / texture / option types and the float4 material attribute
  packing, moved out of `ray_water.cpp`), the surface builder and resident scene (header-only, now in `gsr_core`).
- `ray_bvh.cpp`: binned-SAH BVH (12 bins, all axes in one pass) in a flat two-float4 node layout.
- `wgpu_ray.cpp`: packs the scene exactly as `RayWater::Impl::execute` does (attribute layout, instance groups and
  masks, instance IDs = material base | opacity << 19, camera-relative coordinates); static world BVHs per level, this
  frame's reconstructed triangles in 8k-triangle chunks, native model meshes cached by identity, a TLAS per frame. The
  CPU work runs as parallel jobs on a few worker threads; static material attributes are repacked incrementally (the
  resident scene's projections change every frame while the camera moves).
- `ray_trace.wgsl` (embedded by CMake as a byte array: MSVC limits string literals): the HLSL shading ported line
  for line, with `RayQuery` replaced by a two-level stack traversal (nearer child first) whose candidate test is the
  any-hit logic of each query kind (alpha-tested occluders, level-only, reflection water layers, forced opaque, water
  coverage). Shadow rays stop at the first hit that alone blocks the light.
- `traceWorld` (`wgpu_temporal.cpp`) runs the pass at the engine's EndWorld, before the temporal AA resolve, on the
  world target's colour and GS depth, and copies the result back before the HUD.

## Budget

Software traversal is far slower than DXR hardware. The first port traced everything at the internal resolution
(2560 x 1792 at scale 4) and ran at 1.3 fps. What brought it to playable rates:

- Land shadows and AO, and the water's reflection and shadowed lighting, are traced by `lowMain` at the native
  resolution (one low pixel per `scale` render pixels; `GSR_RAY_SHADOW_SCALE` overrides) and upsampled in `main` with
  depth-aware (shadows) or water-mask (water) weights. `main` keeps the per-pixel water hit, wave normals, refraction,
  Fresnel, glints, caustics and foam.
- `lowMain` also writes a water mask; `main` traces the water surface only near water (it used to cast one long ray per
  render pixel even with no water on screen: 19 fps on its own).
- Fewer rays when the temporal AA integrates the noise (1 sun sample, 2 AO rays, 2 reflection rays; 2 / 4 / 4 without
  it) and one shadow sample at reflection hits.
- CPU: 14 ms to about 4 ms per frame by chunking the frame triangles and repacking static attributes incrementally.

## Validation (Windows, RTX 5080, internal scale 4, 3840 x 2160, TAA on)

- `gsr_test_ray_trace` (runtime tests): BVH structure; on a water plane with a red wall the water reflects the wall and
  the sky stays untouched; a wall shadows the ground behind it, through frame triangles and through the static world;
  a material-only change of the static world reaches the GPU. Identical on Vulkan, D3D12 and GL.
- Surdana, water and shadows on, camera turning through a full circle: 27-72 fps (about 150 fps without ray tracing).
  Contact shadows and AO under trees, the tent and Rynn.
- Shadowmire cave pool (water filling the screen): reflections of the roots and rock, ripples, depth tint; 12-45 fps
  (2-5 fps before the water terms moved to the low pass).
- The same Shadowmire run on `PS2X_GS=d3d11` still evaluates the DXR scene.
- PC Settings > Display > Renderer offers WebGPU / Direct3D 11 on Windows builds with both renderers (restart).

## Limits

- Slower than DXR, and lower resolution shadows / reflections; the D3D11 renderer remains the choice for the full
  effect on RTX hardware.
- In the browser the pass runs, but slower than native (see below).

## Browser and Linux after the change

- Browser (Chrome, RTX 5080): the rebuilt player reaches gameplay in Surdana with the F1 overlay (no Renderer row
  there: WebGPU is its only renderer) and no console errors, at about 85 game frames per second (VSync on, Auto internal
  scale; the guest code runs as WebAssembly). Chrome's WGSL compiler (Tint) accepts `ray_trace.wgsl` with no messages
  and builds all three compute pipelines; the adapter limits far exceed the pass's needs. Modern water was not switched
  on in game there: the harness's synthetic wheel / key events do not reach the overlay.
- The first browser build failed: Emscripten defines `__SSE__` but has no `_mm_getcsr` / `_mm_setcsr`; the surface
  builder and resident scene now skip the float-mode control there.
- Linux (WSL, llvmpipe): the first link failed with undefined `std::vector<unsigned>::reserve`, `std::to_string` and
  similar from the gs objects. GNU ld's LLVM plugin picks the prevailing inline template definitions from the ThinLTO
  `ps2_runtime` bitcode and drops them although native (non-LTO) gs libraries linked after it reference them. Clang
  release links now use lld when it is available (`cmake/ReleaseMode.cmake`; the bootstrap installs `lld`). The game
  then runs the taa_tour to gameplay on the WebGPU renderer with the portable temporal AA.
- Same settings on Windows (no upscaling, TAA or ray tracing), still / turning / settled: WebGPU 207 / 285 / 373 fps,
  Direct3D 11 226 / 289 / 359 fps.

## Browser ray tracing verified (later on 30 October)

- With `modern_water`, `ray_shadows` and `ray_reflections` set in the browser's `/persist/drakan.ini` (IndexedDB), the
  Chrome player traced every world frame in Surdana: 185,582 opaque and 27,756 native water triangles, 1,211 model
  instances, shadows from the native lights, about 37 game frames per second (internal scale 2, VSync on). Contact
  shadows under the trees and tent show.
- Real key input from Playwright opens F1 in the browser (the earlier attempt dispatched synthetic DOM events).
- Two status bugs, on every platform: F1 opens over the pause menu, and the paused frames reset the "last presented"
  ray and water flags, so the status always said no ray-traced image had been displayed; both renderers now keep the
  last gameplay frame's result while a menu is up. The DLSS rows drive the portable temporal AA on WebGPU, so the NVIDIA
  DLSS credit showed there; it now needs the Direct3D 11 renderer. The reflections row is no longer labelled "DXR".
- The pass logs `[modern-water] first ray-traced world` once. It used to log nothing on success, and the settings
  overlay's status query creates the pass before the renderer does, so its start-up line never appeared either.
