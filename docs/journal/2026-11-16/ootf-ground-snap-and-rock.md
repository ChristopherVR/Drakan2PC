# OotF: turrets and trees off the ground, mushed mountain rock (2026-11-16)

Owner report (Order of the Flame): (1) "The turrets are not touching the ground properly, same with trees still."
(2) "The mountain textures look all mushed up." Follow-up to `2026-11-15/ootf-ground-placement.md` (tree grounding,
6 trees) and `2026-11-15/ootf-ground-textures.md` (Drakan 2 ground). Scratch, not committed:
`build/review/ootf-ground2/` (`tools/` probes and run drivers, `survey.tsv`, `packs/`, `runs/`, `ev/` pictures, `play/`
private play folder: `dist/DrakanPC` copy with the `elf =` line on a private ELF, `textures/` hard-linked, no HD step run).
Nothing from either game is quoted here.

## 1. Survey

`tools/survey.ts` measures every placed, upright, modelled object of the 23 PC levels: the lowest 5 % band of its model
against the PC terrain under that footprint (the converted terrain equals the PC terrain at object positions, previous
journal), and whether another object's box is under it. Findings:

* Trees: 361 trees, 68 palms, 7 pines. Most trunks are sunk (278 trees). The old rule moved a tree only when its trunk
  floated **all round** (5 trees); 29 trees, 18 palms and 7 pines float over 15 units on their **downhill side** only.
  That is what still reads as "floating trees" on slopes.
* The old footprint turned by the stored y instead of y - 90 (the engine turns models by y - 90, `ootfpush.ts`,
  `ootfcull.ts`), and the converted pines are drawn with the Drakan 2 pine, whose lowest 5 % (195 units of a 3900 unit
  tree) includes branch tips 300 units out: measured on that band, trees on slopes looked as floating by 100 to 380.
  The drawn pine's trunk foot is a ring of 8 vertices at -205 (radius 160 to 246), the next vertex 145 higher.
