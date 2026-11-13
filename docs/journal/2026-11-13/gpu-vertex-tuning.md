# Tuning the GPU vertex path: exact strip bounds, a tracker that rests, the GS worker's share (2026-11-13)

Continues [gpu-vertex-batch-draw.md](gpu-vertex-batch-draw.md) (the batch draw path, its performance tables and the
list of cheapest fixes) and [gpu-vertex-webgpu-draw.md](gpu-vertex-webgpu-draw.md). Written by the `tune` subagent on
top of local commit 2c7cfa45 (batch records drawn by the shader on Direct3D 11). Scratch under
`build/review/gpu-vertex2/tune` is untracked. The default is unchanged (`vertex_processing = cpu`,
`Capability::proven = false`).

## 1. What changed

### 1.1 Exact bounds for strip batches

* `program_draw::stripBounds` (`vertex_program_draw.h`): the method of `batchBounds` for strip records. Per record the
  constants become one prepared transform (the strip stage's x and y are the operations of `passTransform`; its offset
  is not multiplied by one first, which changes no bit), per vertex the place is estimated in doubles with the same
  error bound, and only the vertices whose interval can reach an extreme are evaluated exactly, with the place lanes
  of `vp::evaluate` alone (`exactTransformPlace`). `programBounds` picks the kind. Both backends now span the
  stand-ins over the exact bounds for both kinds (`d3d11_vertex_program.cpp`, `wgpu_vertex_program.cpp`: two and five
  lines). Before, strip draws took the scissor: a draw with every vertex outside its target or its scissor was
  executed where the packets skip it, and all scissor rows counted as drawn.
* The proof case (`test_vertex_records.cpp`, scene `outside`): strip and batch records into a 512 wide target beside
  the frame under a 640 x 448 scissor, right of the target inside the scissor, right of the scissor, below the scissor
  inside the target's rows and across the target's right edge, after an upload of 128 x 64 pixels into the target;
  then a magnified bilinear sprite on the frame samples the target. After every frame both renderers write their
  targets back (`flushToMemory`) and the whole local memory is compared (new `Sequence::memory`), so a row marked as
  drawn that the packets do not mark shows. Run under every mode and in two switching sequences (gpu every frame and
  every third). `--scene NAME` runs one scene (development).

### 1.2 No tracker work without a batch stage

* `vu1gpu::live::beforeRun`: under a control word whose stages contain no batch family (`strips` alone, or none) the
  tracker is not started; the strip stage is handed over by `intercept` without the tracker's question, and the VIF
  does not report (`following()` stays false). Cost per run: the word, the three checks of the diagnosis modes, and
  the strip check for an MSCNT. A word that switches the batch stages off while the tracker follows: what was deferred
  is written first (as when leaving `records`), the tracker keeps following until it is clean at a call and then
  rests, exactly as under the microprogram.
* Hand-over when the batch stages come on again: the tracker starts from the state it rested in, which is clean (it
  rests only when no lane is pending or unknown, and nothing defers while it rests). That is the state the existing
  restart from the microprogram relies on, at any run boundary: a resumed batch whose set-up the tracker did not see is
  safe because every earlier stage of it executed on real memory. No extra "mark unknown" step is needed.
* Counted: `runs ... not followed (no batch stage on)` in the `[vertex-path] batch tracker` line. Batch runs under a
  strips-only word are not counted as `stage off` while the tracker rests (they never reach it).
* Diagnosis: `DRAKAN_VERTEX_TOGGLE_STAGES=<list>` with `DRAKAN_VERTEX_TOGGLE=<n>`: the toggled frames keep the mode and
  use this stage list instead of the microprogram (`vpath::toggleWordOf`, `toggledTo`, `configure(word, n, other)`;
  also in `gsr_replay`). The toggle line now names the stages: `[vertex-path] frame N: now gpu, stages 0x01`.

### 1.3 The GS worker's share

