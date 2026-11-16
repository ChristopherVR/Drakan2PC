# GPU vertex path: the batch families measured again and cut (2026-11-16)

Continues [perf-vertex-path-gs-2.md](perf-vertex-path-gs-2.md) and the 13 November notes
([gpu-vertex-batch-draw.md](../2026-11-13/gpu-vertex-batch-draw.md), [gpu-vertex-tuning.md](../2026-11-13/gpu-vertex-tuning.md)):
`vertex_processing = gpu` hands only strip records to the shader (`vpath::kGpuDefaultStages = kStrips`) because the
batch families (terrain grid, texture generation, lighting, fade, texture matrix, index lists) cost the GS worker more
than the VU1 worker saved.

Settings everywhere: the owner's `user.ini` (3840x2160, DLSS off, `vsync 0`, `frame_limit 0`, Direct3D 11) with
`GSR_MODERN_WATER=0`, HD textures (`GSR_TEXDIR`), a warm shader cache of our own (`GSR_SHADER_CACHE_DIR`),
`GSR_PROFILE=10`, `DRAKAN_VERTEX_LOG=1`, `--foreground` (the game had the foreground for 25 to 60 % of the samples:
other agents' windows). `scripts/perf/scene_ab_bench.py` one repeat at a time; two play folders (`base` = this
branch's start 4a356312, and the build under test) alternated per repeat by `build/vbf/ab.sh`, the variants
interleaved inside each repeat. Variants: `cpu`; `strips` = `DRAKAN_VERTEX_PROCESSING=gpu` (the default stages);
`all` = the same with `DRAKAN_VERTEX_STAGES=all`. CPU columns are ms per world frame, medians over the repeats with
the range. Scratch and evidence: `build/vbf/` (untracked; the bounds dumps are private game data).

## 1. Before (base)

Surdana start, 3 repeats each, quiet machine (no other game, replay or compiler at the start of any run):

| variant | world fps | frame ms | EE | VU1 | GS | VU1 + GS | GPU total |
| --- | --- | --- | --- | --- | --- | --- | --- |
| cpu | 188.3 (182.0 to 188.4) | 5.12 (5.12 to 5.23) | 3.31 | 5.10 | 4.06 | 9.16 (9.05 to 9.44) | 5.28 |
| strips | 189.4 (187.9 to 189.6) | 5.28 (5.27 to 5.31) | 3.36 | 3.45 | 3.25 | 6.70 (6.65 to 6.82) | 5.28 |
| all | 190.6 (189.6 to 191.3) | 5.24 (5.22 to 5.27) | 3.37 | 3.48 | 4.09 | 7.57 (5.92 to 7.62) | 5.26 |

The frame is bound by the GPU in this scene at these settings (GPU total 5.2 to 5.3 ms in every variant, its
`overlay` pass 4.5 ms), so the frame time does not follow the CPU threads; the result is in the thread columns.
**The batch families did not pay: the VU1 worker saved nothing (3.45 to 3.48 ms) and the GS worker paid 0.84 ms.**

Under `all` the GS worker drew about 27,000 batch-path draws a second (about 140 a frame, about 7 records and 200
vertices each) from 212,000 records a second. Per call (3 runs): `programBounds` 9.52, 9.55, 9.69 us per draw (strip
and batch draws), `programRecord` 0.77 us per record, `programUpload` 1.02 us, `programFlush` 12.3 us per draw.

Host RIP samples (`PS2X_HOSTPROF=12 PS2X_HOSTPROF_BUSY=1`, 12 s at the pose, `all`): the bounds took about 1,600 of
the GS worker's 3,800 active samples (`estimateFastAt` alone 691); the batch tracker about 1,190 of the VU1 worker's
3,700 (`buildRecord` 318, `writeSlot` 204, `collect` 198, `walkIndexList` 146, `stripInputsKnown` 113, `decide`,
`deferStage`, `noteUnpack`, `beforeRun`). Under `strips` the GS worker's packet work for the same batch-path vertices
(`kick`, `gifSubmit`, `emitTri`, `handlePacked`, `toDV`) was about 880 of 2,970.

Order of the Flame, Ruined Village (campaign start, the converted pack from the play folder's `mods/`), one repeat:

| variant | world fps | frame ms | EE | VU1 | GS | VU1 + GS | GPU total |
| --- | --- | --- | --- | --- | --- | --- | --- |
| cpu | 78.6 | 12.31 | 10.87 | 6.16 | 6.42 | 12.58 | 12.72 |
| strips | 78.2 | 12.39 | 10.83 | 4.93 | 5.64 | 10.57 | 12.78 |
| all | 76.8 | 12.58 | 11.48 | 4.70 | 7.32 | 12.01 | 13.02 |

Here too the GPU bounds the frame (GPU total 12.7 to 13.0 ms at 3840x2160). The draws are tiny: about 50,000
batch-path draws a second (650 a frame) of 2.3 records each; per call `programBounds` 3.30 us, `programRecord` 0.67
us, `programFlush` 5.82 us. The draw count is the game's (the CPU path makes 60,000 draws a second there too: every
change of GS state is a draw on both paths).

## 2. Changes (one commit each)

1. **Batch-path bounds four vertices at a time** (`d358d9bd`, `vertex_program_draw.h`). `batchBounds` estimated
   every used vertex in doubles one by one. The vertices of the two common chains (one Transform from the raw
   position; a grid pass and then a Transform) are now estimated four at a time in single precision with SSE2
   (`estimateFast4`); every term of the error model is taken with `kRelFloat` = 4 `kRel` (the VU's own deviation
   plus the single-precision rounding of the estimate itself, about six roundings of 2^-24 of the magnitudes the
   model already scales), the final bound widened once more by a part in 2^10 and 1/64 unit. Only the choice of which
   vertices are evaluated exactly rests on the estimate, so the extremes are the same. A vertex it cannot bound, and
   every other chain, takes the double path as before; WebAssembly without the SSE headers keeps the double path. The
   position passes are prepared once per pass set; the index lookup is skipped when every record says all its
   vertices are drawn (`VertexProgramRecord::allDrawn`, now also set for batch-path records: every strip of three
   or more indices and every vertex among the indices); an exact place after a grid pass evaluates only the
   transform's place lanes (`exactPlace`). In the core, consecutive batch-path records with the same passes share
   their rows of the pass table (`queueBatch`; the shader, the bounds and the history read the same rows) and the
   triangles are written in place. Tried first and dropped: boxes over pieces of 16 or 32 vertices as for strips
   (pieces skipped 32 % at best, no gain: in a draw of 5 to 7 terrain records every record touches the outline).
2. **Tracker trims on the VU1 worker** (`780209f7`, `ps2_vu1_gpu_batch.h`): lane tables instead of switches,
   `collect()`'s result taken from the previous slot when the lanes asked for, the slot's virtual lanes and its chain
   are the same, `stripInputsKnown` over ranges of slots. The tracker's samples 1,190 to 860 (of about 3,000 active).
3. **A bounds tie evaluated once, pass sets kept across draws** (`ea47080f`): a kept vertex whose place depends on
   exactly the inputs of one of the last eight evaluated (the rows of the pass table, position passes, position row
   and grid values) is not evaluated again; a pass set whose kinds and position-pass constants equal the one its slot
   held in the previous draw is not prepared again.
4. **No clock reads per UNPACK without `DRAKAN_VERTEX_LOG`** (`e0afa5c4`): three `rdtsc` per followed UNPACK (about
   850,000 a second in the Ruined Village under `all`) only fed that log line. Not measured on its own; every
   benchmark here runs with `DRAKAN_VERTEX_LOG=1`, so they do not show it.

## 3. After

Per call on the GS worker, the same runs:

| scope | Surdana base (3 runs) | Surdana after 1 + 2 (3 runs) | Ruined Village base | Ruined Village after 1 to 3 |
| --- | --- | --- | --- | --- |
| `programBounds` per draw | 9.52, 9.55, 9.69 us | 6.98, 7.00, 7.13 us | 3.30 us | 2.37 us |
| `programRecord` per record | 0.77, 0.77, 0.77 us | 0.70, 0.69, 0.69 us | 0.67 us | 0.66 us |
| `programFlush` per draw | 12.31, 12.25, 12.30 us | 10.07, 9.88, 9.79 us | 5.82 us | 4.95 us |
| `programUpload` per draw | 1.02, 1.02, 1.06 us | 0.97, 1.00, 1.03 us | 0.44 us | 0.45 us |

Bounds in isolation on the Surdana dump (2,728 batch-path draws, 547,284 vertices, `gsr_test_vertex_program_history`
with `GSR_VP_BOUNDS_BATCHES`, shortest of five calls; the first two in one session, the third in a later one):
6.18 us per draw with every vertex estimated one at a time in doubles (`BoundsScratch::batchScalar`, already with the
per-set preparation and the `allDrawn` skip) to 3.47 us four at a time, 2.85 us with ties evaluated once; exact
evaluations 13,063 / 13,440 (the wider model) / 10,785. The Ruined Village's draws are small (22 vertices on
average in its dump): 0.87 to 0.66 us per draw there, about a seventh of its vertices evaluated exactly.

