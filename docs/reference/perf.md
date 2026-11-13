# Performance work (branch `drakan-perf` of tools/PS2Recomp, worktree `tools/PS2Recomp-perf`)

Machine: 8 cores / 16 threads, RTX 5080, MSVC 14.44, `PS2X_GS=d3d11`, audio off for measurements.
Build dir: `build/recomp_perf` (scripts: `scripts/harness/build_game.ps1 [-Gen]`, `scripts/perf/perf_run.ps1`, `scripts/perf/perf_stab.ps1`, `scripts/perf/perf_bench_build.ps1`).

## What the game targets
The title menu flips DISPFB2 every 2nd vblank: **30 fps (2 vblanks per frame, 60 Hz vblank)**. Slower scenes drop to 20, 15 ... fps
in multiples of the vblank. "frames/s" below = DISPFB2 changes per second, "vblank/s" = guest vblanks that were actually delivered.

## Numbers (Level 0, after the intro book; menu in brackets)
| | baseline (drakan @ 71b498c, /O2) | now |
|---|---|---|
| Level 0 frames/s | **3.1** (6 vblank/s) | **~25** (50 vblank/s) |
| Level 0 EE thread CPU / frame | 157 ms (100 % busy) | ~20 ms per vblank, still 100 % busy (VU1 bound) |
| VU1 throughput | 18 M VU cycles/s | ~66 M cycles/s (~62 M pairs/s); 30 fps needs ~82 M |
| menu frames/s | 28.8 | 28.8 (locked) |
| menu EE thread CPU | 25 % (4.4 ms/flip) | 6-7 % (1.1-1.2 ms/flip) |
| boot to menu, EE thread CPU (first 15 s) | 3.45 s | 1.2 s |
| full /O2 build (runtime + 82 generated TUs) | ~9 min | 4 min 15 s - 5 min |
| exe size | 89.4 MB | 62.6 MB |
| generated sources | 161 MB (with comments) | 100 MB (`emit_comments = false`) |

Where the time was (host RIP sampler, `PS2X_HOSTPROF`): in Level 0 **79 % of the EE thread was the VU1 interpreter** and ~12 % the GS
renderer; generated EE code was ~1 %. So the EE codegen work matters little for Level 0 but halves boot/menu cost; the frame rate was
decided by VU1 and GS. The baseline also advanced the game clock slowly because vblanks are gated on charged EE cycles.

## What changed
Emitter / EE runtime
- `__restrict` ctx/rdram; function entry is `if (ctx->pc != start)` (resume switch only then), no `ctx->pc = start` store.
- `JAL` to known functions and `JALR`: inline `PS2_JAL` / `PS2_JALR` through the dense function table (runtime overrides still apply),
  `PS2Runtime::cpTick` batches the checkpoint accounting (timers + IOP are advanced every 256 charged cycles instead of every call).
  `PS2X_TRACE_CALLS` still works (`g_ps2DispatchTrace`).
- Memory macros: no lambdas, one inline test (`Ps2InFastRam`), cold paths out of line; GPR accessors write 64 bits instead of
  read-modify-write of 128; fixed function-table base `PS2_FNTAB_BASE` (static_asserted in the generated table).
VU1 (all bit-exact against the old interpreter, see below)
- stall model as register-busy bit masks + retire ring; lazy MAC/status/CLIP FIFO; VF/ACC/VI/store results applied at issue
  (they are interlocked, only timing is modelled); vectorised FMAC ops with exact flag classification (double precision only in rare
  cases), vectorised MAX/MINI/ITOF/FTOI/ABS; decode-time summaries; no 64 KB memset per XGKICK.
- **AOT-specialised microcode** (`vu/aot`): the game's VU1 microcode is a fixed 1523-pair image (`analysis/vu/vu1_micromem.bin`,
  `scripts/vu1_aot_image.py`). Each pair has its own function = the interpreter step with the decoded pair as a compile-time constant.
  Used while VU1 memory equals the image; falls back to the generic loop otherwise. `DRAKAN_VU_AOT=0` disables.
- `vu/ps2_vu1_decode.h` (constexpr decode shared by runtime and AOT), `ps2_vu1_step.h` (the pair step), `ps2_vu1_fast.h`.
GS: `GsrBridge` runs the renderer on a worker thread fed by an ordered record queue (`PS2X_GS_ASYNC=0` = old synchronous behaviour);
local->host reads drain first. Moved ~17 % of the EE thread to another core.
Instrumentation: `PS2X_PERF=1` (per second: frames, vblanks, GIF, VU1 cycles, thread CPU), `PS2X_HOSTPROF=<s>` host RIP sampler with
call-site for system DLL samples (`scripts/hostprof*.py`, needs the linker .map, now enabled), `DRAKAN_VUCHECK=1` differential test.

## Correctness checks
- `DRAKAN_VUCHECK=1` runs every VU1 microprogram on both the old interpreter (`vu/ref`, copy of the original) and the new one and compares
  registers, cycle count, VU memory and every XGKICK packet: 130k+ runs per boot, **0 mismatches** after every step (the checker found
  two real bugs during development, a stall-ordering case and a cycle-count case).
- Screens: logo movie, menu, intro book render as before (`PS2X_GS_SHOT`). 10 consecutive boots to Level 0: 9 reached steady state.
- **Pre-existing hang, not caused by this work:** ~25 % of boots (baseline /Od build: 2 of 8; this branch: 1 of 10 to 3 of 8 depending on
  timing) die while loading Level 0 with `[guest-branch:missing-target] JALR 0x12c4d8 target=0` (null vtable slot, `s0=3`
  in `FUN_0012c3f0`, a resource-manager entry lookup, `FUN_0012b640`). It looks like a race between the HLE resource completion
  threads and the game thread. Needs its own investigation.

## Build time (generated code only, `perf_bench_build.ps1`, 82 TUs)
| variant | time |
|---|---|
| /O2, 150 functions/TU (default) | 234-273 s |
| /O2 /Ob1 | 232 s (no effect: calls go through the table) |
| /O1 | 288 s, bigger exe |
| /O2, 75 functions/TU (163 TUs) | 247 s |
| /O2, 300 functions/TU (41 TUs) | 292 s |
| /Od | 145 s, 267 MB exe |
`/MP` is already used. Keep /O2 and 150. `emit_comments = false` shrinks the sources 161 -> 100 MB. Delete `gen/` before changing
`functions_per_file` (the recompiler leaves stale files).

## Remaining hotspots (Level 0)
1. VU1 still ~75 % of the EE thread: per pair ~70 host cycles. Next steps: out-of-line slow paths are done; remaining cost is
   scheduleWrites/stall bookkeeping, flag FIFO settle, XGKICK progress, the 2 KB per-pair functions (I-cache). A real JIT or a
   block-level AOT with static pipeline state would be the next 2x; VU1 on its own thread only helps ~20 %.
2. GS worker thread ~60 % of a core (CPU texture swizzle `pixelAddr`/`LocalMemory::write`, readbacks).
3. IPU/FMV decode (~5 %) is active in Level 0.
4. `PS2X_GS=sw` (default backend) was not tuned.

## Portability notes (docs/reference/portability.md)
- No clang available on this machine (`clang-cl` not installed), so the clang compile check was **not done**.
- Violations: VU code uses SSE2/SSE4.1 intrinsics directly (`ps2_vu1_*`, `ps2_runtime_macros.h`), no wrapper header yet; the macros use
  `PS2_FORCEINLINE/PS2_NOINLINE` and `PS2_VU_*` portable macros (clang attributes behind `#else`), `__restrict` is clang-compatible.
  Async GS has the `PS2X_GS_ASYNC=0` single-thread switch; no host pointers were added to guest-visible state.
  Generated code only gained table calls (`g_ps2RecompiledFunctionTable`), standard C++.
- `ps2_perf.cpp` is Windows-only (guarded by `_WIN32`).

## Expected merge conflicts with `drakan`
`ps2xRuntime/CMakeLists.txt` (source list), `ps2_runtime.cpp` (VU1 callbacks, `dispatchGuestBranch`, perf start), `ps2_runtime.h`
(`cpTick`, macros at the end, `PS2_FNTAB_BASE`), `gs_frontend.cpp` (`hostTick`, counters), `gs_gsr_bridge.cpp` (rewritten around the worker
queue; any change to the present/frame pacing in the bridge or `runtime/gs` API needs re-checking against the queue), `ps2_memory.cpp`
(perf counters), generated-code consumers of `READ*/WRITE*` macros. Regenerate the code after merging (emitter changes).

