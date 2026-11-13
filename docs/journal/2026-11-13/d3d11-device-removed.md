# A removed Direct3D 11 device: a notice and a clean end instead of a crash

Owner's request: "a notice and a clean end, not a crash." The Direct3D 11 renderer had no handling for a removed
device (driver reset, a hang Windows detects, overheating, exhausted GPU memory, another program). This entry records
what was verified in the code, the design, each layer of proof and what it rests on, the call sites, the option of
recreating the device, and what is not established.

Scratch (not committed): `build/review/d3d-reset/` (`wt` worktree, `rt` private build, `bin_base` / `bin_new`
binaries, `hashes/`, `logs/`, `runs/`, `recipes/`, `play/` private play folder with its own ELF copy under `elf/`).

## What the code did before (verified at 0fb03d2d)

| Claim handed over | Verified |
| --- | --- |
| No `DXGI_ERROR_DEVICE_REMOVED` / `GetDeviceRemovedReason` handling in `d3d11_*.cpp` | True. The only `GetDeviceRemovedReason` calls were in `ray_water.cpp`, on its own D3D12 device. |
| `BackendApi::deviceLost` not implemented by Direct3D 11 | True: the base answered "not lost", so the latch in `gsr::Backend` never closed. |
| The `Present` result only feeds a status flag | True (`neuralPresentation.present`, `lastPresentedWater`, `lastPresentedRay`). |
| Unchecked `Map` in `beginVertices` and `setPass` | True, both still unchecked at 0fb03d2d: the reported fix of one of them by another agent is not in the tree (`git log` of `d3d11_draw.cpp` has none). Two more unchecked Maps were found: the display constants in `renderDisplay` and the present constants in `presentLatched`. |

Beyond the Maps, several `Create*` results were used without a check and would hand a null object to a context call
(`ClearRenderTargetView`, `CopySubresourceRegion`, `GetDesc`): `allocRT`, `ensureStaging`, `ensureDisp`, `snapshot`,
`makeSwapRtv`, `makeTex`, the draw vertex buffer and the pipeline objects of `init`.

## Design

Policy is backend-neutral; only the API checks are in `d3d11_*`.

