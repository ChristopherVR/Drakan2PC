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
