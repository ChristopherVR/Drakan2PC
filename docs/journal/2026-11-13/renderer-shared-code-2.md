# One copy of the GS emulation, part 2: BackendApi, presentation, target storage, images

Continues [renderer-shared-code.md](../2026-11-12/renderer-shared-code.md), which did items 1 to 3 of the ranked list in
[vulkan-backend.md](../2026-11-12/vulkan-backend.md). Owner's instruction: remove the duplicated code between the
renderer backends. This entry does item 6 (`*_backend.cpp`), item 5 (`*_present.cpp`), the rest of `*_targets.cpp` and
the small files (backdrop, capture, movie). Item 4 (`*_backend_impl.h`) and `*_device.cpp` were not started: the
coordinator kept those headers free for two other agents and then ended the work there (next steps below).
Scratch: `build/review/gs-share2/` (not committed).

## Measured duplication

Method of the previous entry (`build/review/gs-share/dup.py`): lines of a `d3d11_X` / `wgpu_X` pair identical in order
after stripping comments and blank lines, `D3D11Backend` and `WgpuBackend` treated as one word. Backdrop and movie
became one pair, `*_images.cpp`. Code lines D3D11 / WebGPU, identical:

| Pair | before (4189da39) | after item 6 | after item 5 | after targets | after images |
| --- | --- | --- | --- | --- | --- |
| `*_backend.cpp` | 386 / 325, 278 | 142 / 83, 39 | | | |
| `*_present.cpp` | 693 / 374, 291 | | 442 / 130, 52 | | |
| `*_targets.cpp` | 511 / 314, 165 | | | 373 / 214, 58 | |
| `*_backdrop.cpp` + `*_movie.cpp` | 264 / 130, 108 | | | | `*_images.cpp` 165 / 49, 22 |
| `*_capture.cpp` | 175 / 43, 28 | | | | 153 / 20, 10 |
| `*_backend_impl.h` | 764 / 530, 382 | unchanged | | | |
| `*_device.cpp` | 724 / 375, 161 | unchanged | | | |
| `*_textures.cpp`, `*_draw.cpp` | 209 / 53, 28 | unchanged | | | |
| All pairs | 3726 / 2144, 1441 | 3482 / 1902, 1202 | 3231 / 1658, 963 | 3093 / 1558, 856 | 2972 / 1454, 752 |

The shared units grew from 656 to 1,636 code lines (`gs_targets.inl` 656 -> 842, `gs_backend_api.inl`,
`gs_present.inl`, `gs_images.inl` new). On 0fb03d2d, the base of the proofs, the total was 1,444 (the device-removed
commit 4189da39 added lines to the Direct3D 11 files only).

What the 752 identical lines that remain are:

- `*_backend_impl.h` (382): the `BackendApi` override list of the two classes, the `RT` / `DepthBuf` fields both
  backends keep, and the `Impl` state and declarations of the shared units' members. This is item 4.
- `*_device.cpp` (161): `changeScale`, `applyLive`, `reset` and the texture-quality part of construction, logic that
  is still written twice. Not moved: the GPU vertex path agent was changing `wgpu_device.cpp`.
