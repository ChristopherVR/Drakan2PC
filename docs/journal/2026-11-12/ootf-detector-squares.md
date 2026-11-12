# OotF Ruined Village: the white squares in the tomb are light markers, not detectors

Owner report (playing the Order of the Flame campaign): "white squares floating looking like trigger points" in the domed
tomb of the Soul Crystal (the room of [the push puzzle](ootf-lair-push-puzzle.md)). The suspicion was that the converter's
Trigger Detector stand-ins are drawn. They are not. Private runs and evidence under `build/review/ootf-squares/` (`runs/`,
`packs/`, `evidence/`); nothing from either game is quoted.

## 1. What the squares are

Reproduced with the owner's 15:05 quick save on the installed old pack (`evidence/old-pack-owner-save/`) and with a fresh start
of the level on the newest full pack (formats `d189acd`, `evidence/tomb/before_*.png`). Three small white cubes hang in the
room, each about 100 units wide:

| Cube | Object | PC class | Converted class |
| --- | --- | --- | --- |
| beside the west statue | 2385 at 279956, 9299, 200919 | a model-less dynamic light (engine 133) | clone of the System "Dynamic (Vertex) Light" class, with its model |
| beside the east statue | 3032 at 292276, 9299, 200759 | same class | same |
| over the casket | 3039 at 286516, 9299, 200503 | same class | same |

How each was tied to its object:

* The entity table read out of the running game has a dynamic light entity (class `0x10085`) at exactly these three places,
  and each cube sits at one of them from every side it was looked at.
* A copy of the level with only object 2385's flags changed from 1 to 0 shows no cube beside the west statue and the other two
  unchanged (`evidence/identify/sheet_newpack_hdoff_light2385hidden.png`, bottom row). 3032 and 3039 are identified by
  position and by the final pack, in which all three are gone.
* With `DRAKAN_HD_TEXTURES=0` the cubes are still there (same sheet, middle row), so the HD texture folder is not the cause.

Why they draw: the System light class carries the model "Light Source" (System model 3813): 8 vertices, 12 triangles, a
draw list record and a 32 x 32 texture whose every pixel is opaque white. An object of that class with FLAG_VISIBLE (LVL
object flag bit 0) is drawn as that cube. The PC objects carry the flag (the PC class has no model, so it draws nothing), and
the converter kept it.

Things in the room that are not the squares:

* The plate detectors (3374 to 3376 in the old pack, at the room centre over the casket), the push block's and the plate's
  detectors of the new pack, the door trigger 1010: entities exist at their places, nothing is drawn there.
* The "Statue Crystal" props 2853 / 2854 are the orange crystals in the statues' hands, drawn with their texture.
* A coloured round patch high on the wall is an opening that shows the dome glass (`evidence/identify/zoom_window_and_cube.png`).
* The static lights (engine 132) of the room are not entities at all.

## 2. How the disc keeps detectors and lights invisible

Census of the disc's own levels and classes (`natdet.ts`, `natflags.ts`, `model.ts`, `modlist.ts`, `tex2.ts` in the scratch
folder):

