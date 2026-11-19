# Ruined Village after the terrain-layer optimization

The [first renderer comparison](ootf-renderer-performance.md) established the roughly 46 FPS baseline in the Order of the Flame opening view. This retest used the same view, current campaign pack, and installed release at 3840 × 2160. In gameplay, automatic internal scale selected 8. The renderer used Direct3D 11, HD textures, and 16× anisotropy. Vsync, DLSS, and Modern Water were off. The tests ran on the Ryzen 7 9800X3D and RTX 5080, with one game and a private card per run.

| Diagnostic | World FPS | Median GPU busy |
| --- | ---: | ---: |
| Current installed settings | 69.4, 73.1 | 96%, 99% |
| Original terrain lookup | 74.7 | 99% |
| Original full shader | 45.4 | 99% |
| Internal scale 4, 4K output | 68.0 | 36% |
| Anisotropy 1×, internal scale 8 | 72.3 | 86% |
| Terrain batching enabled | 68.2 | 95% |
| DMA chain pool disabled | 69.9 | 90% |

These were individual screening runs except for the two installed-settings runs. Reverting the terrain shader reproduced the earlier FPS. Reverting the lookup stayed within the observed current performance range. Internal scale 4 left throughput around 68 FPS despite freeing GPU capacity. At scale 8 the GPU was almost fully occupied as well. The new limit includes work on the main game thread even when there is GPU headroom.

The 4K CPU sample covered 2,458 main game thread stacks. Terrain submission at `0x152330` appeared in 57.2%. Terrain chunk submission at `0x158148` appeared in 26.2%. Guest DMA chain gathering under `0x1f1420` appeared in 11.0%. Direct3D Present calls averaged about 0.16 ms with the short shader, and blocking readback took about 0.03 ms per frame. The previous terrain-layer lookup hotspot is from the separate Volcanic and Rift World workloads.

The direct DMA assembly prototype eliminated a copy by filling the buffer already owned by the worker. The four screening runs used about 446 chains per completed frame, and the prototype reused storage for most of those chains. The original ran at 66.5 and 70.7 FPS. Direct assembly ran at 70.4 and 70.6 FPS. This overlapped the original range and the earlier current-settings runs, so the prototype was dropped.

## Grotto single-projection snow shader

The [4K level survey](ootf-4k-level-survey.md) recorded a GPU limit in the Grotto cave. The snow flag made terrain draws run the full shader, which also compiles the loop and temporary arrays for side projections. The current settings use one UV projection in that cave. Direct3D 11 now selects a shorter shader for one-projection draws with snow. It shares the original snow, texture sampling, alpha, and blending calculations. Draws with side projection use the full shader.

Both compiled snow shader variants use 37 temporary registers and zero indexed arrays, down from 66 and four in the full shader. The renderer's WARP test compared the variants on perspective-modulated snow terrain with mipmapped textures, anisotropy, and alpha discard. Both blend modes produced identical pixels. The test also checked that the snow flag changed the rendered output.

Five alternating runs per shader measured the same forward cave-wall route. Median world FPS increased from 56.6 to 101.0, a 78% gain. Median per-run 99th-percentile frame time fell from 27.8 to 17.5 ms. Four of the original shader runs ranged from 56.3 to 57.2 FPS and one reached 30.3 FPS during a presentation stall. All five optimized runs ranged from 99.4 to 102.7 FPS. Median GPU utilization was 100% with the original shader and 99% with the optimized one. The first launch compiled the two additional D3D11 shaders in parallel, and subsequent runs used the disk cache.

The final installed build recorded 72.6 FPS in the Ruined Village opening view and 103.0 FPS on the Grotto cave-wall route with the shader enabled by default. A fixed snowy ridge view in Grotto retained the same snow coverage in both captures. A static mountain crop matched exactly at 98.4% of pixels, with a mean absolute channel difference of 0.0085 out of 255. The installed settings, save files, and campaign pack matched the previous checksums.

Evidence and replay scripts are in `build/ootf-ruined-retest-20261119/`.
