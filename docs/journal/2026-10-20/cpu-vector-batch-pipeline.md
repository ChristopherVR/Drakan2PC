# Vector batch pipeline investigation — 2026-10-20

The 144+ raw FPS target remains unmet. This revision reduces worker preparation,
dependency tracing and ordered collection costs, and passes differential replay
and live gameplay verification. It does not establish a performance win over the
optimized serial path. Live batching and the experimental options remain off by
default; the installed executable and launch configuration were not replaced.

## Changes

- Prepare snapshots only for groups eligible for worker execution. Known serial
  draws avoid allocating/copying 16 KiB input images. Movable cut records avoid
  repeatedly copying those images while splitting DMA streams.
- Share immutable pipeline seeds between groups. Each group stores compact
  architectural state and a clock instead of copying the complete checkpoint.
  Restore preserves the destination interpreter's compiled-code cache validity.
  Cold-start fixtures exposed and now cover that cache ownership bug.
- Validate only consumed memory words, iterating set bits. Merge inherited memory
  in whole chunks or contiguous unwritten runs instead of scanning every word.
- Add optional early dispatch while input preparation continues. Job storage is
  reserved before publishing immutable inputs; late code uploads, segment limits
  and callback exceptions drain workers before destroying their inputs. Completion
  notification occurs while the group's mutex still protects its lifetime.
- Track deferred VF loads and repair untouched architectural lanes from the real
  input. Consuming an alias materializes its exact memory dependency. The bounded
  mode handles a dead packet-exit look-ahead load without general tracking on
  every instruction. Compile-time checks certify the fixed branch paths; other
  call boundaries conservatively retain dependencies. NaNs and sign bits are
  compared and repaired as raw bits.
- Retain fused packet arithmetic on workers. The bulk mode replaces a repeated
  seventeen-instruction dependency walk with precomputed register summaries and
  explicit observation of three loads and four stores. It preserves store
  forwarding, partial lanes, branch exits, destination wrapping, pipeline flags
  and bounded iteration counts. Unsupported cases use the original observer or
  serial fallback. The original kernel still performs arithmetic and retirement.

These changes preserve ordered packet/state collection, camera and motion
records, renderer commands and original callbacks on rejection. They do not
assume that successive geometry draws are independent.

## Correctness

The final bulk-mode live verifier checked **236,648 accepted groups**, covering
789,172 geometry calls, with **zero mismatches**. It exercised 121,548,579 traced
packet instruction pairs through 12,334,602 bulk observer calls. Verification
executes the original path and retains its output; a mismatch disables batching.

The replay matrix covers eight-worker differential verification, four-worker
joined collection, native loop limits 1 and 256, default tracing, general alias
tracking, and early dispatch with late barriers/exceptions. Replays compare full
VU/VIF state, scratch memory, packet bytes and order, camera/motion records,
masked PATH3 packets, EE scratchpad binding and completion. Targeted cases cover
consumed/dead VF loads, changed high words, cross-call overwrite guards, cold
compiled caches and clipping-count predicates. The test results and log hashes
are recorded in the companion JSON.

## Timing evidence and limits

Stage profiling identified roughly 4.1 seconds of preparation and 1.8 seconds of
validation in one gameplay run, in addition to roughly 20 seconds executing
serial groups. Lazy preparation reduced the observed preparation total to
roughly 2.9 seconds. A later word-mask profile recorded roughly 0.83 seconds in
validation. These are cumulative component measurements from separate runs,
with different group counts and some competing activity; they are not an
end-to-end speedup measurement.

Earlier runs whose process audit found no other game/compiler measured:

| Worker configuration | Worker raw FPS | Serial raw FPS |
| --- | ---: | ---: |
| General deferred-load tracking | 83.92 | 95.89 |
| Bounded packet-exit dead-load tracking | 88.85 | 95.61 |
| Fused packet kernel with instruction-by-instruction dependency observation | 56.68 | 90.97 |

The bounded dead-load run's matching exact-load worker control was 86.44 FPS.
This is a small improvement within that worker comparison, while serial remained
faster. General tracking and the first fused-kernel observer added too much CPU
overhead. The later bulk observer addresses that overhead, but its latest raw
timings cannot support a speedup claim.

The bulk run reported 77.50 FPS, followed by controls at 44.25 FPS for ordinary
workers and 48.67 FPS for serial. Follow-up inspection found **100% total CPU
load**, with multiple headless Chrome and Node processes consuming cores. The
existing benchmark audit sampled only other Drakan/compiler processes, so its
empty overlap list did not prove an idle host. The bulk runs and reverse-order
repeats are explicitly excluded from performance claims. Earlier empty-audit
results also lack a general CPU-load record and should be treated accordingly.
Other processes were left untouched.

The expanded audit in the last repeat measured an average of **11.30 logical
cores** of process CPU use outside the game (peak 14.54). It now records CPU
deltas and the busiest external processes alongside the narrower overlap list.
The reverse-order results ranged from 31.50–48.13 FPS for serial and
36.66–40.56 FPS for bulk workers; these contended figures remain excluded.

Runs use the same private executable, deterministic wall encounter, cap/vsync
off, frame generation and neural rendering off, 3840×2160 window, GS scale 4 and
audio enabled. FPS is the duration-weighted mean of the last five nonzero
`worldUpdates/s` windows. Source, private and original saves remain unchanged.
Do not compare executable versions or contended runs to claim a speedup.

## Controls and remaining bottleneck

| Environment variable | Default | Purpose |
| --- | --- | --- |
| `DRAKAN_VU_BATCH` | off | Enable isolated live geometry workers |
| `DRAKAN_VU_BATCH_THREADS` | 4 | Worker count, clamped to 1–8 |
| `DRAKAN_VU_BATCH_VERIFY` | off | Differential verifier; original output retained |
| `DRAKAN_VU_BATCH_PROFILE` | off | Cumulative preparation/worker/collector timings |
| `DRAKAN_VU_BATCH_STREAM` | off | Dispatch closed groups during preparation |
| `DRAKAN_VU_BATCH_PREDICATES` | off | Certified positive clipping-count gate |
| `DRAKAN_VU_BATCH_LAZY_LOADS` | 0 | 1: general aliases; 2: bounded packet-exit load |
| `DRAKAN_VU_BATCH_NATIVE_PACKET` | 0 | 1: exact observer; 2: bulk observer |

Generated clipping scratch data still imposes serial dependencies. Worker
execution also repeats pipeline/dependency bookkeeping around the vector math.
Reducing copies alone does not remove that critical path. The next substantial
step is reducing arithmetic and retirement overhead in the dependent geometry
kernels, or separating independent vertices within those kernels with exact
ordered state reduction. Simply increasing worker count does not solve it.

Machine-readable evidence: [vector-batch-pipeline-20261020.json](data/vector-batch-pipeline-20261020.json).
Private fixture, executable, compiler inputs and raw audit logs remain under
`build/vu-live-batch-20261019`. The public runtime projection includes the source
and tests, excludes private assets, and is checked against its pinned upstream.
