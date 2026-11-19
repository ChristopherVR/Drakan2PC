# Ruined Village GPU filtering at 4K

The [terrain submission retest](ootf-terrain-submission.md) put the Order of the Flame opening view around 78 FPS after the CPU shortcuts. This investigation keeps the view and campaign pack fixed. The installed settings use Direct3D 11, 3840 × 2160 output, automatic internal scale 8, HD textures, hardware filtering, and 16× anisotropy. Vsync, DLSS, Modern Water, and the frame cap are off.

An uninstrumented run of the installed build measured 75.3 world FPS at 99% median GPU utilization. Two diagnostic runs with the same output size and internal scale 4 measured 76.5 and 76.2 FPS at 39.5% utilization. A separate 1× anisotropy diagnostic at scale 8 measured 78.8 FPS at 95.5% utilization. These were process-only overrides. The lower-scale runs show that the scene can reach a similar frame-rate ceiling even with GPU capacity available.

The 4K CPU diagnostic spent about 12 ms of main game thread time per completed frame in steady gameplay. This includes polling for graphics work. The per-draw terrain timer reported 2.6 ms across about 341 sampled draws per frame. Its 1,024-slot query ring missed about 521 draws per frame, so the figure covers only the timed draws. The frame-wide `overlay` interval includes the present boundary and other work.

## Repeat sampling

The terrain audit classified about 980 draws per world in its final window. Nearly all used repeat wrapping on both axes, and the side projection count was zero. The Direct3D 11 shader for eligible single-projection terrain now specializes for repeat wrapping when hardware filtering is enabled. Point filtering uses the equivalent texel mask for repeat. The bilinear path uses the same sampler and coordinates. The shaders share their mip selection, 16× anisotropy, alpha, and blend calculations.

FXC produced identical bytecode for both control blend variants. The repeat variants used 16 temporary registers instead of 35. Static instruction slots fell from 2,363 to 455 for hardware blending and from 2,411 to 503 for software blending. These are shader compilation counts.

The Direct3D 11 WARP fixture passed 408 draws and 23,614 channel checks. The added comparison covered both blend paths, point and bilinear filtering, mixed filter selection, repeating negative coordinates, 1×/4×/8× texture copies, and the coarsest mip. It checked that point and bilinear filtering produced different test pixels, that the coarsest mip drew its own color, and that switching shader variants produced identical pixels for each mode.

## Live comparison

Five pairs alternated the original and specialized paths in the same release executable. The game booted from private cards and rendered the same opening pose at the installed settings. The test counted completed world frames during the last five seconds before a screenshot. No CPU or per-draw GPU profiling ran during those windows.

| Measurement | Original | Repeat sampling |
| --- | ---: | ---: |
| Median world FPS (range) | 74.0 (73.5–78.1) | 79.4 (76.8–81.6) |
| Median GPU utilization (range) | 99% (99–99%) | 89% (82–96%) |
| Median 99th-percentile frame time | 17.53 ms | 17.57 ms |

Throughput rose 7.3% and median GPU utilization fell by 10 percentage points. All five pairs favored the specialization on both measures. Dividing each run's sampled GPU utilization by its throughput gives an estimated 13.38 → 11.58 ms of GPU busy time per world frame. This estimate uses whole-device utilization. The lower-scale diagnostic and the remaining GPU headroom with the specialized shader point to a CPU ceiling for further gains in this view.

The closest-matched pair of gameplay captures had a mean absolute channel difference of 0.026 out of 255 across selected roof, wall, and ground crops. The characters and fire continued to animate across launches. A separate Wartok bridge screening pair measured 94.1 and 95.3 FPS. Its snowy terrain kept the snow shader.

## Installed build

The shader is enabled by default for eligible draws. `GSR_TERRAIN_REPEAT_FILTER=0` restores the general sampler. `GSR_SIMPLE_PIXEL_SHADER=0` retains its full-shader comparison behavior. The installed build confirmed repeat filtering in use and measured 84.0 FPS in one separate 4K launch.

Five renderer tests passed, including the revised pixel fixture. The installed build played the mounted Wartok cave route from a private save copy and started a fresh Surdana game. Checksums for all 16 protected save, settings, and campaign pack files matched the pre-test snapshot.

Run logs, frame timings, and screenshots are in `build/ruined-gpu-20261119/`.