* Turrets: the 21 Ballistas (engine class 37) stand on 21 "Ballisata Base" objects (class 17). Both are mapped **by name onto
  the disc's own classes** (NPCS.ODB 1306 "Ballista", 2350 "Ballisata Base"), so they draw the disc models. The PC base
  model spans 0 to +1395 over its origin; the disc base (`BalistaBase_07_03_01`) spans -1558 to 0: its origin is its top.
  So every converted base was drawn 1558 units under the ground, and the ballista hung in the air on its swivel (the disc
  ballista spawns "Ballisata Middle", -1285 to +386 round its origin). In the game (`runs/wc_ball2`, read back from guest
  RAM: ballista and base at their placed positions, the engine moves neither) the gun hangs about 2300 over flat grass with
  nothing under it. On the disc (LEVEL1NA, LEVEL1OR, LEVEL3ES) a base is placed with its top 0 to 605 over the ballista
  (495 for three of LEVEL1NA's four), its bottom on the ground; the OotF ballista stands 2297 (Alwarren 1990) over its base's
  origin, on a pillar the disc base does not have.
* Other props: 202 class-17 objects float all round with nothing under them, nearly all on purpose (sconces 800 up, books,
  glasses and jugs on shelves and tables, hanging torches, banners, stalactites); a few barrels, crates, plates, jugs and
  plants float 5 to 90 units.

## 2. Ground snap (formats `ootftreeground.ts`, stage `ground snap` in `ootfpipeline.ts`)

Replaces the tree grounding stage; same switch (`DRAKAN_TREE_GROUNDING=off`, option `treeGrounding: false`).

* **Disc-model classes keep the PC bottom** (`nativeModelLift`): a class-17 object whose class is mapped by name onto a disc
  class is raised by `(PC model foot - disc model foot) * scale`, when its PC base touches the ground (within 128 units).
  Push-over pieces are left. Hits: the 21 Ballisata Bases (+1558) and one Alwarren "Brick" (-37: the disc brick's foot is 37
  over the PC one's).
* **Turrets sit on their disc base** (`turretOnBase`): a Ballista over a lifted base (within 256 units) is put
  `BALLISTA_UNDER_BASE_TOP` (495) under the base's top, as the disc seats them: 927 to 1234 units lower. Its firing height
  drops by that much (not played: whether the turrets still see and hit Rynn as before).
* **Trees** (`groundSnap` kind `tree`): a tree whose trunk base is more than 4 units over the **lowest** ground under the
  trunk drops onto it, so no side floats; the uphill side goes into the ground. The footprint is the drawn model's (the
  fitted Drakan 2 pine when it replaced the OotF one, `OotfAssetBuilder.discDrawnPositions`), the lowest band capped at 48
  units (`TRUNK_BAND_MAX`), turned by y - 90. Left: drops over 1200, a terrain rise over 512 under the trunk (cliff edge).
* **Props** (`isGroundableProp`, kind `prop`): only class-17 standing props (not trees, helpers, hanging or wall pieces,
  puzzle plates, architecture by name), with a footprint radius up to 600, floating all round by 4 to 96 units: they drop
  until they touch the **highest** ground under them (a flat base is not buried uphill).
* **Never moved over a mesh**: a vertical ray from each footprint sample against the triangles of every other placed
  scenery or mechanism object (doors, movers, bridges: engine kinds `scenery` / `mechanism`); a surface more than 8 units
  over the terrain and at most 32 over the base keeps the object (`meshUnder`). Tilted placements (no Euler order known)
  count by their bounding sphere. A first version took every modelled class: a scaled "Intermittent sound" marker (radius
  61440) then "supported" tree 60 and ten others in Ruined Village.
* Log per level: `ground snap <level>: n disc-model object(s) kept on the PC bottom`, `turret(s) seated on their base`,
  `object(s) lowered onto the ground: <id> "<class>" down <units>`, `floating, left: <n> <kind> <reason>`;
  `DRAKAN_DBG_GROUND=1` adds a line per object left and the meshes under it.

Counts (full 23-level conversion `packs/g3`, same flags as the play pack but without `--hd`):

| Level | Bases lifted / turrets seated | Trees lowered | Props lowered |
| --- | --- | --- | --- |
| Intro | | 2 (Large Tree 2806 by 103, Owl Oak Tree 9) | |
| Ruined Village | | 7 (31 by 86, 60 by 184, 102 by 97, 1845 by 61, 1458, 1571, 2661 by 8 to 16) | 7 (graves, a barrel: 5 to 20) |
| Wartok Canyons | 13 / 13 | | 1 |
| Grimstone Mines | | | 4 |
| Grotto | 2 / 2 | 3 (snowy pines 44, 61, 216) | 20 (barrels 6 to 87) |
| Islands | | 16 (pines 10 to 102, palms 10 to 379) | 6 |
| Alwarren | 6 / 6 (and Brick 1897 -37) | 1 | 7 |
| Rift Crystal | | | 1 |
| Islands Night | | 10 palms (17 to 379) | 11 |
| the other 14 | | | |
| **all** | **21 / 21** | **39** (before: 6) | **57** |

Largest moves: palm 1841 (Islands, Islands Night) 379, on a slope rising 413 under its base; Grotto MedTree 238 216.

## 3. Mushed mountains (formats `ootfterrain.ts`)

Cause. Since the Drakan 2 ground (15 November) every cell of the OotF brown rock family draws Proc Textures 621 "Brown
Rock Outdoor" (and the grey rocky tracks 535): 128 px tiled over 4 x 4 cells, 32 texels per cell, against the OotF rock
tile's 64 px per cell, and mapped from above like all ground, so on a mountain face one texel row is stretched down the
whole slope. The OotF tiles carried painted plates, cracks and, on the Wartok peaks, snow streaks (rock tiles with 10 to
30 % snow went to the rock family). The result on every steep face is a smooth tan streak ("mush"): `ev/tex_wc_bef_cur.jpg`
(left the morning-of-15-November pack `pack-1115/ootf-levels.before.zip`, OotF ground; right the current dist pack).
Half the rock family's cells are steep (Wartok 13,000 of 26,000 at a gradient of 0.84 or more, Ruined Village 4,400 of
18,600; `tools/grad.ts`).

