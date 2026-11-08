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
