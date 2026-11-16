# DLSS in Wartok Canyons: motion tracking and the posed capture in one pass (2026-11-17)

Brief: with DLSS quality Wartok Canyons was about 1.3 ms per frame slower than with DLSS off (11.1 against 9.8 ms),
the GS worker (renderer) and the EE (game thread) both near full. Cut the remaining DLSS-specific CPU work with the
same DLSS inputs (or proven exact), measured inside one run. Follows [perf-dlss-path.md](../2026-11-16/perf-dlss-path.md)
(section 5 named the levers).

Setup as there: the owner's ini (`build/review/perf-cpu-jitter/user.ini`: 3840x2160 borderless, scale auto = 8, vsync
0, uncapped, HD textures through `GSR_TEXDIR`), both mod packs, `GSR_MODERN_WATER=0`, `scene_ab_bench.py --cycle
video.dlss=quality,off`, background window. Scenes `ootf-canyons` and `surdana` (control). Ryzen 7 9800X3D, RTX 5080.
Two or three other agents built, tested and played on the machine all session; timing runs were taken with
`--quiet-games 0` (no other game, replay tool or compiler before the run; the bench waited up to 17 minutes for that).
One canyon run of the first series had a compiler beside it (marked below). Scratch: `build/scratch/` of the worktree
(benches, `toggle.py` for the old/new split, `scopes.py`, the micro-benchmark; not tracked).

## 1. What was changed

* **The GS worker tracks a segment in one pass** (`motion_history.cpp` `History::track(Key, topology, Source, ...)`,
  `GSR_MOTION_TRACK_FUSED`, default on). Before, `prepareObjectMotion` converted every vertex of a tracked segment
  into a `motion::Vertex` (`s/q`, `t/q`) in its own loop, and `track` then ran one loop for the finite checks, one for
  the correspondence checks and entries, and copied the vertices into the entry kept for the next world. Now `track`
  reads the draw's vertices itself (`motion::Source`: stride and float offsets of `DrawVertex`) and one loop, four
  vertices at a time with SSE2 (scalar tail and a scalar build without SSE), converts, checks and writes the entries;
  the converted vertices are written into a staging vector that is moved into the kept entry instead of copied. The
  lookups that do not depend on vertices (ambiguous, duplicate, previous entry) come first, as the first
  implementation made them before reading a vertex (the previous entry is not changed by anything in between).
* **Draws without object or mesh skip the ordinal map** (`d3d11_world.cpp`, same switch). Every world draw incremented
  `motionOrdinals[key]`, an `unordered_map` cleared each world, so most scenery draws allocated a node. A key without
  object or mesh is refused before the history whatever its ordinal, and an ordinal counts only its own key, so those
  draws now leave the map alone.
* **The EE gathers a posed character in one pass** (`drakan_scene_models.h` `posePositions`, `DRAKAN_SCENE_POSED_FUSED`,
  default on). Before: corner by corner with three nested early-exit loops, then `captureHashWide` over the finished
  triangles (64 bytes each, about 3,000 triangles for a Wartok). Now each triangle's three corners are copied and
  range-checked without branches (`|v| <= 1e7` is false for NaN and both infinities, which is the old `!isfinite(v) ||
  |v| > 1e7`) and its two 32-byte blocks go into the hash right away (`WideHash`: `captureHashWide` fed block by
  block, which `captureHashWide` itself now uses, so the identity is the same value). The pose is decided after the
  loop; a rejected one falls back to the full build as before.
* Measurement switch `GSR_DLSS_CANYON_TOGGLE=<s>` (`gsr::dlssCanyonToggledOff()`): all three run the first way in
  every second period of s seconds ("[dlss-canyon] now old|new at <us>"). `DRAKAN_SCENE_CAPTURE_PROFILE=1` now reports
  the posed part per character for each way (`posed_us`, `posed_first_us`).

Micro-benchmark (`build/scratch/t/bench.cpp`, 180 segments of 372 vertices per world, the history as in the game):
old about 8,400 to 11,500 cycles per segment, fused 4,400 to 6,000 (scalar fused without SSE: 11,700 to 12,100, the
conversion loop was not what cost; the six-field AoS loop did not vectorise).

## 2. Same DLSS inputs

