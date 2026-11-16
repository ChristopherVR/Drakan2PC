# Modern water with the GPU vertex path: the surface reconstruction on the GPU (2026-11-17)

Follows [perf-cpu-jitter.md](../2026-11-16/perf-cpu-jitter.md) sections 6 to 8. With modern water on,
`vertex_processing = gpu` offers no stage: the ray effects read every draw's vertices on the CPU. The exact stream-out
of the shader's vertices (`GSR_VERTEX_SURFACE=1`) works but its same-frame read-back serialises CPU and GPU. Goal: build
the ray scene's frame triangles of shader-drawn draws on the GPU, consumed by the ray pass without the CPU reading
them. Owner's settings (`build/review/perf-cpu-jitter/user.ini`: 3840x2160 borderless, scale auto = 8, vsync 0,
uncapped, HD textures, modern water on), Direct3D 11, RTX 5080, Ryzen 7 9800X3D. Evidence:
`build/review/gpu-water/` (untracked).

## 1. What the CPU water path consumes

Per draw (`gs_draw.inl`, the surface block of `Impl::draw`): a draw into a world target with depth test, a triangle
list, under an accepted world camera, is submitted to the `SurfaceBuilder` (`surface_builder.h`) with its state
(`DrawInput`: state registers, texture facts, the camera and its serial). For a strip batch of the GPU vertex path the
vertices exist only on the GPU (`submitProgramSurface`).

On the builder thread, `reconstructDraw` per triangle: un-projects the three raster vertices with the draw's camera
(`RasterToWorld::point`, double precision), drops invalid ones (z = 0, a failed inversion, an edge over 2,000,000),
looks for a resident triangle under it (`ResidentScene::match`: the centroid's 2048-unit cell, xz barycentric test,
plane distance within 32 units), and then either

