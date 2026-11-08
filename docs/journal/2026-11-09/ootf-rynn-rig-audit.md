# OotF scenes: Rynn's hands, hair, Julian's hand and the scene end, a rig audit (2026-11-09)

User report: walking at the start (Rynn and Delon), the off hand does not hold the weapon properly; Rynn's hair flies upwards
in scenes; Julian's hand (the man whose sword Rynn takes in scene 1.3; "Adam" in the report) is awkward at the start and then
corrects; Rynn's body is wrongly posed when the scene ends and control returns. Outcome: the player's retargeted scene
clips now close her fingers (formats `a60809d`, `dfd5203`); hair, Julian and the scene end were checked in frames and show no
fault of the converter. Probes are private (`build/rynn-probe`, nothing from either game is quoted).

## Follow-up with frames (same day)

* **Capture.** `DRAKAN_AUTOPRESS_STOP_AFTER_LOAD=1` (`play_run.py --keep-cutscene`) never fired for a campaign start (no loading
  screen 29: the screens run 31, 30, title 8, 14, world -1). It now also stops at the first world screen after the title, so
  the Ruined Village opening plays out (1.1, 1.2a, 1.3 about 210 s). Recipe: shots only, no pad taps; runs `rig-g` to `rig-k`.
* **Fingers (fixed).** The unmatched PS2 finger joints now take the first-key pose of the native armed clip `A_RandomIdle`
  (fallbacks `A_1HandedRMUB`, `NCIdle2Magic_Sword`) in every retargeted player clip. Frames: both hands are closed fists on
  the sword in the 1.1 crouch and at the scene end (`rig-k` against `rig-g`).
* **Hair (no fault).** Close-ups of 1.1 and 1.3 show the ponytail hanging normally on a pack that includes the alpha-cut fix
  (`956e798`). The long red-brown bar seen from above in the 1.1 walk is not hair: replacing hair1 to hair3 by a native idle
  pose did not change it (tried and dropped). It looks like the back sheath or the scene sword placed behind her; not chased.
* **Julian (no fault).** In 1.3 his right hand starts on the spear shaft that pins him (clip `0x69e`) and drops at 9.2 s
  (`0x698`); the PC action carries the blend (transition word 0.85 s), which the PS2 action keeps. Not a converter fault.
* **Scene end (no fault found).** Dense frames every 0.7 s around the end of 1.3 (`rig-j`): last scene pose, black frame, then
  an ordinary idle with the head and neck neutral; the channel-3 layer hypothesis is not supported. 1.2a's Rynn actor only
  shows (`type 6` = 1) at 52 s, as authored, which explains her absence from that scene's earlier shots.

## Second follow-up (two-hand hold, scene-end snap)

* **Off arm is faithful.** FK of the PC clip on the PC rig against the retargeted clip on the PS2 rig, 342 clips: the left hand
  stays within 0.001 to 0.008 model units of the PC hand, the left-hand to `Rweapon` distance differs by at most 0.012 (0.033
  against 0.032 in `1_1readysword`, a static two-handed pose). No bind or roll fault in the arm chain, so no world-space arm
  retarget was needed. What the frames show was the hands: the grip pose came from an armed one-handed idle and the left hand
  looked splayed. The grip now comes from the native two-handed clips (`2H_RandomIdle`, fallback `2H_Walk`; formats `ed2b9df`):
  in the 1.1 sword hold both fists wrap the hilt (`rig-o`).
* **Scene-end snap not reproduced.** Frames at 50 ms around the end of 1.3 (`rig-p`) and at 100 ms around the 1.1 to 1.2a cut
  (`rig-q`): the scene ends in a fade to black (5 frames, 0.25 s at 20 fps sampling), gameplay fades in on an ordinary idle;
  frame differences after the fade are fade-in brightness for about 0.4 s, then idle noise (0.2 to 0.5). The only change seen is
  the legs settling from wider to narrower over the first 2 to 3 frames. The scene pose to idle switch itself happens under black
  at every scene cut checked, so a snap would need a capture without the fade (the user's run) or per-joint RAM reads; not done.

* **Mitigation for the settle after the fade (formats `convertSequence`).** Before an actor's release (a type-5 action with a
  non-zero payload) the player now plays the native idle (`NCIdle`, SYSTEM.ADB) from 0.5 s earlier with a 0.4 s blend, so the
  pose under the fade is already the idle stance. Frame check at 50 ms (`rig-r` against `rig-p`): the leg change in the first
  frames after the fade-in is about half as large (consecutive-frame difference 0.065 / 0.10 / 0.08 / 0.095 against 0.19 /
  0.235 / 0.12 / 0.06; a crude metric that includes fade-in), not gone. Only 1.3 of the Ruined Village releases Rynn, so no
  second scene end could be checked.
