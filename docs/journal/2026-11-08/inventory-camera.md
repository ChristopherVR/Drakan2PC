# Inventory close: the camera returned twice (GitHub issue #1) - 2026-11-08

Report (v0.1.9): opening the inventory zooms in on Rynn correctly; on close the camera moves back to its normal
position, then jumps back to the inventory position and moves back a second time.

## Root cause

`InputModule::beforeCameraFollow` (`runtime/input/src/DrakanInput.cpp`, the orbit carry) turns the camera round Rynn
by her heading change since the previous camera tick, so mouse / keyboard turns do not swing the camera late.

The game re-seats Rynn's transform when the inventory closes: `Player_SetState 2ffaa8` (called from `42f640`) sets
`player+0x94` bit `0x20000`, and the state tick `2ffd38` ends it with `300468` once her return-to-idle animation
finishes (about 0.5 s later, 24 to 30 frames). `300468` rewrites her transform and her heading flips by exactly 180
degrees in one tick (`[in-pos]` row0 `(-0.0461 0 -0.9989)` -> `(0.0461 0 0.9989)`, yawA -1.617 -> 1.525); the model
looks the same because the animation compensates. The orbit carry took the flip for a turn and rotated the camera
180 degrees round her in one frame (camPos x 448881 -> 447155, then the native ease from the wrong side back to
448960).

Why only after the ease: the modern Tab screen finishes the close animation at once (`hookViewDraw`), so the screen is
gone 1 frame after the key and the flip lands about 0.5 s later, after the ease back is over (the reported "jump
back"). With the native screen the flip coincides with the end of the slide-out animation, where the carry is still
suspended by the open menu, so it is mostly hidden; it still shows when the animation ends a few frames before the
flip (seen once in a native-screen run).

## Fix

`kMaxCarryYaw` (2.5 rad): a heading change per camera tick above it is not carried (the camera stays put and the native
ease continues). Real steering turns are far below it per tick. Test: `input_test` (half turn in one tick keeps the
camera).

## Verification (in game, `dist/DrakanPC`, harness recipes in the scratchpad)

* Before: `[in-pos] camPos` per frame after the close: smooth ease from 448332 to 448885, then 447206 at +24 frames
  (modern, tutorial box dismissed, second open as well), and the same jump with the native screen on the second close.
  Frame series (`shot` every sample): Rynn close and zoomed, back to normal, then a close-up from behind again.
* After: no jump in either screen mode (consecutive-frame camPos deltas all within the normal ease, no step above 150
  or below -60 units); frame series shows one monotonic return.
* The player+0x94 / cam+0x48 diffs (`300468` clears `0x20000` and `cam+0x48` bit `0x1000000`) were taken with a
  temporary per-frame RAM diff probe, not kept.
