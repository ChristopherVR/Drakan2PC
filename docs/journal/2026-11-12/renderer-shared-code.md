# One copy of the GS emulation for both hardware backends: texture cache, draw code, target policy

Owner's instruction: "We need to remove the duplicate code, so continue making that shared." This entry carries out the
first three items of the ranked list at the end of [vulkan-backend.md](vulkan-backend.md) and gives the WebGPU
backend (which is also the Vulkan and Direct3D 12 renderer) two capabilities it lacked: clean-block retention and
exact partial uploads. Items 4 to 6 of that list are not done.

Scratch (not committed): `build/review/gs-share/` (`hashes/`, `logs/`, `patches/`, `tmp/` with the scripts that made
the moves).

## Measured duplication

Method: lines of a `d3d11_X` / `wgpu_X` pair that are identical in order (Python `difflib` matching blocks) after
stripping comments and blank lines, with `D3D11Backend` and `WgpuBackend` treated as one word
(`build/review/gs-share/dup.py`). The previous entry counted 3,240; its method was not kept, this one gives 3,712 for
the same pairs plus `*_device.cpp` at the start, so only the numbers below compare with each other.

| Pair | Code lines D3D11 / WebGPU, identical: before (28485c31) | after step 1 | after step 2 | after step 3 | after step 4 |
| --- | --- | --- | --- | --- | --- |
| `*_textures.cpp` | 1114 / 1062, 1028 | 28 / 14, 8 | | | |
| `*_draw.cpp` | 1160 / 919, 837 | | 159 / 39, 20 | | |
| `*_targets.cpp` | 1098 / 678, 506 | | | 487 / 276, 165 | 487 / 314, 166 |
| `*_backend_impl.h` | 812 / 562, 473 | 736 / 501, 404 | 721 / 500, 377 | 729 / 514, 383 | 729 / 516, 383 |
| `*_present.cpp` | 677 / 374, 292 | | | | unchanged |
| `*_backend.cpp` | 384 / 325, 279 | | | | unchanged |
| `*_backdrop.cpp`, `*_capture.cpp`, `*_movie.cpp`, `*_device.cpp` | 297 identical in all | | | | unchanged |
| All ten pairs | 6378 / 4457, 3712 | 5216 / 3348, 2624 | 4202 / 2467, 1780 | 3597 / 2079, 1444 | 3597 / 2129, 1445 |

(Measured on the 1fa0fc3e tree with the step 4 flags on; none of these files changed between 1fa0fc3e and 28485c31.)

## The structure that was chosen, and why

The previous entry proposed `gs_texture_cache.h/.cpp` and `gs_draw_plan.h` as units of `gsr_core`, compiled once.
Reading both files of each pair showed why that does not fit yet: the cache, the draw code and the target policy are
member functions over one large state (`Impl`: about 300 members) and call each other and the GPU every few lines.
A unit compiled once needs either a virtual device interface under the Direct3D 11 backend (a Direct3D 11
implementation of `gsr::rhi`, which the previous entry left as a later decision) or the state split into a neutral
base first (item 4). Neither can be proven in small steps.

What the pairs did show is that they were the same text. So the text now exists once, and is compiled once per
backend:

- `gs_backend_shared.h`: the types both backends used under the same names (`TexKey`, `UpKey`, `SlotInfo`,
  `PalettePlane`, `PSConsts`, the `PF_*` flags, `DispConsts`), the texture cache records as templates over the
  backend's texture handle (`UpEntryOf`, `UpDoneOf`, `TexEntryOf`), and `BackendCaps`.
- `gs_texture_cache.inl`, `gs_draw.inl`, `gs_targets.inl`: member functions of `GSR_IMPL`. A backend file includes
  its private header, defines `GSR_IMPL` as its `Impl` and includes the unit (the pattern `backend_select.inl`
  already used). The bodies are the Direct3D 11 text, moved; `git log --follow` on a unit continues into the
  history of the `d3d11_*.cpp` file it came from for the first two (renames), and `git blame -C` finds the third.
