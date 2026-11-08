# Performance review: where the frame goes with the shipped settings (2026-11-08)

Question: "overall performance feels poor, also in multiplayer missions, caves and arena maps". Builds on
`mp-performance.md` (no MP-specific cost; the arena equals single player at the same spot of Level 1E) and
`perf-leftovers.md` (FINISH busy-wait at 4B spot C). No code was changed; this note ranks the hot spots by measured
cost and proposes fixes.

## Method

* Private play folder `build/perfrev/play` (hard links of `dist/DrakanPC`, the 18:07 exe, plus
  `build/final-game/bin/drakan.map`). The shipped `drakan.ini` was used unchanged: 3840x2160 borderless,
  `scale = auto`, `dlss = quality`, `hd_textures = 1`, `texture_upscale = auto`, `anisotropic = 16`, neural rendering,
  ray water / reflections / shadows / GI all **off**, vsync off, world rate uncapped.
* `play_run.py --level 1e` with a `place 579166 128750 440175` recipe (the depths arena spot of `mp-performance.md`, 30 s
  standing) and `--level 1c` (Catacombs start, a cave), `PS2X_PERF=1 DRAKAN_RUNTIME_PROFILE=1 GSR_PROFILE=2
  DRAKAN_PLAYTEST_GOD=1`, audio off. One host-sampler run (`PS2X_HOSTPROF=20 PS2X_HOSTPROF_DELAY=40
  PS2X_HOSTPROF_STACKS=1`, DLSS off) and one `GSR_XFERLOG=1` run.
* Before every run the wrapper waited for no other `drakan.exe` (two checks 15 s apart, at most 8 minutes); the
  `*.others` files record the count at start and end. Another agent ran the game in between, so several runs overlap it
  at the end. Numbers are medians of the 2 s windows while placed; world fps = `flips2/s`.
* Spread: four identical baseline runs gave 84.1, 84.9, 62.1, 68.6 world fps (the 62.1 run had `presentCall` 90 ms per
  2 s, i.e. a GPU shared with something else). Positions were checked per run (`[in-pos]`); one scale-5 run that ended
  away from the spot was discarded. The per-frame GS worker costs below are the stable measure.

## Runs (Level 1E arena spot unless noted)

| Run | Setting changed | World fps | GS worker busy per world frame | `syncRT` per frame | object motion per frame |
| --- | --- | --- | --- | --- | --- |
| base1..4 | none (auto scale = 8 in game) | 84.1, 84.9, 62.1, 68.6 | 11.5 to 15.8 ms | 1.8 to 2.6 ms | 1.2 to 1.7 ms |
| s6 | `PS2X_GS_SCALE=6` | 85.2 | 11.5 ms | 1.4 ms | 1.6 ms |
| s5b, s5c | `PS2X_GS_SCALE=5` | 93.6, 96.6 | 10.1 to 10.5 ms | 0.9 ms | 1.5 ms |
| nodlss, nodlss2 | `DRAKAN_DLSS=off` (scale 8) | 98.2, 105.4 | 9.3 to 9.8 ms | 2.0 to 2.1 ms | 0 |
| nd5 | DLSS off, scale 5 | 101.8 | 9.5 ms | 1.1 ms | 0 |
| s2 | DLSS off, scale 2 | 139.3 | 6.9 ms | 0.3 ms | 0 |
| nohd, nohd2 | `GSR_TEXREPLACE=0 GSR_TEXUP=off` | 70.9, 77.8 | 12.6 to 13.8 ms | 2.1 to 2.5 ms | 1.6 ms |
| cat1 | Catacombs start, shipped settings | 71.2 | 13.8 ms | 2.1 ms | 2.3 ms |
| cat1s5 | Catacombs start, scale 5 | 93.8 | 10.5 ms | 0.8 ms | 2.1 ms |

"GS worker busy" is 2 s minus `workerIdle` divided by the world frames. With the shipped settings the GS worker is
never idle (`workerIdle` 0 to 70 ms per 2 s): **the GS worker thread is the limit**, not the GPU (`presentCall` 10 to
15 ms per 2 s in the clean runs) and not the VU1 worker (about 60 % busy, 370 M pairs/s). The EE thread shows 60 to 97 %
but most of the difference is spinning (section 2). HD textures cost nothing measurable (the `nohd` runs are within the
baseline spread). The ray effects are off in the shipped `drakan.ini`, so they do not explain the complaint.

