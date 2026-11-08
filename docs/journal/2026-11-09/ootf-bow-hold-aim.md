# OotF bow: hold to aim, walk in every direction

Goal: the default `bow_aim = ootf` should behave like Drakan: Order of the Flame (hold LMB to draw and aim with a
crosshair, release to shoot, walk in any direction throughout). `native` (PS2) is unchanged.

## Findings

- Before: `ootf` fired on LMB press and used the native Square attack. The grounded shot starts the whole-body archer
  clip on channel 0 (`0x3016e8` via `0x32cf30`, return `0x301b7c`), which replaces the run/back/strafe clip and its
  root motion, so the legs stop for the shot. The RMB sight already walks in all directions (2026-11-04 notes).
- The hold itself touches no guest state: with LMB held and a movement key down, Rynn keeps moving (unarmed hold
  check in a live arena run: backward speed unchanged while LMB was held).

## Change (runtime/input)

- `Config::bowHoldAim` (default on, cleared without the advanced input hooks); `InputModule::bowHoldAim()` is true
  for `ootf` and `modern`, false for `native`. The old `modern` aim machinery (zoom, shoulder offset, crosshair,
  release tap, arrow retarget at the crosshair ray) now serves `ootf` too. With the RMB sight open LMB still fires on
  press.
- `bowShotChannel`: a shot released from the hold while `player+0x504` / `+0x508` is non-zero also moves the archer
  clip to the torso channel (as airborne shots already did), so the legs keep running, backing away or strafing.
- `bowAutoTarget` stands aside for a launch that `beforeBowLaunch` pointed at the crosshair (`bowAimedLaunch_`), so the
  third-person auto-aim cannot override the crosshair.
- Settings text, `controls.md`, `runtime/input/README.md`, `docs/reference/input.md` section 16 updated.
- Tests: `bow_aim_test` updated (classic/ootf holds, ps2 presses) plus `testWalkingShotChannel`.

## Verified / not verified

Unit tests pass (`ctest -R "bow|input"`); incremental game build succeeded. Live bow run NOT achieved: the arena
pickup found no loose bow and the `play_run.py` bow-pickup recipe left Rynn without a bow in this build (OotF Rynn
model), so no bow shot was observed in game. The torso-channel redirect for walking grounded shots and the exact
crosshair convergence (arrow aims at the camera ray 8000 units ahead; near targets sit slightly off by the shoulder
offset) remain to be checked live. Crossbows (holder mode 4) are not covered.