- `gs_pipeline_state.h`: blend, depth and topology state in neutral terms. These are the `gsr::rhi` types that were
  in `rhi.h`; the draw code decides in them, WebGPU passes them through and Direct3D 11 maps them to its enums.
- A backend file (`d3d11_textures.cpp`, `wgpu_textures.cpp`, ...) now holds the API calls: the functions named in
  the "backend interface" comment at the top of each unit.

The backend interface, complete:

| Unit | A backend provides |
| --- | --- |
| texture cache | `GpuTexture` (handle with `get`, `reset`, `operator bool`), `createSampledTexture`, `copyAliasedBlocks` |
| draw | `clearTarget`, `fixedFunctionBlend`, `beginVertices` / `endVertices`, `RT::view` / `RT::copyView`, `snapshot`, `bindDraw`, `setPass`, `drawVertices`, `endDraw`, `readTarget`, `readBoundTexture` |
| targets | `allocRT`, `beginReadback` / `endReadback`, `uploadTargetRect`, `copyTargetTop`, and for partial uploads `preparePartialPatch`, `patchTargetBlock` |

Calls through it are direct (the units are compiled per backend), so there is no virtual call per draw, vertex or
pixel.

Capabilities: a feature a backend lacks is a flag in its `static constexpr BackendCaps kCaps`, asked with
`if constexpr (kCaps.x)`. Inside a discarded statement a function need not be defined, so the backend without the
feature declares the functions behind the flag and never defines them; the logic is not forked.

| Flag | Direct3D 11 | WebGPU | What is missing on WebGPU |
| --- | --- | --- | --- |
| `texturesOffThread` | yes | no | `rhi::Device::writeTexture` is ordered with the draws of the render thread; needs a thread-safe create-with-data in `gsr::rhi` |
| `materialDetail`, `terrainDetail` | yes | no | the shader fragments exist in HLSL only |
| `motionVectors` | yes | no | no velocity program, and `gsr::rhi` has one colour target per pass |
| `temporalUpscaler` | yes | no | DLSS needs a raw device (previous entry); the WebGPU world is drawn at full resolution, `RT::wgeom` says so |
| `readbackPrefetch` | yes | no | needs a readback that is started without being waited for |
| `readbackVerify` (`GSR_RB_VERIFY`) | yes | no | debug reader not written |
| `cleanRowRefresh` | yes | yes since dc6d3fa4 | |
| `partialUploads` | yes | yes since dc6d3fa4 | |

A side effect of one text: diagnostics that were written in the Direct3D 11 file only now run on both
(`DRAKAN_VU_TRIGGER`, the surface pick and audit, `GSR_BACKDROP_LOG`, `GSR_RB_TRACE`, `GSR_RB_DRAW_TRACE`). They are
all behind environment variables.

Deviations from "moved verbatim", all in step 1 to 3 and none visible in a hash:

- A texture whose shader resource view cannot be created now counts as a failed texture in the decode path too
  (before: only in the HD path). Not reachable without the device running out of memory.
- The WebGPU draw copies its vertices into a vector the backend keeps, instead of a new vector per draw.
- WebGPU's render target keeps `wgeom` (the world's render rectangle, the whole target) and the fields
  `neuralCamera` / `neuralDepth` / `neuralZmax` / `neuralXY` under the Direct3D 11 names (they were `taa*`).
- The order of two early returns in WebGPU's `completeTargetUpload` follows Direct3D 11.

## Steps and proof

All four steps were developed and proven in a private worktree and build tree, then applied to `main` one commit at
a time. The oracle is the capture replay: five recorded GS streams (`surdana` 2,585 frames, `cave` 5,558, `flight`
1,864, `tab` 3,984, `ootf` 4,074) replayed with `gsr_replay` (Direct3D 11) and `gsr_replay_webgpu --gpu-api vulkan`
and `--gpu-api d3d12`, at internal scale 1, 3 and 8, with the HD texture folder given and not given: 90 runs and
325,170 hashed frames per matrix, every frame hashed (`--hashes`). A matrix is compared with the baseline matrix of
commit 28485c31, file by file.

