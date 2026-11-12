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
  was then copied into `wgpu_draw.cpp`; in the second pass below it became one shared rule for both backends.
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
| NVIDIA DLSS Super Resolution / DLAA | yes (NGX) | no, and not possible on wgpu-native without a fork (second pass). The DLSS setting switches on the portable temporal AA at full internal resolution; PC Settings names it so |
| DLSS 5 neural rendering / ray reconstruction | yes (D3D12 interop) | no; ignored with a log line, PC Settings reports it unavailable |
| Motion vectors for the upscaler | yes | not produced (the TAA reprojects with the camera only) |
| Terrain detail and cliff projection, material detail (added on 2026-11-12 by other work; terrain detail off by default since 9c1671e1) | yes | no: the rules are in shared headers (`terrain_detail.h`, `material_detail.h`), the shader fragments exist for Direct3D 11 only. Visible in the last matched pairs, whose ini had them on |
| Modern water, ray-traced shadows, reflections, GI | hardware DXR 1.1 ray queries | WGSL compute over CPU-built BVHs: same scene and shading, lower trace resolution, slower; see the device losses below |
| Readback avoidance | block and bit exact skips, GPU target-to-target copy, prefetch, solid mirror, row-range refresh, GPU replication (`GSR_RB_*`, `GSR_RT_*`) | block-level skip, dirty-row trimming and, through shared rules since the second pass, the solid mirror, the GPU copy, the row-range refresh and GPU replication. No exact partial uploads, clean-block retention or prefetch |
| Compiled shader disk cache, start-up prewarm draws | yes | no (the driver's own pipeline cache) |
| Device loss | not handled | second pass: reported, the session ends in order and the next start begins on Direct3D 11 once; a loss that wgpu declares inside its own surface call still ends the process |
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
  running beside them. The mirror was therefore switched off while a ray effect is on (undone in the second pass,
  where the cause was found). Single runs on the last build: WebGPU on
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

## Second pass (same day): one set of rules for both backends, device loss, DLSS on Vulkan

New requirement from the owner: changes must be made so that work is not done once per renderer (Direct3D 11,
WebGPU, Vulkan), and the DLSS fixes must reach every path. This part supersedes three statements above: the solid
mirror is no longer a copy in `wgpu_draw.cpp`, it is no longer switched off under ray effects, and the device loss with
every ray effect on has an explanation and a fix.

### Logic that moved into shared headers

Vulkan stays what it was: the one WebGPU code path with an adapter choice. There is no Vulkan-only renderer logic.
The rules below are now written once; a backend file holds the API calls and the per-target bookkeeping only. Each
header reads the render-target fields both backends keep under the same names, so there is no second copy to drift.

| Rule | Shared header | Direct3D 11 call site | WebGPU call site | Test |
| --- | --- | --- | --- | --- |
| Solid mirror: when a constant-colour sprite is written to memory instead of read back, the bound (mirrored rows inside memory), the "another dirty owner" exclusion, the memory writes and row stamps | `gs_solid_mirror.h` (`planSolidMirror`, `applySolidMirror`, `solidMirrorEnabled`) | `d3d11_draw.cpp` (the block became two calls) | `wgpu_draw.cpp` | `solidMirrorTest`, `lastPageMirrorTest`, both backends |
| Which targets refresh by row range, and the `GSR_RT_LAZY_SYNC` switch | `rt_sync_rows.h` (`lazySyncTarget`, `lazyTarget`; `drawRows` / `sampleRows` were there) | `lazySyncTarget` body | `wgpu_draw.cpp`, `wgpu_textures.cpp`, new `syncRows` with `RT::partialSync` | `lazyTargetRowsTest`, both backends; `gsr_test_rt_sync_rows` |
| Target-to-target copy on the GPU: eligibility, aliasing checks, stamps and dirty rows afterwards, `GSR_GPU_COPY` | `rt_local_copy.h` (`rt_copy::plan`, `finish`), new | `copyLocal` (the plan, the copy, `finish`) | new `copyLocal` (WebGPU had none) | `localCopyTest`, both backends |
| Uploads applied to a target: runs of covered blocks, the vertical run join, the GPU replication threshold and `GSR_RT_UPLOAD_GPU` | `rt_upload_runs.h` (`uploadedRuns`, `gpuReplication`), new | `completeTargetUpload`, `uploadTargetRect` | `completeTargetUpload`, new `uploadTargetRect` (native pixels into a stage texture, one point-sampled quad replicates them) | new `targetReplicationTest` (scales 1, 2, 3, 4, 8), both backends |
| Renderer names and the start after a lost device | `gsr/renderer_choice.h`, `gsr/renderer_session.h` | n/a (host) | n/a (host) | `renderer_choice`, `renderer_session` |
| Device loss: every forwarder stops once a backend reports its device lost | `gsr::Backend` (`backend_select.inl`), `BackendApi::deviceLost` | not implemented by the backend (reports "not lost") | `WgpuBackend::deviceLost` | `device_loss_webgpu`, `device_loss_vulkan` |

`Stats::targetRefreshBytes` is filled on WebGPU. Edits in `d3d11_*` are the four call sites above and one include line
each in `d3d11_backend_impl.h`; nothing there was reformatted. While this was done the other agent was changing
`d3d11_*`, `gs_internal.h` and `runtime_flags.*`; for some minutes `d3d11_backend_impl.h` did not compile
(`prepareTerrain`, not this work) and a stale mix of objects in the private Release tree crashed the tests at start
until the gs object folders were deleted and rebuilt.

Not brought over, with the reason:

- Readback prefetch for scratch targets the game stopped drawing (`prefetchReadback`): it needs a staging copy that
  outlives the frame and a readback that can be started without being waited for; `gsr::rhi` has `readTextureAsync`,
  but the WebGPU `readbackRT` is written around a blocking read. Policy (which targets, when, the validation of a
  staged copy) should go to a shared header first; about 150 lines on the D3D11 side today.
- Exact partial uploads (`PartialUpload`, `cleanWords`, `readBitsAreClean`) and the clean-block retention across row
  refreshes (`GSR_RB_CLEAN_SYNC`): 33 of the 34 checks the WebGPU build of the suite still leaves out.
- Texture creation off the render thread: depends on which thread may create textures; wgpu-native allows it
  (`concurrentPrograms` already says so for programs), the browser proxy does not. Not attempted.
- The draw batching under motion scopes and the other DLSS-side changes the performance agent is making now.

### One suite, three APIs

`runtime/gs/tests/test_synthetic.cpp`, one source, same expectations (`testlogs/syn_*.log`, last sources):

| Backend | Checks | Failed |
| --- | --- | --- |
| Direct3D 11 (`gsr_test_synthetic`) | 395 | 0 |
| WebGPU on Vulkan (`gsr_test_synthetic_webgpu --gpu-api vulkan`) | 361 | 0 |
| WebGPU on Direct3D 12 (`--gpu-api d3d12`) | 361 | 0 |

The Vulkan run also passes at `--scale 4` and `--scale 8` and with `GSR_RT_UPLOAD_GPU=0` (361 each).
ctest at the end, private Release build of the committed tree: `gpu`, `gpu_no_backend`, `renderer_choice`,
`renderer_session`, `rhi`, `rhi_vulkan`, `ray_trace`, `ray_trace_vulkan`, `partial_upload`, `partial_upload_webgpu`,
`device_loss_webgpu`, `device_loss_vulkan`, `synthetic_webgpu_vulkan`, `rt_sync_rows`, the four settings tests,
`overlay_webgpu_test` and `native_display_commit`: 20 of 20 passed (`testlogs/ctest_subset2.log`).
`gsr_test_partial_upload`: 97 on D3D11, 64 on WebGPU.

The 34 checks WebGPU does not run, all of them counted (`#if !defined(GSR_TEST_WEBGPU)` in the source):

| Checks | What | Why it stays out |
| --- | --- | --- |
| 16 | "partial image upload avoids a GPU readback" (8 formats, applied and abandoned) | capability absent: exact partial uploads |
| 16 | "exact reads skip uploaded bits but read back untouched bits of an abandoned upload" | the same |
| 1 | "refreshing a disjoint CPU row retains the proof that uploaded palette blocks are current" | capability absent: clean-block retention |
| 1 | "three objects with identical state are one draw call" | the performance agent's batching, in progress on D3D11 |

At the start of the day the difference was 36; the guards around the row-range refresh (2), the last-page mirror (2),
the GPU frame copy (1) and the old last-page expectation are gone, and the suite grew on both sides.
The other way round, `deviceLossTest` (5 checks, `--device-loss-only`) runs on WebGPU only: Direct3D 11 has no
device-loss handling.

A stronger comparison than the suite: `gsr_replay_webgpu` (new CMake target, the capture replay tool built on the
WebGPU backend). One recorded GS stream, the Catacombs walk from boot, about 1,790 vsync frames, 3.1 GB
(`runs/b8/capture`), replayed at scale 4 on Direct3D 11 and on Vulkan: of 80 compared frames none differs by more than
41 of 255 in any pixel, and in the frame with the see-through Rynn 620 of 4,194,304 pixels differ at all, none by more
than 8 (`evidence/replay_catacombs_same_stream_d3d11_vs_vulkan.png`).

### The redder see-through Rynn

Not a blend or alpha-test difference. The blend section of `wgpu_draw.cpp` is the D3D11 one line for line (only the
enum names differ), and on the same GS stream the two backends draw her the same (above). The live runs differed
because the game faded her by the camera distance and the two runs were at different points of the walk: the frame
rate decides how far the scripted walk gets. Nothing to fix; the replay tool is the way to settle such a question.

### Device loss

Cause, as far as it could be established:

- It is not a Windows driver reset: the System event log has no `nvlddmkm` or `dxgkrnl` entry for the day, and other
  agents' Direct3D 11 games on the same GPU kept running.
- It is not exhausted video memory: 10 to 12.2 of 16 GB were in use system-wide during the failing runs.
- wgpu-native's own log at `warn` level (`DRAKAN_GPU_LOG`, new) says nothing before the abort, and the device-lost
  callback does not run. The process dies inside `wgpuSurfaceGetCurrentTexture` with "Parent device is lost".
- What fits every observation: before it hands out the next window image wgpu waits for the GPU to finish the
  previous frame, for one second at most, and treats running out of time as a lost device. A frame that takes the
  GPU longer than that (four compute passes at internal scale 8 on a GPU other agents load to 98 %) ends the process.
  The readback that the solid mirror removes had been waiting for the GPU once per frame, which is why the losses
  followed the mirror. This reading of wgpu is inferred from its behaviour and its error text; its source was not
  available on this machine.

Test of that reading, every ray effect on, automatic scale, one submission per frame, runs interleaved on one exe
(six to nine game instances on the machine):

| Build | Unlimited wait for the previous frame before the window image | No wait (`GSR_PRESENT_WAIT=0`) |
| --- | --- | --- |
| 9 (wait before every frame) | 6 of 6 runs finished | 3 of 6 lost the device |
| 10 (wait by the rule below) | 5 of 5 finished | 3 of 5 lost the device |

Every other run of the day in the old shape (no wait, one submission, mirror on) lost its device in 13 of 19 cases.
So: 11 of 11 with the wait, 19 of 30 lost without.

What was changed for it:

- `rhi_webgpu.cpp` waits, without a time limit, for the previous frame's last submission before it asks for the next
  window image (`wgpuDevicePoll` with that submission's index). Doing that before every frame is safe but not free:
  at plain settings it held the renderer to one frame in flight and cost a third to a half of the frame rate against
  no wait in four rounds of four (67.8 against 99.9, 19.7 against 35.4, 21.5 against 36.6, 16.7 against 35.5 fps;
  commit 9125492a had it that way for about two hours). The rule now (commit 9bd45d4a): wait when this frame or the
  previous one dispatched compute work (ray tracing is the only compute user in a frame), or when the GPU has two
  presented frames unfinished. `GSR_PRESENT_WAIT=2` waits always, `0` never.
- `wgpu_ray.cpp` sends each trace pass to the GPU as its own submission (`GSR_RAY_SPLIT_SUBMIT`, default on), so no
  single job holds the frame's raster work and four compute passes. Before the wait existed (build 8) that alone gave
  5 of 5 finished runs against 1 lost of 5 without it; it shortens the jobs and is kept, but the wait is the fix.
- The solid mirror is on under ray effects again (it is the shared rule now): the losses followed the mirror only
  because the readback it removes had been an accidental wait.

### Frame rate after the second pass

Same scene and method as above (Surdana start, 20 s of one-second windows, order rotated per round, 3840 x 2160,
automatic scale 8). `off` = `DRAKAN_DLSS=off` on both renderers. The play folder's `drakan.ini` had gained the keys
other work of the day added with their defaults at that time (`terrain_detail = 1`, `terrain_cliffs = 1`,
`material_detail = hard`, `lod_distance = 4.0`, `draw_distance = 1.5`); the first three change the Direct3D 11 picture
and cost only there. Logs: `testlogs/perf_b7_report.txt` to `perf_b10_report.txt`.

| Build | Machine | D3D11 off | Vulkan off | Vulkan off, no wait | Vulkan off, wait always | D3D11 ini (DLSS Quality) | Vulkan ini (temporal AA) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 7 (shared mirror, row refresh, GPU copy) | loaded | 68.9 (27.1, 67.6, 71.0, 99.5) | 67.4 (51.2, 58.8, 68.8, 111.5) | | | 30.6 | 56.9 |
| 8 (GPU replication, run join, ray split) | heavily loaded | 21.8 (9.8, 20.4, 22.3, 26.6) | 33.5 (23.6, 31.1, 38.0, 43.9) | | | 12.8 | 26.8 |
| 9 (wait before every frame) | heavily loaded | 33.7 (11.4, 20.8, 57.9, 67.5) | 21.0 (16.7, 19.7, 21.5, 67.8) | 36.7 (35.4, 35.5, 36.6, 99.9) | | 8.8 | 44.8 |
| 10 (wait by rule, the committed state) | quieter | 109.0 (24.2, 109.0, 109.7, 110.0) | 78.3 (59.1, 68.0, 96.5, 106.0) | 86.9 (63.8, 75.5, 102.3, 103.7) | 91.5 (55.3, 85.5, 94.8, 102.5) | 65.5 | 98.8 |

Median of all windows, then the four run medians.

- At equal settings the two renderers are within the noise of this machine in builds 7 and 8. In the two quiet
  rounds of build 10 Direct3D 11 made 110 and 110 fps, Vulkan 106 and 96.5: Vulkan at 0.88 to 0.97 of Direct3D 11.
- The wait before every frame cost Vulkan a third to a half only while the GPU was loaded by other programs (build
  9, four rounds of four). On the quieter machine of build 10 the rule, no wait and wait-always are not separable.
- Readbacks per run in build 10: 250 to 940 on Vulkan against 100 to 410 on Direct3D 11 (0.08 to 0.15 against 0.06
  to 0.07 per frame); GPU target copies 136 to 231 against 133 to 301. The copy gap is closed, the readback gap is
  halved since the morning (0.7 per frame then) and not closed: exact partial uploads, clean-block retention and
  prefetch are still Direct3D 11 only.
- With the owner's ini Direct3D 11 (DLSS Quality) was slower than Vulkan (temporal AA at full resolution) in every
  batch of the day, loaded or not, and slower than Direct3D 11 without DLSS. That is a Direct3D 11 / DLSS finding for
  the agent working on it, not a Vulkan result.

Handling, for the losses that still happen (a real driver reset, an unplugged adapter):

- `drakan::gpu::Context::deviceLost()`; the device and its surface are not called into again once it is set
  (`Surface::acquire`, every entry of the WebGPU `rhi::Device`, `Context::poll`). A submit polls once so a loss it ran
  into is reported before the next call.
- `BackendApi::deviceLost`, latched by `gsr::Backend` at the frame boundaries: every forwarder that reaches the device
  returns at once. `gsr::Renderer::deviceLost(&reason)` for the host.
- The host (`gs_gsr_bridge.cpp`) records the loss in `renderer_session.txt` beside the log, tells the player
  (`drakan_win::notice`: a dialog on Windows, the log elsewhere and in scripted runs) and closes the game in order.
- The next start reads the record (`gsr/renderer_session.h`): after a device loss on the shared renderer it begins on
  Direct3D 11 once, with a notice, and leaves the setting alone; the start after that tries the player's choice again.
  A session that died without a record (wgpu-native ending the process, as above) is recognised by the last words in
  the previous log (`drakan.1.log`), or by two unexplained ends in a row.
- Checked in the game with `GSR_TEST_DEVICE_LOSS=900` (`runs/b8/session`): the first run logs
  `[gpu] device lost`, `[gsr] the graphics device was lost (...): ending the session`, exits with code 0 and leaves
  `end=device-lost`; the second run prints the notice and reports `D3D11 renderer on 'NVIDIA GeForce RTX 5080'`; the
  third is back on `WebGPU renderer on '... (Vulkan)'`.

A session that ends inside the graphics library was checked with the leftovers of a real one (`runs/b10/session`:
the record `end=running` and the log of `runs/b9/wait_off_2`, which died with "Parent device is lost"): the next start
prints "The graphics device was lost in the last session (...). This session runs on Direct3D 11; the renderer
setting is unchanged." and reports `D3D11 renderer on 'NVIDIA GeForce RTX 5080'`; its record ends `renderer=d3d11`,
`end=clean`. Five attempts to make the game die that way inside one persistent folder all finished instead (the GPU
was less loaded then), so the two halves were not seen in one sequence.

What is not solved: when wgpu itself declares the device lost inside `wgpuSurfaceGetCurrentTexture`, the process
still ends there; no call of its C API reports that state first. The wait above removes the known way into it. A real
`VK_ERROR_DEVICE_LOST` at that call would still end the process, and is then caught on the next start. The device is
not recreated in the running game.

### DLSS on Vulkan

Not feasible on wgpu-native without forking it:

- NGX on Vulkan needs the application's `VkInstance`, `VkPhysicalDevice` and `VkDevice`, device extensions it names
  before the device is created, and `VkImageView`s plus a command buffer for every evaluation.
- wgpu-native v29's `wgpu.h` exposes native handles for Metal only (`wgpuDeviceGetNativeMetalDevice`,
  `wgpuTextureGetNativeMetalTexture`). There is no Vulkan or D3D12 accessor, no way to add device extensions, and no
  import or export of external images, so a second device beside wgpu's could only exchange frames through the CPU.
- The same holds for DLSS through wgpu's Direct3D 12 path, for FSR 2 / 3 (both need a raw device) and for hardware ray
  queries (`WGPUNativeFeature_RayQuery` is in the header; `Context.cpp` records that requesting it does not work in
  v29, which was not re-tested here).

What it would take instead, smallest first:

1. Truth in the UI (done): on a renderer without NVIDIA DLSS the section is "Anti-aliasing", the row is "Temporal
   anti-aliasing" and says that it runs at full internal resolution with no upscaling and no gain in frame rate, and
   that DLSS needs the Direct3D 11 renderer. A DLSS mode saved from Direct3D 11 is shown as "On (DLSS Quality on
   Direct3D 11)" and kept. The DLSS model and sharpness rows are disabled there with that reason (the WebGPU renderer
   stores the sharpness and never uses it). The `dlss` comment in `drakan.ini` says the same. `settings_schema_test`.
