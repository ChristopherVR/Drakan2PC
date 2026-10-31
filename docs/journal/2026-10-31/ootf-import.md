# Order of the Flame import and PC -> PS2 level conversion (2026-10-31)

Goal: read the player's own OotF install in the editor and turn its levels into a mod pack for this (PS2) game.
Background: `ootf-reuse-study.md`, `ootf-data-study.md`, `docs/reference/level-format.md` sections 9-10.

## What exists

| Piece | Where |
| --- | --- |
| SRSC reader/writer for the 12-byte PC header (`reader.platform`) | `editor/packages/formats/src/srsc.ts` |
| PC object list, PC class records, level loader (`loadPcLevel`, `PcFileSource`) | `formats/src/pc.ts` |
| PC -> PS2 conversion (`convertPcLevel`), PS2 terrain/layer-group writers, `buildPs2Lvl` | `formats/src/convert.ts` |
| Mod pack with `levels = [...]` and `files/ENGLISH/LEVELS/<mangled>/<mangled>.LVL` | `editor-core/src/ootf.ts` |
| Dialog "Import Order of the Flame levels..." (command palette, Ctrl+P), top-down preview | `app/src/editor/ootfimport.ts` |
| CLI for the same conversion | `editor/scripts/ootf-convert.ts` |
| Tests (synthetic fixtures) | `formats/test/pc.test.ts` |

## Verified on the real install (23 retail level files)

* All 23 levels load: 16312 objects, 4445 terrain layers, every object's class resolves in its dependency's ODB.
* PC and PS2 headers/layer definitions are identical; PC objects convert to the PS2 model (u16 -> u32 links, unsigned ->
  signed rotations, duplicate ids renumbered; the first occurrence keeps the id links use).
* PS2 vertex mask (adjacency bits) regenerated for full grids matches the disc layers exactly (LEVEL0RO, 19 layers).

## How classes and parameters map

* Class: PC class name -> PS2 class name (exact, then ignoring case/space/punctuation); ties go to the same engine class
  (`rflClass`), then the lowest `db_index` of the template's dependency table. No match: the object is dropped
  (or replaced by `placeholder`) and reported.
* Parameters (field overrides): matched by field name and base type against the PS2 class's field list; the data area
  is rebuilt with `setObjectParams`. Class references are remapped through the class map; model / sound / animation /
  sequence / texture / channel references are zeroed (PC resource ids do not exist on PS2) and counted.
* The PS2 template level supplies the dependency table, so converted objects can only use classes reachable from it.
  `LEVEL1NA` gave the widest match in a quick test (Ruined Village: 575 of 1858 objects kept; Alwarren: 598 of 1820).
  The low rate is expected: most PC scenery models (trees, buildings, braziers) have no PS2 class. Phase 3 (model and
  texture conversion plus new `0102` class records in a `new/` ODB entry) is what closes that gap.

## Terrain

PC cells carry one texture ref per triangle; PS2 stores one per cell (left triangle wins, a hole in either triangle
keeps the hole). PC texture ids point into PC TXDs, so every cell currently takes `textureFallback` (the template's
dominant texture) unless `mapTexture` supplies a PS2 ref. Vertex flags are written as 0.

## Open

* Whether the PS2 engine accepts a level whose per-layer inline blob was generated (see `terrainblob.ts` /
  level-format.md section 10) has to be tested in the game.
* In-game test of a converted level has not been done in this session.
* Viewport preview of the converted result (needs an in-memory terrain override in the worker); the dialog shows a
  2D overview only.
* Layer groups are regenerated as one "Ungrouped" group; `0x0025` (materials) and `0x0003` come from the template.

## Terrain inline blob (added later the same day)

`formats/src/terrainblob.ts` (`buildPs2TerrainBlob`, structure in level-format.md section 10.1) regenerates the per-layer
inline blob: header, 12x12-cell chunks, per-texture weight arrays, flat-layer tail. It is a structural approximation (6 of
584 disc layers match byte for byte; chunk layout matches 575/584). `ps2BlobFor` (convert.ts) gives each layer a unique
`seq`. Whether the engine draws such layers correctly is untested; it is the first thing to check in the game.
