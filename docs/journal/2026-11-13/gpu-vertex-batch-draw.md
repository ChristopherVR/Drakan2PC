# The batch path drawn by its vertex shader on Direct3D 11 (2026-11-13)

Continues [gpu-vertex-batch-stage.md](../2026-11-12/gpu-vertex-batch-stage.md) (the generated batch stage and its input
layout, section 4) and [gpu-vertex-live-batch.md](../2026-11-12/gpu-vertex-live-batch.md) (batch records at run time,
evaluated on the CPU). Written by the `bdraw` subagent of the GPU vertex work, on top of the strip draw path (local
commit 328a0df1). Scratch under `build/review/gpu-vertex2/bdraw` is untracked. The default is unchanged
(`vertex_processing = cpu`, `Capability::proven = false`).

## 1. What was built

* **Core** (`gs_core.cpp`). `vertexBatch` has a GPU branch. `batchOnGpu` decides per record, every decline counted
  once by reason (and also in the new `Counters::batchFallback`, printed as "of them batch-path records"): the control
  word, the backend (`vertexPrograms(why, batchPath = true)`: shader ready, arithmetic, features), a record that is
  only the closing tag, `program_draw::stateTakes` (triangle strips, Gouraud, no FST, target at least 512 wide) and
  `program_draw::batchTakes`: per vertex, the last pass that writes the position or Q must be a Transform or Project
  pass that cannot set the skip bit (`vp::passNeverSkips`); a vertex that keeps its raw position or whose Q a later
  texture generation replaces is `draw state`, a possible skip bit `skip bit`. Taken records are queued like strip
  records: same GS state shares one backend batch, strip and batch records never share one (different vertex
  programs: the batch is flushed at a change of kind). The record's `BatchVertex` rows are copied with `slot` replaced
  by the row of its first pass (`sel.w`), its passes verbatim, its strips as the triangle list (i, i + 1, i + 2) of
  32-bit indices. Per-record object-motion notes work as for strips (`programNotes`).
* **Registers and vertex queue after a record**, as the packets leave them: the last strip's last two vertices queued
  (a following tag without PRIM continues the strip) and, after the closing empty tag, PRIM written again (queue
  empty, registers of the last vertex). These one or two vertices are not evaluated at once: `settleBatchTail`
  evaluates them when something can see them (GIF data that is not a record, also data of a tag opened earlier on any
  path; a strip record; a batch record the shader does not draw; an outside register write; the end of the program
  batch, while the passes are still there). A following batch record taken by the shader replaces the pending tail,
  so a run of batch records costs no CPU evaluation at all.
* **Exact bounds** (`program_draw::batchBounds`). `Impl::draw` takes a draw's bounds from its vertices: whether the
  draw is skipped (outside the scissor), the rows of the target it marks as written, the destination copy. The strip
  path's stand-ins span the scissor. For batch records that is not exact: in the Ravenshold flight capture the world
  target is 512 wide, draws entirely outside the scissor were skipped by the CPU path (no texture resolved, nothing
  marked) and executed by the stand-ins, and 473 of 2,088 frames differed at scale 3 (Arokh's wings, up to 6 levels;
  `GSR_VERTEX_PROGRAM_DEBUG=2`, batch records on the CPU, gave 0; with exact bounds 0). Evaluating every vertex exactly
  costs about what the shader saves, so each vertex's fixed-point place is estimated in doubles with an error bound
  (VU truncation and IEEE rounding both within 2^-20 relative per chain, plus one unit for the truncation to an
  integer), and only the vertices whose interval can reach an extreme are evaluated exactly; a vertex the estimate
  cannot bound (a register that is not an ordinary float, a value near the largest float, a fixed-point place that
  saturates or wraps, a second grid or position pass after the transform) is always evaluated exactly. Flight replay:
  29,773,373 vertices bounded, 824,557 of them evaluated exactly (2.8 %). The strip path keeps its scissor stand-ins
  (not changed; see "not verified").