Surdana, 3 repeats each (after 1 + 2):

| build | variant | world fps | frame ms | EE | VU1 | GS | VU1 + GS | GPU total |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| base | strips | 189.4 (187.9 to 189.6) | 5.28 | 3.36 | 3.45 | 3.25 | 6.70 (6.65 to 6.82) | 5.28 |
| base | all | 190.6 (189.6 to 191.3) | 5.24 | 3.37 | 3.48 | 4.09 | 7.57 (5.92 to 7.62) | 5.26 |
| after | strips | 188.6 (188.2 to 189.1) | 5.30 | 3.34 | 3.27 | 3.23 | 6.58 (6.44 to 6.64) | 5.31 |
| after | all | 190.9 (190.5 to 191.0) | 5.23 | 3.31 | 3.23 | 3.43 | 6.66 (6.43 to 6.91) | 5.23 |

Ruined Village, one repeat (after 1 to 3):

| build | variant | world fps | frame ms | EE | VU1 | GS | VU1 + GS | GPU total |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| after | cpu | 78.9 | 12.38 | 10.89 | 5.81 | 6.10 | 11.92 | 12.70 |
| after | strips | 77.7 | 12.44 | 11.33 | 4.65 | 5.79 | 10.45 | 12.87 |
| after | all | 76.1 | 12.71 | 11.46 | 4.40 | 7.09 | 11.48 | 13.14 |