* **Idle clip read from RAM (`rig-s`, `log on 1` over the end of 1.3).** The base layer's animation record
  (`*(model+0xe8) + idx*0x80`, `+0x50` id, `+0x54` rate, `+0x58` phase) runs the scene's last two retargeted clips (ids 0x20918,
  0x20919 = new SYSTEM.ADB clips 2328 and 2329) and then, once control is back, id 0x2047b = clip 1147 = `NCIdle` at rate 1.0
  from phase 0.019. So the clip chosen for the pre-release blend is exactly the engine's gameplay idle, and no start phase is
  needed. The blend runs 0.5 s early, so at the release the pose is `NCIdle` at about 0.5 s while the engine restarts it at
  0; the difference between two idle poses is small against the scene pose to idle jump it replaces. Per-joint world
  positions from RAM were not read (the pose matrices were not located).

## First pass (measurements before the frames)

## What was measured (PC Rynn 3713 retargeted onto the PS2 Rynn of SYSTEM.MOD, 342 clips)

* Joint match: 33 of 33 PC joints find a same-named PS2 joint. The 32 unmatched PS2 joints (eyebrows, eyelids, jaw, mouth
  corners, eyes, 24 finger joints, shield, sphere1) hold their bind rotation in every retargeted clip.
* Bind frames: only `Rweapon` (108 degrees) and `hair1` (10 degrees) differ between the two rigs.
* Fingers: native PS2 clips curl them (first-key mean angle from bind: armed clips right about 24, left 60 to 78; unarmed
  walk 5 to 9). Retargeted scene clips keep the bind pose, so a sword in the right hand sits in an open hand and the left hand
  is never closed. This is the best candidate for "not both hands hold the weapon properly" but was not seen in a frame.
* `Lweapon`: native clips leave it on the bind (median 0), the retargeted clips put it 151 degrees away (the PC clips key it
  so; the world-joint rule keeps the PC frame). Nothing is attached to Rynn's left hand in 1.1, so this only matters for a
  held item there.
* Hair: for the 22 clips Rynn plays in 1.1 / 1.3, `hair1` is 40 to 90 degrees from bind (native median 70), `hair2` and `hair3`
  0 to 34 (native p90 about 50). Same-named native clips differ by 12 / 4 / 7 degrees on the first key. Only 8 of 342 clips
  swing a hair joint past 100 degrees (`10_2o`, `death1a`, `demo_death`, `1_6f`, the two tread clips, `10_1b`, `10_c`; native
  has 2 of 499). So the keys are faithful; the hair is not a retarget error in the scene clips. The owl agent's finding
  (graded-alpha skinned cards drawn without depth write, formats `956e798`) is the likelier cause of "flying" hair cards;
  re-check on a regenerated pack.
* Julian: own PC rig (NPCs.mod 3728, 30 joints), converted with the identical-tree path. Scene 1.3 starts him on clip `0x69e`
  (`1_3u`), a one-key static pose of every joint, and at 9.2 s switches to `0x698` (`1_3o`) which animates the head and the
  right arm (45 keys). The first nine seconds are therefore the authored static pose; the right hand then moves, which matches
  "awkward, then corrects". No converter fault found: single-key tracks are kept (`convertPcAnimToPs2`).
* Scene end: the player's last scene clips are plain clips on channel 0 (1.3 ends on `1_3n`), the 1.1 talk clip `1_1rynntalk`
  runs on PC channel 2 (PS2 channel 3, the neck subtree) as a layer and is replaced by `stompwalk` on the same channel at
  7.8 s. Whether the engine keeps a channel layer alive after the scene releases Rynn (head, neck and hair held in the scene
  pose) is untested. The release itself is the run/stop action at the end of the player's actions (`convertSequence`
  hold-actor path in `ootfseq.ts`, shared with the floating-body fix at the 1.1 to 1.2a cut).

## Why nothing was verified in a frame in the first pass

Runs of the Ruined Village on a fresh pack (`rig-a` to `rig-g`) ended scene 1.1 after about 4 s: the harness autopress taps
Start + Cross until the first level has loaded and skips the scene (the Intro tip: `DRAKAN_AUTOPRESS_UNTIL`; with a value
low enough to keep the scene the title menu is never left, with `DRAKAN_AUTOPRESS_STOP_AFTER_LOAD=1` and no taps the run
stalled on the title). So the walk, the hands at the walk and Julian's opening in 1.3 were never on screen in this pass. Also
seen in those runs: Rynn's boots keep the same pose in two shots 1.5 s apart during the first close-up (the scene clock may
stall under several simultaneous games).

## Next steps

