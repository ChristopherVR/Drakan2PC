# OotF: pale lilac triangles over the Grimstone Mines walls (2026-11-14)

Owner's report: large pale lilac, partly see-through triangles cover the rock walls in Grimstone Mines, seen near the dead
slave at (214784, 33189, 276011). Code: `editor/packages/formats/src/convert.ts` (`ps2TriangleBits`, `ps2CellRef`,
`limitWindowTextures`), `ootfterrain.ts` (`ps2CellFlags`). Scratch, tools and every run: `build/review/ootf-mines-tris/`
(private). Nothing from either game is quoted here.

## How it was looked at

* Play folder: a copy of `build/review/baseline-play` (the owner's `drakan.ini`: DLSS quality, HD textures, 1280 x 960,
  ray effects off) with the 14 November `dist` exe and DLLs and a private ELF copy. Packs: `packs/before` is the current
  `dist` pack (formats `10d8713`); `packs/after1` and `packs/after2` are full campaign conversions of the same tree with the
  two changes below (the 9 arena preview pictures the `dist` build adds are not in them). The new terrain textures of
  `after1` were upscaled into the play folder's `textures/` with `analysis/scripts/ootf_hd_pack.py`, so both packs ran with
  HD textures.
* Runs: `DRAKAN_START_LEVEL`, New Game, god mode, scripted live input (`tools/push.py`, the round 3 plan driver over
  `live_drive.py`). Each run holds Cross until Rynn is on foot (the Mines start on Arokh), **places** Rynn 1600 units from the
  spot, then **walks** her there with W and mouse steering, and turns the view round in eight 45 degree mouse steps with one
  shot per step. Camera is the normal follow camera behind Rynn standing on the floor, so the first frames of the brief (a
  camera near the body) were not a camera inside rock: the sheets are there from where a player stands.

## What was wrong

1. **Cells with one hole triangle became whole holes.** The PC gives each of a cell's two triangles its own texture; a cave
   room is cut out of floor and ceiling layers along cell diagonals, so many cells are drawn in one triangle and a hole in
   the other. The converter kept one ref per cell (the PS2 format) and made any cell with a hole triangle a hole, so the
   drawn triangle went too and the clear colour behind the world (pale lilac in the Mines, blue in the Grotto) showed through
   triangular gaps in walls and ceilings. The slave's room is a 3 x 3 floor layer and a 3 x 3 ceiling layer that slopes
   down to the floor as its walls; four of those 18 cells are half holes and are most of the wall in view. It is not water
   (no water layer near), sky, fog volume, light or effect model; the water fix of round 3 does not touch floors.
   Campaign count of floor and ceiling cells with one hole or invisible triangle: Mines 741, Grotto 1383, Succubus Lair 1775,
   Volcanic 2013, Islands 1817, Rift World 1365, Islands Night 998, Alwarren 826, Wartok 220, Rift Crystal 210, Ruined
   Village 95 (11,400 in all).
2. **The disc has a per-triangle form.** PS2 cell flags 0x40 / 0x80 drop the first / second triangle (known for collision,
   PAL 15a2f0), 0x10 / 0x20 are their invisible counterparts. The 14 PAL level files have 10,600 textured floor and
   ceiling cells with only 0x40 or only 0x80, and their chunk draw data counts such a cell as using its texture (29,587 of
   30,714 vertex weights next to one; none skip it). A test pack that set 0x40 / 0x80 on two whole floor cells under the
   slave showed triangular gaps there in game (`runs/mines-exp1`), so the draw honours them too.
3. **A flat, lighter patch once the cells were drawn.** With the half cells drawn, the slave's floor layer has 9 distinct
   textures in 9 cells (the PC paints nearly every Mines cell with a texture of its own). A chunk blends at most 8 textures
   over its cells and their ring of neighbours, so the blob builder cut it to single cells and still dropped a blend in one:
   that drew as a flat lighter square with a hard edge under the slave (`runs/mines-after1`). The same happens wherever a 3 x 3
   block of cells has more than 8 textures: 6,184 chunks over the campaign before this work, 6,401 with the half cells drawn.
   A test pack giving the two half cells a neighbour's texture removed the patch (`runs/mines-exp2`).

