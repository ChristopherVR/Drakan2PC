# OotF Ruined Village opening: the 1.1 to 1.2a cut and the impale (investigation, no converter change)

Two user reports: Rynn's body floats at the cut from the knocked-out shot to the bridge scene, and in the villager scene the
Wartok and its victim are not placed right against each other. This note records what was measured; no fix was shipped because
neither report could be tied to a converter defect with the evidence available (no PC reference footage or PC build here).

## How to capture the real scenes

The harness default (`DRAKAN_AUTOPRESS` taps Start and Cross) skips scene 1.1 after a few seconds and opens the pause menu
every 3 s; frames taken that way show gameplay at the start of the village. A capture that plays the opening is
`ootf_level_run.py ... --campaign --recipe <shots only, no pad taps> --env DRAKAN_AUTOPRESS_UNTIL=9000
--env DRAKAN_PAUSE_UNFOCUSED=0 --env DRAKAN_NO_ACTIVATE=1 --exe <private copy>`; the Intro plays about 60 s, scene 1.1 starts at
about 49 s of the village load and lasts 60.3 s, 1.2a follows after a black frame. Two games started at the same moment can
leave one at the title; stagger them.

## Scene data (PC `Ruined Village.ssd`, scenes 1.1 = seq 381, 1.2a = 964, 1.3 = 390)

* Scene 1.1: Rynn's last clip is 0x3ea (`1_1w`, a one-frame lying pose in the PC `System.adb`), started at 56.29 s; a last
  transform at 59.97 s puts her at y 20634.8, 89 units over the previous placement (20546.4). Ground there is 20000 (terrain
  sample), so the origin is 546 and 635 over it; standing is 508, crouching 242. At 60.27 s every actor is moved to y 200000
  storage. In 1.2a her actor (id 69) only holds the AI (action 6), with no transform and no clip.
* A test that lowered that last key to the earlier y (verified in the pack, 20546.4) showed the same last frames as the
  unchanged pack, so the 89 units are not the cause of what is visible. In the last frames the camera (54.9 to 60.1 s, at
  y 20642 to 20863) is a low close-up beside her and she lies flat with the grass below; the frames before and after look the
  same with or without the key. In 1.2a the shot of her (camera from 8.7 s) shows the Wartoks around where she lies but no
  body in the captured frames; that is not explained yet and is the first thing to look at (does the held player stay where the
  scene left her, and is she drawn).
* Impale, 1.2a at 34.5 to 46 s: victim `Mvillager 2` (obj 2205) placed once at (226162, 18946, 185242) with clips 6eb, 6ec,
  6ed (6ed lifts the root by 0.45 model units over 3 s); Wartok `Wartock Chain 1` (obj 1859, own converted troll rig) at
  (225151, 19280, 184716), clip 6f3. Ground there is 18750. At 4.82 s it attaches `Spear 1` to channel 9 (`Weapon_R`); at 43.06 s
  it attaches slot 23 (the villager) to channel 12 (`Weapon_L`, the left hand) and detaches at 46.25 s. Placement matches the
  authored keys (the camera view at 38 to 40 s shows the villager kneeling in front of the Wartok, the spear level with his
  head). From 43 s in game the villager stands upright overlapping the Wartok's side with his feet at the ground, not hanging
  from the left hand and not on the spear; without the attach action he stays about two body widths away (an experiment that
  also removed the spear attach, so the spear vanished). Open: whether the PS2 engine places an attached actor's origin at the
  channel joint (a weapon grip) while the PC keeps the actor's own offset; nothing in the data decides it.

## Follow-up: attach and pose carry-over

* Native scenes attach whole creatures the same way (Arokh attaches Rynn on channel 14, Rynn attaches General Denman on channel
  21 in `LEVEL1ON.SSD`), so the villager attach is a supported form. In game it takes effect: from 43 s the villager hangs from
  the Wartok's left hand (channel 12 is `Weapon_L` in the converted troll's 20-channel list), his torso behind the Wartok's
  body, and the level spear crosses his midsection (`cut-g` t153000 / t154000, zoom). No defect in the attach, the channel
  mapping or the action type was found; whether the spear should pierce him cleaner is an animation fine point that needs the PC
  footage.
* Carry-over of the end pose: a test pack replayed scene 1.1's last Rynn transform and clip at t = 0 of 1.2a (the player holds
  only the AI there). The shot of 1.2a that was meant to show her (camera from 8.7 s) is an overhead of the Wartoks running
  away; she is not in it with or without the carry-over, so nothing visible changed and the code was dropped. Her state between
  1.1 and 1.3 stays unseen in game; a per-frame position dump (not done) would settle where she is.

## Not changed

No converter or runtime code changed (the temporary rise-clamp and attach-drop experiments were removed). The scene
retarget notes in `2026-11-08/ootf-rynn-scenes.md` and `ootf-grunt-pose-fixes.md` apply to both.
