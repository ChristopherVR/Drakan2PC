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

## Follow-up: crossbow and drawn-pose limits

- Crossbow (holder mode 4): `updateBowAim` now treats mode 4 like the bow for the hold-to-aim (zoom, crosshair,
  release tap), and `bowShotChannel` accepts it for a walking release. The arrow retarget runs in the launch hook
  `0x20e6c8` and relies on that function also launching crossbow bolts (unconfirmed). Reload cadence stays native:
  the release tap is an ordinary attack press, refused by the native cooldown while reloading.
- Locomotion while the button is held: the hold sends nothing to the guest, so run, walk, back, strafe and jump
  stay fully native during the draw. There is no drawn upper-body pose during the hold (the native draw clip only plays on
  the release tap); a held pose would need its own torso-channel clip start and is not implemented.

## Live verification (2026-11-09, private play folder, `bow_aim = ootf`)

Recipe: `play_run.py` with Surdana's Short Wood Bow (`place` on it, E, then in the Tab screen hover the bow cell
with `cursor 250 146` and press ENTER twice: the first press only dismisses the help popup), `DRAKAN_BOW_LOG=1`,
`DRAKAN_PLAYTEST_HOLD=60`. `player+0xe90` reads 5 (bow out). Positions from RAM dumps (`combat_ram_report`), run
`build/harness/bowhold-ootf4` (and `-ootf6` for the rest).

- Strafe (A) with LMB held, released mid-run: 1451 units in the 1.6 s around the hold (about 905/s), 625 in the
  next 0.7 s (about 900/s), 903 in the next second: the speed is unchanged across the draw and the release, and the
  base channel keeps its clip (`player+0xaec` = 2, the archer clip on the torso channel, `+0x94` = 0x80 attack).
- Backward (S), run `-ootf6`: the release logs `airborne shot on torso channel 2` (the walking redirect, `+0x504`
  = 0x202) and Rynn keeps moving back at about 900/s after it (452 units in 0.7 s while accelerating, then 900 in 1 s).
- Forward (W): about 2135/s while held; the shot fires on release and the run continues until the range wall.
- Jump: LMB held, Space tapped, release in the air: `jumping attack gate` + torso-channel shot, arrow launched.
- Every release logged `[bow] aimed shot ... dir (...)`: the arrow was re-pointed along the camera centre ray (the
  crosshair), and the native auto-aim stayed out (`bowAutoTarget` entry followed by no `third-person autoaim`).
  The landing point itself was not measured (the HUD crosshair is not in the renderer screenshots), the aim is by
  construction at the ray point 8000 units ahead.
- Not run live: the crossbow (no crossbow could be obtained in the test level, holder mode 4 code is covered by unit
  tests only), and the drawn upper-body pose while the button is held (not implemented: the native draw clip only
  plays on the release tap, so the hold shows normal locomotion).
