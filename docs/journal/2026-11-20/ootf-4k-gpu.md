# Order of the Flame GPU retest at 4K DLSS Quality

The starting point was the installed render-worker build at `aac5bb6a`.
All gameplay runs used Direct3D 11, an RTX 5080, and DLSS Quality from
2560 × 1440 to 3840 × 2160. Ray-traced shadows, reflections, and global
illumination were active. Modern Water, VSync, and the frame cap were off.

## Rift World

The [previous worker retest](dlss-render-worker.md) showed a 1.46 FPS
difference in the pooled medians. Five pairs sped up and five slowed down.
The median within-pair difference favored the new path by 1.06 FPS. The
99th-percentile frame time fell in both launches. Those results did not
establish a repeatable throughput regression.

The new tunnel profile reached 107.5 world FPS with 76% median GPU use.
The emulated game thread was typically using nearly a full core, around
8.7–9.2 ms per world in the later profile reports. A separate sample had
only 107 game-thread stacks, with guest rendering and terrain lookup
among the larger groups. It was too sparse for a detailed attribution.
Rift World's GPU headroom varied across launches. Its current profile
points to the game thread as the stronger limit in the tunnel.

## GPU profile

| OotF view | Median GPU use | DXR transport | DXR composition | Total DXR |
| --- | ---: | ---: | ---: | ---: |
| Rift World tunnel | 76.0% | 0.63 ms | 1.80 ms | 3.20 ms |
| Ruined Village arrival | 95.5% | 1.39 ms | 3.50 ms | 5.89 ms |
| Volcanic arrival | 97.0% | 1.21 ms | 3.03 ms | 5.14 ms |
| Wartok bridge | 97.0% | 0.89 ms | 2.95 ms | 5.09 ms |

These GPU timestamps are medians inside the gameplay capture, with
profiling enabled. The D3D11 guide interval also includes a wait for DXR.
The D3D12 timestamps measure DXR work directly.

## Initial GPU changes

The D3D11 guide texture can be the destination of the DXR result.
The interop code already copies the input to an independent shared
texture before it queues the result copy. Writing back to the guide
removes a second copy of the same 28.125 MiB color texture.

A second experiment skipped shader work for pixels without an opaque
receiver or water overlay and skipped tile reads with empty lighting keys.
The hardware fixtures matched across 7,831,552 output bytes. The combined
experiment stayed within the paired FPS range in Ruined Village
(69.20 → 69.04) and Volcanic (68.44 → 68.40). Ruined Village DXR
composition stayed near 3.5 ms. The shader shortcut was removed.

## Opaque terrain hits

DXR can accept hits on the resident land instance directly when every
triangle in it passes the existing shader filters unconditionally. The
renderer tracks that condition during its existing material updates.
The test fixtures confirmed active hardware flags on 13 passes and
matched all 7,831,552 output bytes. An in-place DXR color regression
test also matched the existing output byte for byte.

In paired Ruined Village diagnostics with both retained changes enabled,
DXR composition fell from 3.49 to 3.37 ms and total DXR time from 5.66 to
5.52 ms. The changes were then timed with GPU profiling off.

## Five-level retest

Every level ran five adjacent pairs of four-second windows in one process,
reversing the order in alternate pairs and dropping 0.4 seconds at each
boundary. The harness checked both path markers, the loaded level, DLSS
Quality initialization, and both 3840 × 2160 captures. FPS counts world
frames. GPU utilization is the median across the timed capture.

| View | Original FPS (range) | Optimized FPS (range) | Faster pairs | GPU use |
| --- | ---: | ---: | ---: | ---: |
| Rift World tunnel | 97.58 (92.19–101.13) | 97.02 (94.64–100.37) | 1 of 5 | 81.0% |
| Ruined Village arrival | 64.80 (61.04–68.93) | 66.06 (65.22–69.90) | 4 of 5 | 93.5% |
| Volcanic arrival | 69.12 (68.98–69.36) | 69.98 (69.60–70.20) | 5 of 5 | 97.0% |
| Wartok bridge | 60.04 (57.25–65.93) | 62.97 (58.37–65.50) | 3 of 5 | 79.5% |
| Grotto after the turn and walk | 96.65 (90.71–97.64) | 98.11 (92.14–99.12) | 4 of 5 | 98.0% |

Volcanic had the smallest variation and improved in every pair. Ruined
Village and Grotto had higher medians and improved in four pairs each.
Wartok had a large spread, with pair differences from −7.56 to +8.25 FPS.
Rift World's median shift was about half a frame per second within its
window spread. Neither scene established a consistent throughput gain.

The paired Rift World diagnostics showed total DXR time at 3.20 → 2.87 ms
and composition at 1.80 → 1.61 ms. The GPU gains did not consistently
change the world-frame rate. The earlier game-thread saturation and
the repeat measurements explain why the tunnel is a poor measure of
this GPU change.

The installed build completed Rift World and Volcanic with DLSS Quality
active and the hardware opaque flag recorded throughout the timed views.
It matched the original hardware readbacks and passed the ray-water and
lost-signal tests. All 16 protected save, configuration, and mod hashes
matched after the retest.

Logs, recipes, profile data, hardware readbacks, and captures are under
`build/ootf-gpu-20261120/`.
