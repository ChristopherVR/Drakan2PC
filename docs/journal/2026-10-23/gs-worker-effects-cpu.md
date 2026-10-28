# GS worker CPU with effects on (23 October 2026)

Follow-up to [effects off the GS worker](effects-gs-worker.md) and [render-resolution NR](nr-resolution.md). With DLSS
SR Quality, DLSS 5 neural rendering (NR), modern water and ray reflections at 3840x2160 (scale 4), Surdana was limited by
the GS worker once NR runs at render resolution (`DRAKAN_DLSS_NEURAL_RESOLUTION=render`): the worker was 99 % busy
(`DRAKAN_RUNTIME_PROFILE`: idle ~1 %). Goal: cut its CPU cost so a cheaper NR model can reach 110-130 FPS.

Measured from a worktree build (`build/final-game`, DLSS SDK and neural bridge on) against the main dist exe
(48c59fa), `build/perf-review/still.txt` and `steady.txt`, `GSR_TEXREPLACE=0`, `GSR_PROFILE=5`,
`DRAKAN_RUNTIME_PROFILE=1`. Per-frame numbers are `[gsr-profile]` exclusive ms divided by presented frames
(`presentCall` calls); "busy" is `(1 - workerIdle) / handoffs`.

## Where the worker time was (base, render-mode NR, quiet machine)

| stage (ms/frame) | base |
| --- | --- |
| profiled root | 6.8 |
| `draw` exclusive | 3.8 |
| of which surface reconstruction (`GSR_SURFACE_ASYNC=0` build: `drawSurface`) | ~3.5-4.0 |
| `modelGuides` (incl. `Models::prepare` 0.43) | 0.95 |
| `neuralEvaluation` | 0.64 |
| `presentCall` | 0.37 |
| worker busy | 9.4-9.5 |

`ResidentScene::match` itself was cheap (temporary counters: ~39k calls per frame, 1.9 candidate tests per call, 5.8k
hits); the cost was the per-triangle world conversion (`RasterToWorld::point`, double precision), the triangle setup
and the append, for ~100k candidate triangles per frame. Temporary timers in `prepareObjectMotion` (705 vertices per
draw): key 0.06 us, conversion 0.73, `History::track` 1.4, vertex-ring upload 0.52 per draw. Temporary timers in
`Guides::modelMotion` (17.5-21k vertices): projection 0.44-0.46 ms, ring copy 0.10-0.15 ms, the
`SwapDeviceContextState` scope only 5 us.

The host sampler (`PS2X_HOSTPROF`) took one sample round and then stalled until exit in every run with the DLSS/NR
bridge loaded (120 samples in 10 s); the stage profiler and temporary timers were used instead.

## Changes

1. `surface_builder.h`: the GS worker only classifies a world draw and copies its vertices into a job; a
   `SurfaceBuilder` thread converts to world space, matches/observes the resident scene and appends to the target's
   `SurfaceList`, in submission order and with the worker's MXCSR. Readers drain it first: `finalizeNeuralWorld` (before
   the water check and the ray submit), `noteSceneGeometry` (scene replacement), target eviction, device reset and
   effect toggles; the builder is destroyed before the targets and the scene. The camera cache is staged with the same
   key (camera serial, XYOFFSET) at every draw that reaches the block, and a vertex-free job starts a target's list for
   a new world, so lists and material projections are those of the synchronous path. `GSR_SURFACE_ASYNC=0` runs the
   jobs in place.
2. `CachedRasterToWorld`: memo of the last 4 converted vertices (strip/fan neighbours repeat two of three).
   `ResidentScene::match`: multiply-by-1/det prefilter with margins that only reject what the exact division test
   rejects (NaN falls through).
3. `d3d11_draw.cpp`: identical `cbPS` uploads are skipped (draw is its only writer); the last velocity pass no longer
   restores the colour pipeline (and re-uploads constants) when no alpha-fail pass follows.
4. Model motion: `Models::prepare` runs on the builder when the scene models arrive (the world-start camera packet),
   against the committed previous scene; `finalizeNeuralWorld` takes it after its drain if the scene and the
   committed previous scene are unchanged, otherwise prepares in place. `Models::prepare` uses sorted vectors instead
   of per-frame hash nodes and reuses its frame. `Guides::modelMotion` projects with SSE2 doubles in the original
   accumulation order straight into the vertex ring. `History::track` reuses retired vertex vectors.
5. `GSR_PROFILE` buckets: `drawSurface`, `drawObjectMotion`, `drawVertexUpload`, `drawSubmit`, `modelPrepare`,
   `surfaceDrain`.

## GS worker per frame (render-mode NR, same session)