* `GSR_MOTION_PREPARE_VERIFY=1` now also checks the fused form: the shadow history gets every segment converted one
  vertex at a time (`motion::convert`, the renderer's old loop) and tracked with the first `track()` implementation;
  result, entries, kept bytes and the kept vertex copy (epoch, topology, bytes) are compared. Owner's settings, DLSS
  quality, 20 s windows with `GSR_VELOCITY_SKIP_VERIFY=15`, `DRAKAN_SCENE_POSED_VERIFY=1`:

  | run | streams | tracked batches | velocity-skip pixels | fused poses |
  | --- | --- | --- | --- | --- |
  | Wartok Canyons, first build | 1,176,468, 0 differ | 128,351, 0 differ | 38 worlds, 1.16 M valid, 0 differ | 11,510, 0 differ |
  | Surdana, first build | 446,780, 0 differ | 75,214, 0 differ | 167 worlds, 1.34 M valid, 0 differ | 12,795, 0 differ |
  | Wartok Canyons, with the ordinal skip | 948,891, 0 differ | 105,183, 0 differ | 31 worlds, 0.79 M valid, 0 differ | 3,835, 0 differ |

* `DRAKAN_SCENE_POSED_VERIFY=1` builds every fused pose the first way too and compares acceptance, identity and every
  triangle byte (`[scene-posed]`).
* The ordinal skip: `motionOrdinals` is read only for `key.batch` in `prepareObjectMotion`; a key with `object == 0`
  or `mesh == 0` returns before `History::track` (the branch after the ordinal), and the ordinals of other keys do not
  depend on it. The verification counts above include it.
* Unit tests (runtime-tests, 206 of 206 pass): `motion_history` adds 7,200 random draws of 11-float vertices over 300
  worlds (texel and perspective coordinates, q of 0, NaN q, infinite and negative positions, jumps, clipped UVs, count
  changes, repeated identities, cuts) through the fused form, the convert-then-track first implementation and the
  built-in verification: 0 differ, 3,329 matched; `convert()` equals the renderer's loop. `scene_models` adds 64 poses
  through the fused and the first capture and a verifying one, including 1e7, -1e7, 1.0000001e7, NaN, both infinities
  and -0 in one corner: same snapshots byte for byte, 55 poses verified, 12 rejected.
* Pictures (owner's settings, DLSS quality, same start pose, first way against the new one):
  `build/review/dlss-canyon/canyons-sheet.png` and `surdana-sheet.png` in the main checkout (see section 4).

## 3. Gain inside one run

`GSR_DLSS_CANYON_TOGGLE=4` (old in every second 4 s period, the first 0.5 s after each switch left out), 32 s windows
per mode, 3 runs per scene. Median frame interval, old -> new, DLSS quality:

| scene | first build | with the ordinal skip |
| --- | --- | --- |
| Wartok Canyons | 11.44 -> 11.60 (+1.4 %, compiler beside it), 11.69 -> 11.53 (-1.4 %), 11.13 -> 10.97 (-1.4 %) | 11.47 -> 11.04 (-3.8 %), 11.35 -> 11.25 (-0.9 %), 11.02 -> 11.05 (+0.3 %) |
| Surdana (control) | 5.07 -> 5.15, 5.10 -> 5.12, 5.10 -> 5.20 | |

Per world (same phases): GS worker `drawObjectMotion` 0.71 to 0.77 -> 0.54 to 0.57 ms (first build) and 0.69 to 0.73
-> 0.51 to 0.53 ms (with the skip), `draw` -0.08 to -0.34 ms; EE posed characters 18 to 21 us -> 11.3 to 13 us each
(15 in view, about 0.1 ms per capture), Surdana 38 to 44 us -> 24 to 26 us. Surdana does not change: its frame is bound
by the VU1 worker (94 to 96 %).

Whole windows (both ways mixed), Wartok Canyons: DLSS off 10.0 to 10.3 ms, quality 11.0 to 11.3 ms. The GS worker
runs 96 to 98 % and the EE 94 to 98 % in quality: cutting 0.2 ms from one of them alone moves the frame little, which
is why the frame-time change stays within the run-to-run spread while the thread costs fall in every run.

## 4. Pictures

DLSS quality, first way (`GSR_MOTION_TRACK_FUSED=0 DRAKAN_SCENE_POSED_FUSED=0`) against the new one, separate runs at
the same start pose, in `build/review/dlss-canyon/` of the main checkout (`*-before.png`, `*-after.png`, and
`*-sheet.png` with the difference times 4):

* Wartok Canyons: same terrain, trees, river, bridge and HUD; differences only on the animated Wartoks and Arokh, who
  are in another phase of their idle animation in each run (mean absolute difference 1.70 levels, 6.2 % of pixels by
  more than 8, nearly all on the characters) and the river's moving texture.
* Surdana: Rynn is in another idle gesture and the camera stood slightly differently in the two runs (the whole
  picture is shifted by a few pixels, mean difference 8.4 levels); nothing missing or broken in either. Separate runs
  cannot give equal pictures here; the exactness rests on the verification counters of section 2.

## 5. What is still open

* The canyon still costs about 1 ms per frame with DLSS quality over off. GS worker per world (`scopes.py`, one run):
  `draw` 3.70 off against 4.92 quality, of it `drawObjectMotion` 0.05 against 0.5 to 0.6, `resolveTexture` +0.27,
  `textureCoherenceFlush` +0.11, `syncRT` +0.08, `drawSubmit` +0.08; `temporalResolve` 0.26 (of it `modelGuides`
  0.17, mostly `projectModelVertices` and the upload). The rest of `drawObjectMotion` is per-draw work for all about
  2,000 draws (the velocity-skip box test `motion_coverage::bounds`, the target check, the uploads of the drawn
  passes), not the tracking any more.
* EE: the capture is still 0.3 to 0.4 ms (`objects`; resident textures 0.14 ms, mostly the sliced refresh); the
  object-motion tracker (`drakan_object_motion::Tracker`) shows about 1 % of the EE in a sampled profile.
* Not built: moving the posed build to the renderer's builder thread (the EE would copy positions only), the model
  projection on the builder thread (it needs this world's camera, which arrives after the snapshot), uploading the
  default motion entries from a static buffer.
* Not measured: the foreground window, other DLSS modes, other resolutions, played routes.
