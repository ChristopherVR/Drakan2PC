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

## Proof log

| Date | What | Result |
| --- | --- | --- |
| 2026-11-12 | Surdana capture with records (1,423 frames), records evaluated on the CPU, Direct3D 11, scale 3, HD textures: frame hashes against the study's CPU-evaluated run | 1,423 frames, 0 differ |
| 2026-11-12 | Live, Surdana, paused, 1280x960, scale 3, DLSS off, `DRAKAN_VERTEX_EVAL=records DRAKAN_VERTEX_TOGGLE=64`: ten screenshots, 4 taken under `records` and 6 under `microprogram` | the 8 shots after the quest notice left the screen are identical (4 records, 4 microprogram); 104,339 strip runs handed over, 3,995 not recognised and run by the microprogram |
| 2026-11-12 | The same live run again with the final build of increment 1 | the 9 shots after the notice are identical (4 records, 5 microprogram); 96,246 strip runs handed over, 3,703 not recognised |
| 2026-11-12 | "`cpu` equals before": five captures without records (Surdana, cave, flight, inventory, Order of the Flame; `build/review/perf-gpu/caps`) replayed by this build on Direct3D 11, WebGPU on Vulkan and WebGPU on Direct3D 12 at scales 1, 3 and 8 with HD textures, against the hashes the renderer restructuring session made with a build of `1fa0fc3e` (`scripts/harness/replay_hash_check.py --reference`) | 45 comparisons, 162,585 frames, 0 differ |
| 2026-11-12 | `runtime-tests` preset in a private tree of the main tree (111 tests): all pass except `ray_water_lost_signal` (timeout) and `spu2_test` under four parallel jobs on the loaded machine; both pass when run alone. `python scripts/run_tests.py`: OK. `scripts/repository_guard.py`: 0 prohibited | |

## Not verified (running list)

* No shader path exists in the tree yet; `gpu` and `auto` resolve to the microprogram on every backend.
* `records` evaluates with double arithmetic on the GS worker and is slow; it is a diagnosis mode.