* appends it to the target's list (`rt->surface`): water, or no resident match and an opaque or material-eligible
  draw (the eligibility uses the draw's minimum vertex alpha), with vertex lighting, opacity, UV = S/Q, T/Q, texture,
  alpha test and blend flags; or
* updates the resident material projection of the matched triangle (`ResidentScene::observe`: the largest-area draw
  wins per terrain cell and per layer), which the static world's attributes take at the next publication; or
* drops it.

At the world's end (`finalizeNeuralWorld`) the GS worker drains the builder, publishes the resident projection
(`shareStaticWorld`) and hands the list to `RayWater::submit`. On the `RayWater` pass thread the list is packed
(`RayWorldCache::append`: camera-relative positions, seven float4 attributes per triangle, face normals, grouped dry /
water) into per-frame BLAS (one per group) and the dynamic part of the material buffer, beside the static world and
the native models; D3D12 traces the water (and the other ray effects) and D3D11 waits for the result on the GPU.

Measured composition (`GSR_SURFACE_STATS=1`, new, Surdana start, `GSR_VERTEX_SURFACE=1`, one run):

| draws | draws | triangles | water | resident observes | appended | dropped |
| --- | --- | --- | --- | --- | --- | --- |
| strip batches (GPU vertex path) | 27,223 | 43.2 M | 0 | 243,112 (all before the player has control) | 42.7 M (98.9 %) | 0.25 M |
| every other draw | 174,083 | 34.6 M | 638,616 | 12.2 M | 11.8 M | 10.6 M |

So the shader-drawn strip batches give about 78 % of the appended frame triangles, never a water triangle, and after
the intro sequence never match a resident triangle: they are the characters, props and buildings (reflection
fallback geometry), not the terrain or the water.

## 2. Design

* **Stream** (exists): a strip batch's exact vertices (`VSProgramRaw`) go through stream output into a ring, once,
  before its first colour pass. With `GSR_VERTEX_SURFACE=gpu` the ring keeps a whole world (a draw that does not fit
  takes the CPU path; the next world gets twice the room), and it is also a shader resource.
* **Per draw on the CPU** (`submitGpuSurface`, `surface_gpu.h`): what does not depend on the vertices: the state terms
  of `submitDraw`, and the camera the builder would convert the draw with (the latest staged one; the draw still
  stages it and starts the target's list as the CPU path would). Draws in a resident-water state stay on the CPU path
  (evaluated with `evaluateBatch`, exact): their water flag decides on the CPU whether the pass runs.
* **Kernel** (`surface_gpu_hlsl.h`, Direct3D 11 compute, one group per draw, at the world's end inside
  `RayWater::submit` before the inputs are produced): the minimum vertex alpha, eligibility, un-projection, resident
  match and the seven attributes, exactly as `reconstructDraw`, `submitDraw` and `packFrameAttributes`. Float
  additions and multiplications are IEEE operations (`precise`, `D3DCOMPILE_IEEE_STRICTNESS`); division and square root
  are rounded correctly through double precision (needs the Direct3D 11.1 double extensions). One slot per streamed
  triangle; a triangle the CPU path would not append gets NaN positions (inactive in DXR).
* **Hand-over**: Direct3D 11 and 12 share textures, not buffers (`OpenSharedResource1` of a D3D12 buffer and a shared
  D3D11 buffer both fail with `E_INVALIDARG` on this driver), so the kernel writes an R32_UINT texture of 64 KiB rows
  (two, used in turn) created like `NrInterop`'s textures. The pass's existing producer fence orders it before the
  D3D12 queue reads it.
* **Ray pass** (`ray_water.cpp`): the texture's rows are copied into a buffer (`CopyTextureRegion`, the footprint pitch
  is the row), its attributes into the material buffer after all dynamic attributes, a BLAS is built from its positions
  and one more dry frame instance (group 0 mask, opacity 31) is added last, so no other instance index moves.

Nothing waits for the GPU on the CPU; the only new GPU work is the kernel, two copies and one BLAS build.

* **Resident observes** of streamed triangles: the kernel also writes each observe the CPU path would have made
  (slot, resident index found exactly as `match` finds it, texture, positions, UVs) into a record buffer; the renderer
  maps it without waiting a few worlds later, sorts by slot (the CPU path's order) and applies the records on the
  builder (`ResidentScene::observe`). The largest area wins per cell and layer, so the projections end the same; the
  worlds in between see the older projection. Draws of worlds whose ray pass did not run (warm start, bypass) are
  dropped with their observes (counted).

Nothing waits for the GPU on the CPU; the new GPU work is the kernel (two dispatches: each draw's minimum vertex alpha,
then one thread per triangle), two copies and one BLAS build.

Not carried over: the 250,000-triangle cap of one target's list. The kernel's triangles are traced from their own
acceleration structure: only exactly equal hit distances between them and other frame triangles could resolve
differently (none seen, section 3).

## 3. Proof

**Unit tests** (`gsr_test_surface_gpu`, new; `gsr_test_ray_water`, extended): 400 synthetic draws under four
perspective cameras and one unconfigured camera, a resident grid with water, random and resident-aligned triangles,
z = 0 and far-range vertices, flat and Gouraud colour, blend, alpha-test and texture-alpha states, zero and tiny Q:
58,202 slots, 22,303 appended by the CPU path, 0 differ bit for bit (positions and all 28 attribute words); 213
observes, the records equal the CPU path's calls (order, resident index, texture, positions, UVs). A 2,000-draw run of
the same test (297,774 slots, 111,589 appended): 0 differ. The ray water test now hands its dry frame triangles over
as GPU slots in a shared texture, with an inactive NaN slot between every two, three rounds (both textures in turn):
the output picture equals the frame-list picture bit for bit. Full runtime suite: 207 of 207 passed.

**In game, `GSR_SURFACE_GPU_VERIFY=1`** (every world's GPU slots read back and compared with `reconstructDraw` on the
same vertices; owner's settings, `vertex_processing = gpu`):

| run | worlds | slots compared | appended triangles | differ | observes left out that would have changed a projection |
| --- | --- | --- | --- | --- | --- |
| Surdana start | 415 | 14,751,590 | 14,657,372 | 0 | 0 |
| Surdana start (second run) | 1,205 | 42,831,874 | 42,557,477 | 0 | 0 |
| Surdana walk with turns | 867 | 32,850,723 | 32,636,321 | 0 | 0 (722 observes, none changing) |
| Surdana town square (water, villagers, buildings) | 2,047 | 17,484,118 | 17,452,749 | 0 | 45 (of 3,496) |

| Surdana town square, final build (late observes) | 2,995 | 24,366,871 | 24,331,105 | 0 | 4,271 observes applied late, 0 lost |

132.3 million slots, 0 differences. The town square is where streamed triangles (buildings, props standing on the
terrain) match resident triangles; in the first run 45 of their observes would have replaced a stored projection, so
the kernel now records them and the renderer applies them a few worlds late (final run: 4,271 records applied, none
past the 8,192-record capacity; 2,673 draws of worlds without a ray pass dropped, 10 draws that did not fit the
stream took the CPU path).

**Same-frame pictures, `GSR_SURFACE_GPU_COMPARE`**: the world's ray pass runs twice on one frame, first with the CPU
path's triangles of the streamed draws in the frame list, then with the GPU-built slots, both written at 4096x4096
RGBA16F (`scripts/perf/surface_gpu_compare.py`):

| scene | worlds | pixels that differ | largest difference |
| --- | --- | --- | --- |
| Surdana start | 3 | 0 | 0 |
| Surdana walk | 16 | 0 | 0 |
| Surdana town square, the stream and the villagers in view | 6 | 0 | 0 |

All 25 pairs bit-identical (every channel of every pixel). Looked at: the town square pictures show the stream with
the modern water reflections of the buildings and the villager, identical in both; the difference sheets
(`build/review/gpu-water/compare-*/compare/*-sheet.png`) are black.

## 4. Timing

`scripts/perf/scene_ab_bench.py --foreground --repeats 3 --cycle video.vertex_processing=cpu,gpu --variant old
--variant new:GSR_VERTEX_SURFACE=gpu --lead 20 --settle 25 --window 10`, owner's ini, modern water on. `old` is the
shipped behaviour (under `gpu` the capability offers no stage while the ray effects read vertices, so the runs stay in
the microprogram); `new` is `GSR_VERTEX_SURFACE=gpu`. The settle of 25 s covers the strip shaders' compile (17 to 25 s
in every run here: they miss the disk shader cache). CPU columns are milliseconds per world frame. Evidence:
`build/review/gpu-water/bench/`, `bench-*.txt`.

**Surdana start** (quiet for 4 of 6 runs; a compiler appeared during `old-1` and `new-2`):

| mode | variant | world fps | frame ms | 1 % low ms | 0.1 % low ms | EE | VU1 | GS | all other |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| cpu | old | 118.4 | 8.42 | 9.4 | 11.9 | 4.81 | 5.55 | 5.23 | 23.11 |
| cpu | new | 117.9 | 8.46 | 9.6 | 11.0 | 4.74 | 5.76 | 5.59 | 23.94 |
| gpu | old | 118.2 | 8.43 | 9.4 | 11.0 | 4.66 | 5.39 | 5.19 | 23.31 |
| gpu | new | 112.2 | 8.90 | 9.2 | 10.1 | 4.53 | 3.59 | 3.77 | 18.74 |

Pairs old -> new under `gpu`: 8.34 -> 8.85, 8.43 -> 9.39, 8.45 -> 8.90 ms (+6.2 %, 3 of 3 slower). CPU per world frame
falls by about a fifth (38.6 -> 30.6 ms of all threads together: VU1 worker -1.8, GS worker -1.4, the builder and
ray pass threads -4.6) and the 1 % and 0.1 % lows are better, but the frame is about 0.5 ms longer: it is GPU-bound
at these settings (GPU total 8.4 ms), and the `guides` GPU pass (the ray pass and what precedes it on the D3D11
queue) grows from 2.90 to 3.33 ms per world. The kernel takes about 0.17 ms for a world's 35,000 slots (0.27 ms for
58,202 in the unit test); the rest is the stream-out pass of each strip batch and the D3D12 copies and build. The
first kernel (one thread group per draw: about two dozen groups a world) cost +47 % (8.37 -> 12.34 ms); one thread
per triangle fixed that (commit "Run the GPU surface kernel one thread per triangle").

**Surdana town square** (water, buildings, villagers): not conclusive. The machine was shared (a replay tool during
`new-1`, the game window in the foreground 0 to 35 % of the samples, frame rates of one variant from 71 to 103 fps
between runs). The cleanest run, `new-0`, gave cpu 102.7 fps, gpu 99.1 fps (-3.5 %), the direction of Surdana.

So at the owner's 4K settings the GPU path under modern water is now exact and much cheaper on the CPU, but not
faster: it moves about 8 ms of CPU work per world frame onto about 0.5 ms of a GPU that is already the limit. It stays
opt-in (`GSR_VERTEX_SURFACE=gpu`); the shipped behaviour is unchanged.

## 5. What remains

* **GPU cost** (why it does not pay at 4K): the stream-out pass draws each strip batch a second time through
  `VSProgramRaw`; a geometry shader on the first colour pass that both rasterises and streams out would remove it. The
  kernel's double-precision divisions (about 40 per triangle) could use a single-precision estimate with an exact
  double-precision correction instead of `ddiv` (the unit test is the proof harness). Building only the active slots
  (a compaction pass) would shrink the BLAS build.
* A bench at a lower scale (CPU-bound, where the GPU path already won with water off) was not taken; that is where
  this should beat the CPU path first. The town square again on a quiet machine.
* Ravenshold flight hands over no strip records after the player has control, so it does not exercise the path; a
  second water scene with many shader-drawn draws is still to be found.
* WebGPU has no kernel (`kCaps.gpuSurfaces` is Direct3D 11 only).
* Streamed draws of worlds whose ray pass does not run are dropped with their resident observes (counted in the
  `[gsr] gpu surfaces` line); the CPU path would have applied them.