---

# Round 2 (branch `drakan-perf2` of tools/PS2Recomp, worktree `tools/PS2Recomp-perf2`, build dir `build/recomp_perf2`)

Goal: 30 fps (60 vblank/s) in the Level 0 3D scenes with headroom. Machine and method as above (`PS2X_PERF=1`, audio off, `ng.txt` New Game
script, steady-state average of seconds ~25-70). Measured on the merged `drakan` tip including the VIF1 DIR=0 fix (cf73563), whose workload is
lighter than the one measured in round 1 (no junk runs).

## Numbers (Level 0 intro, 3D book / cutscene)
| config | flips2/s (fps) | vblank/s | EE thread CPU | other threads |
|---|---|---|---|---|
| round 1 end (merged build, per-pair AOT, VU1 on the EE thread) | 23-24 | 46-48 | 100 % | GS ~60 % |
| `DRAKAN_VU_BLOCKS=0 PS2X_VU1_THREAD=0` (per-pair AOT + inlined FMAC flag tier + GS fixes) | 28.8 | 57.5 | 100 % | GS ~70 % |
| blocks, `PS2X_VU1_THREAD=0` | **30.0** | **60.0** | **87 %** | GS ~70 % |
| blocks + VU1 thread (default) | **30.0** | **60.0** | **16 %** | VU1 worker ~73-85 % (of which ~27 % parked), GS ~73 % |
| VU1 thread without blocks | 27.6 | 55.3 | 17 % | VU1 worker 100 % (bound) |

Offline VU1 benchmark (4000 recorded Level 0 runs, 6.9 M pairs, one core): per-pair AOT 11.4 ns/pair (88 Mpairs/s) -> 9.4 (flag tier inlined, /GS-)
-> **7.4 ns/pair (135 Mpairs/s, 141 Mcycles/s)** with blocks. 30 fps needs ~83 Mpairs/s.
Headroom: the frame rate is capped by guest vblank pacing at 60, so the table shows load, not the ceiling. The EE thread is nearly free with the
VU1 worker; the worker has ~25 % spare, i.e. ~38 fps worth of VU1 time.
Stability: 10 consecutive boots to Level 0, 10/10 reached the steady state (30.0 fps each, no errors); `DRAKAN_VUCHECK=1` in game: 245k runs, 0
mismatches (threaded); offline bench vs the reference interpreter: 4000 recorded runs, 0 mismatches (registers, cycle count, VU memory, XGKICK
packets); display of frame 2600 of the intro is pixel-identical with `PS2X_VU1_THREAD=0` and `1`; `listener_repro.sh` stress mode
(`DRAKAN_RES_LATENCY_US=0`): 0 of 6 boots lost the main thread.

## What changed
1. **Block-level AOT of the VU1 microcode** (`vu/aot/ps2_vu1_blocks.h`, `ps2_vu1_aot_slice.inc`, `Vu1Step` block support in `ps2_vu1_step.h`).
   Blocks over 8 pairs compile as out-of-line chunks of 8 (`PS2X_VU1_AOT_CHUNK`, 0 = whole blocks): ~1.9 GB per slice in cl instead of up to
   17.9 GB, replay speed unchanged (docs/journal/2026-11-06/vu1-compile-cost.md).
   The compiler (constant evaluation, same `Vu1Decode` data as the exact path) splits the 1523-pair image into blocks (cut at jump targets, after
   branch + delay slot, around pairs it does not handle, 32 pairs max) and computes the issue cycle of every pair for an idle-pipeline entry using the
   interpreter's stall rule. A block runs `Vu1Step::execPair` (the very code of the exact path) with those constants, then updates the busy-register
   state once (`blockExit`). Entry check `blockCanEnter`: no pending Q/P/stores (an FDIV in flight is allowed for blocks that read neither Q nor status
   flags and is committed at its own cycle at block end), no E-bit/branch state, budget, and the busy registers the block reads in its first 3 cycles are
   ready. Otherwise the pair-at-a-time path runs (still bit-exact). Not in blocks: FDIV/EFU/XGKICK/WAITQ/WAITP pairs, E/D/T-bit pairs, reserved encodings.
   `DRAKAN_VU_BLOCKS=0` disables. About 95 % of the pairs run in blocks.
2. **Lazy XGKICK progress** (`Vu1Fast::xgkickCatchUp / xgkickBeforeStore`): the PATH1 copy is applied when a VU data store would hit a qword that is already
   due, at a new XGKICK and at the end of the run, instead of every cycle (cycle counts and packets stay identical).
3. FMAC flag classification: the "class known without double precision" tier (zero lanes) is inlined (`flagsKnownClasses`, shared with `fmacSlow`); the
   normalisation helpers are force-inlined (the block functions are large and MSVC had stopped inlining them); `/GS-` for `vu/*.cpp`.
4. GS path: `GifArbiter` reuses its packet buffers (no malloc per XGKICK packet), the GS worker is woken only when it sleeps (was one kernel call per packet).
5. **VU1 on its own thread** (`runtime/ps2_vu1_async.h`, `vu/ps2_vu1_async.cpp`, `PS2Memory::dispatchVif1/runVif1Job`). The VIF1 stream of a DMA transfer is copied at
   the CHCR write and run in order by a worker (UNPACK, MPG, MSCAL/MSCNT, DIRECT, XGKICK, arbiter drain). DMA completion is still signalled at the kick. The EE thread
   waits (`vu1async::sync()`) before: any access to VU1 memory (`mapVuMemory`), VIF/GIF registers, `GIF_STAT`/`isPath3Masked`, PATH3 / VIF0 DMA and native GIF chains,
   every EE-facing `GS::` call (register writes, uploads, local->host reads incl. the VIF1 DIR=0 readback, VRAM peeks, FB clears), `submitGifPacket`, and at a vertical
   blank where DISPFB changed (a flip shows a finished frame). The job carries the EE context and the FBRST D/T bits captured at the kick (VPU_STAT is reported there).
   `PS2X_VU1_THREAD=0` = old single-thread behaviour (docs/reference/portability.md rule 3: no code outside this switch blocks on the worker; core code only calls `sync()`).
6. Offline harness (`vu/ps2_vu1_check.cpp`): `DRAKAN_VU_RECORD=<file>[,skip,count]` records VU1 runs of a live session, `DRAKAN_VU_BENCH=<file>[,reps]` replays them, checks
   against the reference interpreter and prints ns/pair (`DRAKAN_VU_BENCH_NOCHECK=1`; `PS2X_HOSTPROF` works inside the bench).
   `DRAKAN_VU_STATS=1` enables block statistics and fallback analysis; these diagnostics are off during normal gameplay.
   Build with `build/recomp_perf2/build.ps1`; touching `ps2_vu1_step.h` rebuilds the 4 AOT slices (~5 min), a full header change ~4-6 min.

## Vector CPU continuation (2026-10-18)

The compiled image now folds architectural VF00 source reads, specializes lane
writes and saturating conversions, and aggregates already-due FMAC flags in
blocks without internal flag observers. A guarded 17-pair packet-emission path
uses one entry schedule and complete prefix exits for alternate branches.
Q-sensitive blocks retire pending scalar results at their exact cycles; short
blocks cancel superseded entry deadlines before installing newer writes.

`DRAKAN_VU_TRACE=0` disables the guarded packet path. `DRAKAN_VU_TIMED_Q=0`
keeps Q-sensitive blocks on the exact path while a division is pending.
`PS2X_VU_SPIN=0` disables the bounded handoff spin; the measured default is
4096 PAUSE iterations, capped at that value, before an OS wait. These controls
support performance diagnosis without changing render quality or guest timing.
`DRAKAN_VU_STATS=1` still opts into diagnostic block counters.

Recorded-program differential replay, synthetic pending-scalar/sticky-flag
cases, dependent register reads, and live comparison cover registers, memory,
PATH1 packets and cycle counts. Deployment measurements and receipts are in
`docs/journal/2026-10-18/data/vector-specialize-20261018.json`.