* **Direct3D 11** (`d3d11_vertex_program.cpp`). Two vertex programs, each with its velocity twin, each compiled on two
  background threads at the first record of its kind: strips (`VSProgram`, `VSProgramMotion`) and the batch path
  (`VSBatch`, `VSBatchMotion`, input layout of batch-stage section 4, the previous stream in slot 1). The constants
  ring holds the batch's pass rows (then the previous position passes); the vertex ring 64-byte batch vertices; the
  index ring 32-bit indices. Every Map and CreateBuffer is checked; a failure turns both kinds off for the session
  (`internal`) and the batch is drawn by the exact CPU evaluation instead. A compile failure turns only that kind off.
  The in-draw CPU fallback moves its vertices from the shader counter to the CPU counter (`countedVertices`).
* **Capability follows the compile.** `vertexPathCapability().shaderStages` names the kinds whose shader is ready or
  not yet started; a kind that is compiling (or failed) is left out, and `drakan_vertex_path` re-applies the control
  word when that changes (polled every 250 ms under `gpu` / `auto` only; nothing under the player's `cpu`). So while
  the batch shader compiles (29.5 s cold in the game, 20.0 s in the records test, 5 ms from the shader cache) its runs
  stay in the microprogram instead of being evaluated on the CPU. Before this, a cold first start evaluated 649,479
  records (strips and batch) on the CPU (counted `not ready`) in one 21 s Surdana run, at the frame rate of the
  `records` mode.
* **Shaders** (`vertex_program_hlsl.h`). The motion helpers (`vpNative`, `vpDivideNearest`, `vp_raster`) moved into
  the shared head so both paths use them; `vpHistoryOf` is the one copy of the history formula (`vpHistory` for strips
  and `vbHistory` for the batch path call it). `VSBatch` now converts the depth with `vpDepth` (it used `(float)z`,
  which is not the CPU path's conversion on every adapter: the strip work measured WARP rounding differently from
  2^31 up). `vbPreviousPlace` masks bit 31 of the previous mask, which is the "has a previous state" flag.
* **Motion vectors** (`program_motion.h`, `vertex_program_draw.h`). `ProgramHistory::trackBatch` keeps, per record
  under the same key rule as strips, only what the position depends on: the record's position passes (grid, flat grid,
  transform, project) in order, and per vertex its raw position row, its mask over those passes and its grid values
  (28 bytes a vertex). `program_draw::trackBatchPath` walks the batch with its scope notes; the previous passes follow
  the batch's own rows in the constants table. Topology value: scope hash, GS state, the record's shape (strip lengths,
  indices, pass kinds and masks), raw texture rows, the constants of texture generation and texture matrix passes,
  and the raw position of vertices whose coordinates are generated from it (the CPU path matches processed texture
  coordinates; a moving texture matrix therefore has no history on either path). Strip and batch entries never match
  each other.
* **Features.** Terrain detail (and its debug view) shades per pixel from camera and depth and needs no vertex: it no
  longer forces the fallback, for strips or batch records. Material detail never read vertices. The cliff side
  projection (`terrain_cliffs`) and the terrain audit read the first triangles of the draw on the CPU
  (`prepareTerrain`: texel density from up to eight triangles of `DrawVertex` x, y, q, s, t): they keep the counted
  `feature` fallback. What it would take: the core evaluates the first up to eight triangles of each batch draw exactly
  and hands them to `prepareTerrain` beside the stand-ins (a `gs_draw.inl` hook change), and the draw partition must
  equal the CPU path's. Ray effects keep the fallback.
* **Diagnosis switches.** `GSR_VERTEX_PROGRAM_DEBUG` bit 2: batch records never drawn by the shader; bit 4: strip
  records never (`stage off`). The `[vertex-path]` counters line names shader vertices of batch records and the batch
  records' fallbacks. With `DRAKAN_VERTEX_LOG`, the backend prints the bounds statistics at exit.
* **A crash fix outside the vertex path** (`d3d11_draw.cpp`, `beginVertices` / `endVertices`): the draw vertex
  buffer's CreateBuffer and Map results were not checked, and a failed Map (out of video memory, a removed device)
  wrote through a null pointer. At landing this was dropped in favour of the Direct3D 11 device-loss change
  (`4189da39`), which checks the same two calls through its `deviceOk()` / `map()` helpers (one check per call
  site; a removal sets the device-loss latch, any other failure hands the shared draw a CPU copy and skips the draw).
  The vertex-program Maps here go through `map()` as well, and their failure paths call `probeDevice()`.

