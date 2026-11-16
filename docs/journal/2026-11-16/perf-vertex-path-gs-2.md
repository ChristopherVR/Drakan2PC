# GPU vertex path: the GS worker's cost per draw and per record, second pass (2026-11-16)

Continues [perf-vertex-path-gs.md](perf-vertex-path-gs.md). Same settings: the owner's `user.ini` (3840x2160, DLSS
off, uncapped) with `GSR_MODERN_WATER=0`, Surdana start, `DRAKAN_VERTEX_PROCESSING=gpu` (strip records only, `stages
0x01`) against `cpu`. Evidence and scratch: `build/review/vp2/` (untracked; the batch dumps are private game data).

## 1. What the strip bounds spent

A dump of 1,500 strip batches the game drew in Surdana (`GSR_VP_BOUNDS_DUMP`, new, see 2.1) gives the shape of a draw:
1,822 vertices, 4,411 indices, 12 records of about 150 vertices each, 6 vertices evaluated exactly per batch. With
whole-record boxes about 40 % of the records reached past the known limits, and each of them had all of its
vertices estimated one by one: that, the box pass over every vertex and the index lookup (1.8 to 2.3 us per batch
alone) were the 19 to 25 us of `stripBounds` on the bench (`build/review/vp2/bench`, the old and new header compiled
side by side, the shortest of five calls per batch, two passes each; a busy machine, so only the ranges inside one
session compare).

## 2. Changes (one commit each)

### 2.1 Pieces of 32 vertices (`b71d6f65`)

* `program_draw::stripBounds` boxes each piece of `kBoundsPiece` (32) vertices of a record instead of the whole
  record; the piece with the outermost range per direction goes first, a piece strictly inside the limits is skipped
  (the same argument as for records: its range bounds every place its vertices can take). The record's prepared
  transform is made once (`BoundsScratch::transforms`) instead of twice.
* The box pass (`batchdetail::pieceBox`) works in floats (the conversion to double is exact and keeps the order) and
  uses SSE2 where the target has it (x64 always), with the scalar loop elsewhere (WebAssembly without the SSE
  headers).
* `VertexProgramRecord::allDrawn`: the core sets it when no strip of the record has fewer than three vertices; when
  every record of a batch has it the bounds skip the index lookup.
* Bench, the same dump: 16 vertices per piece 11.8 to 13.5 us, 32: 10.6 to 14.1, 64: 15.7 (one noisy session); the old
  code in the same sessions 19.0 to 29.0 us. Kept 32.
* Proof: `GSR_VP_BOUNDS_DUMP=<file>` (with `_SKIP`, `_COUNT`) writes the strip batches as drawn
  (`vertex_program_dump.h`); `gsr_test_vertex_program_history` with `GSR_VP_BOUNDS_BATCHES=<file>` checks
  `stripBounds` against `vp::evaluate` of every used vertex, once with the records' flags and once with every flag
  cleared (the index lookup), and that no record claims `allDrawn` wrongly. Dump 1 (old build, 1,500 batches,
  2,733,535 vertices): 0 differ. Dump 2 (new build, 1,500 batches, 18,588 records, all 18,588 flagged, 0 wrongly,
  2,868,648 vertices): 0 differ with the flags, 0 with the lookup. The synthetic cases (982 batches up to 800
  vertices, 3,958 up to 200): 0 differ.

### 2.2 The record's triangles in place (`15654d56`)

`vertexProgram` (gs_core.cpp) counted the triangles from the strip lengths first and writes the index list through a
pointer after growing it once per record, instead of one `push_back` per index. Same vertices and indices, same
order. Scratch timing of the copy alone on the dumped records: 1.47 to 1.04 us per record.

### 2.3 One Map per draw on Direct3D 11 (`fc8c3c2d`)

Where the device maps a shader-resource buffer with NO_OVERWRITE (`constantsAppend`), a draw's constants (its own and
the previous ones), vertices and indices go into one ring bound as vertex, index and shader-resource buffer, each part
on a 16-byte row: one Map instead of three. The velocity pass's previous stream keeps its own ring (not merged: no
test draws a velocity pass of a vertex-program batch, see 4). `GSR_VERTEX_PROGRAM_RINGS=1` restores the three rings.

## 3. Measured in the game (`build/review/vp2/base` and `step3`, 3 repeats each, interleaved by the tool's turn lock)

Per call on the GS worker inside the measured window (16 s, `GSR_PROFILE=5`, `build/review/vp2/percall.py`), gpu
runs; base = the tree before these commits, step3 = all three:

