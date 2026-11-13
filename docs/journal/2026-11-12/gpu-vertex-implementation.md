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

## 6. Increment 5: the batch path at run time under `records` (no shader)

Written by the `live` subagent; design, the list of writers of VU memory, the proof and the measurements are in
[gpu-vertex-live-batch.md](gpu-vertex-live-batch.md).

* Before every MSCAL and MSCNT the hook (`vu/ps2_vu1_gpu_live.h`) reads the control word and drives the batch tracker
  with the stage families the word enables; taken vertex runs are deferred, index-list runs go to the renderer as
  batch records, and `gsr_core` (`vertexBatch`) evaluates them on the CPU and draws them through the ordinary vertex
  kick. Two more stage bits: `texgen`, `clipinside`; one more fallback counter: `stale input`.
* Under `cpu` the cost is one atomic load per run (`vpath::vuAttention`) and one per UNPACK.
* The mode can change at any run boundary. Leaving `records` writes what was deferred before the next run executes;
  the tracker then keeps following runs and writes, taking nothing, until every lane of VU memory is the
  microprogram's again, and only then stops (at once on foot in Surdana; not reached during a Ravenshold flight, so
  a session that used `records` there keeps paying the tracker step per run).
* The VIF reports every lane it writes (fast and scalar path), and so does the EE's direct access to VU memory.
  The report costs 0.3 to 0.6 ms per frame on the VU1 worker as written, more than the unpack itself; halving it
  with an inline range test is not done.
* `DRAKAN_VERTEX_SHADOW=1` builds the records beside a game that runs unchanged and compares them with the packets
  the microprogram sends, live. It found one fault the 16 recordings could not show (every clip-code index list
  reads the alpha lane of the last colour slot as a code, whether or not the batch filled it); fixed.
* `records` is a diagnosis mode and slow by construction: the CPU evaluation in double arithmetic runs on the GS
  worker, 220 to 330 ns per record vertex, which is 23 to 26 ms per world frame in Surdana and 7 to 8 ms in the Order
  of the Flame Ruined Village. The VU1 worker itself changes by minus 37 % (Surdana), minus 10 % (Ravenshold) and
  plus 14 % (Order of the Flame Ruined Village: 19,034 UNPACK reports and 1,856 record builds per frame). So the
  batch path only pays with a shader, and the per-frame bookkeeping of the hand-over is itself a cost to bring down.
* Decision taken by the orchestrator: `vpath::Capability::shaderStages` names the stage families a backend has a
  shader for, and `gpu` hands over only those by default. A record no shader can draw would be exact but much slower
  than the microprogram it replaced. An explicit `DRAKAN_VERTEX_STAGES` list is still taken as given.

## 7. Increment 6: strip records drawn by the vertex shader on Direct3D 11, with the upscaler on

Written by the `d3d` subagent on a private tree with the renderer restructuring applied, merged onto main by the
orchestrator after the restructuring's step 4.

* The shared draw code (`gs_draw.inl`) has four small hooks behind `kCaps.vertexPrograms`: a batch of records goes
  through the ordinary `Impl::draw` with three stand-in vertices over the scissor, so target, texture, blend, depth,
  alpha test, scissor and passes are the code every draw uses; the backend then binds the batch's own vertices and
  program (`bindProgramDraw`, `drawProgramVertices`) and its velocity pass (`prepareProgramMotion`,
  `drawProgramVelocity`). The neutral rules are in `vertex_program_draw.h` with their own test.
* Direct3D 11 (`d3d11_vertex_program.cpp`): integer limbs by default, both entry points compiled on a worker thread
  (15.5 s measured cold in a loaded live run, 5 to 28 ms from the shader cache), indexed draws, constants in a buffer
  the shader indexes from a base row. Until the shader is ready, and for everything listed below, the record is
  evaluated on the CPU and counted.
