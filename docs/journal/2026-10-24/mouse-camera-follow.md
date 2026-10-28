# Mouse feel review: camera rubber band and pitch gain

User report: mouse movement doesn't feel like native mouse and keyboard. The target is Drakan: Order of the Flame /
modern third-person mouse look.

## Reviewed and found sound

- Raw Input (`HostInput.cpp`): relative counts only. Nothing is dropped or capped; everything is accumulated
  between world ticks (`FrameInput.cpp`).
- Direct look (`DrakanInput.cpp`): counts x 0.002 rad, no dt multiplier, no curve. The native smoothed rates
  `cam+0x9c/a0/a4` are cleared before `Camera_UpdateLook`, and the stick re-centring is off.
- Ordering (trace `DRAKAN_TRACE_FN=44de40,41f848,13ee90`): Controller C -> `44de40` -> heading turn `13ee90` ->
  camera tick `41f848`, every tick. No one-tick delay.
- Swap chain: `SetMaximumFrameLatency(1)` with a waitable object (`d3d11_device.cpp`).

## Causes found (static, `build/recomp_ui/alldecomp/all_00414280.c`, `FUN_0041f848`)

1. The camera position eases behind Rynn by `min(1, cam+0x210 * avg dt)` per tick. `cam+0x210` tends to the
   camera property Rubber Band Strength (2.0), a ~0.5 s time constant. The orientation copies her matrix at once,
   so a mouse turn rotates the view, then the camera body swings round behind her.
2. The plain on-foot branch shows `1.8 * cam+0x84` as view pitch, while the heading reaches the view 1:1.
   Vertical mouse movement was 1.8x as fast as horizontal.

The table of fields and constants is in `docs/reference/input.md` section 15.

## Change

- Glue hook on `0x41f848` -> `InputModule::beforeCameraFollow` / `afterCameraFollow`. While mouse / keyboard own
  on-foot gameplay, `cam+0x210 = [mouse] camera_follow` (30 /s) for the call. The native value is restored
  afterwards unless a collision push reset it inside the call. It is skipped in cut scenes, for camera flags
  0x20 / 0x80 / 0x100 / 0x20000, while the 0x21c blend timer runs, on Arokh, in menus, and while a controller
  owns the input.
- `[mouse] pitch_view_gain = 1.8`: vertical counts are divided by it in the plain branch only.
- F1 > Controls > Camera follow (0 = Original .. 60).
- Tests: `input_test` 3285 checks, 0 failures. That includes the new `testCameraFollowAndPitchGain`; earlier
  tests pin `pitch_view_gain = 1` through `legacyPitch()`. `settings_test` 339 and `settings_menu_test` 125 pass.

## Live A/B (Surdana, `play_run.py`, 600-count turn = 68.8 degrees, world rate uncapped)

Recipe: `mouse 600 0` at 1000 ms, RAM dumps at +0/50/150/300/600/1200 ms, private ini copies via
`DRAKAN_INPUT_INI`. Camera position comes from `*(cam+0x148)+0x14`, Rynn from `*(*(*(ctx+0x4b0)+0xfc)+0x10)+0x80`.
The table gives the camera's orbit angle short of its settled place behind her, and its horizontal distance from her
(settled 1061).

| after the turn | `camera_follow = 0` (native) | `camera_follow = 30` |
| --- | --- | --- |
| 50 ms | 55.3 deg short, dist 939 | 3.5 deg, 1019 |
| 150 ms | 32.4 deg, 877 | 0.1 deg, 1061 |
| 300 ms | 13.8 deg, 935 | settled |
| 600 ms | 2.7 deg, 1028 | settled |

Natively the camera also cuts the corner: it comes up to 18 % closer to Rynn while it swings round. The native
`cam+0x210` was 4.5..5.0 in that level (level data overrides the 2.0 default). The hook leaves it frozen at its
pre-hook value while the mouse owns the camera; it resumes its own easing afterwards.

## Live checks of pitch, walking and walls

Three configurations: `orig` (follow 0, gain 1), `new` (30, 1.8) and `newg1` (30, 1). The view angles come from
the camera matrix at `*(cam+0x148)+0x20`, where row 0 is forward.

- **Pitch gain confirmed.** For 100 counts up then 100 counts right:
  - `orig`: view pitch 20.6 deg (0.36 rad), yaw 11.6 deg (0.20 rad).
  - `new`: pitch 11.5 deg (0.20 rad), yaw 11.6 deg. The axes now match.
- **Walking backwards (S, 3 s).** The native camera falls in from 1141 to ~870 units behind Rynn and stays
  there while she moves. With `new` it holds 1100 (constant distance).
- **Small first-look-up loss (unexplained).** With follow 30, the first fast look-up sometimes ends ~0.009 rad
  short on `cam+0x84` (about 1 degree of view):
  - Repeat runs of 180 counts up: 3 of 6 `new` runs gave 0.191 instead of 0.200, 0 of 5 `orig` runs did.
  - The next look-up adds exactly. The run with `DRAKAN_INPUT_LOG=1` (every frame) didn't show it, so it is
    timing dependent.
  - It is not the native clamp: `Player_CameraLookTick` passes the constant 0.6283, so the cap is
    0.7 x 0.6283 = 0.44.
  - The writer was not found. The candidates are Camera_UpdateLook's rate or soft-limit paths reacting to the
    camera's new position in the same tick.