2. Lower render cost without any SDK: the internal scale is an integer and at 3840 x 2160 the automatic rule picks 8,
   that is 4096 rows for a 2160 row output. Mapping the quality modes to an internal scale (the performance agent
   lists "auto scale by output height, or capped under an upscaler" at 10 to 30 %) plus the existing temporal AA would
   give most of what a spatial upscaler gives here, on every backend. A plain shader upscaler (FSR 1: EASU and RCAS,
   MIT licence, two fullscreen passes in WGSL and HLSL) only pays below scale 5 at this output size.
3. Motion vectors on the WebGPU path. They block any temporal upscaler and they would fix the temporal AA on moving
   characters (it reprojects with the camera only). Half is shared already: `motion_history.cpp`, `model_motion.h` and
   `guide_camera.cpp` are in `gsr_core`, and the WebGPU backend keeps the object scopes and the history (it counts
   matched batches and uses none). Missing: a velocity program in WGSL (the D3D11 one is HLSL in `gs_shaders.h`), a
   second colour target or a second pass in `gsr::rhi` (it has one colour target per pass), the previous-position
   vertex stream, and the resolve that reads the vectors. `d3d11_world.cpp` (878 lines) is the reference and shares
   10 % of its lines with `wgpu_temporal.cpp`. Not started: it is days of work and its only consumer today would be
   the temporal AA.