| scope | base (3 runs) | after (3 runs) |
| --- | --- | --- |
| `programBounds` per draw | 22.0, 21.7, 24.0 us | 15.4, 12.4, 12.5 us |
| `programUpload` per draw | 5.36, 5.34, 6.20 us | 4.25, 3.21, 3.35 us |
| `programRecord` per record | 0.81, 0.80, 0.92 us | 0.77, 0.70, 0.70 us |
| `programFlush` per flush (wraps the backend's draw) | 29.0, 28.4, 32.0 us | 20.7, 17.2, 17.4 us |

About 23 program draws and a few hundred records a frame: about 0.2 ms of bounds and 0.05 ms of upload per frame
less on the GS worker, and a few hundredths of a millisecond for the records.

Frame times (median of 3, range), the same run each:

| build | variant | world fps | frame ms | VU1 cpu ms | GS cpu ms |
| --- | --- | --- | --- | --- | --- |
| base | cpu | 183.5 (127.1 to 183.5) | 5.30 (5.28 to 7.61) | 5.12 | 4.14 (4.00 to 4.21) |
| base | gpu | 169.9 (112.5 to 188.6) | 5.88 (5.30 to 8.21) | 3.73 | 3.55 (3.48 to 4.02) |
| after | cpu | 140.4 (135.8 to 182.0) | 7.42 (5.20 to 7.50) | 5.36 | 4.07 (4.01 to 4.13) |
| after | gpu | 165.8 (140.8 to 175.0) | 5.46 (5.35 to 5.94) | 3.65 | 3.36 (3.22 to 4.13) |

**The machine was busy**: another agent's game ran before or during most runs (the tool's `games: 1`), the GPU was at
99 to 100 % before two of them, and the game had the foreground for 0 to 46 % of the samples. The frame times are not
a result (the ranges overlap everywhere; cpu after is slower than cpu base though nothing on the cpu path changed).
The GS worker's CPU per world frame under gpu went 3.55 to 3.36 ms in the medians, within the ranges. Only the
per-call numbers above are a result.

## 4. Tests

| What | Result |
| --- | --- |
| `gsr_test_vertex_records`, `GSR_VERTEX_PROGRAM_WAIT=1`, combined ring and `GSR_VERTEX_PROGRAM_RINGS=1` | each: 1,866 colour frames, 0 of 13,197,148,160 pixels differ; 354 depth pictures, 0 differ; local memory 0 of 421,527,552 words differ; 10,050 checks, 0 failed (700,512 vertices by a shader) |
| ctest `vertex_records`, `vertex_program_draw`, `vertex_path`, `vu_gpu_batch_policy`, `synthetic`, `vertex_program_history`, `vertex_program` (after 2.1 and 2.2) | 7 of 7 passed |
| ctest after 2.3 (`-j 1`): `vertex_path`, `program_motion`, `vertex_program_draw`, `vertex_program_webgpu_vulkan`, `vertex_program_webgpu_d3d12`, `vertex_program`, `vertex_program_history`, `synthetic`, `vertex_records`, `vertex_records_webgpu_vulkan`, `vertex_records_webgpu_d3d12`, `vu_gpu_batch_policy` | 12 of 12 passed |
| `gsr_test_vertex_program_history` on the two dumps | 0 differ (2.1) |
| clang-format 19, `scripts/repository_guard.py` | clean; 0 prohibited |

## 5. Not done, not proven

* No live paused toggles (`DRAKAN_VERTEX_TOGGLE`) or capture hashes were taken; the pictures rest on the records
  test (every scene and switching sequence, memory compared) and the bounds on the dumps.
* The velocity pass of a vertex-program batch (DLSS or neural upscaling on) is not drawn by any test; that is why
  its previous stream was left in its own ring. Merging it would save one more Map per draw with DLSS on.
* The per-draw constant buffer (XYOFFSET and the first constants row) is still written every draw. Writing it only
  when XYOFFSET changes needs the row elsewhere: absolute rows in the vertices (a pass over every vertex at upload),
  or the row as a per-instance attribute from an immutable counting buffer with `StartInstanceLocation`, which
  changes the input layouts and the generated HLSL of every entry point. Not built.
* The shape hash in `vertexProgram` is one multiply per strip; not worth gating on a motion consumer.
* `VertexProgramVertex` is 64 bytes of which 8 are padding; the upload copies about 117 KB per draw. A smaller
  vertex changes the shader inputs; not tried.
* WebGPU uses the same `programBounds` and core; its per-call cost was not measured.
* With modern water on (the owner's setting) the path stays in the microprogram, so none of this reaches that
  setting.
