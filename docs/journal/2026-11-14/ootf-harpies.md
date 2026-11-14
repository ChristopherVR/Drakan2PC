# Order of the Flame: the harpies (succubi and the Queen), checked against the PC model

Owner request (round 7): "Can we also ensure the harpies in the game models are properly modeled." Scratch, tools, packs,
runs and evidence are under `build/review/ootf-harpies/` (not tracked); nothing from either game is quoted here, only
class, clip and field names and counts.

## 1. Which creatures, where, and how they are converted

The flying female creatures of Order of the Flame are the **Succubus** (engine class 100, PC model "Succubus": 45 joints,
258 vertices, 506 polygons, 5 textures, 41 clips) and **the Queen** (class 101, the Succubus Lair boss: 39 joints, 433
polygons, 38 clips). Placed: Islands 5, Islands Night 2, Succubus Lair 11 succubi plus the Queen; the Islands arrival scene
and the waypoint actors use the same model.

The PS2 has neither class. Both are clones of the PS2 flying creature class 30 (the converter picks the template with the
best field coverage: the Drakan 2 Gryphon) that carry the **converted PC model, skeleton, skinning and clips**
(`MODEL_STANDINS`). AI, attacks, health (50 / 300) and drops come from the PC class fields as before and were not changed.

## 2. Is there a Drakan 2 harpy to use instead?

No. Every class 30 creature on the disc is a dragon or a beast: Blade Dragon (crow), SnoCrow, Crimson Dragon (bat), Skeleton
Dragon, sand dragons, Ice Dragon, Gryphon, Armor Gryphon, Shaza, Tora; no other class names a harpy, succubus or winged
woman, and the 9 November scene audit found no counterpart either. None has a humanoid skeleton or the size and silhouette,
so no row was added to the Drakan 2 model table; the OotF model stays.

## 3. Is the converted model faithful? (offline comparison)

`tools/pose.ts` + `tools/render.py` pose both versions independently and draw them side by side:

* PC side: the PC mesh, weights and textures, posed straight from the PC keys. A check over every clip showed that the
  PC key's translation of every non-root joint is exactly the pivot term of its rotation about the joint's bind position
  (residual under 0.003 model units for all non-root joints of all 41 clips), so the converter's dropping of non-root
  translations loses nothing; only the root has real translation, and it is kept.
* Converted side: the model, skeleton, clips and textures read back from the pack (`RESOURES.MOD` / `.TXD`, the
  `NPCS.ADB` entries) and posed with the editor's PS2 rig code.

Result (`evidence/succubus_pc_vs_converted*.png`, `evidence/queen_pc_vs_converted.png`): bind pose, idle, flight, hover,
wing slash, air and ground attack, falling, crash, both deaths, and the Queen's hover, flight, spell attack and death are
the same picture from three sides. Same 506 / 433 triangles at the same places, no stretched or missing part, no T-pose,
wings and hair follow their bones, scale identical. The only difference is in the textures
(`evidence/succubus_textures_pc_vs_converted.png`): the wing and hair textures have graded alpha on the PC (a third of the
wing texels are 60 to 99 % opaque); the converter writes skinned textures with alpha cut at 50 % (`hardAlpha`, needed since
the Intro owl because blended creature polygons do not write depth), so the converted wing membranes are opaque with the
same tattered holes. Left as it is (section 6).

## 4. What was wrong: the clips the flying template plays

The template has clip fields the PC class names differently or lacks. They used to get the "lowest converted clip" stand-in:

| Clone | Template field | Before | After |
| --- | --- | --- | --- |
| Succubus | Idle On Ground | wing slash (attack) | its own Idle (PC field "Idle") |
| Queen | Idle On Ground, Take Off | spell attack | her Hover |
| Drone (Islands) | Crash (the PC Drone has none) | its sting | its flight clip, which the PC uses for every flying state |
| Ice Dragon (Grotto, 1 placed) | Idle On Ground, Take Off | its take-off clip | its hover |

Fix (formats `ootfclass.ts`, `mergeDefaults`): a template field "Idle On Ground" takes the PC class's "Idle" clip, and the
stand-in for other foreign clip fields is Base Idle, else **Hover**, else the lowest clip. A full pack converted with and
without the change differs only in these four class records (`packs/base` against `packs/fix1`, same formats tree, wide
chunks, subtitles). Test: `ootfclass.test.ts` "gives a converted flier its own idle on the ground ..." (fails without the
fix). Formats suite 62 files, 658 tests; `tsc` and Prettier clean.

## 5. In game (owner's `drakan.ini`, `play/` = copy of `baseline-play`, dist exe, private ELF)

Direct level starts on an empty card, god mode, `DRAKAN_PLAYTEST_AUTOAIM` keeping Rynn (or Arokh) facing the nearest
creature of the class. Placed: Rynn's start points next to the creatures. Played: the pickup and fight.

* **Succubus Lair, hover and kill** (`runs/sl_spot1`, `runs/sl_fight1` dist pack, `runs/sl_fight_fix` fixed pack;
  `evidence/lair_hover_dist.png`, `evidence/lair_fight_before_after.png`): Rynn placed on the Death Mace, E picks it up,
  Tab / Enter / Enter / Tab equips it; placed 600 units from a hovering succubus she walks in with W and swings with real
  clicks: 50 -> 25 -> dead in two hits in both packs. Seen close up: body, hair and both wings drawn, wings flapping in
  hover, the crash clip on death, the corpse stays on the floor (it rests against the wall there, so its wing stands up).
  Identical in both packs, as expected (the fix does not touch hover, attack or death).
* **Islands** (`runs/is_aro1`, `is_foot*`, `is_alt1`, dist pack; `evidence/islands_flight_dist.png`): the two gate
  succubi fly up and circle over Rynn at about 2,000 units, attack her with their beam (her orb changes colour), and draw
  as red winged figures; from Arokh both were seen climbing with flapping wings.
* The read clip time of a circling succubus stood still while she was out of the picture and ran while she was in view
  (lair hover, Islands climb). A diagnostic pack with Glide pointed at Hover changed nothing, so the glide field is not
  the cause; most likely the engine does not animate off-screen models. Not a model defect, nothing changed.

## 6. Not verified

* A landed succubus playing "Idle On Ground" was not seen in game: the perched Lair succubi stay hidden until their
  trigger and none landed in the runs. The fix is shown by the class records and the test only.
* The Queen was not reached in game; her conversion was checked offline only.
* The opaque wing membranes (graded PC alpha cut at 50 %) are unchanged; making them translucent needs blended skinned
  polygons with correct depth order (the Intro owl problem) and was not tried.
* The circling clip freeze off screen (section 5) is inferred, not proven from the engine code.
* Islands Night was not run. No campaign save was made (direct starts); the fixed pack needs a pack rebuild and a new
  campaign game. Drakan 2 is untouched (converter only), so no Drakan 2 scene was played.