## 1. Auto internal scale 8 at 4K and the CPU-expanded target uploads

`AutoInternalScale::choose` (`ps2recomp/ps2xRuntime/src/lib/gs/gs_gsr_bridge.cpp:84`) takes `ceil(max(vw, vh) / 512)`
for the native-512 display: at 3840x2160 with the 16:9 viewport that is `ceil(7.5)` = 8 (the log shows `scale=6->8`
when the world opens), so every 512-wide target is 4096 wide, 64 times the native pixel count.

The cost that grows with the scale on the CPU is `syncRT` (`runtime/gs/src/d3d11_targets.cpp:355`): when the game
uploads texture data into memory a target occupies, the whole page row is refreshed by `uploadTargetRect`
(`d3d11_targets.cpp:330`), which reads the row, **replicates every pixel scale x scale times on the CPU**
(`expandRenderTargetRows`) and sends the result with `UpdateSubresource`. At scale 8 that is 9.6 to 12.4 GB per 2 s
(`syncUpload` in `[gsr-profile]`), 1.8 to 2.6 ms of the GS worker per frame, against 0.9 ms at scale 5 and 0.3 ms at
scale 2. The host sampler (DLSS off, scale 8) puts 22 % of the GS worker's samples in the NVIDIA user-mode driver and
7 % in `memcpy`, plus `expandRenderTargetRows` 3 % and `readRectCT32` 1.4 %. DLSS SR work also scales with the target
size (motion pass, temporal resolve and SR run at the scaled resolution).

Measured: scale 5 instead of the automatic 8 gives 1E 94 to 97 against 62 to 85 world fps, Catacombs 94 against 71.
Without DLSS the 1E spot is EE/serialisation bound and scale 5 and 8 measure the same (102 against 98 to 105).

Fixes:

* (a) Pick the auto scale from the output height, or cap it (6 or 7), when DLSS SR reconstructs the world anyway; or
  round 7.5 down (3584 for a 3840 output is 93 % width, with the sharp present filter). Low risk, a quality trade;
  +10 to 30 % world fps on a 4K screen. Workaround for players now: `[video] scale = 5` (or 6) in `drakan.ini`.
* (b) Upload the row at native size and expand it on the GPU (a point-sampled draw or compute pass into the target,
  like `PartialUploadGpu` in `partial_upload_d3d11.h` does for 8x8 patches). Removes most of `syncRT`, the 4 to 6 GB/s
  upload and much of the driver time, independent of the scale. Medium risk (all target refresh paths go through it;
  `GSR_RB_VERIFY=1` and the `rt_readback` tests cover the equality). Expected 1.5 to 2 ms per frame on the GS worker at
  scale 8, i.e. 15 to 20 % of its frame.

## 2. The per-frame GS-to-EE readback serialises EE, VU1 and GS

`GSR_XFERLOG=1` shows one local-to-host transfer per world frame at the 1E spot: `dir=1 src bp=3a60 bw=1 psm=0 32x32`
(4 KB), always followed by an upload of 64x64 CT24 to bp 0x3a80, i.e. the game renders a small image, reads it back
and makes a texture from it on the EE. The call chain from `hostprof_stacks.csv` is `f_0x1d8798 -> f_0x1df670 ->
f_0x1e88d0 -> f_0x1dda88 -> f_0x1dbfa8` (JAL at 0x1ddb18), inside the world render. `f_0x1dbfa8` polls GS CSR FINISH
before the transfer: **36 % of the EE thread inclusive** at the 1E spot (DLSS off), 23 % at 4B spot C
(`perf-leftovers.md`). FINISH is set only when the VU1 worker and then the GS worker have processed everything the EE
queued before it (`gs_core.cpp:1117`, bridge `onFinish`), and the read itself drains the bridge queue
(`GsrBridge::consumeLocalToHost`, `gs_gsr_bridge.cpp:1070`) and maps the target back. So within each frame the EE
cannot run ahead of the renderer: frame time is close to EE work + GS tail instead of max(EE, GS). This is why the EE
looks 95 % busy while its own work is about 5 ms per frame.

