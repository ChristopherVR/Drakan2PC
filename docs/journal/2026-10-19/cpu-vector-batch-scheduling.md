# VU batch scheduling and observer refinement, 2026-10-19

Status: experimental, default off. The 144+ raw FPS target remains unmet. No executable or launcher was installed. Existing optimized serial execution and frame-cap controls remain available.

The later [pipeline and dead-load investigation](../2026-10-20/cpu-vector-batch-pipeline.md) records subsequent changes and measurements.

## Changes

Repeatedly rejected draws now execute once through the original callbacks. A scheduling-only history backs off for 63 occurrences after the first rejection, increasing to 1,023 occurrences; a later probe can restore worker eligibility. Current input snapshots continue updating during bypass. Cache matches never authorize results: every worker result still passes full-input or consumed-input validation.

The owner executes the head group while workers compute later groups. This removes unnecessary head-group tracing, packet capture and checkpoint collection, and preserves the default fused kernels for that work. All packets, cameras, motion markers, PATH3 masks, drains and completion barriers retain their original stream order.

A shared dependency plan of at most 64 bytes per instruction replaces reads of each interpreter's larger decoded execution records. The fixed seven-pair UV loop uses its existing fused arithmetic kernel on workers. The observer models its bounded VI11 countdown, VI12 address progression, issue clocks, register lanes and memory accesses. Unexpected pair counts fail closed to original execution; native loop limits above 32 use ordinary tracing.

## Gameplay measurements

Same private executable: `04FAF80B4C7DD6E0CB2A5C69F6F1EFF952D46D8444ADA980D2FE3E92CDD766D3`. Ryzen 7 9800X3D; 3840Ã—2160 window, GS scale 4, audio on, uncapped, vsync/frame generation/DLSS/neural/ray effects off. Raw world FPS is weighted over the last five nonzero gameplay windows. Verification FPS is excluded.

| Mode | Raw world FPS | Overlapping process samples |
| --- | ---: | ---: |
| Serial control | 99.74 | 0 |
| Adaptive collection, 4 workers | 86.95 | 0 |
| Adaptive collection, 2 workers | 77.90 | 24 |

The final four-worker / serial comparison has no recorded process overlaps. Runs with nonzero overlap samples are contended diagnostics. They do not establish a speedup over serial execution. The overlap records include process IDs and executable paths; no other chat's processes were changed.

The clean four-worker path remains 12.82% slower than serial execution. It recovers substantially from the earlier clean 53.86 FPS worker result, but should remain disabled by default.

The earlier clean implementation measured 53.86 FPS with four workers versus 99.99 serial. Do not compare contended new measurements with that clean historical pair as a speedup claim.

## Correctness and wasted work

The final gameplay verifier checked 246,521 accepted groups, covering 822,290 geometry calls, with zero mismatches. It bypassed 207,447 known dependent groups and executed 10,035 heads on the owner. Only 25,087 speculative geometry calls were rejected, versus 1,876,726 in the earlier verifier. Counts are from different gameplay runs and describe duplicate-work volume, not a normalized FPS improvement.

Repeated 4,000-call replay cases compare full architectural state, VIF state, scratch memory, packet bytes/path/order, camera snapshots, interleaved renderer records, motion cursors, held masked PATH3 packets, EE scratchpad binding and completion. Final tests cover eight-worker sparse verification, four-worker joined collection, native loop limits 1 and 256, and disabled adaptive scheduling. Every case passes. The source projection applies to the pinned public runtime revision; investigated source hashes match the frozen executable inputs and private game assets are excluded.

## Controls and limits

`DRAKAN_VU_BATCH=1` opts into batching; the default remains off. `DRAKAN_VU_BATCH_THREADS=2` or `4` selects workers. `DRAKAN_VU_BATCH_ADAPTIVE=0` disables learned scheduling for diagnostics. `DRAKAN_VU_BATCH_VERIFY=1` compares worker results with original execution and retains original output; a mismatch disables batching. Prediction and consumed-input controls remain available.

Generated clipping scratch buffers still create real serial dependencies. Avoiding their duplicate execution reduces CPU waste, but this patch does not split the dependent vertex/clipping pipeline into independent multicore work. Batch preparation, dependency observation and ordered collection also cost CPU time. No 144+ FPS or default-on multicore improvement is claimed.

Machine-readable evidence: `docs/journal/2026-10-19/data/vector-batch-scheduling-20261019.json`. Private replay fixtures, executable, audit logs and compiler inputs remain under `build/vu-live-batch-20261019`.

The [2026-10-20 pipeline follow-up](../2026-10-20/cpu-vector-batch-pipeline.md)
reduces snapshots, validation scans and packet dependency observation, and adds
dead-load repair. It also identifies general CPU contention that the earlier
game/compiler-only process audit could miss. Empty overlap lists alone do not
establish an idle benchmark host.