## 4. Per family, and the decision

`DRAKAN_VERTEX_STAGES` as a bit mask (the variant syntax of the bench splits on commas): `nofade` = every family but
fade and texture matrix (505), `terrain` = strips, grid, point light, texture generation, lighting, index lists (249),
`lit` = strips, lighting, index lists (97). The build of section 3 with change 3 (`s4`), one exe, variants
interleaved.

Surdana, 2 repeats (the second on a busier machine: another agent's replay ran during 4 of its 6 runs):

| variant | world fps | frame ms | VU1 | GS | VU1 + GS | GPU total | records the renderer evaluated on the CPU |
| --- | --- | --- | --- | --- | --- | --- | --- |
| cpu | 185.5 (183.0 to 188.0) | 5.25 | 5.16 | 4.07 | 9.23 (8.92 to 9.53) | 5.37 | |
| strips | 188.9 (188.8 to 189.0) | 5.28 | 3.60 | 3.37 | 6.97 (6.88 to 7.05) | 5.29 | 0 |
| all | 190.5 (190.5 to 190.5) | 5.24 | 3.60 | 3.78 | 7.38 (7.27 to 7.50) | 5.24 | 0 |
| nofade | 189.8 (189.5 to 190.1) | 5.25 | 4.18 | 3.60 | 7.78 (7.67 to 7.90) | 5.27 | 41,580 a second |
| terrain | 179.8 (170.0 to 189.5) | 5.28 | 4.13 | 3.72 | 7.84 (7.77 to 7.92) | 5.52 | 41,589 a second |
| lit | 175.5 (163.2 to 187.9) | 5.41 | 3.39 | 3.78 | 7.17 (7.10 to 7.23) | 5.83 | 93,572 a second |

Ruined Village, 1 repeat (quiet machine):

| variant | world fps | frame ms | EE | VU1 | GS | VU1 + GS | GPU total | CPU-evaluated records |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| cpu | 77.9 | 12.40 | 10.91 | 5.86 | 6.16 | 12.03 | 12.85 | |
| strips | 77.8 | 12.51 | 11.28 | 4.45 | 5.50 | 9.95 | 12.87 | 0 |
| all | 76.8 | 12.73 | 11.13 | 4.49 | 6.83 | 11.32 | 13.02 | 0 |
| nofade | 77.8 | 12.56 | 11.00 | 4.72 | 6.10 | 10.82 | 12.87 | 35,379 a second |
| terrain | 77.9 | 12.48 | 10.92 | 4.80 | 6.45 | 11.25 | 12.84 | 35,481 a second |
| lit | 78.6 | 12.48 | 10.97 | 4.71 | 6.15 | 10.86 | 12.74 | 49,374 a second |

Reading, plainly:

* **The batch families still do not pay.** In Surdana `all` is now about even with `strips` (VU1 + GS plus 0.08 ms
  per world frame in the three repeats of section 3, plus 0.41 ms in these two), in the Ruined Village it costs 1.0
  to 1.4 ms of CPU per world frame more than `strips` and about 0.15 to 0.3 ms more GPU time, and the frame time is
  1.8 to 2.2 % longer there in both runs (the GPU bounds the frame in both scenes at these settings). **The default
  stays strips** (`vpath::kGpuDefaultStages`; its comment now cites this note).
* **Where the rest goes.** In Surdana the VU1 worker saves next to nothing with `all` (3.23 against 3.27, 3.60
  against 3.60 ms): what the microprogram no longer runs, the tracker spends following every run and every UNPACK
  and building the records (about 860 of the VU1 worker's 3,000 active samples after change 2). In the Ruined
  Village the draws are tiny (650 a frame, 2.3 records each in the measured window, 22 vertices on average in the
  dump): per draw the bounds and the upload cost about 2.8 us (2.35 and 0.44) beyond the draw both paths share, plus
  0.65 us per record, against the packets' work on the same thread.
* **Partial stage sets are worse than either.** A set without fade and texture matrix leaves those runs to the
  microprogram; the tracker then writes the deferred passes back before them (`slots written back` over a Surdana run:
  80,000 under `all`, 9.8 million under `nofade`), and the index lists that follow build records from rows that are
  already processed, which `batchTakes` declines (`draw state`) and the renderer evaluates on the CPU. The families
  only work together.

## 5. Proof

| What | Result |
| --- | --- |
| ctest `vertex_path`, `program_motion`, `vertex_program_draw`, `vertex_program`, `vertex_program_history`, `vertex_program_history_warp`, `synthetic`, `vertex_records`, `vertex_records_webgpu_d3d12`, `vu_gpu_batch_policy`, `vertex_batch_program`, `vertex_program_webgpu_d3d12`, `vertex_program_webgpu_vulkan` (`-j 1`) | 13 of 13 passed after change 1, and again after changes 3 and 4 |
| `vu_gpu_batch_policy` after change 2 | 5,630,490 checks, 0 failed |
| `gsr_test_vertex_program_history`, Surdana dump (`GSR_VP_BOUNDS_DUMP`, now both kinds: 272 strip and 2,728 batch-path draws) | 0 differ against every used vertex evaluated, 0 with the index lookup, 0 records claiming `allDrawn` wrongly (13,356 batch-path records, all flagged), 0 differ between the four-wide and the double estimate |
| the same, Ruined Village dump (18 strip and 3,982 batch-path draws, 4,972 records, 88,705 vertices) | 0 differ against every used vertex, 0 with the index lookup, 0 records claiming `allDrawn` wrongly, 0 differ between the four-wide and the double estimate (exact evaluations 13,193 and 13,029) |
| the same, synthetic cases, batches up to 64 and up to 800 (`GSR_VP_BOUNDS_BATCH=800`) | batch path 3,685 and 293 batches, 0 differ, also with every record flagged all drawn and against the double estimate |
| `DRAKAN_VERTEX_SHADOW=1 DRAKAN_VERTEX_STAGES=all`, Surdana, after change 2 | 4,389,387 runs; 920,956 records, 46,344,437 vertices compared, 0 differ; 359,956 vertex runs evaluated, 0 differ; 3,108,475 runs executed on the shadow memory, 0 with other packets; 0 lanes changed without a reported write; 0 runs drawing an unknown lane |
| clang-format 19.1.5 on every changed C++ file, `scripts/repository_guard.py` | clean; 3,107 approved, 0 prohibited |

## 6. Not done, not proven

* No capture hashes and no live paused toggles (`DRAKAN_VERTEX_TOGGLE`) were taken; the pictures rest on the records
  test (pictures and GS memory of records against packets, every scene and switching sequence), the bounds on the
  dumps and the synthetic cases, and the tracker on the shadow run and the policy test. Under the default (`strips`) the
  changed code runs only in the strip bounds, whose piece ordering moved into shared helpers with the same steps;
  the tracker does not follow and the core's batch-path code is not reached.
* The four-wide estimate's error model is argued (section 2) and checked on the dumps and cases, not proven
  formally; a wrong bound would show as a draw whose bounds differ from every vertex evaluated.
* `vertex_records_webgpu_vulkan` was not run (the WebGPU backend has no batch shader; the shared code it uses is the
  strip bounds, covered by `vertex_records_webgpu_d3d12` and `vertex_program_webgpu_vulkan`).
* Change 4 was not measured on its own (every run here sets `DRAKAN_VERTEX_LOG`).
* With modern water on (the owner's setting) only strips can leave the microprogram anyway (the ray surface stream
  carries strip batches only, `vertexPathCapability`).
* What would make the families pay, by the numbers above: the tracker's cost on the VU1 worker (it now costs about
  what the microprogram saved), and, in the Order of the Flame levels, the fixed cost of a shader-drawn draw (bounds
  and upload of a draw of a few dozen vertices); the GPU time of 650 small shader draws a frame there is higher too.
* Runs aborted by stopping a benchmark (exit 0xC0000142 of the harness processes) were left out.