4. DLSS itself on Vulkan: a wgpu-native build with a native-handle patch (a fork to maintain, a Rust toolchain in the
   build), or a renderer on a raw Vulkan device (approach (b), rejected above). Neither is proposed.

Nothing was downloaded.

### Logic still duplicated between `d3d11_*.cpp` and `wgpu_*.cpp`

Measured by lines that are identical in order after stripping comments and blank lines (`D3D11Backend` and
`WgpuBackend` treated as the same word). 3,240 of the WebGPU backend's 4,270 such lines are copies.

| Rank | File pair | Lines (D3D11 / WebGPU) | Identical | What is the same in principle | Proposed neutral home |
| --- | --- | --- | --- | --- | --- |
| 1 | `*_textures.cpp` | 943 / 918 | 887 (96 %) | the whole texture cache: keys, decode, CLUT, mip chains, HD replacement and upscale jobs, the worker pool, eviction, `findRTForTexture`. Only create, upload and bind differ | `gs_texture_cache.h/.cpp` in `gsr_core`, templated on a small `GpuTexture` handle with `create(w, h, mips)`, `upload(mip, rect, pixels)`; the D3D11 and rhi versions of those are a few lines each |
| 2 | `*_draw.cpp` | 1,030 / 830 | 751 (90 %) | draw classification (world, HUD, inventory, menu page, title, subtitles), widescreen and HUD mapping, GS state to blend / depth / alpha-test decisions, constants block, scissor | `gs_draw_plan.h`: `DrawPlan plan(const GsDrawState&, vertices, TargetFacts, FrameFacts)` returning the mapped vertices, the constants, blend / depth state in neutral enums and the passes; a backend translates the enums and issues the draw |
| 3 | `*_targets.cpp` | 987 / 573 | 428 (74 %) | target lookup and aliasing, dirty rows, row stamps, readback packing, `blocksAreClean`, `trimDirtyRows`, `beginTargetUpload`. The rules moved today are the start | finish `rt_*.h`: readback packing (`rt_readback.h` exists on the D3D11 side), partial uploads (`partial_upload.h`), then one `RenderTarget` bookkeeping struct both `RT` types derive from |
| 4 | `*_backend_impl.h` | 700 / 535 | 409 (76 %) | the `Impl` state: counters, flags, caches, the `RT` and `DepthBuf` fields | a shared `BackendState` base (everything that is not a GPU object) and a shared `RenderTargetState` |
| 5 | `*_present.cpp` | 504 / 330 | 252 (76 %) | display latch, CRT composition decisions, field modes, aspect and viewport, window shots, frame end | `gs_present_plan.h`: which circuits, rectangles and filters; a backend blits |
| 6 | `*_backend.cpp` | 311 / 263 | 221 (84 %) | `noteSceneCamera` phase machine, status strings, forwarding | move the phase machine to `gsr_core` (it touches no GPU object) |
| 7 | `*_backdrop.cpp`, `*_capture.cpp`, `*_movie.cpp` | 381 / 155 | 119 | which backdrop for which screen, capture geometry | small; fold into 1 and 5 |
| 8 | `d3d11_world.cpp` / `wgpu_temporal.cpp` | 774 / 336 | 36 (10 %) | world detection is shared in spirit; the temporal passes are different features (DLSS, neural rendering, motion vectors against a portable TAA) | not duplication; see motion vectors above |