- **Walls.** In a 10 s back-up-and-sweep path, neither run showed a clean camera collision. The paths diverged
  where the native run snagged on geometry (camera distance fell to 797 there). Unverified.

## Open

- Wall behaviour: the native collision now runs unchanged (no k = 1), but it was not exercised against a wall.
- Subjective feel at the 30 /s default, and collision pushes at k = 1 (the camera snaps in front of walls instead
  of easing) need a play test.
- Bow sight (`FUN_0032a608`, bit5) and Arokh flight keep the native camera.

## Follow-up: microstutter with the rigid follow

User report on the rigid build: movement feels awkward and micro-stutters. Per-tick logs (`[in-pos]` now carries
`camPos=` from `*(cam+0x148)+0x14` and the tick `dt=`). Walk W 4 s, then turn 8 counts / 20 ms. The build includes
the other session's tick snapping, so ticks were 16.7 ms +- 0.0 in every run. Jitter = mean / max deviation of the
per-tick speed from its 7-tick average.

| `camera_follow` | Rynn, steady walk | camera, steady walk | camera, walk + turn | turn settled after |
| --- | --- | --- | --- | --- |
| 0 (native rubber band) | 2.8 % (max 15 %) | 0.7 % (5 %) | 0.5 % (1 %) | > 600 ms |
| 1000 (rigid, previous build) | 3.1 % | 5.6 % (138 %) | 4.6 % (95 %) | 1 tick |
| 10, rigid + smoothed pivot shift | 2.8 % | 8.7 % (181 %) | 6.0 % (235 %) | 1 tick |
| **8, orbit carried + native easing** | 2.9 % | **1.2 % (7 %)** | **0.7 % (1 %)** | **1 tick** (71.4 deg at 50 ms) |

- Rynn's own root motion wobbles with her gait (27..48 units per tick at a steady walk). The native rubber band
  hides that; a rigid camera shows it as judder.
- The pivot-shift attempt moved the camera after the native collision test. Single ticks then landed in
  scenery and the next collision test yanked them in (one-tick jumps of ~200 units, f=1012 and f=1124).
- The final design turns the previous camera position round Rynn by her ground-plane heading change before the
  tick, then lets the native easing (rate 8/s) and collision run unchanged.
- Sign convention: heading row 0 rotates opposite to positions in the ground plane. Measured: heading -68.8 deg
  moves the settled camera bearing `atan2(dz, dx)` by +68.8 deg.
- The first-look-up pitch loss is gone: 4 of 4 runs of 180 counts gave exactly 0.200 (the rigid camera caused it).

## Follow-up 2: vertical jitter, one-tick hitches, mouse sampling

User report on the orbit build: starts okay but the camera still jitters, especially looking up / down.

- **Pitch was not carried.** Looking up / down swings the native target on an arc round Rynn's anchor (player
  `+0x51c..`, y ~240, bobbing a few units with her animation): between pitch 0 / 0.2 / 0.311 the camera's angle
  round it changed by 20.1 / 31.5 deg against a view tilt of 20.6 / 32 deg. The orientation snapped while the
  position eased behind. The previous camera position is now also turned about the camera's right axis
  (column 0 of `*(cam+0x148)+0x20`) by 1.8 x the `cam+0x84` change, the sense taken from the back axis
  (column 2). Measured (3 counts / 10 ms up and down): camera arc angle + view tilt stays at 9.45..10.07 deg,
  i.e. locked. Yaw now uses the same Rodrigues rotation (+y, angle = heading change).
- **One-tick hitches.** 2 of 9 walk runs had one tick where the camera moved a third of Rynn's step (63 % =
  1 - 2.73 / 8: that tick ran at the native `cam+0x210`, so a single tick had failed an ownership check). The rate
  is now rewritten every tick and handed back only after 10 ticks without ownership; the orbit state survives
  such gaps too. Afterwards: 0 camera-only hitches in 4 runs, camera jitter 1.1..1.3 % at a steady walk (Rynn's
  own gait 2.8..3.0 %), turns settled within the first tick.
- **Mouse sampling.** Each frame took the counts since the previous `beginFrame`, whose host time wobbles by about
  1-2 ms around the snapped 16.7 ms tick, so a steady sweep turned ~6 % unevenly per frame. Motion is now windowed
  on the refresh grid (`runtime/input/README.md`, `testRefreshAlignedMouseWindow`: frames +-2 ms off the grid
  still get 16 or 17 counts of a 1 count / ms sweep). Scripted runs bypass the raw-input accumulator, so this part
  is CPU-tested only.
- A sudden 180-count look-up still ends ~0.009 short of the counts when the camera drops ~330 units in one tick. A
  gradual sweep is exact (0.3333 after 100 steps of 3 counts). This is presumably the native ground check; not
  traced further.