* Counted fallbacks: `draw state` (FST, a flat-shaded lit strip, a PRIM that is no triangle strip, a target under 512
  wide, a draw outside the world phase while a HUD or menu map could move vertices), `feature` (any ray effect,
  terrain detail or cliffs, the LOD statistic, the surface audit switches), `not ready`, `adapter`, `internal` (one
  log line, off for the session).
* Motion vectors with the upscaler on come from the vertex shader: the core records per record which object-motion
  scope it belongs to, `motion::ProgramHistory` keeps the previous constants and raw positions per scope key, and
  the velocity entry point evaluates the position twice and builds the same history value the CPU path computes.
  A plain float division in the shader was NOT the CPU's: 928,357 of 3,045,645 history values differed on the RTX
  5080 (the division is off by one last bit on about 30 % of pairs) and 42,116 on WARP (its unsigned-to-float
  conversion rounds differently from 2^31 up). The shader therefore divides the significands in integers with
  round to nearest even and converts depth in integers; after that 0 differ on both.
* Known differences from the CPU path's motion vectors, by design: validity is per vertex instead of per draw;
  correspondence is by equal raw texture rows instead of processed coordinates within 1e-4; one history entry per
  record instead of per run of records under a scope; the first world after a path switch has no history for strip
  draws (the two histories are separate).
* Not removed: the scene-model motion pass and the EE-side capture behind it (see "Not verified").
* `Capability::proven` is false, so `auto` still resolves to `cpu`; `gpu` hands over the strip stage only.

## 8. Increment 7: strip records drawn by the vertex shader on WebGPU (Vulkan and Direct3D 12)

Written by the `wdraw` subagent (opus); details in
[gpu-vertex-webgpu-draw.md](../2026-11-13/gpu-vertex-webgpu-draw.md).

* The same five calls implemented on the WebGPU backend (`wgpu_vertex_program.cpp`): the generated WGSL in a vertex
  stage, constants through a storage buffer where the device allows one in the vertex stage and through an
  `rgba32uint` texture otherwise, indexed draws, pipelines built in the background (first pipeline of the program on
  a device: 1.8 to 3.0 s on Direct3D 12, about 1.1 s on Vulkan with a cold driver cache; further states 1 to 8 ms).
  Until a pipeline exists the batch is evaluated on the CPU inside the draw and counted `not ready`.
* The rhi gained integer vertex attributes, per-draw data for the vertex stage, indexed draws and background
  pipelines, and a latent fault was fixed on the way (bind groups were cached by a program's address, which a new
  program can reuse).
* The backend has no motion vectors (its temporal anti-aliasing reprojects with the camera only), so the velocity
  calls are never made there.
* A device lost while the path is on ends in the counted fallback and an orderly session end (test
  `vertex_records_device_loss_*`). A real device loss under GPU oversubscription still aborts inside wgpu-native
  before any callback; the System log showed GPU driver resets (nvlddmkm event 153) at such moments, which take
  Direct3D 11 replays down as well. Not caused by this work and not fixed by it.
* The browser is not built or run; what the proxy needs is listed in the linked note.

### Landing of increment 7 (2026-11-13, by the orchestrator that took over)

The commit of the `wdraw` subagent (`44b6bea7`, never on `main`) was applied to `main` unchanged, with these
additions:

* **CI.** `vertex_program_webgpu_d3d12` accepts a software adapter, and a CI runner gives WebGPU on Direct3D 12 the
  Microsoft Basic Render Driver. On it the full test took 86 s in ctest and 142 s run directly on this machine. It now
  runs a reduced set on a software adapter (`vpcases::standardCases(true)`: a tenth of the generated vertices and
  probes) in 22 s. To time a software case on a machine with a graphics card, `DRAKAN_GPU_SOFTWARE_ONLY=1` (new, tests
  only) makes a WebGPU context that accepts a software adapter take only that. Timed on the software adapter:
  `rhi_texture_draw_data` 20 s, `vertex_stage_webgpu_d3d12` 27 s, `rhi` 14 s; `vertex_records --warp` (Direct3D 11)
  144 s while the machine was loaded (75 s on CI the day before). `vertex_records_webgpu_*` and
  `vertex_records_device_loss_*` skip without a hardware adapter.