1. Get a frame sequence of 1.1 (walk) and 1.3 (Julian) with the autopress stopped once New Game has been taken.
2. If the hands are open: borrow the finger pose of a native armed clip for the unmatched finger joints of the player's
   retargeted clips (`retargetPcAnim` option for unmatched joints; right hand grip, left hand relaxed).
3. If a head / neck layer survives the scene end: write an explicit neutral clip on channel 3 at the end of the player's actions.

## Third pass (user report: hair a stiff strip, off hand not on the sword, mushed fingers)

Evidence (private, `build/review/rig/`): `hands-before-after-1_1-crouch-4x.png`, `fingers-before-after-1_1-crouch-5x.png`,
`hair-scene-crouch-before-after.png`, `hair-scene-crouch-forced-hair1-key-test.png`,
`before-gameplay-ponytail-ootf-top-vs-drakan2-bottom.png`, `gameplay-ponytail-ootf-pitch-sequence.png`,
`gameplay-walk-ootf.png` / `gameplay-walk-drakan2.png`, `model-offline-ponytail-*.png`, `user-gameplay-ponytail-crop.png`.

* **Which Rynn.** Gameplay and scenes both use the PS2 Rynn of `SYSTEM.MOD` (model 3877, 65 joints); the pack ships no Rynn
  model, only extra `SYSTEM.ADB` clips (scene clips retargeted from the PC Rynn) and the field-definition blob (identical prefix).
* **Hair, gameplay.** At the default camera the ponytail is 30 % of Rynn's height in both OotF and Drakan 2 (same mesh, same
  idle). The user's long strip is the same mesh seen from a high camera: a ponytail hanging down and slightly back is stretched
  down the screen when the camera looks down (`gameplay-ponytail-ootf-pitch-sequence.png`). No converter fault found.
* **Hair, scenes (the horizontal kinked bar in the 1.1 crouch and the lunge).** Not clip data. FK of the converted clips puts
  the chain hanging (idle: 78 degrees below horizontal) or slightly back (crouch 1_1readysword: 29 degrees, same as the PC
  clip on the PC rig; `walk` converted from the PC clip equals the native PS2 `walk` key for key), yet in the game the bar looks
  the same for three different hair keys: the old clips, clips whose hair1 to hair3 were rewritten to the idle hang (via a
  tried and dropped gravity pass), and a clip with hair1 forced to a 90 degree turn on every key
  (`hair-scene-crouch-forced-hair1-key-test.png`). So the engine drives the hair itself (the Rynn class has `Hair1 Channel` =
  joint 6, hair1) and ignores the keys of the scene clips for it. Why it is horizontal only in converted scenes (Drakan 2 scenes
  hang normally) is open: the next step is reading the hair joint matrices from RAM during the crouch. The converter's
  quaternion conversion is not the cause: PC `walk` and `NCIdle` converted with `convertPcAnimToPs2` reproduce the native PS2
  hair pose to 1 to 6 degrees on identical-bind rigs. Retargeting onto the PS2 rig (hair1 bind differs by 10 degrees) leaves a
  10 degree error that the engine override hides.
* **Off hand (fixed, formats `HandHold`).** The two arms differ: with the PC clip the PS2 Lhand ends 30 units (model units
  x 2048) off the native two-handed grip along the right weapon frame, about one palm: the off hand floated beside the hilt. Where a
  clip holds the left hand within 90 to 150 units of `Rweapon` the retarget now solves Luparm / Lforearm (two bone, elbow
  towards the PC elbow) and sets the hand frame to the left hand of the native `2H_RandomIdle` in the weapon frame, blended by
  distance. FK check: Lhand lands at (3, -24, 34) in the weapon frame, the native value, in 8 of 8 sampled hold clips
  (`1_1j`, `1_1k`, `1_1standup`, ...). In the game both hands now close on the grip (`hands-before-after-1_1-crouch-4x.png`).
  Wrongly applying the grip in the weapon frame before the weapon frames take the PC orientation missed by 100 units (the hold
  runs after the weapon-frame step).
* **Fingers.** The finger quaternions are the native clip's own (not converted), so the finger-chain twist in
  `convertPcAnimToPs2` found by the owl check does not touch them. What looked mushed was the palm being off the grip; with the
  hand on the grip the left hand wraps the hilt. The right hand's fingers still point along the hilt below it (they are the
  first key of `2H_RandomIdle`, which curls them around the PS2 sword's thicker grip).

## Fourth pass: the scene ponytail sits at its bind pose (engine side), tests and what is left

Evidence (private, `build/review/rig/hair-fix-*.png`): `hair-fix-offline-crouch-clip-vs-hair-at-bind.png`,
`hair-fix-channel-layer-test-bar-unchanged.png`, `hair-fix-gameplay-idle-hair1-forced-no-change.png`.

