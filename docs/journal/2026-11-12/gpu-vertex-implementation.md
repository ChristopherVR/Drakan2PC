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

## Not verified (running list)

* No shader path exists in the tree yet; `gpu` and `auto` resolve to the microprogram on every backend.
* `records` evaluates with double arithmetic on the GS worker and is slow; it is a diagnosis mode.
