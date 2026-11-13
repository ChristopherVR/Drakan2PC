# GPU vertex path on WebGPU: strip records drawn by the generated WGSL in a vertex stage (2026-11-13)

Follows [gpu-vertex-implementation.md](../2026-11-12/gpu-vertex-implementation.md) and
[gpu-vertex-stages.md](../2026-11-12/gpu-vertex-stages.md) (the generated WGSL had only been run in a compute pass).
Base: commit 328a0df1 (main b9beb22e plus the Direct3D 11 strip draw). The WebGPU backend is also the Vulkan and the
Direct3D 12 path (wgpu-native) and, later, the browser's. Default behaviour is unchanged: `vertex_processing = cpu`,
`proven` stays false.

## 1. What was built

| File | What |
| --- | --- |
| `runtime/gs/src/vertex_program_wgsl_draw.h` (new) | The WGSL of a vertex-program draw: the renderer's draw program (fixed-function or shader blend) + the generated stages + `VpAttr` (four integer attributes of `VertexProgramVertex`), `vpDraw` (uniform slot 1: XYOFFSET and the first constants row), `vpConstant` over a storage buffer or an `rgba32uint` texture (binding 24), the entry point `vsProgram` |
| `runtime/gs/src/wgpu_vertex_program.cpp` (new) | The backend side: `vertexPrograms`, `vertexPathCapability`, `drawVertexProgram` and the members `gs_draw.inl` calls (`bindProgramDraw`, `drawProgramVertices`; `prepareProgramMotion` / `drawProgramVelocity` are stubs, see 3) |
| `runtime/gs/src/rhi.h`, `rhi_webgpu.cpp` | Integer vertex attributes (`ProgramDesc::AttributeType`), per-draw data for the vertex stage (`drawDataPath`, `writeDrawData`, `reserveDraw`), `drawIndexed`, pipelines created on a worker thread (`ProgramDesc::backgroundPipelines`, `pipelineState`, `waitPipelines`), `currentProgram`; bind groups keyed by a program id instead of its address (a released program's address could be reused by a new one and get its stale bind group: found by the new rhi test) |
| `runtime/gs/src/wgpu_backend_impl.h`, `wgpu_device.cpp` | `kCaps.vertexPrograms = true`, three `override` declarations, the block of program members, one `programGpu.reset()` in `~Impl` before the device |
| `runtime/gs/tests/test_vertex_program_webgpu.cpp` (new) | The vertex-stage proof (2) and `GSR_VP_BENCH` |
| `runtime/gs/tests/test_vertex_records.cpp` | `--device-loss` mode |
| `runtime/gs/tests/test_rhi.cpp` | check 9: integer attributes, draw data, indexed draw, background pipeline |
| `runtime/gs/CMakeLists.txt` | the new sources and tests: `vertex_program_webgpu_vulkan` / `_d3d12`, `vertex_records_device_loss_vulkan` / `_d3d12`, `rhi_texture_draw_data` |
| `docs/reference/env-flags.md` | `GSR_VERTEX_CONSTANTS`, `GSR_VERTEX_ARITHMETIC` and `GSR_VERTEX_PROGRAM_WAIT` on WebGPU |

How a batch is drawn: `drawVertexProgram` reserves room in the device's rings for everything the draw writes
(`reserveDraw`: a submission between the constants and the draw would let a later draw overwrite them), then calls the
shared `Impl::draw` with the three stand-ins. `bindProgramDraw` takes the program bound by `bindDraw` (fixed-function
or shader blend), copies the batch's constants into the draw data, binds the vertex-program twin and the `vpDraw`
uniform. `drawProgramVertices` draws the batch indexed when the pipeline of this pass's blend and depth state exists;
otherwise it evaluates the batch on the CPU (`vp::evaluate`, as `gs_core` would) and draws that triangle list with the
ordinary program under the same state, counted `not ready`, and moves the batch's vertices from the core's "drawn by a
shader" counter to "evaluated on the CPU". One summary line per session:
`[gsr-webgpu] vertex programs: N batches (V vertices) drawn by the shader, ...`.

The vertex stage hands the renderer's own vertex shader body exactly the vertex the CPU path puts into its vertex
buffer. The body is not written a second time: `vertexShaderBody()` copies the text of `vs` out of `kWgslGsCommon` at
start-up, so any change to `vs` (for instance the order proposal for the jitter sums) reaches this path without an
edit, and the comparison in 2 is against that same body. The inputs are built from bits (`vpSixteenth`: k / 16 as the
float's exponent less four; `vpDepth`: the nearest float of the GS depth in integers), so no compiler can fold the
stage's arithmetic into the body's. WGSL has no `precise`; the question "does the program entry give the clip
position bit for bit of the ordinary shader, plain and with the temporal AA jitter" is answered by measurement (2).

Constants path, decided once per device from the limits it reports (`rhi_webgpu.cpp createDrawData`): a read-only
storage buffer in the vertex stage when `maxStorageBuffersPerShaderStage >= 1` and the binding size is at least the
8 MiB ring, else an `rgba32uint` texture 4096 texels wide (128 rows) read with `textureLoad`. Native wgpu-native
devices are core WebGPU devices (no compatibility limits are requested):

| Device | Limits seen / specified | Path |
| --- | --- | --- |
| Vulkan, RTX 5080 | 524,288 storage buffers per stage, 2 GiB binding | storage buffer (measured) |
| Direct3D 12, RTX 5080 | 262,144, 2 GiB | storage buffer (measured) |
| browser, core profile | at least 8 per stage, applying to the vertex stage | storage buffer (by the specification; not run) |
| browser, compatibility profile | `maxStorageBuffersInVertexStage` may be 0; `maxTextureDimension2D` 4096 | texture (not run) |
| wgpu GL backend (Linux fallback) | may report storage buffers but have none in the vertex stage | the pipeline would fail and turn the path off (counted `internal`); not run |

`GSR_VERTEX_CONSTANTS=texture` forces the texture path where both exist; both paths are proven in 2.

Every failure turns the path off for the session with one log line and is counted: module creation (`internal`), a
pipeline the worker could not create (error scopes on the worker thread; `internal`), a ring that cannot hold a batch,
a draw that could not be recorded, a lost device (`device lost`). The CPU evaluation draws the batch that hit the
failure, so no frame loses a draw. Nothing is latched: the core asks `vertexPrograms()` for every record.

## 2. Proof

| What | Command | Result |
| --- | --- | --- |
| generated WGSL in a vertex stage (integer attributes, flat integer varyings, constants in a storage buffer and in an `rgba32uint` texture) against the CPU evaluation: packet and previous place | `GSR_VP_RECORDS=build/review/gpu-vertex/records/all.rec DRAKAN_GPU_BACKEND=vulkan\|dx12 gsr_test_vertex_program_webgpu` | Vulkan and D3D12, each path: 1,215,215 vertices (400,000 generated, 200,000 probes, 615,215 recorded), 0 differ |
| the draw's vertex stage against the renderer's vertex shader body fed the CPU-evaluated vertex as float attributes: clip position, colour, stq, fog | same | Vulkan and D3D12, both paths, each with no jitter, a short binary jitter and a temporal AA jitter (2 x 0.3721 / 1920, 2 x 0.1913 / 1080): 1,215,215 vertices, 0 differ (12 comparisons) |
| control of that comparison | same | the TAA jitter changes the clip position of 1,215,215 of 1,215,215 vertices against the unjittered output; all 1,215,215 outputs written on both sides |
| records test, the shader drawing | `GSR_VERTEX_PROGRAM_WAIT=1 gsr_test_vertex_records_webgpu --gpu-api vulkan` | 1,464 colour frames (10,354,032,640 pixels) and 288 depth pictures (2,036,858,880 pixels), 0 differ; 514,398 record vertices drawn by the shader (the backend's own count agrees: 0 on the CPU); negative controls detected (12 of 21 colour frames, 15 of 21 depth pictures differ); 7,872 checks, 0 failed |
| records test, Direct3D 12 | `--gpu-api d3d12` | the same totals: 0 pixels differ, 514,398 shader vertices, 7,872 checks, 0 failed |
| records test, constants in the texture | `GSR_VERTEX_CONSTANTS=texture GSR_VERTEX_PROGRAM_WAIT=1 ... --gpu-api vulkan` | the same totals, 0 pixels differ, 514,398 shader vertices |
| records test without the wait (pipelines pending: batches drawn from the CPU inside the shared draw) | `gsr_test_vertex_records_webgpu --gpu-api vulkan\|d3d12` | Vulkan: 8,268 batches (444,316 vertices) by the shader, 519 (34,184) from the CPU; D3D12: 1,071 (59,032) by the shader, 7,712 (418,964) from the CPU; both 0 pixels differ, 7,872 checks, 0 failed |
| Surdana capture with records, CPU evaluation against the shader | `replay_hash_check.py --backends vulkan,d3d12 --scales 1,3,8 --hd off --variant cpu:DRAKAN_VERTEX_EVAL=records --variant gpu:DRAKAN_VERTEX_EVAL=gpu,GSR_VERTEX_PROGRAM_WAIT=1` | 6 comparisons, 1,423 frames each, 0 differ; every gpu replay: 154,885 records, 11,625 program draws, 23,518,719 vertices by the shader, 0 by the CPU, no fallback |
| the same with `GSR_UPSCALE=dlaa` | `GSR_UPSCALE=dlaa ... --scales 3` | 2 comparisons, 1,423 frames, 0 differ, but the hashes equal those without it: the temporal AA was not active in the replay, so this is NOT a frame-level jitter proof (the jitter is proven at vertex level only, row 2) |
| the Surdana capture without the wait (as the game would run) | `DRAKAN_VERTEX_EVAL=gpu gsr_replay_webgpu <capture> --scale 3 --gpu-api vulkan\|d3d12 --hashes ...` against the `records` hashes | 1,423 frames, 0 differ on each; Vulkan 23,508,588 vertices by the shader and 10,131 on the CPU (14 records `not ready`), D3D12 23,208,837 and 309,882 (114 `not ready`) |
| "cpu equals before" | `replay_hash_check.py --caps build/review/perf-gpu/caps --backends vulkan,d3d12 --scales 1,3,8 --hd off --reference build/review/gs-share/hashes/n_base --jobs 4` | 28 of 30 comparisons 0 differ; `tab` at scale 8 lost its device on both APIs with four replays at scale 8 on the shared GPU (wgpu: "Parent device is lost" in a submit; no record in that capture, the vertex path never asked); rerun alone (`--captures tab --scales 8 --jobs 1`): 3,984 frames, 0 differ on each. In all 30 comparisons, 108,420 frames, 0 differ |
| device loss with the path on | `gsr_test_vertex_records_webgpu --device-loss --gpu-api vulkan\|d3d12` | both: frames 0 to 3 drawn by the shader (13,110 vertices before frame 3), the device destroyed at the end of frame 3, frames 4 to 7: 260 of 260 records counted `device lost`, 0 shader vertices, orderly end, 0 failed checks |
| rhi | `gsr_test_rhi` on Vulkan and D3D12, with and without `GSR_VERTEX_CONSTANTS=texture` | all checks pass (4 runs) |
| suites | `gsr_test_synthetic_webgpu --gpu-api vulkan\|d3d12`; `ctest` of the whole `runtime-tests` tree; `python scripts/run_tests.py`; `scripts/repository_guard.py` | synthetic: 361 checks, 0 failed on each; ctest: 186 tests, 0 failed, 7 skipped (need game data), including the new `vertex_program_webgpu_vulkan` / `_d3d12`, `vertex_records_device_loss_vulkan` / `_d3d12`, `rhi_texture_draw_data`; Python tests pass; guard: 0 prohibited |

## 3. Motion vectors

`WgpuBackend::Impl::kCaps.motionVectors` is false (the default of `BackendCaps`); `gs_draw.inl` calls
`prepareProgramMotion` and `drawProgramVelocity` only under `if constexpr (kCaps.motionVectors)`, so neither is called
on this backend. The portable temporal AA reprojects with the camera alone. The two members are empty stubs; no motion
vectors were built.

## 4. Cost

Surdana capture with records, scale 3, one replay at a time, `--bench` over all 1,423 frames (renderer thread, ms per
frame; `records` = the CPU evaluation in `gs_core`, the diagnosis mode the capture was recorded for, not the
microprogram, which a capture cannot replay):

| API | mode | mean (rounds 1 / 2) | median | p95 | thread CPU |
| --- | --- | --- | --- | --- | --- |
| Vulkan | records | 11.77 / 11.73 | 2.80 / 2.58 | 31.3 / 31.4 | 16.0 / 16.0 |
| Vulkan | gpu | 4.78 / 5.48 | 1.20 / 1.23 | 8.5 / 8.9 | 8.7 / 9.0 |
| D3D12 | records | 11.72 / 12.18 | 2.53 / 2.49 | 30.7 / 32.2 | 15.7 / 16.6 |
| D3D12 | gpu | 6.28 / 6.08 | 1.22 / 1.20 | 8.2 / 8.0 | 8.8 / 8.9 |

The gpu means include the first frame's wait for the pipelines (`GSR_VERTEX_PROGRAM_WAIT=1`). The GPU was shared
with other sessions (27 % busy at the start).

Shader throughput, the draw's whole vertex stage on lit strip vertices drawn as points
(`GSR_VP_BENCH=7 gsr_test_vertex_program_webgpu`, medians of 7 runs of 8 x 262,144): Vulkan 3,646,587 vertices per ms
(storage buffer), 3,376,513 (texture); D3D12 3,343,140 (storage buffer, runs from 1,063,734 to 3,730,260), 1,665,067
(texture). A Surdana frame has about 16,500 record vertices (23,518,719 over 1,423 frames): 5 to 10 microseconds of
vertex work at these rates.

Pipeline creation as the game sees it (`[rhi] pipeline ... after N ms in the background`): module creation (naga)
5 to 17 ms. Vulkan: the first pipeline 1.1 s on a cold driver cache (`vertex_program_webgpu` test, first run), 2 to
8 ms once the driver has cached it; in the records test 84 pipelines, median 2 ms, max 5 ms. Direct3D 12: the first
pipeline of each program on every device 1.8 to 3.0 s (24 devices in the records test: 1,769 to 3,006 ms), every
further pipeline of that program 1 to 3 ms, so wgpu-native compiles the vertex stage with DXC once per module and
device and reuses it across blend and depth states. In the Surdana replay without the wait: Vulkan 2 pipelines of 2 ms,
14 records drawn from the CPU; D3D12 1,468 ms for the first, 114 to 133 records (309,882 to 355,367 vertices) drawn
from the CPU meanwhile, no stall.

D3D12 cache: the cost is 2 to 3 s per start, in the background, with the CPU evaluation meanwhile; a disk cache is not
needed for correctness or smoothness. wgpu-native offers a pipeline cache for Vulkan only. If the first seconds matter,
the cheaper step is to start one pipeline right after the module is built, before the first record asks; not done.

## 5. Batch records

Not started: the batch draw on Direct3D 11 (`bdraw`) had no patch yet. What the WebGPU side needs from
`VertexProgramBatch` for batch records, following `gpu-vertex-batch-stage.md` section 4:

* the vertices as `vp::BatchVertex` rows with `sel.w` already the row of the record's first pass, relative to the
  batch's own table (the backend adds the first row of its draw data, as `vpDraw.offset.z` does for strips), 64 bytes
  each, all four rows integer attributes (`uint32x4`);
* the pass tables of the batch's records as one array of rows (13 rows per pass), so one `writeDrawData` carries them;
  a flag or a separate batch kind telling the backend that this batch is batch records (another entry point, another
  program twin: `vbEvaluate` instead of `vpEvaluate`, and no `vpDepth` rounding of Z: `VSBatch` uses `(float)z`);
* indices as for strips (a triangle list, 32-bit, base 0);
* the GS state rules (`stateTakes`) and the skip rule (`vp::passNeverSkips`) applied by the core before a record
  joins a batch, as for strips.

With that, the WebGPU side is: a second pair of programs (`vertexBatchWgsl()` + the same draw bindings + an entry
point over `vbEvaluate`), the program chosen in `bindProgramDraw` by the batch kind, and `shaderStages` gaining the
batch families. The rhi needs nothing more. The CPU fallback per pass needs `vp::evaluateVertex` instead of
`vp::evaluate`.

## 6. Browser

Not built and not run. What the path needs there:

* `rhi_proxy.cpp` answers `drawDataPath() = None` (the base class default), so in a browser the backend reports
  `backendHasPath = false` and records are evaluated on the CPU, counted `backend`. To turn it on, the proxy has to
  forward `drawDataPath` from the GPU thread's device facts at open, record `writeDrawData` / `reserveDraw` /
  `drawIndexed` into its batches (copying the data, as it does for `draw`), keep `currentProgram` on its side, and
  answer `pipelineState`. On the browser's GPU thread `concurrentPrograms()` is false and a background pipeline is
  created at once with `wgpuDeviceCreateRenderPipeline`, which the browser compiles later and may stall on at the
  first draw; `wgpuDeviceCreateRenderPipelineAsync` with a fact that flows back to the proxy is the browser's way to
  keep "not ready" honest. Not built.
* The proxy reports no device loss (`lost()` is false), so the path would not see one; the device-lost promise of the
  browser device would have to reach the proxy.
* Limits: core profile, storage buffer path; compatibility profile, texture path (the 4096-wide texture fits its
  `maxTextureDimension2D`). Integer vertex attributes, 32-bit indices, flat integer varyings and `textureLoad` of a
  `texture_2d<u32>` in the vertex stage are core WebGPU and allowed in compatibility mode.
* Unverified: Tint's compile time and correctness for the 46,000-character module, Dawn on its Direct3D 11 backend
  (FXC, which the native D3D12 path avoids because it may never finish a large shader), Firefox and Safari.

## 7. Not verified

* No live game run: no game build was made in this session, so the paused-frame toggles in Surdana and an interior
  with `renderer = vulkan` were not taken. The replay and records proofs are the evidence.
* The temporal AA jitter at frame level (the replay did not turn the temporal AA on, see the table). At vertex level it
  is proven on both APIs with jitter values that are not short binary fractions.
* That the ordinary entry point `vs` itself and its copy compile to the same instructions: the comparison is between
  the program's entry point and the same body called from a probe entry point; WebGPU has no stream output to read
  the real `vs` entry's clip position. The records test and the capture replays compare the real entry points through
  rasterisation (0 pixels differ).
* A device lost in the middle of a frame or while the pipeline worker runs: only the test destruction at a frame
  boundary (`GSR_TEST_DEVICE_LOSS`) was exercised. Real losses seen today (scale 8, four replays on a shared GPU) ended
  in wgpu-native's abort in a submit before any device-lost callback, with the path off (not caused by it, not fixed).
* The records test's own per-frame "drawn by a shader" totals miss the vertices this backend moves to "on the CPU" at
  a frame end (the move happens at the flush in vsync, outside the test's sampling window); the global counters and the
  backend's summary line agree (shader + CPU = 23,518,719 in every replay).
* Linux, the wgpu GL backend, AMD or Intel adapters through WebGPU, and any browser (section 6).
* Batch records on WebGPU (section 5).