* **The bar is the hair at its bind pose.** Skinning the PS2 mesh in the 1.1 crouch with hair1 to hair3 at their bind rotations
  (all other joints from the retargeted clip) gives the long horizontal bar with the kinked tip seen in the game; with the clip's
  own hair keys it hangs down the back. The bind pose of the chain points straight back (hair1 to hair3 about 10 degrees below
  horizontal), so in converted scenes the engine leaves the hair joints at rest and ignores their tracks.
* **Tracks are ignored, on any layer.** Besides the earlier test (all hair keys of the scene clips replaced, even forced to a 90
  degree turn: no change), a duplicate of every whole-body player clip on the hair channel (skeleton channel 5, then joint id 6)
  with forced hair keys also changed nothing; so did forcing hair1 in seven native idle clips in gameplay (override
  `SYSTEM.ADB` entries; gameplay hair keeps its look, the override may not apply to native ids). The Rynn class `Hair1 Channel`
  field (default 6) is read by the engine for a hair controller that is not driven by clip data.
* **Not the actor mode.** The disc's own scenes hold Rynn like ours: 59 of 85 Rynn actors in the PS2 SSDs are mode 1, 25 mode 0.
* **Engine code not found yet.** The field table in the ELF (`0x4fe930`, 3295 entries of default, 0, name pointer, type) has no
  member offsets, and the jaw controller found next to it (`FUN_00404e08` family, `Controller_Update_C` callers) is a generic
  joint controller with angle fields; hair has only a channel field. Next: Ghidra on the Rynn class constructor to see which
  object takes `Hair1 Channel` and what it does in scene mode, or ship a Rynn model whose hair joints rest in a hanging bind
  pose (rotate the hair vertices and the hair1 bind together; gameplay would still be driven by whatever the engine does today).
* **Right hand fingers not changed.** They are the native `2H_RandomIdle` grip of the right hand, which wraps the PS2 sword's
  grip axis; the PC sword's grip axis in the weapon frame differs, so the fingers run along the hilt. A fix needs the hilt axis
  of the scene sword in the Rweapon frame (rotate the right hand about it); not done.

## Fifth pass: the engine drives hair1 only; hair2 and hair3 now rest hanging (formats `ootfhair.ts`)

Evidence (private, `build/review/rig/`): `hair-final-scene-1_1-before-after.png`, `hair-final-gameplay-ootf-before.png`,
`hair-final-gameplay-ootf-after.png`, `hair-final-gameplay-drakan2.png`.

* **Step 1 (Ghidra) did not find the consumer.** The ELF field table has no member offsets, the class data is addressed by
  index, and the speech and joint controller code next to the jaw is not the hair code. Not diffed against a disc scene Rynn:
  the converter writes no Rynn class (the scenes use the player object of the template, the same class), and the disc's own scenes
  hold Rynn in mode 1 / 0 like ours. Not chased further.
* **What the data says.** With the hair joints' bind rewritten (all three, to the native idle's hanging pose) the first segment of
  the ponytail in a scene stayed horizontal and only the lower part hung: hair2 and hair3 follow the bind pose of the model, hair1
  does not. The engine owns hair1 (class field `Hair1 Channel` = joint 6) and sets its rotation itself; in the 1.1 crouch that is
  its old bind orientation relative to the head (straight back, which points up and back with the head bowed). Clip keys do not
  reach hair1, hair2 or hair3 in scenes.
* **What is shipped.** For a pack that adds player scene clips, `overrides/SYSTEM.MOD/0208_*` and `0203_*` rewrite the hair
  joints of every Rynn record with an `NCIdle` rig (3877, 3945, 3982): hair1 untouched, hair2 and hair3 take the world orientation of
  the native idle's hair, their inverse bind matrices, vertices and normals move with them. A clip pose (absolute parent-relative
  keys) skins to the same vertices as before (checked on `NCIdle`, `walk`, `2H_RandomIdle`: only 2 of 30 hair vertices that are
  shared with other joints differ, by 12 to 43 model units / 2048). Result in the scene: a short stub straight back, then the
  ponytail hangs, instead of a straight horizontal bar. Not natural yet: the stub is the hair1 strip, which the engine keeps
  pointing back.
* **Gameplay.** Idle, run and jump captured with the same recipe on the old pack, the new pack and Drakan 2: the hair hangs as a
  strip down the back in all three at idle and swings in the run; no visible change from the patch.
* **Open.** A full fix needs hair1's controller: either its input (what makes it point straight back in scenes while it hangs
  in gameplay) or re-weighting the hair1 strip onto the head joint (weights live in the `0208` joint lists; the writer of
  `ootfanim.ts` does not round-trip these records byte for byte, so it needs an in-place patch). Right hand fingers: later.
