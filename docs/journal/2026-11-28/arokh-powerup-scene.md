# Wartok Canyons: Arokh's poison power-up scene (mouse, length, rider pose)

Owner report, after Arokh flies into the poison Quickening crystal (Dragon Power-up `0x10039`, PC 2538 / the generator
copy): (1) during the scene ("Hmm poison, useful weapon") the mouse still turns Arokh and a click fires (shots audible),
while W does nothing; (2) the scene is very long (about 30 s) and something seems not to happen; (3) afterwards Rynn
looks like she stands on Arokh.

Setup: exe of `dist/DrakanPC` (after `Build-LocalGame.ps1 -Incremental`), the owner's `drakan.ini` and
`drakan_input.ini` (`[mouse] dragon_mode = mouse`), a copy of `mc0_ootf` whose `Slot33` is the Wartok quicksave with the
boss dead and the crystal up (the owner's own `Slot33` is now a Grimstone Mines save, so the Wartok one was taken from
run `crystal-takefix10`; `build/review/arokh-scene/mc0_ootf`). Recipe `build/review/arokh-scene/base3.txt`: F8, Arokh
placed into the crystal, W, 1200 counts right, 1200 up, left click, shots through and after the scene.

## The PC scene (`Wartok Canyons.ssd` 434 "quickening", 41.5 s)

Stomp Player 3080 plays it (Sequence List `[434]`), started by Timer 3079 from the crystal. Actors: the camera (one key
at 0.007 s, one at 41.53 s: a slow 41 s drift from (299971, 41133, 290453) to (302973, 41122, 282871)), the Quickening
Effect 2541 (On at 0.004 s), Rynn (clip 560 "ride", held 0 to 41.52 s), Arokh (clip 541 "hover", one transform key to
(289710, 35246, 281738), held to 41.53 s). No sound action: the speech comes from elsewhere. The converted scene (LEVEL1ON
`0311_83_77`) has the same keys, times and holds; it ends at 41.53 s as on the PC.

## 1. Mouse steering and breath during the scene

Cause: a scene's run/stop action stops the held object's controller, so keys and sticks do nothing, but

* `[mouse] dragon_mode = mouse` turns Arokh by writing his transform itself (`InputModule::steerDragon`), outside his
  controller, and its gates (menu, `0x4fbafc`, `+0x94` bit 0) are all clear in this scene;
* the left click is `MOUSE1 = SQUARE`, the native Attack, and the game still fires the breath while he is held.

The hold is visible in the handle flags (`+4`): Arokh read `0x1f001069` before the take, `0x17001049` through the whole
scene (bit `0x20` clear) and `0x17001069` once the scene gave him back (RAM watch `arokh-scene-base2`); Rynn on foot
reads `0x17081069` in play (earlier `boss-J`). In `arokh-scene-base3` the click in the scene cost 15 breath (`d+6b4` 100
-> 85): a shot.

Fix (`runtime/input/src/DrakanInput.cpp`): `controlledActorHeld` reads the controlled actor's handle flag `0x20` each
PadMgr update. While it is clear, `steerDragon` does not steer (the counts are dropped, as in a menu) and the pad sample
loses the live scheme's Attack button (`nativePadButton(0x4006)`, Square when the scheme has none), for the controller and
the mouse alike; Cross, Start and Select stay (scene skip, pause). `DRAKAN_DRAGON_TRACE` lines carry `held=`. Test:
`input_test` (a held dragon is not steered and sends no Square; both come back with the flag).

Evidence (`build/harness/arokh-scene-fix1`, same recipe, `DRAKAN_DRAGON_TRACE=1`): `held=1` from the take to the end of
the scene (frames 6238 to 7990), every mouse tick in it `own=0` with the yaw unchanged, `own=1` again after it and
1200 counts turn him by 2.4 rad; the breath stays at 100 through the click. Images:
`build/review/arokh-scene/issue1_scene_mouse_before_after.png` (top: before, bottom: after; the hover clip moves the
wings in both), `issue1_after_scene_mouse_turns.png` (mouse turning after the scene).

## 2. The long scene: the Quickening Effect showed nothing

