# Renderer and GPU performance review (2026-11-12)

Area: the Direct3D 11 backend of `runtime/gs` and the GS stream decode in front of it. Runs, captures, logs and tools
stay private under `build/review/perf-gpu/` (not committed). The WebGPU files were not edited; what the twin needs is
listed below.

## Result

- Five changes, all leaving the pictures bit-identical in replay (hash of the display of every 20th frame, five
  captures, against the build from before the changes):
  1. scratch render targets are refreshed from GS memory only in the rows a use needs (`GSR_RT_LAZY_SYNC`);
  2. rows refreshed above internal scale 1 are replicated on the GPU instead of the CPU (`GSR_RT_UPLOAD_GPU`);
  3. a scratch target the game stopped drawing into is staged for readback at once, so the readback that follows many
     frames later maps a finished copy instead of waiting for the GPU queue;
  4. the GPU texture of an HD or upscaled image is created on the worker that decoded it, not on the render thread;
  5. the 4x4 probe the game draws on the last memory page (and reads back to the EE every frame in some views) is
     mirrored into memory like other constant sprites, so that read no longer touches the GPU.
- Readbacks that still wait for the GPU, per capture, before and after 3 and 5 (`GSR_RB_TRACE`, exact counts):
  Surdana 253 to 37, cave 570 to 19, flight 112 to 24, tab 624 to 22, Order of the Flame 576 to 43.
- Exact (counter) gain of 1 + 2 at scale 8: the bytes the CPU sends to refresh render targets fall from 8.6 to 22.6 MiB
  per frame to 0.10 to 0.34 MiB per frame over whole captures (boot, title, loading, play), see the table. Renderer thread
  CPU per replayed frame fell in every capture measured twice, by more than the spread between the repeats.
- The live frame rate gain is **not established**: 9 to 11 other `drakan.exe` and compilers shared the one GPU all
  day, and the live A/B runs are inside their own spread (below).
- Not fixed, with reasons: the wait of the remaining readbacks (design below), the EE waiting for GS FINISH before
  its per-frame read (guest side), auto scale 8 at 4K with DLSS, per-draw costs of DLSS object motion.
- One machine finding outside the renderer: a 64 byte cache line of one capture file read back as garbage for about
  twenty minutes without the file having been written (section "Other areas").

## Conditions

- Ryzen 7 9800X3D (16 threads), RTX 5080, 3840x2160 at 60 Hz. Between 5 and 11 other `drakan.exe` plus compilers ran
  during every measurement. Wall-clock and GPU numbers are therefore polluted; counters, bytes and picture hashes
  from replays are exact, thread CPU time is fairly stable.
- Owner's `drakan.ini` (copy in `build/review/play`): borderless 3840x2160, `scale = auto` (the log shows 8 in the
  world), `aspect = auto`, `fov = 99`, DLSS quality, HD textures, `texture_upscale = auto`, anisotropic 16, vsync
  off, world rate uncapped, ray effects off.
- Captures (`PS2X_GS_CAPTURE=30000` from boot with that ini, `play_run.py`, recipes in
  `build/review/perf-gpu/recipes/`): `surdana` (Level 1A start, camera sweep, walk; 2585 vsync frames), `cave`
  (Level 1C Catacombs start, walk, two sword swings; 5558), `flight` (Level 4A Ravenshold on Arokh; 1864), `tab`
  (Surdana with the modern Tab inventory opened and closed; 3984), `ootf` (Order of the Flame campaign start; 4074).
  4.2 to 7.1 GB each. No capture of a fight with enemies and spell effects was made (no save fixture was used).
- `gsr_replay` got `--bench A-B`, `--bench-csv`, `--hashes` and `--no-png` (renderer thread time and CPU per frame,
  counters per frame, a hash per captured display image). `scripts/perf/gs_replay_ab.py` alternates two environments
  or two builds over one capture and prints medians, ranges and whether the pictures match.
- A replay is the core GS path only: no DLSS, no world guides, no ray pass, no present. After the title picture the
  replay stays in the renderer's "GUI page" state (`sceneCamera.guiScreen = 8` is never cleared without the runtime's
  scene packets), so HD images are decoded on the replay thread, which the live game does only for menu pages. Replay
  CPU time therefore contains PNG decoding that live play does on workers.

## Baseline per scene (replay, scale 8, build before the changes)

World frames are the vsync frames with at least 5000 vertices.

