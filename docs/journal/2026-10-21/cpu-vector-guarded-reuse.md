# Guarded geometry reuse investigation — 2026-10-21

The 144+ raw FPS goal remains unmet. A clean comparison measured **97.19 FPS
serial**, **93.66 FPS with guarded observer reuse**, and **83.81 FPS with reuse
plus producer grouping**. Every run preserved the source, private and installed
saves. The previous runtime/default configuration is retained, including the
30 FPS/original cadence option. No executable or settings deployment was made.

These are individual clean samples, not a precise regression estimate. They
provide no reason to promote the alternatives. Earlier comparisons had general
CPU contention and cannot establish a speedup. The preceding retained serial
measurement was approximately 99.41 FPS; see the
[dependency-plan investigation](cpu-vector-dependency-plans.md).

## Implemented and tested prototypes

An exact control tape can reuse dependency observation while executing current
geometry arithmetic. Guards check VIF commands, run boundaries, write provenance,
address/branch inputs, integer-load controls, relevant MTIR inputs, dispatch PCs,
timing and pair counts. They do not use a hash to authorize reuse. A failed guard
discards private output and reruns the existing full observer. Metadata and packet
provenance still pass the original ordered collector.

Shadow mode independently executes the full observer and compares architectural
state, VU memory, ordered events and dependency coverage. It always retains the
fully observed output. The implementation was also tested with contiguous packed
guard records to reduce allocations and memory traffic.

Producer grouping joins a learned producer/consumer edge only inside a still-open
private group. A separate experiment reseeded selected rejected groups from the
completed batch head. The latter recovered only 8,092 of 27,809 attempted retries
and was not retained.

Two precision experiments preserve deferred origins through bitwise MOVE/MR32
and validate only the low halfword when MTIR consumes an index. Full arithmetic
reads retain full dependencies, and surviving copied registers restore their
exact input bits. These passed the synthetic/replay checks, but the live scene
did not exercise the narrow halfword cases, so they establish no gameplay gain.

## Verification

| Prototype / live run | Cache-hit shadow comparisons | Collector comparisons | Mismatches |
| --- | ---: | ---: | ---: |
| Guarded observation reuse | 163,006 | 167,864 | 0 |
| Reuse plus producer grouping | 138,198 | 142,770 | 0 |
| Packed guard records | 136,762 | 141,071 | 0 |
| Packed general deferred loads | 112,590 | 116,268 | 0 |
| Bitwise memory-origin copies | 129,898 | 134,033 | 0 |
| Inherited register origins | 135,338 | 139,300 | 0 |

The machine-readable evidence records collector counts and executable hashes.
Direct cached publication also passed both recorded input streams, exact and
deferred-load modes, one/two/four/eight workers, mixed camera/motion records,
masked uploads, ordered packets, late rejection, owner exceptions, scratchpad
binding and completion checks. Direct live cached/packed collection additionally
passed 225,096 and 178,670 serial comparisons respectively.

## Remaining CPU limit

In the profiled packed/grouped run, batch-owner phase totals included **16.07 s
serial fallback**, **9.45 s waiting**, **2.24 s preparation**, **0.65 s validation**
and **0.33 s collection**. Worker execution summed to 19.13 s across workers;
that sum is not directly comparable to owner wall time. Profiling is diagnostic
and is excluded from raw FPS measurements.

Reducing observer work and adding workers did not shorten the frame's serial
geometry critical path sufficiently. The next architectural work must address
per-object geometry computation or dependencies themselves, rather than simply
increasing worker count or grouping more calls.

A separate camera transport opportunity copies 16 KB for a production tap that
reads 112 bytes. It was not implemented here: that tap is disabled in the raw
baseline and cannot explain the measured ceiling.

## Reviewable artifacts

The [measurement evidence](data/vector-guarded-reuse-20261021.json)
contains contention audits, save checks, binary hashes, verification counters
and phase totals. Three source-only
[experimental patches](../../../analysis/experiments/vector-guarded-reuse-20261021/manifest.json)
preserve the tested approaches. Each was applied in an isolated fixture and
checked against the normalized tested source. They are not part of the runtime
projection, contain no game binaries/captures, and must not be treated as the
recommended performance configuration.