* **Two crashes of `vertex_records_webgpu_vulkan`, cause inferred, not shown.** During the first proof run this test
  died with an access violation twice. Both times it was right after the log line "pipeline 'gs vertex program, hw
  blend' ready after 7 ms in the background", in scene `many` under mode `gpu` at scale 1. The first was in the whole
  `runtime-tests` run at about 08:20. The second was a separate run at about 08:32. At that time about 15 GPU
  processes of several agents shared the GPU at about 12.5 GiB:
  * At 08:30 the System log has five GPU driver resets (`nvlddmkm` event 153, 08:30:22 to 08:30:28), and the
    Application log has a LiveKernelEvent 141 (video engine timeout) with `WATCHDOG-20261113-0830.dmp`.
  * `vertex_records_webgpu_d3d12` ran 584 s in the same ctest run and ended in a fast-fail abort at that reset.
  * The second crash falls in the recovery window after the reset; the first came about ten minutes before it,
    while that run was going on.

  After the load came down, the test was clean:
  * 10 runs, 0 pixels differ in any of them: 2 full Release runs, 4 scale-1 runs and 4 full runs of a
    RelWithDebInfo build, under `cdb`.
  * A stress of the hand-over window: a test-only delay before a finished pipeline is published
    (`GSR_TEST_PIPELINE_DELAY_MS`, 0, 3, 15, 40 and 120 ms). 30 runs of scene `many` alone, with and without
    `GSR_VERTEX_PROGRAM_WAIT`. Then 10 runs of every scene at scale 1 without it, where the pipeline becomes ready
    in the middle of 64-frame switching sequences: 137 to 1,035 batches per run evaluated on the CPU while pending,
    then 1,883 to 2,723 by the shader, 488 colour frames per run, 0 differ. No crash and no driver reset during any
    of these runs.

  Read from the code:
  * Pipelines are inserted into the program's map under its mutex before the pending count drops. The draw looks
    them up under the same mutex.
  * Under `GSR_VERTEX_PROGRAM_WAIT` the program thread is joined at once.
  * Bind groups are keyed by a program id, not an address.

  No unsynchronised publish was found. The likely cause is the known end of wgpu-native on a device lost to a
  driver reset (see the device-loss point above); it is not shown.

  So that the next such crash leaves a stack, the records tests and both replays now write a minidump when they die
  of an unhandled exception (`tests/crash_dump.h`; `GSR_CRASH_DUMP_DIR`, `GSR_CRASH_DUMP=0`; nothing is written
  otherwise). A fast-fail abort leaves none.

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
| 2026-11-12 | Increment 5, as reported by its author (commands: gpu-vertex-live-batch.md section 6): shadow comparison over 33,762,388 runs (Surdana with fight, quick save and load; a level change; Ravenshold flight; Order of the Flame Ruined Village; the intro cut scene and the Isles) | 12,619,604 records, 559,825,390 vertices compared, 0 differ; 12,949,714 runs executed on the shadow memory, 0 with other packets, 0 lanes; 0 unreported writes |
| 2026-11-12 | Increment 5, same author: paused toggles microprogram against records, 24 shots each, in Surdana, the fight spot, Ravenshold in flight, the Lair arena, Order of the Flame Ruined Village and Volcanic; twelve stage masks in Surdana; three captures under `records` replayed on Direct3D 11 at scale 3 | every scene one picture (Catacombs only with an animated torch flame masked); captures 1,657 + 1,505 + 1,796 frames replay without error |
| 2026-11-13 | Increment 5, re-run by the orchestrator on a game build of the main tree: VU1 replay of the 16 recordings; shadow comparison, Surdana 60 s walk (4,028,012 runs); paused toggle over Ravenshold, 24 shots (5 microprogram, 19 records) | replay: 2,604,739 batch and 708,250 strip vertices, 0 differ, 0 other packets, 0 lanes; shadow: 1,344,549 records, 67,767,301 vertices, 0 differ, 0 other packets, 0 lanes; toggle: 24 of 24 identical |
| 2026-11-13 | Increment 5 on the main tree: `runtime-tests` preset (178 tests, 0 failed), Python tests, and "`cpu` equals before": five captures on Direct3D 11, Vulkan and Direct3D 12 at scales 1, 3, 8 without HD textures against the reference | 45 comparisons, 162,585 frames, 0 differ |
| 2026-11-13 | Increment 6 (Direct3D 11 strips), by the orchestrator on the merged tree and again on the new main: `runtime-tests` 181 of 181; `vertex_program_history` on the RTX 5080 and WARP (3,045,645 history pairs); `vertex_records` with 493,806 vertices drawn by the shader; the Surdana capture with records at scales 1, 3, 8 without HD textures, CPU evaluation against shader (23,518,719 vertices by the shader per replay, no fallback); "`cpu` equals before" on Direct3D 11, 15 comparisons | 0 history values differ; 0 of 5,983,272,960 colour pixels and 0 of 1,230,602,240 depth-picture pixels differ; 3 x 1,423 frames, 0 differ; 15 comparisons, 0 differ |
| 2026-11-13 | Increment 6, as reported by its author: paused toggles microprogram against shader in Surdana, the Inquisitor's Lair and the Northern Tundra (1280x960, DLSS off); paused Surdana with DLSS quality and a fixed jitter sample, ten worlds dumped; three more captures with records at scales 1 and 3 | 10 of 10 identical in each level; prepared colour, depth, motion, model motion and the raw velocity target: 0 pixels differ between 5 microprogram and 4 gpu worlds; 6 comparisons, 7,696 frames, 0 differ |
| 2026-11-13 | Increment 6 on a game build of main (07ddda79) by the orchestrator: paused toggle in Surdana, 24 shots, twice (first run compiles the shader: 101,857 records `not ready`; second from the cache); a 30 s moving run and a still in the Lair arena at 3840x2160 with DLSS quality and `vertex_processing = gpu` | 24 of 24 identical in both runs (13 gpu / 11 microprogram and 10 / 14; 24.8 and 57.9 million vertices by the shader); 140.6 million vertices by the shader in the moving run, no fallback beyond `stage off`, `clipper` and 24 `not ready`; the 4K picture looked at: as expected |
| 2026-11-13 | Increment 7 (WebGPU strips), by the orchestrator on the merged tree: the vertex stage against the CPU evaluation and against the renderer's own vertex shader, plain and jittered, on Vulkan and Direct3D 12 (1,215,215 vertices each); `vertex_records` on both with 514,398 vertices drawn by the shader; the Surdana capture with records on Vulkan and Direct3D 12 at scales 1, 3, 8; `runtime-tests` 186 of 186; "`cpu` equals before" on three backends, 45 comparisons (one Direct3D 12 replay at scale 8 lost its device under load and was identical when rerun alone) | 0 vertices differ; 0 of 10,354,032,640 colour and 0 of 2,036,858,880 depth-picture pixels differ; 6 x 1,423 frames, 0 differ, 23,518,719 vertices by the shader each; 45 comparisons, 162,585 frames, 0 differ |
| 2026-11-13 | Increment 7 at its landing, by the orchestrator that took over, on main `0fb03d2d` with the commit applied, one GPU process at a time after 08:35: vertex stage and draw program on Vulkan and Direct3D 12 (`GSR_VP_RECORDS`); `vertex_records` on Vulkan and Direct3D 12; the Surdana capture with records at scales 1, 3, 8 on three backends; live paused toggles `gpu` against the microprogram every 64 frames in Surdana (1280x960, DLSS off) on Vulkan and on Direct3D 12 from a game build of the tree | 1,215,215 vertices each, 0 differ; 514,398 vertices by the shader, 0 of 10,354,032,640 colour and 0 of 2,036,858,880 depth-picture pixels differ, 7,872 checks, each backend; 9 x 1,423 frames, 0 differ, 23,518,719 vertices by the shader per replay, no fallback; Vulkan 24 of 24 identical (12 gpu, 12 microprogram; 46,995,581 vertices by the shader), Direct3D 12 24 of 24 (12 / 12; 53,577,990) |
| 2026-11-13 | The same landing: "`cpu` equals before" on three backends, five captures at scales 1, 3, 8 without HD textures against the `1fa0fc3e` reference | 39 of 45 identical; the 6 that differ are the ones `dc6d3fa4` (WebGPU clean-block retention and partial uploads, on main before this) recorded as changed by design: Order of the Flame frames 1201 to 1260 at scales 3 and 8 and 396 cave frames at scale 8, on Vulkan and on Direct3D 12. The same cave and Order of the Flame replays at scales 3 and 8 on Vulkan and Direct3D 12 by a build of main `0538bb95` without the change give the same hashes as the change: 8 comparisons, 0 differ, so the 6 predate it. The reference of "`cpu` equals before" for WebGPU needs re-taking after `dc6d3fa4` |
| 2026-11-13 | The same landing: `runtime-tests` on main `2653a797` with the change | 186 of 187; `world_readback` failed once (its ray pass stuck 21 s under load, Direct3D 11 and 12, not the vertex path) and passed alone twice. Python tests OK; guard 0 prohibited |