## 2. Proof

All counts exact. Scratch logs: `build/review/gpu-vertex2/bdraw/logs`. RTX 5080, shared machine (other agents'
replays and games ran during most of these; noted where it matters).

| What | Command | Result |
| --- | --- | --- |
| Neutral rules | `gsr_test_vertex_program_draw` | 57 checks, 0 failed (17 new: `batchTakes` with transform, project, no transform, no pass, Q replaced, a skipping transform; `trackBatchPath` over two scopes and four worlds: only position passes and position inputs kept, the previous table row, a moved texture row or texture matrix does not match, strip and batch entries never match) |
| History value, batch chains | `GSR_VP_RECORDS=<strip records> GSR_VB_RECORDS=<16 batch dumps> gsr_test_vertex_program_history` (RTX 5080) | batch path: 2,451,056 vertices (2,330,816 recorded in 57,528 records) x 3 offset / raster settings = 7,353,168 vertex pairs (6,867,559 with a valid history, 5,184,138 with a depth ratio other than 1), 0 differ, integer limbs and doubles; strips 3,045,645 pairs, 0 differ (the moved helpers) |
| The same on WARP | `GSR_VP_WARP=1`, same records | identical totals, 0 differ, both libraries |
| Exact bounds | same test, CPU part | 75,250 batches of 1 to 64 vertices (2,447,600 vertices, 551,550 of them evaluated exactly), estimate-and-refine against every vertex evaluated: 0 differ. Two faults this found during development (a second grid pass reading its height from the first one's float; products that saturate in the VU and not in doubles) are guarded in the code |
| Records test | `GSR_VERTEX_PROGRAM_WAIT=1 gsr_test_vertex_records` (Direct3D 11) | 52,683 runs as records (1,560,132 vertices; 9,300 batch records with 498,576 vertices); 589,920 vertices drawn by a shader, 75,522 of them of batch records; "backend has no path" 0; 1,464 colour frames, 0 of 10,354,032,640 pixels differ; 288 depth pictures, 0 of 2,036,858,880; 7,872 checks, 0 failed; fallbacks over the run: draw state 129, skip bit 2,232 (the test's records built to be declined) |
| Captures with records, Direct3D 11, HD off | `replay_hash_check.py --variant cpu:DRAKAN_VERTEX_EVAL=records --variant gpu:DRAKAN_VERTEX_EVAL=gpu,GSR_VERTEX_PROGRAM_WAIT=1 --scales 1,3,8 --hd off` | Surdana walk 2,269 frames, arena (interior) 1,484, Ravenshold flight 2,088, Order of the Flame Ruined Village (30 s walk) 2,528: 12 comparisons, 25,107 frames, 0 differ. Shader vertices of batch records in one gpu replay at scale 3: 13,839,518 / 6,409,921 / 34,573,769 / 37,746,588. Surdana and arena were replayed before the exact bounds existed; flight and Ruined Village with the final code |
| The flight fault and its fix | flight capture, scale 3 | before exact bounds: 473 of 2,088 frames differ (scales 3 and 8; scale 1: 0); batch records forced to the CPU (`GSR_VERTEX_PROGRAM_DEBUG=2`): 0; strips forced to the CPU: 10,539 pixels still differ in frame 1292; draw log: the CPU path skipped draws whose vertices are all outside the scissor, the stand-ins executed them; with exact bounds: 0 of 2,088 at 1, 3 and 8 |
| Live paused toggles (`DRAKAN_VERTEX_EVAL=gpu DRAKAN_VERTEX_TOGGLE=64 DRAKAN_FOLIAGE_WIND=0`, 1280x960, DLSS off, 24 shots, final build) | `tog.sh`, `cmpshots.py` / `cmpall.py` | Surdana 24/24 one picture (10 gpu, 14 microprogram), fight 24/24 (13/11), Arokh flight 24/24 (13/11), Lair arena 24/24 (9/15), Catacombs 24/24 with the torch flame box 35 x 81 px masked (`maskcmp.py`; 16/8), OotF Ruined Village 24/24 (11/13), OotF Volcanic 24/24 (16/8). Batch-path shader vertices per run 27.5 to 80.3 million |
| Terrain and material detail | `windowed.ini` with `terrain_detail = 1`, `material_detail = hard`, the frozen HD pack plus `materials` (`GSR_TEXDIR`), Surdana toggle | 24/24 one picture (12 gpu, 12 microprogram); no `feature` fallback; terrain detail changes 806,955 of 1,228,800 pixels against it off, material detail 898,708 against it off with the same pack (55 material maps, 186,215 draws shaded) |
| DLSS inputs, paused, fixed jitter sample (`windowed-dlss.ini`, `GSR_TEMPORAL_DUMP_EPOCHS`, `GSR_TEMPORAL_JITTER_INDEX=3`) | `dlss.sh`, `cmpdumps.py`, `groups.py` | Surdana, 12 worlds (6 gpu, 6 microprogram): world colour, entry colour, prepared colour, depth and motion, object motion, model motion: every world identical to the reference (0 differ in 77 comparisons). Flight, 30 worlds: colour, depth, entry and prepared colour one group; motion targets one group of 27 worlds of both paths and three single worlds (two microprogram, one gpu), so the variation is not the path |
| GPU memory beside a 4K game | `crash.sh`: owner's ini (3840x2160, DLSS quality, gpu path) and two scale-8 shader replays of the flight capture | both replays and the game ended normally; replay counters identical to the unloaded replay; dedicated memory up to 15,953 of 16,303 MiB, then `nvidia-smi` reported nonsense values (2^44) 21 times; 8 GPU jobs on the machine (other agents' included). The 0xC0000005 was not reproduced |
| Python tests, guard | `python scripts/run_tests.py`, `scripts/repository_guard.py` | 561 tests OK (3 skipped); 0 prohibited |
| `runtime-tests` whole (before the final bounds code) | `ctest -C Release -j 2` | 180 of 181 passed (7 skipped: game data); `vertex_records_webgpu_d3d12` aborted with 0xc0000409 under load (the WebGPU backend has no vertex-program path; see not verified) |

## 3. Performance

`scripts/perf/scene_ab_bench.py` (its turn lock; one game), owner's `owner4k-dlss.ini` (3840x2160 borderless, DLSS
quality, Direct3D 11) with the live cycle `video.dlss=quality,off`, 3 interleaved repeats, variants
`cpu:DRAKAN_VERTEX_PROCESSING=cpu`, `gpu:DRAKAN_VERTEX_EVAL=gpu` (every stage the backend draws), `strips:...,
DRAKAN_VERTEX_STAGES=strips`; shader cache warm (`GSR_SHADER_CACHE_DIR`). Median (range); CPU ms per world frame.
Conditions: another agent's benchmark shared the turn lock, and every run started "busy" (two to three replay or
compiler processes of other agents); Surdana's ranges are wide because of one disturbed repeat per variant.

