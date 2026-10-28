# Renewed GPU upload and geometry tests — 21 October 2026

The 144+ raw FPS goal remains unmet. The renewed texture-only control measured
**99.37 raw FPS** with no contention flag (external CPU peak 0.607 cores).
All other raw measurements in this round were contention-flagged, so there is
no clean candidate/control pair and no established FPS gain or regression.
No runtime default, installed binary, setting or save was changed.

## Additional graphics work

The native GPU upload prototype now joins consecutive changed render-target
page rows into one upload. Clean rows are never included: they can contain
GPU-only pixels that must survive. The native conversion buffer is retained
between calls to avoid reallocating it on each synchronization. Pixel
conversion, storage stamps, copy invalidation and CPU fallback are preserved.

The hardware test passed **8,769,808 exact RGBA comparisons**. It covers scales
1–8, partial row offsets, resource growth/reuse, restored shader/buffer/SRV/UAV
and render-target bindings, and all 256 dirty/clean patterns across eight rows.
The pattern checks compare the complete target, including untouched rows,
against a per-pixel reference rather than only checking uploaded regions.

The combined-row phase profile retained a reported **0.500 MiB of pixel upload
payload per latched gameplay frame**. Its `syncRT` inclusive scope averaged
1.032 ms/frame. Prior control and native-upload profiles measured 7.994 MiB /
1.302 ms and 0.500 MiB / 1.074 ms respectively. These are diagnostic wall-time
scopes under different contention, not proof of a timing improvement. The
pixel traffic reduction does not mean all GPU traffic decreased sixteenfold;
expanded pixels are still written/copied on the GPU.

## Geometry follow-through

An earlier seven-block zero-register preservation candidate had no valid
active gameplay comparison because its launch switch had been filtered out.
The frozen candidate was staged with its matched control and reference-checked
against both recorded gameplay streams: **4,000 runs each, zero mismatches**.
The earlier exhaustive block test had passed 114,688 state/data/packet cases.

The new gameplay manifest records `DRAKAN_VU_ZERO_RESETS=1`, and the candidate
printed its seven-block activation marker. This closes the activation gap.
The measured control/candidate values were 90.89 / 50.61 FPS, but external CPU
load peaked near fourteen cores in both runs; these are not a valid speed
comparison. This geometry pair uses its own older frozen renderer, so it must
not be compared directly with the newer GPU-upload builds.

## Gameplay measurements

| Comparison | Control raw FPS | Candidate raw FPS | Interpretation |
| --- | ---: | ---: | --- |
| Renewed GPU upload ABBA | 99.37 / 84.33 | 92.32 / 91.06 | Only the first control was clean |
| Combined-row upload | 96.09 | 59.85 | Both contended |
| Geometry zero-register preservation | 90.89 | 50.61 | Both contended; activation confirmed |

Eight raw runs and one diagnostic run completed. All original, source and
private save checks passed. No local compiler runs overlapped these benchmark
launches. External activity is recorded by the general CPU audit, not merely
by checking for other game processes.

The work remains private pending a useful performance comparison. It establishes
exact output and fewer allocation/upload operations, not progress to 144 FPS.

- [Measurements, hashes and phase data](data/upload-coalescing-20261021.json)
- [Tested source patch and hardware test](../../../analysis/experiments/upload-coalescing-20261021/README.md)
- Private runs, replay logs and build scripts: `build/texture-gameplay-20261021`.

The combined-upload patch was applied in an isolated fixture and checked against
the tested normalized source. Its README identifies the preceding native GPU
upload patch required before applying it.
