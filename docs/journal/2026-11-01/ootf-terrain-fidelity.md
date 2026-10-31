# OotF converted terrain: fidelity and the Ruined Village load hang (2026-11-01)

Code: `editor/packages/formats/src/` `terrainblob.ts` (`planTerrainSlots`, chunk texture window), `convert.ts`
(`limitLayerTextures`, `MAX_LAYER_TEXTURES`, `BlobBuilder(layer, all)`), `ootfterrain.ts` (`ps2CellFlags`), `ootfclass.ts`
(`levelPropertiesParams`), `ootfpipeline.ts` (`ensureSingletons`). Runs: `build/ootf-run/rv*`, `b-*` (private screenshots),
helper scripts `build/ootf-run/rvrun.sh` and `batch.sh`. Terrain-only runs use `--only textures,singletons`.

## Ruined Village did not load: more than 8 textures in one layer

Symptom (every Ruined Village run, with or without objects): `beforeLevelLoad` of the converted level, then the guest main
thread sits at one pc forever (`PS2X_WATCHDOG`: pc 0x4acdd0 inside `FUN_004acd80`, a callee of the 1510-caller helper
`FUN_004ac628`; with fewer textures: pc 0x80000080, an exception, ra 0x165a00). A heap-corruption signature, not a size limit
(the terrain entry, 695 KB, is smaller than Surdana's 1.19 MB).

Cause found by statistics, then confirmed in game: no layer or 12x12 chunk of the 13 PAL levels uses more than **8 textures**
(LEVEL1NA 7, LEVEL5NE 8, LEVEL8OS 8, chunk maximum 8), while the converted Ruined Village had layers with 42 textures (chunks
with 37). The engine's per-layer arrays are evidently sized for 8. Fix: `limitLayerTextures` (default
`MAX_LAYER_TEXTURES = 8`, `ConvertOptions.maxTexturesPerLayer`, env `DRAKAN_TERRAIN_LAYERTEX`): keep the 8 most used textures
of a layer, give every other cell the texture of the nearest kept cell. Result: the level loads and Rynn walks.

Things that were NOT the cause (each tried): all layers in one `seq`/slot rectangle vs. the packed groups below; the
number of terrain textures (cap 24: still 21 per layer, different crash); objects (stripped to the singletons: same hang).
Truncating the layer list to 60 is not a valid bisect (the `visible` lists then point past the end and the world stalls at
0 fps).

## Slot groups (`planTerrainSlots`)

On the disc a group (`seq`) holds 1 to 20 layers and each layer owns a `4w x 4h` rectangle of the group's pool (LEVEL1NA seq 1:
twelve layers at x 0..11, 12..15, 16..31 / z 0..15, 0..3 ... in a 64 pool). The converter wrote `seq 1`, origin 0 for every
layer. It now packs neighbouring layers (32-cell tiles) into groups of a 256 pool with shelf packing; a layer wider than the
pool gets its own group. In-game this is accepted (Alwarren 291 layers, Islands 478 layers, Volcano 941, all load); whether
it changes the look was not measurable (the old single group also loaded when layers were few).

## Blob exactness

Compared with the disc blobs (script walked LEVEL0RO, LEVEL1BS, LEVEL1ED): chunk geometry matches in every layer; the per-chunk
texture list matched 6 of 19 layers before and 16 of 19 now (LEVEL1ED 22 -> 23 of 36, LEVEL1BS 28 of 41). A chunk also lists
the textures of the cells that touch its vertices from the neighbouring chunks: cell window `x0-1..x1`, `z0-1..z1` (the
`+1` on the far side was worse). Texture order inside a list is a per-texture priority not stored in the layer (22 of 41
match with first-seen order); weights and normals match in few layers (normals are within +-3 of ours in 55 to 90 % of the
vertices: probably baked light, not pure geometry). The engine draws the regenerated blobs either way.

## Cell flags, diagonals, UVs

* Diagonal: PC bit 0 = backslash. Checked on the PC data itself: with bit 0 as the diagonal 330 of 1858 Ruined Village objects
  sit within 1 unit of a terrain surface, 279 with the inverted rule (same test as level-format.md section 10). Kept.