| Scene, DLSS | Variant | World fps | Frame ms | EE | VU1 worker | GS worker | GPU world ms | GPU total ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Surdana, quality | cpu | 133.8 (109.7 to 144.2) | 6.90 (6.37 to 8.24) | 6.58 | 5.89 | 5.81 | 0.51 | 6.94 |
| | gpu | 144.5 (97.7 to 145.6) | 6.50 (6.36 to 9.63) | 6.57 | 3.81 | 5.82 | 0.37 | 6.92 |
| | strips | 146.8 (89.0 to 159.3) | 6.28 (5.90 to 9.42) | 6.35 | 4.42 | 4.88 | 0.36 | 6.82 |
| Surdana, off | cpu | 143.5 (96.6 to 163.7) | 6.01 (5.71 to 8.74) | 6.21 | 5.57 | 4.78 | 0.56 | 6.85 |
| | gpu | 160.4 (108.0 to 164.3) | 5.65 (5.58 to 8.66) | 5.89 | 3.65 | 5.36 | 0.57 | 6.30 |
| | strips | 163.3 (129.8 to 180.2) | 5.59 (5.19 to 6.37) | 5.40 | 4.04 | 3.86 | 0.51 | 6.04 |
| Surdana fight, quality | cpu | 94.9 (94.6 to 95.4) | 9.91 (9.91 to 10.00) | 6.90 | 9.72 | 10.01 | 1.31 | 10.53 |
| | gpu | 79.2 (74.9 to 89.6) | 11.67 (10.87 to 12.98) | 7.10 | 5.79 | 11.56 | 1.41 | 12.53 |
| | strips | 92.9 (80.5 to 100.5) | 10.63 (9.91 to 11.77) | 8.23 | 7.30 | 8.88 | 1.18 | 10.71 |
| Surdana fight, off | cpu | 100.4 (90.1 to 104.4) | 9.53 (9.18 to 10.22) | 6.04 | 9.45 | 8.03 | 1.44 | 9.93 |
| | gpu | 100.5 (85.1 to 100.7) | 9.73 (9.47 to 11.36) | 5.92 | 5.44 | 9.58 | 1.41 | 9.97 |
| | strips | 95.5 (94.5 to 105.6) | 10.06 (9.36 to 10.33) | 6.66 | 7.15 | 6.61 | 1.37 | 10.49 |
| OotF Ruined Village, quality | cpu | 52.0 (49.9 to 52.1) | 17.54 (17.03 to 17.59) | 17.21 | 6.20 | 8.88 | 2.04 | 19.25 |
| | gpu | 50.8 (49.3 to 55.5) | 16.73 (16.68 to 16.74) | 19.31 | 6.33 | 14.32 | 2.00 | 19.48 |
| | strips | 52.9 (48.8 to 53.1) | 17.43 (17.26 to 18.23) | 16.61 | 7.62 | 9.56 | 2.45 | 18.88 |
| OotF Ruined Village, off | cpu | 72.6 (71.5 to 81.4) | 12.91 (11.79 to 13.06) | 12.03 | 6.24 | 7.48 | 1.89 | 13.75 |
| | gpu | 72.8 (68.5 to 75.7) | 14.20 (13.48 to 15.06) | 13.16 | 5.84 | 12.19 | 1.84 | 13.48 |
| | strips | 75.0 (69.0 to 77.4) | 12.67 (11.99 to 13.49) | 12.10 | 6.55 | 7.30 | 1.63 | 13.48 |