Profiled first: `GSR_PROFILE` has buckets for the vertex-program work now (`cpu_profile.h`: `programRecord`,
`programEvaluate`, `programFlush` in the core's own report line; `programBounds`, `programUpload`, `programMotion` in
the Direct3D 11 backend's).

* **The largest item was not the hand-over but records the shader declined.** In the Ravenshold flight replay
  (`caps2/flight`, scale 2, `DRAKAN_VERTEX_EVAL=gpu`) 121,435 of 834,883 batch records were declined as `draw state`,
  every one of them into a 128 wide target (character shadows; counted with a temporary probe, removed), and evaluated
  on the CPU in the core: `programEvaluate` 1,057 ms of the core's 1,362 ms per 10 s window. The rule existed only
  because the bounds were the scissor. With exact bounds for both kinds it is gone: `stateTakes` takes any target from
  one page wide (`kAnyTarget`); `GSR_VERTEX_PROGRAM_DEBUG` bit 8 restores the old rule (`kWideTarget`, 512) for
  diagnosis. The `outside` scene draws strip and batch records (constant colour, with and without IIP) into a 128 wide
  target too, inside it, across its edges and outside its scissor, samples it on the frame and compares memory.
  After: 0 records evaluated on the CPU in that replay, `fallbacks: none`; core time per record 0.75 us (was 2.9 us).
* **Bounds cheaper.** One prepared transform per pass with its magnitudes (`FastTransform`, `estimateFast`); the
  terrain chain (grid, then transform) has a fast form too (`estimateGridFast`); both are the same operations in the
  same order as the general estimate, so the same bound. A vertex is kept for the exact pass only when it can still
  reach a limit when it is estimated (the limits only tighten); the kept ones are evaluated best first, so the exact
  extreme found rules out the rest (`finishExtremes`); a single Transform is evaluated exactly with its place lanes
  alone. The brute-force check counts the vertices evaluated exactly: recorded game vertices in batches up to 800,
  strips 19,588 to 14,937, batch path 82,571 to 64,369.
* Not done (see section 4): one Map per draw instead of four to six on Direct3D 11, absolute constant rows in the
  vertex data instead of the per-draw constant buffer, the strip record's register tail deferred like the batch one,
  the shape hash only where a motion consumer is on, the leaner batch record.

## 2. Proof

All counts exact. Scratch logs: `build/review/gpu-vertex2/tune/logs`, `tune/live/logs`, `tune/hashes`. RTX 5080,
shared machine (other agents' builds, replays and games ran throughout; at 08:30 the driver reset under
oversubscription, and every run in flight then was repeated, see section 4).

| What | Command | Result |
| --- | --- | --- |
| The strip fault, before the fix | `gsr_test_vertex_records --scene outside --scale 3`, strip draws over the scissor (code of 2c7cfa45 with the new scene) | 128 of 136 colour frames differ (80,251,180 pixels), local memory after write-back 7,359,600 of 140,509,184 words differ; mode `records` 0: the stand-ins, not the shader |
| The same with exact strip bounds | `--scene outside --scale 3` and `--scale 8` | 136 colour frames, 0 of 350,945,280 (scale 3) and 0 of 2,495,610,880 (scale 8) pixels differ; 24 depth pictures 0 differ; memory 0 of 140,509,184 words; 714 checks, 0 failed |
| Small targets on the shader | `--scene outside` (with the 128 wide target), scales 3 and 8, before and after the `stateTakes` change | 0 pixels and 0 memory words differ both before (CPU evaluation) and after; after: 36,864 shader vertices (13,824 of batch records) against 27,648, `draw state` fallbacks 64 (flat batch records only) against 320 |
| Records test, Direct3D 11, item 1 | `GSR_VERTEX_PROGRAM_WAIT=1 gsr_test_vertex_records` | 93 scene and mode combinations, 117 sequences; 55,131 runs as records (1,648,260 vertices, 10,116 batch records); 672,864 vertices by a shader (103,170 of batch records); 1,866 colour frames, 0 of 13,197,148,160 pixels differ; 354 depth pictures, 0 of 2,503,639,040; memory 0 of 421,527,552 words; 10,050 checks, 0 failed |
| Records test, WebGPU, item 1 | `gsr_test_vertex_records_webgpu --gpu-api vulkan` and `d3d12` | each: 569,694 shader vertices; 1,866 colour frames, 0 of 13,197,148,160 pixels; 354 depth pictures, 0 differ; memory 0 differ; 10,050 checks, 0 failed |
| Records test, WebGPU Vulkan, final code, alone | same | 583,518 shader vertices; 0 of 13,197,148,160 pixels, 0 depth, 0 memory words differ; 10,050 checks, 0 failed (the same binary crashed once with 0xc0000409 inside the whole ctest run; see section 4) |
| Bounds against brute force | `GSR_VP_RECORDS=<strip records> GSR_VB_RECORDS=<16 batch dumps> gsr_test_vertex_program_history` (limbs), batches up to 200 / 64 vertices (default) and up to 800 (`GSR_VP_BOUNDS_BATCH=800`) | strips: 31,193 batches (1,013,742 vertices) and 2,484 batches (1,015,201), 0 differ; batch path: 75,250 batches (2,447,600 vertices) and 6,044 batches (2,451,034), 0 differ; history values 0 differ in total (strips 3,045,645 pairs, batch path 7,353,168 pairs per library) |
| Neutral rules | `gsr_test_vertex_program_draw` | 59 checks, 0 failed (a 128 wide target taken; the old rule declines it; width zero declined) |
| Control word, stage toggle | `gsr_test_vertex_path` | ok (toggle word, periods between two words, 60 frames at period 3 switch 20 times, never leaving the mode) |
| Tracker policy with the rest | `vu_gpu_batch_policy_test` | 5,630,490 checks, 0 failed; the hook model now rests and wakes like `beforeRun` in every schedule, new schedules strips only, stage toggles of period 1 to 40, random stage sets: 1,368 switches, 1,799 runs taken while switching, 60 rests, 58 wakes, 704 runs not followed; memory equal wherever the tracker calls it real, packets equal |
| VU1 replay | `tune/live/run_all.sh 1` (16 recordings, one at a time) | 34,725 vertex runs, 0 differ; 57,528 records, 2,604,739 vertices, 0 differ; strips 708,250 vertices, 0 differ; the same 11 start-of-recording mismatches (run 0 or 1) as the reference agent's logs |
| Shadow mode | `DRAKAN_VERTEX_SHADOW=1`, Surdana recipe (`tune/shadow.sh`) | 6,777,160 runs; 2,484,349 records, 124,614,147 vertices compared, 0 differ; 1,071,423 vertex runs, 0 differ; 3,221,388 runs executed on the shadow memory, 0 with other packets, 0 lanes; 0 lanes changed without a reported write; 0 runs drawing an unknown lane |
| Capture hashes, records against shader, Direct3D 11, HD off | `tune/hash.sh final 1,3,8` (`replay_hash_check.py`, variants `cpu:DRAKAN_VERTEX_EVAL=records`, `gpu:DRAKAN_VERTEX_EVAL=gpu,GSR_VERTEX_PROGRAM_WAIT=1`) | strip capture (Surdana) 1,423 frames at each of 1, 3, 8: 0 differ (23,518,719 shader vertices, 0 by CPU evaluation, fallbacks none); flight capture 1,353 frames at each scale: 0 differ (45,522,723 shader vertices, 39,411,745 of batch records, 0 by CPU evaluation, fallbacks none) |
| cpu equals before | `replay_hash_check.py --reference gs-share/hashes/n_base --variant cpu --hd off --backends d3d11 --scales 1,3,8` on the five `perf-gpu/caps` | 15 comparisons, 0 differ (cave 5,558, flight 1,864, ootf 4,074, surdana 2,585, tab 3,984 frames each) |
| Live paused toggles, gpu against microprogram (`DRAKAN_VERTEX_TOGGLE=64`, 1280x960, DLSS off, 24 shots) | `tune/togall.sh surdana flight ruined` | Surdana 24/24 one picture (9 gpu, 15 microprogram), flight 24/24 (12/12), OotF Ruined Village 24/24 (14/10); `draw state` fallbacks 7, 34, 13 |
| Live paused stage toggles, all stages against strips only (`DRAKAN_VERTEX_TOGGLE_STAGES=strips`) | `tune/tog.sh stog-*` | Surdana 24/24 (10 strips, 14 all; tracker started 22 times, rested 22, 4,733,077 runs not followed), flight 24/24 (14/10; 330,170 not followed), Ruined Village 24/24 (10/14; 2,904 not followed); 0 runs that drew an unknown lane in all three |
| Whole `runtime-tests` | `ctest -C Release -j 1` in `tune/rt` | 184 of 186 passed (7 skipped: game data); `synthetic_webgpu` timed out (305 s) and `vertex_records_webgpu_vulkan` ended with 0xc0000409, both while the GPU was oversubscribed; rerun alone: `synthetic_webgpu` passed (75 s), `vertex_records_webgpu_vulkan` passed (row above) |
| Python tests, guard, format | `python scripts/run_tests.py`, `scripts/repository_guard.py`, clang-format 19 on every changed C++ file | 561 tests OK (3 skipped); 0 prohibited; clean; no em-dashes in the diff |

## 3. Performance

### 3.1 The GS worker in a replay (flight capture, Direct3D 11, scale 2, frames 300 to 1352, `gsr_replay --bench`)

The same binary, three configurations interleaved, three repeats each; renderer thread wall time per frame (median of
the frames) and its CPU time per frame. Machine: other agents' replays and games ran throughout.

| Configuration | Median ms per frame (3 repeats) | Thread CPU ms per frame |
| --- | --- | --- |
| shader, small targets taken (this patch) | 5.69, 5.73, 5.88 | 15.33, 15.33, 15.27 |
| shader, the old rule (`GSR_VERTEX_PROGRAM_DEBUG=8`: 128 wide targets on the CPU) | 7.51, 7.68, 8.39 | 16.38, 16.44, 16.98 |
| records evaluated on the CPU (`DRAKAN_VERTEX_EVAL=records`) | 29.05, 31.80, 31.95 | 29.86, 29.90, 30.97 |

The ranges do not overlap: taking the small targets is minus 1.8 to 2.5 ms of median renderer time per frame (minus
24 to 30 %) in this replay. Profile (`GSR_PROFILE=10`, one 10 s window each, the same replay): the core's
`programRecord` plus `programEvaluate` went from 1,362 ms for 472,379 records (2.9 us each, 1,057 ms of it CPU
evaluation) to 400 ms for 534,503 records (0.75 us each, nothing evaluated); `programBounds` is 15.5 us per draw
(1,016 ms for 65,369 draws, before the cheaper bounds; 41,088,894 vertices bounded over the replay, 934,651 of them
exactly, 2.3 %); `programUpload` 2.3 us per draw (149 ms for 63,406).

### 3.2 Live, owner's configuration (3840x2160 borderless, Direct3D 11, `owner4k-dlss.ini`, live cycle DLSS quality / off)

`tune/bench.sh` through `scripts/perf/scene_ab_bench.py` (its turn lock, one game, `--wait-quiet 60 --keep-busy`,
`GSR_GPU_PASS_TIMING=1`), 3 interleaved repeats, shader cache warm. **Every run started on a busy machine** (4 to 6
replay processes of other agents, GPU 85 to 100 % busy before most runs, once two games); the frame times are about
twice those of the batch-draw note under the same settings and the frame-time ranges are wide. Only the thread
columns are usable, and only as a direction. Median (range) over the repeats; CPU ms per world frame.

Surdana, the four corners (frame queue = `PS2X_FRAME_QUEUE=1 PS2X_READBACK=late`; `strips-fq` lost its quality window
in one repeat and `gpu-fq` its off window in one):

| DLSS | Variant | World fps | Frame ms | EE | VU1 worker | GS worker |
| --- | --- | --- | --- | --- | --- | --- |
| quality | cpu | 62.8 (57.6 to 67.9) | 15.01 (12.70 to 16.60) | 12.15 | 8.71 (8.45 to 8.85) | 8.25 (7.73 to 8.80) |
| | strips | 65.4 (64.4 to 68.3) | 15.19 (14.13 to 15.25) | 12.26 | 5.61 (5.37 to 5.76) | 8.77 (8.02 to 8.96) |
| | gpu | 62.2 (61.9 to 91.3) | 15.62 (9.83 to 15.77) | 12.48 | 5.82 (5.33 to 5.89) | 9.12 (8.59 to 9.48) |
| | cpu-fq | 63.1 (57.2 to 122.2) | 15.75 (8.07 to 16.95) | 7.45 | 8.06 | 7.18 |
| | strips-fq | 63.7 (63.3 to 64.2), 2 runs | 15.45 | 7.82 | 5.56 | 8.58 |
| | gpu-fq | 66.9 (58.2 to 90.7) | 14.77 (8.92 to 16.42) | 7.08 | 5.45 | 9.11 |
| off | cpu | 75.2 (67.9 to 78.2) | 11.52 (11.08 to 13.84) | 9.71 | 6.89 (6.87 to 9.30) | 5.66 (5.49 to 6.84) |
| | strips | 78.9 (78.7 to 80.6) | 11.93 (11.22 to 12.29) | 10.18 | 5.17 (4.93 to 5.51) | 6.55 (6.08 to 6.69) |
| | gpu | 73.7 (70.4 to 99.0) | 13.29 (9.30 to 13.66) | 10.81 | 5.25 (5.22 to 5.92) | 7.93 (6.99 to 8.04) |
| | cpu-fq | 76.5 (67.9 to 93.0) | 12.65 (9.46 to 14.04) | 6.47 | 8.40 | 6.32 |
| | strips-fq | 85.9 (77.9 to 135.9) | 11.53 (6.25 to 12.48) | 5.45 | 4.47 | 5.66 |
| | gpu-fq, 2 runs | 68.7 (65.0 to 72.4) | 13.78 (13.17 to 14.40) | 6.27 | 4.91 | 7.39 |

Order of the Flame, Ruined Village (no frame-queue variants):

| DLSS | Variant | World fps | Frame ms | EE | VU1 worker | GS worker |
| --- | --- | --- | --- | --- | --- | --- |
| quality | cpu | 39.7 (26.5 to 49.7) | 26.20 (18.37 to 33.43) | 21.39 | 8.57 | 9.62 (9.59 to 14.70) |
| | strips | 29.9 (23.9 to 46.0) | 33.33 (20.42 to 49.57) | 28.15 | 8.67 | 11.65 (11.21 to 12.03) |
| | gpu | 24.3 (23.8 to 31.4) | 49.90 (33.31 to 49.93) | 40.61 | 8.48 | 17.26 (16.26 to 17.74) |
| off | cpu | 64.8 (37.9 to 70.1) | 13.43 (13.42 to 23.82) | 13.61 | 8.55 | 8.88 (7.68 to 10.26) |
| | strips | 42.4 (34.8 to 55.5) | 21.88 (16.09 to 27.78) | 17.76 | 8.13 | 9.76 (8.54 to 10.95) |
| | gpu | 37.0 (33.0 to 51.7) | 26.57 (17.31 to 31.21) | 19.66 | 7.12 | 12.94 (12.64 to 13.21) |

GPU time per pass was recorded (`GSR_GPU_PASS_TIMING=1`) but is not tabled here (`tune/bench/*`). Not measured: `surdana-fight`, `ravenshold`, `tundra-c`, `arena-1e`, `ootf-volcanic`, the Ruined Village with the
frame queue, and strips-only with the code before item 2 (the base build's benchmark was stopped when the GPU was
oversubscribed and not repeated).

Reading, plainly:

* **No frame-time gain is established in any scene.** In Surdana every frame-time range overlaps every other; in the
  Ruined Village the `gpu` and `strips` frames are slower in the medians, but the runs show the pacing steps of a
  saturated machine (33, 50 and 67 ms) and the ranges overlap for `strips`.
* **Thread times, as a direction.** The VU1 worker gains under `strips` and `gpu` in Surdana (8.71 to 5.61 and 5.82
  ms with DLSS quality, ranges disjoint; 6.89 to 5.17 and 5.25 without DLSS, ranges disjoint) and nothing in the Ruined Village (8.57 / 8.67 / 8.48).
  The GS worker pays: Surdana plus 0.5 to 0.9 ms (`strips`) and plus 0.9 to 2.3 ms (`gpu`); the Ruined Village plus
  1 to 2 ms (`strips`) and plus 4 to 7.6 ms (`gpu`, ranges disjoint from `cpu`).
* **What `auto` should hand over** (when the project proves it): strip records only, on Direct3D 11 and on WebGPU; the
  batch families stay in the microprogram until the per-draw cost on the GS worker is cut (one Map per draw, no
  constant-buffer Map, the strip tail; the Ruined Village has about 515 program draws a frame). The batch path pays
  on the VU1 worker in outdoor Drakan scenes only and costs the GS worker everywhere.

## 4. Not verified

* Items 3 and 4 are only partly done. Not built: one Map per draw on Direct3D 11 (vertices, indices, pass rows and
  the previous stream in one ring with the three bind flags), absolute constant rows in the vertex data so the
  per-draw constant buffer is written only when XYOFFSET changes, deferring the strip record's register tail (three
  full evaluations per strip record taken by the shader, `vertexProgram`) like the batch tail, the shape hash only
  where a motion consumer is on, the inline range test before the VIF report, the leaner batch record. Measured
  sizes for the order: in the flight replay `programUpload` is 2.3 to 2.8 us per draw (0.2 ms per frame at about 76
  draws a frame; the Ruined Village has about 515 a frame, so about 1.2 ms there, inferred), the VIF report 0.3 to
  0.6 ms per frame (live note 7.3; under strips only it is now gone, see 3.2).
