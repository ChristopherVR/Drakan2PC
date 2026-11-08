# OotF water: Rynn walks through it, and it does not look like Drakan 2 water (2026-11-08)

User report: in converted Order of the Flame levels Rynn does not swim; she walks on the bottom through the water, and the
water does not look like The Ancients' Gates water. Code: `editor/packages/formats/src/convert.ts` (`ps2VertexTypes`,
`ps2BlobBuilder`), `terrainblob.ts` (`noDrawData`), `ootfterrain.ts` (`TerrainTextureBuilder.liquidOf`, `PS2_WATER_TEXTURE`).
Runs and helper scripts: `build/ootf-run/water*` (private). Nothing from either game is quoted here.

## How the PS2 engine knows water

* **Layer type 2 is water** on both platforms (PC: "Ocean 03", "River 12", "Well Water", "Temple Water 04" ...). The draw loop
  `0x152330` sorts visible layers by `layer+0x4c` (type) and hands the type 2 list to the water pass `0x161a78`. The converter
  already copied the type, so the converted levels had water layers (102 in Ruined Village).
* **The vertex type marks the water surface.** The low byte of a PS2 vertex word is the vertex type: 0 Normal, 1..64 the Level
  Properties `Vertex Color` entries, then the water kinds. Every PS2 water layer uses water kinds (Surdana, Kragmor, Stratos
  0x41; Andrellian Isles, Ravenshold, Northern Tundra, Valley 0x71; Shadowmire 0x5a), 0 almost only where no water cell touches
  the vertex. The PC keeps the same kinds in its vertex type byte (`Dragon.rfl` list: 0 Normal, 1 "Water, Still", 2..25 slow / medium / fast currents, 37..44 Water Phase,
  45..49 Shallow Water 92%..60%, 64..71 Underwater 1..8, 72..75 the Alpha..Gamma colours): Ruined Village water is 1, Islands
  and the Intro 49. PS2 = PC + 0x40 (the 64 vertex colours were inserted after Normal): 1 -> 0x41 (Surdana's pond), 49 -> 0x71
  (Ravenshold). The converter wrote 0 for every vertex, so the converted water had no water kind.
* **Water layers have no chunk draw data.** All 171 water layers of Surdana and 103 of Northern Tundra have the empty inline blob;
  the converter gave them chunks like a floor.
* **The look comes from the texture's material.** A PS2 texture header has its material at +40, a class of the TXD's own ODB.
  The disc's water cells name Proc Textures entries (0x10e, 0x471d, 0x467f, 0x4682, 0x270, 0x3972; lava 0x1d7) that are 64 x 64
  placeholder pictures (a "T", an arrow); their materials (`PROCTEES.ODB` 1 "Water", 240 "Surdana Water", 230 "Stormy
  Water_Level 3a", 232 "Northman Water", 95 "goblin forest water", 94 "Lava (Base)", engine class 0x100d1) carry the two
  scrolling textures, flow, alpha by distance, waves and specular. The PC water texture is the same idea (PC flags 0x42 =
  dynamic + alpha, material "Water", RFL class 0x72, PC `Mountain World Common TD` 0x10e, `Tropical World TD` 0x340); PS2
  0x10e with material 1 "Water" is the port of that PC material (same textures 388 / 389, same water effect 134117). The
  converter turned the PC picture into an ordinary 8 bpp terrain texture (mean alpha 42 of 255) with the splat material 45.
* `Use Water Level` / `Water Level` of Level Properties is 0 on all 13 PS2 levels: not how water works (the 2026-11-01 note that no
  water could be derived from PC data is superseded).

## Fix (formats)

* `ps2VertexTypes`: on water layers a PC vertex type 1..71 becomes `0x40 + type`; all other vertices stay 0 (PC floors carry no
  PS2 vertex colours).
* `ps2BlobBuilder` writes the empty blob for water layers (`TerrainBlobOptions.noDrawData`).
* `TerrainTextureBuilder.addLevel`: a PC texture used on a water layer whose material (texture header +38, resolved through the
  container's `.db` and `.odb`) has RFL class 0x72 maps to the template's native PROCTEES 0x10e ("Water"), or 0x1d7 ("Lava
  (Base)") when the material's damage is above 0. Its picture is converted only where it is also painted on floor cells (Intro 19,
  Ruined Village 12, Wartok 66 cells). `mapTexture` gets the cell's layer for this.

Converted Ruined Village after the fix: 9500 + 114 water vertices at 0x41 / 0x71, 102 water layers with empty blobs, 7646 water
cells on Proc Textures 0x10e (before: 0 / drawn / a converted 0x4720 picture).

## Verification

Converted packs (`ootf-convert.ts --campaign 1`, template LEVEL1NA), run from a private copy of the play folder
(`build/ootf-run/water/dist`) with `ootf_level_run.py --campaign --no-convert`, `DRAKAN_PLAYTEST_HOLD=30`, god mode and
`DRAKAN_PLAYTEST_WATCH=player=1` (Rynn's position once a second). Recipe: `place` over the Ruined Village ocean ("Ocean 02",
surface 11000, sea floor about 6600) at (248832, 11800, 105472), wait 6 s, hold W for 3 s.

| Pack | Rynn after the drop | Swimming forward | Picture |
| --- | --- | --- | --- |
| old converter (`water-base`) | falls to y 7136 (the sea floor) and stays | walks on the floor at y 7136..7149 | no water surface, she stands on the sea bed |
| vertex types only (`water-vt`) | floats, y 10748..10886 | swims, y 10780 -> 10242 | air meter, treading pose; no surface visible (night) |
| full fix (`water-fix`) | floats, y 10733..10880 | swims, y 10802 -> 10265 | dark blue rippled surface, head above it with ripple rings, green underwater view when she dives |

`build/ootf-run/water/compare_rv.png`: old converter left, fix right, top the float after 6 s, bottom after swimming forward.
So the vertex type alone decides swimming; the empty blob and the native water texture make the surface draw.

Water look elsewhere (Rynn starts on Arokh there, so the placement moved the dragon; no swim): Islands
(`build/ootf-run/water-isl/run/grid.png`): turquoise translucent water with the sea bed showing through, as Drakan 2 water;
Wartok Canyons (`build/ootf-run/water-wk/run/grid.png`): the river at the start and a cave pool drawn as blue rippled water.
Every run loaded its level and quit by script (PASS).

Tests: `pc.test.ts` (water vertex kinds, other layers 0, empty blob for water layers), `ootfterrain.test.ts` (liquid textures on
water layers map to the native water / lava entries, the same texture on a floor and other textures are converted, no native
entry keeps the picture). `npm test` (130 files, 1203 tests, with other sessions' work in the tree) and
`python scripts/run_tests.py` (401) pass.

## Open

* Not verified in game: Rynn swimming outside Ruined Village (Islands and Wartok start on Arokh; Wartok's water is 500 to 2000
  deep, a wade), the lava mapping (no PC level has a damage material on a water layer; unit test only), the Intro (49 -> 0x71).
* PC water layers slope up at some shore cells (69 of 102 Ruined Village layers have a few cells whose outer vertices rise to the
  bank, up to 16494 on "Copy of Ocean 01"; the disc's water layers are flat in 164 of 171). Those strips now draw: one shows as
  a slanted sheet in front of the rock spire in the Ruined Village shots. Flattening them to the surface height was not tried.
* Every liquid maps to the plain "Water" material 1; the PC per-texture settings (flow direction 90 on "blended water", speeds)
  and the PS2's other water looks (Surdana Water 240, Stormy Water 230 for the tropical sea) are not chosen per level.
* Grotto's only water layer has a snow material texture and type 0 vertices: it stays a non-swimmable layer as before.
* The water colours come from the template's Level Properties (Surdana's (A)..(H) colours); not compared with the PC look.

## Follow-up: water standing in the air, waterfalls, per-world water (same day)

User reports on a pack with the fix above: "some water is higher in the sky", and the waterfalls do not look like Drakan 2's.

### Water above the surface

* Cause: the PS2 water pass also draws a water layer's hole cells. The disc keeps every vertex of a water layer at the surface
  height (flat), so its hole cells lie under the bank. The PC lifts the vertices that no water cell uses up the bank, in places
  far above it ("Copy of Ocean 01": surface 11000, hole vertices up to 16494). Converted, those hole cells drew as sheets of
  water standing in the air (the slanted sheet in front of the Ruined Village rock spire, a strip across the Islands cave mouth).
  Proof: flattening only those vertices (pack `water-fix2`) removed both; the water cells themselves were never involved (3 of
  7646 sloped water cells in Ruined Village stand above the ground).
* Fix (`waterHoleHeights`, convert.ts): a vertex no water cell uses is lowered to the height of the nearest water vertex (breadth
  first over the grid) and to `WATER_HOLE_SINK` (64) below the highest floor under it (`terrainGroundBelow`), but no more than
  2048 below that water height (a cliff foot would otherwise make a wall of water); a lower PC height is kept. Water cell
  vertices are untouched (sloped rivers keep their slope). A first try that only kept hole vertices under the ground (not
  under the water) left the spire sheet (`water-fix3`): the ground under it is high.
* Counts (hole vertices standing above their water height): 0 by construction. Hole cells with a corner more than 1000 above
  the floor: Ruined Village 67 -> 56, Islands 195 -> 116 (those corners now sit at most 2048 below the surface, over a much
  lower floor); Surdana has 16.

### Per-world water material

Water now maps to PROCTEES 0x471d (material 240 "Surdana Water") for the mountain world and keeps 0x10e (material 1 "Water") for
the Islands world (`nativeWaterTextures`). Tried and rejected for the Islands: 0x467f "Stormy Water_Level 3a" drew grey gravel
(its wave texture is grey; Andrellian Isles colours it), Surdana Water drew pale sand coloured over the Islands' 0x71 vertices
(`water-isl3`, `water-isl4`).

### Waterfalls

The disc's waterfalls are the same mechanism as the OotF ones (an animated texture listed frame by frame in the model):
`SurdanaFalls02` and `SurdanaFalls_05LU` use WORLDCON 652..681 (64 x 64, 8 bpp, 30 frames at 30 fps, alpha selector 8 = blended),
`SurdanaFalls01` WORLDCON 591..620 (32 x 32, 15 fps, a noisy sheet like the OotF one). Every OotF waterfall sheet uses one
30-frame texture per world (`Intro Objects` 5024, `Ruined Village` 5024, `Wartok Canyons` 5064, `Tropical Common` 5663,
`Alwarren` 5732; 32 x 32, 30 or 40 fps); rocks and pools around them use other textures. Five rows in
`DEFAULT_PS2_TEXTURE_SUBSTITUTIONS` (ootfps2tex.ts) give them WORLDCON 652 with its frames; both are square, so the UVs carry.

### Verification

Same harness and private play folder as above.

* Ruined Village ocean (`water-fix4`): the spire sheet is gone, Rynn still floats (y 10790..10857) and swims; Islands from Arokh
  (`water-isl4`/`water-isl5`): the cave mouth strip is gone. `build/ootf-run/water/compare_float.png` (before = the 93d009f7 pack).
* Waterfall: Rynn placed at three sides of Ruined Village "Waterfall Tall #2" (210944, 16500, 239616), packs with the row off
  (`--ps2-texture-map` turning it off, `falls-old`) and on (`falls-new`): the sheet changes from the blue noisy OotF texture to the
  streaked Drakan 2 falls, still animated. `build/ootf-run/falls/compare_rv_falls.png` (night level, the bottom row shows the falls).
  The Intro owl shot could not be compared: in this run the pause menu opened over it (autopress taps), so the Intro falls are not
  photographed with the new texture.

Still open: shore foam has no OotF counterpart to swap (the PC levels have no foam objects or textures near water); the PC water
flow directions are not carried; the Islands keep the plain Water material.
