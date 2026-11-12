# GPU vertex path: implementation record (from 2026-11-12)

The study is [gpu-vertex-feasibility.md](gpu-vertex-feasibility.md). This file records what is implemented, in the
order it landed, with the proof of each increment. Scratch (builds, runs, hashes, logs): `build/review/gpu-vertex2/`,
not tracked.

## 0. Design rule: the path can always be swapped back

Requirement from the owner: "Make sure the GPU vertex path can be configured so we can always swap back." The CPU
path is a permanent, selectable alternative, not scaffolding.

1. **One player setting.** `[video] vertex_processing = cpu | gpu | auto` in `drakan.ini`, environment
   `DRAKAN_VERTEX_PROCESSING`, row "Vertex processing" on the F1 settings page (Performance). `auto` uses the GPU
   only where the renderer and adapter are proven identical. It applies while the game runs; no restart. The default
   is `cpu` and is not changed without the owner's decision.
2. **`cpu` is the behaviour from before this work.** The microprogram runs on the VU1 worker, no record is built, no
   hook acts (one atomic load per MSCNT), the scene-model capture and the CPU object-motion tracking run as before
   and a capture holds the same packets. None of the CPU-side code the GPU path can replace is removed.
3. **Three evaluation modes stay selectable** (`gsr/vertex_path.h`, `vpath::Eval`): `microprogram`; `records` (a
   recognised run is sent as a record of its inputs and the renderer evaluates it on the CPU with the VU's
   arithmetic: the reference of the GPU path); `gpu` (the record is drawn by a vertex shader). Diagnosis switches:
   `DRAKAN_VERTEX_EVAL`, `DRAKAN_VERTEX_STAGES` (one bit per stage family, so one stage can be turned off alone),
   `DRAKAN_VERTEX_TOGGLE=<frames>` (alternate with the microprogram inside one run), `DRAKAN_VERTEX_LOG=1`.
4. **Automatic fallback, counted.** Per run (stage off, engine not in a skippable state, layout not recognised,
   clipper), per record (backend without the path, adapter without the arithmetic, a GS state the shader does not
   cover, a possible skip bit, a renderer feature that reads vertices on the CPU), per session (device lost, internal
   failure). Every fallback ends in the microprogram or in the CPU evaluation of the record, both exact. One counter
   per reason (`vpath::Fallback`), printed by `DRAKAN_VERTEX_LOG=1`.
5. **Nothing persistent depends on the setting.** A record is honoured by every renderer under every setting, so a
   capture taken with one path replays with the other; saves do not see the path at all.
6. **One control word** (`vpath::State::word`: evaluation mode and enabled stages) is read by the VU1 worker for
   every run and by the renderer for every record. A run or record that started under one value finishes under it;
   a change takes effect at the next object. That is safe because both paths give the same GS input bit for bit.

## 1. Increment 1: the record path in the tree, behind the setting (no shader yet)

What landed: the control word and counters (`runtime/gs/include/gsr/vertex_path.h`, test `vertex_path`), the MSCNT
hook (`ps2_runtime.cpp`, `vu/ps2_vu1_gpu_program.h`: `intercept` reads the control word per run), the record
handling in `gsr_core` (`gs_core.cpp`: a record in the PATH1 stream is evaluated with `vp::evaluate` and drawn
through the ordinary `kick`, or handed to a backend as an indexed triangle list of raw vertices,
`BackendApi::drawVertexProgram`; no backend implements that yet, so `gpu` resolves to `cpu`), the host glue
(`drakan_vertex_path.{h,cpp}`), the setting (`runtime/settings`, `drakan_config.cpp`), and
`scripts/harness/replay_hash_check.py` (frame hashes of captures against a reference set or between two
configurations).

Compared with the prototype patch of the study: the two prototype switches `DRAKAN_GPU_VERTEX` and
`GSR_VERTEX_PROGRAM` are gone (one control word instead), nothing is latched at start, a batch for a backend is
indexed (one raw vertex per strip vertex instead of three per triangle), and every decline is counted by reason.