| Capture | world frames | vertices per world frame (median) | draw calls per world frame (median, max) | target row refreshes per world frame | readbacks per world frame | refresh bytes per frame, whole capture |
| --- | --- | --- | --- | --- | --- | --- |
| surdana | 1158 | 38704 | 62, 232 | 11.1 | 0.18 | 13.9 MiB |
| cave | 993 | 50238 | 134, 278 | 17.6 | 0.54 | 8.6 MiB |
| flight | 993 | 42396 | 142, 358 | 12.2 | 0.09 | 22.6 MiB |
| tab | 1008 | 70470 | 116, 255 | 13.5 | 0.60 | 8.6 MiB |
| ootf | 1204 | 16818 | 674, 1538 | 17.5 | 0.41 | 10.0 MiB |

- Draw calls are already batched hard: 60 to 140 per world frame for 40000 to 70000 vertices on the disc levels. The
  converted Order of the Flame level needs five to ten times as many draw calls for a third of the vertices.
- With DLSS on, the live game issues about a quarter more draw calls than the replay of the same stream (Surdana:
  111068 against 89832), because every object-motion scope ends the current batch.
- Texture streaming: 37 T8 uploads per frame in Surdana play (623000 pixels per frame), 10 to 18 of them recognised
  as identical re-uploads (`revalidated`); 0.3 to 1.7 new texture decodes per frame.
- Live profile of the baseline exe (Surdana, 6 s of `[gsr-profile]` windows, 10 other games running): `presentCall`
  2445 ms and `presentLatched` 480 ms exclusive (the GPU was the limit under that load), `syncRT` 362 ms with
  3051 MiB sent, `drawObjectMotion` 140 ms, `readbackMap` 133 ms in 2 calls, `surfaceWait` 100 ms, `drawVertexUpload`
  87 ms, all draws 1072 ms inclusive.
- GPU time per pass: not measured. The renderer has GPU timestamps only for the upscaler (`GSR_UPSCALE_TIMING`) and
  the ray pass (`GSR_RAY_GPU_TIMING`); with ten other games on the GPU a timestamp interval says nothing about the
  pass. No general per-pass GPU timer was added.

## Ranked costs

1. **Render target refreshes from memory** (`syncRT` and `completeTargetUpload` through `uploadTargetRect`). A target
   object is at least 512 rows tall. The game draws 128 rows of its 128 pixel wide scratch targets (shadows, distant
   characters, orb liquid) and uploads textures into the memory below them every frame; every changed page row of
   every target was copied to the GPU before each use, replicated scale x scale on the CPU (1 MiB per 128 pixel row,
   4 MiB per 512 pixel row at scale 8). Title and loading screens upload into the frame buffers themselves (13 page
   rows per frame at the title, 54 MiB per frame at scale 8) and movies stamp 16x16 macroblocks into both frame
   buffers (two 32 KiB uploads per block). Fixed (changes 1 and 2).
2. **Readbacks that wait in `Map`.** Surdana capture: 253 readbacks. 39 are the game's own 32x32 local-to-host read
   (target at page 0x1ff, `l2h` in the counters), 214 are forced by an image upload into memory inside the dirty rows
   of a scratch target the game no longer draws into (`beginTargetUpload` declines a target not drawn for two
   frames, so the upload reads it back once). Each waits for everything the GPU has queued: 30 to 290 ms under
   today's load in the other agent's runs, 85 ms worst in my baseline run. In the cave the forced readbacks are of a
   256x256 target (489 of 570). Mostly fixed (changes 3 and 5).
3. **HD texture creation on the render thread** (`pumpUp`: `CreateTexture2D` with initial data, 22 MiB for a 2048
   pixel image with mips). Fixed (change 4).
4. **DLSS object motion per draw** (`prepareObjectMotion`: three ordered-map lookups, a node insert, three vertex
   passes and a second vertex buffer map per world draw): 140 ms per 6 s here, 1.2 to 2.3 ms per frame in the
   2026-11-08 review. Not changed.
5. **Auto internal scale 8 at 3840x2160** (`ceil(3840 / 512)`): 4096x4096 targets, a 256 MiB float motion target,
   DLSS output of 16.7 megapixels for an 8.3 megapixel display. Not changed (a picture change; see the 2026-11-08
   review, fix 1a).
6. **GS stream decode**: no single hot spot; `gifSubmit`, `kick`, `toDV`, `emitTri`, `handlePacked` together about a
   sixth of the replay thread's own samples. CT24 uploads still go pixel by pixel (`putPixel`), but the game uses
   them at boot and on the title only (1700 pixels per frame in play against 80000 at boot).
7. Present: flip-discard, 3 buffers, waitable object, tearing with vsync off. Nothing to fix was found.

## Changes

### 1. Row-range refresh of scratch targets (`GSR_RT_LAZY_SYNC`, default on)