The length is the PC's: 41.53 s, the same keys and holds (above). What the PS2 scene lacked is the Quickening Effect
2541 (PC engine 80: spark model and density, flare rings of 1500 / 3000 units at 3 and 9 rad/s, "Lightning Object"
3052 "Quickening Lightning"). The PC draws it round the dragon, not where the object stands: the four effects stand 5000
to 26000 units from their scene's Arokh (Wartok 20000 under him in the canyon, Grotto 25700 above him; census
`build/review/arokh-scene/q80.mts`). The converter stood in a PS2 Particle Spawner (engine 71) at the object, and in
game the scene showed Arokh hovering with only the shrine's gas lightning round him for 41 s (`arokh-scene-base3`).

Tried and dropped:

* Moving the spawner onto Arokh in the scene, waiting for the message, larger and denser: the RAM watch shows it moved
  (`arokh-scene-pack1w`, class `0x10047` at Arokh's key) but no particle was ever drawn (`arokh-scene-pack2`).
* A new generator class of the effect's own "Quickening Lightning": the record converted with the right spawn, but its
  object never spawned, in the save and in a fresh level (`arokh-scene-fresh2`, `-fresh3`; the same object with the
  existing "Gas Lightning Generator" class spawned, `-fresh4`). Not explained.

Fix (formats `ootfgenerators.ts` `addQuickeningGenerators`, `ootfquickening.ts` `quickeningAtDragon`): each effect gets
one more object, a generator of the effect database's lightning generator of the crystal's element ("Gas" for Wartok's
poison, "Ice" Grotto, "Light" Alwarren, "Lava" Volcano), whose bolts draw on the PS2. The scene gets an actor for it:
a key at Arokh's position and On at the effect's message, Off at the scene's last action. Save layout: build
`493b53de681b8791` kept, 4 objects added (old saves get them through `save_added_objects`).

Evidence: in `arokh-scene-pack4` (save, owner's settings) the generator (ref 1668) stands at Arokh's key (289710, 35246,
281738) and releases bolts through the scene (release timer `d+8` running, as the shrine's generators).
`build/review/arokh-scene/issue2_scene_burst_before.png` / `_after.png`: 40 frames 0.25 s apart, 7 s into the scene.

Not verified / differs: the gas bolts last 0.09 s (3 frames of 0.03 s) and come one every 1 to 3 s, and the shrine's
four gas generators already throw bolts past Arokh, so in still frames the difference is small. The PC's sparks and
flare rings are not reproduced. Grotto, Alwarren and Volcano were not played.

## 3. Rynn stood on Arokh after the scene

Cause: a scene that gives the player back ends her on an end idle (`endIdleForActor`, 0.5 s before the release), and
that was always the standing `NCIdle` (1147). Here she rides through the whole scene (PC clip 560 "ride"), so the
standing idle was the pose the game kept: upright on his back. Arokh got the ground Ready (`G_ReadyW2` 1607) while
hovering.

Fix (formats `ootfseq.ts` `lastBodyClip`, `ootfdragon.ts` `dragonFlightClips`, `ootfpipeline.ts`): the end idle follows
the last whole-body clip before the release: the disc `Ride` (1455) after the PC "ride", and for Arokh the disc clip of
the same Fly / Hover / Soar field (1635 `F_hover2` here), else Ready as before.

Evidence: `build/review/arokh-scene/issue3_rider_after_scene_before_after.png` (left `arokh-scene-base3`: Rynn upright;
right `arokh-scene-pack2`: seated astride, 4 s after the scene).

Not verified: other scenes ending with Rynn mounted (Volcano 9.9, the Grotto fly-throughs) were not played.

## Needs

* The exe in `dist/DrakanPC` has fix 1 (`Build-LocalGame.ps1 -Incremental`, 28 November 18:24).
* Fixes 2 and 3 are converter changes (formats `bfe48d5`, `2994f54`): the pack must be converted again with
  `--previous dist/DrakanPC/mods/ootf-levels.zip` (build `493b53de681b8791` stays). The incremental game build did not
  reconvert it, and it was not installed by hand: the formats working tree held another session's uncommitted breath
  icon change (`ootfbreath.ts`, `ootfpipeline.ts`) that a conversion would have shipped. The test packs
  (`build/review/arokh-scene/pack1`, played through `DRAKAN_MODS`) were converted from that same tree.
