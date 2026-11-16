# CPU use and jitter at the owner's settings (2026-11-16)

The owner reports very high CPU use and jitter. Measured with the shipped `dist/DrakanPC/drakan.exe` (built 16 Nov
03:40) and a copy of the owner's `drakan.ini` (borderless 3840x2160, scale auto = 8, vsync 0, frame_limit 0,
world_rate uncapped, DLSS off, HD textures, texture_upscale auto). Quiet machine (no other game, replay tool or
compiler before or during any run), Ryzen 7 9800X3D (8 cores, 16 threads), RTX 5080, Dell U2720Q on the RTX 5080 over
DisplayPort at 59.997 Hz. Evidence: `build/review/perf-cpu-jitter/` (untracked).

## 1. The owner's last session

`dist/DrakanPC/drakan.log`, 11,449 `[frame-uncapped]` windows of 2 s (about 6.4 hours): mean 233 flips/s; 6,199
windows at 240 to 259, 2,662 at 220 to 239, 1,503 at 200 to 219. The game drew about four frames per refresh of a
60 Hz panel. With vsync off the swap chain presents with `DXGI_PRESENT_ALLOW_TEARING` (`d3d11_present.cpp`), so each
refresh shows about four torn slices whose positions move, and `tick_snap` (whole refreshes per world step) only acts
with vsync on.

A cross-adapter present was suspected from WMI (it lists the 4K mode under the AMD iGPU) and ruled out by `dxdiag`:
both monitors are on the RTX 5080.

## 2. Switches at those settings (Surdana start, `scene_ab_bench.py --foreground`, 3 interleaved repeats)

CPU is ms per world frame (EE thread, VU1 worker, GS worker, every other thread of the process together).

| variant | world fps | frame ms | 1 % low ms | 0.1 % low ms | EE | VU1 | GS | all other |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| owner's settings | 116.9 (115.6 to 117.7) | 8.48 | 10.5 | 13.4 | 5.94 | 6.38 | 6.07 | 25.38 |
| `DRAKAN_FRAME_LIMIT=60` | 60.0 | 16.67 | 16.9 | 19.6 | 6.00 | 6.72 | 6.53 | 26.39 |
| `DRAKAN_VSYNC=1` | 60.0 | 16.67 | 16.9 | 20.2 | 5.72 | 5.99 | 5.96 | 22.51 |
| frame queue (`PS2X_FRAME_QUEUE=1`, `PS2X_READBACK=late`) | 108.3 (100.0 to 110.7) | 9.28 | 12.9 | 16.8 | 5.85 | 6.36 | 6.93 | 28.12 |
| `PS2X_VU_SPIN=0` | 115.4 | 8.51 | 11.2 | 14.8 | 6.14 | 6.63 | 6.51 | 26.17 |

* CPU per world frame is the same in every variant, so process CPU scales with the frame rate: about 3.7 logical
  processors at 117 fps (`ee_thread_cpu` about 70 %, `other_cpu` 270 to 310 %), about half of that at 60. The owner's
  session ran at about twice this scene's rate.
* Under vsync or a 60 cap the 1 % low equals the refresh (16.9 ms against 16.67); the uncapped frame time varies
  between 8.4 and 13 ms inside a window, every frame lands on the panel at a different phase.
* The frame queue is slower here (+7.5 % frame time in 3 of 3 pairs). FINISH polls were 0 in this scene: the
  per-frame read-back spin does not happen at the Surdana start, so it is not the cause there.
* `PS2X_VU_SPIN=0` changes nothing measurable.
* The foreground share of the game window was 0 to 100 % between runs (the harness could not always activate it);
  the pairs were consistent anyway.

## 3. Where the CPU goes (`PS2X_HOSTPROF=1 PS2X_HOSTPROF_BUSY=1`, one run)

Per-second thread CPU of a typical window: GS worker 74 %, VU1 worker 74 %, EE thread 77 %, an unnamed thread 64 %,
a second unnamed thread 35 to 65 % (not seen by the sampler), Direct3D driver threads (`nvwgf2umx.dll`) 10 to 17 %.
The first unnamed thread runs in `drakan.exe` around RVA 0x54c000 to 0x558000 when not parked. No PDB or linker map
of this exe exists (the build tree was removed), so neither unnamed thread is named yet; it needs a rebuild with a
map file.

## 4. GPU vertex path at these settings

`DRAKAN_VERTEX_PROCESSING=gpu` (strip records only, `stages 0x01`) against `cpu`, 3 interleaved repeats:

| variant | world fps | frame ms | 1 % low ms | EE | VU1 | GS | all other | GPU world ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| cpu | 100.1 (96.8 to 116.5) | 9.70 | 15.0 | 7.63 | 8.17 | 8.38 | 31.92 | 1.05 |
| gpu | 33.8 (32.8 to 34.3) | 29.21 | 34.9 | 8.00 | 5.46 | 29.08 | 54.90 | 4.70 |

The VU1 worker saves 2.7 ms per frame, the GS worker pays 20.7 ms and is saturated (96 %), and the GPU world time
grows 4.5 times. The 13 November tuning note measured +0.9 to 2.3 ms on the GS worker for the same path in Surdana
at a 1280x960 window; at 3840x2160 and scale 8 the cost is an order of magnitude larger. `vertex_processing = gpu` is
a regression at the owner's settings; `cpu` (the shipped default) stays.

## 5. Open

* Name the two unnamed busy threads (rebuild with a map file, then `hostprof.csv` RVAs).
* Profile the GS worker under `gpu` at scale 8 (`GSR_PROFILE`: `programBounds`, `programUpload`, `drawSubmit`) to
  find what grows with the internal scale.
* Whether the shipped defaults should pace to the display (vsync on, or a frame limit at the refresh) is the
  owner's decision.