Proof: see section "Proof log" below.

## 2. Increment 2: one description of a stage, generated HLSL and WGSL, one arithmetic source

Written by the `wgsl` subagent, checked and landed by the orchestrator; details, the operation list, the cost table
and the root cause of the jitter bit are in [gpu-vertex-stages.md](gpu-vertex-stages.md).

* `gsr/vertex_stage.h`: a stage is a list of operations over constant rows and vertex attributes; the two strip
  stages are written in it. `ir::Evaluator` runs it on the CPU (the reference; `vp::evaluate` is pinned to it by the
  test `vertex_stage`).
* `gsr/vu_exact.inl`: the VU arithmetic on the bits of a float in 32-bit integers, one text compiled as C++
  (`gsr/vu_limbs.h`) and emitted as HLSL and WGSL (`src/vertex_stage_emit.h`). The double functions of the study
  remain as a second arithmetic for Direct3D 11. `vp::chooseArithmetic` picks per backend and adapter; integer limbs
  are the default everywhere, so the path does not depend on the optional double feature of Direct3D 11.
* `VSMain` (`gs_shaders.h`) now fixes the order of its two sums per coordinate with `precise`. Cause of the "61
  positions differ in one last bit under jitter" of the study: FXC reordered the unprotected three-term sum
  differently in each shader `VSMain` is inlined into. Consequence for the ordinary renderer, stated plainly: frames
  without jitter are unchanged (hashes below); with DLSS on, `VSMain` gives the same bits as before (shown at vertex
  level: the new entry point against the previous `VSMain` text, 1,215,215 jittered vertices, 0 differ), but the
  velocity shader `VSMotion`, which inlines `VSMain`, may now round a jittered position like the colour pass where
  it did not before. Whether it differed before was not measured.
* Shader compile time is a constraint: FXC needs 6 to 10 s for the integer version of the draw's entry point (2 to 4 s
  with doubles). A backend must compile it in the background and stay on the CPU path until it is ready; the shader
  cache makes that once per machine.

No renderer uses the generated shaders yet.

## 3. Increment 3: the batch path as recogniser and CPU evaluation (no shader, nothing acts at run time)

Written by the `batch` subagent; the stage description, the record design and the estimate are in
[gpu-vertex-batch-path.md](gpu-vertex-batch-path.md). What it changes in the plan:

* Every batch stage without clip codes is a function of one vertex: depth fade, texture matrix, grid lights, point
  light, texture generation, directional light and transform read and write only the lanes of their own slot, and
  the index-list run only copies. The chain composes into one function from a raw row and per-stage constants to the
  packet fields, so a vertex shader can do it. The chain length depends on the draw: the texture matrix accumulates
  over terrain layers (1.1 to 6.7 passes per record on average, 13 at most in the new recordings), so the shader is a
  loop over a pass list with a bound of 24.
* A batch is several MSCNTs and the EE rewrites single lanes of VU memory between them. `vu1gpu::Tracker`
  (`vu/ps2_vu1_gpu_batch.h`) follows that: a vertex run becomes pending passes per slot, an index-list run becomes
  one self-contained record (`gsr::vp::Batch`, `gsr/vertex_batch.h`), a run that has to execute is preceded by the
  exact evaluation of what was deferred, and lanes a skipped batch would have left behind are marked stale, never
  guessed. It needs the EE's writes lane by lane from the VIF unpack.
* Runs with clip codes and the clipper stay in the microprogram; the split is per run. A clip-code index-list run
  whose vertices are all inside can be taken (few: 0 to 196 per recording).
* The index-list run is 18 to 49 % of the VU1 engine time but already a plain copy in the compiled engine; building
  a self-contained record for it costs about as much as running it. The saving of the batch path on the VU1 worker
  comes from the vertex passes. A leaner hand-over for index lists (rows of a batch once, per run only indices) is
  not built.