| Commit | Step | Runs identical to the baseline | Texture set of the HD-on runs | Tests on the step's own tree |
| --- | --- | --- | --- | --- |
| cf17e89e | 1: texture cache (`gs_texture_cache.inl`, `gs_backend_shared.h`) | landed on the proof of the step 1+2 tree; its own matrix afterwards: 90 of 90 | step 1+2 tree: the pack before the evening refit; own matrix: HD-off before, HD-on frozen copy | ctest 163 of 163; suite 395 / 361 / 361 |
| 9e502cd7 | 2: per-draw code (`gs_draw.inl`, `gs_pipeline_state.h`) | 90 of 90 | the pack before the refit, on both sides | ctest 163 of 163; suite 395 / 361 / 361 |
| 5162001f | 3: target policy (`gs_targets.inl`) | 90 of 90 | frozen copy on both sides | ctest 163 of 163; suite 395 / 361 / 361 |
| 4089aaa9 | 4: WebGPU clean-block retention and exact partial uploads, present and switched off | 90 of 90 | frozen copy on both sides | ctest 163 of 163; suite 395 / 361 / 361 |

Suite = `gsr_test_synthetic` on Direct3D 11 / `gsr_test_synthetic_webgpu --gpu-api vulkan` / `--gpu-api d3d12`,
checks run, none failed. The 34 checks WebGPU does not run are the ones listed in the previous entry (16 + 16 exact
partial uploads, 1 clean-block retention, 1 motion-scope batching); with the two flags of step 4 switched on WebGPU
runs 394 and only the batching check remains.

Conditions and what went wrong on the way, because the commit bodies of cf17e89e, 9e502cd7 and 5162001f carry a
wrong line:

- Concurrency: at most four of this work's replays at a time. Other agents' games and replays shared the GPU.
- GPU memory: the three commit bodies say dedicated GPU memory stayed at or below 9.9 / 10.0 GiB. That is false; the
  figure came from an `awk` comparison that compared the samples as text. From the same 15 second samples, computed
  numerically: above 12 GiB in 383 of 1,544 samples (25 %), up to about 15.9 GiB of 16.
- Driver resets: the Windows System log has `nvlddmkm` event 153 ("Restarting TDR occurred") at 22:13 to 22:14,
  02:23 and 03:09 to 03:10 (24 events), each at about 15.3 GiB dedicated memory and 100 % GPU. Each ended the four
  replays running at that moment, Direct3D 11 ones (exit 139) and WebGPU ones (exit 127) alike; all ten were scale 8
  runs. Their hash files were truncated, not wrong, and all ten were run again.
- Why the frame identity holds anyway: every comparison is of two independent runs made at different times that
  agree on every one of 325,170 frames. A failed allocation would have to corrupt both runs identically, frame for
  frame, to pass. A run hit by a reset produced a truncated file and was repeated.
- Texture input: `Build-LocalGame.ps1 -DistDirectory <play folder>` stages the shared HD pack from `dist/DrakanPC`
  into the play folder. The pack in `dist` had been regenerated in the evening, and a game build of this work at
  23:33 replaced the textures the replays were reading. The baseline and step 2 matrices had finished before
  (23:21 and 23:11) and compare like with like. The HD-on halves of the baseline, step 1, step 3 and step 4 were
  taken again from a frozen copy (`build/review/gs-share/textures_frozen`).
- Base: the steps were first written on 3f1232f2, then re-applied on 1fa0fc3e and on 28485c31 as `main` moved; only
  the 28485c31 matrices count. Between 28485c31 and each landing `main` changed, among the files of the step, only
  `runtime/gs/CMakeLists.txt`.