* **Detectors (engine 61).** Every detector class but one names a System box model (3725 "Sound Detector", 3726 "EAX
  Detector", 3727 "Trigger Detector"): 12 triangles and bounds, and **no draw list record**. Most detector objects carry
  flags `0x101` (FLAG_VISIBLE | FLAG_SCALED), the rest `0x1`, `0x100`, `0x0` and a few with bit 3. So it is not an object
  flag and not a clear texture that hides them: there is nothing to draw. The one other class is the first level's own
  detector class, whose box model has a draw list and an opaque texture; its 31 placed objects carry flag `0x1`.
* **The engine does not draw detectors anyway.** Tried in game: a converted detector cloned from that class (box with draw
  list), moved over the casket with scale 3 x 1 x 1 (6,144 x 2,048 x 2,048 units), once with flags `0x101` and once with
  `0x100`. The entity is there, nothing is drawn (`evidence/identify/sheet_detector_with_drawable_box.png`).
* **Dynamic lights (engine 133).** All 30 placed dynamic lights of a class with the "Light Source" model carry flags `0x0`
  (29) or `0x8` (1), never bit 0. The three placed lights of model-less classes carry `0x1` or `0x11`.
* **Static lights (engine 132)** carry `0x1` in most levels with the same model; they never become entities.

The converted pack before the fix (`census.ts` on the 23 levels of the full pack): 783 detector objects, 779 of a class with
model 3726 or 3727 and four that are clones of the first level's detector class (they play a sequence, so they live in the
database that has one): Ruined Village 930 and 1093, Alwarren 506, Grotto 175. By the test above those are not drawn either.
All converter-made stand-ins (plate boxes, exit walk-in boxes, music trigger halves, scene relay detectors, quest lock
columns, the push puzzle's detectors) use the template "Trigger Detector" class and flags `0x101`: the disc's own form.
Nothing to change there.

Dynamic lights: 49 placed, 25 of a class with the model, 22 of those with FLAG_VISIBLE: Ruined Village 4 (the three of the
tomb and 2676), Grimstone Mines 7, Rift Crystal 5, Succubus Lair 3, Wartok Canyons 1, Islands 1, Islands Night 1.

## 3. Fix

Formats `db1a9c3`: `src/ootfmarkers.ts` (`hideLightMarkers`), a stage "markers" at the end of the per-level object passes in
`ootfpipeline.ts`, tests `test/ootfmarkers.test.ts`. A placed dynamic light whose PS2 class carries a model loses
FLAG_VISIBLE, as on the disc. Nothing else changes: a full 23-level pack converted with and without the change (same
converter otherwise, formats `d189acd`) differs in 22 bytes, one per light, in seven LVL files, plus the build id in
`mod.toml` (`evidence/pack_diff.txt`). Every ODB, MOD and TXD is identical, and so is every detector.

The light entity is still created with the flag cleared (entity table of the tomb, before and after), and the tomb still
brightens when the puzzle is solved (mean luma of the frame after the ring has risen: 30.9 before, 30.6 after).

## 4. Played

Play folder `build/review/ootf-push/play` (the owner's `drakan.ini` and `drakan_input.ini`, HD textures, DLSS), packs given
with `--mods`: "before" = full pack of formats `d189acd` (file for file the same content as the pack of the push puzzle
note), "after" = the same converter plus this change. Driver `push.py` on `scripts/harness/live_drive.py`.

Settings that differed from the owner's: a 1280 x 960 window instead of borderless full screen, `game = d1`,
`title_menu = modern` (`evidence/ini_diff_run_vs_owner.txt`), sound off, an empty memory card, a direct start of the level
with the opening skipped by the harness's Cross taps, only the level pack in the mods folder (the owner also has
`drakan-character-motion`; it was present in the save run on the old pack). God mode (`DRAKAN_PLAYTEST_GOD=1`) in the lair
run of the after pack and in the Wartok runs; `DRAKAN_LEVELCHANGE_TRACE=1` in the Wartok exit runs. Rynn is put at the
places with the harness's `place` command; the last steps into a trigger are walked where the table says so.

| Place | Before | After | Does the detector still fire (after pack) |
| --- | --- | --- | --- |
| Tomb, Ruined Village (`evidence/tomb/`) | three white cubes | none, from the owner's spot and from three more views | push detector: walked into the block, block z 207019 to 207578; plate detector: walked onto the tile by the statue, ring y 1544 to 7688. Same numbers on the before pack |
| Lair scene trigger, Ruined Village (detector 3345 to 1078 to a Stomp Player; `evidence/lair/`) | no box drawn; scene plays | no box drawn; scene plays | placed on the ledge, W for 3 s: the awakening scene runs (Rynn at the crystal, then Arokh) |
| Walk-in exit box, Wartok Canyons (3272 / 3273, the way back; `evidence/exit/`) | no box drawn 4,500 units in front of it | same | walked in on Arokh: log `touch handler ... event 1001`, then the save prompt on screen. Same lines on the before pack |
| Plate stand-in, Wartok Canyons (3268, two "Secret Entrance" movers; `evidence/plate/`) | no box drawn | same | 12 s at the level start: both movers still. Placed inside the box: the movers start 1.4 s later (2.0 s before the fix) and end at the same positions on both packs (z 128842 to 129149, x 231431 to 232046) |

The first lair try on the after pack ended with Rynn dead from the drop off the ledge (the placement is 2,700 units above
the floor; on the before pack she survived the same drop with half her health). It was repeated with god mode.

The owner's old save on the new pack: the game refuses it ("made with a different version of the mod pack",
`evidence/old_save_on_new_pack.txt`). The cubes go away with a regenerated pack and a new game; an old save loads only with
the pack it was made on, and that pack keeps the cubes.

## 5. Not verified or only inferred

* Only the three tomb lights were looked at in game. The other 19 lights get the same one-byte change; they were not visited.
  A look at Wartok's light 3040 showed nothing before or after: its entity was not at the placed position in that run.
* That the lights still light: shown only by the entity being there and by the tomb brightening as before. No light was
  measured on its own.
* "G Energy Bow Light", a converted dynamic light class with the cube model that no level places (an effect of a weapon), was
  not looked at. If the engine draws the cube for a spawned light, that one would show it.
* Lights 3032 and 3039 were not switched individually; they are identified by position and by the result.
* `DRAKAN_HD_TEXTURES=0` was taken to work from the code that reads it; the log has no line for it.
* The four detectors with the drawable box (Ruined Village 930, 1093, Alwarren 506, Grotto 175) were not looked at where they
  stand, only one of them moved into the tomb. In Alwarren nothing was seen at 506 from one side before Rynn died there; in
  the Grotto the placement started a scene.
* The exit, the plate and the lair trigger were reached by placement; the exit was fired on Arokh (the mount detector), not
  on foot. The level change after the save prompt was not taken.
* The Islands and Rift Crystal plate puzzles, music triggers and quest lock columns were not played. They are untouched by
  the change (byte-identical levels apart from the light flags).
* Why the first level's detector class has a drawable box and the engine still does not draw it was not read in the code;
  the statement rests on the one test.
* Tests: the formats suite was run on the working tree, which held other sessions' uncommitted changes (61 files, 612 tests
  passed), and `tsc --noEmit`. The committed state alone was exercised by the two full conversions, not by the test runner.
  The editor workspace's other tests and the Python tests were not run.
* Full screen, hand play, the German tree and co-op were not tried.
