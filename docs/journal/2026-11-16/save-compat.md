# Saves across rebuilds of the game and the OotF pack

Report: "with each version the save games are not compatible". Reference:
[modding.md "Saves and pack builds"](../../reference/modding.md#saves-and-pack-builds). Private work in
`build/review/save-compat/` (copies of the player's cards in `user-saves/`, packs in `packs/`, play folder `play/` with its
own ELF copy, runs in `runs/`, tools in `tools/`: `run.py` + `f8load.txt` quick load from the title, `uidclass.py` the
offline check below, `clsdiff.ts` / `lvldiff.ts` / `lvldump.ts` pack diffs, `conv.sh` the release conversion).

## What breaks

Nothing in the exe. Every refusal came from the pack record (`<save>.rsg.mods`, 2026-11-07): it stores the build of every
active content pack and the game refuses the save when the installed build differs. The converter's build was the
**content hash of every file** of the pack, so any rebuild (a model, a texture, music, a terrain fix) refused every save,
also when nothing a save names had changed:

| Pack (15 Nov) | build | class tables vs current | level object tables vs current |
| --- | --- | --- | --- |
| morning (`pack-1115/ootf-levels.before.zip`) | `807978eb11272b0c` | identical | identical ids and classes (3 objects moved) |
| before music | `2f5e09559a0a31ca` | identical | identical |
| before models | `2e019828af5cc834` | identical | identical |
| current (`build/ootf/mods`) | `da1e598e3d3adfdd` | | |

Drakan 2 saves broke the same way: a disc-level save made while the OotF pack was active (`mc0/BESCES-50006Slot1/Level 1C -
Catacombs.rsg`, record `ootf-levels 1a8d4afc503be6a1`) recorded the OotF pack, although that pack only adds classes (all ids
above the disc's, `overrides/` holds only the widened 0x103 field lists) and levels: no disc class or level changes.

The real incompatibilities, between converter versions, are id shifts:

* **Class ids.** `planClassMapping` handed new class ids out in plan order (PC classes sorted by db and id), so a class the
  converter newly supports renumbers every class after it. 9 Nov pack (`d248f6c701888bf8`, found in
  `build/review/saves/sandbox`) vs 15 Nov: 713 of 1692 RESOURES classes and the MOUNTAON chain (`Rope` 4093 inserted:
  TallLadder 4085 -> Cable, ..., Fire Fly Group 01 4092 -> Ladder) hold other classes under the same id, and Ebon Knight
  (7093) or the pressure plates changed engine class. The map (`++top`) and grown fire (`++fxTop`) classes counted on from
  the same maximum.
* **Object ids** of objects the converter adds (one-way exit boxes, cull boxes, door holds ...) are handed out in feature
  order after the PC ids (`addObject`, max + 1): a feature that adds objects earlier moves the later ones.

How a save names them (from the user's saves and the 2026-11-05 loader notes): a changed object is a 0x102 record whose
first word is its class, `(database index in the save's 0x22 list) << 16 | class id`; untouched objects are listed by
uid (0x24) and recreated from the level file; 0x21 pairs uid and slot. So an old save restores its records under whatever
class the id names now, and lists uids that may now be other objects.

`tools/uidclass.py` compares, for every saved record that is a level object, its class word with the class the level gives
that uid (with `lvldump.ts` of a pack):

| User save | Recorded build | vs 9 Nov pack | vs current pack |
| --- | --- | --- | --- |
| `mc0_ootf/Slot2..4` Ruined Village | `d248f6c701888bf8` (9 Nov) | 0 differences | 7 (the Rope and Fire Fly objects) |
| `mc0_ootf/Slot1` Ruined Village (+ Book) | `daf6d8d09586c549` (14 Nov, pack not on disk) | 8 | 3 (added objects 3376, 3380, 3383) |
| `mc0_ootf/Slot13`, `Slot33`, card root | `da1e598e3d3adfdd` (current) | 9 | 0 |

In game with the current exe (`play/`, sidecar copied with the build rewritten to `da1e598e3d3adfdd`, F8 from the title, walk,
F5, F8): the 14 Nov Slot1 save loads at the Book's return pad in the church and survives F5 / F8 (`runs/e2a-daf6`); the
9 Nov Slot2 save loads too (`runs/e2b-d248`), but its fireflies are restored as a Ladder class record: loads, silently
wrong. So the refusal was the visible failure; the id shifts are what a guard must still catch.

## What changed

* **Pack build = save format** (formats `savelayout.ts`, converter `ootf-convert.ts`). The save layout of a pack is what a
  save names: every class record in `new/` / `overrides/` (`class|<ODB>|<id>` -> name, engine class, field count), every
  level's database list and object table (`obj|<level>|<uid>` -> db:class). Models, textures, sounds, music, terrain,
  positions and class defaults are read from the pack again and do not count. The converter reads the pack it replaces
  (`--previous <zip>`, default the `--out` file; `extras.py` now moves the old pack to `ootf-levels.previous.zip` instead
  of deleting it and passes it) and keeps its `build` while the new layout only adds keys; otherwise the build is the hash
  of the new layout and the log lists what changed (`WARNING: saves of the previous pack ... will be refused`).
* **Pinned class ids.** The previous pack's class ids are handed to the converter (`classIdPins`: per ODB, name + engine
  class -> ids): the k-th class of that name keeps the k-th old id, every old id stays reserved, new classes go above them.
  Used by `planClassMapping` and the map and fire class allocators. Synthetic object ids are not pinned (they still follow
  feature order); a change there is caught by the layout check and refuses old saves instead of breaking them.
* **`save_scope = "levels"`** in the OotF pack's manifest: the game neither records the pack for a save of another level nor
  checks an older record of it there, so Drakan 2 saves stop depending on OotF rebuilds. The game cuts long level titles in save names (`OotF 02 - Ruined
  Village .rsg` for the Book), so a title that starts with the save's name counts as the pack's level.
* **`[general] save_pack_check = 0`** (env `DRAKAN_SAVE_PACK_CHECK`): a save of another pack build loads anyway (logged
  `loaded anyway`); missing packs and damaged records are still refused. For the player who wants an old save back and
  accepts the risk (the 9 Nov saves above).

## Evidence for the fix

* `conv.sh pinned2` (release flags, HEAD converter with uncommitted work of other sessions, `--previous` = the current pack):
  `save build da1e598e3d3adfdd kept`. The HEAD converter's output differs from
  the installed pack (12250 files instead of 12253, 9 RESOURES classes with other defaults) and its content hash differs,
  but `clsdiff.ts` / `lvldiff.ts` against the installed pack: 0 renamed, 0 layout changes, 0 objects with another class.
* `conv.sh rv-pinned2` / `rv-fresh` (Ruined Village alone): the one-level pack keeps every class id the full pack gave (0 renamed in every ODB, `0 ids
  changed meaning`; the refusal is only for the 22 levels that are gone); the same conversion without pins renumbers all
  455 of its classes (`renamed=342` in RESOURES alone). A first version keyed the pins by name only and gave Ebon Knight
  (7093) and Village Waterfall 04 (17602) another engine class's id in the one-level pack: the key is now name + engine
  class.
* In game: the pinned full pack in `play/mods` with the current exe (it still compares builds only), the user's
  Slot13 save (record `da1e598e3d3adfdd`) in Slot33: F8 from the title loads Ruined Village at the church, W walks, F5
  writes a record with the same build, F8 loads it again (`runs/g-slot13`). With the old content hash this pack would have
  refused it.
* Tests: formats `savelayout.test.ts` (inserting a class in the middle of the plan keeps the others' ids; a removed class's
  id is never reused; occurrence and engine-class keys; layout keys, compatible growth, a changed object class), runtime
  `save_record_test` (scoped packs, records per level, `loadOtherBuilds`), `test_builder_extras.py` (`--previous`).

## Which of the user's saves load

| Save | Before | Now |
| --- | --- | --- |
| `mc0_ootf/Slot13`, `Slot33`, card root (15 Nov, `da1e598e3d3adfdd`) | load until the next pack rebuild | keep loading across rebuilds of the pack while the layout only grows (verified with a rebuilt pack) |
| `mc0/Slot2..5`, `Slot13` (Surdana, Shadowmire, no record) | load | load (unchanged) |
| `mc0/Slot1` Catacombs (record of OotF `1a8d4afc503be6a1`) | refused | loads once the exe is rebuilt and the pack carries `save_scope` (unit test only, not run in game) |
| `mc0_ootf/Slot1` (14 Nov, `daf6d8d09586c549`) | refused | still refused by default (3 added objects changed ids since, the pack is not on disk); loads with `save_pack_check = 0` after the exe rebuild (with a rewritten record it loads and plays with the current exe: `runs/e2a-daf6`) |
| `mc0_ootf/Slot2..4` (9 Nov, `d248f6c701888bf8`) | refused | still refused by default (class ids changed meaning); `save_pack_check = 0` loads them, fireflies come back as a Ladder record (`runs/e2b-d248`) |
| backup `save-backup-2026-11-13` (12 Nov, `ce304b247c999f9f`) | refused | as the 14 Nov save: pack not on disk, not tested |

## Not verified

* The runtime half (`save_scope`, `save_pack_check`, records per level) is covered by `save_record_test` but was not run
  in game: the exe was not rebuilt (no Build-LocalGame on `build/final-game` from this task). The release pack was not
  reconverted either; the next `extras.py ootf` run reconverts it with the installed pack as `--previous`.
* Order for the player: reconvert (`extras.py ootf`, which now hands the installed `da1e598e3d3adfdd` pack to the
  converter as `--previous`), then rebuild the exe. The build stays `da1e...` only while every release conversion sees the
  pack it replaces; a private conversion copied over `build/ootf/mods` starts a fresh layout hash and refuses today's
  saves. `build/review/save-compat/packs/pinned2/ootf-levels.zip` (build `da1e598e3d3adfdd`, HEAD converter) can serve
  as `--previous` if the installed pack is lost.
* Object ids the converter adds are not pinned; the layout check then refuses old saves of that level (no silent break).
  A converter change that adds objects must append them (modding.md).
* Adding objects keeps the build: an old save then lacks the new objects (it never listed them). Not tried in game with a
  real added object; the loader evidence (statics are recreated from the save's 0x24 list) says so.
* The layout ignores class defaults, models and animations. Whether a record's length follows the model is open: in
  the 14 Nov Slot1 save the converted creatures NPCS 7117 / 7118 have 394 / 548-byte records, in the 15 Nov saves 424 /
  578 (`tools/compat.py`), and that save still loads and plays (`runs/e2a-daf6`). An attempt to make a save with
  `pack-1115/ootf-levels.before-models.zip` (`runs/e3-make`, `DRAKAN_START_LEVEL` with `_FROM=OotF 00 - Intro`) started
  Drakan 2's Surdana instead of the Ruined Village (the save is `Level 1A - Surdana.rsg`), so it says nothing about the
  model upgrade.
