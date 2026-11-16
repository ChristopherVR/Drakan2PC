# What the DLSS path costs, and the object-motion work cut (2026-11-16)

Owner's request: DLSS Super Resolution must be fast. Brief: find what the DLSS path costs per CPU thread and per GPU
pass, cut the biggest DLSS-specific costs, keep the image DLSS receives the same (or prove a change exact), measure
inside one run. Follows [gpu-dlss-finish.md](../2026-11-14/gpu-dlss-finish.md) (velocity skip) and
[perf-cpu-jitter.md](perf-cpu-jitter.md).

Setup: the owner's ini (`build/review/perf-cpu-jitter/user.ini`: 3840x2160 borderless, scale auto = 8, vsync 0,
uncapped, HD textures) with `GSR_MODERN_WATER=0`, DLSS switched live by `scene_ab_bench.py --cycle
video.dlss=quality,off`, HD textures through `GSR_TEXDIR`, both mod packs. Scenes: Surdana start (`surdana`) and Wartok
Canyons (`ootf-canyons`, the heavy one). Background window (harness default). Ryzen 7 9800X3D, RTX 5080. Two other
agents built and played on the machine during the session; every run below was taken when the bench saw no other game,
replay tool or compiler before or during it (the bench waited up to 730 s for that), except the baseline canyon pair
of section 1, which had a replay tool (one run) and a compiler (the other) beside it. Scratch: `build/dlss/` of the
worktree (benches `bench/`, tools `tools/`, picture `evidence/sheet.png`; not tracked).

## 1. Where the DLSS frame goes (before)

Frame time: median interval between world flips. CPU: ms per world frame of each thread (`[perf]`); `draw` and its
sub-buckets: GS worker ms per world from `GSR_PROFILE`; GPU: `GSR_GPU_PASS_TIMING` ms per world.

Surdana, quiet machine, toggle run `tg2` (old phases):

| | DLSS off | DLSS quality |
| --- | --- | --- |
| frame ms | 5.20 to 5.35 | 5.03 to 5.18 |
| busiest threads (% of one core) | VU1 worker 93 to 97, GS worker 73 to 75, EE 60 to 63 | VU1 worker 93 to 95, GS worker 89 to 90, EE 66 to 70 |
| GS `draw` (inclusive) | 1.04 | 1.90 |
| of it `drawObjectMotion` | 0.00 | 0.59 |
| `temporalResolve` (guides, model motion, DLSS call, compose) | - | 0.28, of it `surfaceWait` 0.11 and `modelGuides` 0.08 |
| EE scene model capture (`capture` in `[scene-pacing]`) | 0 | 0.44 |

The Surdana frame is bound by the VU1 worker in both modes, so DLSS quality and off take the same time there.

Wartok Canyons, quiet machine, `tg2` (old phases):

| | DLSS off | DLSS quality |
| --- | --- | --- |
| frame ms | 9.72 to 9.89 | 11.70 to 12.40 |
| busiest threads | GS worker 96 to 97, EE 91 to 94, VU1 worker 87 to 89 | GS worker 95 to 97, EE 81 to 89, VU1 worker 70 to 82 |
| GS `draw` (inclusive) | 3.50 | 5.28 |
| of it `drawObjectMotion` | 0.04 | 1.22 |
| `temporalResolve` | - | 0.46, of it `surfaceWait` 0.17 and `modelGuides` 0.17 |
| EE scene model capture | 0 | 0.49 |

Here the GS worker is the limit and DLSS quality was 1.9 to 2.5 ms per frame slower than off. (On 14 November, with
the velocity skip, the same pose measured 13.9 ms quality against 16.1 off; the machine and the build differ, so only
in-run comparisons are used below.)

GPU per world (same runs; the GPU is idle for most of the frame, `overlay` 2.6 to 9 ms):

| pass | Surdana off / quality | Canyons off / quality |
| --- | --- | --- |
| world | 0.38 to 0.47 / 0.29 to 0.33 | 1.29 to 1.42 / 1.29 to 1.40 |
| worldEntry (depth into the render rectangle) | - / 0.26 | - / 0.17 |
| guides | - / 0.05 | - / 0.04 |
| modelMotion | - / 0.04 | - / 0.06 to 0.07 |
| evaluate (DLSS) | - / 1.31 | - / 1.31 |
| compose | - / 0.33 | - / 0.32 |
| display | 0.08 / 0.35 | 0.08 / 0.43 |
| presentBlit | 0.24 / 0.12 | 0.24 / 0.15 |