## Remaining hot spots (earlier block baseline)
VU1 worker: ~23 % of its time is still exact-path pairs (FDIV pairs and the blocks that read Q while a division is in flight, read-conflict entries, ~270k of 1.37 M
block entries per 4000 runs), then FMAC flag FIFO bookkeeping (`settleFlags` 4 %) and `processVIF1Data` UNPACK (4 %, generic per-vector loop). GS worker ~73 %.

## Merge notes (drakan-perf2 -> drakan)
Merged with `drakan` up to cf73563 already (VIF1 DIR=0, call-completion fix; no emitter or call-path changes in this branch, generated code untouched, no regeneration needed).
Touched outside `vu/`: `ps2_memory.h/.cpp` (VIF1 jobs, sync points), `ps2_runtime.cpp` (VU1 callbacks use the job meta, meta provider), `gs_frontend.cpp` (sync calls), `gs_gsr_bridge.cpp`,
`ps2_gif_arbiter.*`, `CMakeLists.txt` (new source, `/MP`, `/GS-` and `/constexpr:steps` for `vu/*.cpp`). A semantic VU1 fix (vu1_fix.patch) applies to `execPair`/`upperFmac`/`lower`/
`execLower` unchanged (the block code only supplies cycles); if it changes a latency or register-usage rule in `ps2_vu1_decode.h` the block tables follow automatically.

## Default native performance mode (2026-10-18)

The exact CPU optimizations are standard in all modes. New and missing-key
configurations now select uncapped world cadence. The frame hook, executor
vblank policy and GS handoff read the same configuration default. Explicit
`world_rate = 30` retains the original cadence and cycle-based vblank policy;
invalid values still fall back to 30. The F1 menu labels uncapped as default,
retains the original 30 and capped 60 options, and requires a restart.

`Play.cmd` and the historical `Play-RawFPS.cmd` alias respect saved settings.
`Play-Original30.cmd` overrides world cadence and presentation to 30 FPS for a
single session without rewriting the INI. No CPU optimization switches are
changed. Existing graphics preferences are preserved by normal launch.

Validation evidence: `docs/journal/2026-10-18/data/default-performance-20261018.json`.

### World-rate velocity correction (2026-10-23)

`FUN_00325d30` turns a tick's displacement into units per second with a multiplier capped at 30
(`min(30, 1 / (engine+0x2c * 0x512db4))`). Above 30 Hz that under-reports speed by `30 / rate`; the visible symptom
was running jumps going straight up (jump start needs a forward speed above 1000). With `world_rate` 60 or uncapped,
`drakan_frame_rate.h` rescales the result by `rate / 30` (rate capped at 1000 Hz, untouched while paused). Details:
`docs/journal/2026-10-23/running-jump.md`.

Audit of other 30 Hz assumptions (`docs/journal/2026-10-23/world-rate-audit.md`): the engine is otherwise
frame-time correct (on-foot movement, Arokh flight, NPC walking and combat match at 30 Hz and uncapped). Two more
fixes: `windHook` (0x141d80, wind direction eased 25% per tick, now `0.75^(dt*30)`) and the stick-mode mouse in
`runtime/input` (full deflection per 1/30 s instead of per update, so Arokh mouse steering keeps its rate).

Attack + jump freeze (2026-10-25): the in-air update `2fa328` starts the in-air loop when the takeoff animation is
within 0.03 s of its end, after setting layer 0's speed to 0 (`450cf0`). A 30 Hz tick steps over that window, so the
call is a no-op; above 30 Hz it lands inside it while the takeoff still plays, the speed 0 reaches the attack layer of
a swing that the jump interrupted, and `3253a0` keeps it: Rynn hung in the jump pose for ~1.8 s. `layerSpeedHook`
skips that one call while an attack layer is active. Details: `docs/journal/2026-10-25/attack-jump-freeze.md`.

## Arithmetic input normalization (2026-10-19)

Compiled VU blocks now track which VF and ACC lanes already contain normalized
arithmetic results. A compile-time proof skips repeat operand normalization only
when all required lanes are known safe. Memory loads and integer conversions
invalidate the affected lanes; each block starts with unknown incoming values.
The arithmetic, flags, scheduling, recovery paths and guest cycles are unchanged.
The fixed program removes 235 of 757 eligible operand normalization operations.

Clean alternating baseline/optimized/optimized/baseline gameplay measurements
averaged 63.75 to 65.54 raw world FPS (2.81%);
4x scale, 4K window, audio on, no frame generation. This remains below 144 FPS
and establishes performance only for the measured scene. Atomic completion waits,
packed flag queues, local destination refactoring and lazy reset were not retained
because their throughput gains were absent or inconsistent.

Validation includes 200,000 randomized comparisons across four host rounding
modes, 8,192 scalar/flag timing comparisons, four 4,000-run reference replays,
a live reference check and seven standalone CPU suites. Configuration and saves
are preserved. Uncapped remains the default; `Play-Original30.cmd` retains the
30 FPS option. Evidence: `docs/journal/2026-10-18/data/fps144-20261018.json`.


## Slow walking and aspect stalls, 19 October 2026

The installed build was tested with the current save, scripted movement, audio,
4x internal resolution, a 3840x2160 window target, DLSS Quality and neural
rendering off. Last-five-window diagnostic rates were 26.65 world FPS with
water/reflections/shadows, 31.98 with shadows off, and 37.23 with all ray effects
off. Background compilation remained active, and later candidate runs overlapped
another game instance. These measurements identify substantial effects cost;
they do not establish a clean optimization gain or reproduce sustained 3 FPS.

Fresh sampling of the candidate without ray effects places 89.4% of the busiest
worker's executable samples in vector execution. Compiled vector blocks 212 and
209 lead that worker. Texture hashing, decoding, address conversion, uploads and
render-target synchronization lead the renderer samples. Both remaining CPU work
and ray effects need attention; removing the aspect timeout does not remove them.

The widescreen fix preserves established gameplay presentation during camera
silence. It does not change graphics settings, resolution or simulation timing.
Evidence and limits: `docs/journal/2026-10-19/data/aspect-stall-20261019.json`, with detailed
profiles and private save runs in `build/walking-fix-20261019`.

## Experimental live multicore geometry (2026-10-19)

Command-aligned VIF batching and ordered VU state/packet collection are available
behind `DRAKAN_VU_BATCH=1`; they remain disabled by default. The live verifier
checked 204,941 accepted groups with zero mismatches, but the clean raw comparison
measured 53.86 FPS with four workers against 99.99 FPS on the original serial VU
path. Saved clipping context and generated scratch-buffer dependencies cause
expensive speculative rejection and recomputation. This is not a 144+ FPS win and
should not be enabled in default launchers. See
[the measured investigation](../journal/2026-10-19/cpu-vector-live-batch.md) and
`docs/journal/2026-10-19/data/vector-live-batch-20261019.json` for validation, limits and
diagnostic controls.

The scheduling/observer follow-up avoids repeatedly rejected speculation, executes
batch heads on the owner, shares compact dependency plans, and retains fused UV
arithmetic on workers. Its live differential run verified 246,521 groups with
zero mismatches. Batching remains opt-in and the 144+ raw FPS goal remains unmet;
see [the follow-up measurements](../journal/2026-10-19/cpu-vector-batch-scheduling.md) and
`docs/journal/2026-10-19/data/vector-batch-scheduling-20261019.json`. Contended timings are
identified in the report and must not be treated as a clean speedup claim.

The 2026-10-20 pipeline follow-up reduces snapshots for known serial draws,
shares immutable pipeline seeds while preserving each interpreter's code-cache
state, and validates/copies memory by word masks. Experimental dead-load tracking
repairs architectural VF lanes instead of rejecting unused look-ahead values;
its bounded packet-exit variant avoids general per-instruction tracking.
Early dispatch, integer predicates and deferred loads remain opt-in experiments.
See [the pipeline investigation](../journal/2026-10-20/cpu-vector-batch-pipeline.md) for the
final source hashes, differential checks, clean/contended FPS and remaining limit.

The [2026-10-21 transform follow-up](../journal/2026-10-21/cpu-vector-transform.md) computes two
XY vertices per SIMD operation, including exact exceptional-value and flag
handling, and retains that kernel in live workers. Targeted tests now require
tracked microcode and assert compiled execution. Isolated dense/sparse transforms
improve, but contended gameplay has not demonstrated the 144+ FPS target;
the new kernel and live batching remain opt-in.

