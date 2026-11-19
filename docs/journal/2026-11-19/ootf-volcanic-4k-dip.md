# Volcanic's 4K frame rate dip

The [three-level retest](ootf-latest-4k-retest.md) measured a 2.0% drop at Volcanic's arrival view with the latest CPU and GPU changes enabled together. Switching them independently shows that the repeat-filter shader causes the drop in this view. Grouped vertex decoding offsets part of the loss.

## Four configurations at 4K

Twenty launches used the same installed release, campaign pack, and arrival view as the previous retest. The output was 3840 × 2160 in Direct3D 11 at automatic internal scale 8, with HD textures, hardware filtering, 16× anisotropy, and no frame cap. Each launch used a private save card. All four configurations ran once per block, with the order rotated across five blocks.

The test measured completed world frames during the final five seconds of each capture. It verified the loaded level, actual output scale, and both runtime flags. Windows reported 100% CPU utilization before each block. The baseline still varied by 1.3 FPS across runs.

| Grouped decoding | Repeat-filter shader | Median FPS (range) | Median p99 frame time | Median GPU utilization | Median GPU power |
| --- | --- | ---: | ---: | ---: | ---: |
| Off | Off | 90.59 (89.81–91.06) | 16.11 ms | 98% | 232 W |
| On | Off | 92.91 (92.34–93.38) | 15.93 ms | 98% | 230 W |
| Off | On | 84.29 (83.04–87.69) | 16.91 ms | 79% | 195 W |
| On | On | 88.19 (86.71–90.78) | 16.81 ms | 82% | 200 W |

Turning on the repeat-filter shader reduced FPS in all five blocks with either decoder. With grouped decoding enabled, the median of the paired losses was 4.5%, with individual losses of 1.9–6.9%. Grouped decoding improved FPS in all five blocks with either shader. With both changes enabled, the configuration median was 2.6% lower and the median paired loss was 3.2%.

The renderer logs its first repeat-filter draw once per launch. In seven of ten enabled launches that first draw occurred in Volcanic. In the other three it occurred in the intro. The disabled launches had no repeat-filter marker. The screenshots show the same arrival view for all four configurations.

## The renderer takes longer to submit draws

Two separate instrumented pairs at the same 4K settings kept grouped vertex decoding enabled. The repeat-filter shader raised the graphics worker's CPU time from 8.6 to 9.5 ms per completed world frame. The main game thread stayed near 10.4 ms and issued about 270 DMA chains per frame.

| Profiled measurement | General shader | Repeat-filter shader |
| --- | ---: | ---: |
| Renderer draw scope, elapsed time per present | 2.40 ms | 2.87 ms |
| Draw submission scope, elapsed time per present | 0.47 ms | 0.65 ms |
| Draw binding scope, elapsed time per present | 0.21 ms | 0.31 ms |
| Present call, elapsed time per present | 0.93 ms | 0.13 ms |

The submission and binding scopes are nested in the draw scope. Binding took longer at the same rate of about 12.65 state binds per draw. The graphics clock medians were within 8 MHz at scale 8. The profile measures several Direct3D calls together, so the cost of each call remains open.

Two more instrumented pairs kept 4K output and reduced internal scale to 4. GPU utilization was 32–41%. The repeat-filter shader still slowed both pairs, from 88.7 to 84.4 FPS and from 90.0 to 88.9 FPS. The graphics worker still spent about 1.2 ms more CPU time per completed frame with the repeat shader. These measurements point to CPU-side rendering overhead in this view.

Scripts, run logs, frame times, GPU samples, and screenshots are under `build/ootf-volcanic-dip-20261119/`.

## Volcanic-only fallback trial

The first candidate selected the general terrain shader in Volcanic only. All five 4K pairs verified the intended shader selection in one executable. The repeat path ran at 90.09 FPS at the median (89.40–94.62). The fallback ran at 89.87 FPS (88.26–91.49). The fallback gained 2.08 FPS in one pair and lost 0.23–3.12 FPS in the other four. Median GPU utilization rose from 85% to 98%.

The fallback was removed. The general shader traded the earlier CPU-side limit for GPU saturation in the follow-up. The next candidate targets redundant work when setting up each terrain draw.

## Terrain binding reuse trial

The terrain path bound its constant buffer, noise and normal texture views, and sampler on every draw. Those calls also invalidated the rest of the draw-state cache. The trial recorded the four bindings in the existing cache after render-target handling. A foreign context operation still invalidated them, and the repeat-filter shader used the same inputs.

In a separate 4K profile pair, tracked state binds fell from 12.65 to 2.73 per draw with the new path. The two measured windows each had about 148,300 draws per 240 presented frames. The scope change cuts repeat setup for the same draw volume.

| 4K view | Original bindings, median FPS (range) | Cached terrain bindings, median FPS (range) | Pairs |
| --- | ---: | ---: | ---: |
| Volcanic arrival | 92.02 (89.17–95.21) | 93.42 (85.49–94.77) | 5 |
| Ruined Village arrival | 85.09 (83.03–87.15) | 81.09 (80.45–81.72) | 2 |
| Rift World tunnel | 150.80 (150.32–151.28) | 139.04 (131.33–146.75) | 2 |

Volcanic's median 99th-percentile frame time improved from 16.68 to 15.78 ms. Two Volcanic pairs gained FPS and three lost FPS, so the 1.5% difference in medians is within the measured spread. Both screening pairs in each of the other two levels lost FPS.

A follow-up limited reuse to Volcanic. A native Volcanic-to-Rift World quickload switched the binding mode back as expected. Direct3D verified 11,447,050 cached state slots with zero mismatches. Both 4K Volcanic retest pairs lost FPS, from a median of 98.91 to 96.74. The binding-reuse candidate was removed.

The live comparisons, profiles, and captures are under `build/ootf-volcanic-fix-20261119/`.

## Terrain height lookup trial

The lookup candidate used the lowest and highest encoded corner heights to classify quads whose corners agree. Mixed quads kept the original PS2 interpolation and guest cycle charges. Volcanic and Rift World produced 10,747,902 comparisons against the native terrain lookup with zero differences.

In Volcanic's two completed 4K screening pairs, the original path ran at 99.21 and 87.92 FPS. The candidate ran at 88.35 and 85.00 FPS. Windows rejected the next control launch at its memory commit limit, and a retry reached gameplay but timed out before the benchmark finished. These samples establish no throughput gain for the candidate.

All three candidates were removed. The follow-up records the earlier repeat-filter regression and the workloads it exposed. A reliable 4K frame-rate fix remains open.
