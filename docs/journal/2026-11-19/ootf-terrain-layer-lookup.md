# Terrain-layer lookup in Volcanic and Rift World

The [4K level survey](ootf-4k-level-survey.md) found the native terrain-layer lookup at `0x14b6b8` in 40.5% of Volcanic's main game thread samples and 35.9% of Rift World's. The callers under `0x149d10` check the layer of each terrain cell an actor overlaps. Adjacent-cell probes supplied more than 95% of the calls in both levels. An experiment that reused identical consecutive all-layer misses found no eligible calls. A second experiment based on a single overlapping rectangle stopped finding eligible cells after level load.

The fast path specializes the two actor callers that query an exact cell center with flags 5 (`0x14a05c` and `0x14a0a8`). It reads the live layer lists and checks their integer rectangles directly. For a covering cell, it uses the native triangle and layer-type rules and the guest floating-point helpers for height comparisons. It preserves authored rank order, the directed search cutoff, and the descending all-layer retry. The original routine handles other input shapes, unrecognized records, and resumed calls. The camera and save-restoration callers still use the original routine.

The fast path batches the original search's loop and height-helper cycle charges at its return. The next regular checkpoint can deliver pending guest work. A comparison against the native routine in Volcanic and Rift World checked 9,568,254 eligible queries with zero result differences. It included initial level loads, the normal Volcanic arrival, and the Rift World tunnel turn.

## Measurement

The installed release ran at 3840 × 2160 with Direct3D 11, automatic internal scale resolving to 8, HD textures, 16× anisotropy, and the frame rate uncapped. Vsync, DLSS, and Modern Water were off. The NVIDIA RTX 5080 and Ryzen 7 9800X3D ran the original and optimized paths in the same executable with process-local switches. The tests interleaved fresh boots with private cards and measured completed world frames in the same 5.6-second windows. The frame analysis skipped the first ten intervals around the opening screenshot. The recorded level and output scale were checked in every run.

| View | Original FPS, median (range) | Optimized FPS, median (range) | Gain | 99th-percentile frame time | Median GPU busy |
| --- | ---: | ---: | ---: | ---: | ---: |
| Volcanic entrance | 45.9 (42.6–47.5) | 83.1 (75.5–85.9) | 81% | 33.6 → 16.8 ms | 54.5% → 93.5% |
| Rift World tunnel turn | 62.5 (61.9–65.2) | 107.8 (104.6–133.6) | 72% | 31.5 → 14.8 ms | 40% → 69% |

Each result uses five valid runs per path. The Rift World series restarted after a baseline screenshot coincided with a presentation handoff timeout. That window had too few completed frames for the specified sample. Side-by-side captures show the same terrain and camera views. Animation phases differ across runs.

Separate CPU traces put the optimized lookup at 11.3% of main game thread samples in Volcanic and 12.7% in Rift World, inclusive. The original lookup accounted for 51.1% and 45.2%, respectively, in the survey traces. GPU utilization reached 93.5% in the optimized Volcanic series. The Rift World series retained more GPU headroom.

The installed build enables the fast path by default. With no lookup override, it recorded 83.7 FPS at the Volcanic entrance and 106.7 FPS in the Rift World turn. A copied Wartok cave save and a fresh Surdana start both completed gameplay smoke checks. Checksums of the player's save, settings, and campaign pack files matched the pre-test snapshot.

Scripts, frame logs, screenshots, and per-run results are in `build/ootf-layer-20261119/`.