| stage (ms/frame) | base | this branch |
| --- | --- | --- |
| profiled root | 6.77 / 6.87 | 5.70 / 6.24 |
| `draw` exclusive (all sub-stages) | 3.79 / 3.86 | 1.21 / 1.20 |
| `modelGuides` + `modelPrepare` | 0.95 / 0.96 | 0.17 / 0.17 |
| `surfaceDrain` (worker waiting for the builder) | - | < 0.01 |
| `neuralEvaluation` (mostly D3D12 completion wait) | 0.64 / 0.65 | 2.42 / 2.95 |
| worker idle | 0.8 % / 0.9 % | 5.8 % / 3.6 % |
| worker busy | 9.41 / 9.53 | 8.34 / 8.85 |
| busy without `neuralEvaluation` | 8.77 / 8.88 | 5.92 / 5.90 |

Same period, NR off (CPU exposed, SR + ray water/reflections on): profiled root base 9.9 / 9.9, synchronous builder
(`GSR_SURFACE_ASYNC=0`) 8.4 / 7.8, this branch 4.2 / 3.8 ms per frame. With the reconstruction in place the
`drawSurface` stage alone is 4.0-4.3 ms; on the builder it is the job copy, 0.22-0.35 ms.

The worker now waits on the GPU: `GSR_NR_PROFILE` shows `completionWait` 1.14 ms per evaluation (596 blocked waits in 5
s) and `GSR_NR_GPU_TIMING` 3.8-4.0 ms of NR GPU time at 1364x1364. The remaining worker CPU per frame is ~6 ms (about
2.6 ms GIF/record handling outside the profiled stages, `drawObjectMotion` 0.47, `drawVertexUpload` 0.26,
`drawSurface` 0.22, `presentCall`/`presentLatched` ~0.5 excluding waits, Streamline's own `slEvaluateFeature` 0.47),
i.e. a worker-only ceiling of ~150 FPS.

## Frame rate (interleaved, noisy machine)

Other agents ran builds, games and GPU work during the whole session; one run in each series was hit by contention
(22-40 FPS) and is not listed. World FPS:

| config | base | this branch |
| --- | --- | --- |
| render-mode NR, still view | 105.4, 104.2 | 112.3, 105.0 |
| render-mode NR, slow turn (`steady.txt`) | 77.1 | 83.7 |
| output-mode NR (GPU bound) | 84.5, 71.9, 65.9 | 82.5 |
| NR off | 116.0, 78.6, 69.6, 71.1, 60.6 | 77.4, 79.4, 78.3, 84.0 |

With NR on, the branch is GPU-bound at ~105-112 FPS (NR render-mode ~3.9 ms GPU plus SR, rays and the world), so the CPU
savings show mostly as worker idle and NR completion waits. Output-mode NR (6.85 ms GPU) is unchanged. With NR off the
branch runs with the GS worker ~37 % idle and the EE thread at 95-97 % (`ee_thread_cpu`); the base runs were
worker-bound (idle 1.5-2 %) and spread 60-116 FPS with the machine load.

## Verification

- GS tests in a DXR tree (`-DGSR_DXR=ON`, Release): 24 of 25 pass, `live` skipped (needs data). New: `surface_builder` (threaded
  builder equals the synchronous path over 137 worlds, 63k triangles, 20k water, with camera changes, two targets and
  vertex-free jobs; memo equals the plain conversion), `nr_guides` SIMD projection against the scalar reference (283
  accepted, 117 rejected inputs, bit-exact), `model_motion` explicit-previous/reused-frame form. The existing
  40,000-query cell matcher test covers the prefilter.
- In game (`--shots 900,1500`, `GSR_WATER_LOG=1`, `GSR_RAY_CPU_CACHE_CHECK=1`): 27 of 27 cache checks with zero
  mismatches (base, branch, and branch with `GSR_SURFACE_ASYNC=0`); scene counts 141,133 resident, 27,756 native water,
  530 coverage triangles, 1,219 instances in all three; model motion matched 5 (17,505 vertices) or 11 (21,429) as in
  the base; screenshots show the same scene (differences only in the idle animation phase and NR noise:
  branch vs synchronous builder mean |diff| 0.25-0.42 per channel).

## Remaining limits and risks

- GPU with NR on. The CPU side now has ~1.5-2.5 ms per frame of slack at 105-112 FPS.
- GS worker CPU (~6 ms): GIF parsing/record handling (~2.6), object motion (~0.5, `History::track` and the per-draw
  vertex conversion), per-draw D3D11 submission (~0.5 for vertex ring, state and draw calls), NR/present interop.
  A per-draw state cache needs every other context user to invalidate it (NR/SR scopes, temporal passes, ImGui); not
  done.
- EE thread with NR off (95-97 % busy).
- The builder adds a thread that is busy ~3-4 ms per frame. Its results are only read after a drain; a new reader of
  `RT::surface` or the resident scene must drain first (`surfaceBuilder->drain()`).
