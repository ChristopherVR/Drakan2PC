# World frames no longer wait for the ray tracing compile

Follow-up to [title-freeze-ray-prewarm.md](title-freeze-ray-prewarm.md), "Still open": with a cold NVIDIA driver cache
the DXR pipelines take about 18 s to compile, and a world frame that wanted a ray effect before the prewarm thread
finished waited for it on the GS worker (`takeRayWater`), stalling the guest for up to the compile time (issue #1's
clean-machine stutter, outstanding-work section 4). The WebGPU backend compiled its four ray programs synchronously
on the renderer thread in the first world frame (or in the settings status query).

## Change

* `runtime/gs/src/ray_pass_loader.h` (new, pure): `PassLoader<T>` builds a pass on a background thread once and hands
  it over with a non-blocking `take()`; `acquirePass` is the render thread's per-frame step (start if needed, adopt a
  finished build, return null meanwhile). `GSR_RAY_PREWARM_DELAY_MS` holds the build (test aid, cut short at shutdown).
* D3D11: `takeRayWater` (the wait) is gone. `rayPass()` adopts the finished `RayWater`; the world frame computes
  `wantRayScene = wantRayEffect && rayPass()` before its early return, so while the pass is building the frame is the
  plain raster (no guide preparation, no ray dispatch). `waterRenderingAvailable` only starts or adopts, never builds,
  and answers "Ray tracing is starting"; `waterRenderingStatus` / `rayTracingStatus` say the same while the build runs.
  `GSR_SHADER_PREWARM=0` now only defers the start to the first frame that wants ray effects (still off-thread).
* WebGPU: `WgpuRay::compilePrograms` (the slow part) is split from the pass constructor (buffers, textures, the dummy
  upload, which stay on the renderer thread). New `rhi::Device::concurrentPrograms()`: true for the native wgpu-native
  device, false for the proxy, whose recorded batches belong to one thread (in the browser the pass is still created in
  the frame; the browser's pipeline creation does not wait for the compile). The compile starts at init and when a ray
  effect is switched on; `traceWorld` returns early (plain raster) until `acquireRayPass` adopts the result.
* Every ray pass access was audited: `waterRenderer` / `rayPass` are dereferenced only after a null check or inside
  `wantRayScene` (D3D11) / after `acquireRayPass()` (WebGPU); the status functions read a pass only when it exists. All
  of them run under the renderer lock, so adoption from the settings query and from the GS worker cannot race.
* Test `gsr_test_ray_pass_loader` (ctest `ray_pass_loader`): a fake pass whose build is released after 30 frames;
  every frame returns at once (slowest well under 50 ms) and draws without effects with "Ray tracing is starting",
  the effects come on with the first frame after the release and never switch off again, one build, one adoption; an
  unavailable pass (no DXR), a build that returns nothing, and shutdown during the delay (skips the build, no wait).

## Real runs (exe 4E2FEDF0..., `build/rt19/play`, outputs `build/rt19/runs/`)

German (`language = german` in the copied ini), `DRAKAN_GAME=d2`, New Game through `play_run.py` autopress, ray
reflections, shadows, modern water and `GSR_RAY_GI=1`, `PS2X_PERF=1`, `GSR_RAY_PREWARM_DELAY_MS=90000` (the build
finishes about 52 s into gameplay). The driver cache itself was not cleared (shared machine); the delay stands in for
the cold compile.

| Run | Gameplay before ready | At ready | After |
| --- | --- | --- | --- |
| D3D11 (`delay90`) | Surdana loaded at 29 s; 200 to 240 flips/s every second, no ray effects (`g45000.png`) | `[modern-water] ray tracing ready` at perf second 80; 230 -> 168 flips/s, no second below 143 | ray shadows / reflections / GI on (`g60000.png`), about 155 flips/s |
| WebGPU, Vulkan (`wgpu_delay90`) | 165 to 240 flips/s, no effects | `ready` at perf second 80, first ray-traced world in the next frame | compute ray tracing on (`g70000.png`), 60 to 80 flips/s |

No `[perf-stall]` in either run (it fires after 3 s without a flip), and no second of gameplay with fewer than 47
flips: no frame waited for the build. Previously the same world frame would have blocked for the whole build.

## Still open

* The first DXR pass after the switch creates the NR interop and its first submit took 172 ms on the pass thread; the
  frame rate did not dip below the steady ray-traced rate, but a one-off hitch there is worth a frame-time trace.
* A real cold-cache run (cleared NVIDIA DXCache on a clean machine) to confirm the 18 s compile now only delays the
  effects.