DLSS adds about 2.1 ms of GPU work per frame (evaluate 1.3, compose, display, depth entry), none of which bounds the
frame at these settings.

### What `drawObjectMotion` was made of

Cycle counters around the parts of `prepareObjectMotion` (temporary, Wartok Canyons, 2,000 draws per world, 77
vertices per draw on average): about 3,000 cycles a draw, of which 2,400 tracking, 330 the velocity-skip box test,
100 the upload, 45 the target check. 9 % of the draws carry scope notes (characters drawn as segments, 1.2 tracked
segments of 372 vertices each), and those took 95 % of the tracking: about 15,000 cycles per segment, 40 cycles per
vertex in `motion::History::track` (per-vertex early exits, a zero-filled output then copied twice). In Surdana most
draws are untracked scenery, whose stream of default entries was zero-filled, scanned for a valid entry and then left
out by the velocity skip.

The `surfaceWait` was a second preparation of the model motion: at every world's end the EE sent the world's model
snapshot again (`shadowFrame.finish()`, for the native character shadow's ownership flag), the renderer queued a
second `prepareFrom` on its builder thread and then waited for it in `finalizeNeuralWorld`.

The EE capture (`DRAKAN_SCENE_CAPTURE_PROFILE=1`): Surdana 0.47 ms per capture (object loop 0.32, of it 5 posed
characters 0.21; resident textures 0.10; resource map 0.02), Wartok Canyons 0.62 ms (15 posed characters 0.31,
textures 0.14).

## 2. What was changed (commit `fe03387a`)

* **The previous-position stream is built only where it carries something** (`d3d11_world.cpp`
  `prepareObjectMotion`, `GSR_MOTION_PREPARE_FAST`, default on). A segment that does not match has the default entry
  `Previous{}` throughout; a matched one has a valid entry at every vertex. So the stream is written in place: tracked
  segments straight into it, default entries in the gaps, and a draw none of whose segments matched is not built at
  all (no zero fill, no scan for a valid entry; "may be valid" is whether a segment matched) and is written as default
  entries directly into the mapped upload only when its pass is drawn.
* **`motion::History::track` without early exits** (`motion_history.cpp`): the finite check and the correspondence
  check run over all vertices with branch-free flags, the entries are written into the caller's buffer (new
  pointer overload), and the batch is decided after the loop. The first implementation is kept as
  `trackReference` (`setFast(false)`, `GSR_MOTION_PREPARE_FAST=0`). Tracking went from about 2,400 to 1,070 cycles
  per draw in the canyon.
* **No second model snapshot at a world's end unless a ray pass reads it** (`drakan_dlss.cpp`). Only the ray passes
  read `SceneModelInstance::nativeShadow`; the end-of-world snapshot is now sent only when a ray effect is on and the
  native shadow claimed a caster (`DRAKAN_SCENE_SHADOW_RESEND=1` = always, as before). `surfaceWait` falls to 0.
* Measurement switches: `GSR_DLSS_PATH_TOGGLE=<s>` (`gsr::dlssPathToggledOff()`) runs both changes the old way in
  every second period, `PS2X_SCENE_PACING` now also times the capture, `DRAKAN_SCENE_CAPTURE_PROFILE=1` its parts.

## 3. Same DLSS inputs

* `GSR_MOTION_PREPARE_VERIFY=1` assembles every draw's stream the old way beside the fast one and keeps a second
  `History` that receives every call with the first `track()` implementation, comparing result, entries and kept
  bytes. Owner's settings, DLSS quality, 20 s windows: Surdana 551,266 streams and 92,811 tracked batches, Wartok
  Canyons 2,982,786 streams and 318,503 tracked batches: **0 differ**. The same runs with `GSR_VELOCITY_SKIP_VERIFY=15`:
  0 differing pixels in 206 and 98 compared worlds (1.6 M and 3.9 M valid pixels).
* Unit test `motion_history` (runtime-tests): 7,200 random batches over 300 worlds (moving, clipped, non-finite,
  negative depth, jumps, out-of-range depth ratios, count changes, repeated identities, missed worlds, cuts), fast
  against the first implementation through both output forms and through the built-in verification: 0 differ, 3,056
  matched.
