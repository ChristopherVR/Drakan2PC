# Explicit vector write-back choice â€” 2026-10-19

The CPU profile of installed build `cd25328dbe3e3ba8d962636216667d5990636731f7ffc9c76404aa1efca3e1cb` identifies the VU worker as the leading CPU stage. This follow-up targets redundant work in the compiled vector program. Evidence: `build/vu-shadow-20261019`.

## Change

`Vu1Step::execPair` already chooses between writing an upper result directly to its vector register and writing it into a scratch buffer when the lower unit needs the old value. It previously rediscovered that choice by comparing the chosen pointer with the scratch-buffer address after both units ran.

MSVC retained that pointer comparison and generic masked copy-back paths even in many statically decoded pairs. The candidate records the original choice in `upperDirect`, then uses that boolean for the final copy-back condition. The compiler can fold the condition when compiling a known pair.

The buffer selection, lower-unit observation order, suppressed lower writes, arithmetic, lane masks, flags, timing and native kernels retain their previous behavior. The public interpreter layout and header are unchanged. No FMAC noalias annotation or experimental sign-batch change is included; both experiments were excluded as described in `docs/journal/2026-10-19/cpu-vector-fmac.md`.

## Code generation

An isolated MSVC Release probe of the hottest generated block (block 47, pair 419) changes from **3,839 to 3,240 instructions (-15.6%)**, and from **17,336 to 14,919 bytes (-13.9%)**. Probe byte counts exclude link-time padding. This establishes removal of generated work; controlled FPS runs determine the actual throughput gain.

## Validation and disposition

The full vector image was rebuilt, including all four generated slices. A new frozen-reference test passed 131,072 cases and exercises upper arithmetic/conversion forms, no-write cases, partial masks, lower loads/stores/moves/rotates that read or write the same register, independent lower operations, I-bit immediates, interrupted/resumed execution and all four caller rounding modes. The existing FMAC (131,072), native packet (114,688), native UV (114,688) and timed-scalar (8,192) checks also passed. The new test also passes against the preceding implementation. Live differential and visual deployment checks were not run after the FPS gate failed.

**Not deployed.** Four clean ABBA runs measured **100.451 FPS installed versus 100.554 FPS candidate (+0.10%)**. That difference is smaller than the control run spread, so there is no repeatable improvement. All measured saves are unchanged, with no audited competing game/compiler process during those four runs. The 144+ raw FPS target remains open. No canonical runtime-source or patch promotion was made. Existing graphics and original-30 controls were not edited.

## CPU time and next bottleneck

The installed baseline was profiled with `PS2X_PERF=1` and matching map/CSV. GetThreadTimes reports actual CPU time, rather than treating every sampled PC as a CPU-cycle measurement. In steady samples 25–39, the vector worker averages **94.6%** of one logical core, the GS renderer **81.5%** and the EE/game thread **41.8%**. The vector worker remains the leading CPU stage; the renderer is also substantial. Further main-thread offloading and removal of unused copy-back code do not establish a throughput gain. Common vector arithmetic/pipeline work or independent geometry parallelism is the next substantive target.

The full linked candidate really contains the shorter block (14,928 bytes versus 17,344, including padding); this was not a stale-binary test. `final-block47-code.asm` is code-only disassembly with an explicitly empty sample CSV.

The Windows hardware counters are enumerated, but the named WPR trace cannot start without a missing OS privilege (`0x80070005`). No trace or privilege setting was left changed. A separate process-only physical-core check was prepared; another build and the installed game then occupied the machine. Its waiting harness was stopped without changing those processes, so no affinity result is claimed. The normal full-runtime CMake configuration was restored. See `docs/journal/2026-10-19/data/vector-shadow-20261019.json`.