* Share of the VU1 engine time in runs the recogniser takes (strips and batch together): 53 to 87 % on foot in
  Drakan 2, 90 % and 57 % in the two flight recordings, 84 % and 80 % in the two Order of the Flame levels. What
  stays: clip codes and clipper 9 to 42 %, set-up 1 to 12 %.
* The strip runs the study saw declined ("outside the vertex window") were tables that fill the window exactly; the
  recogniser now takes them.

The record handling in `gsr_core`, the run-time hook (`Tracker::step` at the MSCNT, `noteWrite` from the fast VIF
unpack path) and every shader are still to do.

## 4. The records test

`runtime/gs/tests/test_vertex_records.cpp` (ctest `vertex_records`, `vertex_records_webgpu_vulkan`,
`vertex_records_webgpu_d3d12`), written by the `rectest` subagent: two renderers of one backend side by side, one
fed the packets of the microprogram, the other the records, colour and a depth picture compared in full after every
frame, at scales 1, 3 and 8, under every evaluation mode and with the mode switching inside a sequence, with the
counters asserted and negative controls. It found one fault by reading: after a record handed to a backend the
vertex queue was empty, where the packets leave the last two vertices of the last strip (a following tag without
PRIM continues the strip); fixed in `gs_core.cpp`. Today it proves the `records` mode and the fallback; when a backend
answers yes to `vertexPrograms()` the same test is the proof of its shader.

## 5. Increment 4: the batch path as a generated vertex stage

Written by the `bstage` subagent; description, input layout for the backends, cost tables and what is not
established are in [gpu-vertex-batch-stage.md](gpu-vertex-batch-stage.md).

* One generated function per pass kind (`gsr/vertex_batch_stage.h`), a carried row as state (`Op::State`), and one
  loop written once in the emitter for HLSL and WGSL and mirrored on the CPU: it walks the set bits of the vertex's
  pass mask (at most 24) and dispatches on the kind read from the pass table. No pass logic exists twice.
* Two findings that matter beyond the batch path. The double arithmetic of Direct3D 11 returned -0 for a sum of a
  literal zero and -0 (FXC folds it even under `precise`); fixed in the double library, the strips never used such a
  sum. And the engine's MAX / MINI compare raw register bits, which differs from the operand form when one side is a
  NaN: the batch description reproduces the engine; the strip stage (`vp::evaluate`) clamps the fog depth with
  operand-normalised constants and would differ from the microprogram for NaN clamp constants. That last point is
  read from the code, never seen in a recording, and not changed.
* FXC needs 14 to 24 s for the batch entry point with integer limbs (5 to 7 s with doubles), 23 to 30 s with the
  previous-position twin: background compilation is mandatory, and the first seconds after the first start on a
  machine run on the CPU path.

No renderer draws a batch record with a shader yet.

## Proof log

