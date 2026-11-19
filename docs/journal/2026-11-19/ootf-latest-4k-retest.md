# Wartok, Volcanic, and Rift World retest at 4K

This retest isolates the [repeat-sampling shader](ootf-ruined-gpu-filter.md) and [grouped vertex decoder](ootf-ruined-cpu.md). The existing terrain-layer search and memory dispatch optimizations ran on both sides.

The installed release was built at `2599a140`. Runs used the same campaign pack, Direct3D 11, 3840 × 2160 output, automatic internal scale 8, HD textures, hardware filtering, and 16× anisotropy. Vsync, DLSS, Modern Water, and the frame cap were off. Each of the three views had five alternating pairs with private cards. The benchmark counted completed world frames during the five seconds before its final screenshot and checked the loaded level and output scale.

Separate diagnostic runs used CPU sampling and renderer timers at the installed scale and at scale 4. Both diagnostics kept 4K output. The scale 4 runs use a process-local override to reduce GPU work.

## Gameplay results

The Wartok view faces the bridge. Volcanic uses the arrival view, and Rift World turns toward the tunnel near the start. The median compares the five runs on each side of the same camera recipe.

| View | Baseline world FPS (range) | Latest world FPS (range) | Change | 99th-percentile frame time | GPU utilization |
| --- | ---: | ---: | ---: | ---: | ---: |
| Wartok bridge | 94.60 (92.80–97.43) | 98.94 (95.40–106.27) | +4.6% | 14.88 → 15.21 ms | 75% → 84% |
| Volcanic arrival | 89.16 (88.05–90.30) | 87.38 (82.52–88.89) | −2.0% | 16.44 → 16.80 ms | 98% → 83% |
| Rift World tunnel | 117.63 (114.17–118.23) | 125.36 (119.15–128.85) | +6.6% | 14.27 → 12.90 ms | 76.5% → 61.5% |

Wartok gained FPS in all five pairs. The repeat-filter usage marker fired during the intro in one launch and remained silent in the other four. That points to the grouped decoder as the main contributor in the bridge view. Volcanic used repeat filtering in all five launches. Its GPU utilization fell 15 percentage points, and it lost FPS in four of five pairs. Rift World gained FPS in every pair and had shorter slow frames. It logged repeat-filter use after loading Rift World in four launches, and first logged it during the intro in the other launch.

## Remaining limits

The diagnostics below use one launch at each internal scale per scene. CPU samples and renderer timers add overhead. The scale comparison holds output at 3840 × 2160 and changes how much work the renderer does internally.

| View | Profiled FPS at scale 8 → 4 | GPU utilization at scale 8 → 4 | Main game / GS / VU1 worker CPU time per frame at scale 8 |
| --- | ---: | ---: | ---: |
| Wartok bridge | 102.0 → 95.3 | 88% → 33% | 8.9 / 7.1 / 8.4 ms |
| Volcanic arrival | 88.7 → 86.2 | 91.5% → 38% | 10.7 / 8.8 / 6.8 ms |
| Rift World tunnel | 123.4 → 140.6 | 59.5% → 27.5% | 7.6 / 4.1 / 4.8 ms |

The CPU threads overlap. Their times are medians across the final four report intervals and do not add up to the wall time of a frame.

- Wartok. Reducing GPU load did not raise FPS. The game thread and VU1 worker each used over 8 ms per frame. Terrain drawing and its callees made up 35% of the main thread stack samples at scale 4. The scale 8 stack captures were too sparse for a reliable breakdown.
- Volcanic. The main thread used about 10.5–10.7 ms per frame at both scales. Terrain drawing accounted for 36–38% of its samples, including its callees. Terrain surface classification alone accounted for 9–12%. That main-thread cost persists with GPU utilization down to 38%. The profiles identify the throughput limit and leave the exact cause of the small A/B slowdown open.
- Rift World. Reducing internal scale raised FPS in the diagnostic run, showing a cost tied to rendering load despite modest average GPU utilization. The main thread still spent 25–28% of its sampled time in terrain drawing and its callees. It also polled the graphics FINISH flag about 244,000 times per second at scale 8 and 147,000 at scale 4. That is a sign of synchronization traffic. This comparison changed internal scale and did not isolate the cost of those polls.

All 30 captures loaded their expected levels. The installed executable and the 16 protected save, settings, and campaign files matched their pre-test hashes. Local scripts, captures, frame times, GPU samples, and symbolized profiles are under `build/ootf-latest-4k-20261119/`.
