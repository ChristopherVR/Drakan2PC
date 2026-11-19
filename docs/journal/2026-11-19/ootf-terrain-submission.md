# Terrain preparation and submission in Order of the Flame

The [Ruined Village trace](ootf-ruined-village-retest.md) put the terrain draw in 57.2% of main game thread stacks, including its callees. Chunk submission appeared in 26.2% and DMA chain submission in 11.0%. A separate diagnostic counted about 63 million scratchpad accesses and 1.6 million peripheral accesses in a five-second gameplay interval. Terrain commands use the scratchpad while being built and peripheral registers when submitting transfers.

The memory dispatcher now reuses the already identified scratchpad range to compute its offset. A naturally aligned 32-bit access to the canonical peripheral range goes straight to the existing register handler. Peripheral aliases still go through the general translator. The path preserves scratchpad bytes, alignment checks, register side effects, and DMA completion handling.

## Memory dispatcher in isolation

The runs used the same installed campaign pack, Direct3D 11 at 3840 × 2160, automatic internal scale of 8, HD textures, and 16× anisotropy. Vsync, DLSS, and Modern Water were off. World rendering was uncapped. Each pair used the same release executable and private cards, with the dispatch path toggled before launch. The benchmark alternated pair order and counted completed world frames over the five seconds before the terminal screenshot, after loading and settling in each scene.

| View | Original FPS, median (range) | Direct dispatch FPS, median (range) | Median FPS change | 99th-percentile frame time | GPU utilization |
| --- | ---: | ---: | ---: | ---: | ---: |
| Ruined Village opening | 74.2 (70.8–77.9) | 75.1 (73.3–76.6) | +1.1% | 17.0 → 17.3 ms | 99% → 99% |
| Wartok Canyons bridge | 81.6 (80.1–86.2) | 92.8 (87.9–93.6) | +13.7% | 17.0 → 15.3 ms | 68% → 75% |
| Rift World tunnel turn | 111.4 (103.7–116.5) | 114.7 (110.3–135.1) | +3.0% | 15.4 → 14.1 ms | 65% → 73% |

Each result uses five runs per path. Wartok improved in all five pairs, with each optimized run faster than the highest original run. The GPU had capacity to render the added frames. Ruined Village stayed near full GPU use and the medians fell within the run spread. Rift World recorded three faster pairs and two slightly slower pairs. Its higher median fell within the variation between runs. Side-by-side 4K screenshots matched the three terrain views. Characters, vegetation, water, and fire continued to animate between captures.

The VIF1 worker profile put motion-boundary detection and command checks at about 1% of worker samples. A prototype reused the existing boundary check to skip the intervening command calls. Its single screening pair fell within the Ruined Village run spread.

## Direct access from the runtime

Terrain also issues many aligned 32-bit and 128-bit loads and stores. The runtime can carry those scratchpad accesses out directly once it has handled tracing, address diagnostics, and the special flip readiness reads. Stores still reach the same DMA completion handling. Other addresses and misaligned operations use the existing memory dispatcher.

One CPU-instrumented pair per view isolated this shortcut with the direct dispatcher active on both sides. The main game thread's median CPU time per completed frame fell from 10.60 to 9.10 ms in Wartok and from 9.57 to 8.02 ms in Rift World. Rift World submitted about 191 DMA chains per frame in both runs. The combined comparison uses separate runs without CPU sampling.

## Both shortcuts at the same 4K settings

The original path and the two shortcuts ran in the same executable. Alternating fresh launches produced five pairs per scene. The two shortcuts were either both disabled or both active.

| View | Original FPS, median (range) | Optimized FPS, median (range) | Median FPS change | 99th-percentile frame time | GPU utilization |
| --- | ---: | ---: | ---: | ---: | ---: |
| Ruined Village opening | 76.4 (70.3–77.2) | 78.2 (76.1–79.5) | +2.3% | 17.4 → 16.7 ms | 99% → 99% |
| Wartok Canyons bridge | 82.4 (80.3–87.6) | 96.2 (93.8–97.3) | +16.7% | 16.5 → 15.0 ms | 72% → 81% |
| Rift World tunnel turn | 103.8 (94.4–111.9) | 115.6 (114.5–142.1) | +11.3% | 15.9 → 14.5 ms | 61% → 75% |

Wartok and Rift World improved in all five pairs. Their optimized runs submitted frames to the GPU more quickly and GPU utilization rose. The Ruined Village median rose by 1.8 FPS, within the run spread at 99% GPU utilization. Rift World also had an occasional long frame in both modes. All valid runs contributed to the results.

## Installed build

Both shortcuts are on by default. `PS2X_TERRAIN_BUS_FAST=0` restores the general path for comparison. A separate launch of the installed build in each scene confirmed both shortcuts were active. Those runs recorded 75.7 FPS in Ruined Village, 97.1 FPS in Wartok, and 126.9 FPS in Rift World.

Matched captures showed the same terrain views with both shortcuts active. The installed build completed a walk and turn from a copy of the mounted Wartok cave save and a fresh Surdana start. Six terrain and motion tests passed. Checksums for all 16 protected save, settings, and campaign pack files matched the pre-test snapshot.

Scripts, run logs, frame timings, and screenshots are in `build/ootf-terrain-submit-20261119/`.