- `*_targets.cpp` (58), `*_present.cpp` (52), `*_backend.cpp` (39), `*_images.cpp` (22), `*_capture.cpp` (10),
  `*_textures.cpp` / `*_draw.cpp` (28): API boilerplate, the hook signatures (each backend defines the same hook names)
  and include / namespace lines. Examples: the `struct PC { float a[4]; uint32_t m[4]; }` present constants and their
  eight assignments in `drawWindowImage` (the Direct3D 11 one also chooses the upscaler's window-sized view), the
  `RectConsts` / viewport / scissor lines of the two GPU replication paths, `upscaleCut` (each backend resets its own
  temporal history: DLSS and neural state against the portable TAA), the present-wait forwarders, `createXBackend`.

## Structure

Same pattern as items 1 to 3: one text compiled once per backend, defining members of `GSR_IMPL` (and, for the
BackendApi class, of `GSR_BACKEND`), included by each backend's file. No virtual call was added.

| Unit | Holds | Backend file |
| --- | --- | --- |
| `gs_backend_api.inl` (new) | the BackendApi forwarders, construction and destruction, the scene-camera phase machine (`noteSceneCamera`: water pass, item grid, subtitle, world begin / end markers, world and GUI cameras, motion epochs), object motion scopes, level changes (`noteSceneGeometry`), scene models, the ray-tracing status lines | `d3d11_backend.cpp`, `wgpu_backend.cpp` |
| `gs_present.inl` (new) | `renderDisplay` (circuits, PMODE, save preview and menu hold copies, the loading screen's held menu), `latchDisplay`, `present`, `presentLatched` (window viewport and letterbox, the overlay's context, window screenshots, last presented ray / water state), `clusterHud`, `endFrame` (cache sweep and eviction, target and depth ageing, `GSR_RTDUMPF`) | `d3d11_present.cpp`, `wgpu_present.cpp` |
| `gs_targets.inl` (extended) | also `allocRT` bookkeeping, `clearInventoryCanvas`, `uploadTargetRect` (memory read, GPU replication or CPU expansion, `GSR_RT_REFRESH_STATS`), `copyAliasedBlocks`, `getDepth`, the item grid capture | `d3d11_targets.cpp`, `wgpu_targets.cpp` |
| `gs_images.inl` (new) | captures (`capture`, `captureLastGameplay`, `captureFramebuffer`), HD movie frames (`drawMovieFrame`), wide menu backdrops (`wideBackdropSet`, `drawWideBackdrop`) | `d3d11_images.cpp`, `wgpu_images.cpp`, which replace `*_backdrop.cpp` and `*_movie.cpp`; `*_capture.cpp` keep `readTexture` and Direct3D 11's diagnostics |

### The interface as it ended up

The previous entry's members (`createSampledTexture`, `clearTarget`, `bindDraw`, `setPass`, `beginReadback`, ...)
plus these. Because `d3d11_backend_impl.h` and `wgpu_backend_impl.h` were not to be edited before item 4, the new API
halves are free functions over the Impl in namespace `gsr::backend_hook`, defined by the backend file before it
includes the unit (so they can return the backend's own types and inline). They are temporary: item 4 makes them
members of the Impl, a rename. Each unit's header comment lists its hooks.

| Unit | Hooks |
| --- | --- |
| `gs_backend_api.inl` | `constructed` (Direct3D 11 wires `BackendApi::lostNow`), `releaseBeforeWorkers`, `markGpuPass`, `worldPhaseOpened`, `motionWorldOpened`, `levelHistoryReset`, `sceneModelsNoted`, `rayDeviceMissing`, `acquireRayPass`, `rayPassOf`, constants `kRayReflectionsName`, `kRayImageSuffix` |
| `gs_present.inl` | `gpuPassScope`, `keepSavePreview`, `keepMenuHold`, `drawDisplay`, `drawMenuHold`, `kNeuralRendering`, `deviceGone`, `latchCompare`, `displayLatched`, `acquireWindowImage`, `skipPresent`, `drawWindowImage`, `overlayDevice`, `gridCaptureView`, `afterOverlay`, `readWindowImage`, `presentWindowImage`, `frameEnding`, `endFrameWorlds`, `frameAdvanced`, `frameEnded` |
| `gs_targets.inl` | `replaceTargetTexture`, `clearCanvasMargins`, `targetUnusable`, `replicateTargetRect`, `writeTargetRect`, `nativeTargetSource`, `copyTargetBlock`, `createDepthTexture`, `prepareGridCapture`, `saveGridRect`, `restoreGridRect`, `gridRect` |
| `gs_images.inl` | `readImage`, `maxImageSize`, `movieProgramReady`, `movieImageLoaded`, `createMovieTexture`, `writeMovieImage`, `drawMovieImage`, `createBackdropTexture`, `backdropProgramReady`, `drawBackdrop` |

Most hooks are API calls. A few carry a Direct3D 11-only feature where WebGPU has none and the hook is empty:
`latchCompare` / `afterOverlay` (the matched compare and world-shot diagnostics), `displayLatched` (the upscaler's
window-sized view, `renderViewImage`), `endFrameWorlds` / `frameAdvanced` (DLSS's world phases and fault),
`worldPhaseOpened` / `levelHistoryReset` / `sceneModelsNoted` (neural rendering and model motion state). They are the
places where those features would hook in on WebGPU.

### The device-removed checks survived the move

The Direct3D 11 device-removed work (4189da39) landed while these steps were being proven; the steps were rebased onto
it. Every helper call of that commit in the files that moved is still there, counted over `d3d11_*.cpp` and the shared
units before and after: `deviceFailed` 9, `deviceOk` 17, `gone()` 28, `map` 18, `probeDevice` 8, `testLoss` 7,
`testLossFrameEnd` 2. Where a moved function had an early exit, a hook carries it: `presentWindowImage` has
`testLoss(Present)` then `deviceOk(hr)`; `replaceTargetTexture` keeps the clear, copy and assign inside the creation
check; `latchDisplay`, `presentLatched`, `endFrame`, `uploadTargetRect` and `getDepth` keep their `gone()` exits
through `deviceGone`, `acquireWindowImage`, `frameEnding`, `targetUnusable` and `createDepthTexture`; `drawDisplay` and
`drawWindowImage` return false after a failed `Map` and the shared code returns where the Direct3D 11 code returned.
WebGPU's hooks return "not gone": its lost device is latched by `gsr::Backend` at the frame boundary, as before.

## Deviations from "moved verbatim"

None of these is visible in a frame hash.

- Diagnostics written for one backend now run on both: `GSR_BACKDROP_LOG`, the `GSR_RT_REFRESH_STATS` refresh table
  (WebGPU), `GSR_RT_UPLOAD_SPECIALIZE=0` (WebGPU now honours it; the expansion is identical either way),
  `GSR_HUD_CANVAS_LOG` prints `method=` on both (WebGPU: `drawRect`).
- Window screenshots (`GSR_WINSHOT`): a shot is announced when the image was read, on both backends (WebGPU announced
  it only when the PNG was written). A requested screenshot (`shot` in a test script) that fails prints the window
  size on both (Direct3D 11 printed 0x0).
- WebGPU `capture()` returns false when the display texture could not be created, instead of dereferencing it.
- The Direct3D 11 matched-compare and world-shot diagnostics stay Direct3D 11 only; the world shot is taken right after
  the overlay instead of after the window screenshots (the same image).
- The CPU profiler counts the unbinding of the window image inside `PresentCall` on Direct3D 11.
- The item grid capture no longer stores its rectangle when the rectangle is empty (it is never read then).

## Proof

Oracle: the capture replay, every frame hashed (`gsr_replay` for Direct3D 11, `gsr_replay_webgpu --gpu-api vulkan`
and `--gpu-api d3d12`), five captures (`surdana` 2,585 frames, `cave` 5,558, `flight` 1,864, `tab` 3,984, `ootf`
4,074), scales 1, 3 and 8, HD textures off and on (frozen set `build/review/gs-share/textures_frozen`): 90 runs and
325,170 frames per full matrix, compared file by file. Baseline: 0fb03d2d, 90 of 90 runs with the full frame count.

| Tree | Scales | Runs identical to the baseline | Frames |
| --- | --- | --- | --- |
| first chain on 0fb03d2d, all four steps (superseded by the rebase) | 1, 3, 8 | 90 of 90 | 325,170 |
| first chain, each step | 1, 3 | 60 of 60 for present and targets; item 6 56 of 60 plus 4 hash files cut short afterwards by a replay of mine that I ended (not differences) | |
| rebased onto 4189da39, all four steps (= main after c6202466) | 1, 3, 8 | **90 of 90** | **325,170** |
| rebased, item 6 (fbdf631a) | 1, 3 | 60 of 60 | 216,780 |
| rebased, present (aba4df6a) | 1, 3 | 60 of 60 | 216,780 |
| rebased, targets (335b03fe) | 1, 3 | 24 of 60 finished, all identical; the rest stopped on the coordinator's word | 83,946 |
| rebased, images (05cd3dc2) | 1, 3, 8 | its tree is the cumulative tree | |

4189da39 (Direct3D 11 device-removed handling) lies between the baseline and the rebased tree; its author proved it
frame-identical to 0fb03d2d (50 of 50 runs). The steps were applied to main on d5860d86 unchanged; each step's tree
was rebuilt there and compiled, and main after c6202466 equals the proven tree (empty `git diff`).

Conditions, stated because the previous entry's were wrong once: the GPU (16 GiB) was shared with three to four other
agents' replays, games and tests all day. The NVIDIA driver reset at 08:30:22 to 08:30:28 (System log, nvlddmkm
event 153, five events) with about 15.4 to 15.8 GiB in use machine-wide: four of my replays were running then, and a
ctest of mine with GPU tests; two scale-8 baseline replays died ("Parent device is lost") and were re-run. From 08:34
the coordinator limited me to two replays and one at scale 8 (two at scale 8 below 10.5 GiB from 15:49); my runner
starts a replay only below 10.5 GiB (and while no `scene_ab_bench.py` turn lock is held) and ends its own newest
replay above 14.8 GiB (9 times in the first matrices). A chained script of mine that should have been stopped in the
morning ran a further matrix with the old limits (four replays, two at scale 8) from 14:16 to 14:50, at most 13.0 GiB
machine-wide; its results were discarded. Rebased matrices: 650 memory samples at 15 s, above 12 GiB in 5, at most
13.7 GiB, no driver reset after 08:30. Every counted run exited 0 with the full frame count (`checkok.py`).

Tests, each step's rebased tree (private MSVC Release, runtime-tests configuration with `GSR_DLSS=OFF`):
`gsr_test_synthetic` 395 checks on Direct3D 11, `gsr_test_synthetic_webgpu` 394 on Vulkan and 394 on Direct3D 12,
none failed; the whole ctest 190 of 190 passed (7 data-dependent tests skip: hle_test, fmv_vdb, gameui_ram_test,
world_ram_test, world_pose_ram_test, live, spu2_test). Earlier, on the first chain, a ctest run beside the 08:30
driver reset failed synthetic_webgpu (timeout) and vertex_records_webgpu_d3d12 (crash), both during the reset.

Linux: the WebGPU backend's translation units (`wgpu_*.cpp`, `backend_select_webgpu.cpp`; they include every shared
unit) pass a syntax-only compile with clang 14 and g++ 11 against Linux headers under WSL (Ubuntu 22.04), on the
final tree; the only warnings are those 0fb03d2d has too (`rhi.h` unused parameters, three lambda captures in
`gs_texture_cache.inl` with clang). Not linked, not run: the root `linux-runtime-tests` preset needs Python 3.11
(WSL has 3.10) and a Linux wgpu-native download. The Direct3D 11 units were compiled with MSVC only; the browser
build was not made.

## Renderer thread CPU time

Cave capture, scale 3, HD textures (frozen set), whole capture, renderer-thread CPU ms per frame, the five builds
interleaved, three repeats, other agents' work on the machine. First chain (on 0fb03d2d), median (range):

| Backend | base | item 6 | + present | + targets | + images |
| --- | --- | --- | --- | --- | --- |
| Direct3D 11 | 2.167 (2.153 to 2.229) | 2.111 (2.078 to 2.274) | 2.128 (2.106 to 2.173) | 2.137 (2.055 to 2.471) | 2.145 (2.092 to 2.370) |
| WebGPU on Vulkan | 2.387 (2.359 to 2.631) | 2.432 (2.350 to 2.440) | 2.398 (2.302 to 2.592) | 2.339 (2.328 to 2.589) | 2.437 (2.412 to 2.612) |

Every after-median is within 2.6 % of the base median (lower or higher) and the ranges overlap. The rebased tree was not timed
separately (its addition is the device-removed checks, which their author timed: no rise).

## Live presentation checks

What the headless replays cover of the present code: `renderDisplay` and the display image (the replay tool hashes
it), `latchDisplay` without the upscaler view, `endFrame`. Not covered: the window image, its viewport and letterbox,
the overlay, screenshots, the present call, window modes. Those were checked in the game, before (a build of
0fb03d2d) and after (a build of c6202466), with the same scripted run (`build/review/gs-share2/live/present.txt`,
`live.py`; `play_run.py` on the play folder `build/review/gs-share2/play`, a copy of `baseline-play`, borderless
3840 x 2160, DLSS Quality, `GSR_PROFILE=2`): a start in Surdana, vsync on, frame limit 30, aspect 4:3, HUD scale 0.8,
F1, quick save (F5), window mode windowed, a resize to 1600 x 900 client pixels, minimise for 3 s and restore (from
outside the game, Win32), borderless again, pause. Runs: Direct3D 11 with DLSS Quality, Direct3D 11 with DLAA
(`DRAKAN_DLSS=dlaa`), WebGPU on Vulkan with temporal AA; plus the boot movies with HD movie frames on both renderers
(`GSR_WINSHOT` at vsync frames 100 to 800).

Result: every run ended at its scripted quit with exit 0 and no error or device line, every shot was written, the
window shots have the same sizes before and after (windowed 1280 x 960, resized 2402 x 1353 at the desktop's scaling,
borderless 3840 x 2160), and the pictures match shot for shot (pillar box at 4:3, the resized window, the pause menu,
the HUD); the scene differs only by where the scripted walk had got to. The HD movie frames (Surreal logo) are drawn
on both renderers before and after. Images: `build/review/gs-share2/live/sheet_d3d11.png`, `sheet_d3d11_dlaa.png`,
`sheet_vulkan.png` (before left, after right), the runs in `live/runs/b_*` and `a_*`, movie frames in `live/movie/`.

Frame pacing from `[gsr-profile]` (presents per second in 2 s windows, median; the longest `presentLatched` per
window, median): Direct3D 11 73.5 to 132.0 per s, 14.9 to 6.1 ms; DLAA 63.3 to 75.5 per s, 39.3 to 13.4 ms; Vulkan
51.6 to 144.4 per s, 94.2 to 28.9 ms. The after runs were faster, but the before runs were taken in the morning while
my matrices and other agents' work loaded the GPU and the after runs on a quieter machine, so this does not measure
the change; the replay CPU table above does.

Not seen in these checks: the F1 settings overlay does not appear in either the before or the after shot (the
scripted tap and the shot timing; the overlay itself was not looked at), and the save preview of the quick save was
not opened in a save screen. Full-screen exclusive mode was not tried.

## Direct3D 11-only capabilities after the moves

| Capability | Where its logic is now | Available to WebGPU with API calls only? | What is missing on WebGPU |
| --- | --- | --- | --- |
| Readback prefetch (`kCaps.readbackPrefetch`, `prefetchReadback`) | the call in `endFrame` is shared (`gs_present.inl`, behind the flag); the policy (which targets, sizes, staleness) and the staged copy are in `d3d11_targets.cpp`, mixed line by line | no | a copy that is started now and read later: `rhi::Device::readTextureAsync` exists, but each call submits the recorded work and the result arrives through `poll`; the WebGPU `RT` would need a per-target staged buffer and its writes stamp (a field in `wgpu_backend_impl.h`), and `beginReadback` would have to wait for a pending read. Policy and calls first have to be separated (about 50 lines) |
| Texture creation off the render thread (`kCaps.texturesOffThread`) | shared (`gs_texture_cache.inl`, `pushDone`) | no | `rhi::Device::createTexture` + `writeTexture` record into the render thread's command stream and are not thread-safe; needs a thread-safe create-with-pixels in `gsr::rhi` (wgpu-native allows it natively, the browser proxy does not) |
| Motion-scope batching (`deferObjectMotion`, `setMotionNotes`) | the scope bookkeeping is shared (`applyObjectScope`, `gs_backend_api.inl`); the deferral and the notes stay in `d3d11_backend.cpp` | no | it only pays with motion vectors: a velocity program in WGSL, a second colour target per pass in `gsr::rhi`, the previous-position stream (`kCaps.motionVectors`) |
| Terrain detail (`kCaps.terrainDetail`) | rules in `terrain_detail.h`, the call in `gs_draw.inl`; `prepareTerrain` (`d3d11_terrain.cpp`, 430 lines) mixes rules with constant-buffer and SRV binding | no | the HLSL fragment `gs_terrain.inc` has no WGSL counterpart; the detail textures and their binding slots |
| Material detail (`kCaps.materialDetail`) | rules in `material_detail.h`, loading shared (`gs_texture_cache.inl`); `prepareMaterial`, `ensureMaterialGpu` in `d3d11_material.cpp` | no | `gs_material.inc` (HLSL) has no WGSL counterpart; the material map binding |
| DLSS (`kCaps.temporalUpscaler`) | the world phase machine and the latch are shared; the evaluation is NGX | no | NGX needs a raw D3D11/D3D12 or Vulkan device; wgpu-native v29 exposes none (vulkan-backend.md) |
| Hardware ray tracing (DXR water, shadows, reflections) | status lines shared (`gs_backend_api.inl`); WebGPU already runs the same effects as compute ray tracing (`wgpu_ray.cpp`) | no | `WGPUNativeFeature_RayQuery` did not work in v29; hardware ray queries need a raw device |

None of them is a few API calls away on WebGPU; none was implemented.

## Next steps (not done)

1. Item 4, `*_backend_impl.h` (382 identical lines): a shared member block or state base for the `Impl` (counters,
   caches, flags, scene and motion state), a shared `RT` / `DepthBuf` field set, the `BackendApi` override list once,
   and the `backend_hook` functions turned into `Impl` members (a rename per hook). Care points: member order is
   construction and destruction order (`surfaceBuilder` after `rts` and `residentScene`; `startupTitle` reads `cfg`),
   field defaults differ (`neuralZmax`), and some members have different types per backend (`motionHistory`:
   `PairedHistory` on Direct3D 11, `History` on WebGPU; `wgeom`). Estimate: about a day of work plus a full proof
   matrix (about 6 h on the shared card at the current limits).
2. `*_device.cpp` (161 lines): `changeScale`, `applyLive`, `reset` and the texture-quality part of construction into
   a shared unit, with hooks for device recreation. Estimate: half a day plus a proof matrix.
   `D3D11Backend::deviceLost` (`d3d11_device_loss.cpp`) can join `gs_backend_api.inl` then.
3. The remaining API boilerplate (present constants, replication passes) is small; a `gsr::rhi` implementation for
   Direct3D 11 would remove it, which is the larger decision the previous entries left open.

## Commits

fbdf631a (item 6), aba4df6a (item 5), 335b03fe (targets), 05cd3dc2 (images), c6202466 (deletes `d3d11_backdrop.cpp`,
which 05cd3dc2 had left: my landing script listed the patch's files with rename detection and missed the rename
source; the file was no longer compiled), and the commit adding this entry.

## Not verified

- Linux and browser builds were not linked or run; clang was used only for a syntax check of the WebGPU units; the
  Direct3D 11 units were not compiled with clang.
- A Debug build was not made.
- The targets step's own tree has 24 of 60 runs at scales 1 and 3 (all identical); its frame identity rests on the
  cumulative tree.
- DLSS frames are not in the replays (the replay tools run without DLSS); DLSS Quality and DLAA were looked at live
  only, in short scripted runs.
- The device-removed paths that now run through hooks were not provoked in the game after the move; the ctest
  device_loss_d3d11_frame / _map / _texture tests passed on each step's tree.
- Movies beyond the boot logos, saving and loading beyond one quick save, long play and multiplayer were not
  exercised.