`syncRows` refreshes only the page rows a use names; `syncRT` is the whole-target case and every other caller keeps
it. A draw into a lazy target refreshes the hull of its scissor rows and the target's dirty range (a readback writes
the whole dirty interval to memory, so no row may enter it stale); a texture sample refreshes the texture's rows plus
one page row on each side. Rows left out keep their old stamp, so `rowsFromMemory` (the stale-target policy of
`findRTForTexture`, unchanged) still counts them as "memory is newer", and any later whole-target use refreshes them.
A target is lazy only while it is under 480 pixels wide, was never displayed and has no world, temporal or neural
state. `RT::partialSync` stops the `seen == clock` short cut after a partial refresh. Policy in `rt_sync_rows.h`.

### 2. GPU replication of refreshed rows (`GSR_RT_UPLOAD_GPU`, default on)

`TargetUploadGpu` (`rt_upload_d3d11.h`): the native pixels go into a small texture, a compute pass writes each to its
scale x scale block of a scratch texture, `CopySubresourceRegion` puts that into the target. Only compute-stage
bindings are touched and restored. Rectangles under 16384 scaled pixels stay on the CPU path.
`completeTargetUpload` now joins a run of blocks with the identical run directly below it, so a movie macroblock is
one 16x16 rectangle on the GPU path instead of two 16x8 on the CPU path.

### 3. Staging a scratch target when it goes stale (`GSR_RB_PREFETCH`, existing switch)

At the end of the first frame in which a dirty lazy target was not drawn, `prefetchReadback(rt, true)` copies its
dirty rows to its staging texture. The existing validation in `readbackRT` (same write count, rows and scale) decides
whether that copy may be used. In the Surdana capture 177 of the 253 readbacks now map a copy made earlier
(`readback-prefetched` in `GSR_RB_TRACE`); before, only the game's own read was predicted.

The first version only staged targets up to 16384 pixels, like the per-draw prediction; the cave target is 256x256,
so the stale case takes up to 65536 (one copy per stale event, not one per draw). Cave: 55 of 570 readbacks
predicted with the small limit, 551 with the larger one.

### 5. The 4x4 probe on the last memory page (`GSR_RB_SOLID`, existing switch)

What the game reads: in every world frame in which it applies (39 frames of the Surdana capture, all during the
camera sweep) the game draws one sprite of 4x4 pixels into a 64 pixel wide CT32 target and then reads 32x32 pixels
of that target to the EE (`TRXDIR` 1). The sprite is textured with the **depth buffer** read as a 1024x1024 CT32
texture (`TEX0` base 0x2000, which is `ZBUF` page 0x100) at one small spot (texture coordinates 0.21..0.24 by
0.06..0.08 in the logged draw), plain overwrite (`ALPHA` 0x0a), vertex colour 127,127,127,128. So it copies the scene
depth around one screen point and hands it to the game: an occlusion probe (whether a light source such as the sun
is hidden is the obvious use; which game object asks was not traced). The renderer cannot sample the depth buffer as a
texture (it is GPU only), `resolveTexture` fails, and the sprite is drawn untextured: the 16 pixels the game gets
are always 0x807f7f7f, whatever the scene.

The read was served from memory already wherever the probe target lies inside memory: the cave, flight, tab and
Order of the Flame captures have no readback of the `read` class, although the game reads there too (538, 0, 151 and
436 local-to-host transfers in the live capture runs). In Surdana the target is on page 0x1ff, the last one; the
target object is 64 rows tall there, more than memory holds, and that ruled the CPU mirror out for the whole target.
The mirror now only requires the mirrored page rows to lie inside memory. `GSR_RB_VERIFY=1` on the Surdana capture:
0 mismatches.

Consequence for the CPU side: the EE's wait before this read (it polls GS FINISH, guest 0x1dbfa8) is in no capture
the GPU readback any more; it is the wait for the VU1 and GS workers to reach the read in the stream. The answer
the EE gets today is a constant, so a guest-side hook could return it without waiting, with exactly today's
behaviour; that would also fix the probe at "sees no depth". Doing it right (sampling the depth buffer) needs the
frame's depth and therefore the wait. That decision is outside the renderer.

### 4. Texture creation on the worker

`pushDone` creates the immutable texture and its view on the thread that finished the job (device calls are
free-threaded; the pool is joined before the device goes away). `pumpUp` only adopts it. Jobs that run on the render
thread on purpose (menu pages) are as before.

## Measurements

Replay, whole captures, scale 8, HD textures, `gsr_replay_ab.py`, 2 repeats alternating; side A is the new build with
`GSR_RT_LAZY_SYNC=0 GSR_RT_UPLOAD_GPU=0`, side B the defaults (changes 1 and 2, before 3 and 4 were written).
Refresh bytes are exact. CPU is the replay thread's user plus kernel time per vsync frame, median (range).