With `PS2X_FRAME_QUEUE=1 PS2X_READBACK=late` on every variant (Surdana only; the other scenes were not run with it):

| Surdana, DLSS | Variant | World fps | Frame ms | EE | VU1 worker | GS worker | GPU total ms |
| --- | --- | --- | --- | --- | --- | --- | --- |
| quality | cpu | 128.1 (125.3 to 129.1) | 7.14 (7.07 to 7.43) | 5.05 | 6.01 | 5.73 | 7.80 |
| | gpu | 132.5 (122.5 to 133.0) | 6.92 (6.92 to 7.08) | 4.54 | 3.78 | 6.24 | 7.58 |
| | strips | 129.0 (127.6 to 135.0) | 6.96 (6.93 to 7.64) | 4.93 | 4.47 | 4.77 | 7.70 |
| off | cpu | 141.3 (135.4 to 154.4) | 6.24 (5.84 to 7.13) | 3.99 | 6.00 | 4.77 | 7.34 |
| | gpu | 155.8 (151.2 to 156.6) | 5.99 (5.93 to 6.01) | 3.65 | 3.71 | 5.42 | 6.45 |
| | strips | 152.5 (143.2 to 158.1) | 5.98 (5.77 to 6.98) | 3.80 | 4.55 | 3.87 | 6.55 |

Pairs cpu to gpu: minus 2.1 % (quality) and minus 3.8 % (off) median frame time, gpu faster in 3 and 2 of 3 pairs;
the same pattern (VU1 worker minus 37 %, GS worker plus 0.5 to 0.65 ms).

Reading, plainly:

* **The batch path on the GPU does not pay as built.** The VU1 worker gains (Surdana minus 35 %, the fight minus 40
  to 42 %), but the GS worker pays more than that: plus 0.6 ms in Surdana without DLSS, plus 1.5 ms in the fight,
  plus 4.7 to 5.4 ms in the Order of the Flame Ruined Village, where the VU1 worker gains nothing (6.20 to 6.33 ms:
  19,034 UNPACK reports and 1,856 record builds a frame eat the deferred passes, as the live note measured). Frame
  times: Surdana about even (minus 0.2 % and minus 2.2 % in the pairs), the fight plus 6 to 17 %, the Ruined
  Village minus 4.7 % with DLSS (a pacing effect: the EE is the bound there) and plus 8.7 % without. The strips-only
  word is the best of the three in Surdana (minus 7 to 9 %) and about even elsewhere.
