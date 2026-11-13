# OotF Islands: the "Ctrl landing" onto one plateau, and Rynn stuck on Arokh's back (round 5)

Follows [play check A](ootf-playcheck-islands-rift.md), section 2, and the 6 November [Arokh flight](../2026-11-06/ootf-arokh-flight.md)
"Still open" (Rynn stands on Arokh after the dismount at (389638, 62313, 778893)). Scratch, tools, runs and evidence:
`build/review/ootf-landing/` (private). Nothing from either game is quoted here.

## Set-up

* Play folder: a copy of `build/review/baseline-play` (the owner's `drakan.ini`: DLSS quality, HD textures, 1280 x 960),
  the `dist` exe and DLLs of 14 November, the `dist` HD textures, a private ELF copy. Packs: the `dist` pack
  (`dist/DrakanPC/mods/ootf-levels.zip`, "before") and a full 23-level conversion of the fixed converter (formats
  `e671be8` plus the fix, `--campaign 1 --template LEVEL1NA`, no `--hd`: `packs/fix`, "after").
* Harness: check A's plan driver (`tools/push.py` over `live_drive.py`, `tools/runf.sh`), direct start of the Islands,
  New Game, god mode, live keyboard input. Nothing was placed in any run of this note.

## 1. The plateau is the Islands arrival scene, not a landing

A per-frame read of Arokh while Ctrl was pressed (`diag1`, `diag2`) showed his landed word set and his position on the
plateau within one frame, 13,000 to 39,000 units away. A RAM search for the plateau point before the press found it in
scene keys, and the PC data names the scene: PC detector 3150, a box at the mouth of the arrival tunnel, "Detect Which?"
Rynn On Mount, Task Play Sequence, plays sequence 1 "Stomp5.1" (converted with a Stomp Player relay since 6 November). That
scene is 167 s long: Arokh flies out of the tunnel on scripted keys, lands on the rock plateau at 17 s, Rynn climbs down and
talks with the succubi, and at the end she is attached to him again and handed back mounted. The PC level does the same.

* Without input nothing happens (`scene1`: 187 s in the tunnel); the first W carries him into the box and the scene starts.
* The scene camera follows the dragon from the tunnel, so it looks like play. Check A's `is-land3` and `is-land4` "turning
  flights" are the scene's own path (identical positions at identical times); the harness's W, Space and mouse did nothing.
* Ctrl is Cross, which skips a scene: the "landing" was the skip, which puts every actor at its last key (the plateau).
  Left alone the scene ends at the same place with Rynn on Arokh and the player in control of him (`scene2`, `fix-scene`).

So the plateau is the intended end of the arrival. No landing search, landing spot, perch marker or terrain flag is
involved, and nothing about landing elsewhere was wrong (section 3).

## 2. Rynn stuck on Arokh's back: the scene put her on his neck (fixed in the converter)

The scene's end hands Rynn over with an attach action on Arokh, point 19. On the PC Arokh rig channel 19 is the saddle; on
the PS2 Arokh rig (the disc's dragon model) channel 19 is a neck joint and the saddle is channel 14, where every disc scene
that seats Rynn on him attaches her (1.02, 3.01, 4.1, 6.02, ...). The converter remapped the player's channels by joint name
but kept every other actor's, so Rynn rode on his neck (`evidence/evidence_scene2.png`, top left), and the dismount, which
detaches the saddle point, left her held at the neck: she stood on his back, W and S moved her about 100 units and pulled
her back (`scene2`), and a quicksave kept that state.

Fix (formats, `ootfpipeline.ts` `dragonSceneChannels`): scene actions on the converted dragon map his channels PC rig ->
PS2 rig by joint name, like the player's. The template data the converter reads does not include the dragon's MOD, so in
practice only the saddle is mapped (PC channel named "saddle" -> 14); other channels are kept as before (they already were,
and e.g. the Rimril and Rift World scenes that attach Rynn at PC channel 14 keep 14). In the new pack the three attaches of
"Stomp5.1" name point 14 instead of 19. Of the other converted scenes that attach Rynn to Arokh, all name 14 already and
keep it; only "Intro 3" still names 19, because its dragon is a scene prop, not the level's Dragon class (the converter
detaches her there before the level ends, so nothing is held). Test: `ootfseq.test.ts` "puts a handed-over Rynn
on the PS2 Arokh's saddle ...".

## 3. Played after the fix (pack `packs/fix`)

| Run | What was played | Result |
| --- | --- | --- |
| `fix-scene` | W into the arrival box, the whole scene watched (184 s), Cross taps to dismount, W 4 s, S, A | Rynn sits on the saddle; the dismount puts her on the plateau floor beside him (386990, 62096, 779205); W walks her away, off the plateau edge into the sea below (`evidence/evidence_fix-scene.png`) |
| `fix-land2` | scene skipped with Cross, dismount, W 0.5 s, A, S, walk back into Arokh | on the plateau floor after the dismount, walks, remounts by walking into him (controlled actor back to the dragon) |
| `fix-land6` | scene skipped; Space and W to take off; W with A / D steering (from the measured heading) and Space / Ctrl to hold 82,000; Ctrl held over the island east of the plateau (420000, 800000) until landed; Cross to dismount; W 2 s, A, S; walk back into Arokh. Then the same to the Alwarren exit (315000, 285000). F5, dismount, W, F8 | island: landed at (421104, 51019, 795358), dismount, walk 7,400 units, remount by walking into him (`evidence/fix-land6_island.png`). Alwarren exit: landed at (318736, 52180, 284890), dismount and walk work (`fix-land6_alwarren.png`); after the walk Arokh rose about 8,000 units and hovered, so the walk-in remount did not happen. F5 / W / F8 on foot: back at exactly the saved point (318670, 52017, 285358) |
| `fix-bell` | scene skipped, flown by way of (600000, 700000) to the bell route at (593307, 474070), Ctrl held, dismount, walk to the door, walk back; F5 mounted, dismount, W, F8 | landed in the courtyard in front of the bell temple door (581883, 56019, 474397), dismount, walk, remount (`fix-bell.png`); F8 restores him landed with Rynn on the saddle at the saved point |
| `fix-land5` | as `fix-land6`, other order | island and Alwarren landings as above; the remount at Alwarren failed the same way (Arokh rose and hovered); F5 while Rynn was still sliding down a slope, F8 back within 290 units |

Every landing ended where Arokh was when Ctrl was held, on ground under him. Flying into the high cliffs east of the
plateau or straight at the tower mountain stops him in front of the rock (his look-ahead), which a player answers by
climbing and turning; two early runs that flew into such places are left out (`fix-land3`, `fix-land4` s1). One of them
(`fix-land3`) descended through the hole cells of the island east of the plateau into the sea cave below it (the PC terrain
has a ceiling layer with holes over a sea-level floor there) and could not climb out again within 120 s; not looked at
further.

## 4. Wartok Canyons and Rift World

Neither has an arrival scene that moves Arokh, and neither showed the Islands symptom. Direct starts on the fixed pack,
real input only (`q-wartok`, `q-riftworld`): Space and W to fly off, Ctrl held until landed, Cross to dismount, W, S, walk
back into him.

* Wartok Canyons: flown 53,000 units, landed where he was (87659, 12207, 95870), dismount beside him, walk, remount.
* Rift World: flown 23,000 units, landed where he was (342416, 91326, 293598), dismount, walk; the walk took Rynn 8,000
  units down the slope and the walk-in remount stalled on the slope (not retried).

## Not checked

* The owner's own route: everything here is the scripted driver with live keys; the landings were chosen on open ground.
* Why Arokh, after the dismount at the Alwarren exit, rose and hovered (2 of 2 runs) so that walking into him did not
  remount; on the island, at the bell temple and on the plateau he stayed landed and the remount worked.
* The sea cave under the island east of the plateau (`fix-land3`): Arokh entered it through hole cells and stayed there.
* The scene itself (camera, subtitles, the succubi) was watched only as far as positions and the frames in `scene2` /
  `fix-scene`; the pack used here was converted without `--hd` (HD textures matched as usual).
* The terrain memory crash of the Islands (another agent's fix) was not hit in these runs.
