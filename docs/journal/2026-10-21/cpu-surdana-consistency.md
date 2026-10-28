# Surdana consistency and texture resource reuse — 2026-10-21

Target: consistent 144+ raw FPS, especially approaching the first mission city and fighting there. The target is not achieved. All new candidates remain private; installed binaries and defaults were not changed.

## Work completed

- Added repeatable 30-second movement and combat scripts using existing Level 1A – Surdana save fixtures. These are proxies: the exact city viewpoint reported by the user has not been reproduced or visually confirmed.
- Added diagnostic intervals between distinct completed world epochs, including per-window p50/p95/p99, maximum interval, and counts above 6.94/16.67/33.33 ms. These measure completed world output, not physical display latency. Percentiles are reported per window, not averaged into a global p99.
- Implemented exact 64-block stamp maxima with scalar boundary checks. Passed 210,688 reference queries after interleaved block and rectangle writes, including wrapping. Component range queries became much faster, but stamp checks occupied only roughly 0.008–0.015 ms per latched frame in these gameplay profiles. This is not the main bottleneck.
- Implemented reuse of an invalidated immutable GPU texture only when every decoded mip matches its previous CPU pixels. Coherence flushes, decode, callback, animation classification and replacement/upscale selection still run. Retained CPU pixels count against the cache budget. The Release build succeeded; both gameplay routes exercised reuse.

## Measured work removed

Final five profile windows, normalized by completed display latches:

| Resident candidate | Texture creation/reuse attempts per latch | Actual creation attempts per latch | Avoided allocations |
|---|---:|---:|---:|
| sur-motion-resident-a | 32.00 | 0.36 | 98.88% |
| sur-combat-resident-a | 54.17 | 0.25 | 99.53% |

This is a measured reduction in resource creation attempts, not an FPS percentage. The textureCreate scope includes comparison and reuse; actual creation attempts are its call count minus textureResourceReuse calls.

## Remaining limits and bottlenecks

All six scene runs completed with original/private saves unchanged, but all were externally contended. ModernWarfare and other Drakan tests were active during this investigation. The scene runs also enable diagnostics, including VU statistics, so none is a clean raw-FPS comparison. No builds or unit tests from this investigation ran concurrently with its gameplay measurements.

Blocking GPU readback maps remain a larger concern. In the final resident profile windows, readback waits averaged about 6.0 ms per latch during movement and 9.5 ms during combat, with individual waits reaching 26 and 44 ms. These include external GPU/CPU contention; they do not establish the uncontended game cost or prove the cause of the user’s stutters. VU execution remains active and the multicore/default geometry mode is unchanged.

The new resource-reuse candidate needs broader visual checks and an uncontended gameplay comparison before promotion. The prior clean control remains 99.37 FPS; a consistent 144+ result has not been demonstrated.

Evidence: [results JSON](data/surdana-consistency-20261021.json). Source patches, reference test and routes: [experiment](../../../analysis/experiments/stamp-groups-20261021/README.md).