The [dependency-plan follow-up](../journal/2026-10-21/cpu-vector-dependency-plans.md) adds
block register summaries and sparse store-provenance masks. Full replay and
live verification pass, but the clean comparison remains around 96 raw FPS.
Worker-count and early-dispatch tests still trail the 99.41 FPS serial control.
An inherited-scratch predictor was tested and discarded after it failed to
improve throughput. The 144+ target remains open.


## GS worker and effects pipeline (2026-10-22)

The installed settings (DLSS + neural rendering + ray water/reflections, 4K) were limited by CPU/GPU serialization on the
GS worker, not by GPU power (RTX 5080 at ~33 % load). Fixed in 4de3972 and 1f43ac7; Surdana 23.7 -> ~67 world FPS
(still view), 28.1 -> 76.4 (slow turn). Details: [the review](../journal/2026-10-22/perf-review.md).

- Ray water (`ray_water.cpp`) keeps three frame slots for CPU-written memory (allocator, descriptors, upload buffers) and
  waits only before reusing a slot; GPU frames are serialized by the D3D11/D3D12 interop fences. Resident level geometry
  (`RayWater::setStaticWorld`, versions from `ResidentScene`) has persistent BLAS relative to a base point, rebased after
  16384 units; materials live in VRAM and are updated by range copies.
- The NR bridge (`gsr_nr.cpp`) uses an allocator ring; a shared fence orders D3D11 writes to the presentation texture
  after the D3D12 copy.
- The draw vertex buffer is a ring (`vbOffset`, NO_OVERWRITE, discard on wrap).
- Textures re-uploaded with identical bytes are revalidated by `LocalMemory::hashBlocks` (`TexEntry::rawHash`).
- CLUT loads flush only the CLUT's blocks. A 32-bit texture aliasing one dirty 32-bit target is assembled from the
  target's blocks on the GPU (`aliasedTarget`, `copyAliasedBlocks`); block-aligned uploads into a target drawn this or
  the previous frame are applied to the target texture (`beginTargetUpload`/`completeTargetUpload`).
- Exit line counters: `texRevalidated`, `readbacks`, `targetUploads`, `aliasedTextures`.

Remaining limit: the VU1 worker (~3.3 ns/pair, ~3.2 M pairs per frame in Surdana) caps the game near 75-85 FPS with or
without effects.

## Functional VU1 engine (2026-10-23)

`vu/aot/ps2_vu1_func.cpp` runs the fixed microcode image without the cycle-exact pipeline model. Blocks of pairs are
compiled from `kVu1Image` (decoded once into a compact table), chained to their compile-time successors, and execute the
same arithmetic as the exact engine (`calculateFmac` semantics, exact slow path for edge lanes). Timing is reduced to
what the image observes: one cycle per pair, DIV/WAITQ/XGKICK stalls, Q (7) and P (24) latency, MAC/CLIP visible 4 cycles
after issue. `analysis/scripts/vu_timing_reads.py <vu1_micromem.bin>` lists every timing-sensitive read with its producer
distances; all are decided by pair distance (several exactly at the latency edge). A forward dataflow over the pair graph
skips operand normalization for lanes known to be normal. Status flags and cycle-exact `cycles` are not maintained; the
replay check (`DRAKAN_VU_BENCH`, `DRAKAN_VUCHECK`) skips those two fields while the engine is on.

20,000 recorded Surdana runs: 0 mismatches against the reference (registers, Q/P/I, MAC, clip, VU memory, every packet);
3.3 -> 1.06 ns/pair. Surdana without effects: ~80 -> ~175-200 world FPS (VU worker and GS worker both near saturation).
With DLSS, neural rendering and ray water the GS worker's effects work (surface-triangle reconstruction ~2.4 ms/frame,
ray scene ~4 ms, model guides ~1.5 ms, per-draw state ~3.7 ms) caps the rate near 75 FPS. `DRAKAN_VU_FUNC=0` selects the
exact engine. Details: [journal](../journal/2026-10-23/vu1-functional-engine.md).

## VU worker and GS worker overheads (2026-10-23)

Effects off, Surdana still view: ~179 -> ~237 world FPS (interleaved A/B; at that rate the VU1 worker is saturated,
not the host present). Details: [journal](../journal/2026-10-23/cpu-ceiling.md).

- `resetScheduler` clears the exact-engine pipelines only when the exact loops ran since the last full reset
  (`Vu1Fast::Spare::schedClean`).
- GIF records produced inside a `gsr_batch::Scope` (the VU1 worker, one job) are staged per thread and appended to the
  bridge queue at once; all producer paths go through `GsrBridge::Impl::lockProducer`, which appends a thread's staged
  records before anything else it queues or waits for.
- Non-wrapping XGKICK packets are submitted from VU memory; PATH1/PATH2 packets skip the GIF arbiter copy when it is
  empty (`gs/gs_direct_path.h`); `GS::processGIFPacketPath` takes no frontend lock on the VU1 worker.
- Masked and STMOD 1/2 UNPACKs without fill cycles use `vif_unpack::maskedRuns` (SSE2).
- GS: PSMT8 uploads swizzle four rows per block column (`image_upload::writeT8Rows`), the draw state is cached
  between primitives, CSM1 CLUT reads use the page offset tables, the draw batch has a fixed capacity.

Remaining: microcode blocks are ~68 % of the VU worker (~92 % busy at 238 FPS); the GS worker's vertex path is ~25 %
of its time (~3.2 ms busy per frame).

## Effects path off the GS worker (2026-10-23)

- The DXR pass runs on its own thread (`RayWater::submit`, `Impl::loop`/`execute`). The GS worker only validates, detects
  changed material textures and issues the D3D11 side in frame order (`NrInterop::produce11`, `reserve11`, queue the
  job, then `consume11`: a GPU wait on the consumer fence plus the output copy). The job is queued before the D3D11 wait
  because a driver may wait for a shared fence on the CPU. The pass thread packs, uploads, records, submits and
  `signal12`s the consumer fence, also after a failure. Jobs own their inputs (moved frame triangles,
  `ResidentScene::projectedShared`, `shared_ptr` models, copied changed texels); at most two are queued. `render()` is
  the synchronous form for tests.
- A watchdog thread checks the consumer fence every 100 ms. A pass incomplete for over 1 s is logged (`[ray-water]
  watchdog:` with the pass-thread stage and both fence values). Once the pass has been stuck past the limit
  (`GSR_RAY_WATCHDOG_MS`, default 3 s after its signal was queued on D3D12, 7x that before), the watchdog raises both
  shared fences from the CPU (`NrInterop::release12`), which releases every D3D11/D3D12 wait, and turns the pass off for
  the session. `gsr_test_ray_water --lost-signal` (ctest `ray_water_lost_signal`) drops a signal to exercise this.
- Every per-frame dynamic vertex buffer is a NO_OVERWRITE ring (draw VB, object-motion VB `motionVbOffset`, model-motion
  VB `modelOffset`). A WRITE_DISCARD map of a buffer the GPU still uses blocks the worker for milliseconds once the GPU
  runs behind; when a CPU stage suddenly grows, look for such a map first (host sampler: ntdll samples with that caller).
- `ResidentScene::match` reads per-cell, per-water-class candidate arrays.