- The runner hung twice on a bare `wait` (three hours lost), and two matrices stopped with the harness kept running
  beside newer ones for a while in the afternoon (17 replays at once, memory spilled); everything from that period
  was discarded.

Game: `Build-LocalGame.ps1 -Incremental` from the main tree under the build lock at 9e502cd7, 5162001f, 4089aaa9 and
dc6d3fa4,
each without error (this build has the DLSS code compiled in, the private tree has not). Live after each, `play_run.py`
with `PS2X_GS=d3d11` and `PS2X_GS=vulkan`: the Surdana start (outdoor, inventory, pause, F1 settings) and a start in the
Catacombs (`--level 1c`), all to their scripted quit, no error or device-lost line. Matched frames were looked at
(`build/review/gs-share/live1`, `live3`, `live4`, `live5`, `pairs.png`): the same picture on both renderers.

## What WebGPU gained (steps 4 and 5)

With both flags on (`cleanRowRefresh`, `partialUploads`; the tree before they were switched off for 4089aaa9)
WebGPU's counters become exactly Direct3D 11's. Per frame, scale 3, HD textures on, whole capture:

| Capture | Readbacks: WebGPU before, with the flags, Direct3D 11 | Readbacks skipped | Target uploads | Target refresh KiB |
| --- | --- | --- | --- | --- |
| surdana | 0.1269, 0.0828, 0.0828 | 0.098, 0.324, 0.324 | 15.905, 15.940, 15.940 | 310.7, 311.5, 311.5 |
| cave | 0.1098, 0.1026, 0.1026 | 0.096, 1.724, 1.724 | 5.832, 6.236, 6.236 | 145.0, 164.1, 164.1 |
| flight | 0.0612, 0.0601, 0.0601 | 0.272, 0.306, 0.306 | 75.226, 75.736, 75.736 | 921.1, 967.7, 967.7 |
| tab | 0.1619, 0.1566, 0.1566 | 0.028, 0.073, 0.073 | 10.113, 10.124, 10.124 | 186.3, 186.7, 186.7 |
| ootf | 0.1448, 0.1414, 0.1414 | 0.317, 6.386, 6.386 | 1.228, 2.634, 2.634 | 119.4, 169.5, 169.5 |

Vulkan and Direct3D 12 give the same numbers. Draw calls do not change. The suite goes from 361 to 394 checks.

4089aaa9 landed the code with the flags off, because the pictures are not identical: with the flags on, 6 of the 60 WebGPU runs
differ from the WebGPU baseline (ootf frames 1201 to 1260 at scale 3 and 8, and 396 cave frames at scale 8, HD
textures off; scale 1 and every HD-on run are identical).

What the difference is:

- Direct3D 11, which has both features, differs from itself in the same frames when they are switched off
  (`GSR_PARTIAL_UPLOAD=0 GSR_RB_CLEAN_SYNC=0`).
- With texture upscaling off (`--texup 1`) the two paths give identical pictures on Direct3D 11 (ootf scale 3 to
  frame 1261, cave scale 8 whole capture: 0 frames differ). So GS memory and the targets hold the same on both paths.
- The cause is which textures the automatic xBRZ upscale is applied to. `aliasedTarget` only finds a target while it
  is dirty, and `resolveTexture` upscales a texture that overlaps such a target only when its content is an authored
  image. The game uploads small images into memory that 128 x 512 scratch targets occupy (ootf frames 1094 to 1165 in
  the readback trace). Without the features that upload reads the target back, the target is clean, the texture is
  an ordinary one and is upscaled; with them the target stays dirty and the texture stays at native resolution.
  With the HD pack on the texture is replaced on both paths; at scale 1 nothing is upscaled.
- Size: ootf scale 3, all 60 differing frames, both backends: at most 2 of 255 in any channel, 9,625 to 44,051
  pixels per frame (1.87 % of the image), about 2,267,500 differences of 1 and 1,852 of 2 in total. Cave scale 8,
  frames 2904 to 2986 (78 of the 396 differing frames): 476 to 782 pixels per frame, at most 2. The worst frame at
  16 times the difference shows faint speckle on dark hillsides (`build/review/gs-share/diag/worst_16x.png`).

