# Live multicore VU geometry, 2026-10-19

Status: experimental and disabled by default. The 144+ raw FPS target is not achieved. No game executable or launcher was installed by this investigation.

Later scheduling and observer refinements are measured in [the follow-up report](cpu-vector-batch-scheduling.md). The measurements below describe the earlier implementation.

## Measured gameplay

Same private executable: `7472B161CA1B804C51C64812E9B50D55ED64A6F4EC2633B02E9F5EF5F5C1E939`. Ryzen 7 9800X3D, four geometry workers. 3840Ã—2160 window, internal scale 4, audio on, uncapped, vsync off, frame generation off, DLSS/neural and ray effects off. Source, fixture, input, executable and save audits are recorded beside every run.

| Mode | Raw world FPS | Overlapping process samples |
| --- | ---: | ---: |
| Existing serial VU execution | 99.99 | 0 |
| Live multicore collection, verifier off | 53.86 | 0 |

Both raw runs had no overlapping game/compiler/linker processes. Verification timings are excluded from performance comparisons.

The live verifier checked 204,941 accepted groups, covering 801,117 geometry calls and 944,185,766 instruction pairs, with zero mismatches. It compared worker results against original serial callbacks and retained original output. The raw worker run collected 885,053 calls; 2,059,530 speculative calls failed dependency validation and used the original callbacks.

The implementation currently does more work than it saves in this fixture. Keep it out of default launchers. The prior command-slice version also measured 47.75 FPS against 97.24 FPS in a clean serial control. Adding cores alone does not remove the dependency chain or the cost of rejected speculative geometry.

## Live transport and collector

The VIF worker gathers an owned, bounded stream containing VIF DMA data, PATH3 packets and renderer records. A private one-pass VIF decoder records real command boundaries and input snapshots. It splits at MSCAL commands rather than at whole DMA jobs: a single job can finish old geometry and start another setup. Absolute motion-marker offsets remain intact, and a motion cursor spans all slices of the original job. Original DMA completion is published only after every slice and ordered renderer record completes.

Each worker has isolated VU code/data and interpreter state. It applies VIF commands and computes geometry without touching the real renderer, global EE scratchpad binding or owner VU state. The collector validates the state actually consumed: VF/ACC lanes, VI registers, Q/P/I, memory words or ILW low halves, and flags. It rebases absolute scheduler clocks, preserves unwritten state and merges computed output in submission order. The fixed image has no STATUS read/replace instruction, allowing exact sticky-flag contribution merging. MAC dependencies respect actual retirement time, including stalled exact pairs.

CLIP provenance follows each six-bit history shift and its four-cycle retirement. Flag queries validate only surviving consumed input bits, and the collector repairs unqueried inherited history in final CLIP/working state. Empty scheduled slots skip dependency bookkeeping while still clearing the one-pair VI branch backup. The actual interpreter arithmetic and scheduled instruction slots are unchanged.

A bounded, four-way history cache predicts architectural inputs and the three saved integer words at qword 65. Its 4,096 compact entries retain no old scheduler, packet, camera, motion or renderer records. MSCAL resets scheduling; the collector rebases the clock and validates every consumed predicted value against actual live owner state. Hash avalanche avoids clustering aligned DMA/object identifiers. This is input prediction: all current geometry still executes. The verifier saw 315,145 predictions and accepted 198,099; misses or incorrect predictions never bypass validation. `DRAKAN_VU_BATCH_PREDICT=0` disables this diagnostic predictor.

Packets retain transfer-time provenance. Unwritten inherited payload bytes can be reconstructed from the real initial scratch memory; GIFtag bits controlling length and termination remain dependencies. Stores overlapping a lazy transfer stay conservative. Camera snapshots receive the same treatment. PATH3 masks, held packets, motion scopes, renderer records and drains execute on the collector at their original stream positions.

Code uploads and unsupported or dependent geometry retain original callbacks. Oversized batches decline without changing owner state. The optional verifier compares full output and normalized scheduler state, retains serial output and disables further collection after a mismatch. Only inactive scheduler data and per-instruction decoder scratch are normalized; active state is retained.

## Remaining CPU constraint

The largest recorded memory conflict is VU qword 65, which stores and restores clipping-loop integer context. Other conflicts involve generated geometry scratch buffers. See the measured rejection histogram in the results JSON. A whole setup/continuation group still consumes results produced by earlier groups. Starting every worker from the same stale checkpoint therefore causes expensive rejection and serial recomputation.

The next useful optimization is to forward or isolate this clipping context, or partition independent vertex/triangle arithmetic inside the continuation pipeline. Dependency checks cannot simply be removed: they preserve real geometry and packet behavior. The live batching and ordered collection path is now available to validate that work.

## Validation and reproduction

Eight focused CTests pass, including production queue barriers, VIF uploads, ring transfers, busy retirement, flags, SIMD and operand checks. Recorded tests compare 4,000 geometry calls in full-upload, sparse-upload and joined-DMA forms, with interleaved PATH3/renderer records, masked uploads, saved PATH3 FIFO, motion sidecars and code/batch-limit barriers. Joined-DMA tests check 13,409 packets and 4,000 camera updates; sparse tests check 16,075 packets and 42 camera updates. Four- and eight-worker checks retain exact output. These are correctness tests; their sparse-input authoring uses a serial oracle and is not a gameplay FPS measurement.

Repeated-input cases run 8,000 geometry calls with fresh motion frame/scene epochs, exercising predicted seeds while ensuring current sidecars and renderer records are retained. Use `--repeat` alongside either recorded transport mode to reproduce this check.

Private executable: `build/vu-live-batch-20261019/candidate/drakan.exe`. Set `DRAKAN_VU_BATCH=1` to opt in and `DRAKAN_VU_BATCH_THREADS=4` (1â€“8 supported). Dependency validation is enabled with batching; `DRAKAN_VU_BATCH_DEPENDENCIES=0` selects strict whole-state validation for diagnostics. `DRAKAN_VU_BATCH_VERIFY=1` retains and checks original serial output and must be off for raw timing. Recording and incompatible mutable diagnostics disable batching.

Run `vu_live_batch_tests <gameplay.vur>`, optionally with `--sparse` or `--joined`. The private build and benchmark scripts, frozen link-input hashes, manifests, logs and overlap audits are in `build/vu-live-batch-20261019/`. The distributable source projection excludes the game VU image and preserves unrelated authored sections. Structured results: `docs/journal/2026-10-19/data/vector-live-batch-20261019.json`.
