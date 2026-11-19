# Render-worker performance with 4K DLSS Quality

## CPU profile

The baseline was commit `d304e6f5` on a Ryzen 7 9800X3D and GeForce RTX 5080.
Surdana town square used Direct3D 11 and DLSS Quality at 2560 × 1440 input
and 3840 × 2160 output, with ray-traced shadows, reflections, and GI on.
Modern Water, VSync, and the frame cap were off.

The GS worker spent 15.24 ms of CPU time per world frame in the profiled
launch. It spent 28.8% of 2,650 stack samples reconstructing surfaces, with
15.2% in the resident terrain matcher and its callees. The background
surface builder also spent 22.0% of its samples matching and 19.5% in its
material observation routine. The profiling launch measured 61.2 world FPS.

## Changes

- In a native terrain cell with power-of-two determinants, the matcher uses
  the exact reciprocal to compute barycentric coordinates. It preserves
  traversal order, the tolerant barycentric bounds, and the plane test.
- The existing resident triangle index now locates a material projection
  through a 32-bit slot. Only triangles with observations occupy the
  projection array.
- Matched opaque draws skip per-vertex UV conversion when neither the cell
  nor its layer will replace a projection.

The matcher alone saved about 10% per lookup in a native-grid fixture.
The fixture compared 131,072 queries and found zero result differences.
Two Surdana screens of the matcher alone showed no clear frame-rate gain.

## Gameplay retest

The comparison alternated the original and optimized paths in four-second
intervals inside one game process. It reversed the order of adjacent pairs
to reduce the effect of changing host load. The harness counted completed
world frames in the middle 3.2 seconds of each interval and checked the
loaded scene, DLSS Quality resolutions, and 4K capture. The FPS runs had
sampling and GPU timers off. The diagnostic executable kept the two
projection stores synchronized during updates. The installed build uses
only the slot array.

| 4K view | Pairs | Original FPS (range) | Optimized FPS (range) | 99th-percentile frame time |
| --- | ---: | ---: | ---: | ---: |
| Surdana town square | 10 | 69.08 (57.21–74.73) | 74.05 (70.96–75.31) | 19.87 → 18.93 ms |
| Ruined Village arrival | 5 | 70.50 (70.35–70.69) | 70.74 (70.41–70.94) | 24.17 → 24.39 ms |
| Wartok bridge | 5 | 80.38 (79.66–81.11) | 80.28 (79.79–80.74) | 18.69 → 18.56 ms |
| Volcanic arrival | 5 | 70.72 (70.23–71.12) | 71.11 (70.73–71.26) | 14.87 → 14.64 ms |
| Rift World tunnel | 10 | 105.28 (82.94–117.65) | 103.82 (90.26–117.10) | 31.46 → 26.47 ms |
| Grotto after the turn and walk | 5 | 98.07 (97.46–98.50) | 97.97 (97.92–98.46) | 16.02 → 15.92 ms |

Surdana gained FPS in all ten pairs, with a 7.2% increase in the median.
The two launches measured 66.85 → 74.41 and 70.43 → 73.69 FPS. Ruined
Village, Wartok, Volcanic, and Grotto were at 98% median GPU utilization
and moved by less than 1%. Rift World had five faster and five slower
pairs, with a median paired gain of 1.06 FPS. Throughput varied across
its two launches, and the 99th-percentile frame time fell in both.

The final install repeated the Surdana profile. The GS worker used 12.14 ms
of CPU time per world frame, down from 15.24 ms in the baseline profile.
The surface builder used 7.98 ms, down from 10.06 ms. These figures are
from one instrumented launch per build. The final instruction sample was
too sparse for another detailed breakdown.

## Correctness

In separate verification launches, the original and optimized matchers
returned identical indices for 73,058,242 queries during the timed portions
of Surdana and Wartok. A projection fixture compared textures, exact UVs,
version numbers, and change lists through equal-area observations,
non-finite areas, retained snapshots, and level replacement.
The installed build passed the scene geometry, surface builder, and
ray-water test suites and completed the Surdana profile with hardware ray
tracing and DLSS Quality active. All 16 protected file hashes matched after
deployment and testing.

Raw captures, symbolized profile inputs, and comparison scripts are under
`build/dlss-worker-20261120/`.

## Parallel Linux builds

The first CI run failed while two native WebGPU executables staged licenses
into the same directory. A Linux stress test reproduced six failures in 180
parallel invocations. Serializing the post-build copies through a lock in the
build directory passed all 180 invocations with exact runtime and license
contents. The two Windows WebGPU test executables also built with the same
staging script.
