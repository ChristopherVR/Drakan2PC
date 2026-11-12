# DLSS Quality slower than DLSS off: measured, causes, fix (2026-11-12)

Question from the owner: Direct3D 11 with `dlss = quality` ran slower than with DLSS off, and in some runs fell to 5 to
8 fps inside `Present`. An upscaler that renders fewer pixels should be faster. Scratch data (runs, logs, pictures,
tools) is under `build/review/dlss-speed/` and is not tracked.

## Answer

- **It was true on a quiet machine.** Surdana start, 3840x2160 borderless, the owner's ini, no other game or replay
  tool running: DLSS off 174.0 world fps, quality 101.8 (58 % of off), balanced 111.0, performance 108.0, DLAA 75.1.
  (An earlier pair at 14:45 with one other game running: off 136.3, quality 72.7.)
- **Cause: the upscaler worked for the square internal target, not for the window.** At 3840x2160 the automatic
  internal scale is 8, a 4096x4096 target for the 512x512 frame, of which the window shows 3840x2160. DLSS wrote all
  16.8 megapixels (the window has 8.3) from a 2730x2730 render rectangle (quality mode of the window is 2560x1440),
  then rebuilt output-sized colour, depth and motion guides that only the removed neural path read, and wrote colour
  and alpha back in two more full-target passes. DLSS off renders the same 4096x4096 target directly, and this game's
  scenes are so cheap to rasterise (0.3 to 1.5 ms of GPU time per world at 16.8 megapixels) that there was nothing
  for the upscaler to save; it only added work.
- **Fix (commit 1fa0fc3e):** quality, balanced and performance now write the window's pixels (3840x2160) and render
  the mode's ratio of that (quality 2560x1440); the dead guide rebuild is gone; one write-back pass instead of
  three; the presenter shows the upscaler's own pixels one to one wherever the HUD left them alone. DLAA is unchanged
  on purpose.
- **After, same machine state, same runs:** quality 155.0 world fps in Surdana (from 101.8, +52 %), 126.8 in the
  Northern Tundra view (from 85.6; DLSS off makes 111.1 there, so quality is now 14 % faster than off), 117.8 in the
  Inquisitor's Lair arena (from 81.7; off 124.3), 61.3 in the Order of the Flame Ruined Village (from 50.4; off
  85.6).
