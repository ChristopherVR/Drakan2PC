# "Support Vulkan": what was already there, the `renderer = vulkan` choice, parity and cost against Direct3D 11

Request: "support Vulkan". Machine: Windows 11, RTX 5080 (driver 617.14 as Vulkan reports it) plus an AMD integrated
GPU, 3840 x 2160 desktop. About seven agents shared the machine: three to nine game instances ran at any time, the GPU
sat at 98 to 99 % and between 3 and 14 of its 16 GB were in use. Every frame rate below is depressed and noisy; only the
interleaved A/B relation means anything, and not much.

All scratch is under `build/review/vulkan/` (not committed): `runs/` (logs and frames), `evidence/` (pictures),
`testlogs/`, `recipes/` (the scripts that made them).

## Finding: the game already ran on Vulkan

The premise "Windows renders with Direct3D 11, a second WebGPU backend exists for Linux and the browser" is out of date
since 2026-10-30 ([renderer-selection.md](../2026-10-30/renderer-selection.md)):

- The Windows exe links both backends (`gsr_both`), and `renderer = webgpu` is the default written into a new
  `drakan.ini`. Only an ini from an older build still says `renderer = d3d11`; the owner's play folder is one of those.
- The WebGPU backend runs on wgpu-native v29.0.1.1, whose adapter search (`runtime/gpu/src/Context.cpp`) ranks Vulkan
  first, then Direct3D 12 on Windows and OpenGL on Linux. `PS2X_GS=webgpu` on this machine gave
  `[gpu] device: NVIDIA GeForce RTX 5080 via Vulkan` before any change of this entry (`runs/pre_vk`, the 08:09 exe of
  `build/review/play`).
- A developer override existed (`DRAKAN_GPU_BACKEND=vulkan|dx12|gl`): strict, no fallback.

What a player could not do: ask for Vulkan by name, see it in PC Settings, or be told when the renderer ran on
Direct3D 12 instead. This entry adds that. It does not add a renderer.

## Approach

(a) wgpu-native held to Vulkan: chosen. It is the shipped path on Linux and the default on Windows and it passes the
same synthetic GS suite as on Direct3D 12.

(b) A hand-written Vulkan backend beside D3D11: not started. Nothing found says (a) cannot work. (b) would be a third
copy of a backend (`d3d11_*.cpp` is 6,780 lines, `wgpu_*.cpp` 5,580 plus the `rhi` layer) with its own swap chain,
memory and pipeline code. Its only gains over (a) are what `webgpu.h` hides: a raw `VkDevice` for NVIDIA NGX (DLSS on
Vulkan), FSR's Vulkan path and `VK_KHR_ray_query`. If those are wanted on Linux, the smaller step is an upscaler bridge
on a raw device, not a whole backend.

What Vulkan gives a player: the Linux build (Steam Deck included) uses it natively already; on Windows it is an
alternative to wgpu's Direct3D 12 path that needs neither `dxcompiler.dll` nor FXC. It gives no DLSS or FSR today.
Nothing here was run on Linux, on a Steam Deck, under Proton, or on an Intel or AMD GPU.

## What changed

- `gsr::Config::gpuApi` (`Auto`, `Vulkan`, `D3D12`, `OpenGL`), read by the WebGPU backend (`wgpu_device.cpp`): the
  adapter search is held to that API. When the device cannot be opened it logs
  `[gsr-webgpu] Vulkan is not available (<reason>); falling back to the automatic graphics API` and opens again with the
  automatic choice.
- `gsr/renderer_choice.h`: `parseRendererChoice` maps `[video] renderer` / `PS2X_GS` names (`webgpu`, `vulkan` / `vk`,
  `d3d12` / `dx12`, `opengl` / `gl`, `d3d11`); the GS bridge uses it and logs an unknown name.
- PC Settings > Display > Renderer lists WebGPU (default), Vulkan and Direct3D 11 on Windows (a restart applies it).
  Linux has no Renderer row (one renderer); `renderer = vulkan` in its `drakan.ini` goes through the same code.