Fix. `TerrainTextureOptions.nativeSteepKeep` (default `NATIVE_STEEP_KEEP` = 621, 535) and `steepGradient` (default
`STEEP_GRADIENT` 0.84, about 40 degrees; dev knob `DRAKAN_TERRAIN_STEEP`, 0 = off): a cell of those families whose height
gradient is at least that keeps its converted OotF texture (`cellGradient`; `ConvertOptions.mapTextureCell` gives
`convertPcLevel` the per-cell choice). Flat and gentle rock stays Drakan 2 rock, grass, snow and mud are unchanged.
Campaign: 39,105 steep cells, 90 OotF textures converted for them (only textures that paint a steep cell). Log
`drakan 2 ground, steep rock cells kept on their OotF texture: <cells> (<textures>)`. With wide chunks (32 textures per
chunk) the extra textures fit; the converter's chunk limits apply as before.

Not chosen: a tiling-1 material for the 621 copy (repeats the 128 px picture every cell, no disc material checked for the
same sounds and noise pass); triplanar mapping (the PS2 terrain has one planar UV per cell corner, no side projection; the
port's `terrain_cliffs` is a Direct3D 11 renderer option, off by default, not a pack setting).

## 4. In the game

Placed views (direct level start, god mode, audio off, `place` with fixed yaw; machine shared with up to three other
games, so the frame counters mean nothing). Before = dist pack of 15 November 19:39 (`packs/dist1939.zip`), after = `packs/g3`.

| Picture (before left, after right) | Seen |
| --- | --- |
| `ev/wc_before_after.jpg` row 1 | Wartok start from Arokh: the peaks show rock plates and snow streaks again instead of smooth tan; flat ground still Drakan 2 grass |
| `ev/wc_before_after.jpg` rows 2 to 4 | Ballistas 110, 51, 50: before the gun hangs in the air with no base; after the disc base stands on the ground (on the ledge for 51) and the gun sits on it |
| `ev/rv_trees_before_after.jpg` row 1, `ev/rv_t31.jpg` | Ruined Village trees 60 and 31: the trunk's bottom edge in the air before, in the grass after; tree 31 read back at 20934 (PC 21020, -86) |
| `ev/rv_trees_before_after.jpg` row 3 | tree 102: no visible difference at this view (dusk, foliage) |
| `ev/tex_rv_bef_cur.jpg` | Ruined Village rock faces in the morning and dist packs (dusk, hard to judge); after, `ev/rv_t31.jpg` right: the rock wall behind the tree shows its OotF plates |

Not verified: Islands, Islands Night, Grotto, Alwarren and the Intro in the game (counts only); the ballistas firing and
Rynn being hit after the 927 to 1234 lower seat; the HD step for the pack (the steep cells use the converted OotF tiles,
whose HD images the dist texture folder already holds from the packs before 15 November, not checked per hash); the far
colour maps of steep cells in a distant view; frame time with the extra ground textures; walking on the snapped props.

## 5. Code

Formats submodule: `src/ootftreeground.ts` (ground snap, footprint, mesh test, disc lift, turret seat), `src/ootfpipeline.ts`
(stage `ground snap` instead of `tree grounding`, steep log, `mapTextureCell`), `src/ootfassets.ts`
(`discDrawnPositions`), `src/ootfterrain.ts` (steep keep, `cellGradient`), `src/convert.ts` (`mapTextureCell`); tests
`test/ootftreeground.test.ts`, `test/ootfterrain.test.ts` (synthetic). The play pack needs a reconversion (and the HD
step); the exe does not change.
