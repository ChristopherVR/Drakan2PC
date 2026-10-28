# Experimental geometry dependency patches

These alternatives are archived for review, not enabled or recommended for
deployment. They did not meet the 144+ raw FPS goal. The clean comparison
favored the retained serial configuration.

Each patch targets the same retained `ps2_vu1_batch.cpp`; apply only one at a
time. `manifest.json` records baseline, tested source, patch and executable
hashes. Each patch was applied to an isolated baseline fixture and reproduced
the normalized tested source exactly. No game binaries or captures are included.

- `guarded-reuse.patch`: exact dependency-observer guard tape and shadow mode.
- `packed-producer-groups.patch`: contiguous guards plus learned producer groups.
- `precise-bitwise-origins.patch`: deferred MOVE/MR32 origins and low-halfword
  MTIR dependencies, including inherited-register origins. This precision path
  passed replay tests but had no narrow-read coverage in the measured live scene.

The prototypes are opt-in through `DRAKAN_VU_BATCH_TRACE_CACHE=1`; shadow
verification uses `DRAKAN_VU_BATCH_TRACE_CACHE_VERIFY=1` and must precede any
release-mode experiment. Grouping additionally uses `DRAKAN_VU_BATCH_FUSE`.
Bitwise-origin observation requires `DRAKAN_VU_BATCH_LAZY_LOADS=1` and
`DRAKAN_VU_BATCH_ALIAS_COPIES=1`. Existing batch prerequisites and collector
verification remain required. These switches are not installed defaults.

See [the investigation](../../../docs/journal/2026-10-21/cpu-vector-guarded-reuse.md) and
[the evidence](../../../docs/journal/2026-10-21/data/vector-guarded-reuse-20261021.json) for tests,
limitations and the remaining serial critical path.