- `[gpu] device: ...` ends with the driver text the adapter reports. The start-up line `[gs] backend: d3d11 (gsr)`, which
  was printed for every GPU renderer, now says `[gs] backend: gsr (GPU renderer)`.
- Bug found by the fallback test, fixed: with a window, `drakan::gpu::Context` created a probe surface before
  enumerating adapters. On an instance none of whose backends started (Vulkan asked for, no Vulkan driver) wgpu-native
  aborts the process in `wgpuInstanceCreateSurface` ("Failed to create surface for any enabled backend"). The game died
  there (`runs/build1/novulkan`, exit 0xC0000409). Adapters are now enumerated first and an empty list is an ordinary
  error. Test `gpu_no_backend` (aborts without the fix, passes with it).
- Performance gap found and narrowed: the WebGPU backend read a render target back once per gameplay frame
  (171 blocking readbacks in 170 presented frames, 2.1 s of every 5 s on the GS thread at internal scale 8,
  `GSR_PROFILE`). Direct3D 11 avoids exactly that readback with its "solid mirror" (`gs_solid_mirror.h`,
  `GSR_RB_SOLID`): with `GSR_RB_SOLID=0` D3D11 does 1,144 readbacks in 2,804 frames instead of about 90. The mirror
  is now in `wgpu_draw.cpp` too (the same proof and exclusions; off while a ray effect is on, see below).
  The integration half of `solidMirrorTest` was compiled for D3D11 only; it now runs on WebGPU as well
  (`partial_upload_webgpu`: 8 of its checks fail with `GSR_RB_SOLID=0`, none with the mirror).
- `[gsr-webgpu] DLSS 5 neural rendering is ignored: it needs renderer = d3d11` when that setting is on.
- Tests added: `renderer_choice`, `gpu_no_backend`, `partial_upload_webgpu`, `synthetic_webgpu_vulkan`
  (`gsr_test_synthetic_webgpu --gpu-api vulkan`, skipped with 77 unless the renderer's adapter name ends in
  `(Vulkan)`), `ray_trace_vulkan` and `rhi_vulkan` (`DRAKAN_GPU_BACKEND=vulkan`).

Tried and withdrawn: a frames-in-flight limit in `rhi_webgpu.cpp` (wait for the queue when more than two presented
frames are pending), as a guess at the device losses below. It did not change them (5 of 5 runs lost the device) and
was reverted.

No SDK or binary was downloaded. The private test build reused the wgpu-native archive already unpacked in
`build/runtime-tests/_deps` (`FETCHCONTENT_SOURCE_DIR_DRAKAN_WGPU`). No CMake option of `build/final-game` changed; the
game was rebuilt incrementally six times into `build/review/vulkan/play` under the build lock.

## Proof that it is Vulkan

Game, `PS2X_GS=vulkan`, otherwise the owner's ini (`runs/surdana_vulkan/stdout_1.log`):

```
[gsr] renderer 'vulkan': the WebGPU renderer is asked for Vulkan
[gpu] adapter candidate: NVIDIA GeForce RTX 5080 (Vulkan, type 1)
[gpu] adapter candidate: AMD Radeon(TM) Graphics (Vulkan, type 2)
[gpu] device: NVIDIA GeForce RTX 5080 via Vulkan, dual-source blending, driver 617.14
[gsr-webgpu] renderer on NVIDIA GeForce RTX 5080 (Vulkan), internal scale 5, fixed-function blending
```

`Vulkan` in the `device` line is `WGPUAdapterInfo::backendType` and `617.14` is `WGPUAdapterInfo::description`, both
returned by `wgpuAdapterGetInfo` for the adapter the device was created on; neither is a string of ours. Only Vulkan
adapters are enumerated (the automatic search also lists the D3D12 and OpenGL ones).