With neural rendering on, the frame is GPU bound on an RTX 5080: GPU timestamps (a probe, not committed) put DLSS NR at
~11 ms and the D3D11-side ray segment at ~5.6 ms per frame (D3D12 ray work itself ~1.35 ms: dynamic BLAS 0.47, three
animated model BLAS + TLAS 0.73, trace 0.08; the rest is waiting behind the previous frame's NR on the GPU). The worker
then waits in `neuralEvaluation`/`presentCall`. Details: [journal](../journal/2026-10-23/effects-gs-worker.md).

## Combined result on main (2026-10-23, after 9cc92fd)

Surdana still view, 4K, `GSR_TEXREPLACE=0`, interleaved against the 64bb666 build, only while no other GPU work (HD
movie upscaling, other game instances) was running:

| Configuration | 64bb666 | main |
| --- | --- | --- |
| Installed settings (DLSS SR + NR, ray water/reflections) | 64.7, 75.4, 66.5, 75.2 | 79.5, 79.4, 79.3, 79.1 |
| Effects off | 173.4, 173.9, 177.3 | 204.7, 237.2 (one contended run: 103) |

VU replay on the main build: 0 mismatches in 20,000 runs, 1.0 ns/pair. Two ceilings remain: with neural rendering on,
the GPU (DLSS NR ~11 ms per frame); with effects off, the VU1 worker (96-100 % busy at ~238 FPS; the present handoff
costs the GS worker only ~4 % and is not the limit, see
[blas-refit-and-vu-ceiling](../journal/2026-10-23/blas-refit-and-vu-ceiling.md)). Other GPU work on the machine (e.g. `realesrgan-ncnn-vulkan`) halves the
installed-settings rate, so check for it before measuring.

Animated native model BLAS are refitted in place per topology instead of rebuilt in new allocations every frame
(`RayWater::Impl::Deformable`): installed settings 79.1-79.5 -> 84.0-84.1 FPS.

## GS worker CPU with effects on (2026-10-23)

With render-resolution NR the GS worker was the limit (99 % busy, ~8.8 ms CPU per frame excluding NR waits). Surface
triangle reconstruction for the ray scene (~3.5-4 ms per frame) and model-motion preparation now run on a
`SurfaceBuilder` thread (`runtime/gs/src/surface_builder.h`, `GSR_SURFACE_ASYNC=0` keeps them on the worker) whose
results are read only after `drain()`; the model-motion projection uses SSE2 straight into its vertex ring; identical
draw constant uploads and the redundant pipeline restore after the last velocity pass are skipped. GS worker per frame
(render-mode NR, same session): profiled `draw` exclusive 3.8 -> 1.2 ms, `modelGuides` 0.95 -> 0.17 ms, busy time
excluding `neuralEvaluation` 8.8 -> 5.9 ms; the worker now waits on the GPU (NR completion) and the frame rate went
104-105 -> 105-112 FPS (still view, GPU-bound). New `GSR_PROFILE` buckets: `drawSurface`, `drawObjectMotion`,
`drawVertexUpload`, `drawSubmit`, `modelPrepare`, `surfaceDrain`. Any new reader of `RT::surface` or the resident
scene must call `surfaceBuilder->drain()` first. Details: [journal](../journal/2026-10-23/gs-worker-effects-cpu.md).

## VU1 on several cores (2026-10-23, not merged)

An exact parallel VU1 mode (units of 8 MSCAL groups on helper threads from snapshots of VU memory and registers,
inherited reads tracked by a second functional-engine instance, in-order commit or serial re-run) verified with 0
mismatches in replay and over ~3.7M live units, but ran at 88-125 FPS against 148-167 serial on the shared test
machine: helpers run at ~3.4 ns/pair (tracking alone is 1.5x), and the owner's duplicated VIF scan and in-order commit
stall the EE. The old `DRAKAN_VU_BATCH` path still uses the exact engine on its workers (125 against 148 FPS). The code
was kept out of main; see [the journal](../journal/2026-10-23/vu1-multicore.md) for the design, rejection causes and
what would be needed for it to pay off.

## VU1 loops in SIMD lanes (2026-10-23)

The functional engine runs every one-block loop of the image (`vu/aot/ps2_vu1_func.cpp`, namespace `lanes`) several
iterations per step: AVX-512 4, AVX2 2 (`vu/aot/ps2_vu1_lanes.h`, chosen by CPUID; `DRAKAN_VU_LANES=0` off, `=2` AVX2,
`DRAKAN_VU_LANES_STATS=1` per-loop counters). Each 128-bit lane is one iteration. A compile-time plan from the decoded
pair table gives every operand component its reaching definition (same iteration, previous iteration = lane shift, or
invariant), which makes the software-pipelined vertex loops lane-parallel; VI registers must be constant inductions,
Q comes from at most one DIV, and loops with observed MAC flags, CLIP or flag reads are not taken. FMAC lanes the
scalar engine would compute on its exact slow path are recomputed with the scalar `fmac`; the exit iteration's MAC
likewise. Load/store addresses are checked per run (in VU memory, loads before same-address stores, no double stores).

Eligible: 0x2B80/0x2ED0 (vertex transform), 0x2DA0 (lighting), 0x0D10, 0x04D8, 0x0950, 0x0A70, 0x0BD0, 0x01B8.
Replay: 0 mismatches (both widths); 1.0 -> 0.78-0.82 ns/pair (AVX2 0.84); the loops themselves ~2x faster.
Surdana effects off: +10-15 % world FPS against lanes off / main in interleaved runs (quiet round 238 -> 263,
755 -> 835 M VU1 pairs/s). The VU worker stays saturated; the loops are now ~7 % of it and the rest is spread over
non-loop blocks (setup, clip-flag loop, packet loops, VIF unpack). Details:
[journal](../journal/2026-10-23/vu1-simd-vertices.md).

## Combined result after the lane executor and the GS builder thread (2026-10-23, main bab9100)

Surdana still view, 4K, `GSR_TEXREPLACE=0`, both builds on the same settings file (`DRAKAN_INI`; installed
settings: SR Quality + NR, ray water/reflections, ray shadows off), interleaved against the 64bb666 build. Other
sessions loaded the machine, so pairs vary; every pair favours main:

| Configuration | 64bb666 | main |
| --- | --- | --- |
| Installed settings, NR at output resolution (GPU bound) | 72.9, 48.1 | 81.7, 66.2 |
| Installed settings, NR at render resolution | - | 92.9, 111.1 |
| Effects off | 115.5, 174.6 | 158.2, 216.8 (peak 2 s window 226) |

VU replay on this build: 0 mismatches in 20,000 runs. `[video] texture_dump = 1` writes every newly seen texture and
roughly halves the effects-off rate (84 FPS); compare builds with one settings file (`DRAKAN_INI=<copy>`), never with
whatever the play folder currently holds.

## Vsync hitches from the unthrottled swapchain (2026-10-24)

With vsync on, gameplay at a steady 60 FPS still froze for ~48 ms about once a second (more in Ravenshold). The
swapchain is created with a frame-latency waitable object and `SetMaximumFrameLatency(1)`, but nothing waited on it
(`Renderer::waitForPresent` held the renderer lock, so the bridge had stopped calling it). DXGI then queues flips up to
the 3 buffers; a GPU readback of a dirty render target (`readbackRT`: CLUT loads and uploads over the small 128-wide
scratch/shadow targets) waited in `Map` until those flips had scanned out, ~3 refreshes. The GS worker held the
renderer lock for that time, the host could not present, the EE waited at the handoff, and the guest vblanks were
delivered late and then in a burst: one 50-55 ms `dt` and three updates ~4 ms apart, i.e. a freeze and a jump.

Fix: `Renderer::waitForPresent` waits on a duplicate of the waitable outside the renderer lock (the slot is consumed
until the next `Present`), and `GsrBridge::hostTick` calls it before each present when vsync is on and NR is not
presenting (since the follow-up below, NR's D3D12 swapchain too). Surdana walk/turn script, 12 s, vsync on, 59.996 Hz display: present gaps over 30 ms 11 -> 0, slow GS
records (all `readbackRT`) 13 -> 0, engine `dt` SD 5.05 -> 0.7-0.9 ms (the vsync-off control: 0.90); Ravenshold has ~38
readbacks/s and none is slow. It also removes ~2 refreshes of queued input latency. Details: [journal](../journal/2026-10-24/vsync-readback-hitches.md).

### Engine tick time in whole refreshes (2026-10-24)

The engine tick does not follow the guest vblank: it runs ~2-3 ms after each present, and `1c3028` measures it with
the 1 ms game clock. Shown frames last whole refreshes, but the measured tick was 16/17 ms (clock resolution) and, at
some phases of the host release, alternated 14/19 ms: world motion judders under a perfectly even present cadence.
`tickTimeHook` (`drakan_frame_rate.h`, policy `snapTickToRefresh` in `drakan_frame_policy.h`) gives the tick whole
refreshes while the bridge reports vsync-locked presentation (`drakan_present_clock.h`: vsync on, NR not presenting,
not minimised; refresh from `QueryDisplayConfig` for the window's monitor), carrying the remainder so game time stays
the clock's within half a refresh; ticks under half a refresh pass through. `[video] tick_snap = 0` turns it off.
40 s walk/turn A/B: ticks off the refresh grid 39-40 % -> 0 %, tick SD 0.57-2.68 -> 0.03-0.33 ms, game-wall drift
-1.9 ms. The guest vblank (60.000 Hz timer) and this display (59.996 Hz) beat about every 5 minutes; that only
gates EE frame starts, which follow the present.

NR presentation had the same unthrottled queue (its D3D12 swapchain: waitable, latency 1, 2 buffers, never waited
on; readbacks waited 30-33 ms). `gsr::nr::Bridge::presentWaitHandle` feeds the same unlocked wait, and both the wait
and tick snapping now apply whenever vsync is on. NR on, 40 s walk: present gaps over 25 ms 31 -> 1, tick SD 3.54 ->
0.78 ms, ticks off the refresh grid 70.8 % -> 0.2 %, GS records over 20 ms 30 -> 0.

The readbacks themselves remain (~1 per frame while moving in Ravenshold, ~0.5-0.8 ms each in `Map`, ~5 % of the GS
worker). Uncapped with vsync off (246-361 FPS, VU1-bound) windows with 552 readbacks per 2 s ran no slower than quiet
ones, so removing them (the ownership work in `../journal/2026-10-21/cpu-gpu-readback.md`) is not worth its risk now.

## Periodic 17 ms hitch from the scene-model texture refresh (2026-11-01)

Standing still (uncapped, 4K scale 4, vsync off) the frame time showed a 16-19 ms gap every 32nd frame (~4 per second, 78-86
per 20 s), p99 17.6 ms, world fps swinging 123-150. `DRAKAN_FRAME_AUDIT=3` + `scripts/perf/ee_samples.py` showed the EE thread
inside `drakan_scene_models::hashBytes` / `Capture::texture` for the whole gap: with scene-model capture on (ray effects / NR),
every 32nd capture re-sampled every resident texture to 128x128 and hashed its 64 KB with the byte-serial FNV in `captureHash`
(151 textures = 11 ms on the EE thread). The GS worker sat idle meanwhile and the flip waited a vblank.

Fix: texture identity uses `captureHashWide` (4 lanes, 32-byte blocks; equality-only, not persisted), and the refresh runs in
~250 us slices over the following captures (`Capture::refreshTextures`) and replaces the texture set when complete. After:
gaps over 14 ms 78-86 -> 1-5 per 20 s, p99 17.6 -> 10-13 ms, gap SD 2.4 -> 1.2-1.6 ms, world fps 123 -> 125-157 (run to run
differences are load from other processes). Details: [journal](../journal/2026-11-01/still-view-fps-jitter.md).

Round 2 (same day): posed scene-model instances were rebuilt from scratch every capture (~84 `mesh()` calls, 2.6 of ~3 ms).
They now reuse a per-resource topology template and only re-read positions (`Capture::posedMesh`), output verified
byte-identical to the previous implementation over 420 captures. Capture cost 2.7-3.5 -> 0.6-0.7 ms per frame; Surdana still
view 128-157 -> 192-215 fps, gap SD 0.7-0.9 ms, p99 under 8 ms (the EE thread is now waiting on the VU1 worker).

Round 3 (same day): the frame limiter now sleeps precisely (`gs_precise_sleep.h`): present spread 0.74-0.78 -> 0.15-0.18 ms at
144/120/90 fps. GPU texture creation for finished upscale jobs is budgeted to 2 ms per present. The per-frame GPU readbacks
(15-20 % of the GS worker) were mostly redundant: the game uploads a texture's CLUT / image over a scratch render target and the
following read of exactly those blocks pulled the whole target back although the upload had already been applied to it on the
GPU. Reads of blocks an applied upload made identical now skip the readback, dirty rows an upload fully rewrote are trimmed,
and the game's frame-buffer-onto-itself copy is a no-op (`GSR_RB_SKIP=0` = old behaviour, `GSR_RB_VERIFY=1` cross-checks every
skip): readbacks per walk 2800 -> ~1200, +12..23 % fps in interleaved A/B pairs, 0 mismatches over 93 M verified pixels. See the
[journal](../journal/2026-11-01/still-view-fps-jitter.md).
`syncRT` (memory -> GPU row uploads for targets the game streams textures over, ~13 % of the GS worker while walking) is 46 % cheaper
after a per-page stamp table for `maxStamp` and a block-wise `readRectCT32`: 2.8 s -> 1.5 s per walk.
The game's frame-buffer-to-frame-buffer copy (about six per second) is now one GPU copy when both ends are whole targets instead of two
full readbacks, a per-pixel CPU copy and a re-upload (`GSR_GPU_COPY=0` = old path): +6..12 % fps, p99 down, 0 mismatches over 369
verified copies. Walking Surdana now measures ~195 fps (p99 8.4 ms) with the DLSS effects on.

## Ray-model snapshot and normal reuse (2026-11-04)

The ray backends now recognize a new immutable model snapshot containing exactly the same geometry by metadata and full
triangle-byte comparison. Unchanged poses retain their material/normal cache and WebGPU BVH. Material flags and texture-base
changes still invalidate/rebase attributes; WebGPU also handles alpha-flag changes without rebuilding unchanged geometry.
Welded corners with identical normals share their packed value, and bounded normal quantization avoids general `lround` calls.
`GSR_RAY_MODEL_REUSE=0` disables snapshot and welded-normal reuse for comparisons. Rendering tests and 1.8 million normal
comparisons passed. The isolated quantizer is approximately 8% faster; this is not a game-FPS percentage. Stationary-view
measurements and the shared-machine limitations are recorded in the [journal](../journal/2026-11-04/stationary-performance-review.md).

## Campaign map sweep (2026-11-04)

All 13 playable PAL campaign maps loaded in the [all-map sweep](../journal/2026-11-04/all-map-performance-sweep.md).
Twelve completed two fixed-view samples and a movement check; the redirected Isles start returned to the title during
its second sample and remains open. Transition to Desert is absent from the disc. Several stationary views remain
readback-bound, particularly Catacombs, Northern Tundra and Shiverbane; a good Surdana result does not establish global
frame-time consistency. The journal records per-map FPS/p99, cinematic and survival fixtures, executable identity,
screenshots/log locations and the user's retained concurrent-game session. These are diagnostic samples, not isolated
cross-map benchmarks or full campaign playthroughs.

## Small uploads, exact readers and constant alpha tests (2026-11-04)

D3D11 can patch the exact written bits of small unaligned image uploads on the GPU. Exact mip/local-to-host reader
rectangles and retained knowledge across CT32 CPU-row refreshes avoid reading back data the CPU already owns. Unknown
bits and unsupported cases retain the readback fallback. Small genuine scratch reads can stage after their producing
draw, with strict version/row/scale validation. Both ray backends skip interpolation and texture-alpha filtering for
constant alpha-test results; cutouts and translucent alpha retain full evaluation.

The [investigation and measurements](../journal/2026-11-04/generic-map-performance-fixes.md) record the toggles, pixel
regressions, executable identities and shared-machine limits. Catacombs readbacks fell from about two to one per present,
but its remaining GPU wait still limits FPS. Separate alpha-shortcut comparisons measured approximately 5% / 7% lower
ray-pass GPU time in Catacombs / Tundra, with modest game-FPS gains. These mechanisms apply across maps; they do not
guarantee the same improvement in every scene.

## Native coverage and upload expansion (2026-11-05)

Both ray backends cache exact native-texture alpha coverage. Nonblended GREATER-0 cutouts whose every texel has positive
alpha can accept a ray hit without bilinear alpha sampling. Original material classification remains unchanged, and a
single zero-alpha texel prevents the shortcut. Texture replacement and coverage changes invalidate cached attributes.
D3D11 CPU uploads also specialize exact pixel replication at common render scales and copy repeated rows in doubling
runs. The implementation remains portable C++.

Corrected same-executable coverage pairs measured 2–17% lower ray GPU time in Isles, Catacombs, Tundra and Shadowmire;
observed FPS gains were smaller. Common scale-four expansion cases measured about 25–30% less CPU time in isolation,
with no meaningful Catacombs FPS gain in the separate upload-only pair. These percentages must not be added together.
See the [continued investigation](../journal/2026-11-05/native-alpha-coverage-performance.md) for comparisons, the earlier
native-settings path correction, exact image/pixel regressions and the latest per-map sweep. Debug toggles are
`GSR_RAY_ALPHA_COVERAGE=0` and `GSR_RT_UPLOAD_SPECIALIZE=0`.

## Accepted-hit and rejected-batch query work (2026-11-05)

Both ray backends avoid reevaluating nonblended opacity after a successful alpha test and skip AO whose sanitized
ambient term is exactly zero. Entirely reconstructed opaque frame batches are excluded from receiver, AO and shadow
queries whose existing per-triangle tests reject them universally. Water reflections retain those batches, and mixed
batches retain per-hit filtering. Geometry capture, visual quality and contributing ray sample counts are unchanged.

Same-executable batch-filtering comparisons measured Isles at about 52 → 58 FPS in both orders, Catacombs 63.1 → 65.1
and Shadowmire 93.9 → 99.5. Ray GPU time fell about 12–30% in those pairs; this is a GPU-time reduction, not an FPS
percentage. Readback waits still limit the game. A Tundra redirected-start visual anomaly reproduced with filtering
disabled, invalidating its FPS pairs; that issue remains open. See the
[measurements and qualifications](../journal/2026-11-05/ray-query-work-pruning.md), including exact image regressions
and the all-map checks. Diagnostic switches are `GSR_RAY_ACCEPTED_ALPHA`, `GSR_RAY_ZERO_AMBIENT` and
`GSR_RAY_PRUNE_REJECTED` (`0` restores the corresponding old work).


## Combat frame hitches (2026-11-05)

The D3D11 model-motion pass projects moving poses into reusable CPU storage and queues a byte-range update
to its vertex buffer. This removes a recurring CPU wait when discarding an in-flight buffer. Render-target
uploads also reuse their native and scaled scratch storage. When measuring long frame intervals, separate
scripted screenshot and memory-dump frames from ordinary gameplay. The detailed traces and A/B results are in
[the hitch investigation](../journal/2026-11-05/occasional-frame-hitches.md).


## Scene geometry under CPU contention (2026-11-05)

After observing a draw backlog, the renderer prepares the next model pose while the surface worker finishes its queue. At a join it can take
ownership of the remaining queued draws after the active batch completes. All geometry and material updates
stay in submission order. With `GSR_PROFILE=2`, inspect `surfaceWait` for blocked time, `surfaceAssist` for
useful work on the joining thread and `surfaceDrain` for the total cost of the join. A separate `rayEnqueue`
measurement covers submitting to the bounded DXR queue, including waiting for room. Compare the two schedules
with `GSR_SURFACE_OVERLAP`. See [the combat measurements](../journal/2026-11-05/scene-geometry-join.md).

## Northern Tundra weak spots (2026-11-07)

On a quiet machine (0 other `drakan.exe`, 3 repeats) the 4B spots run 175 (A), 139 (B), 98 (C), 149 (D), 168 (E) world fps with
display presents at 60 Hz. The earlier 45 fps reading was contention from other games. C, the worst, is limited by the **EE thread**
(ee_ms/flip 8.1 against 4.3 at A, thread 96% busy), not the GPU: `presentCall` is lowest there (0.3 to 0.8 ms), and the toggles
`GSR_RAY_SHADOWS=0` (+12%), water/reflections off (+7%), `DRAKAN_POLY_NEAR_CLIP=0` (0%) do not move ee_ms/flip; DLSS off is slower (-22%).
The EE profile is diffuse guest code (more objects in view), our capture hooks are flat at about 1 ms. Numbers and method:
[journal](../journal/2026-11-07/fps-4b.md). Use the `.map` that belongs to the exe under test with `hostprof.py`
(`hostprof.csv` is written to the exe's folder and overwritten by the next run, copy it before starting another).


## Shader prewarm and the Northern Tundra spot C profile (2026-11-08)

* D3D11 shader work never runs on the GS worker while the guest waits: the uber shader variants compile side by side
  before the guest starts, every start-up pipeline is drawn once (drivers translate at the first draw), and a
  background thread compiles the lazy shaders and builds the DXR device. Compiled blobs are cached in
  `<exe>/shadercache`. Anything still compiled late prints `[gsr-shader] late ...` (switches in `env-flags.md`).
* `PS2X_HOSTPROF_STACKS=1` with `PS2X_HOSTPROF` records EE call chains; `scripts/perf/hostprof_stacks.py` gives
  inclusive cost. At spot C about 23 percent of the EE thread is the guest polling GS FINISH (`f_0x1dbfa8`, loop at
  0x1dc688) before a per-frame readback, i.e. waiting for the VU1 and GS workers; JALR dispatch is negligible; scene
  model capture is about 9.5 percent. Details: `docs/journal/2026-11-08/perf-leftovers.md`.

## Multiplayer versus single player (2026-11-08)

Measured, no MP-specific overhead: the depths arena (solo host) and single player at the same spot of Level 1E run at the same world rate (about 60 fps at 720p and at 4K, EE and GS threads near 100 %). The MP inventory (`World 0.0 fps`, Present about 63 fps) behaves like the SP inventory: the guest still draws the paused 3D world every frame. Details and candidates: `docs/journal/2026-11-08/mp-performance.md`.

## Shipped-settings review (2026-11-08)

With the shipped `drakan.ini` (4K, `scale = auto`, DLSS SR quality, ray effects off) the **GS worker thread** is the
limit in the 1E arena spot and the Catacombs start (never idle, 11.5 to 15.8 ms per world frame); the GPU and VU1
worker are not. Auto scale picks 8 at 3840x2160 (16:9, `ceil(3840 / 512)`), and target page-row refreshes
(`syncRT` -> `uploadTargetRect`) replicate pixels scale x scale on the CPU: 1.8 to 2.6 ms per frame and 5 to 6 GB/s of
`UpdateSubresource`. Scale 5 measured 94 to 97 against 62 to 85 world fps (1E) and 94 against 71 (Catacombs). DLSS SR
object motion costs 1.2 to 2.3 ms per frame; a per-frame 32x32 GS-to-EE readback (guest `f_0x1dda88`, FINISH wait
in `f_0x1dbfa8`) serialises EE and renderer (36 % of the EE thread at 1E with DLSS off). HD textures cost nothing
measurable. Ranked fixes and numbers: [journal](../journal/2026-11-08/perf-review.md).

## Start-up, load times and hitches (2026-11-12)

Measured and changed outside the frame loop; numbers, conditions and what is not established are in the
[journal](../journal/2026-11-12/perf-loading-hitches.md).

* Mod packs at start-up: `ModManager::installFiles` reads all pack members in one parallel batch (indexed zip
  directory, the file held open for the batch, table-driven inflate), then rebuilds the containers. The log line
  `mods: pack files installed in N ms: ...` gives the cost at every start (two packs with 17126 members: about 1 s,
  before 6 s and more).
* A level load is about 500 resource batches and 1100 requests but only a few ms of disc reads; its time was the thread
  hand-overs between the resource worker and the EE thread. A batch's EE calls now run on the EE thread as one
  `hle::IEeCallSequence` (`DRAKAN_RES_CHAIN=0` = one call at a time) and the 50 us resource latency counts from the
  submission (`DRAKAN_RES_LATENCY_ELAPSED=0` = after the batch is taken). New code that needs several dependent guest
  calls should build a sequence rather than post call by call.
* Measure: `play_run.py --timestamps --env DRAKAN_IO_STATS=1 --env DRAKAN_LIFECYCLE_LOG=1` and
  `scripts/perf/load_phases.py`; hitches with `DRAKAN_FRAME_AUDIT=2` and `scripts/perf/hitch_survey.py` (frames over 2x
  and 4x the median with GS record, present, resource batch and log lines inside them).
* The long frames of a walk are GPU readbacks on the GS worker (`readbackMap`), not file or resource work.

## CPU side of a gameplay frame (2026-11-12)

Measured with the owner's settings in a 1280x960 window on the loaded shared machine (3 to 10 other games), so the
solid figures are CPU time per world frame and the single-threaded VU1 replay; world fps gains were not established.
Details, tables and the open items: [journal](../journal/2026-11-12/perf-cpu.md).

* The frame is serialised by the game's per-frame 32x32 GS read-back: the EE polls CSR.FINISH (`f_0x1dbfa8`, loop at
  0x1dc688) until the VU1 and GS workers have finished the frame. 40 to 70 % of the EE thread's wall time is that wait
  (poll, read-back drain, VU1 sync), which is why no thread is saturated while the CPU side limits the frame rate.
  Not solved (needs to know what the image is for).
* VU1 loop path (`vu/aot/ps2_vu1_lanes.h`, `fmacBad`): finite FMAC results of 2^120 and above stay in SIMD when their
  value is provably the plain one. Converted Order of the Flame terrain carries +-FLT_MAX texture coordinates and sent
  every step of the texture-matrix loop 0x04D8 to the scalar exact path: VU1 replay of the Ruined Village 3.4 -> 0.9
  ns per pair, VU1 worker CPU per world frame 18.7 -> 8.7 ms (Volcanic 22.6 -> 10.9 ms). Values are identical
  (`vu_lanes_tests`, replay against the reference). `DRAKAN_VU_LANES_STRICT=1` = the old rule.
  The +-FLT_MAX values are stale entries of the third vertex set (VU 516 to 740) that stage 0x0468 transforms in place
  over all 225 entries for every converted terrain pass, while the following batches upload only their own 6 or 16
  vertices; a non-identity pass matrix multiplies the leftovers again every frame. Drawn coordinates are right; the
  converter's UVs are not involved (`docs/journal/2026-11-14/ootf-performance.md`).
* Order of the Flame levels: 1 to 2.4 terrain passes per drawn cell against 0.03 on the disc (chunks of 1 to 7 cells
  under the 8-texture limit), so 5,000 to 6,000 draws and 16,700 VU1 runs per frame at the Wartok Canyons start
  (Surdana 232 draws). The cost is per pass on the EE, the VU1 worker and the GS worker (`drawSubmit` about 10 us a
  draw); no converter re-cut that keeps the picture removes more than 4 % (same journal).
* Scene model capture (`drakan_scene_models.h`, runs on the EE thread whenever DLSS SR, neural rendering or a ray effect
  is on): flat copy of the live set, cached untextured counts, sampled source signatures with a full check every 16th
  capture. Surdana: 16 to 19 % of the EE thread -> 8.6 to 9.8 %. `DRAKAN_SCENE_CAPTURE_FAST=0` = per-capture work.
* DMA chain copies (`runtime/ps2_chain_buffers.h`): gathered in a scratch vector, one exact allocation for the block
  that travels, blocks of 32 KB and more reused. About 470 chains of 9.4 KB per world frame; allocation and copy below
  the kick 5.4 -> 1.6 % of the EE thread in the Ruined Village. `PS2X_DMA_CHAIN_POOL=0` = growing vector per chain.
* Tried and left off: letting the FINISH poll give up its time slice (`PS2X_FINISH_YIELD=1`). In the two quietest runs
  it collapsed the frame rate (93 -> 28 and 52 -> 8 world fps): the slice goes to any ready thread, background workers
  of the game included.
* Tools: `PS2X_HOSTPROF_BUSY=1` (sample only threads that use CPU; check the sample count, the sampler starves on a
  loaded machine), `dma_chains/s`, `dma_chainMB/s`, `dma_pooled/s`, `ee_ms/flip2` and thread names in the `[perf]`
  line, `scripts/perf/cpu_scene_bench.py` (interleaved scene benchmark: world fps, frame interval percentiles, CPU per
  world frame for the EE thread, the VU1 worker and the rest), `DRAKAN_DEBUG_SKIP_FN` (diagnostic).

## Render target refreshes (2026-11-12)

Render target objects are at least 512 rows tall and the game uploads textures into the memory under its 128 pixel
scratch targets every frame, and into the frame buffers on the title, loading screens and in movies. On Direct3D 11:

* a scratch target (under 480 pixels wide, never displayed, no world state) is refreshed from GS memory only in the
  page rows a draw or a texture sample needs (`rt_sync_rows.h`, `GSR_RT_LAZY_SYNC`); other targets are refreshed whole;
* above internal scale 1 the refreshed pixels are sent at native size and replicated by a compute pass
  (`rt_upload_d3d11.h`, `GSR_RT_UPLOAD_GPU`); adjacent block runs of an upload are joined first;
* a scratch target the game stopped drawing into is staged for readback at the end of that frame, so the readback an
  upload into its memory forces later maps a finished copy (`GSR_RB_PREFETCH`);
* HD and upscaled textures are created on the texture worker, the render thread only adopts them.
* the game's 4x4 depth probe on the last memory page, which the EE reads back every frame in some views, is served
  from the CPU mirror of constant sprites (`GSR_RB_SOLID`); the probe is drawn untextured because the depth buffer
  cannot be sampled as a texture, so its value is a constant today.

At scale 8 the bytes sent to refresh targets fell from 8.6 to 22.6 MiB per frame to under 1 MiB per frame over five
whole captures, with identical pictures. `GSR_RT_REFRESH_STATS=1` prints the refreshes per target and the memory the
targets hold. `scripts/perf/gs_replay_ab.py` re-measures on a recorded stream (`gsr_replay --bench / --hashes`).
Numbers, conditions, the WebGPU work left and what is not fixed (the blocking readback):
[journal](../journal/2026-11-12/perf-gpu.md).

## Which of the 12 November changes are proven (round 2, 2026-11-12)

State after the second round of the day ([journal](../journal/2026-11-12/perf-round2.md)). The machine was never
quiet (2 to 5 other games at every check), so no frame-rate or load-time verdict could be taken; the gated
benchmarks are written and queued (`scripts/perf/scene_ab_bench.py`).

| Change | Proven | Not proven |
| --- | --- | --- |
| VU1 loop path for huge finite values (`DRAKAN_VU_LANES_STRICT`) | identical values (tests, replay); VU1 replay 3.4 to 0.9 ns per pair and VU1 worker CPU per frame halved in the Order of the Flame levels | world fps in any scene |
| Scene model capture (`DRAKAN_SCENE_CAPTURE_FAST`, `DRAKAN_SCENE_CAPTURE_MOTION_ONLY`) | equal snapshots in the tests; capture share of the EE thread 16 to 19 % to 8.6 to 9.8 % in Surdana | world fps; EE time of the motion-only snapshot |
| DMA chain copies (`PS2X_DMA_CHAIN_POOL`) | same bytes; allocation and copy share of the EE thread | world fps |
| Target refreshes and readbacks (`GSR_RT_LAZY_SYNC`, `GSR_RT_UPLOAD_GPU`, `GSR_RB_PREFETCH`, `GSR_RB_SOLID`), motion batching (`GSR_MOTION_BATCH`) | identical pictures in replay; refresh bytes and waiting readbacks per capture | live frame rate and frame time |
| Resource batches on the EE thread (`DRAKAN_RES_CHAIN`, `DRAKAN_RES_LATENCY_ELAPSED`) | same calls and order; hand-overs per level | a shorter load on a quiet machine |
| DLSS writes the window's pixels (`GSR_UPSCALE_WINDOW_OUTPUT`) | measured quiet by its own session: Surdana 101.8 to 155.0 world fps | the cross-check of round 2; DLSS with ray effects |
| Mod packs read beside the renderer start-up (`DRAKAN_MODS_EARLY`, round 2) | the pack phase of the start-up 0.615 to 0.242 s, four interleaved pairs with disjoint ranges under load | a quiet-machine start-up time |
| Read-back modes and the frame queue (`PS2X_READBACK`, `PS2X_FRAME_QUEUE`, round 2, **default off**) | FINISH polls per frame from about a million to 1, the EE's waits for the VU1 worker to 0, EE CPU per frame 19 to 22 ms to 7.2 to 7.4 ms in two scenes; late answers differ in about one read in a thousand. Quiet machine, 4K, 14 Nov: frame time unchanged in five scenes except Surdana without DLSS (6.00 to 5.47 ms), 1 % and 0.1 % lows in the Ruined Village 27 to 19 and 28 to 20 ms (DLSS quality); no effect where the pose does not read back | input latency; the case with another GPU load (two busy-machine runs were much slower); a played session. Recommendation: on after those two checks |

Round 2 also found that the end-of-frame wait is three waits (the FINISH poll before the probe read in the views
that read back, the flip routine's status reads and the display commit in every view), that every world frame waits
for its own `Present` (so a paced present caps the world rate), and that the process teardown after the window
closes is not DLL detach work (ending the process without it was not faster). All absolute frame rates of the
12 November notes come from windows that were never activated; the foreground comparison is still open.