* The end-of-world snapshot: with `DRAKAN_SCENE_SHADOW_RESEND=1 GSR_MOTION_PREPARE_VERIFY=1` both preparations of a
  world log a hash of the model-motion vertices and textures (`[model-prepare]`; `tools/resend_pairs.py` pairs a
  world's start and end snapshots, which straddle the epoch counter). Surdana 3,083 worlds, Wartok Canyons 863 worlds,
  all with moving models (19,236 to 29,508 model-motion vertices): **0 differ**. `prepareFrom` does not read
  `nativeShadow`, and the next world's preparation reads the previous snapshot only through the same fields.
* Pictures: `build/dlss/evidence/sheet.png`, DLSS quality before (baseline exe) and after (new path phase) at the same
  start pose: same terrain, trees, water and HUD; differences only on the animated Wartoks and Arokh's head (mean
  absolute difference 1.86 levels in the canyon, 0.61 in Surdana).
* Not compared: a temporal-dump comparison of the guide textures (`GSR_TEMPORAL_DUMP_*`) between the two paths. The
  replays (`gsr_replay`) have no engine world and no DLSS, so they cannot show this path; the proof is the in-game
  verification above.

## 4. Gain inside one run

`GSR_DLSS_PATH_TOGGLE=4` (the first 0.5 s after each switch left out), 24 s windows per mode, 3 runs per scene, quiet
machine (`tools/toggle.py`). Median frame interval per run, old -> new:

| scene | DLSS quality | DLSS off (control) |
| --- | --- | --- |
| Wartok Canyons | 11.76 -> 11.07, 12.40 -> 12.02, 11.70 -> 11.18 (-5.9, -3.1, -4.5 %) | 9.76 -> 9.90, 9.89 -> 9.66, 9.72 -> 9.80 |
| Surdana | 5.03 -> 5.10, 5.18 -> 5.09, 5.04 -> 5.10 | 5.28 -> 5.29, 5.20 -> 5.13, 5.35 -> 5.35 |

GS worker per world, old -> new (DLSS quality): Wartok Canyons `draw` 5.28 -> 4.49 ms (`drawObjectMotion` 1.22 ->
0.72, `temporalResolve` 0.46 -> 0.25, `surfaceWait` 0.17 -> 0); Surdana `draw` 1.90 -> 1.49 ms
(`drawObjectMotion` 0.59 -> 0.19 to 0.28, `temporalResolve` 0.28 -> 0.16, `surfaceWait` 0.11 -> 0). The canyon is
faster in 3 of 3 runs; Surdana does not change because its frame is bound by the VU1 worker (93 to 96 %), with or
without DLSS.

## 5. What DLSS still costs, and what is open

* Wartok Canyons with DLSS quality is still about 1.3 ms per frame slower than off (11.1 against 9.8). The GS worker
  stays at 95 % and the EE rose to 83 to 95 %: the remaining DLSS work on the GS worker is the tracking of the
  character segments (about 11,000 cycles per notes draw, 0.45 ms per world), the velocity passes and their state
  changes, and the model-motion guide upload (`modelGuides` 0.17 ms); on the EE the model capture (0.5 ms per world,
  posed characters 0.3 of it).
* Next levers, not built: the per-vertex conversion (`1/q`) and the history copy could be fused into one pass over
  the segment; the posed-character meshes of the EE capture (a template copy, a gather of positions and a hash per
  character per world) could be gathered without branches or built on the renderer's builder thread from copied
  positions; the GPU display pass (0.35 to 0.43 ms with DLSS against 0.08) builds the window picture with area
  averaging of the HUD regions and matters only once the GPU becomes the limit.
* Velocity passes drawn in the same pass as colour (MRT) were not built: the colour pass of most draws uses dual-source
  blending, which Direct3D 11 does not allow together with a second render target (`gs_shaders.h`, `PSMotion`), and
  after the velocity skip the remaining passes cost 3 ms per second on the GS worker.
* Not measured: the activated foreground window, DLSS balanced, performance and DLAA, other resolutions, modern water
  or another ray effect on (then the end-of-world snapshot is still sent whenever the native shadow claims a caster),
  played routes (only the start poses).