| Capture | refresh KiB per frame, A | B | thread CPU ms per frame, A | B | hashed frames, different |
| --- | --- | --- | --- | --- | --- |
| surdana | 14255.2 | 178.5 | 9.72 (9.28 to 10.16) | 8.29 (7.96 to 8.62) | 130, 0 |
| flight | 23128.3 | 350.4 | 12.68 (12.33 to 13.04) | 9.57 (8.99 to 10.14) | 94, 0 |
| cave | 8808.2 | 115.3 | 5.85 (5.71 to 5.98) | 5.05 (4.88 to 5.23) | 278, 0 |
| tab | 8772.5 | 103.8 | 8.45 (8.42 to 8.48) | 7.42 (7.38 to 7.46) | 200, 0 |
| ootf | 10219.3 | 154.6 | 9.50 (9.36 to 9.64) | 8.12 (8.07 to 8.17) | 204, 0 |

- The same five captures replayed with the build from before the changes against the new build (scale 8, one run
  each): 130, 94, 278, 200 and 204 hashed frames, none different; all counters equal.
- Whole-replay `syncRT` wall time (one run each, polluted): Surdana 7482 ms to 217 ms, flight 7001 ms to 945 ms.
- Scale 3, Surdana: 2004.6 to 401.7 KiB per frame with changes 1 and 2 before the run join, 130 frames identical.
- The final build with all five changes against the build from before the changes, all five captures, scale 3
  and (before change 5 and the larger stale limit) scale 4: 130, 94, 278, 200 and 204 hashed frames, none different.
  Surdana also at scale 2 with `GSR_RB_VERIFY=1`: 0 mismatches.
- Capture integrity: each capture's MD5 is taken before and after a comparison; all were stable in the runs above.