- **Quality is still slower than off in three of the four scenes,** by 12 % (Surdana), 5 % (arena) and 28 % (Ruined
  Village). There the frame is not limited by GPU work: with DLSS off the timed GPU passes take 0.6 to 1.8 ms of a 5.6 to
  11.7 ms frame. What DLSS adds there is CPU work per world (object motion on the GS worker, scene capture and
  motion scopes on the game's main thread) and about 2.2 ms of GPU work; no upscaler setting can win that back.
  Where the raster is heavier (the tundra view) quality, balanced and performance are now faster than off.
- **The 5 to 8 fps collapse is GPU memory oversubscription, reproduced.** With 11 to 22 other programs on the GPU
  the adapter's dedicated memory was full (15.2 to 15.7 of 16 GiB, 7 to 25 GiB more in shared memory), the game had
  1.3 to 2.2 GiB of its own GPU memory moved to shared memory and `Present` took 150 to 560 ms per call. DLSS on
  needs more GPU memory than off, so it falls over first; the old pipeline needed about 0.35 GiB more than the new one.

## Conditions

- Ryzen 7 9800X3D, RTX 5080 (driver 617.14), one 3840x2160 60 Hz display, Windows 11.
- Owner's `drakan.ini` unchanged for the 3840x2160 runs (copy of `build/review/baseline-play`, only `elf =` points
  at a private copy): borderless 3840x2160, `scale = auto` (8), `aspect = auto`, `fov = 99`, HD textures,
  `texture_upscale = auto`, anisotropic 16, `mip_bias = auto`, `sharp_present = 1`, `vsync = 0`, `frame_limit = 0`,
  `world_rate = uncapped`, `tick_snap = 1` (inactive without vsync), foliage wind on, ray effects and neural
  rendering off, no `dlss_sharpness` (0), no `dlss_preset` (the SDK's default model for each mode).
- 2560x1440 runs: the same ini with `window_mode = windowed`, `fullscreen = 0`, `window_width = 2560`,
  `window_height = 1440` (`build/review/dlss-speed/ini/owner-1440.ini`); the log shows internal scale 5.
- Differences from the owner's play in every run (the harness, `scripts/harness/play_run.py`): audio output off, an
  empty memory card with New Game, god mode, the window is never activated and stays behind other windows
  (`DRAKAN_NO_ACTIVATE=1`), `PS2X_PERF=1`, `DRAKAN_FRAME_AUDIT=2`, `GSR_PROFILE=2`, `GSR_GPU_PASS_TIMING=1`.
- Method (`build/review/dlss-speed/tools/cycle_bench.py`): one game run per scene and pipeline. At a fixed pose the
  recipe switches the DLSS mode through the live settings path (`setting video.dlss <mode>`, not written to the
  ini), waits 3 s, measures 6 s between two screenshots, and goes on to the next mode; two rounds, the order
  rotated in the second (off, dlaa, quality, balanced, performance; then dlaa ... off). So the five modes are
  interleaved inside one process at one pose. "old" is the same exe with `GSR_UPSCALE_WINDOW_OUTPUT=0`, which runs
  the previous pipeline (target-sized output, guide rebuild, two write-back passes); "new" is the default. Old and
  new are separate runs, two minutes apart.
- Figures: world fps = world frames in the window / its length (`[ft] F` lines); frame ms = median and 99th
  percentile of the intervals between world frames; thread CPU = CPU time of the named thread per world frame from
  the one-second `[perf]` lines (EE = the game's main thread); `Present()` = wall time inside `IDXGISwapChain::Present`
  per call (`[gsr-profile] presentCall`); GPU ms = `[gpu-pass]` (below). Each cell: median of the two rounds, the two
  values in brackets.
- Scenes (`scripts/perf/cpu_scene_bench.py` names): `surdana` = Level 1A start; `tundra-c` = Northern Tundra spot C
  (`place 751616 140546 1224542`), the heavy outdoor view; `arena-1e` = Inquisitor's Lair arena spot
  (`place 579166 128750 440175`), the interior; `ootf-ruined` = Order of the Flame campaign start (Ruined Village).
- Load: the quiet window was arranged through the coordinator (16:24 to 16:47). Every run below lists the other
  `drakan.exe`, replay tools and compilers counted before it and the peak during it (ours included in the peak).

## GPU time per pass: how it is measured

`GSR_GPU_PASS_TIMING=1` (new: `gpu_pass_stats.h`, `d3d11_gpu_passes.h`, `gsr_test_gpu_pass_stats`): a Direct3D 11
timestamp query at every pass boundary, read a few frames later without waiting, summed over 120 presented frames and
divided by the worlds begun in them. `GSR_UPSCALE_TIMING`, the old switch, timed `TemporalPipeline::resolve`, which no
longer runs (`upActive()` is false): that is why it printed nothing usable.

What the columns mean, and their limits:

- `world`: from the engine's world begin to the display latch (scene raster and the HUD that follows it). Commands
  reach the GPU in bursts, so this is close to GPU work, but any wait for the CPU inside it counts too.
- `worldEntry`: depth and backdrop shrunk into the render rectangle. `guides`: colour, depth and camera motion
  prepared at render size. `modelMotion`: posed-model motion raster. `evaluate`: the DLSS evaluate call.
  `convert`: output-sized guide rebuild (old pipeline only). `compose`: write-back of colour, alpha and depth.
  `display`: display circuits into the display image, plus the new window-sized image. `presentBlit`: the blit into
  the window.
- `overlay`: starts at the host overlay and runs through `Present` to the next world begin. It is mostly the GPU
  waiting for the next frame's commands, so it is not work; it is listed because it is where a wait shows.
- **A caveat on `evaluate`:** the timestamps bracket what the Direct3D 11 queue sees. In the modes that write
  4096x4096, `Present` blocks 3 to 13 ms per call although the timed passes add up to far less. Either part of the
  upscaler's work is not seen by these timestamps, or presentation itself is paced (see "Not established"). The
  frame times in the tables are the measure of the gain, not the `evaluate` column.

## Where the time went (old pipeline, quality, Surdana, 3840x2160, quiet)

Per world, old against new:

| | old | new |
| --- | --- | --- |
| Target, upscaler output, render rectangle | 4096x4096, 4096x4096, 2730x2730 | 4096x4096, 3840x2160, 2560x1440 |
| Pixels rasterised, pixels the upscaler writes | 7.45 M, 16.78 M | 3.69 M, 8.29 M |
| World frame interval, median | 9.99 ms | 6.11 ms |
| `Present()` wait per call (120 and 189 presents per second) | 3.87 ms | 0.55 ms |
| GPU `evaluate` | 2.60 ms | 1.35 ms |
| GPU `convert` (guide rebuild at 4096x4096: RGBA16F, R32F, RG16F) | 0.40 ms | none |
| GPU `compose` (old: RGB pass, alpha pass, depth restore; new: one pass, depth restore) | 0.59 ms | 0.33 ms |
| GPU `guides` | 0.18 ms | 0.07 ms |
| GPU `world` (raster and HUD) | 0.60 ms | 0.31 ms |
| GPU `worldEntry` | 0.25 ms | 0.26 ms |
| GPU `display` + `presentBlit` | 0.32 ms | 0.45 ms |
| EE thread CPU | 7.58 ms | 5.74 ms |
| GS worker CPU | 5.15 ms | 5.13 ms |

- With DLSS off the same scene has a 5.6 ms world interval, 0.26 to 0.35 ms of `world` GPU time, 5.1 to 5.3 ms of EE
  CPU and 4.2 ms of GS worker CPU per world: the frame is a chain of CPU threads and the GPU idles.
- The old gap to off was 4.4 ms per world (9.99 against 5.62 ms). The fix removed 3.9 ms of it (to 6.11 ms). Of
  the GPU work the timestamps see, 2.2 ms per world went: evaluate 1.25 (half the output pixels), convert 0.40
  (gone), compose 0.26, guides 0.11, raster 0.29, with 0.13 added in display and blit. The rest of the 3.9 ms shows
  as `Present` no longer waiting (3.87 ms per call before, 0.55 after); what it waited for is not established.
- The remaining 0.5 ms to off (6.11 against 5.59 ms) goes with CPU work: 0.9 ms more GS worker time and 0.4 ms more
  time on the game's main thread per world than with DLSS off.
- The EE thread's 7.58 ms in the old pipeline is mostly waiting: the game polls GS FINISH before its per-frame read
  (perf-cpu.md, 3.1), so a renderer that is behind shows up as EE CPU time. It falls to 5.74 ms with the new one.
- Checked and not found: no `Map` for reading, no `Flush` and no query readback in the DLSS path per frame
  (`d3d11_world.cpp`, `sr_combined.cpp`, `nr_guides.cpp`, `dlss_ngx.cpp`; the only `GetData` calls use
  `DONOTFLUSH`); the motion readback in `validateObjectMotion` only runs with `runtimeFlags().motionValidate`. Plain
  Super Resolution uses the Direct3D 11 NGX entry points (`NGX_D3D11_CREATE_DLSS_EXT`, `NGX_D3D11_EVALUATE_DLSS_EXT`)
  on the game's own device: no Direct3D 12 interop, shared textures or fences (those belonged to the removed neural
  bridge). The upscaler feature is created once per mode and size: a new log line, `[dlss-sr] upscaler feature
  created`, appears once per mode switch in every run and never per frame. No mip generation runs on world targets.
- NVIDIA's published cost: the DLSS programming guide's table of execution times is, from memory, about 0.7 ms for
  the RTX 4080 class at a 3840x2160 output in performance mode with the older CNN model; I did not have the document
  at hand and did not download it, and it has no figure for the transformer model the 310.x runtime selects by
  default. The measured 1.35 ms at 3840x2160 and 2.6 ms at 4096x4096 (twice the pixels) are in line with that order
  of magnitude; nothing points at a format conversion or a readback inside the evaluate.

## What changed

1. **Sizes** (`upscale::plan`, `upscale_render_size.h`, tests in `gsr_test_upscale_render_size`): the upscaler
   writes the window pixels the frame covers when that is a whole number on both axes and not above the target;
   the render rectangle is the mode's ratio of that. At 3840x2160: quality 2560x1440, balanced 2226x1252,
   performance 1920x1080, all to 3840x2160. At 2560x1440 (scale 5): quality 1706x960 to 2560x1440. DLAA: the whole
   target in and out, as before.
2. **Texture LOD bias** (`upscale::lodBias`): `log2(render / output)` of the plan, -0.585 for quality. The earlier
   formula `log2(render px per native px / scale)` gave the same -0.585 for the old sizes and would have given
   -0.678 for the new ones; the bias is the NVIDIA guide's value for the real output on both axes.
3. **No guide rebuild** (`sr_combined::Inputs::guides = false`): the output-sized colour, depth, motion and neural
   targets are neither written nor allocated: 384 MiB less GPU memory at 4096x4096 (two RGBA16F, one R32F, one
   RG16F), and the upscaler's own output target shrinks from 128 to 63 MiB.
4. **One write-back pass** (`sr_combined::Pipeline::composeTarget`): the finished picture is made at output size
   (RGBA8, with the optional sharpening) and written into the target unfiltered with the raster's alpha.
5. **Presentation** (`renderViewImage`, `PSPresentView`): the display image is built at window size at the latch;
   a window pixel whose footprint in the target still equals the write-back shows the upscaler's pixel one to one,
   anything else is the area average as before. So the picture is not resampled twice, and the HUD is untouched.
   `build/review/dlss-speed/runs/f1/s1.png` (`GSR_UPSCALE_VIEW_DEBUG=1`) shows the whole world tinted (direct) and the
   HUD and the FPS counter not.
6. The per-axis render rule of the morning (`GSR_UPSCALE_VIEW_RENDER`) is replaced by the plan. Its picture
   difference (mean absolute difference 3.0) came with an upscaler that still wrote 4096x4096 from 2560x1440, with
   ratios of 1.6 and 2.84 on the two axes; the plan has 1.5 on both.

Policy is in backend-neutral headers with unit tests; `d3d11_*` only calls it. The edits in files of the running
backend refactor are one line in `d3d11_draw.cpp`, one in `d3d11_backend.cpp`, members in `d3d11_backend_impl.h`, one
shader in `gs_shaders.h` and its creation in `d3d11_device.cpp`.

## Measurements, old against new, quiet machine

### surdana, 4k

- old: before the run (other drakan.exe, replay tools, compilers, GPU %) [0, 0, 0, 0], waited 0 s, peak during (drakan.exe with ours, replay tools, compilers) [1, 0, 0], exit 0
- new: before the run (other drakan.exe, replay tools, compilers, GPU %) [0, 0, 0, 3], waited 1170 s, peak during (drakan.exe with ours, replay tools, compilers) [1, 0, 0], exit 0

| mode | pipeline | world fps | frame ms median | frame ms 99 % | EE cpu ms/world | GSWorker cpu ms/world | VU1Worker cpu ms/world | Present() ms per present | presents/s |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| off | old switch (same code path) | 174.0 (173.2, 174.7) | 5.62 (5.57, 5.68) | 7.5 (7.4, 7.5) | 5.14 (5.05, 5.22) | 4.22 (4.07, 4.37) | 5.28 (5.24, 5.33) | 0.06 (0.06, 0.06) | 228 (226, 230) |
| off | new | 175.6 (174.9, 176.3) | 5.59 (5.57, 5.61) | 7.3 (7.2, 7.4) | 5.30 (5.23, 5.37) | 4.22 (3.98, 4.47) | 5.33 (5.23, 5.44) | 0.05 (0.05, 0.05) | 230 (228, 232) |
| dlaa | old | 75.1 (74.9, 75.3) | 13.38 (13.17, 13.60) | 20.4 (20.2, 20.7) | 9.10 (8.85, 9.36) | 4.91 (4.74, 5.08) | 5.44 (5.41, 5.48) | 7.38 (7.36, 7.40) | 120 (120, 120) |
| dlaa | new | 79.6 (79.5, 79.6) | 12.14 (12.12, 12.16) | 20.5 (20.2, 20.8) | 9.32 (9.21, 9.42) | 5.29 (5.19, 5.39) | 5.27 (5.11, 5.42) | 6.63 (6.60, 6.67) | 120 (120, 120) |
| quality | old | 101.8 (101.5, 102.2) | 9.99 (9.85, 10.14) | 15.1 (15.0, 15.1) | 7.58 (7.55, 7.62) | 5.15 (4.99, 5.30) | 5.37 (5.36, 5.38) | 3.87 (3.87, 3.87) | 120 (120, 120) |
| quality | new | 155.0 (154.8, 155.1) | 6.11 (6.06, 6.15) | 9.1 (8.9, 9.2) | 5.74 (5.60, 5.88) | 5.13 (5.12, 5.14) | 5.46 (5.45, 5.47) | 0.55 (0.54, 0.55) | 189 (189, 190) |
| balanced | old | 111.0 (110.8, 111.1) | 8.94 (8.92, 8.96) | 13.6 (13.3, 13.9) | 7.36 (7.32, 7.39) | 5.20 (5.09, 5.30) | 5.48 (5.43, 5.53) | 3.16 (3.13, 3.18) | 121 (120, 122) |
| balanced | new | 161.2 (160.8, 161.7) | 6.03 (6.02, 6.04) | 8.2 (8.1, 8.3) | 5.69 (5.63, 5.74) | 4.95 (4.70, 5.21) | 5.26 (5.20, 5.32) | 0.05 (0.05, 0.05) | 203 (202, 203) |
| performance | old | 108.0 (107.3, 108.7) | 8.93 (8.89, 8.98) | 14.4 (14.3, 14.5) | 7.57 (7.53, 7.61) | 5.19 (5.13, 5.24) | 5.42 (5.29, 5.56) | 3.34 (3.33, 3.36) | 121 (120, 121) |
| performance | new | 161.5 (161.2, 161.8) | 6.03 (6.02, 6.03) | 8.2 (8.1, 8.3) | 5.66 (5.64, 5.69) | 5.03 (5.02, 5.04) | 5.43 (5.38, 5.48) | 0.05 (0.05, 0.05) | 204 (203, 204) |

GPU ms per world (median of the rounds; a pass missing from a mode is empty):

| mode | pipeline | world | worldEntry | guides | modelMotion | evaluate | convert | compose | afterWorld | display | presentBlit | overlay | frame | total |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| off | old | 0.35 |  |  |  |  |  |  |  | 0.08 | 0.24 | 5.08 |  | 5.75 |
| off | new | 0.26 |  |  |  |  |  |  |  | 0.08 | 0.24 | 5.11 |  | 5.69 |
| dlaa | old | 0.96 | 0.00 | 0.47 | 0.03 | 2.77 | 0.49 | 0.41 |  | 0.09 | 0.23 | 7.88 |  | 13.34 |
| dlaa | new | 1.31 | 0.00 | 0.42 | 0.03 | 2.69 | 0.00 | 0.32 |  | 0.09 | 0.23 | 7.43 |  | 12.52 |
| quality | old | 0.60 | 0.25 | 0.18 | 0.03 | 2.60 | 0.40 | 0.59 |  | 0.09 | 0.23 | 4.86 |  | 9.83 |
| quality | new | 0.31 | 0.26 | 0.07 | 0.04 | 1.35 | 0.00 | 0.33 |  | 0.34 | 0.11 | 3.64 |  | 6.46 |
| balanced | old | 0.42 | 0.23 | 0.13 | 0.03 | 2.59 | 0.40 | 0.57 |  | 0.09 | 0.23 | 4.32 |  | 9.01 |
| balanced | new | 0.29 | 0.26 | 0.05 | 0.04 | 1.33 | 0.00 | 0.33 |  | 0.35 | 0.12 | 3.47 |  | 6.23 |
| performance | old | 0.43 | 0.22 | 0.09 | 0.03 | 3.52 | 0.34 | 0.57 |  | 0.09 | 0.23 | 3.74 |  | 9.25 |
| performance | new | 0.30 | 0.25 | 0.03 | 0.04 | 1.74 | 0.00 | 0.30 |  | 0.35 | 0.11 | 3.05 |  | 6.18 |

### tundra-c, 4k

- old: before the run (other drakan.exe, replay tools, compilers, GPU %) [0, 0, 0, 1], waited 0 s, peak during (drakan.exe with ours, replay tools, compilers) [1, 0, 0], exit 0
- new: before the run (other drakan.exe, replay tools, compilers, GPU %) [0, 0, 0, 3], waited 0 s, peak during (drakan.exe with ours, replay tools, compilers) [1, 0, 0], exit 0

| mode | pipeline | world fps | frame ms median | frame ms 99 % | EE cpu ms/world | GSWorker cpu ms/world | VU1Worker cpu ms/world | Present() ms per present | presents/s |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| off | old switch (same code path) | 110.0 (109.0, 111.0) | 9.10 (8.99, 9.21) | 9.5 (9.4, 9.6) | 5.56 (5.55, 5.57) | 4.45 (4.43, 4.47) | 5.61 (5.41, 5.81) | 4.48 (4.43, 4.52) | 120 (120, 120) |
| off | new | 111.1 (110.2, 112.0) | 8.99 (8.93, 9.05) | 9.5 (9.4, 9.7) | 5.56 (5.38, 5.75) | 4.48 (4.35, 4.61) | 5.44 (5.31, 5.57) | 4.33 (4.21, 4.45) | 120 (120, 120) |
| dlaa | old | 56.8 (56.7, 56.9) | 17.55 (17.53, 17.57) | 18.2 (18.0, 18.3) | 6.21 (5.96, 6.47) | 6.16 (6.09, 6.23) | 5.92 (5.87, 5.98) | 11.16 (11.15, 11.17) | 114 (114, 114) |
| dlaa | new | 58.8 (58.6, 59.0) | 16.98 (16.93, 17.04) | 17.9 (17.8, 18.0) | 6.54 (6.32, 6.75) | 6.23 (6.21, 6.25) | 5.65 (5.47, 5.84) | 10.55 (10.45, 10.65) | 117 (115, 118) |
| quality | old | 85.6 (85.6, 85.7) | 11.65 (11.65, 11.65) | 12.1 (12.1, 12.2) | 6.02 (5.59, 6.45) | 5.80 (5.68, 5.92) | 5.62 (5.43, 5.80) | 5.47 (5.46, 5.47) | 120 (120, 120) |
| quality | new | 126.8 (126.4, 127.2) | 7.86 (7.84, 7.87) | 10.1 (9.6, 10.5) | 6.27 (5.79, 6.74) | 6.00 (5.59, 6.41) | 5.64 (5.20, 6.08) | 1.56 (1.45, 1.67) | 135 (134, 135) |
| balanced | old | 95.4 (95.4, 95.5) | 10.44 (10.44, 10.44) | 11.0 (10.9, 11.0) | 6.24 (5.99, 6.48) | 5.83 (5.70, 5.96) | 5.78 (5.76, 5.80) | 4.27 (4.26, 4.28) | 120 (120, 121) |
| balanced | new | 138.1 (137.3, 139.0) | 7.22 (7.16, 7.27) | 9.7 (9.6, 9.8) | 6.15 (6.06, 6.25) | 5.92 (5.89, 5.94) | 5.56 (5.51, 5.61) | 1.06 (1.01, 1.10) | 156 (153, 158) |
| performance | old | 97.1 (97.0, 97.1) | 10.27 (10.27, 10.27) | 10.8 (10.7, 10.8) | 6.40 (6.31, 6.48) | 5.96 (5.90, 6.02) | 5.71 (5.69, 5.74) | 4.05 (4.03, 4.07) | 120 (120, 120) |
| performance | new | 140.2 (139.2, 141.2) | 7.07 (7.07, 7.08) | 9.7 (9.6, 9.8) | 6.06 (5.92, 6.20) | 5.88 (5.87, 5.88) | 5.63 (5.53, 5.74) | 0.94 (0.93, 0.95) | 160 (159, 162) |

GPU ms per world (median of the rounds; a pass missing from a mode is empty):

| mode | pipeline | world | worldEntry | guides | modelMotion | evaluate | convert | compose | afterWorld | display | presentBlit | overlay | frame | total |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| off | old | 1.53 |  |  |  |  |  |  |  | 0.08 | 0.23 | 7.25 |  | 9.09 |
| off | new | 1.41 |  |  |  |  |  |  |  | 0.08 | 0.23 | 7.29 |  | 9.00 |
| dlaa | old | 1.28 | 0.00 | 0.37 | 0.05 | 2.56 | 0.39 | 0.32 |  | 0.08 | 0.23 | 12.34 |  | 17.61 |
| dlaa | new | 1.43 | 0.00 | 0.37 | 0.05 | 2.66 | 0.00 | 0.26 |  | 0.08 | 0.23 | 11.99 |  | 17.06 |
| quality | old | 1.09 | 0.19 | 0.16 | 0.05 | 2.45 | 0.31 | 0.47 |  | 0.08 | 0.23 | 6.64 |  | 11.67 |
| quality | new | 0.95 | 0.17 | 0.07 | 0.05 | 1.37 | 0.00 | 0.32 |  | 0.33 | 0.11 | 4.54 |  | 7.90 |
| balanced | old | 0.94 | 0.18 | 0.12 | 0.05 | 2.43 | 0.31 | 0.47 |  | 0.08 | 0.23 | 5.67 |  | 10.48 |
| balanced | new | 1.05 | 0.17 | 0.05 | 0.04 | 1.36 | 0.00 | 0.31 |  | 0.33 | 0.11 | 3.82 |  | 7.25 |
| performance | old | 0.81 | 0.16 | 0.08 | 0.05 | 3.28 | 0.25 | 0.46 |  | 0.08 | 0.23 | 4.88 |  | 10.28 |
| performance | new | 1.00 | 0.16 | 0.03 | 0.04 | 1.77 | 0.00 | 0.29 |  | 0.33 | 0.11 | 3.40 |  | 7.15 |

### arena-1e, 4k

- old: before the run (other drakan.exe, replay tools, compilers, GPU %) [0, 0, 0, 0], waited 0 s, peak during (drakan.exe with ours, replay tools, compilers) [1, 0, 0], exit 0
- new: before the run (other drakan.exe, replay tools, compilers, GPU %) [0, 0, 0, 0], waited 0 s, peak during (drakan.exe with ours, replay tools, compilers) [1, 0, 0], exit 0

| mode | pipeline | world fps | frame ms median | frame ms 99 % | EE cpu ms/world | GSWorker cpu ms/world | VU1Worker cpu ms/world | Present() ms per present | presents/s |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| off | old switch (same code path) | 124.0 (124.0, 124.1) | 8.01 (7.99, 8.03) | 11.7 (11.6, 11.9) | 7.41 (7.19, 7.63) | 5.44 (5.17, 5.71) | 6.39 (5.83, 6.96) | 2.27 (2.27, 2.28) | 128 (128, 128) |
| off | new | 124.3 (124.2, 124.4) | 7.95 (7.93, 7.96) | 11.8 (11.8, 11.8) | 7.19 (7.18, 7.20) | 5.47 (4.99, 5.95) | 5.96 (5.64, 6.28) | 2.27 (2.25, 2.28) | 128 (128, 128) |
| dlaa | old | 56.3 (56.2, 56.4) | 17.61 (17.58, 17.64) | 26.1 (26.1, 26.2) | 13.04 (13.03, 13.05) | 7.28 (7.26, 7.30) | 5.69 (5.26, 6.12) | 9.79 (9.76, 9.81) | 112 (112, 113) |
| dlaa | new | 57.9 (57.9, 57.9) | 17.22 (17.19, 17.25) | 26.1 (26.1, 26.2) | 12.70 (12.47, 12.93) | 7.22 (7.14, 7.30) | 6.26 (5.82, 6.71) | 9.27 (9.15, 9.39) | 116 (116, 116) |
| quality | old | 81.7 (81.7, 81.8) | 12.55 (12.51, 12.59) | 20.1 (19.8, 20.3) | 10.51 (10.45, 10.58) | 7.17 (7.09, 7.25) | 6.03 (6.01, 6.05) | 4.43 (4.41, 4.44) | 120 (120, 120) |
| quality | new | 117.8 (117.7, 118.0) | 8.42 (8.42, 8.43) | 11.9 (11.8, 12.0) | 7.97 (7.91, 8.03) | 7.15 (7.11, 7.19) | 5.89 (5.87, 5.91) | 0.90 (0.88, 0.91) | 120 (120, 121) |
| balanced | old | 90.3 (89.9, 90.7) | 11.08 (11.03, 11.13) | 16.3 (16.3, 16.4) | 9.71 (9.68, 9.74) | 7.41 (7.41, 7.41) | 5.83 (5.61, 6.04) | 3.37 (3.35, 3.40) | 120 (120, 120) |
| balanced | new | 127.8 (127.8, 127.9) | 7.72 (7.70, 7.73) | 10.7 (10.7, 10.7) | 7.48 (7.41, 7.55) | 7.23 (6.97, 7.49) | 6.11 (5.94, 6.28) | 0.28 (0.28, 0.29) | 136 (136, 136) |
| performance | old | 91.0 (90.8, 91.3) | 11.19 (11.04, 11.35) | 16.4 (16.4, 16.5) | 9.68 (9.59, 9.77) | 7.44 (7.43, 7.46) | 6.08 (5.97, 6.20) | 3.30 (3.27, 3.32) | 120 (120, 120) |
| performance | new | 129.4 (129.3, 129.5) | 7.57 (7.55, 7.59) | 10.6 (10.4, 10.9) | 7.30 (7.25, 7.35) | 7.06 (7.04, 7.08) | 5.93 (5.77, 6.10) | 0.11 (0.07, 0.14) | 139 (139, 139) |

GPU ms per world (median of the rounds; a pass missing from a mode is empty):

| mode | pipeline | world | worldEntry | guides | modelMotion | evaluate | convert | compose | afterWorld | display | presentBlit | overlay | frame | total |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| off | old | 1.48 |  |  |  |  |  |  |  | 0.07 | 0.23 | 6.28 |  | 8.07 |
| off | new | 1.41 |  |  |  |  |  |  |  | 0.07 | 0.23 | 6.35 |  | 8.06 |
| dlaa | old | 2.48 | 0.00 | 0.40 | 0.05 | 2.55 | 0.40 | 0.30 |  | 0.07 | 0.23 | 11.29 |  | 17.77 |
| dlaa | new | 2.21 | 0.00 | 0.41 | 0.05 | 2.55 | 0.00 | 0.25 |  | 0.08 | 0.23 | 11.51 |  | 17.31 |
| quality | old | 2.01 | 0.21 | 0.18 | 0.04 | 2.48 | 0.34 | 0.45 |  | 0.08 | 0.23 | 6.23 |  | 12.25 |
| quality | new | 0.94 | 0.17 | 0.07 | 0.05 | 1.32 | 0.00 | 0.31 |  | 0.33 | 0.11 | 5.20 |  | 8.51 |
| balanced | old | 1.03 | 0.19 | 0.13 | 0.04 | 2.47 | 0.32 | 0.45 |  | 0.08 | 0.23 | 6.15 |  | 11.08 |
| balanced | new | 0.86 | 0.17 | 0.05 | 0.05 | 1.31 | 0.00 | 0.30 |  | 0.33 | 0.11 | 4.66 |  | 7.84 |
| performance | old | 1.01 | 0.18 | 0.09 | 0.04 | 3.37 | 0.28 | 0.44 |  | 0.08 | 0.23 | 5.30 |  | 11.02 |
| performance | new | 0.94 | 0.16 | 0.03 | 0.05 | 1.74 | 0.00 | 0.28 |  | 0.33 | 0.11 | 4.11 |  | 7.75 |

### ootf-ruined, 4k

- old: before the run (other drakan.exe, replay tools, compilers, GPU %) [0, 0, 0, 0], waited 0 s, peak during (drakan.exe with ours, replay tools, compilers) [1, 0, 0], exit 0
- new: before the run (other drakan.exe, replay tools, compilers, GPU %) [0, 0, 0, 0], waited 0 s, peak during (drakan.exe with ours, replay tools, compilers) [1, 0, 0], exit 0

| mode | pipeline | world fps | frame ms median | frame ms 99 % | EE cpu ms/world | GSWorker cpu ms/world | VU1Worker cpu ms/world | Present() ms per present | presents/s |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| off | old switch (same code path) | 88.9 (88.6, 89.1) | 11.15 (11.14, 11.15) | 16.7 (16.7, 16.7) | 9.85 (9.78, 9.91) | 6.49 (6.39, 6.60) | 5.18 (5.00, 5.35) | 4.26 (4.26, 4.27) | 120 (120, 120) |
| off | new | 85.6 (85.5, 85.6) | 11.70 (11.68, 11.72) | 17.3 (16.8, 17.9) | 10.25 (10.21, 10.28) | 6.88 (6.73, 7.04) | 5.25 (5.12, 5.38) | 4.61 (4.58, 4.63) | 120 (120, 120) |
| dlaa | old | 36.9 (36.6, 37.1) | 29.30 (28.70, 29.89) | 41.5 (41.0, 42.0) | 18.65 (18.36, 18.93) | 7.78 (7.26, 8.31) | 5.04 (4.97, 5.11) | 12.90 (12.73, 13.07) | 74 (73, 74) |
| dlaa | new | 36.8 (36.8, 36.9) | 29.87 (29.80, 29.93) | 41.7 (41.3, 42.0) | 19.32 (18.99, 19.66) | 7.49 (7.05, 7.92) | 5.49 (5.32, 5.67) | 12.67 (12.62, 12.71) | 73 (73, 73) |
| quality | old | 50.4 (50.4, 50.4) | 18.90 (18.85, 18.96) | 31.6 (31.5, 31.6) | 15.54 (15.29, 15.79) | 8.08 (7.83, 8.32) | 5.38 (5.05, 5.71) | 8.53 (8.47, 8.58) | 101 (101, 101) |
| quality | new | 61.3 (61.3, 61.4) | 16.64 (16.63, 16.65) | 27.1 (27.0, 27.2) | 13.90 (13.55, 14.25) | 7.94 (7.92, 7.96) | 5.41 (5.15, 5.67) | 5.37 (5.36, 5.38) | 120 (120, 120) |
| balanced | old | 55.0 (55.0, 55.1) | 17.34 (17.27, 17.42) | 29.4 (29.4, 29.5) | 14.74 (14.62, 14.86) | 7.78 (7.72, 7.84) | 5.31 (5.26, 5.36) | 7.51 (7.48, 7.54) | 110 (110, 110) |
| balanced | new | 65.5 (65.5, 65.6) | 15.68 (15.64, 15.72) | 25.1 (25.0, 25.1) | 13.17 (13.00, 13.34) | 7.72 (7.62, 7.81) | 5.38 (5.37, 5.38) | 4.84 (4.83, 4.84) | 120 (120, 120) |
| performance | old | 56.5 (56.4, 56.6) | 17.16 (17.16, 17.17) | 28.3 (28.2, 28.3) | 14.19 (14.08, 14.31) | 7.88 (7.86, 7.90) | 4.93 (4.44, 5.42) | 7.56 (7.53, 7.60) | 112 (112, 113) |
| performance | new | 67.5 (67.5, 67.6) | 15.18 (15.16, 15.21) | 24.7 (24.6, 24.8) | 12.85 (12.74, 12.97) | 7.82 (7.76, 7.87) | 5.59 (5.57, 5.62) | 4.79 (4.70, 4.88) | 120 (120, 121) |

GPU ms per world (median of the rounds; a pass missing from a mode is empty):

| mode | pipeline | world | worldEntry | guides | modelMotion | evaluate | convert | compose | afterWorld | display | presentBlit | overlay | frame | total |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| off | old | 0.89 |  |  |  |  |  |  |  | 0.07 | 0.23 | 10.09 |  | 11.26 |
| off | new | 1.47 |  |  |  |  |  |  |  | 0.06 | 0.23 | 9.95 |  | 11.71 |
| dlaa | old | 1.28 | 0.00 | 0.33 | 0.02 | 2.55 | 0.36 | 0.27 |  | 0.07 | 0.22 | 22.14 |  | 27.24 |
| dlaa | new | 0.81 | 0.00 | 0.32 | 0.02 | 2.51 | 0.00 | 0.22 |  | 0.07 | 0.23 | 23.03 |  | 27.20 |
| quality | old | 1.41 | 0.19 | 0.14 | 0.02 | 2.44 | 0.33 | 0.44 |  | 0.07 | 0.23 | 14.54 |  | 19.81 |
| quality | new | 1.62 | 0.16 | 0.07 | 0.02 | 1.33 | 0.00 | 0.31 |  | 0.32 | 0.11 | 12.39 |  | 16.32 |
| balanced | old | 1.52 | 0.18 | 0.10 | 0.02 | 2.42 | 0.31 | 0.43 |  | 0.07 | 0.23 | 13.01 |  | 18.29 |
| balanced | new | 1.04 | 0.16 | 0.04 | 0.02 | 1.30 | 0.00 | 0.31 |  | 0.32 | 0.11 | 11.96 |  | 15.26 |
| performance | old | 1.70 | 0.16 | 0.07 | 0.02 | 3.22 | 0.24 | 0.41 |  | 0.07 | 0.23 | 11.56 |  | 17.69 |
| performance | new | 1.59 | 0.15 | 0.03 | 0.02 | 1.73 | 0.00 | 0.28 |  | 0.32 | 0.11 | 10.56 |  | 14.78 |

### surdana, 1440

- old: before the run (other drakan.exe, replay tools, compilers, GPU %) [0, 0, 0, 2], waited 0 s, peak during (drakan.exe with ours, replay tools, compilers) [1, 0, 0], exit 0
- new: before the run (other drakan.exe, replay tools, compilers, GPU %) [0, 0, 0, 0], waited 0 s, peak during (drakan.exe with ours, replay tools, compilers) [1, 0, 0], exit 0

| mode | pipeline | world fps | frame ms median | frame ms 99 % | EE cpu ms/world | GSWorker cpu ms/world | VU1Worker cpu ms/world | Present() ms per present | presents/s |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| off | old switch (same code path) | 175.8 (175.4, 176.2) | 5.57 (5.56, 5.58) | 7.1 (7.0, 7.2) | 5.33 (5.31, 5.36) | 4.35 (4.24, 4.46) | 5.26 (5.20, 5.32) | 0.06 (0.06, 0.06) | 230 (230, 231) |
| off | new | 176.1 (175.5, 176.8) | 5.57 (5.54, 5.59) | 7.0 (6.9, 7.1) | 5.11 (5.06, 5.17) | 4.13 (4.11, 4.15) | 5.39 (5.33, 5.45) | 0.06 (0.06, 0.06) | 232 (230, 234) |
| dlaa | old | 161.9 (161.4, 162.5) | 6.02 (5.99, 6.04) | 8.1 (8.0, 8.2) | 5.59 (5.58, 5.61) | 5.01 (4.85, 5.18) | 5.33 (5.33, 5.34) | 0.06 (0.06, 0.06) | 204 (203, 206) |
| dlaa | new | 162.4 (162.3, 162.4) | 6.00 (5.99, 6.00) | 8.0 (8.0, 8.1) | 5.62 (5.52, 5.71) | 5.05 (4.96, 5.14) | 5.37 (5.34, 5.39) | 0.06 (0.06, 0.06) | 205 (205, 205) |
| quality | old | 161.6 (161.2, 162.0) | 6.02 (5.99, 6.04) | 8.1 (8.0, 8.3) | 5.51 (5.46, 5.56) | 5.08 (5.01, 5.15) | 5.31 (5.24, 5.38) | 0.06 (0.06, 0.06) | 203 (202, 204) |
| quality | new | 162.1 (161.7, 162.4) | 6.00 (5.98, 6.01) | 8.1 (8.0, 8.1) | 5.54 (5.45, 5.63) | 5.05 (5.00, 5.09) | 5.51 (5.35, 5.68) | 0.06 (0.06, 0.06) | 204 (203, 206) |
| balanced | old | 160.5 (160.2, 160.8) | 6.05 (6.04, 6.06) | 8.4 (8.3, 8.5) | 5.61 (5.56, 5.66) | 4.92 (4.83, 5.00) | 5.47 (5.43, 5.52) | 0.06 (0.06, 0.06) | 202 (200, 204) |
| balanced | new | 162.7 (162.5, 162.8) | 5.99 (5.98, 6.00) | 8.1 (8.1, 8.2) | 5.62 (5.54, 5.70) | 5.07 (4.96, 5.18) | 5.39 (5.35, 5.43) | 0.06 (0.06, 0.06) | 206 (206, 207) |
| performance | old | 161.9 (161.5, 162.2) | 6.00 (5.99, 6.02) | 8.3 (8.3, 8.3) | 5.59 (5.54, 5.63) | 4.95 (4.82, 5.08) | 5.43 (5.33, 5.53) | 0.06 (0.06, 0.07) | 204 (203, 205) |
| performance | new | 163.0 (162.5, 163.4) | 5.98 (5.97, 5.99) | 8.0 (8.0, 8.1) | 5.52 (5.52, 5.52) | 4.91 (4.84, 4.97) | 5.37 (5.32, 5.42) | 0.06 (0.06, 0.06) | 206 (205, 207) |

GPU ms per world (median of the rounds; a pass missing from a mode is empty):

| mode | pipeline | world | worldEntry | guides | modelMotion | evaluate | convert | compose | afterWorld | display | presentBlit | overlay | frame | total |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| off | old | 0.28 |  |  |  |  |  |  |  | 0.02 | 0.11 | 5.28 |  | 5.69 |
| off | new | 0.33 |  |  |  |  |  |  |  | 0.02 | 0.11 | 5.21 |  | 5.68 |
| dlaa | old | 0.38 | 0.00 | 0.15 | 0.04 | 1.14 | 0.19 | 0.09 |  | 0.02 | 0.08 | 4.07 |  | 6.17 |
| dlaa | new | 0.40 | 0.00 | 0.16 | 0.04 | 1.15 | 0.00 | 0.11 |  | 0.02 | 0.08 | 4.19 |  | 6.16 |
| quality | old | 0.43 | 0.08 | 0.05 | 0.04 | 1.07 | 0.15 | 0.15 |  | 0.04 | 0.11 | 4.08 |  | 6.19 |
| quality | new | 0.45 | 0.07 | 0.02 | 0.04 | 0.68 | 0.00 | 0.13 |  | 0.15 | 0.06 | 4.56 |  | 6.17 |
| balanced | old | 0.39 | 0.07 | 0.03 | 0.04 | 1.06 | 0.14 | 0.15 |  | 0.03 | 0.11 | 4.19 |  | 6.22 |
| balanced | new | 0.49 | 0.07 | 0.02 | 0.04 | 0.68 | 0.00 | 0.13 |  | 0.15 | 0.06 | 4.51 |  | 6.15 |
| performance | old | 0.44 | 0.07 | 0.02 | 0.04 | 1.31 | 0.13 | 0.15 |  | 0.03 | 0.11 | 3.87 |  | 6.18 |
| performance | new | 0.44 | 0.07 | 0.01 | 0.04 | 0.80 | 0.00 | 0.11 |  | 0.15 | 0.06 | 4.46 |  | 6.14 |

### ootf-ruined, 1440

- old: before the run (other drakan.exe, replay tools, compilers, GPU %) [0, 0, 0, 0], waited 0 s, peak during (drakan.exe with ours, replay tools, compilers) [1, 0, 0], exit 0
- new: before the run (other drakan.exe, replay tools, compilers, GPU %) [0, 0, 0, 0], waited 0 s, peak during (drakan.exe with ours, replay tools, compilers) [1, 0, 0], exit 0

| mode | pipeline | world fps | frame ms median | frame ms 99 % | EE cpu ms/world | GSWorker cpu ms/world | VU1Worker cpu ms/world | Present() ms per present | presents/s |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| off | old switch (same code path) | 136.5 (131.6, 141.3) | 7.13 (6.96, 7.30) | 10.4 (9.5, 11.3) | 7.24 (6.98, 7.50) | 6.80 (6.53, 7.06) | 5.46 (5.20, 5.72) | 0.06 (0.06, 0.06) | 153 (145, 162) |
| off | new | 137.9 (134.6, 141.2) | 6.99 (6.92, 7.05) | 10.4 (9.8, 11.0) | 7.24 (7.06, 7.42) | 6.88 (6.70, 7.06) | 5.33 (5.11, 5.54) | 0.06 (0.06, 0.06) | 157 (148, 166) |
| dlaa | old | 66.5 (66.4, 66.6) | 15.95 (15.77, 16.12) | 26.5 (26.3, 26.6) | 13.81 (13.69, 13.93) | 8.51 (7.82, 9.19) | 6.04 (5.60, 6.48) | 2.79 (2.69, 2.89) | 120 (120, 120) |
| dlaa | new | 67.0 (66.9, 67.0) | 15.98 (15.83, 16.13) | 25.7 (25.0, 26.4) | 13.73 (13.69, 13.77) | 8.00 (7.77, 8.23) | 6.58 (6.13, 7.03) | 2.82 (2.77, 2.86) | 120 (120, 120) |
| quality | old | 80.2 (80.1, 80.2) | 12.82 (12.80, 12.83) | 16.7 (16.7, 16.7) | 11.32 (11.16, 11.48) | 8.29 (8.05, 8.54) | 5.67 (5.49, 5.86) | 2.08 (2.06, 2.09) | 120 (119, 120) |
| quality | new | 89.1 (88.9, 89.2) | 11.21 (11.17, 11.25) | 16.6 (16.5, 16.6) | 10.69 (10.48, 10.90) | 8.22 (7.96, 8.47) | 5.91 (5.40, 6.43) | 1.23 (1.15, 1.31) | 120 (119, 120) |
| balanced | old | 85.4 (85.3, 85.4) | 11.85 (11.83, 11.86) | 16.7 (16.7, 16.7) | 11.01 (10.88, 11.13) | 8.08 (7.57, 8.58) | 5.64 (5.48, 5.80) | 1.90 (1.88, 1.91) | 120 (120, 120) |
| balanced | new | 94.1 (93.8, 94.3) | 10.52 (10.46, 10.58) | 16.1 (16.1, 16.1) | 10.34 (10.24, 10.44) | 8.09 (8.03, 8.14) | 6.12 (5.88, 6.36) | 1.00 (0.91, 1.09) | 120 (120, 120) |
| performance | old | 88.1 (87.8, 88.4) | 11.41 (11.38, 11.44) | 16.6 (16.6, 16.6) | 10.54 (10.52, 10.56) | 7.93 (7.83, 8.02) | 5.37 (5.33, 5.42) | 2.00 (1.96, 2.05) | 120 (120, 120) |
| performance | new | 95.8 (93.6, 98.0) | 10.36 (10.14, 10.58) | 15.4 (15.4, 15.4) | 10.15 (9.93, 10.36) | 8.93 (8.60, 9.26) | 6.75 (6.30, 7.20) | 0.61 (0.30, 0.92) | 120 (120, 120) |

GPU ms per world (median of the rounds; a pass missing from a mode is empty):

| mode | pipeline | world | worldEntry | guides | modelMotion | evaluate | convert | compose | afterWorld | display | presentBlit | overlay | frame | total |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| off | old | 0.62 |  |  |  |  |  |  |  | 0.02 | 0.10 | 6.53 |  | 7.27 |
| off | new | 0.59 |  |  |  |  |  |  |  | 0.02 | 0.10 | 6.56 |  | 7.27 |
| dlaa | old | 1.49 | 0.00 | 0.12 | 0.02 | 1.11 | 0.15 | 0.07 |  | 0.02 | 0.07 | 11.97 |  | 15.04 |
| dlaa | new | 1.85 | 0.00 | 0.12 | 0.02 | 1.10 | 0.00 | 0.09 |  | 0.02 | 0.07 | 11.66 |  | 14.94 |
| quality | old | 1.01 | 0.08 | 0.04 | 0.02 | 1.06 | 0.14 | 0.14 |  | 0.03 | 0.07 | 9.88 |  | 12.48 |
| quality | new | 0.77 | 0.07 | 0.02 | 0.02 | 0.68 | 0.00 | 0.13 |  | 0.09 | 0.04 | 9.48 |  | 11.29 |
| balanced | old | 0.82 | 0.08 | 0.03 | 0.02 | 1.05 | 0.13 | 0.14 |  | 0.03 | 0.07 | 9.36 |  | 11.71 |
| balanced | new | 0.74 | 0.07 | 0.02 | 0.02 | 0.67 | 0.00 | 0.12 |  | 0.09 | 0.04 | 8.89 |  | 10.66 |
| performance | old | 0.87 | 0.07 | 0.02 | 0.02 | 1.31 | 0.11 | 0.14 |  | 0.02 | 0.07 | 8.75 |  | 11.38 |
| performance | new | 0.83 | 0.07 | 0.02 | 0.02 | 0.79 | 0.00 | 0.11 |  | 0.10 | 0.04 | 8.52 |  | 10.50 |

Reading the tables:

- World fps, 3840x2160, off / quality old / quality new: Surdana 175.6 / 101.8 / 155.0; tundra 111.1 / 85.6 / 126.8;
  arena 124.3 / 81.7 / 117.8; Ruined Village 85.6 / 50.4 / 61.3. Balanced and performance gain the same way. DLAA
  moves little (75.1 to 79.6, 56.8 to 58.8, 56.3 to 57.9, 36.9 to 36.8): its sizes are unchanged and it only loses
  the guide rebuild.
- 2560x1440 (scale 5, a 2560x2560 target): Surdana is CPU-bound in every mode and old equals new (161.6 and 162.1
  for quality, off 176.1); the Ruined Village gains (80.2 to 89.1 for quality, off 137.9). The tundra view and the
  arena were not run at 2560x1440 (the quiet window was limited to ten runs).
- DLSS off run with the old switch and without it is the same code path; the two columns differ by 0.2 to 3.7 %,
  which is the run-to-run spread to hold against every other difference here.
- Presents per second sit at 120 (twice the display's 60 Hz) in many rows, old and new, and with DLSS off in the
  tundra, the arena (128) and the Ruined Village, with `Present` waiting 2.3 to 13 ms per call while the timed GPU
  passes are a fraction of the frame. See "Not established": in those rows the frame rate may be set by how
  presentation is paced for a window that is not in the foreground, not by GPU work.
- CPU: the game's main thread spends 5.1 to 5.3 ms per world with DLSS off in Surdana and 5.7 with quality (new);
  7.2 against 8.0 in the arena; 10.2 against 13.9 in the Ruined Village (1745 object-motion segments per world
  there, perf-gpu.md). The GS worker spends 4.2 against 5.1 (Surdana), 4.5 against 6.0 (tundra), 5.5 against 7.2
  (arena), 6.9 against 7.9 (Ruined Village). That is the cost that keeps quality under off where the GPU is idle:
  per-draw object motion (`prepareObjectMotion`, 0.6 to 0.85 ms per world in Surdana), the scene capture and motion
  scopes on the main thread. It lives in the draw path and the guest hooks and was not changed here.

## The collapse to single-digit frame rates

Seen all day by several agents as "DLSS falls to 5 to 8 fps, the time is inside Present". Reproduced three times
while 9 to 18 replay tools and 2 to 4 other games were on the GPU (15:14 to 15:55). `tools/vram_watch.ps1` sampled
the Windows counters `GPU Adapter Memory` and `GPU Process Memory` every 5 s (`runs/vram.csv`):

| Run (pipeline, time) | Adapter dedicated, shared (GiB) | The game's dedicated, shared peak (MiB) | Result |
| --- | --- | --- | --- |
| Surdana cycle, new, 15:14 | 11.1 to 15.4, 5.3 to 10.5 | 1869, 1295 | off 61.5 world fps; quality 9.9, balanced 12.3, DLAA 4 presents/s; `Present` 67 to 455 ms |
| Surdana cycle, new, 15:35 | 11.3 to 14.6, 4.6 to 12.6 | 2728, 1283 | off 54.2; quality 42.6, balanced 41.1, performance 48.8; DLAA 20.5 with `Present` 132 ms |
| Surdana cycle, old, 15:39 | 13.2 to 15.3, 3.8 to 15.4 | 2200, 2225 | off 30.9; quality, balanced, DLAA 1 to 4 presents/s, `Present` 114 to 282 ms |
| Tundra and arena cycles, both, 15:43 to 15:55 | 13.9 to 15.3, 9.8 to 25.2 | 1736 to 2073, 1719 to 1869 | every mode, off included, 0 to 24 world fps |

- When the adapter's 16 GiB of dedicated memory is full, Windows moves resources of the processes into shared
  (system) memory. A frame that touches such a resource stalls, and the stall is paid in `Present`.
- DLSS on allocates more than off: per world buffer a 4096x4096 RGBA32F object-motion target (256 MiB; the game
  uses two buffers), the render-sized guides, the upscaler's history, and in the old pipeline 384 MiB of
  output-sized guide targets plus a 128 MiB output. So the DLSS modes are the first to have memory moved: DLSS off
  kept 31 to 62 fps in the three Surdana runs (and fell too in the last row, at 25 GiB of shared memory).
- The new pipeline needs about 350 MiB less at 3840x2160 (384 MiB of guides and 65 MiB of output gone, 95 MiB of
  finished pictures and the window image added). In the two Surdana runs four minutes apart the new one held 41 to
  49 fps in the reduced modes where the old one made 1 to 4 presents per second; the load was not identical in the
  two runs, so that pair is an indication, not a measurement.
- Not a start-up order or window mode effect in these runs: the same exe, ini and borderless window made 155 fps
  an hour later on the free GPU.
- What would lower the risk further (not done): the object-motion target as two smaller targets (RG16F motion and
  R32F depth, 8 instead of 16 bytes per pixel: 128 MiB less per buffer) needs the draw path's pixel shader outputs;
  a lower internal scale when dedicated memory is short.

## Picture

Matched runs at the Surdana start, foliage wind off, 3840x2160, window screenshots (`runs/pic-old`, `pic-new`,
`pic-off`, `pic-dlaa`; recipe `recipe_pic.txt`: a still, a turn and a second still, a run forward with five shots,
a third still). Taken under load (the frame rate differs between the runs, so the moving shots and the later
stills are not at the same position). Crops at one screen pixel per pixel, old | new | off, in
`build/review/dlss-speed/out/`:

- `still_a_trees.png` (distant trees, tent, cliff): I could not tell old from new: the same leaf edges, tent
  stripes and cliff texture. Off looks the same at this size too.
- `still_a_grass.png` (grass blades and ground texture close to the camera): no difference I could see between
  old and new in blade edges or ground detail.
- `still_a_hud.png` (health orb at twice the size): the HUD frame and orb look identical in all three; the grass
  behind the HUD is the same in old and new.
- `still_b_centre.png` (after the turn; includes DLAA): tree bark, ground and Rynn's hair match in sharpness
  across old, new, off and DLAA; Rynn's idle pose differs between the runs.
- `move_3_rynn.png` (running): no ghost trail behind Rynn's arms, legs or hair in the new pipeline; the grass
  behind her is clean. The three runs are at different places, so this is a look at each, not a matched pair.
- Mean absolute difference of the first still (world region, 8 bit levels): old against new 1.16, old against off
  1.03. The morning's per-axis experiment had 3.0. Two runs of one setting differ by 0.5 to 1.4 (perf-gpu.md).
- `runs/f1/s1.png`: the debug tint, see "What changed", 5.

Judgement: no visible loss from the old pipeline to the new one in these stills, although the new one feeds the
upscaler half the samples (3.69 against 7.45 megapixels) and shows its output without the 1.9 times vertical
supersampling the old path had as a side effect. A player who wants that supersampling back has DLAA, which costs
what the old quality mode cost. Not looked at: shimmer over time (no video was taken), thin geometry such as
ropes in motion, interiors, the Order of the Flame level, 2560x1440.

## Tests and builds

- `runtime/gs` configured alone into `build/review/dlss-speed/gs`, Release, after the changes and after the
  clang-format pass: `ctest`: 45 tests, 44 passed, `live` skipped (it needs a capture). New: `gpu_pass_stats`
  (26 checks); extended: `upscale_render_size` (plan, LOD bias, write-back mapping: 39 checks in all),
  `sr_sharpness` (16 cases; the new one checks `composeTarget` pixel for pixel on WARP).
- Game builds with the shared lock, `Build-LocalGame.ps1 -Incremental` into `build/review/dlss-speed/play`: six,
  of which one failed (`ThreadNaming.h` included after `windows.h` in `gs_gsr_bridge.cpp`: redefinition errors;
  fixed by calling `SetThreadDescription` directly) and five ended with exit 0. The last one was made after the
  commit and the format pass, from the tree as it then was (other sessions' edits included), and passed the smoke
  run below.
- The root `runtime-tests` preset and the Python tests were not run.
- `PSPresentView` has no automated test: it is covered by the debug tint picture and by the matched screenshots.

## Not established

- **Why `Present` waits at 120 presents per second.** In the old pipeline's reduced modes, in DLAA, and with DLSS
  off in three scenes, presents are held at 120 (or 110 to 128) per second and `Present` blocks 2 to 13 ms, while
  the timed GPU passes leave most of the frame idle. Candidates: the upscaler doing work the Direct3D 11
  timestamps do not see; composition pacing of a borderless window that is not in the foreground (harness windows
  are never activated; the owner plays in the foreground, where the swap chain can flip independently with
  tearing). If it is the second, the owner's own numbers will differ from every number here, in both directions.
  One run in the foreground with `GSR_PROFILE=2` (`presentCall`) would tell.
- The frame rate in the owner's real play (foreground, audio on, a save in a town or a fight). Every run here is a
  still view from New Game.
- Whether the collapse can still happen with the new pipeline on a GPU with less than 16 GiB, or with fewer other
  programs than today. Only the mechanism is shown.
- The NVIDIA figures quoted from memory.
- 2560x1440 in the tundra view and the arena; any scene at another window size or aspect; a window whose mapping is
  not a whole number of pixels per frame falls back to the old sizes and was only unit tested.
- Movement quality over time (shimmer, disocclusion), beyond the five moving stills.
- `docs/reference/env-flags.md` does not list the new switches: that file has another session's uncommitted edits.
- DLSS on the WebGPU / Vulkan renderer: it has no DLSS; nothing here applies to it yet.

## Re-measuring

```sh
# the five modes in one run at one pose, new pipeline, then the old one (add --wait-quiet 600 to wait for a free GPU)
python build/review/dlss-speed/tools/cycle_bench.py --exe <play>/drakan.exe --out-root <runs> --name surdana-new \
    --scene surdana --key video.dlss --rounds 2 --settle 3 --window 6
python build/review/dlss-speed/tools/cycle_bench.py ... --name surdana-old --env GSR_UPSCALE_WINDOW_OUTPUT=0
# separate processes, as the earlier notes did
python scripts/perf/cpu_scene_bench.py --exe <play>/drakan.exe --scene surdana --repeats 3 \
    --variant off:DRAKAN_DLSS=off --variant quality:DRAKAN_DLSS=quality --env GSR_PROFILE=2 --env GSR_GPU_PASS_TIMING=1
```

`cycle_bench.py`, `summ.py` (the tables above), `crops.py` and `vram_watch.ps1` are scratch tools under
`build/review/dlss-speed/tools/`; they are not in the repository.
