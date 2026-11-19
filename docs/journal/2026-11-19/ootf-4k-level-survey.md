# Order of the Flame campaign at 4K

The November 19 local build ran the installed campaign in Direct3D 11 at 3840 × 2160, with auto internal scale, HD textures, 16× anisotropy, uncapped world rate, and DLSS off. The tests used private cards. The level start redirected to the target campaign map, and the trace confirmed that the requested map was playable through each measured window.

The initial sweep measured a view at arrival, a turn in place, and a short forward-input segment. Each window was about 5.6 seconds. The renderer and game thread counters were enabled in this pass. FPS comes from completed world frames after excluding the first ten frame intervals following the opening screenshot. The NVIDIA driver supplied GPU utilization, sampled twice per second. These are sampled views rather than a complete playthrough.

| Campaign level | Arrival FPS | Turned FPS | Forward FPS | Notes |
| --- | ---: | ---: | ---: | --- |
| Grimstone Mines | 67.1 | 58.6 | 69.9 | The turned view faces the green crystals inside the cave. |
| Grotto | 59.6 | 70.2 | 49.1 | The GPU was at 99–100% in all three windows. |
| Islands | 142.3 | 148.0 | 204.5 | Also tested a separate flight pose. |
| Alwarren | 151.2 | 176.0 | 120.9 | |
| Rift Crystal | 25.6 | 97.6 | 170.5 | The arrival includes one-second presentation pauses. See the retest below. |
| Islands Night | 96.8 | 116.4 | 120.4 | |
| Succubus Lair | 106.0 | 119.4 | 105.9 | The arrival camera was occluded by geometry. The turn showed the corridor. The forward-input segment reached a cutscene. |
| Volcanic | 41.2 | 47.8 | 54.7 | GPU utilization was 39–52%. Forward input reached a cutscene. |
| Rift World | 70.2 | 59.0 | 62.0 | GPU utilization was 37–49%. |

The Ruined Village Book loaded and transitioned to Ruined Village before a comparable measurement window completed. The intro and multiplayer arenas are outside this gameplay sweep. The earlier [Wartok comparison](ootf-terrain-performance.md) and the [Ruined Village comparison](ootf-renderer-performance.md) cover those two main campaign levels.

## Additional views

| View | FPS | GPU utilization |
| --- | ---: | ---: |
| Grimstone Mines, snowy rim | 80.9 | 99% |
| Grotto, snowy ridge | 101.1 | 99% |
| Islands, flight over the sea | 99.6 | 38% |
| Rift World, tunnel and hall | 61.3 | 38% |

The lava chamber placement in Volcanic put Rynn in the lava under test invulnerability. It served as a rendering stress test. Throughput was 35.3 FPS at either internal scale. GPU utilization fell from 42.5% to 18% at scale 4. The normal arrival is a separate test.

## Follow-up tests

Retests at the installed settings used the frame audit with the renderer profiler off. In the resolution diagnostic, the output stayed at 3840 × 2160 and only the internal scale changed from auto (8) to 4.

| View | Installed settings FPS | Diagnostic scale 4 FPS | GPU utilization, installed → scale 4 |
| --- | ---: | ---: | ---: |
| Grotto, forward along the cave wall | 55.5 | 116.1 | 99% → 77% |
| Grimstone Mines, turned toward the green crystal | 56.6 | 59.6 | 86% → 31% |
| Volcanic, normal arrival | 42.6 | 42.1 | 49% → 22% |
| Rift World, turned in the tunnel | 65.8 | 67.6 | 42% → 16% |

Grotto responds strongly to reducing the pixel workload. A separate run at the original internal scale with anisotropic filtering disabled recorded 93.7 FPS in the same cave-wall segment, versus 55.5 FPS with the installed 16× setting. The captures show comparable views. Texture sampling accounts for a substantial part of the GPU cost. Grimstone's 99th-percentile frame time improved from 27.3 to 19.4 ms at scale 4, while world throughput stayed near 60 FPS. The original Grimstone profile attributed about 16.6 ms per world frame to the emulated main game thread.

In a separate run at the original graphics settings, presenting on frame submission yielded 54.8 FPS during the same Grimstone turn, with a 99th-percentile frame time of 22.6 ms. The turn still ran near the same average rate as the 56.6 FPS retest with the default presentation path.

The first Rift Crystal arrival logged several waits of about one second in Direct3D `Present` with median GPU utilization of 6%. A repeated arrival with screenshots reached 114.4 FPS. Two further replays omitted all screenshots. The first arrival windows ran at 125.3 and 136.2 FPS, and six windows across those runs ranged from 98.9 to 136.2 FPS. None of the screenshot-free windows had a frame interval over 100 ms. The first stall remains a transient presentation event with an unconfirmed trigger.

At Volcanic's normal entrance, the profiled game thread used 21.7 ms of CPU time per world frame at 41.2 FPS. In a separate stationary entrance trace, 40.5% of 2,874 samples on that thread landed directly in the native layer lookup at `0x14b6b8`. The function searches the possible terrain layers for a position. The dominant call chain ran through the per-frame object update at `0x140ba8`. At this spot the GPU had headroom, and time in `Present` was about 0.18 ms per call. Earlier work links this map's CPU load to its active rift movers. Which objects contribute most to the current lookup cost remains open.

The Rift World turn trace also sampled the game thread in the same native layer search, accounting for 35.9% of 2,762 samples. The wrapper hooks each accounted for under 1% of direct samples in both levels. The search itself is the shared hotspot. Reducing internal scale improved the 99th-percentile frame time from 30.6 to 18.4 ms in the separate Rift World pair, with similar average throughput.

Raw traces, screenshots, diagnostic recipes, and per-window results are under `build/ootf-4k-survey-20261119/`.
