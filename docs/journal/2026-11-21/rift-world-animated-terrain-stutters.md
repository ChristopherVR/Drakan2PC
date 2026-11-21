# Rift World animated terrain stutters

After the DXR composition change, 36 of the 37 world-frame intervals over
33.33 ms overlapped at least 2 ms of a slow resident-geometry submission. The
geometry worker accounted for 429 ms inside those intervals. The host spent
406 ms presenting the surrounding frames and less than 0.05 ms acquiring the
renderer lock. These are overlapping trace observations, not an allocation of
all frame time.

Seven guest memory snapshots in the benchmark tunnel showed the same 206
layers and quad topology. Four layers had changing vertex heights. The
renderer received 71,517 triangles on each refresh and treated each changed
height hash as a new scene. This discarded the terrain lookup, material
projections, texture caches, model asset cache, and temporal history. Building
and refreshing the scene introduced stalls on the renderer worker while the
GPU was busy presenting frames.

The capture now tracks topology independently of the sampled heights. The
renderer applies height changes to the existing scene when the layer storage
and topology match. It updates affected triangle planes and projected geometry
and publishes a new geometry version for DXR. The native world animation and
existing capture cadence are preserved. Changes in layout or quad topology use
the full scene replacement path.

The scene-geometry test covers a height change with the terrain lookup already
built. It checks the new height and triangle matching, retained material
projection, and an unchanged snapshot held by a previous frame. It also
checks that a topology change uses the replacement path.

## Benchmark method

The RTX 5080 ran Dota 2 throughout all launches. The harness repeated the
Rift World tunnel route and scripted turn from the DXR composition benchmark.
The D3D11 renderer used DLSS Quality from 2560 × 1440 to 3840 × 2160 with
ray-traced shadows, reflections and GI on. Modern Water, VSync and the frame
cap were off. Each release build used the same assets and frozen settings.
The order was prior optimized build, patched build, patched build, prior
optimized build.

Each launch contributed the middle 3.8 seconds of each of fourteen 5-second
windows after the first screenshot. The parser checked the level, DXR startup,
DLSS mode and dimensions, 4K screenshot dimensions, and clean shutdown. A
process sampler checked for concurrent Drakan instances or compiler activity.
Another sampler recorded Dota 2's 3D-engine utilization. Game-frame intervals
came from native display commits. Presentation intervals came from the host
returning from presentation with a new world frame.

Raw logs, validation JSON, GPU samples and captures are in the ignored
`build/rift-stutters-20261121/` directory. The throughput runs use frame tracing
and omit detailed GPU and stack profilers. Dota 2 load varies during the runs.

## Results

All 28 paired windows qualified, giving approximately 106 measured seconds
per build. The same Dota 2 process ran throughout. Its sampled 3D-engine
utilization ranged from 12% to 47% before and 7% to 48% after. The median
within-pair change in Dota 2 utilization was −0.5 percentage points.

| Measurement | Prior build | Patched build |
| --- | ---: | ---: |
| World FPS | 68.84 | 67.07 |
| Presented world FPS | 68.82 | 67.06 |
| 99th-percentile world-frame interval | 34.78 ms | 31.82 ms |
| 99th-percentile presentation interval | 27.78 ms | 25.59 ms |
| Longest presentation interval | 38.12 ms | 32.81 ms |
| World-frame intervals over 33 ms | 114 | 44 |
| Presentation intervals over 33 ms | 18 | 0 |
| World-frame intervals over the prior benchmark's 33.33 ms threshold | 93 | 35 |
| Presentation intervals over 33.33 ms | 13 | 0 |
| Terrain refreshes taking over 8 ms on the render worker | 233 | 0 |

All 93 baseline world intervals over 33.33 ms overlapped a slow terrain
refresh. All 35 patched intervals above that threshold overlapped the host
presentation span. The first patched run had none. The second had 35, with
zero presentation intervals above the threshold in either run.

Pooled world FPS fell by 2.6%. Across the 21 pairs with sampled median
Dota 2 utilization within five percentage points, pooled world FPS went
from 67.72 to 61.42 and presentation intervals over 33.33 ms went from 11 to
zero. This sampler measures Dota 2 utilization without controlling the
workload, and shared GPU load varies within each window.

Both release builds exited cleanly on every launch. The terrain regression
test passed. The 4K captures show the expected tunnel framing and HUD, with
lighting and animation differences across the independently timed frames.
