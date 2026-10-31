# OotF sounds, sequences, campaign exits, music and text in the running game (2026-11-01)

Verifies the converters of `2026-10-31/ootf-sound.md`, `ootf-sequences.md`, `ootf-campaign.md` in the real game and fixes what failed.
Harness: `build/ootf-run/mk.sh <name> <converter args>` (converter only) and `build/ootf-run/run.sh <name> <mods dir> <level title> <timeout>`
(`play_run.py` with `DRAKAN_MODS`, `DRAKAN_START_LEVEL`); `DRAKAN_HLE_LOG=900000` logs every resource request
(`resource: <FILE> 0302/<id>`), `DRAKAN_SND_LOG=2` the SPU, `DRAKAN_LEVELCHANGE=<exit index>,...@<frames>` fires exits.
At most 2 new classes per ODB in every pack (`--max-classes 2`). Single-player levels were used (Relic is a multiplayer map: it has no exits).

Result: **sounds merged and playing (fixed), sequences fire (fixed), campaign exits and arrival markers work between converted levels
(fixed), music objects load and stream their tracks (fixed, started by test aid), exit text works, class / object text not seen on screen.**

## Findings that blocked the tests (not in the five items)

* **Terrain textures stall the load of larger levels.** Intro and Ruined Village with the terrain-texture stage (`textures`, PROCTEES.TXD
  append) never finish `Level_Load` (no `afterLevelLoad` in 240 s, no log line at all); the same levels with the fallback texture
  (`--only singletons`) load in 1 s. Relic (32 layers) is fine with textures. So all runs below use fallback terrain textures
  (`--only singletons,classes,texts,sounds,sequences,music`). Owner: terrain session. Not bisected here.
* The test aid `DRAKAN_LEVELCHANGE` ignores levels whose name contains "Intro" (it takes them for the cut-scene level), so the
  campaign's own `OotF 00 - Intro` cannot be driven by it; the intro hand-over exit (`intro-next`) was not fired in game.

## 1. Sounds into the SDB: fixed, works

Before: `RESOURES.SDB.new` (a standalone file the game never reads), ids 1.., group 1 (clashing with the host).
Now (`ootfpipeline.ts`, `ootfsound.ts`): `PipelineContext.hostSdb` (the CLI loads `RESOURES.SDB`; `ootf_level_run.py` extracts it)
-> `new/RESOURES.SDB/0302_<id>_<grp>.bin` + `0304_...` per sound, **ids taken from the free ids below the host's maximum** (RESOURES.SDB
has 343 holes below 608, as the ODB rule "inside the existing id range" suggests), group = the host's most used existing group (94), so no
0x0301 record. `convertPcSounds` got `ids.nextId`, `ps2SoundEntries(sounds)` returns the records (type `SdbRecord`).
The game log: `RESOURES.SDB rebuilt with 206 entry patch(es) (5542910 -> 8611282 bytes)` for two levels, no complaints.
Two more defects fixed on the way:

* A sound ref is `(db << 16) | id` with **db relative to the owner ODB's `.DB`**. The old mapping only mapped classes owned by the host ODB, so
  every sound of an NPC / item / level class became 0. `mapResource` now computes the index of Resources in the owner's `.DB`
  (`hostDbOf`, from `dbFiles`: NPCS 2, SYSTEM 1, LEVEL1ON 4, ...) and writes `(db<<16)|newId`; sounds of dropped objects and of owners that
  cannot reach the host SDB are no longer converted (Ruined Village: 49 sounds instead of 103).
* Sequence refs were also zeroed for classes outside the host ODB (Stomp Player is in LEVEL1ON.ODB); sequences go to the SSD of the
  owner's stem at db 0, so they are mapped for every ODB.

In game (RV, 49 sounds): the engine requests `RESOURES.SDB 0302/5,7,8` (ours) and then `0304/5,7,8` (60576 / 64688 / 32992 bytes), the SPU
`setAddr` points at the uploaded areas and the voices are keyed (NPC "Sound to Play" and item "Sounds" fields carry refs such as 131118 =
db 2 / id 46). Peaks in the SPU snapshots are non-zero while they play.

## 2. Sequences (SSD): fixed, fire

