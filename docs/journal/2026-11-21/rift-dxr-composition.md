# Rift World DXR composition

## Shader change

DXR uses a smaller composition shader when the transport pass is active
and the water-coverage flags make a water hit impossible. The shader
keeps the original pixel reconstruction, opaque shadow queries, 5 × 5
transport filter and RGBA16 color stores. The original composition
shader still handles water receivers and coverage diagnostics. The
renderer also uses it if the specialized pipeline fails to initialize.

The baseline cached-composition bytecode in the comparison build
matched the previous shader byte for byte. DXC produced 105,724 bytes
for the specialized shader and 350,024 bytes for the original.

## Benchmark method

Rift World tunnel after the scripted turn, with D3D11, DLSS Quality
from 2560 × 1440 to 3840 × 2160, ray-traced shadows, reflections and
global illumination active, and Modern Water, VSync and the frame cap
off. The host was a Ryzen 7 9800X3D and RTX 5080 running Dota 2 throughout.

The same Release executable alternated the two shaders in five-second
phases. Adjacent pairs reversed the order. Frame counts and intervals
come from completed world frames in the center 3.8 seconds of each
phase. A process monitor excluded phases near compiler or other game
activity. A separate sampler recorded Dota 2's utilization of its 3D
GPU engine. GPU timestamps and the same-frame image checks use separate
launches from the gameplay comparison.

## Gameplay with Dota 2 open

Eleven pairs across two launches produced approximately 42 seconds of
measured gameplay per shader. A compiler overlapped one additional pair,
which was excluded. Total GPU utilization was 99% and 98% at the median
in the two launches. Dota 2's median 3D-engine utilization differed by
zero percentage points within the median pair.

| Rift World tunnel | Original | Specialized |
| --- | ---: | ---: |
| Pooled world FPS | 61.89 | 63.93 |
| Median window FPS and range | 64.48 (50.95–67.09) | 66.72 (54.05–68.98) |
| Pooled 99th-percentile frame interval | 36.43 ms | 34.83 ms |
| Pooled 99.9th-percentile frame interval | 45.71 ms | 45.68 ms |
| Frame intervals over 33.33 ms | 36 | 37 |

The specialized shader was faster in eight of eleven pairs. The median
paired improvement was 4.15%, with individual pairs ranging from
−7.58% to +11.49%. The pooled rate rose 3.30%. The 99th-percentile
interval fell in seven pairs, and the rarest long intervals were similar.

## DXR timestamps

Three separate timestamped pairs showed a lower composition time in
every pair. The median paired saving was 0.43 ms (11.1%). The median
composition window went from 3.91 ms to 3.20 ms, with ranges of
3.46–3.97 ms and 3.14–3.48 ms. The median total DXR pass went from
6.27 ms to 5.55 ms and improved in two of the three pairs.

## Image verification

A separate Rift World run dispatched both shaders on each of 11
sampled frames with identical inputs and the same ray scene. The
2560 × 1440 RGBA16 output consumed by DLSS matched bit for bit for
all 40,550,400 pixels. The run also captured the 3840 × 2160 output
with DLSS Quality active. The hardware fixture snapshots matched
all 7,831,552 bytes across the original and specialized paths.

The final Windows Release build passed the ray-water and lost-signal
tests. Its Rift World smoke run selected the specialized shader for
all 746 recorded 2560 × 1440 DXR passes in the 4K DLSS Quality window
and exited cleanly.

The gameplay harness verified the expected level, native DXR pipeline,
DLSS Quality input and output dimensions, shader eligibility and clean
renderer shutdown. Raw logs, comparison scripts, checksums and sample
images are in the ignored `build/rift-dxr-compose-20261121/` directory.
