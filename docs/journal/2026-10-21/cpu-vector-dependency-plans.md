# Worker dependency plans — 2026-10-21

The worker observer can summarize register dependencies once per compiled VU
block and use sparse store-provenance masks. These changes remain opt-in with
`DRAKAN_VU_BATCH_STATIC_REGS=1`. The 144+ raw FPS goal remains unmet. Installed
executables, launcher defaults and the existing 30 FPS/original controls were
not changed by this investigation.

## Changes

Fixed-image plans contain read-before-write masks and effective register writes.
Per-instruction prefixes preserve MTIR readiness and delayed VI branch reads.
Native UV/transform iterations retain the same prefix semantics. General
deferred aliases, active aliases, the certified packet-exit load and the separate
native packet observer keep their existing conservative paths.

Memory addresses, scalar and CLIP retirement, load/store ordering and packet
transfer provenance remain dynamic. Block stores already populate a bitset for
packet provenance; the observer now also uses it for forwarded load lanes and
final memory-write masks. Each completed dispatch clears only touched chunks.
The original 32-store support limit remains unchanged.

`DRAKAN_VU_BATCH_STATIC_REGS_VERIFY=1` compares register masks, prefixes, MTIR
readiness and forwarded lanes against the original tracker. Differences reject
the speculative result. `DRAKAN_VU_BATCH_VERIFY=1` additionally compares full
state and ordered output against serial execution and retains serial output.

An optional `DRAKAN_VU_BATCH_REUSE_PROFILE=1` diagnostic measures repetition;
it does not reuse geometry. Numeric repetition accounted for only 1.26% of
observed instruction pairs, while dependency metadata repeated for 93.18%.
This motivated static plans instead of a geometry-result cache.

## Verification

The register-plan build passed 83,305,184 live block checks and 241,645 accepted
group comparisons without a difference. The final store-provenance build
passed replay of both 4,000-call recordings with eight workers, including
register/prefix/forwarding checks and full state/output verification. Its live
run verified 265,746 accepted groups with zero mismatches, unsupported groups
or exceptions.

The expanded replay matrix also passed four-worker joined collection, general
aliases with eight workers and early dispatch, and exact loads with native
packet observation disabled. It includes native loop limits 1 and 256, masked
uploads, camera/motion records, packet ordering, code/mask barriers, late
rejection, owner exceptions, scratchpad binding and completion. Joined replay
published 3,024 collected geometry calls in its final pass and matched serial
state and output.

## Performance evidence

A clean run of the register-plan build measured 96.53 raw FPS. This is one
measurement, not a controlled speedup claim. Its original off/on comparison
also contained CPU contention and is retained as diagnostic evidence.

An additional arithmetic-only slot shortcut and hoisted pipeline scan passed
verification but showed no measurable gain in an uncontended ABBA comparison:
95.49 FPS control versus 95.40 FPS candidate. Those changes were discarded.

The uncontended store-provenance ABBA comparison measured 95.55 FPS control
versus 96.28 FPS candidate. The difference is smaller than the observed run
spread, so it does not establish a material win. Early dispatch measured
96.93 FPS with four workers, 92.00 with two and 94.32 with eight. The same
candidate measured 99.41 FPS with batching disabled; multicore remains opt-in.

The current phase profile recorded 21.71 seconds in serial execution, 2.78 in
preparation, 2.83 waiting, 0.74 in validation and 0.33 in collection. Worker
execution summed to 22.90 seconds across cores. These are cumulative diagnostic
times, not an FPS comparison. The owner still spends most sampled execution in
vector routines. CPU texture hashing/uploads/decoding are also active on the GS
thread; freeing VU time alone may expose that limit.

A bounded inherited-scratch predictor was also tested and discarded. It never
exempted a predicted word from validation. Targeted cases learned a scratch word,
collected a correct prediction, rejected a deliberately changed value and
recovered after backoff. Live verification checked 269,162 groups without a
mismatch, including 11,291 accepted groups with learned scratch inputs. Its clean
comparison was 95.82 FPS disabled versus 95.65 enabled. The subsequent phase
profile still spent 21.71 seconds on serial execution despite more speculative
work, so the prototype is excluded from the retained source.

Evidence is retained in `build/vu-store-provenance-20261021` and the separate
scratch-prediction experiment in `build/vu-memory-seeds-20261021`.
Measurements use the private deterministic encounter, audio on, 3840×2160,
GS scale 4, uncapped output, and frame generation/neural/ray effects off. Each
run audits external CPU use and preserves original and private saves.

The remaining issue is the critical path through dependent geometry and ordered
collection. More workers alone do not establish a win. The next decision uses
the current profile and measured raw FPS rather than isolated arithmetic speed.

Machine-readable evidence: `docs/journal/2026-10-21/data/vector-dependency-plans-20261021.json`.
