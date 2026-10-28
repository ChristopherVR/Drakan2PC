# Effects work off the GS worker (23 October 2026)

Follow-up to [the functional VU1 engine](vu1-functional-engine.md): with DLSS Quality, DLSS neural rendering (NR),
modern water and ray reflections (3840x2160, scale 4) Surdana ran at ~75 world FPS, the game thread waiting on the GS
worker ~60 % of the time. Measured from a worktree build (`build/final-game`, DLSS SDK and neural bridge on), still view
(`build/perf-review/still.txt`), `GSR_TEXREPLACE=0`.

## Changes

1. `ResidentScene::match` (d912493): candidates stored contiguously per 2048-unit cell and water class in insertion
   order with the terms the test needs; degenerate triangles dropped once; last cell lookup cached; the draw path
   reuses the water-layer match. A randomized test compares 40,000 queries with the original scan (identical).
2. NO_OVERWRITE rings for the model-motion and object-motion vertex buffers (02c0a05).
3. DXR pass on its own thread (9887ff8): the GS worker issues only the D3D11 side (input copies + producer fence,
   GPU-side wait on the consumer fence + output copy); packing, uploads, BLAS/TLAS recording and submission run on a pass
   thread that signals the consumer fence after `ExecuteCommandLists` (and after failures). Inputs are owned by the job.
4. Hash containers in `Models::prepare` (785049c).

## Finding: WRITE_DISCARD maps block once the GPU is behind

Moving the ray pass off the worker first made things worse: `modelGuides` went from 1.5 to 6.6 ms. Timing inside
`Guides::modelMotion` showed `Map(WRITE_DISCARD)` of the ~0.9 MB model-motion vertex buffer taking 5-10 ms per frame.
After a ring fixed it, the host sampler put ~45 % of the worker's samples in ntdll called from `prepareObjectMotion`:
the object-motion vertex buffer was mapped with DISCARD once per world draw. With both as rings the worker no longer
blocks there; it now waits in `neuralEvaluation`/`presentCall`, i.e. on the GPU.

## GS worker per frame (`GSR_PROFILE=5`, same session, interleaved runs)

| stage (ms/frame) | before (64bb666) | after |
|---|---|---|
| rayScene (on worker) | 3.85-3.97 | 0.11-0.18 |
| modelGuides | 1.54 | 0.80-0.88 |
| draw exclusive (incl. surface reconstruction) | 3.71-3.73 | 3.27-3.36 |
| pass thread (rayPack+wait+upload+build) | - | ~4 (runs in parallel, mostly idle) |

Runs at the time had no other game process on the GPU (p_base3 / p_new3, p_new4).

## Frame rate

Another agent ran the game and GPU tests on the same machine for most of the session; the GPU was frequently at 97-100 %
before our runs started, and the same binary measured anywhere from ~40 to ~82 FPS. Pairs that ran without a detected
second game process:

| config | before | after |
|---|---|---|
| installed settings, still view | 71.0 | 80.8 / 80.5 (two builds) |
| installed settings, turning-shots recipe | 40.9 | 81.9 |
| neural rendering off (`DRAKAN_DLSS_NEURAL=0`) | 80.2 | 116.6 |
| same, heavy GPU contention throughout | 63.7 / 32.6 (NR on) | 97.5 / 41.7 (NR on) |

Treat these as direction, not absolute numbers.

## The GPU is now the limit with neural rendering

Temporary GPU timestamp probes (D3D11 queries around the stages of `finalizeNeuralWorld`, D3D12 timestamps in the ray
command list; `build/gpu-probe.patch` in the worktree, not committed), in a ~41 FPS run:

| GPU segment (ms/frame) | |
|---|---|
| guide prepare + ray pass, as seen by D3D11 | 5.6 |
| of that, D3D12 ray work: dynamic BLAS (61k tris, FAST_BUILD) | 0.47 |
| animated model BLAS (3 meshes, 5.5k tris, rebuilt every frame) + TLAS | 0.73 |
| sky + trace dispatch | 0.10 |
| model guides | 0.06 |
| DLSS SR | 0.88 |
| DLSS NR | 11.1 |
| compose | 0.11 |

nvidia-smi showed 92-99 % utilization at 270-300 W during gameplay. NR alone is ~11 ms, so with NR on 300 FPS is out of
reach on this GPU regardless of CPU work; the remaining ray GPU cost (~1.35 ms) is mostly per-frame BLAS builds. Without NR
the worker CPU time is again the limit (VU1 and GS workers).

## Verification

- GS unit tests: 23 passed, `live` skipped (needs data); DXR test with the D3D12 debug layer, including a new
  32-frame asynchronous submit test that must reproduce the synchronous output exactly.
- `GSR_RAY_CPU_CACHE_CHECK=1` in game: zero mismatches; `GSR_WATER_LOG=1` scene counts identical in kind
  (174.58k opaque incl. 141,133 resident, 27,756 native water, 530 coverage triangles, 1,219 model instances) before/after.
- Renderer screenshots (frames 900/1500, and a turning recipe) match in content.

## Next

- GPU: NR cost dominates; options are a user decision (lower NR input resolution, NR off by default at 4K, or NR every
  other frame).
- Ray GPU: refit (ALLOW_UPDATE) the animated model BLAS instead of rebuilding with new allocations each frame; build the
  per-frame triangle BLAS from the previous frame's capacity.
- CPU: per-draw state churn in `Backend::Impl::draw` (~1 ms of D3D11 calls for ~190 draws) and the remaining surface
  reconstruction (`match`/`point` ~10 % of the worker).