| Date | What | Result |
| --- | --- | --- |
| 2026-11-12 | Surdana capture with records (1,423 frames), records evaluated on the CPU, Direct3D 11, scale 3, HD textures: frame hashes against the study's CPU-evaluated run | 1,423 frames, 0 differ |
| 2026-11-12 | Live, Surdana, paused, 1280x960, scale 3, DLSS off, `DRAKAN_VERTEX_EVAL=records DRAKAN_VERTEX_TOGGLE=64`: ten screenshots, 4 taken under `records` and 6 under `microprogram` | the 8 shots after the quest notice left the screen are identical (4 records, 4 microprogram); 104,339 strip runs handed over, 3,995 not recognised and run by the microprogram |
| 2026-11-12 | The same live run again with the final build of increment 1 | the 9 shots after the notice are identical (4 records, 5 microprogram); 96,246 strip runs handed over, 3,703 not recognised |
| 2026-11-12 | "`cpu` equals before": five captures without records (Surdana, cave, flight, inventory, Order of the Flame; `build/review/perf-gpu/caps`) replayed by this build on Direct3D 11, WebGPU on Vulkan and WebGPU on Direct3D 12 at scales 1, 3 and 8 with HD textures, against the hashes the renderer restructuring session made with a build of `1fa0fc3e` (`scripts/harness/replay_hash_check.py --reference`) | 45 comparisons, 162,585 frames, 0 differ |
| 2026-11-12 | `runtime-tests` preset in a private tree of the main tree (111 tests): all pass except `ray_water_lost_signal` (timeout) and `spu2_test` under four parallel jobs on the loaded machine; both pass when run alone. `python scripts/run_tests.py`: OK. `scripts/repository_guard.py`: 0 prohibited | |
| 2026-11-12 | Increment 2, re-run by the orchestrator in the main tree with `GSR_VP_RECORDS` = the 615,215 recorded vertices: `gsr_test_vertex_stage` (limb library against doubles and the processor's round-toward-zero, 16,000,000 operand triples x 18 operations; evaluators; `vp::evaluate`), `gsr_test_vertex_program` (Direct3D 11, RTX 5080: doubles and limbs, raw output and the draw's entry point, plain and jittered), `gsr_test_vertex_stage_webgpu` on Vulkan and on Direct3D 12 | 1,215,215 vertices per path, 0 differ on every path |
| 2026-11-12 | Increment 2: five captures on Direct3D 11 at scales 1, 3, 8 against the reference hashes (the `VSMain` change) | 15 comparisons, 54,195 frames, 0 differ |
| 2026-11-12 | Increment 2: `runtime-tests` preset, two parallel jobs | 168 tests, 0 failed, 6 skipped (need game data) |
| 2026-11-12 | Increment 2: live, Surdana, owner's ini (3840x2160, DLSS quality), two screenshots looked at | world, HUD and character as expected; no comparison possible across runs |
| 2026-11-12 | Increment 3, re-run by the orchestrator with a game build of the main tree: VU1 replay of 16 recordings of 8,000 runs with VIF write lists (`DRAKAN_VU_BENCH_BATCH=1 DRAKAN_VU_BENCH_GPUPROG=1`; Surdana, street, square, fight, arena, catacombs, mine, tundra, valley, stratos, two Isles scenes, Ravenshold and Shiverbane mounted, Order of the Flame Ruined Village and Volcanic) | 34,725 vertex runs evaluated against the microprogram's memory, 0 differ; 57,528 batch records, 2,604,739 vertices, 0 differ; 708,250 strip vertices, 0 differ; runs executed on the tracker's memory: 0 with other packets, 0 lanes other than recorded; 0 draws from a stale lane; declines: clip codes and clipper only |
| 2026-11-12 | Records test, as reported by its author on Direct3D 11, Vulkan and Direct3D 12 (identical totals): 1,017,252 record vertices, 846 colour frames and 174 depth pictures compared | 0 pixels differ of 5,983,272,960 and 1,230,602,240; controls detect a colour step and a depth step |
| 2026-11-12 | Increment 4, re-run by the orchestrator in the main tree with the 16 record dumps: `gsr_test_vertex_batch_stage` (description against `vp::evaluateVertex` and `vp::batchToGif`, both arithmetics), `gsr_test_vertex_batch_program` (Direct3D 11, RTX 5080, doubles and limbs), `gsr_test_vertex_batch_webgpu` on Vulkan and Direct3D 12: 2,451,056 vertices per path (120,240 generated, 2,330,816 recorded in 57,528 records; chains up to 24 passes), packet, previous position and row | 0 differ on every path |

## Not verified (running list)

* No shader path exists in the tree yet; `gpu` and `auto` resolve to the microprogram on every backend.
* `records` evaluates with double arithmetic on the GS worker and is slow; it is a diagnosis mode.
