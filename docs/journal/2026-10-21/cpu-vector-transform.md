# Paired geometry transforms — 2026-10-21

The transform kernel now processes two independent XY vertices in one SIMD
operation and can run inside the live geometry workers. The **144+ raw FPS goal
remains unmet**. The isolated kernel is faster, but whole-game measurements do
not establish a default-on performance win. `DRAKAN_VU_TRANSFORM_KERNEL=1` and
live batching remain opt-in. The installed executable was not replaced.

## Implementation

The fixed loop at pair 334 writes only X/Y. Its kernel places one vertex in the
low SIMD lanes and the next vertex in the high lanes, and reuses normalized
matrix operands across bounded iterations. Four shared FMAC operations compute
both vertices, including the existing exact handling of zero, denormal,
overflow and exceptional operands. Arithmetic order and rounding are preserved.

Arithmetic/flag computation is separate from architectural writeback. MAC and
current STATUS select the last vertex; sticky STATUS accumulates both vertices
and every intermediate product. Stores retain their original issue cycles,
including the preceding vertex carried in VF18. A look-ahead load moves only
when it cannot overlap the preceding store. Alias, counter, budget and loop-limit
boundaries retain ordered single-vertex execution. Pipeline retirement occurs
at the same statically checked exit.

The worker observer tracks the same bounded iterations and consumed inputs,
then validates and collects state and packets in original order. A different
pair count or unsupported condition rejects speculative results. The native
loop limit remains respected; worker tracing falls back for limits above 32.

## Correctness and test coverage

The targeted kernel tests previously supplied untracked code buffers, which kept
`execute` on the generic interpreter despite changing the native dispatch table.
The transform, UV and packet tests now use tracked microcode memory and assert
that AOT execution is active. The transform test also calls the kernel directly
to require fast-path coverage.

The final tests pass:

- 114,688 transform state/data/packet comparisons per configuration, with native
  loop limits 1, 32 and 256, budgets 1–1024, signed counter/address wrapping,
  arbitrary IEEE bits, sparse matrices, pending pipelines and dependent tails.
- 114,688 UV and 114,688 packet comparisons with compiled execution active.
- Eight-worker sparse differential replay and four-worker joined collection,
  including late barriers, exceptions, scratchpad binding and ordered sidecars.
- A recorded 4,000-call gameplay fixture compared with the independent reference
  interpreter, with the kernel both disabled and enabled: zero mismatches.
- Live gameplay verification: **149,406 accepted groups**, covering 505,288
  geometry calls, with **zero mismatches**. The worker observer exercised
  1,619,590 native transform pairs, with no unsupported groups or exceptions.

An earlier loading-time reference run reported at least 212,992 comparisons
with zero mismatches. It exceeded its 100-second limit before reaching the
gameplay script, so it is not counted as a completed gameplay benchmark.

## Measurements

The isolated tests use eight ABBA rounds, each containing 240,000 calls with a
2,048-cycle budget. They measure thread CPU time and require AOT activation:

| Transform workload | Throughput ratio | CPU-time reduction |
| --- | ---: | ---: |
| Dense finite matrices | 3.24× | 69.2% |
| Sparse matrices with zero products | 2.72× | 63.3% |

These are kernel measurements. They exclude VIF decoding, GS work, dependency
observation and ordered collection. They must not be presented as game FPS gains.

The final uncapped encounter runs used one frozen executable, 3840×2160, GS
scale 4, audio on, and cap/vsync/frame generation/neural rendering off:

| Configuration | Raw world FPS | Mean external CPU cores |
| --- | ---: | ---: |
| Kernel disabled, serial | 52.60 | 10.74 |
| Kernel enabled, serial | 54.17 | 10.75 |
| Kernel enabled, four workers | 47.20 | 9.18 |

All three runs were CPU-contended. The earlier ordinary-only kernel's ABBA runs
also varied substantially. The full recorded replay's short wall-time samples
were inconsistent (25.35 ms disabled versus 42.03 ms enabled, best of five), so
those samples also do not establish a performance win. Correctness passed in
both replay modes. Whole-pipeline profiling is needed to explain the remaining
cost instead of extrapolating isolated SIMD throughput to frame rate.

The benchmark audit now records CPU deltas from other processes and marks busy
samples alongside game/compiler overlaps. An empty game/compiler overlap list
does not establish an idle host. Other processes were left untouched. Source,
private and original saves remained unchanged in completed encounter runs.

## Controls and evidence

`DRAKAN_VU_TRANSFORM_KERNEL=0` retains the existing transform path; `1` enables
the new kernel. `DRAKAN_VU_BATCH=1` additionally enables live workers, and
`DRAKAN_VU_BATCH_VERIFY=1` compares worker results with original execution.
Existing frame-limit controls are unchanged.

[Machine-readable evidence](data/vector-transform-20261021.json)
contains executable/source hashes, CPU rounds, run settings, contention audits,
test-log hashes and verification diagnostics. The public runtime projection
applies to its pinned upstream, matches the frozen runtime inputs and excludes
private game assets. Private fixtures and raw logs remain in
`build/vu-transform-20261021`.