Sequences were already written (`new/LEVEL1ON.SSD/0311_<id>_<folder>.bin`, `0312` folder names, SSD rebuilt with 9 entry patches), but **no
object referenced them**: `Stomp Player` objects had `Sequence List` zeroed (see above). With the remap, RV has three Stomp Players with
`Sequence List [7]` (Initial State 1), `[8]`, `[6]` (Message To Play Next 4 = Play Sequence). In game the engine requests
`LEVEL1ON.SSD 0311/7`, then `8` and `6` in that order (the chain 7 -> 8 -> 6 playing), after `0311/2` of the stock intro. A screenshot of the
camera was not taken (the loads were slow, see the load time note below).

## 3. Campaign exits and arrival markers: fixed, works (RV <-> Wartok Canyons)

Pack = Intro, Ruined Village, Ruined Village Book, Wartok Canyons (fallback textures), start level `OotF 01 - Ruined Village`.
RV got 3 exits (to Intro: arrival marker, to Book, to Wartok); strings `0x7000..0x7003` in `new/SYSTEM.XDB` (dest ref in the object
= `(2 << 16) | id`).

* **Bug 1 (fixed)**: a native transition to a converted level failed (`afterLevelLoad result=-4`, then a jump to address 0). The template's
  splat dependency in the LVL header names `Level 1A - Surdana_splats`; the engine opens it beside the level being loaded. The test aid
  `DRAKAN_START_LEVEL` redirects that name to the start level's own, which hid the problem for the first level only; from the second level
  on the redirect pointed at the wrong level's splat files. `convertPcLevel` now writes the dependency as `<title>_splats` (mangles to the
  pack's `<first 6>TS`), so no redirect is needed.
* Verified chain (`DRAKAN_LEVELCHANGE=2,0,0@300`): RV -> Wartok -> RV -> Wartok, `afterLevelLoad ok=1` four times, the destination
  texts resolve to the converted titles, the player is placed exactly on the arrival marker (`arrival report ... frame 1 pos` equals the marker
  position: RV 148480 24496 211968 from Wartok; Wartok 76800 10094 15360 from RV), stays there for 200+ frames (no bounce), `.rlt` is
  written and read back (`fromSave=1` on the return).
* Not tested: the intro hand-over (aid limitation above), exits whose destination is outside the converted set (the converter drops them
  with a warning, there is nothing to walk into), a real walk into a marker (the player was moved onto the trigger by the aid, as in level-change.md).

## 4. Music objects: fixed (db), works when started

The converter pointed the new music objects at `Sounds = [track]` with db 0 = the class's own stem (PS2MUSIC.SDB), which is what the disc's
objects use (`0xaa`, `0x59`, `0x5b` in LEVEL1NA). The earlier worry about db 14 was unfounded. What was missing: nothing starts them. PC music
triggers are started by detectors; with `Start State For Triggered Sounds = 0` (the class default) a converted trigger is silent until its
chain fires. `applyMusicTriggers` got `startOn` and the pipeline `musicStartOn` (CLI `--music-start-on 1`, test aid only): with it the engine
requests `PS2MUSIC.SDB 0302/87, 88, 91, 105` (the palette tracks of RV) and streams 160 KB chunks into the SPU. Whether the detector chains
fire the objects in normal play was not walked.

## 5. Text

* Exit destination strings: **work** (end to end: `new/SYSTEM.XDB/0400_28672..` read by the game's string manager, the transition opened
  `Levels\OotF 03 - Wartok Canyons\...`).
* Class defaults (item names `Battle Hammer`, `Mithril Short Sword`, ...) and object texts are written as `new/RESOURES.XDB/0400_245xx_1.bin` etc.;
  the engine requests them only when the text is shown (pickup, inventory, detector pop-up), none was triggered, so **not seen in game**.
* Fixed: classes in an ODB with no XDB on the disc (FX) got ids in a non-existent `FX.XDB` (the mod loader logged `no file named FX.XDB, 2 entry
  patch(es) dropped`). `PipelineContext.xdbFiles` (CLI: the `.XDB` files next to the extracted ODBs) makes those string defaults 0.
  Object texts of such ODBs stay literal.

## Notes

* Load times: RV with 600 objects took 55 s to load while three other game instances ran; a single instance needs about 1 s for a level.
* Still open: terrain textures vs level load (above); the 3-record limit per ODB was exceeded by FX in one pack (3 new records) without
  affecting the run, the limit is being lifted elsewhere; a screenshot-level check of the sequence camera and of a text pop-up.