* Bits 2 and 3: occur in the same combinations on both platforms (0xd : 0xc : 0x9 : 0x5 : 0x8 : 0x4 = 253k : 141k : 16k : 16k : 15k :
  15k on the PAL discs, 7.0k : 1.4k : 0.7k : 0.7k : 0.8k : 0.8k in Ruined Village). `ps2CellFlags` now carries bits 0, 2, 3
  (mask 0x0d, env `DRAKAN_TERRAIN_CELLBITS`); PC bit 1 (12 % of PC cells, 0.4 % on PS2) is dropped. Visual effect not isolated
  (no obvious artefact in the runs).
* UV order is unchanged from the PC cells (`Uc Ud Ub Ua Vc Vd Vb Va`, same in both formats per level-format.md); the in-game
  textures look right-way-up but no tile with a recognisable orientation was compared.
* Vertex flags (PS2 first u16: 0x200, 0x100, 0, 0x271, 0x300 ...) stay 0 on converted layers; PC has only a type byte (0 or 1).
  The mapping is not known; the engine runs fine with 0. Layer definition flags are 0 on all PC layers; PS2 layers carry 0x1000 /
  0x14 / 0x10 / 0x08 bits (0x04 flat). `DRAKAN_TERRAIN_LAYERFLAGS` ORs bits into every converted layer for experiments.

## Level Properties, sky, fog, water

* PC `Level Properties` is a class whose defaults the engine reads; the converter copies them by name. The Min/Max Fog Depth rule
  now takes max(PC, template) (PC 12 / 30 lu would have cut the PS2 view distance to a third).
* PS2 sky and distance fog come from the day/night cycle, selected by `Level Time (0-24hrs)` of the Level Properties object, and the
  sky itself is an **object** (class 26 `Procedural Sky`, db 11, present in all 13 PS2 levels). A converted level had none, so the
  world drew on a white backdrop with white fog. `ensureSingletons` now clones the template's `Procedural Sky`; a PC fog colour
  with mean luminance below 64 (Ruined Village, Rift, Mines, Volcano: black; Alwarren, Islands Night: dark) sets
  `Level Time` to 0 (`NIGHT_FOG_LUMINANCE`, `NIGHT_HOUR`; env `DRAKAN_LEVEL_TIME` overrides).
* Layer light / ambient colours are copied unchanged from the PC layers and are why Ruined Village is dark (it is a night level:
  ambient 0x15131e).
* Water: PC has no water-height field; PS2 `Use Water Level` is 0 on all 13 levels (even where `Water Level` is set, e.g. 199000
  in LEVEL3ES), so no water plane can be derived or switched on from PC data. Not done.

## Per-level results (terrain + singletons + sky, no objects beyond the existing PS2 classes)

`--only textures,singletons`, template LEVEL1NA, New Game, the converted level replaces Level 1A. "Load" = `afterLevelLoad` of
the converted level and Rynn walking in the screenshots / world sample. Skipped: Intro (another session).

| Level | Layers | Cells | Result |
| --- | --- | --- | --- |
| Ruined Village | 346 | 30k | loads after the 8-textures-per-layer cap (hang before); night sky + dark ground as in the PC data |
| Alwarren | 291 | 20k | loads |
| Islands | 478 | 79k | loads |
| Islands Night | 239 | 38k | loads |
| Rift Crystal | 135 | - | loads |
| Succubus Lair | 267 | - | loads |
| Grimstone Mines | 346 | - | loads |
| Grotto | 344 | 17k | loads |
| Wartok Canyons | 441 | 39k | loads |
| Rift World | 206 | 54k | loads |
| Volcano | 941 | 56k | loads (first run only timed out: the machine was loaded, the Intro had not finished in 150 s) |

(The batch ran while the fixes were still being added: Alwarren to Islands Night ran without the sky object, the later levels
with it; every one passed.)

Full pipeline (models, classes) for Ruined Village, `--max-classes 2` (the Intro-failing 3+ class limit avoided): level loads,
601 entities, 260 models and 441 textures (8.3 MB) converted, gameplay=1, about 450 fps. The default pipeline (all 214 cloned classes)
still fails at the Intro (`afterLevelLoad` of Level 0 never completes), the other session's problem.

Harness notes: the default recipe's `sync play 60` gave up before a big level finished loading; use a recipe with `sync play 300`
(`build/ootf-run/recipe_long.txt`). Do not wait on a hang: `PS2X_WATCHDOG=10` prints the guest pc every 10 s and
`analysis/functions.csv` names the function.
