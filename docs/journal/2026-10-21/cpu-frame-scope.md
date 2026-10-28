# Broader CPU bottleneck check — 2026-10-21

The 144 FPS goal remains unmet. Optimizing only VU instruction arithmetic was
too narrow: the serial VU worker is busy, but graphics submission also consumes
most of a core. The next larger optimization should be selected by its effect
on the complete frame, not just a VU replay or worker throughput measurement.

## New diagnostic evidence

The retained serial configuration was profiled with independent Windows
per-thread cumulative CPU counters. Over approximately 17.26 seconds within
the configured 22–40 second sampling window:

| Thread role inferred from symbols | Average CPU cores consumed |
| --- | ---: |
| VU worker | 0.951 |
| Graphics submission / texture processing | 0.835 |
| Generated game code | 0.409 |
| Four texture upscale workers combined | 0.408 |

Other threads also consumed CPU, including the profiler. These are diagnostic
thread CPU measurements, not percentages of frame time. The RIP sampler
suspends every thread and captures blocked states too; its sample counts must
not be interpreted as CPU-time attribution. The old category report selected
the thread with the most executable samples and incorrectly called it busiest.
That label has been corrected. `scripts/perf/hostprof_threads.py` now correlates RIP
states with independently measured thread CPU time.

The graphics thread had 661 samples in `texup::hashContent`; texture upscaling
workers repeatedly sampled in `smoothUpscale`. This suggests worthwhile work
outside geometry, but does not prove that textures determine the FPS ceiling.
The existing geometry measurements still support investigating the VU path.

Evidence: `docs/journal/2026-10-21/data/vector-serial-thread-profile-20261021.json`.
Raw inputs are in `build/vu-packed-cache-fusion-20261021/`: candidate/hostprof.csv,
serial-thread-cpu.json, serial-thread-process.json and
t/serial-thread-profile-a. The private executable hash was
B725BB67CDB401513460ED2EDB3F54F80E870A6AD87D3149632C4F0D7D71869B.
The input script completed and all three save checks passed. The harness failed
on a missing process exit code and did not write completion/contention evidence;
this run is not a clean FPS benchmark. The analysis window approximates sampler
startup using process start plus its configured delay.

## Implemented reduction in texture work

The texture decode path now reuses its freshly computed content hash for
replacement/upscale lookup. It invalidates that local hash when menu smoothing
changes the pixels. It does not cache identity across uploads or skip texture
coherence checks.

RGB-only replacement matching now hashes masked pixels directly, removing a
temporary image allocation and a separate mask/write pass. Hash values and
replacement pack identifiers remain unchanged. Release renderer compilation
and the `texup` test passed in `build/texture-hash-review-20261021`, including
RGB equivalence for empty, odd-sized and ordinary images. No gameplay FPS gain
has been established for these source changes; no installed binary was replaced.

## Direction

Measure graphics submission and VU execution together when assessing the next
candidate. Investigate repeated texture decode/upscale and cache invalidation
alongside larger native geometry loops. Do not infer unchanged texture contents
from an address alone: GS slots, palettes and render-target contents can change.
Keep speculative multicore geometry disabled by default until it beats serial
in complete gameplay. Preserve uncapped and original/30 FPS controls.

The [follow-up gameplay investigation](cpu-texture-submission.md)
benchmarked the texture changes and tested exact block footprints, disabled
geometry diagnostic timing, and native GPU row expansion. The GPU prototype
reduced measured upload traffic sixteenfold; contention prevented establishing
an FPS improvement, so the new variants remain private.