- `runtime/gs/src/device_loss.h` (new, standard C++, unit test `device_loss_policy`): which results mean a removed
  device (`DXGI_ERROR_DEVICE_REMOVED`, `DXGI_ERROR_DEVICE_HUNG`, `DXGI_ERROR_DEVICE_RESET`,
  `DXGI_ERROR_DRIVER_INTERNAL_ERROR`; any other failure, such as a failed Map or out of memory, counts only when
  `GetDeviceRemovedReason` then reports a removal, and the device's reason is preferred), the plain words for each
  cause, and the test hook's switches (`GSR_TEST_DEVICE_LOSS`, `GSR_TEST_DEVICE_LOSS_SITE`).
- `gsr/renderer_session.h` (extended, unit test `renderer_session`): the record counts losses in a row on one
  renderer (`lost=` line; older records read as 0), `lost()` records a loss, `lossNotice()` chooses the player's text,
  `lossFallsBack()` says whether the next start changes renderer. The host (`gs_gsr_bridge.cpp`) now uses these for
  both backends instead of its own text.
- `BackendApi::lostNow` (`gs_internal.h`): an atomic flag a backend sets, from any thread, when it finds its device
  gone in the middle of a frame. `gsr::Backend` (`backend_select.inl`) reads it on every forwarded call (one relaxed
  load, no virtual call) and latches `m_dead` at once, instead of at the next frame boundary. This is the existing
  latch closing earlier, not a second mechanism. WebGPU never sets the flag and is latched exactly as before.
  `beginPresentWait`, `endPresentWait` and `upscaleCut` are now latched too (a swapchain waitable object may never be
  signalled again on a lost device).
- `d3d11_device_loss.cpp` (new): `deviceOk(hr, site)` (true on success; on a failure asks the device and latches a
  removal once, logged with the call result, the removed reason, the code name, the plain words and the thread),
  `map(...)` (Map with that check and the hook), `probeDevice(site)` (after a failure that carries no HRESULT, such as
  a DLSS evaluation), the hook, and `D3D11Backend::deviceLost`. The Impl's `gone()` reads the owning backend's
  `lostNow`.
- Within the one call in which a removal is found, the rest of that call must not use a failed result: the Direct3D
  11 functions that would (target, depth, display and staging creation, readbacks, uploads, the world, DLSS and
  motion passes, latch and present) return early on `gone()`. Draws whose Map failed get a CPU copy of their vertices
  (`vbFallback`) so the shared draw code (`gs_draw.inl`, unchanged) never writes through a null pointer, and the draw
  is not issued.
- The end is the WebGPU path's: the host's `hostTick` sees `Renderer::deviceLost`, writes `end=device-lost` to
  `renderer_session.txt`, shows the notice (`drakan_win::notice`: a dialog on Windows, the log in scripted runs) and
  asks the window to close; the game ends in order and exits with code 0.

### The notice

Title: "Drakan: the graphics card stopped working". Text for a first loss on Direct3D 11 (cause from
`GetDeviceRemovedReason`, here `DXGI_ERROR_DEVICE_HUNG`):

> The graphics card stopped working: it took too long to answer and Windows restarted the graphics driver.
>
> The game will close now. Progress since your last save is lost.
>
> If this happens again: update your graphics driver, lower the Internal render scale in the settings (F1), or turn
> off the ray-traced effects (shadows, reflections, global illumination).

A second loss in a row on the same renderer replaces the last paragraph with "This has happened in 2 sessions in a
row. Please update your graphics driver before you play again. If it still happens, lower the Internal render scale
in the settings (F1) and turn off the ray-traced effects (shadows, reflections, global illumination)." A WebGPU loss
shows the generic cause (wgpu-native's own text stays in the log and the record) and adds "The next start uses the
Direct3D 11 renderer once; your renderer setting is not changed." The next start after a WebGPU loss shows "The graphics card
stopped working in the last session. This time the game uses the Direct3D 11 renderer; your renderer setting is not
changed." (before: "The graphics device was lost in the last session (<library text>) ..."). The names "Internal
render scale" and the ray-traced rows are the settings menu's own labels. The unit test checks for no em-dash, no "DXGI" and no "device".

### What the next start does after a Direct3D 11 loss: nothing different

Decided: the next start uses the player's choice unchanged and shows no start notice.

- There is no safer renderer to fall back to. Direct3D 11 is the default and the one the WebGPU path falls back to;
  the WebGPU renderer has its own losses (the wait rule of `vulkan-backend.md`) and lacks DLSS and the hardware ray
  passes, so switching to it would change the game the player chose without making a reset less likely.
- Turning settings down automatically (internal scale, ray effects) would change the player's settings on a guess:
  the cause of a reset is often outside the game (driver update, another program).
- The player was told at the moment of the loss. A second loss in a row says so and puts the driver update first,
  which is the "suggestion" the request asked for; the count is in the record (`lost=`).

## Proof layers

Which claim rests on which layer:

| Layer | What it shows | What it does not show |
| --- | --- | --- |
| Unit tests (`device_loss_policy`, `renderer_session`) | the result classification, the words, the hook's parsing, the record's loss count, the notice text and the next-start decision | anything about the driver |
| Forced loss in the synthetic suite (`device_loss_d3d11_frame` / `_map` / `_texture`, beside `device_loss_webgpu` / `_vulkan`, one source) | the latch, that forwarding stops right after the call that found the loss (or within the frame for a texture created on a worker), that nothing reaches the backend afterwards, captures fail cleanly, the reason is plain words | a real removed device: the hook makes the checked call report `DXGI_ERROR_DEVICE_REMOVED`, the real device keeps working |
| Forced loss in the real game (below) | the host's ordered end at each site: notice, record, exit code, log | the same: the device stays alive, so a call the checks missed would not crash here |
| Static audit (call-site list below) | every call whose failure would be used is checked or guarded | that the list is complete beyond the files read |
| Real removal (`dxcap -forcetdr`) | not run: see below | |

### Unit tests and the synthetic suite (forced loss)

- `device_loss_policy`, `renderer_session`: pass.
- `device_loss_d3d11_frame`, `_map`, `_texture` (new) and `device_loss_webgpu`, `device_loss_vulkan`: 9 / 9 / 9 / 8 / 8
  checks, none failed. Each frame draws three separate textured sprites with textures uploaded that frame; the loss
  is in frame 1. `Stats::drawCalls` (counted inside the backend's draw) stops at 6 (frame site: end of frame 1), 4
  (map: right after the draw whose Map failed) and 4 or 5 (texture: the first texture creation of frame 1 can run on a
  texture worker, and then the next forwarded call stops). Nothing more reaches the backend afterwards (draw count and
  texture decodes unchanged over two more frames, an upload, a flush, a readback and a capture).
- The same cases on the software adapter (`--warp`, "Microsoft Basic Render Driver"): pass in 0.4 s. They do not
  depend on the adapter's rounding, so CI runs the whole case, not a reduced one.

### The real game (forced loss)

Private play folder (`build/review/d3d-reset/play`, a copy of `baseline-play` with its own ELF copy), `renderer =
d3d11`, DLSS Quality active (`[dlss-sr] upscaler feature created`), HD textures, 3840 x 2160; `play_run.py` with
`--attempts 1` (the harness wants a scripted quit, so it reports these runs as not ok; the exit code and the log
decide). Build of 4189da39 plus the host change (later builds add the two fixes below).

| Run | Hook | Where the loss was found | Exit | Notice | Record |
| --- | --- | --- | --- | --- | --- |
| r1 | frame 3200 | end of a gameplay frame, 4 s after control (frame 2608) | 0 | yes | `end=device-lost lost=1` |
| r2 | map 3200 | `draw vertices` in gameplay (control at 2380) | 0 | yes | same |
| r3 | present 3200 | `Present` in the intro cut scene (the frame counter ran ahead) | 0 | yes | same |
| r3b | present 4200 | `Present` in gameplay, 6 s after control (frame 3352) | 0 | yes | same |
| r4, r4b, r4c_1, r4c_3 | texture 1950 / 2150 / 2050 | texture creation on the title screen, in the intro scene, in a menu | 0 | yes | same |
| r4c_2 | texture 2050 | texture creation during the loading screen of the intro level (`gui screen 8 -> 14`, `Level 0 - Intro loaded`, then the loss) | 0 | yes | same |
| r5 | upscale 3200 | right after a DLSS evaluation in gameplay (control at 2639) | 0 | yes | same |
| r6 | present 6000 | `Present` with the settings overlay (F1) open over the pause menu (`[settings-harness] 2 toggle ok` 37 s before, `state.txt` `open=1`, shot `runs/r6_shots/overlay_open.bmp`) | 0 | yes | same |
| s1, s2 | map 3200, same log folder | gameplay, twice in a row | 0, 0 | second one: "This has happened in 2 sessions in a row ..." | `lost=1`, then `lost=2` |
| s3 | none, same folder | normal run after two losses: no start notice, Direct3D 11, scripted quit | 0 | no | `end=clean` (`lost=2` left in the record: fixed, see below) |
| c1, c2 | frame 3200, then none | loss, then a normal run | 0, 0 | yes, no | `lost=1`, then `end=clean abnormal=0 lost=0` |
| w1 | WebGPU on Vulkan (`PS2X_GS=vulkan`), frame 3200 | end of frame (the WebGPU hook) | 0 | yes, with "The next start uses the Direct3D 11 renderer once" | `renderer=webgpu end=device-lost lost=1` |
| w2 | none, same folder | next start on Direct3D 11 with the start notice | 0 | start notice | `renderer=d3d11 end=clean` |
| r0 | none | baseline: gameplay, scripted quit | 0 | no | (the run's log folder did not exist; s3/c2/w2 cover a clean record) |

Every run: no crash dump, no access violation or exception line, an ordered shutdown (`[run] shutdown: threads 41 to 53
ms, renderer 31 to 532 ms`) and the `[log] flush marker`. Between the loss and the exit the log shows only the
latch, the notice and the shutdown, with two exceptions found and fixed:

- r5 (DLSS): "[dlss-sr] SR depth restore failed" after the loss: the world pass went on within the call that found the
  loss, `map` refused the depth constants and the fallback then issued a `CopyResource`. Harmless on a removed device,
  but against the rule; the two world passes now return right after a loss found at the evaluation.
- s3: the clean end left `lost=2` in the record (no effect on the next start, which only carries the count after a
  loss). `gsr::session::cleanEnd` now clears both counts; c1/c2 show `lost=0`.
- w2: the start notice after a WebGPU loss was the older text and showed the player the library's words ("The
  graphics device was lost in the last session (destroyed on request (test))"). It is now plain words; the reason
  stays in the record and in the log line.

After the fixes (build with commit 3's code):

| Run | Hook | Where | Exit | Log between loss and exit | Record |
| --- | --- | --- | --- | --- | --- |
| r5b | upscale 3200 | right after a DLSS evaluation in the intro scene's world (before control) | 0 | latch, notice, shutdown only | `lost=1` |
| w1b, w2b | WebGPU loss, then the next start | as w1 / w2 | 0, 0 | | `renderer=webgpu end=device-lost`, then `renderer=d3d11 end=clean lost=0` |

w2b's start notice: "The graphics card stopped working in the last session. This time the game uses the Direct3D 11
renderer; your renderer setting is not changed." The log line adds "(previous session: destroyed on request (test))".

Not seen: the dialog itself. Scripted runs write the notice to the log (`drakan_win::notice` skips `MessageBoxW` under
`DRAKAN_INPUT_SCRIPT` / `DRAKAN_NO_ACTIVATE`), so whether the dialog shows in front of a borderless fullscreen window
was not checked. r4b logged "[hle] post(sid=0x1310 fno=0): guest handler 0x1e6208 did not return within 20 s" 0.5 s
after its loss, at 7.4 s into the run; it cannot be a 20 s wait that began at the loss, and it was not investigated.

### A real removal (not provoked)

During the replay matrix the NVIDIA driver reset the GPU at 08:30:22 to 08:30:28 (`nvlddmkm` event 153, five times),
with about 15 GiB of 16 in use by every agent's replays (including two of this work's runs started by the runner
mistake below). At that moment:

- the base binary's Direct3D 11 replay (`cave` scale 3) ended with exit code 139 (an access violation), the crash this
  work removes;
- this work's Direct3D 11 replay (`cave` scale 1, built before the thread id was added to the log line, otherwise the
  same code) logged `[gsr] Direct3D 11 device removed at texture readback: call result 0x887a0005, removed reason
  0x887a0005 DXGI_ERROR_DEVICE_REMOVED (...)`, replayed the rest of the capture without the device (hashes stop at
  3,369 of 5,558 frames, readbacks fail after the loss) and exited 0;
- two WebGPU processes ended too (a base `gsr_replay_webgpu` with 127, and the private ctest's
  `vertex_records_webgpu_d3d12` with 0xc0000409).

So the headless detection path (Map result, `GetDeviceRemovedReason`, latch, no crash) is shown on a real driver
reset, once. The game path (host, notice, record) after a real reset is not: the replay tool has no host.

### No behaviour change when nothing fails

- Replays, base 0fb03d2d against the change applied on 0fb03d2d, every frame hashed (`gsr_replay --hashes`), five
  captures (surdana 2,585, cave 5,558, flight 1,864, tab 3,984, ootf 4,074 frames): Direct3D 11 at scales 1, 3 and 8
  with HD textures off and on (frozen set `build/review/gs-share/textures_frozen`), WebGPU on Vulkan and on Direct3D 12
  at scale 3, HD off and on. 50 of 50 runs identical, 180,650 frames per side, every run exit 0 with the full count.
  One replay at a time; GPU memory system-wide (other agents' jobs included) median 8.2 GiB, peak 14.7 GiB, above 12
  GiB in 34 of 811 samples.
- Renderer thread CPU (`gs_replay_ab.py`, cave, scale 3, HD on, three interleaved repeats): base 2.347 ms per frame
  (2.283 to 2.392), new 2.294 (2.218 to 2.297). The checks add a branch per draw or per call, none per vertex or
  pixel.
- Synthetic suite: 395 (Direct3D 11), 394 (Vulkan), 394 (Direct3D 12), none failed, on the base and with the change,
  and again on 2caaafdf.
- Private ctest (185 tests): 183 passed in the full run; `synthetic_webgpu` timed out (300 s) on the loaded GPU and
  `vertex_records_webgpu_d3d12` died in the driver reset above; both passed when run again alone.
- The two later fixes (world passes returning after a loss at the evaluation, `cleanEnd`) only act after a loss or at
  the session's end; they were checked with the suite and the forced-loss tests, not with a second replay matrix.

### Real removal on demand: not done

`dxcap.exe` is installed (`C:\Windows\System32`), so `dxcap -forcetdr` could reset the driver. It resets it for
every process on the GPU, and other agents' GPU jobs ran all day, so it was not run; it needs a window from the owner.
Nothing was installed.

## Call sites

Changed or wrapped (all in `runtime/gs/src`):

| File | Call | Before | Now |
| --- | --- | --- | --- |
| `d3d11_draw.cpp` | `CreateBuffer` (draw vertex ring) | unchecked | `deviceOk`; capacity only on success |
| | `Map` vertex ring (`beginVertices`) | unchecked, copied into the result | `map` (hook site `map`); failure: CPU copy, no `Unmap`, no `Draw` |
| | `Map` draw constants (`setPass`) | unchecked, copied into the result | `map` (site `map`); failure: not uploaded, marked stale |
| | `Draw` (`drawVertices`, `drawVelocity`) | always | only with mapped vertices |
| | `ClearRenderTargetView` (`clearTarget`) | always | only with a view |
| `d3d11_present.cpp` | `makeTex` + views (`ensureDisp`) | unchecked | null texture: no views, size reset; views `deviceOk` |
| | `Map` display constants (`renderDisplay`) | unchecked | `map`; failure returns before using the display texture |
| | `Map` view constants x2 (`renderViewImage`) | checked | `map` |
| | `Map` present constants (`presentLatched`) | unchecked | `map`; failure: no Present |
| | `Present` | result fed a status only | `deviceOk` (hook site `present`) |
| | `latchDisplay`, `presentLatched`, `endFrame` | | return on `gone()`; `endFrame` probes the device after a DLSS / NR fault and runs the hook's frame site |
| `d3d11_targets.cpp` | `makeTex` + views (`allocRT`) | unchecked, null used by Clear / Copy | checked; on failure the old objects stay, the CPU bookkeeping still grows |
| | `CreateTexture2D` (`ensureStaging`) | unchecked | `deviceOk`; callers check for a null staging texture |
| | prefetch copy, readback (`beginReadback`), verify (`verifyRTBlocks`) | Map checked | `map`; return on `gone()` or a missing texture |
| | `copyTargetTop`, `patchTargetBlock`, `uploadTargetRect` | | return on `gone()` or a missing texture |
| | `getDepth` | creates checked by `if (d->dsv)` | no creation on `gone()` |
| | `snapshot` copy + view | unchecked | checked; return on `gone()` |
| `d3d11_device.cpp` | `makeTex` | unchecked | `deviceOk`, nothing on `gone()` |
| | `GetBuffer` + view (`makeSwapRtv`), `ResizeBuffers` (`resize`) | view unchecked, resize unchecked | `deviceOk`; no resize on `gone()` |
| | pipeline objects of `init` | unchecked | one null sweep after them: init fails instead of leaving nulls |
| `d3d11_textures.cpp` | `CreateTexture2D` + view (`createSampledTexture`, render thread and texture workers) | checked | `deviceOk` (hook site `texture`); nothing created on `gone()` |
| `d3d11_material.cpp` | material texture + view (workers); `Map` material constants | checked | `deviceOk`, `gone()`; `map` |
| `d3d11_terrain.cpp` | `Map` terrain constants | checked | `map` |
| `d3d11_capture.cpp` | three `Map` (motion validation, stage dump, `readTexture`) | checked | `map`; `readTexture` refuses a null staging or source texture |
| `d3d11_world.cpp` | `Map` depth stretch constants, motion vertices | checked | `map` |
| | `enterWorld`, `enterTemporalWorld`, `ensureMotionTarget`, `prepareObjectMotion`, `finalizeNeuralWorld`, `finalizeWorld`, `recoverReducedWorld` | | return on `gone()` |
| | after `combinedSr->resolve` and `tp->resolve` (DLSS evaluate) | failure became a fault | failure probes the device; hook site `upscale` |
| `d3d11_vertex_program.cpp` | `Map` motion constants, program constants | checked | `map` |
| | ring and buffer failures | `programFailed` | `probeDevice` first |
| `backend_select.inl` | every forwarder that reaches the device | `m_dead` | `stopped()` (also `lostNow`); present wait and `upscaleCut` latched |

Left as they are, on purpose:

| Where | Why |
| --- | --- |
| `getBlendState`, `getDepthState` (`CreateBlendState`, `CreateDepthStencilState`) | a null state binds the default state; no pointer is used |
| `warmMainPipelines` `GetData` loop (start-up) | it loops only while `GetData` returns `S_FALSE`; a removed device returns an error and ends the loop |
| `appendRing` / `reserveRing` in `d3d11_vertex_program.cpp` | already checked; their callers now probe the device (kept a free template, not routed through `map`) |
| shader creation in the background compiles (vertex programs, movie, backdrop, `partial_upload_d3d11.h`, `rt_upload_d3d11.h`, `sr_combined.cpp`) | already checked, a failure falls back; a removal there is found at the next Map or Present |
| `GetData` in `d3d11_terrain.cpp` and `d3d11_gpu_passes.h`, `CreateQuery` in `d3d11_gpu_passes.h` | timing diagnostics behind `GSR_TERRAIN_GPU_TIMING` / `GSR_GPU_PASS_TIMING`; compared with `S_OK` or guarded by `if (slot.end)` |
| save preview and menu hold textures in `renderDisplay` | guarded by `if (savePreviewTex)` / `if (menuHoldTex && menuHoldSrv)` |
| window shots, matched compares, `GSR_RTDUMPF` (`swapRtv->GetResource`, `readTexture`) | debug flags; after a loss `presentLatched` and `endFrame` return before them |
| `sr_combined.cpp`, `nr_interop.cpp`, `dlss_ngx.cpp` | their results are checked already; they run only inside the world passes, which return on `gone()`; a failed evaluation now probes the device |
| `ray_water.cpp` | its own D3D12 device with its own removal check (`fault`, watchdog), unchanged |
| settings overlay (`runtime/settings/src/OverlayD3D11.cpp`, `imgui_impl_dx11.cpp`) | every Create and Map there is checked; it draws only inside `presentLatched`'s callback, which no longer runs after the latch |
| `d3d11_backdrop.cpp`, `d3d11_movie.cpp` | creation checked already, no Map |

Threads after the latch: the GS worker keeps running and every forwarder returns at once; the texture workers finish
their CPU work and create nothing (`createSampledTexture`, `createMaterialTexture` return on `gone()`); the ray pass
thread and its watchdog are unchanged; DLSS and NR objects are not called again (the world passes return) and are
released by `~D3D11Backend` as before; the overlay is not drawn. Shutdown order is unchanged.

## A runner mistake: TaskStop ends the shell, not its children

To cut the replay matrix from two parallel runs to one, the agent's TaskStop was used on the background shell that
ran `matrix.sh`. That ended the bash shell only: its `xargs -P 2` and the replays under it kept going and worked
through the rest of the list, while a second, serial `matrix.sh` was started. For a while up to three replays of this
work ran at once (the limit was two), and both instances wrote the same hash files. Stopping the leftover processes
by their ids was refused by the session's permission check and was not worked around; the coordinator took option
(b): both instances ran to their end, every hash file either could have written was discarded, and the whole matrix
was run again one replay at a time. For the next runner: start the matrix so that one stop ends it and every child
(its own process group or job object, or a stop file the loop checks before each run), and never start a second
instance into the same output folder.

## Recreating the device and carrying on (option for later, not done)

Possible in principle because GS memory (`LocalMemory`) is the truth: every render target, depth buffer and texture
can be rebuilt from it. What it would take:

1. On the latch, keep the GS worker paused at a frame boundary (the latch already stops it from reaching the device).
2. Release every Direct3D object of the Impl (targets, depth buffers, texture cache and upscale cache, staging and
   prefetch copies, display, view, swapchain, the shader and state objects, terrain, material, vertex-program rings,
   NGX feature and parameters, NR guides, the combined upscaler) and the device and context, then create a new device
   and swapchain on the same window and run `init` again (shader cache hits keep this to about a second).
3. The hard part: GPU-owned target contents that were never read back are lost. The dirty rows of every target are
   newer than memory. Losing them means one frame (or more, for targets the game draws once and keeps, such as the
   inventory canvas and scratch targets) shows stale content until the game redraws them. Readback before the reset is
   impossible after a real removal.
4. The DXR device (`ray_water`), its shared fences and the NR interop textures have to be rebuilt too, and NGX
   shut down and initialised again on the new device.
5. The host would need a "device recreated" path instead of the ordered end, and the record a new end kind.

Estimate: two to four days with tests, plus a real-removal test that needs a quiet machine. It is not small and
safe, so it is not in this change.

## Not established

- The game after a real driver reset: every game-run claim rests on the forced-loss hook, which makes one checked call
  report `DXGI_ERROR_DEVICE_REMOVED` while the device keeps working. A call the audit missed would not crash in those
  runs. The one real reset was seen by the headless replay tool only.
- That no call reaches the device after the latch is shown for the forwarders (the draw count in the synthetic test)
  and read from the code for the rest; nothing counts device calls. The D3D11 runtime's behaviour on a removed device
  (calls on valid objects do nothing, Map and Create fail) is from its documentation, and from the one real reset.
- NGX shutdown (`dlssShutdown`, `NVSDK_NGX_D3D11_Shutdown1`) and the release of the DLSS feature on a removed device:
  unchanged and not exercised (the forced loss leaves the device alive).
- The ray passes (`ray_water.cpp`, their own D3D12 device, shared fences): unchanged; after a real reset they rely on
  their own fault handling and watchdog. Not exercised with the ray effects on.
- The dialog (`MessageBoxW`) was not seen; scripted runs log the notice.
- A loss during a load of a playable level: the hit was the intro level's load; Surdana's load was not hit (the frame
  number is a poor clock across runs).
- Loss during a movie, during saving, in multiplayer, with the ray effects on: not run.
- Linux and the browser: not built or run (the backend-neutral headers are standard C++; the WebGPU path is unchanged
  except for the forwarders' extra relaxed load and the shared notice text).
- A Debug build was not made.
- The completeness of the call-site audit beyond the files listed.

## Commits

- d1cc91f6 `refactor(game)`: the backend-neutral rules and notice (`device_loss.h`, `renderer_session.h`), tests.
- 4189da39 `fix(game)`: the Direct3D 11 checks, `BackendApi::lostNow`, the forced-loss tests, `graphics.md`,
  `env-flags.md`. Its body carries the replay matrix.
- The commit that adds this note: the host uses the shared notice and `cleanEnd`, the plain start notice, the world passes return after a
  loss at the DLSS evaluation, this note.
