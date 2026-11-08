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

## Crosshair, crossbow, draw pose, parallax (follow-up)

- **Crosshair drawn**: confirmed with the settings host's harness inbox (`DRAKAN_OVERLAY_SHOT_DIR`, a `shot.txt` written
  every 0.5 s by a helper while the recipe ran). Frames taken while LMB was held show the gold crosshair at the centre of
  the game image over the archery target (about 20 of 77 frames); frames outside the hold show none. The renderer
  `shot` command does not include the overlay, which is why the earlier PNGs had none.
- **Crossbow**: no player crossbow exists. The 2026-11-08 notes decoded the placed-object lists of 11 campaign levels
  (only bows) and the native Villiager Cross Bow is an NPC weapon, so "the crossbow" is the player's bow. The mode 4
  support added above only affects a holder in mode 4 and cannot be exercised in game.
- **Held draw pose: not implemented.** The native attack `0x3016e8` plays one clip (0xe1, 0xe7 crouched) through the
  motion setter `0x32cf30`, first on `player+0xaec` and then on the torso channel (property `0x52`); the arrow leaves
  on that clip's launch event (`0x20e6c8`), so there is no separate "held" clip. Holding the drawn frame would need
  (a) starting that clip on the torso channel at press, (b) deferring the launch event until release (skip the
  launch hook and call it later with the nocked arrow still in `bow+0x158`) and (c) pausing the torso channel's playback
  at the last drawn frame, which needs the channel record's rate/phase fields in the model (`model+0xe8`, stride 0x80,
  record `+0x50` = motion) written safely. None of that is verified, and a half version would break shooting (attack
  state, cooldown `+0xb38`, ammunition), so the hold still shows normal locomotion plus the crosshair.
- **Parallax**: the arrow converges on the camera ray 8000 units ahead, so a target nearer than that is hit
  roughly `lateral offset * (1 - D / 8000)` to the side of the crosshair (the arrow starts about the shoulder offset,
  200 units, beside the camera ray: about 150 units at 2000 units range, about 100 at 4000). A correct fix needs the
  ray's hit point. The guest has a ray cast (`0x375a80`, used by the item pickup `0x31a278`, called through a vtable),
  but its argument layout is not decoded yet; the arrow landing against near and far targets was not measured.

## Held draw pose (implemented, `[mouse] bow_draw = 1`, default on)

Decoded live from RAM dumps during normal shots. The player model's animation records are at `*(model+0xe8) + i * 0x80`
(`model = *(actor+0x50)`): `+0x50` clip id, `+0x54` playback rate, `+0x58` phase in seconds (advances by dt * rate).
Rynn's bow draw/loose clip is id `0x20896` (rate 1.25), passed as `source` to the motion setter `0x32cf30` at return
`0x301b7c` (first call, channel 0) and `0x301c5c` (torso channel, 2); after the shot `0x20895` plays on the torso. The
**arrow launch is an event inside that clip at about 0.02 s** (`0x20e6c8` runs right after the clip starts), not at its
end, so starting the clip fires an arrow. A walking shot keeps the legs' own clip on records 0 and 3 (`0x206ad`) and the
archer clip on records 1 and 2.

Design (`InputModule::bowDrawFrame`): at press the same motion setter plays `0x20896` on the torso channel (property
`0x52`) at rate 0.5; the launch hook is muted while the aim is held (`suppressBowLaunch`), so the early event does
not shoot; when the record's phase reaches 0.20 its rate is set to 0, which holds the pose. If native locomotion
overwrites the clip it is started again. On release the frozen clip stays until the release tap's native attack starts
its own clip over it (so ammo, `+0xb38` cooldown, attack state and the launch all stay native); if the tap is refused
(reload) for 12 frames the clip is given its rate back and launches stay muted for 60 frames. The clip id is learned from
the native call (`noteBowArcherClip`), default `0x20896`. `bowChannelFrames_` (separate from the aim window) keeps the
walking torso-channel redirect valid until the attack starts.

Live (`play_run`, run `build/harness/bowhold-d`): standing hold and strafing hold both show records 1 and 2 frozen at
rate 0, phase 0.20, with the legs' idle/run clip untouched; overlay frames (`DRAKAN_OVERLAY_SHOT_DIR`) show Rynn with
the bow raised and arm drawn plus the gold crosshair; two releases gave exactly two launches and the ammo counter went
200, 199, 198; the strafing release took the torso channel (`aec` 2) with the legs keeping `0x206ad`. The held frame
flickers back to the start when native code overwrites the clip (the clip restarts at 0.5 rate). Not run live: holding
while jumping or running backward (the same records apply; only strafing and standing were exercised), the crouched
clip `0xe7`, and the post-release snap (the native attack restarts the clip from its start, so the pose jumps from the
frozen frame back to the nock while the arrow leaves).

Parallax / raycast: not attempted beyond the notes above; the 8000-unit convergence stays.
