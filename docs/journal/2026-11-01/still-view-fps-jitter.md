# Still-view fps jitter (uncapped, 4K scale 4, vsync off), 2026-11-01

Report: fps jumps around even when looking at one spot. Measured with
`play_run.py --recipe fps_steady_wall.txt --env DRAKAN_FRAME_AUDIT=2 --env PS2X_PERF=1` on Surdana (the player's installed
`drakan.ini`; another game instance was running throughout, so absolute numbers are pessimistic and vary run to run).

## Symptom

* World fps 123-150 from one second to the next, present gaps mean 7.3 ms, SD 2.4 ms, p99 17.4 ms.
* A 16-19 ms present gap recurs every **32 world frames** (the world serial of consecutive hitches differs by exactly 32; ~4 per
  second, 78-86 per 20 s). Not the FPS overlay (`DRAKAN_SHOW_FPS=0` gave 78 against 86).
* In a hitch the engine update `U` runs on time, the EE display flip `F` follows only after the next guest vblank `V`, and that
  `V` is processed 2-15 ms late: the EE thread was busy, not waiting.

## How it was found (what was ruled out)

* Scheduler: no guest thread blocks in the hitch (`blockCurrent` never fires for the main thread); the GS worker is idle
  (`WorkerIdle` 7-8 ms), so it was not waiting on the renderer lock, a readback, the VU1 worker or the commit retirement
  (scopes for all of them stayed under 2 ms). The EE thread's `GuestDispatch` scope covered the whole gap.
* A timestamped EE-thread RIP sampler (`DRAKAN_FRAME_AUDIT=3`, `ps2_perf.cpp`, resolved by `scripts/perf/ee_samples.py`) put
  ~75 % of the samples inside a hitch in `drakan_scene_models::hashBytes` and the rest in `Capture::texture`.
* `Capture::capture` rebuilds the resident texture set `if (textures.empty() || (++ticks % 32) == 0)`: for each resource a
  128x128 sample, then FNV-1a over 64 KB, byte-serial (about 4 cycles per byte). Timed: 151 textures, 11 ms, per refresh
  (`loadTexture` part 50 us). That is the 32-frame period; scene-model capture runs whenever the ray / NR effects are on.

## Fix

* `captureHashWide` (`object_motion_capture.h`): 4 independent multiply/xorshift lanes over 32-byte blocks, ~10x faster. Used
  for texture identities only (`drakan_scene_models.h`, `drakan_scene_assets.h`); identities are compared inside one run.
* `Capture::refreshTextures`: the refresh is resumable, ~250 us of work per capture (always at least one texture), and the new
  set (and the loaded-texture merge) replaces `textures` only when complete. First fill and level change stay synchronous.
* Test: `gsr_test_scene_models` checks the hash (every byte position, tail, length, seed); with level RAM supplied it also
  checks that the sliced refresh settles on the identical texture set.

## Result (same recipe, settings and machine load)

| | before | after (4 runs) |
|---|---|---|
| present gaps > 14 ms in 20 s | 78-86 | 1, 2, 5, 22 (one 2 s disturbance) |
| p99 gap | 17.4-17.6 ms | 9.7-12.9 ms |
| gap SD | 2.4 ms | 1.2-1.6 ms |
| frames/s | 123-137 | 125-157 |

## Left

Remaining spread is the content itself (gaps 4-10 ms, VU1-bound; EE thread ~95 % busy with the VU1 handoff spin). Not
investigated: whether the same scene-model capture costs time every frame (mesh hashing in `Capture::mesh`) when many
animated objects are on screen.
