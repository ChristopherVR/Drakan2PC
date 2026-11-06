# Editor: Order of the Flame levels in the viewport (browser walkthrough)

Driven in a real browser (Playwright, Vite dev server) with the player's own English OotF install
(`build/ootf/cab_disc`) and the PAL disc. Nothing from either was committed.

## What was wrong

* The importer (Tools > Import Order of the Flame levels...) only produced a mod zip. A converted level could not be
  opened, so there was no way to look at terrain, objects, textures, Logic or Story of an OotF level, or to put OotF
  levels on the campaign map.
* The project map showed every converted exit as "no readable destination": the exit scan read only `LEVEL*.XDB` string
  tables, but the converter writes destination texts (ids from 0x7000) into `SYSTEM.XDB`.
* Terrain of converted levels cost about 9000 draw calls (Islands 16 fps, Volcanic 22 fps in the test browser): the
  shared terrain mesh was split per 32-cell origin chunk and texture, and OotF levels have hundreds of small layers.

## What changed

* `virtualdisc.ts`: an overlay over the opened disc. Pack files (`files/...`) appear at their disc paths, a same-path
  file replaces the disc file (the pack ships widened `RESOURES.MOD` / `WORLDCON.MOD` / `LEVEL1ON.MOD` under their disc
  names), and `overrides/<FILE>/` and `new/<FILE>/` entry patches are merged into whole copies of the disc containers
  (class tables, animations, string tables). The worker lists the converted levels next to the disc levels
  (`LevelInfo.virtual`), so the level list, Project Settings and the campaign map all see them. A class lookup that had
  1358 "class name not found" warnings on Ruined Village now has 87, and 316 of 342 classes decode a model.
* The importer opens a converted level from its result list. The zip is built when it is downloaded (it was built after
  every conversion). The progress line names the level being converted; conversion notes sit behind a disclosure.
* Exit destinations resolve through `SYSTEM.XDB` as well. The campaign map chains Ruined Village, Wartok Canyons and
  Grimstone Mines with their arrows.
* Untouched terrain layers merge per 128-cell chunk: Islands 3028 draws / 57 fps, Volcanic 1920 / 60, Ruined Village
  1530 / 60 (same triangle counts, same picture).
* The Logic dock opens at half the Level Editor height and its toolbar no longer stretches over the dock; the Show menu
  opens over the narrow-viewport tool bar; the importer's template list no longer offers converted levels.

## Walkthrough results

| Step | Result |
| --- | --- |
| Import all 23 levels | Convert takes about 70 s (about 35 s converting, the rest building the pack) and blocks the page; no console errors |
| Open each level | All 23 open in 4.5 to 8 s with terrain, textures and models; Validation warnings per level 3 to 154 (unknown classes and unreadable exits), no errors |
| Select, move, undo, redo | Select Rynn in the Outliner, move X by 100, undo and redo restore the value; autosave offers the edit again after a reload |
| Project panel and campaign map | Levels add, exits chain across converted levels; the map fits its cards |
| Logic graph | "Show the whole level" on Ruined Village: 219 objects, 83 links, 34 to fix |
| Story panel | Lists the Drakan 2 quests of the disc, not the OotF text (known gap) |
| Template Level dropdown (issue 1) | Opens inside the modal dialog and is clickable (the dropdown host is the dialog) |
| Play (F5) | Not run: Play launches the game and needs the game lock; not taken in this pass |

## Not done

* Converting 23 levels runs on the page thread (about 70 s). Moving `convertOotfLevels` and `buildOotfPack` into a worker
  would keep the UI alive and let the progress line update.
* Converted levels are not persisted. A project that lists them shows "Not a level of the open disc" until the install is
  imported again; editing a converted level and exporting a project would write overrides for an LVL that exists only in
  the OotF pack, so a project export over converted levels needs the OotF pack installed as well.
* Story shows disc text only; the OotF strings (new `SYSTEM.XDB` / `LEVEL1ON.XDB` entries) are not listed.
* No German install is available: issue 3 stays covered by the synthetic tests of the formats package.