## Not verified (running list)

* `Capability::proven` is false on every backend, so `auto` resolves to the microprogram everywhere; `gpu` hands over the strip stage on Direct3D 11.
* `records` evaluates with double arithmetic on the GS worker and is slow; it is a diagnosis mode.
* The DLSS CPU overhead is not removed by this path yet. With DLSS Super Resolution on, the scene-model motion pass
  (`finalizeNeuralWorld`, `Guides::modelMotion`) draws model triangles over the same motion target, so for
  characters, which are the strip objects, the upscaler mostly gets the model path's vectors and the EE-side
  scene-model capture has to keep running. Removing it needs a link per instance between an object-motion scope and
  a scene-model instance (`ObjectMotionScope.objectId / generation` to `SceneModelInstance.object / generation`), a
  rule that the model pass leaves out instances fully drawn by records, and a proof per pixel that the vectors made
  by the vertex shader equal the model pass's for those instances. Not built.
* Reference hashes with HD textures: the shared HD pack was regenerated on 12 November and staged into play folders
  by later builds, so HD-on references made before about 21:10 that day match no build any more. References here
  are HD off from increment 5 on; HD-on coverage returns with the frozen pack `build/review/gs-share/textures_frozen`
  and its re-taken baseline.
* A latent exactness fault of the strip path, found while the batch path was built and not fixed yet: a batch of
  strip records is drawn through stand-in vertices that span the scissor, so a draw whose vertices all lie outside
  the scissor is executed where the packet path skips it (what the target remembers afterwards can then differ).
  Strip objects have passed the engine's box test as inside the view, and no capture, test or live comparison has
  shown a difference; the batch path, where it did show (473 of 2,088 frames of a flight capture at scales 3 and 8),
  got exact bounds. The same bounds are still to be applied to strips.
* With the upscaler on, moving motion vectors made by the vertex shader are proven at vertex level (history values)
  and on paused worlds (all vectors zero) only; a capture replay has no object-motion scopes, so the motion target
  of a moving world was never compared pixel for pixel between the two paths.
* Under `gpu` with only the strip stage handed over, the batch tracker still follows every run and every UNPACK
  report (0.3 to 0.6 ms per frame on the VU1 worker) although it can take nothing. To remove.