Loaded modules of the running game (`testlogs/modules_*.txt`, from `Get-Process`): on `vulkan` the process has
`vulkan-1.dll` and `nvoglv64.dll` (NVIDIA's Vulkan / OpenGL driver) and no `D3D12Core.dll`, `nvwgf2umx.dll` or
`nvngx_dlss.dll`; on `d3d11` it has `nvwgf2umx.dll` (NVIDIA's D3D driver), `D3D12Core.dll`, `_nvngx.dll`,
`nvngx_dlss.dll` and no `vulkan-1.dll`. (`d3d11.dll`, `d3d12.dll` and `dxgi.dll` are loaded in both: wgpu-native links
them.)

Fallback, with the Vulkan loader told to find no driver (`VK_ICD_FILENAMES`, `VK_DRIVER_FILES`,
`VK_LOADER_DRIVERS_DISABLE=*`), game run `runs/novulkan`:

```
[gsr] renderer 'vulkan': the WebGPU renderer is asked for Vulkan
[gpu] no usable GPU adapter (backend Vulkan)
[gsr-webgpu] Vulkan is not available (no usable GPU adapter (backend Vulkan)); falling back to the automatic graphics API
[gpu] device: NVIDIA GeForce RTX 5080 via D3D12, dual-source blending
[gsr] WebGPU renderer on 'NVIDIA GeForce RTX 5080 (D3D12)', ...
```

The run reached gameplay, wrote its two screenshots and quit cleanly. When WebGPU cannot start on any API the existing
behaviour applies (software GS); that path was not exercised.

## Feature matrix (from the code, 2026-11-12)

| Feature | Direct3D 11 (`d3d11`) | WebGPU on Vulkan (`vulkan`, `webgpu`) |
| --- | --- | --- |
| GS emulation, both blend paths, render targets, feedback | yes | yes (same `gsr_core`; fixed-function blend needs dual-source blending, else in the shader) |
| Internal resolution 1..8, auto scale, widescreen, HUD scale, wide backdrops | yes | yes |
| HD texture replacement packs, texture dump | yes | yes (`wgpu_textures.cpp`, same `drakan::texture` cache and hashes) |
| Texture upscale (2x..8x, xBRZ / smooth), anisotropic, mip bias | yes | yes; with `texture_budget_mb = auto` the budget is a fixed 1536 MB (no VRAM query), an explicit value is honoured |
| Movies, HD movie pack frames | yes | yes (`wgpu_movie.cpp`) |
| F1 settings overlay, modern menus and inventory, HUD layers, FPS counter | yes | yes (`OverlayWebGpu`) |
| Window screenshots, save previews, frame-buffer capture | yes | yes (`wgpu_capture.cpp`) |
| NVIDIA DLSS Super Resolution / DLAA | yes (NGX) | no. The DLSS setting switches on the portable temporal AA; every mode renders at full internal resolution, there is no upscaling |
| DLSS 5 neural rendering / ray reconstruction | yes (D3D12 interop) | no; ignored with a log line, PC Settings reports it unavailable |
| Motion vectors for the upscaler | yes | not produced (the TAA reprojects with the camera only) |
| Modern water, ray-traced shadows, reflections, GI | hardware DXR 1.1 ray queries | WGSL compute over CPU-built BVHs: same scene and shading, lower trace resolution, slower; see the device losses below |
| Readback avoidance | block and bit exact skips, GPU target-to-target copy, prefetch, solid mirror (`GSR_RB_*`) | block-level skip, dirty-row trimming and, since this entry, the solid mirror |
| Compiled shader disk cache, start-up prewarm draws | yes | no (the driver's own pipeline cache) |
| Device loss | not examined | none handled: wgpu-native aborts the process |
| Exclusive fullscreen | not offered (`caps.exclusiveFullscreen = false`) | same |
| Present pacing | waitable swap chain | the surface's present mode (FIFO with vsync, else Mailbox or Immediate) |

## Test results as run

Private build `build/review/vulkan/rt` (`runtime-tests` preset, Release), last run on the final sources, logs in
`build/review/vulkan/testlogs/`:

| Test | Backend | Result |
| --- | --- | --- |
| `gsr_test_synthetic` | Direct3D 11 | 376 checks, 0 failed |
| `gsr_test_synthetic_webgpu --gpu-api vulkan` | WebGPU, adapter `(Vulkan)` | 340 checks, 0 failed |
| `gsr_test_synthetic_webgpu --gpu-api d3d12` | WebGPU, adapter `(D3D12)` | 340 checks, 0 failed |
| `gsr_test_synthetic_webgpu` | WebGPU automatic, adapter `(Vulkan)` | 340 checks, 0 failed |
| the same with Vulkan drivers hidden, `--gpu-api vulkan` (first build) | falls back to `(D3D12)` | exit 77 "the renderer is not on Vulkan" (the intended skip) |
| `gsr_test_partial_upload` | Direct3D 11 | 95 checks, 0 failed |
| `gsr_test_partial_upload_webgpu --gpu-api vulkan` | WebGPU on Vulkan | 62 checks, 0 failed (8 fail with `GSR_RB_SOLID=0`) |
| ctest `gpu`, `gpu_no_backend`, `renderer_choice`, `rhi`, `rhi_vulkan`, `ray_trace`, `ray_trace_vulkan`, `partial_upload`, `partial_upload_webgpu`, `settings_test`, `settings_schema_test`, `settings_menu_test`, `gameui_settings_test`, `overlay_webgpu_test`, `native_display_commit` | | 15 of 15 passed |
| `python scripts/run_tests.py` | | 532 tests, 1 error: `test_build_affinity` timed out starting PowerShell (not rerun; unrelated to these files), 2 skipped |

At the start the Vulkan suite was 331 of 331 and D3D11 365 of 365; the counts grew because another agent added checks
to `test_synthetic.cpp` during the day. The D3D11 build of that file has 36 more checks than the WebGPU build (sections
compiled for D3D11 only; not listed one by one). The full ctest run of the preset was not made, and nothing was run on
a machine without Vulkan except through the hidden-driver environment.

## Live comparison

`build/review/vulkan/play` is `build/review/baseline-play` rebuilt from the working tree, with the owner's `drakan.ini`
unchanged except `elf =` (a private copy). Each pair differs only by `PS2X_GS=d3d11` / `PS2X_GS=vulkan`. Harness
conditions that differ from real play: audio off, window not activated (borderless 3840 x 2160 behind other windows),
a fresh memory card, autopress through the menus, `PS2X_GS_PACING=1`, scripted input. Of the owner's settings the
Vulkan path cannot honour `dlss = quality` (it runs the temporal AA at full resolution). `dlss_neural`,
`modern_water` and `ray_*` are off in that ini.

Pictures of the last build: `build/review/vulkan/evidence/pair_<scene>_<shot>.png` (D3D11 left, Vulkan right; the
whole frame above, a 1:1 crop below) with the two full frames beside each. Earlier builds of the day are in
`evidence/build1` and `evidence/build4`; `evidence/contaminated` and `runs/contaminated` hold a batch that ran while a
second batch of this task was still alive and are not used. "mad" is the mean absolute difference of the two frames,
out of 255; frames are not pixel-matched because animation time differs between runs.

| Scene | Result |
| --- | --- |
| Game choice card at start (`title`) | identical to the eye, mad 0.03 |
| Drakan 2 title menu (`title2`, `DRAKAN_GAME=d2`) | identical to the eye, mad 0.02 |
| Surdana outdoors (`surdana_outdoor`) | same geometry, HD textures, fog, sky; mean colour equal within 0.2 per channel, mad 2.6. D3D11 (DLSS Quality) is smoother on grass and foliage edges, Vulkan (temporal AA at full resolution) shows more texture grain. Rynn's idle pose differs between runs |
| HUD (`surdana_hud_after_inventory`) | health and mana orbs and their frames the same |
| Modern inventory (`surdana_inventory`) | same layout, icons, text, cursor and 3D preview, mad 0.6 |
| Pause menu (`surdana_pause`) | same |
| Interior: Catacombs (`catacombs_stand`, `catacombs_walked`) | standing: same, mad 0.4. After the walk the camera is inside the cell bars on both; the see-through Rynn in front of the camera looks redder and more solid on Vulkan in both builds compared. Not explained (the camera is a little further along on Vulkan) |
| Second level: Shadowmire cave pool (`shadowmire_stand`, `shadowmire_walked`) | same water, rock, fog and the same smeared rock texture near the camera on both |
| Order of the Flame, first level (`ootf_stand`, `ootf_walked`, `DRAKAN_GAME=d1`) | same night scene, flames, HUD; Rynn mid-gesture in one frame, camera a step apart after the walk |
| Surdana pond (`pond_pond`, `pond_pond2`) | not matched in the last batch: on Vulkan Rynn was still diving when the picture was taken and the second picture is under water. The matched pond pairs are in `evidence/build1` and `evidence/build4` (same water, mad 3) |
| F1 PC Settings (`evidence/build4_f1_*`) | opens on both; Renderer lists the choice, shows "Vulkan" on the Vulkan run, and changing it raises "Restart required: Renderer". Taken on build 4; on the last build the harness's `toggle` command was not acknowledged within 15 s on either renderer, so there is no picture of it from that build. Only the top of the Display page is in the picture: the DLSS and ray tracing rows on Vulkan were not looked at |

No garbage, missing geometry or wrong texture was seen in a Vulkan frame.

## Every ray effect on (not the owner's settings)

`DRAKAN_DLSS_NEURAL=1 GSR_MODERN_WATER=1 GSR_RAY_SHADOWS=1 GSR_RAY_REFLECTIONS=1 GSR_RAY_GI=1` at the Surdana pond,
automatic internal scale (8 at this output size). Picture: `evidence/ray_effects_pond_grid.png` (top: D3D11 DXR,
Vulkan, WebGPU on D3D12; bottom: Vulkan at scale 4, Vulkan with water and shadows only, plain Vulkan).

- When it runs, the Vulkan image has the same water, reflections, shadows and shoreline foam as the DXR one, at a
  fraction of the frame rate (3.5 against 11.4 fps on the counters of one pair of pictures, `evidence/build4_pair_all_ray_effects_*`).
- It does not always run. The Vulkan device was lost a few seconds after the first ray-traced world
  ("Parent device is lost" in `wgpuSurfaceGetCurrentTexture`), and wgpu-native then aborts the process
  (exit 0xC0000409). Counts by build: first build 1 of 1 fine; with the solid mirror on (builds 4 and 5) 9 of 11 runs
  lost the device; with the mirror kept off under ray effects 5 of 5 undisturbed runs were fine (2 by
  `GSR_RB_SOLID=0` on build 4, 3 on the last build), and 5 of 5 lost the device while a second batch of this task was
  running beside them. The mirror is therefore off while a ray effect is on. Single runs on the last build: WebGPU on
  D3D12, Vulkan at scale 4 and Vulkan with water and shadows only were fine.
- The cause is not established. The frames before a loss took 1 to 3 s each, which fits a driver time-out on one long
  submission, and the readback the mirror removes also submits a frame's commands early; a frames-in-flight limit did
  not help. One plain-settings Vulkan run (no ray effect, mirror on) also lost its device, on build 4 with nine game
  instances running and 14.4 of 16 GB of video memory in use; about 50 other plain Vulkan runs did not.
- D3D11 with the same settings did not fail in the three runs made.

So the gap is twofold: compute ray tracing at a large internal scale can exceed what the device tolerates on a loaded
GPU, and the WebGPU renderer has no answer to a lost device.

## Frame rate

Scene: Surdana start, Rynn standing, 20 s between two screenshots; `[gsr-pacing]` one-second windows of presented
world frames (`recipes/perf.txt`, `recipes/perf_report.py`). Four configurations per round, order rotated each round:
`ini` = the owner's ini (D3D11 runs DLSS Quality, Vulkan runs the temporal AA at full resolution), `off` =
`DRAKAN_DLSS=off` on both (no temporal pass: the like-for-like pair). Output 3840 x 2160, automatic internal scale 8,
vsync off, uncapped world rate, HD textures on.

Before the solid mirror (builds 1 and 2, 5 rounds, `testlogs/perf1_report.txt`):

| Configuration | Median fps | Run medians | 10th..90th percentile of windows | Median frame ms |
| --- | --- | --- | --- | --- |
| D3D11, ini | 15.4 | 4.8, 5.3, 7.7, 30.0, 34.0 | 4.5..42.0 | 65.0 |
| Vulkan, ini | 15.7 | 12.7, 13.7, 13.9, 19.9, 21.7 | 11.9..22.8 | 63.6 |
| D3D11, off | 33.1 | 10.8, 31.0, 32.3, 38.6, 43.5 | 10.8..50.6 | 30.2 |
| Vulkan, off | 17.2 | 10.4, 12.3, 19.1, 20.1, 20.4 | 10.4..24.4 | 58.0 |

Last build (mirror in, 4 rounds, `testlogs/perf4_report.txt`):

| Configuration | Median fps | Run medians | 10th..90th percentile of windows | Median frame ms |
| --- | --- | --- | --- | --- |
| D3D11, ini | 22.9 | 18.4, 19.2, 26.9, 60.4 | 17.7..69.0 | 43.7 |
| Vulkan, ini | 36.1 | 30.0, 33.8, 37.1, 120.5 | 27.6..122.9 | 27.7 |
| D3D11, off | 78.2 | 38.0, 53.8, 124.4, 146.3 | 37.5..146.9 | 12.8 |
| Vulkan, off | 93.0 | 36.6, 83.9, 112.2, 145.9 | 36.0..146.5 | 10.8 |

Build 4, the same code path for these settings (4 rounds, `testlogs/perf2_report.txt`): D3D11 ini 15.8, Vulkan ini
27.4, D3D11 off 34.5, Vulkan off 26.6.

What can be said:

- The machine's load moved the result by a factor of four between runs of one configuration. In the quietest round
  of the last batch the like-for-like pair was 146.3 (D3D11) against 145.9 (Vulkan).
- Before the mirror Vulkan was below D3D11 at equal settings in four rounds of five (0.32 to 0.66 of it); the profile shows why
  (one blocking readback per frame). After it the two are not separable in this noise: in the last batch Vulkan was
  ahead in two rounds, equal in one and behind in one, and in the build 4 batch it was behind (0.77 of D3D11).
- Readbacks per run after the mirror: 320 to 570 on Vulkan against 50 to 370 on D3D11, and no GPU target-to-target
  copies on Vulkan (130 to 350 on D3D11). The other `GSR_RB_*` savings are still D3D11 only.
- With the owner's ini D3D11 (DLSS Quality) was slower than Vulkan (temporal AA) in every batch, and slower than D3D11
  without DLSS. In three of the first five D3D11 runs the frame rate fell to 5 to 8 fps with the time inside Present.
  That was not investigated; it should not be read as "Vulkan is faster than D3D11 with DLSS" on a free GPU.
- At internal scale 2 (one run each, before the mirror): D3D11 37.3, Vulkan 45.6.

## Not established

- Linux, Steam Deck, Proton, Intel and AMD GPUs, and a machine that really has no Vulkan driver: not run. The
  fallback was exercised only by hiding the drivers from the loader on this machine. `renderer = vulkan` on Linux is
  inferred from the shared code.
- Fallback when no API of the WebGPU renderer starts (software GS): not exercised.
- Performance on a free machine: not measured. Every number above was taken beside three to nine other game instances.
- Why the device is lost with every ray effect on, and whether the solid mirror is a cause or only removes a
  per-frame wait: not established. The mirror with ray effects on is unmeasured (it is off there).
- The mirror on WebGPU is covered by the synthetic test and by the play runs above; it was not compared against the
  memory path pixel by pixel in the game (`GSR_RB_VERIFY` exists on D3D11 only).
- F1 settings on Vulkan beyond the top of the Display page (the DLSS and ray rows, applying settings live) and the
  modern title, save and map screens: not looked at.
- Long play, level changes, saving and loading, movies, multiplayer and the co-op HUD on Vulkan: not run. The runs
  were 25 to 100 s each from a New Game.
- The D3D11 behaviour with DLSS under load (5 fps runs) and the redder see-through Rynn in the Catacombs frame on
  Vulkan: seen, not explained.
- The full runtime ctest preset: not run (other agents' uncommitted work is in the tree); the subset above was.
