# Rift World frame rate after the terrain fix

The terrain fix removed recurring render-worker stalls. In separate profiled
replays of the same tunnel route with Dota 2 open, the older build recorded
1,722 timed ray passes in 2,123 world frames. The terrain fix recorded 1,756
timed ray passes in 1,938 world frames. Both builds temporarily show the
raster image while DXR prepares refreshed terrain. The fix made the ray
effects available on a larger share of the frames.

Timed DXR work per world frame rose from 3.72 ms to 4.17 ms. Mean CPU time in
the ray-scene submission fell from 0.58 ms to 0.50 ms. The DXR composition was
the largest measured DXR stage on the stutter-fixed build at 2.70 ms per timed
pass. The profiles identify higher GPU work per world frame as a contributor
to the throughput drop. The two profiles sampled median Dota 2 3D utilization
of 35% and 40%, so they cannot isolate the whole FPS difference.

The ray pass now uses instance masks to keep model geometry out of vertical
terrain-receiver searches. The upward search stops at the distance of a
closer hit below the receiver. Composition keeps direct lighting in registers
until the transport contribution is resolved. It preserves the intermediate
half-float rounding of the color target and stores the combined pixel once.
The direct pass and transport filter share the same raster position and
linearized color. The dry path evaluates the shadow crease calculation only
within the range where it influences visibility.
The animated terrain update stays on the existing incremental path.

In 11 matched Rift World frames, both shaders ran on identical ray and raster
inputs. All 40,550,400 RGBA16F pixels matched before DLSS. A wider workgroup
also matched the image, but its timed composition was slower, so the final
shader keeps the 8 × 8 mapping.

Across four paired windows with GPU timing enabled, the median paired savings
were 0.013 ms in composition and 0.046 ms in total DXR time. Dota 2's sampled
3D load was about 21% in these windows after it restarted. These timings
are separate from the gameplay benchmark.

## Gameplay benchmark

The same scripted Rift World tunnel route ran on the RTX 5080 at 4K DLSS
Quality with ray shadows, reflections and GI enabled. Modern Water, VSync
and the frame cap were off. With Dota 2 open, the same Release comparison
executable alternated the two shaders in five-second phases. The center
3.8 seconds of each phase were measured. The terrain fix was active in both
variants. Compilers and other Drakan sessions invalidated earlier attempts.
Seven pairs qualified in the final run, with 26.5 seconds per variant and
zero median difference in paired Dota 2 GPU utilization.

| Metric | Terrain fix | Terrain fix with shader changes |
| --- | ---: | ---: |
| Pooled world FPS | 99.14 | 98.87 |
| World-frame gaps over 33 ms | 0 | 0 |
| Presentation gaps over 33 ms | 0 | 0 |
| 99th-percentile presentation interval | 13.74 ms | 13.91 ms |

The median paired FPS change was −0.37%, ranging from −5.04% to +3.52%.
The test found no measurable change in average FPS. The separate shipping
build ran another 53.08 measured seconds at 99.24 FPS, with zero world or
presentation gaps over 33 ms and zero slow terrain refreshes. Its longest
presentation gap was 13.79 ms. It exited cleanly. Dota 2's median sampled
GPU use was 17%, compared with 37–39% in the earlier terrain-fix benchmark.
The earlier GPU load and FPS recovery at that load remain unverified.

Raw captures, timing logs and build evidence are in the ignored
`build/rift-fps-recovery-20261121/` directory.
