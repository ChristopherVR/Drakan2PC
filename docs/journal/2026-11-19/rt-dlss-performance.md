# 4K ray tracing and DLSS performance

## Test setup

Surdana town square on a Ryzen 7 9800X3D and GeForce RTX 5080. The game output
was 3840 × 2160, with frame limiting and VSync off. Ray-traced shadows,
reflections, and global illumination were enabled. Modern Water was disabled.
Each launch tested the same pose in DLSS Quality and with DLSS off. The
unprofiled runs counted completed world frames.

## Baseline

Four launches completed both modes. One additional launch timed out. Median
world FPS was 59.3 in DLSS Quality (54.2–64.3) and 27.1 at native resolution
(26.9–27.4). Host activity varied across launches.

| Mode | Acceleration structure | Lighting transport | Composition | Total DXR |
| --- | ---: | ---: | ---: | ---: |
| DLSS Quality | 0.87 ms | 1.26 ms | 3.98 ms | 6.12 ms |
| Native | 0.87 ms | 5.36 ms | 17.39 ms | 23.65 ms |

These medians come from D3D12 timestamps in two profiled launches. At native
resolution, the ray pass uses a 4096 × 4096 internal target. Composition
accounts for almost three quarters of the DXR pass. DLSS reduces the number
of pixels in that pass and exposes more of the render worker's CPU cost.

## Atlas samples and acceleration structures

The DXR texture atlas wraps signed pixel coordinates into power-of-two tiles.
Using a bit mask removes the signed remainder instructions in the transport
and composition shaders. An RTX 5080 hardware fixture matched 7,897,088
bytes of RGBA16 output across the original and modified shaders. Transport
ran slightly faster in the initial gameplay sample. The full frame rate
varied across launches.

A separate experiment skipped opaque frame geometry when every ray mask
would exclude it. The fixture matched exactly and the game skipped about
66,000 triangles per frame during dry warm-up. Geometry in the measured
town-square windows kept the shortcut inactive for most frames. The
experiment was removed.

## Shared lighting samples

The composition shader still filters the same 5 × 5 neighborhood. Each 8 × 8
thread group now loads its half-resolution transport tile and two-sample
border once into shared GPU memory. Neighboring pixels reuse the loaded
depth, reconstructed world position, material guide, linear color, and
incoming light. The filter keeps its material test, weights, and sample
order.

The gameplay probe alternated shaders every five seconds inside one process.
It counted completed world frames during the middle four seconds of each
complete interval. Separate launches collected GPU timestamps. Only intervals
after the ray pipeline was ready are included.

| 4K mode | Direct samples | Shared samples |
| --- | ---: | ---: |
| Native world FPS | 26.46 (25.69–26.81) | 28.19 (27.93–29.02) |
| DLSS Quality world FPS | 57.97 (55.76–58.28) | 58.02 (54.02–58.02) |
| Native composition | 18.52 ms | 15.40 ms |
| Native total DXR | 24.86 ms | 21.53 ms |
| DLSS Quality composition | 3.80 ms | 3.26 ms |
| DLSS Quality total DXR | 5.85 ms | 5.30 ms |

Each row is the median of three complete intervals for each shader. The
native test gained 6.5% world FPS and all three native pairs improved. A
separate warm launch timed the native GPU pass. Composition used 17% less
GPU time with DLSS off and 14% less with DLSS Quality. The world frame
rate with DLSS stayed within the variation between intervals. In the baseline
profiles the GS worker used 15.3–15.7 ms of CPU time per world frame with DLSS
Quality.

The profiled launch compiled both experimental pipelines and missed the
first native timing window while shaders were warming up. The release
configuration creates the shared-sample pipeline by default and creates
the direct-sample pipeline if initialization fails or
`GSR_RAY_CACHED_COMPOSE=0` is set. It skips the tile load whenever there is
no opaque lighting transport. The GPU timing log reports when the shared
tile was used.

The final default and direct-sample builds produced identical RGBA16
readbacks across 7,831,552 bytes in the RTX 5080 fixture suite. The suite
covers water, moving geometry, reflections, shadows, and indirect lighting.
The installed build also completed a Wartok Canyons gameplay check with
DLSS Quality and Modern Water enabled. The GPU log confirmed that the
shared tile was used with native water coverage.

The raw logs, launch recipes, and comparison scripts are under
`build/ootf-rt-perf-20261119/`.

The [Volcanic repeat filter comparison](ootf-volcanic-4k-dip.md) measured the
reported 2 FPS dip separately, with ray effects and DLSS disabled.
