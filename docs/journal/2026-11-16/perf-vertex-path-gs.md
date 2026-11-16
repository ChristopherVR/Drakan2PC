# GPU vertex path: the GS worker's cost per draw and per record (2026-11-16)

Continues the "not done" list of [gpu-vertex-tuning.md](../2026-11-13/gpu-vertex-tuning.md) section 4. Owner's
settings (3840x2160, scale 8, uncapped) with `GSR_MODERN_WATER=0` (with modern water the path is held in the
microprogram, [perf-cpu-jitter.md](perf-cpu-jitter.md)), Surdana start, `DRAKAN_VERTEX_PROCESSING=gpu` against `cpu`,
`GSR_PROFILE=10`. Evidence: `build/review/perf-vp/` (untracked).

## 1. Where the GS worker spent it

Before (run `surdana-prof`, 2 interleaved repeats): cpu 135.1 fps (7.50 ms), gpu 167.7 fps (5.92 ms), gpu faster in
2 of 2 pairs (-21.2 %); VU1 worker 7.23 to 4.68 ms per world frame, GS worker 5.68 and 5.54 ms (about 92 % busy at
168 fps, the limit). Per call on the GS worker under gpu: `programBounds` 49.9 us (about 21 draws a frame),
`programRecord` 2.32 us (about 490 records a frame), `programUpload` 6.8 us. A host RIP sample of the GS worker
(289 samples) showed `vp::evaluate` and the VU arithmetic it uses at about 13 %: the strip record's register tail.

## 2. Changes

* **Record boxes in the strip bounds** (`program_draw::stripBounds`, `batchdetail::estimateBoxFast`). Each record's
  raw positions become a box; every place its vertices can reach is bounded with interval arithmetic and the
  estimate's error model (w clearly positive, the truncation, two units of margin; false on saturation or the 16-bit
  wrap). A record whose range lies strictly inside the limits the extremes are already known to reach is neither
  estimated nor evaluated; the records with the outermost ranges (and those without a range) go first. The bounds
  are the same set of extremes whatever the order. `gsr_test_vertex_program_history` (strip bounds against
  `vp::evaluate` of every vertex): 3,958 batches up to 200 vertices and 982 up to 800, 0 differ (synthetic cases;
  no recorded game vertices were available). In the game: 49.9 to 28.8 us per draw.
* **Deferred strip tail** (`GsCore::Impl::StripTail`, `settleStripTail`). A shader-drawn strip record no longer
  evaluates three vertices to leave the registers and the vertex queue as its packets would; the last one or two
  vertices and a copy of the constants are kept and evaluated by the first reader (every caller of
  `settleBatchTail`, which now settles both tails). A following strip record with vertices, or a shader-drawn batch
  record, replaces the tail (it writes PRIM and loads every register); a strip record without vertices settles it
  before its PRIM write. `vertex_records` (pictures and GS memory of records against packets), `vertex_path`,
  `vertex_program_draw`, `vertex_program_history`, `synthetic`, `vu_gpu_batch_policy`: passed. In the game: 2.35 to
  0.95 us per record.

## 3. After both (run `surdana-prof3`, 3 interleaved repeats)

| variant | world fps | frame ms | 1 % low ms | EE | VU1 | GS | all other |
| --- | --- | --- | --- | --- | --- | --- | --- |
| cpu | 182.8 (162.8 to 183.6) | 5.25 | 7.9 | 3.50 | 5.16 | 4.33 | 11.15 |
| gpu | 187.7 (182.4 to 188.6) | 5.29 | 7.4 | 4.29 | 4.24 | 4.37 | 10.94 |

The whole machine ran faster in this run (cpu 135 fps before, 183 now), so only figures inside one run compare. The
GS worker no longer limits gpu (the frame time sits at 5.27 to 5.29 ms in all three repeats); frame time cpu against
gpu: 1 of 3 pairs faster (+0.3 % median). Per call: `programBounds` 27.2 us, `programRecord` 0.95 us,
`programFlush` 64.9 to 37.3 us.

## 4. Open

* With modern water on (the owner's setting) the path stays in the microprogram: none of this reaches that setting.
* The EE thread rose under gpu in this run (3.50 to 4.29 ms per world frame); earlier runs showed the opposite, not
  examined.
* Still on the list of 13 November: one Map per draw, absolute constant rows in the vertex data, the shape hash only
  where a motion consumer is on, the leaner batch record.
