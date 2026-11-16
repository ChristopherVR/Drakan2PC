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

The VU1 worker saves 2.7 ms per frame and the GS worker pays 20.7 ms (96 % busy). The cause is not the shader: with
`DRAKAN_VERTEX_LOG=1` every record fell back with reason `feature` (shader vertices 0, CPU 42,467,136), and
`GSR_PROFILE` shows `programEvaluate` taking 1.2 to 1.5 s of every 2 s on the GS worker. The owner's `modern_water = 1`
sets `BackendState::rayEffects` (the surface reconstruction un-projects every draw's triangles on the CPU), so the
renderer refuses each record and evaluates it with the CPU reference, while the VU1 side keeps handing the runs over.
The internal scale is not involved (section 5).

Fix: the backends publish `program_draw::cpuReadsVertices` of their settings (`vertexReaders`, written in
`applyLive`, `initTerrain` and `programTakes`), and `vertexPathCapability` offers no stage while it is set
(`Capability::cpuVertexReaders`), so `drakan_vertex_path` keeps the runs in the microprogram within 250 ms and the
`[vertex-path]` line names the reason. Direct3D 11 and WebGPU.

## 5. Settings bisect (Surdana, owner's settings with one change, 2 interleaved repeats)

| variant | world fps | frame ms | 1 % low ms | EE | VU1 | GS | process CPU |
| --- | --- | --- | --- | --- | --- | --- | --- |
| owner's settings | 101.6 (91.1 to 112.2) | 9.59 | 14.6 | 7.45 | 7.84 | 8.12 | about 390 % |
| `GSR_MODERN_WATER=0` | 132.9 (123.7 to 142.2) | 7.44 | 9.4 | 5.03 | 7.21 | 5.84 | about 270 % |
| water off and `DRAKAN_VERTEX_PROCESSING=gpu` | 152.1 (149.5 to 154.8) | 6.44 | 8.6 | 5.32 | 5.10 | 6.06 | not sampled |
| `GSR_TEXUP=1` | 107.8 | 8.99 | 13.6 | 7.23 | 7.66 | 7.90 | about 400 % |
| `GSR_TEXREPLACE=0` | 103.8 | 9.41 | 14.3 | 7.19 | 7.62 | 8.18 | about 390 % |
| `PS2X_GS_SCALE=5` | 103.1 | 9.29 | 14.9 | 7.65 | 7.92 | 8.53 | about 350 % |

* The two unnamed busy threads of section 3 (about 60 % and 50 % of a processor) are gone with modern water off: they
  are the modern water work. Modern water costs about 1.1 processors and 21 % frame time in this scene (2 of 2
  pairs faster without it).
* With water off the GPU vertex path works (142,718,586 shader vertices, 37,777 on the CPU, fallbacks only `not
  ready` 251) and pays at 3840x2160 and scale 8: frame time -13 % against water off alone, VU1 worker 7.21 to 5.10 ms.
  The 1 % low improves, the longest frame does not (11 and 28 ms in the two runs).
* HD textures, texture upscaling and scale 5 against 8 change nothing beyond the run-to-run range.

## 6. Open

* Make the GPU vertex path and modern water work together: the surface reconstruction needs the transformed
  triangles, which the shader path has only on the GPU (stream output or a compute pass that writes them for the
  surface builder).
* Whether the shipped defaults should pace to the display (vsync on, or a frame limit at the refresh) is the
  owner's decision; `tearing is the jitter` is inferred, not measured on the panel.
* Only the Surdana start was measured; the owner's session ran at about twice its rate, and no scene with the
  per-frame read-back (FINISH polls) was benchmarked.