So the two readback skips are exact for memory, not for the picture once textures are upscaled.

## Step 5: the two capabilities switched on (dc6d3fa4)

After the finding above the owner's coordinator approved switching both flags on, as a `perf(game)` commit of its
own: WebGPU then takes exactly the decisions Direct3D 11 takes. Proof of dc6d3fa4 against the 28485c31 baseline:

| What | Result |
| --- | --- |
| WebGPU on Vulkan and on Direct3D 12, texture upscaling off (`--texup 1`), scale 1, 3, 8, five captures | 30 of 30 runs identical to the baseline, 108,390 frames: the skips are exact for GS memory |
| Direct3D 11, default settings, scale 1, 3, 8, HD textures off and on (frozen copy) | 30 of 30 identical |
| WebGPU at the default settings (taken on the same sources before the flags were switched off for 4089aaa9) | 54 of 60 identical; the 6 runs and their size are described above |
| Suite | 395 (Direct3D 11), 394 (Vulkan), 394 (Direct3D 12), none failed; ctest 163 of 163 |
| Counters | equal to Direct3D 11's on all five captures (table above) |

Conditions: at most four replays at a time and two at scale 8, GPU memory at the end of each run at most 11.5 GiB,
no run failed. The one check WebGPU still does not run is "three objects with identical state are one draw call"
(motion-scope batching, `kCaps.motionVectors`).

Wording: these skips were described as exact. They are exact for GS memory and for what the targets hold; they are
not picture-identical once the automatic texture upscale is on. No reference document or test name claimed picture
identity for them ("bit-exact" does not occur for them in `docs/reference` or in `test_synthetic.cpp`), so nothing
was renamed; `graphics.md` now says "memory-exact".

## Open: one upscale decision whether or not the target is dirty

Not approved, not done. Today a CT32 texture whose memory a CT32 target occupies is treated in two ways depending on
whether that target is dirty at the moment of the decode (`aliasedTarget`, `resolveTexture`): aliased (target blocks
copied in, no xBRZ upscale unless its content is an authored image) or ordinary (decoded from memory and upscaled).
Which of the two happens depends on whether some earlier read happened to read the target back, which is how the two
readback skips reach the picture.

- What it would take: decide from memory ownership, not from the dirty flag. For example, treat the texture as an
  ordinary one when an applied upload has proven all of its blocks clean (`blocksAreClean` already answers that), and
  as aliased otherwise. The change is in shared code (`gs_texture_cache.inl`), so it reaches both backends at once.
- What it would change: pictures on the default renderer, in the frames where such textures are drawn with automatic
  upscaling (in the five captures: the ootf and cave frames above, by at most 2 of 255), and possibly which textures
  are offered to the HD pack dump. It needs its own replay proof with the upscale on and off.
- Not established: which texture it is in the ootf and cave frames (the scratch targets and the rule were traced,
  not the single texture), and why the cave capture differs at scale 8 (8x upscale) and not at scale 3 (4x).

## For the owner: a driver reset ends a Direct3D 11 session with a crash