Fix: find out what 0x1dda88 reads (32x32 CT32 at 0x3a60; candidates are a dynamic shadow/blob or a light/flare
visibility texture). If it only feeds a visual, hook 0x1dda88 to skip the FINISH wait and serve the previous frame's
copy (read back asynchronously after the GS worker reaches it). Expected: up to max(EE, GS) instead of the sum, about
+20 to 30 % when the GS worker is not saturated (DLSS off, scale 5 or less, and after fix 1). High risk: guest
semantics, a one-frame latency in whatever uses the image, needs RE and visual checks.

## 3. DLSS SR object motion on the GS worker

`prepareObjectMotion` (`runtime/gs/src/d3d11_world.cpp:240`) runs for every world draw while DLSS SR is on: a
`GetDesc`, a `std::map` ordinal lookup, `motion::History::track` (`motion_history.cpp:46`: `ambiguous_`,
`current_`, `previous_` tree lookups, a node insert and three per-vertex loops; draws average about 240 vertices) and a
second vertex-buffer map. Measured 1.2 to 2.3 ms per frame (10 to 17 % of the GS worker; Catacombs is the highest).
DLSS on against off at scale 8: 84 against 98 to 105 world fps. Every object-motion scope also flushes the current
batch (`Renderer::noteObjectMotion`, `gs_core.cpp:2042`), which is needed for attribution.

Fix: hashed or sorted-vector maps reused across frames instead of `std::map` nodes, one key lookup per draw, one fused
SSE pass over the vertices (finite check, previous-position projection, copy). Low to medium risk; expected 0.7 to 1 ms
per frame with DLSS on.

## 4. GIF parsing and vertex conversion

Host samples of the GS worker (DLSS off): `gifSubmit` 5.3 %, `kick` 4.7 %, `toDV` 3.2 %, `handlePacked` 2.9 %,
`emitTri` 2.9 %, `beginDraw` 1.0 %, `buildState` 0.8 %, about 20 % in total for ~94 k vertices and ~13 k GIF packets
per frame (`runtime/gs/src/gs_core.cpp:1365`, 1423, 486, 505, 710). Fix: a fast path for PACKED tags whose register
list is the usual ST/RGBAQ/XYZF2 strip or list (decode the whole loop without the per-register switch and emit
`DrawVertex` directly). Medium risk (GS semantics, covered by `test_synthetic` and the capture replays); expected 0.5 to
1 ms per frame.

## 5. Small per-draw costs

* Vertex upload: one `Map(NO_OVERWRITE)` per draw (`d3d11_draw.cpp:767`), 0.4 to 0.6 ms per frame. Batch the frame's
  vertices into one map per flush window. Low gain.
* `hashClut` (`d3d11_textures.cpp:663`) hashes 256 CLUT entries serially for every indexed-texture draw; a CLUT
  generation counter bumped by `loadClut` would make it a compare. `resolveTexture` exclusive is about 0.2 ms per frame.
* Readback `Map` waits: 0.1 to 0.6 ms per frame here, not a limit.
* Present handoff (`waitForHandoff`): about 0.25 ms per frame.

## 6. Already known, still open

* Paused menus redraw the whole world (`mp-performance.md`).
* Two instances on one GPU wait on each other's GS worker.

## Summary ranking

| # | Hot spot | Measured cost | Fix | Gain | Risk |
| --- | --- | --- | --- | --- | --- |
| 1a | auto scale 8 at 4K | scale 5: +10 to 30 % world fps | choose auto scale by height / cap with DLSS SR | +10 to 30 % | low (quality) |
| 1b | CPU pixel replication of target rows | 1.8 to 2.6 ms/frame, 5 to 6 GB/s | GPU expansion of native rows | 15 to 20 % of GS frame | medium |
| 2 | per-frame 32x32 readback + FINISH wait | 23 to 36 % of EE | serve last frame's copy after RE | up to +20 to 30 % when not GS bound | high |
| 3 | DLSS object motion | 1.2 to 2.3 ms/frame | flat maps, fused vertex pass | 0.7 to 1 ms/frame | low-medium |
| 4 | GIF parse / vertex build | ~20 % of GS worker | packed strip fast path | 0.5 to 1 ms/frame | medium |
| 5 | per-draw map / CLUT hash | under 1 ms/frame | batch maps, CLUT generation | small | low |

Logs: `build/harness/perfrev_*`, summaries with `build/perfrev/summ.py <run>`, caller analysis `build/perfrev/callers.py`.