* The bounds are cheaper in exact evaluations (about a quarter fewer) but their time per vertex was not shown to
  fall: the unit timing (shortest of five calls, a busy machine) gave 30 to 35 ns per recorded game vertex in batches
  up to 800, before and after the fast forms alike, and the old code was not timed under the same load. Bounds per
  record for static records were not tried.
* The patches were split from one worktree (`tune/split.sh`): tune-2 alone (on tune-1) and tune-1 alone were not built
  or tested; only the final tree (all three) was.
* The WebGPU backend got the exact bounds for strips (the five lines in `drawVertexProgram`) and is covered by the
  records test on Vulkan and Direct3D 12 only; no WebGPU capture was hashed and WebGPU live play was not run.
* `vertex_records_webgpu_vulkan` crashed once (0xc0000409) inside the whole ctest run, at about the time of the
  driver resets; alone it passed twice (item 1 code and final code). Not shown that it is unrelated to this work;
  this work does not touch the WebGPU pipeline build (the new orchestrator reports a separate segmentation fault on
  the WebGPU strip landing tree after "pipeline 'gs vertex program, hw blend' ready in the background").
* The small-target change (records into 128 wide targets now drawn by the shader) is proven by the records test scene,
  the flight capture (0 of 1,353 frames differ at scales 1, 3 and 8, every shadow record now on the shader) and the
  live toggles; the Order of the Flame captures with records and DLSS inputs (motion targets) were not checked for it.
* Short stage toggles rarely let the tracker rest once batch stages have run (flight 6 starts, Ruined Village 1):
  under a word that starts with strips only it never follows; after batch stages it follows until it is clean at a
  call, which can take long (terrain slots no batch refills, as the live note found for the microprogram).
* No OotF capture with records was taken (the `caps2/ootf` folder is empty); the GS-worker profile is from the flight
  capture only.