During the night the NVIDIA driver restarted the GPU three times (`nvlddmkm` event 153, "Restarting TDR occurred on
GPUID:100": 22:13 to 22:14, 02:23, 03:09 to 03:10; 24 events) while about 15.3 of 16 GiB of dedicated memory were
in use and the GPU was at 100 %, with several agents' programs on it. Each restart ended every renderer process on
the GPU, and the Direct3D 11 replays ended with exit code 139 (an access violation), not with an error message.

From the code, not from a provoked reset:

- `d3d11_*.cpp` has no test for a removed device: no `DXGI_ERROR_DEVICE_REMOVED`, no `GetDeviceRemovedReason`.
- `D3D11Backend` does not implement `BackendApi::deviceLost` (the base reports "not lost"), so the latch in
  `gsr::Backend` (`backend_select.inl`) that stops every call after a loss on the WebGPU path never closes.
- The result of `Present` (`d3d11_present.cpp`, `presentLatched`) only feeds a status flag.
- `beginVertices` and `setPass` (`d3d11_draw.cpp`) copy into the pointer `Map` returned without looking at its
  result; after a removed device `Map` fails and the pointer is null. That matches the exit code.

So a player on `renderer = d3d11` whose driver resets would see the game crash, where the WebGPU path ends the
session in order and starts on the other renderer next time. A minimal safe handling, not implemented: check the
`Map` results in the draw path and the `Present` result for device removed, report it through
`BackendApi::deviceLost` so the existing latch and the host's ordered end with a notice (`gsr/renderer_session.h`)
apply, and let the next start try the player's choice again. The replay tools run headless, so the wait before the
window image that protects the WebGPU game path is not on their path; whether that wait would have prevented these
resets was not tested, and with Direct3D 11 processes ending too it is not the whole answer.

## Renderer thread CPU time

`scripts/perf/gs_replay_ab.py`, cave capture, scale 3, HD textures, whole capture, three interleaved repeats,
baseline binaries of 28485c31 against the tree of 4089aaa9 (other replays were running beside it):

| Backend | Thread CPU ms per frame before (median, range) | after |
| --- | --- | --- |
| Direct3D 11 | 1.751 (1.676 to 2.229) | 1.757 (1.704 to 2.356) |
| WebGPU on Vulkan | 1.937 (1.920 to 2.665) | 1.945 (1.892 to 1.974) |

The medians differ by 0.3 and 0.4 %, far inside the spread. Pictures of all six runs per backend identical.

## Not done, and why

- Items 4 to 6 of the list (`*_backend_impl.h` state base, `*_present.cpp`, `*_backend.cpp` with the scene camera
  phase machine): not started. `d3d11_present.cpp`, `d3d11_world.cpp` and `d3d11_backend.cpp` were being changed by
  the DLSS work during the day.
- In `*_targets.cpp` the remaining identical lines are `allocRT`, `clearInventoryCanvas`, `getDepth`, the grid
  capture and `uploadTargetRect`, each mixed with API calls line by line.
- Readback prefetch, texture creation off the render thread, motion-scope batching, terrain and material detail on
  WebGPU: see the capability table; each needs code in `gsr::rhi` or a WGSL fragment, not only a call.

## Not verified

- Linux, the browser build and clang: nothing here was compiled or run there. `if constexpr` with a function that is
  declared and never defined is standard C++17; only MSVC Release builds were made (private tree and game).
- A Debug build was not made.
- `git blame` continuity was checked for `gs_texture_cache.inl` only (`git log --follow` reaches the history of
  `d3d11_textures.cpp`).
- The behaviour-preserving claims rest on the five captures, the synthetic suite and three short live runs per
  renderer. Movies, saving and loading, long play, multiplayer, DLSS frames (the replay tools run without DLSS) and
  the ray effects were not exercised; the live runs used the play folder's ini with DLSS Quality on Direct3D 11 and
  the frames looked the same as before by eye only.
- The diagnostics that now also run on WebGPU (`DRAKAN_VU_TRIGGER`, surface pick and audit, `GSR_RB_TRACE`) were not
  run there.
- The WebGPU masked patch (`patchTargetBlock`) is covered by the suite (394 of 394 on Vulkan and Direct3D 12) and
  by the replays; it was not looked at in a live game beyond the short runs per renderer.
- Which texture causes the picture difference of the two features, and why cave differs at scale 8 and not at 3:
  not established.
- The driver resets were read from the System log; the statement that the Direct3D 11 backend would crash on a
  removed device is from the code (no device-removed test in `d3d11_*.cpp`, unchecked `Map` results), not from a
  provoked reset.
- CPU time was measured on one capture at one scale on a machine other agents were using.
