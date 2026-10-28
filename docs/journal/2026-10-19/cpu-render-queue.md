# CPU render pipeline performance

The installed build enables ordered graphics submission and CPU ray-data reuse.
The executable is SHA256
`ebb54534b8246c425a9946d3d69c24544163da6606930fc6c721153149ccbe58`.
The deployment receipt is
`docs/journal/2026-10-19/data/cpu-render-queue-20261019.json`.

## Measurements

The same stationary gameplay fixture, audio enabled, 4x scale and a 3840x2160
window target were used. Vsync, frame limits and frame generation were disabled.
Compiler/game process overlaps were recorded and excluded from comparisons.

| Configuration | Before | After | Gain |
| --- | ---: | ---: | ---: |
| Raw rendering; optional DLSS/neural/water/ray effects disabled | 64.76 FPS | 89.95 FPS | 38.9% |
| DLSS quality, neural rendering, modern water, ray reflections and shadows enabled | 17.96 FPS | 24.89 FPS | 38.6% |
| Ordered queue alone, same binary control | 66.36 FPS | 89.50 FPS | 34.9% |

The enhanced result averages two clean runs at 24.90 and 24.89 FPS. Its
pre-change binary was rerun after optimization to check the baseline. A
same-binary ray-cache-off control measured 21.49 FPS; that toggle measures only
part of the change, since the new code also changes buffer reservation and
material-loop organization.

Raw performance does not describe the saved enhanced configuration. Those
preferences remain enabled. The 144/200 FPS target has not been reached, and a
fixed scene does not establish whole-game performance.

## Bottlenecks and changes

Barrier attribution found roughly 6,200 full vector-queue waits per second at
GIF_MODE reads, consuming about 1.37 seconds of EE waiting per two-second window.
Removing that wait exposed the next barrier at PATH3 submission, then scene
camera markers.

VIF1 jobs, copied PATH3 packets and owned scene-marker tasks now use the same
bounded FIFO. The EE can prepare later work while the worker preserves packet
and metadata order. Queued scene tasks hold renderer lifetime leases and own
camera/geometry/model snapshots. GIF_MODE remains in the EE-owned register
table; worker-owned status, VIF0, readback and display boundaries retain their
completion barriers.

After this change, about 82.3% of vector-worker samples were productive VU
execution, with only 3.4% in ntdll. VU execution remains the principal raw
rendering CPU cost.

Profiling the enhanced configuration found substantial CPU work in RayWater
material/geometry assembly and repeated vector allocation. The renderer now
reuses staging vectors, reserves the complete material capacity, and caches
immutable mesh attributes. Exact mesh ownership, material flags and texture
table indices invalidate the cache. Instance transforms, opacity, lighting,
texture uploads and GPU acceleration structures retain their existing update
paths.

## Validation

- 101,258 ordered worker jobs passed completion, metadata, back-pressure and
  shutdown checks.
- Actual memory/DMA tests passed FIFO VIF/PATH3/task ordering, source reuse,
  chain/scratch/wrapping transfers, masked PATH3 and blocked-worker overlap.
- Live VU reference checking completed 327,680 runs with zero mismatches.
- Existing VU checks passed 24,000 recorded reference runs, 32,768 native-loop
  comparisons, normalization, retirement and timing cases.
- Cached ray attributes matched uncached construction byte-for-byte over at
  least 360 enhanced gameplay frames.
- Hardware ray tests passed with caching enabled and disabled: exact instance
  output, alpha changes, moving instances/blockers, reflections, shadows, sky,
  scene retirement, level reset, finite RGB and original alpha preservation.
- Raw and enhanced screenshots were inspected. Saved preferences, original
  saves, launchers and existing graphics deployment receipts were preserved.

## Controls and evidence

`DRAKAN_RENDER_QUEUE=0` restores synchronous PATH3/scene submission.
`GSR_RAY_CPU_CACHE=0` disables CPU ray staging reuse and attribute caching.
`GSR_RAY_CPU_CACHE_CHECK=1` compares cached attributes against uncached
construction; use it for correctness checks, not performance measurements.

Native VU loop modes remain opt-in. Two alternating runs per mode measured
89.51/89.49 FPS for default dispatch and 89.40/90.07 FPS for mode 3, so the
default remains original block dispatch with the validated retirement change.

`Play-Original30.cmd` and the in-game original-30 option remain available.
All detailed logs, matching maps, profiles, comparisons and the backup receipt
are in `build/vector-loops-20261019`. The runtime projection remains in
`patches/runtime/drakan-runtime.patch`; the separate ray CPU diff is
`build/vector-loops-20261019/ray-cpu-cache.patch`.