Live game (new exe, `cpu_scene_bench.py`, owner's ini, standing still, changes 1 and 2 off against on, 2 repeats):

| Scene, round | other games | world fps, off (each run) | on | target refresh MiB per second, off | on | `syncRT` ms per second, off | on |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Surdana, 1 (exe of 10:09, 2 repeats) | 1 to 5 | 94.8, 96.5 | 40.8, 96.5 | 1145 to 1153 | 3.2 to 7.4 | 40 | 1.1 to 2.1 |
| Ravenshold, 1 (2 repeats, my own scale 8 replays ran beside it) | 5 to 9 | 35.0, 43.9 | 20.1, 22.3 | 519 to 725 | 5.7 to 6.8 | 33 to 45 | 3.0 to 3.7 |
| Ravenshold, 2 (final exe of 10:44, 3 repeats, off also has `GSR_RB_PREFETCH=0`) | 2 to 7 | 80.5, 84.0, 33.6 | 77.2, 96.5, 25.7 | 824 to 1548 | 7.3 to 25.9 | 41 to 59 | 3.9 to 7.5 |
| Surdana, 2 (same) | 7 | 17.4, 18.0, 17.3 | 16.5, 18.7, 19.0 | 219 to 272 | 1.2 to 1.5 | 12 to 15 | 0.5 to 0.8 |

- World fps: no difference outside the spread in any round. The low "on" runs of round 1 have `presentCall` at 600
  to 730 ms per second (the present waiting for a GPU other processes were using); round 2 of Ravenshold, run without
  my replays beside it, shows the same spread on both sides. The frame rate followed the number of other games
  (Surdana 95 fps with 1 to 5 of them, 17 with 7).
- Exact in the live game: the refresh bytes (about 1.1 GiB per second at 95 fps before, 3 to 26 MiB after) and the
  refresh count; `syncRT` time fell from 12 to 59 ms per second to 0.5 to 7.5 ms.
- Screenshots of both sides look the same (`build/review/perf-gpu/out/live_surdana_off_left_on_right.png`); live
  frames are not comparable pixel for pixel (foliage wind, orb animation), the replay hashes are the proof.

## Re-measuring

```sh
cmake -S runtime/gs -B build/gs -A x64 && cmake --build build/gs --config Release --target gsr_replay
# capture (from boot, about 5 GB per minute): play_run.py ... --env PS2X_GS_CAPTURE=30000 --env PS2X_GS_CAPTURE_DIR=<dir>
python scripts/perf/gs_replay_ab.py --replay build/gs/Release/gsr_replay.exe --capture <gscap.bin> --scale 8 \
    --texdir <play folder>/textures --repeats 3 --a GSR_RT_LAZY_SYNC=0 --a GSR_RT_UPLOAD_GPU=0
# against an older build: add --a-replay <old gsr_replay.exe> and drop the --a pairs
GSR_RT_REFRESH_STATS=1 gsr_replay <gscap.bin> --scale 8 --first 999999   # per target: refreshes, path, bytes; target memory
GSR_RB_TRACE=1 GSR_XFERLOG=1 gsr_replay <gscap.bin> --first 999999       # why each readback happened
python scripts/perf/cpu_scene_bench.py --exe <play>/drakan.exe --scene surdana \
    --variant off:GSR_RT_LAZY_SYNC=0,GSR_RT_UPLOAD_GPU=0 --variant on --repeats 3 --env GSR_PROFILE=2
```

## Tests

- `gsr_test_rt_sync_rows` (new, 19 checks): the row policy.
- `gsr_test_rt_upload_gpu` (new): 10.7 million texels of GPU replication against `expandRenderTargetRows`, scales 2 to
  8, four rectangle shapes, bindings restored.
- `gsr_test_synthetic`: `lazyTargetRowsTest` added (11 checks): an upload below the drawn rows costs no refresh; an
  upload inside a dirty range widened by a later draw survives the readback; a blended draw over uploaded rows
  refreshes exactly its four page rows and blends with the upload. 376 checks pass at scales 1, 2, 3 and 8 (365
  before). With `GSR_RT_LAZY_SYNC=0` the two refresh-count checks fail, as they should.
- `ctest` in `build/review/perf-gpu/gs` (Release) after changes 1 and 2: 39 tests, 38 passed, `live` skipped (it
  needs a capture). After change 5: 37 passed, `partial_upload` failed (the old expectation, see below), `live`
  skipped. With the test updated: 38 passed, `live` skipped. `gsr_test_live` run by hand on the Surdana capture with changes 1 to 4 and again with all
  five: 34 passed, 0 failed both times.
- `lastPageMirrorTest` (4 checks, change 5): `gsr_test_synthetic` now has 380 checks, all pass at scales 1, 3, 8.
  The existing `solidMirrorTest` asserted the old rule ("target allocations crossing the VRAM boundary retain GPU
  readback") with the very shape of the game's probe; on Direct3D 11 it now expects that sprite to be mirrored and a
  sprite that reaches the rows past the end of memory to keep the readback (`gsr_test_partial_upload`: 97 checks pass;
  the first `ctest` run after change 5 failed there, 2 checks, until the test was updated). The WebGPU build keeps
  the old expectation.
- No dedicated test covers change 3 (the copy is accepted or refused by checks that existed) or change 4 beyond
  `gsr_test_live` and the texture tests.

## What the WebGPU twin needs

- `wgpu_targets.cpp` `syncRT` (line 208): a row-range variant with the `partialSync` flag on its `RT`, called from
  `wgpu_draw.cpp:198` (`rt_sync::drawRows`) and `wgpu_textures.cpp:711` (`rt_sync::sampleRows`) for targets that pass
  the same `lazySyncTarget` test; `rt_sync_rows.h` is backend neutral. Then remove the `#if !defined(GSR_TEST_WEBGPU)`
  around the two counter checks in `lazyTargetRowsTest`.
- The scaled uploads at `wgpu_targets.cpp:232` to 237 and 451 to 455: send native pixels and replicate in a compute
  or render pass (`rgba8unorm` storage or a point-sampled quad), and join vertically adjacent block runs.
- `Stats::targetRefreshBytes` is filled by Direct3D 11 only.
- Change 5: if the twin has the solid mirror, the same bound (mirrored rows inside memory, not the whole target).
- Changes 3 and 4 depend on backend details (readback staging, texture creation thread) and were not looked at there.

## Found, not fixed

| Item | Estimated gain | Risk |
| --- | --- | --- |
| Non-blocking readback for uploads into stale targets: issue the staging copy, let the upload write memory, merge the copy later for blocks the CPU did not write since (block stamps), complete it at once when anything reads those blocks | removes the remaining `Map` waits (19 to 43 per capture after changes 3 and 5) | medium to high: every memory reader must complete a pending copy first |
| The EE waits for GS FINISH before its 32x32 probe read (0x1dda88 / 0x1dbfa8); the probe result is a constant today | the EE and the workers stop running in lock step (40 to 70 percent of EE wall time in the CPU agent's runs) | high: guest hook; fixes the probe at "no occlusion data" |
| Depth buffer sampled as a texture (what the probe asks for) | correctness of whatever uses the probe | medium |
| Scene model capture on the EE with DLSS SR: the model-motion consumer (`modelGuides`, `model_motion.h`) needs new data only for instances whose pose or transform changed since the previous world; an "unchanged" mark per instance from the capture would let it reuse its previous entry | EE time (CPU agent's measurement), not measured here | medium: a contract between `scene_models.h` and the capture; not designed here |
| Row refreshes that remain: 3 to 12 per world frame, mostly 512 pixel rows of the two frame buffers (64 KiB each now) | small | low to medium (world passes read those targets) |
| Scratch targets allocated 512 rows for 128 used: 3 to 17 targets, 150 to 590 MiB at scale 8 in the captures | VRAM, not time | medium (`findRTForTexture` reasons about rows past the target) |
| Motion target `R32G32B32A32_FLOAT` at target size (256 MiB at scale 8), cleared every world | GPU time, unmeasured | medium: precision of DLSS inputs |
| `prepareObjectMotion` maps and vertex passes | 0.5 to 1 ms per frame with DLSS | low to medium |
| Auto scale by output height, or capped with DLSS SR | 10 to 30 percent in the 2026-11-08 runs | picture change |
| CT24 upload fast path | boot and title only | low |
| `readbackRT` and the solid mirror set `rt.seen = mem.clock()` although rows outside the dirty range may be stale; a use with no memory write in between would skip their refresh | correctness, not seen in any capture | low |

## Other areas

- **Order of the Flame, Ruined Village: the GS worker or the GPU finishes last** (CPU agent). Consistent with this
  capture: 674 draw calls per world frame (max 1538) against 60 to 140 on the disc levels, for a third of the
  vertices, so per-draw costs (state, texture resolve and, with DLSS, one object-motion scope per draw) dominate
  there. Which of the two it is was not separated under today's load.
- **Ray-traced shadows on** (one live run, Surdana, owner's ini plus `GSR_RAY_SHADOWS=1`, 6 to 9 other games): 12
  world fps against about 95 without, `[ray-gpu]` compose median 21.7 ms, p90 42 ms, `rayWait` about 42 ms per pass.
  The 2026-11-07 note measured a 12 percent cost on a quiet machine. Not attributable today.
- **Title screen `syncRT` 9640 MiB in 2 s** (loading agent): confirmed, it is ranked cost 1 (13 frame buffer rows per
  frame at the title); 64 times fewer bytes now at scale 8.
- **`textureCreate` 40 to 65 ms on the GS worker** (loading agent): the scope showed 38 to 108 ms maxima in my
  replays under load; change 4 moves the creation of HD and upscaled textures off that thread. Native decoded
  textures are still created there (small).
- **File cache corruption.** `caps/surdana/gscap_20261112_080904.bin` (written 08:10, never opened for writing
  again) replayed with 7611008 GIF tags until about 08:57 and with 7607584 from 08:59, with every build. 64 bytes at
  file offset 326422400 (a 64 byte aligned line: 56 bytes of high-entropy data and 8 zero bytes inside a run of
  `07 07 07 80` pixels) were wrong; MD5 `042e673d...`. One unbuffered read of that page (`dd iflag=direct`)
  returned the right bytes, and buffered reads have been right since; MD5 `bd74d9fd...`, stable in every later check.
  So the disk was right and the cached page was not. Cause unknown (memory, a driver, load). Anything that reads large
  files on this machine can be hit; results of that window were discarded.
- The replay tool cannot leave the title's GUI state (see "Conditions"); a way to feed scene packets would make
  replays match live texture loading.
- The game build was run four times with the shared lock (09:00, 09:46, 10:09, 10:44), each `exit=0`.

## Not established

- Any live frame rate or frame time gain (see the live table and the load).
- GPU time per pass, the DLSS share and the ray pass share: not measured under this load.
- State changes per frame were not counted (the backend has no counter for them).
- The GPU memory growth another agent saw (0.7 GiB in 13 minutes): in the captures (about a minute each) render
  target and depth memory go up and down between 150 and 590 MiB and do not accumulate, and both texture caches drop
  entries unused for 240 frames. No long run was made, so a slow leak is neither shown nor excluded.
- 2560x1440 output was not run; every live run was 3840x2160 borderless.
- A fight with enemies and effects, and ray-traced shadows in a replay (the replay has no ray pass).

## DLSS follow-up (same day)

Asked for after the report above: the DLSS costs listed as "not fixed". Policy lives in backend-neutral headers with
unit tests (`motion_segments.h`, `scene_models.h` / `model_motion.h`, `upscale_render_size.h`,
`drakan_scene_camera_policy.h`); the Direct3D 11 files only call them. Load: 0 to 9 other `drakan.exe`.

### Corrections to the report above

- "With DLSS on the live game issues about a quarter more draw calls than the replay" compared runs of different
  length and was wrong. Measured per world with the same exe (below), a scope note cost 9 to 17 percent more draw
  calls in Surdana and none in the Order of the Flame level.
- `drawObjectMotion` (140 ms per 6 s in the first baseline) is 0.6 to 0.85 ms per world in Surdana in all four
  variants below; the changes here did not lower it outside the spread.

### 1. Scope notes no longer end a batch (`GSR_MOTION_BATCH`, default on)

`Renderer::noteObjectMotion` used to flush the batch at every Begin and End. Now, when the backend accepts it
(`BackendApi::deferObjectMotion`), the note is kept with the number of batch vertices queued before it, and the batch
goes on while the GS state allows. The backend gets the notes with the draw (`setMotionNotes`) and walks the
segments between them (`motion::forEachSegment`): each segment is tracked with the scope, ordinal and vertices the
separate draw had. `motion::History` and the ordinal table use hash maps instead of ordered maps, and a segment
whose scope the history refuses anyway is no longer converted vertex by vertex.

Live, Surdana start, standing, owner's ini, per world between the two bench marks (`DRAKAN_MOTION_LOG`,
`[gsr-profile]`), two runs per variant:

| Variant | draw calls per world | matched segments | matched vertices | rejected segments | scope packets |
| --- | --- | --- | --- | --- | --- |
| `GSR_MOTION_BATCH=0` (4 runs) | 224 to 226 | 35 | 28605 | 163 | 160 |
| default (4 runs) | 188 to 211 | 36 | 28638 | 163 | 160 |

- The one extra matched segment (33 vertices) is an object whose separate draw lay outside the scissor: `draw()`
  returns before motion tracking for a draw with an empty clipped box, so it had no history entry; inside a merged
  batch it is tracked. It draws no pixel either way and has a previous position when it enters the picture.
- Order of the Flame start: 1794 to 1852 draw calls per world in both variants, 86 matched and 1659 rejected segments
  in both. Nothing merges there: the draws differ in state, not in scope.
- Pictures: a scope note changes nothing in a replay (no notes are recorded), so the proof is the unchanged
  segmentation above and the matched live shots below. `motionBatchTest` checks three scoped sprites become one draw
  call with the same pixels.

### 2. Model snapshots for motion only (`DRAKAN_SCENE_CAPTURE_MOTION_ONLY`, default on)

Contract (`scene_models.h`): `gsr::sceneModelsMotionOnly(rayEffects, temporalConsumers)` is true when only super
resolution or neural rendering read the snapshot (`Renderer::sceneModelsMotionOnly()`). Then the capture marks the
snapshot `motionOnly` and leaves out every instance that does not deform and has the transform, opacity, resource
and LOD slot it had at the previous capture, before its source is checked or its mesh looked up
(`unchangedInstances` counts them). An instance that starts to move after being left out carries
`previousTransform` / `previousOpacity`; `motion::Models::prepareFrom` uses that when the previous snapshot lacks the
object and takes the previous mesh to be the current one. An object that was hidden, invalid or new at the previous
capture carries nothing and gets no motion for that world, as before. With any ray effect on the snapshot is full.

| Scene | instances in the snapshot, full | motion only | left out as standing |
| --- | --- | --- | --- |
| Surdana start | 1215 (86 meshes) | 9 to 11 (7 meshes) | 1283 to 1285 |
| Order of the Flame start | 543 to 544 (100 meshes) | 5 to 6 (3 to 4 meshes) | 537 to 538 |

- Equivalence: `test_scene_models` runs a full and a motion-only capture over the same memory for 40 worlds (a posed
  model, a static one that stands, moves on three consecutive worlds, stops, moves again, is hidden and comes back
  elsewhere) and requires the consumer's vertices to be equal value for value in every world.
  `test_model_motion` covers the consumer rules alone.
- Known differences from the full capture, none seen: a mesh whose source content changes in place while its
  instance moves for the first time is taken as unchanged for that one world; the camera-backdrop classification of
  an object is first evaluated when it moves.
- EE thread time: not shown. `cpu_scene_bench`, Surdana, 2 runs each: full 8.67 (8.06 to 9.28) ms per frame,
  motion only 10.13 (6.96 to 13.29) and 11.32 (7.79 to 14.84); the runs with 36 to 45 fps (other games) carry the
  high values. The capture's share has to be re-measured with the host sampler on a quiet machine.

### 3. Auto scale 8 at 3840x2160 with DLSS

What the rule does: `AutoInternalScale::choose` (`gs_gsr_bridge.cpp`) takes `ceil(longer view side / 512)` for the
512x512 frame, 8 at 3840x2160 in 16:9, and does not look at the upscaler. The upscaler's output is the frame times
the scale, 4096x4096, and its render rectangle the mode's ratio of that, 2730x2730 in quality mode. The present pass
keeps 3840x2160 of the 4096x4096.

Is 8 wasteful: horizontally no. Quality mode for a 3840 wide picture reads 2560 columns; scale 8 gives 2730, scale 7
gives 2389 and an output of 3584 that the present pass would have to stretch to 3840. Vertically yes: the square
frame is rendered and upscaled at 1.9 times the lines the screen has (2730 rows where quality mode for 2160 lines
reads 1440; 16.8 megapixels of DLSS output for an 8.3 megapixel screen). No integer scale fixes that, because one
scale serves both axes: every lower scale loses horizontal resolution the player sees.

What was built: `upscale::renderSize` (`upscale_render_size.h`) with a second rule that sizes the render rectangle
per axis against the screen pixels the frame covers (2560x1440 here, half the shaded pixels; the DLSS output stays
4096x4096). `GSR_UPSCALE_VIEW_RENDER=1` turns it on; **it is off by default.** DLSS accepted the unequal ratios
(1.6 by 2.84) without a fault. Matched shots at the Surdana start (`build/review/perf-gpu/out/crop_old_new_view.png`,
`crop2_old_new_view.png`): the picture is close but not the same, mean absolute difference 3.0 against 0.5 to 1.4
between two runs of one setting, and no frame rate gain showed in two runs each under load (60.9 against 60.8 world
fps; Ravenshold one run each, 66.3 against 57.5). It changes what the player sees without a proven gain, so the
default stays. The real fix is an internal scale per axis (4096x2560 targets at this screen), which touches every
`rt.w * scale` in the renderer and was not attempted.

### 4. Other DLSS findings

- **History dropped on slow frames (fixed).** `drakan_scene_camera_policy.h` reset the temporal history when two
  world cameras were more than 6 vertical blanks apart, i.e. whenever a world took over a tenth of a second. In the
  Order of the Flame runs at 8 to 10 fps, 9, 44, 34 and 68 percent of the worlds had no matched segment at all
  (1745 rejected instead of 86 matched and 1659 rejected), with and without the changes above. One reset caught with
  `DRAKAN_CAMERA_RESET_DIAG=1` at 48 fps reads `reason=8` (TickGap), `deltaUnsigned=8`. The gap is now 30 blanks
  (`kResetTickGap`); `DRAKAN_CAMERA_RESET_TICKS=6` restores the old rule. The existing policy test asserted the six
  tick threshold and was changed. Not re-measured at 8 to 10 fps: the machine ran the scene at 48 fps afterwards
  (0 and 1 worlds without matches with the old rule, 0 and 0 with the new one, of about 965).
- **Earlier changes under DLSS.** Matched shots at the Surdana start, foliage wind off, the exe of 05:30 (before any
  change of this review, with the stale-target fix) against the final exe: mean absolute difference 1.46 and 0.48
  for the two pairs, against 1.43 between the two old runs and 0.46 between the two new ones. Crops side by side
  look the same (shadow, orbs, HD textures, foliage edges). Live frames cannot be compared exactly (DLSS jitter phase).
- Motion vectors do not depend on the target refresh or readback changes: those touch scratch targets only, and
  `lazySyncTarget` excludes every target with world, temporal or neural state.
- The DLSS pass's own GPU time was not measured (`GSR_UPSCALE_TIMING` printed nothing usable in these runs).

### Replay proof, every frame

The build from before all changes against the final build (all changes of this note; the DLSS ones are inert in a
replay), scale 3, HD textures, the display of **every** frame hashed: Surdana 2585, flight 1864, cave 5558, tab 3984,
Order of the Flame 4074 frames, none different; capture MD5s stable.

### Tests (follow-up)

- New: `gsr_test_motion_segments` (8 walks), `gsr_test_upscale_render_size` (14 sizes), `motionBatchTest` in
  `gsr_test_synthetic` (385 checks pass at scales 1 and 3; with `GSR_MOTION_BATCH=0` its draw-count check fails as it
  should), the motion-only sections in `test_scene_models` and `test_model_motion`.
- Changed: `scene_camera_policy_tests.cpp` (32 checks pass, built by hand with `cl`; the root `runtime-tests` preset
  was not run).
- `ctest` in `build/review/perf-gpu/gs` (Release) with everything: 44 tests, 43 passed, `live` skipped;
  `gsr_test_live` by hand on the Surdana capture: 34 passed, 0 failed.
- Game builds 5 and 6 with the shared lock: `exit=0`. A formatting-only pass over five files followed build 6.

### What the WebGPU side needs (follow-up)

- Batching: implement `deferObjectMotion()` / `setMotionNotes()` and walk `motion::forEachSegment` wherever it
  tracks object motion; until then the core keeps ending the batch at each note for that backend. The WebGPU backend
  has no object-motion consumer today, so it can return true and only apply the notes.
- Motion-only snapshots: nothing, if it reads snapshots through `motion::Models`; a backend that walks
  `SceneModels::instances` itself for anything but ray effects must honour `motionOnly`.
- Render rectangle: call `upscale::renderSize` instead of its own ratio arithmetic.
- The tick gap is guest side and applies to every backend.

### Not verified (follow-up)

- Any frame rate or EE time gain from 1 and 2 (counts only).
- The tick-gap fix at the frame rates where the fault occurred.
- The view rule in motion, and its gain on a quiet machine.
- 2560x1440, and the Vulkan agent's lifted code paths (its edits were in the tree during the last builds).