## Fix (formats)

* `b6ff168`: floors and ceilings keep the cell's texture (the left triangle's, else the right one's) and set the PS2
  triangle bits from the PC's two refs (`ps2TriangleBits`: hole 0x40 / 0x80, invisible 0x10 / 0x20); a cell is a hole
  only when both triangles are, invisible when neither has a texture. Invisible cells get the disc's 0x30 directly;
  PC flag bits 0x10..0x80 (noise on a handful of cells) are dropped. `ps2CellFlags` keeps a converted layer's own triangle
  bits. Water layers keep their whole-cell rule (the water pass draws every cell).
* `d937581`: `limitWindowTextures` keeps every 3 x 3 block of a floor or ceiling within 8 textures: the texture the fewest
  cells of an over-full block use gives way to the texture most of its neighbours use. 4,763 of 383,241 drawn cells change
  over the campaign (Mines 103, Islands 1,331); no chunk drops a blend any more (0 of 6,401).

Tests: `pc.test.ts` (both diagonals, hole and invisible halves, collision of the drawn and the removed triangle, the
adjacency mask, the shading pass keeping the bits, water layers unchanged), `terrainblob.test.ts` (window limit, no reduced
chunk afterwards). `npm test` in `editor/packages/formats`: 61 files, 630 tests; `tsc` clean.

## Played

| Where | Played | Before (`dist` pack) | After (`after2`) |
| --- | --- | --- | --- |
| Mines, dead slave room | dismounted, placed 1600 away, walked in from the south-west and from the north, turned round | lilac triangles in the walls and ceiling in 17 of 18 frames (`runs/mines-before/sheet.png`) | none in 18 frames, no patch (`runs/mines-after2/sheet.png`) |
| Mines, second dead slave (278555, 222373) | the same; a troll attacked | no sheet in view | the same (`cmp_mines2.png`) |
| Grotto ice cave (212000, 156500) | placed on a ledge, fell to the floor below, walked, turned round | blue sky through the cave walls (`runs/grotto-before`) | closed walls (`runs/grotto-after2`, `pair_grotto_t7.png`) |
| Ruined Village above the village (229376, 155648), where the window rule changed cells | the same; a Scavenger fight | | no visible change (`cmp_rv.png`) |

Pairs at full size: `pair_slave_t1.png` (before / after1 with the patch), `pair_exp2_t1.png` (patch / no patch),
`pair_slaveB_t2.png`. Intermediate `runs/mines-after1` and the two test packs `exp1`, `exp2` are kept.

## Other sheets asked about

* Islands sea edge (49 drop cells after round 3): water hole cells over the abyss, a water layer matter; this change does
  not touch water layers. Not looked at in game.
* Rift World "large translucent figure" (`build/review/ootf-water/runs/rw4-after2`): the frames show Arokh's underside and
  wings seen from very close below, drawn faded the way the engine fades a model next to the camera; it is not a terrain
  sheet. Rift World has 1,365 half-hole cells, so it had the wall gaps too, but it was not played here.

## Not verified

* Succubus Lair, Volcanic, Islands, Islands Night, Alwarren, Rift World, Rift Crystal and Wartok were not played; their
  half-hole cells are fixed by the same rule, from the data only.
* The invisible half bits (0x10 / 0x20, 159 cells in the campaign, most in Succubus Lair) were not seen in game.
* Collision of the newly drawn triangles was not walked into on purpose; it follows the PC (the removed triangle has none).
* The window rule changes which texture 1.2 % of cells draw; only three places were looked at. The PC level painting is
  reproduced less exactly there, by design.
* The owner's full play folder was not used (it is `dist`); the arena preview pictures were missing from the test packs.