Proposed neutral interface, in the order that pays: (1) `gs_texture_cache` and (2) `gs_draw_plan`, each as a pure
`gsr_core` unit with its own tests, used by both backends, one at a time, with the capture replay as the oracle
(same hashes before and after on both backends). No big-bang rewrite: after 1, 2 and 3 a backend is the GPU calls plus
its own features (DLSS, DXR, neural rendering on Direct3D 11). The step after that, if wanted, is a Direct3D 11
implementation of `gsr::rhi` so that the two backends become one; that is a decision for then, not a prerequisite.

A rule for new work, written into `docs/reference/graphics.md`: a rule both backends follow goes into a
backend-neutral header with a test first; a backend file only holds API calls.

### Not established in the second pass

- The reading of wgpu's one-second wait is inferred from behaviour (11 of 11 runs finished with our own wait, 19 of
  30 lost without it), not read in its source.
- A real driver-level device loss was never produced: the clean path was exercised with a destroyed device
  (`GSR_TEST_DEVICE_LOSS`) and the start-after-abort path with a real aborted run's record and log.
- The dialog itself (`MessageBoxW`) was not seen: scripted runs log the notice instead.
- The conditional wait was measured on plain settings and with every ray effect on, not with single ray effects.
- GPU replication, the GPU target copy and the row-range refresh on WebGPU are covered by the synthetic suite on
  Vulkan and Direct3D 12 and by the capture replay; they were not compared pixel by pixel against the memory path in a
  live game.
- Linux and the browser were not built or run with any of this. The browser path (`rhi_proxy.cpp`) reports no device
  loss and takes none of the native waits.
- Direct3D 11 has no device-loss handling; `BackendApi::deviceLost` is there for it.
- Material detail and terrain detail (other work of the day, Direct3D 11 shader fragments, terrain detail off by
  default since commit 9c1671e1) have no WebGPU side yet; their rules are in shared headers already
  (`terrain_detail.h`, `material_detail.h`). The play folder used here had both on in its `drakan.ini`, which is why
  the ground differs in the last Surdana pairs (`evidence/pair_surdana_*`, `evidence/b8`).
- The full runtime ctest preset was not run. `native_display_commit_test` did not link at commit 9125492a
  (`drakan_cfg::getFloat`); fixed in eb1fe1be.

Commits: 9125492a (the coordinator's commit of the day's work, with everything above up to build 9), 9bd45d4a (the
wait by rule), eb1fe1be (test stub), and the commit that adds this text.