* **Where the GS worker's time goes.** Per batch record it now parses, checks, hashes and copies the record, builds
  the index list, and per draw estimates the bounds of every vertex (about 1.1 ms a frame for 14,000 vertices in the
  flight replay, see not verified) and maps four ring buffers and a constant buffer. The CPU path's packets cost the
  same thread 55 to 85 ns a vertex (live note 7.2), so a hand-over that costs about as much per vertex cannot win on
  that thread; the gain is on the VU1 worker, which is not the bound in these scenes. In the Ruined Village the draws
  are small (1,302,071 program draws for 1,938,006 records in the capture), so the per-draw cost dominates.
* **Cheapest fixes, in order of expected effect** (none built; the record format is unchanged, as asked):
  1. per frame instead of per draw: append a frame's program vertices, indices and pass rows to the rings without a
     Map per draw (one Map per ring per frame or a persistent staging copy), and drop the per-draw constant-buffer Map
     (the ring offset as a vertex attribute or a root constant in the batch's own buffer);
  2. bounds: an estimate per record from a bounding box of its raw positions through the transform (one estimate
     per record, vertices only when the record straddles a limit), or bounds only where the draw has an effect that
     depends on them (rows marked written, destination copy, skip);
  3. the hand-over on the VU1 side, as the live note named it: the range test inline before the VIF report, and the
     rows of a batch sent once with indices per index-list run instead of a self-contained record per run (which
     would also cut the GS worker's copy and hash per record).

## 4. Not verified

* Surdana and arena captures were replayed with the build before the exact bounds; flight and Ruined Village with
  the final code. The live toggles and the DLSS dumps used the final game build (`inc7`), except the terrain and
  material detail toggles (`inc5`, before exact bounds).
* The strip path still takes the scissor as its bounds (stand-ins). The same kind of difference as in flight is
  possible for strip records in a target smaller than the scissor or for a strip draw entirely outside it; no capture
  showed one. `batchBounds` could be applied to strips with an estimate of the strip transform.
* The cost of exact bounds: about 1.1 ms per world frame on the renderer thread in the flight replay (scale 2, about
  14,000 batch vertices a frame, 2.8 % of them evaluated exactly), measured with `gsr_replay --bench` against the
  same build with bounds switched off (temporary switch, removed); 2.2 ms before the per-record preparation. Not
  measured in the other scenes.
* Real motion sequences at 3840x2160 with DLSS quality under `gpu` (cracks, flicker, ghosting) were not looked at;
  the DLSS evidence is the paused input dumps. The batch-path history was exercised by the unit test only: in the
  paused dumps no batch record had a scope with a stable topology to match.
* The cold first start with the capability following the compile was not measured in the game after that change
  (the 649,479 `not ready` records are from the build before it).
* `vertex_records_webgpu_d3d12` aborted once (0xc0000409) in the whole `runtime-tests` run under load; not rerun alone.
  The whole preset was not rerun with the final code; `vertex_program_draw`, `vertex_program_history` and
  `vertex_records` were.
* The crash of the strip agent (0xC0000005, scale 8 beside a 4K game) was not reproduced; two scale-8 shader replays
  beside the owner's 4K game ran to the end at 15,953 of 16,303 MiB. The System log has `nvlddmkm` event 153 (driver
  reset) bursts at 22:13 to 22:14 on 12 November and 02:23, 03:09 to 03:10 on 13 November, none at 04:34 when this
  run ran. The draw vertex buffer's unchecked Map was a real null-pointer path and is fixed; whether it was that crash
  is inferred, not shown.
* Ravenshold was not benchmarked (the turn lock was shared with another agent's benchmark and a scene took over an
  hour); `tundra-c` and `arena-1e` neither; the frame-queue variant only in Surdana.
* Cliff side projection and terrain audit keep the `feature` fallback; ray effects too.
* WebGPU has no batch shader yet (the records are honoured there through the CPU evaluation as before).
