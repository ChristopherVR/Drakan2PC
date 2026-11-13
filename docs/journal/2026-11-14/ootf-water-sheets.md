# OotF water: floating sheets, water that vanished while swimming, all-hidden water layers (2026-11-14)

Owner's report: "the water is still problematic where some water is floating, Rynn not being able to swim through it even."
Code: `editor/packages/formats/src/convert.ts` (`waterHoleHeights`, `cropWaterLayer`, `inertWaterLayer`, `otherWaterAt`).
Scratch, tools and every run: `build/review/ootf-water/` (private). Nothing from either game is quoted here.

## How it was looked at

* Every water layer of the 13 campaign levels from the PC data, grouped into bodies (layers that touch with surfaces within
  600): `tools/bodies.ts`, `tools/audit.ts`; the list with its state is below.
* Play: a private play folder (copy of `build/review/baseline-play`, the owner's `drakan.ini` with DLSS quality, HD textures,
  own ELF copy), a scratch driver `tools/wdrive.py` (live input through `DRAKAN_INPUT_LIVE`, Rynn's position and animation
  every 0.1 s, renderer shots), New Game into the level with `DRAKAN_START_LEVEL`, god mode. Rynn was **placed** next to or
  in each water body (the `place` aid) and then **played** with real keys: W to swim, Ctrl (Crouch) to dive, Space to
  surface, W into the bank to climb out. Animation ids in the logs: 20637 treading water, 205f0 swimming, 205fc / 205fe
  under water, 208a0 / 20680 climbing out, 2047b standing.
* Full campaign packs converted from the same tree with and without the change (`packs/before1`, `packs/after2`), installed
  one at a time into the private play folder.

## What was wrong

1. **The PS2 water pass draws every cell of a water layer.** Tried in game on Ruined Village with the hole vertices of all
   water layers raised 4000 above the water: the hole cells drew as sheets in the sky whether they carried the hole flags
   0xc0, the invisible texture with 0x30, an ordinary floor texture, or vertex bit 0x100 (`runs/raise`, `raiseinv`,
   `raiseft`, `raiseflag`). The disc keeps those vertices at the water height, under the bank. The PC lifts them up the
   bank or leaves open air beyond the water's edge, so the converted hole cells stood in the air. The 2026-11-08 rule put
   them under the ground but at most 2048 below the water, which over a drop (a ledge pool, a cliff lip, the church pools)
   is a slanted sheet hanging over the valley. Rynn cannot swim in it: it is not water to the engine.
2. **Half-water shore cells.** Since 2026-11-09 a PC cell that is water in one triangle draws whole; the corner only its
   hole triangle uses kept its PC height, up to 900 above the sea. Rynn could tread water in it, above the sea: in the
   before pack she floats at y 11840 over sea level 11000 at (190489, 95079) (`runs/rv3-before1/track.jsonl`).
3. **Water layers with no visible water at all.** 43 water layers in Volcanic and 23 in Rift World are all holes or
   invisible cells on the PC (invisible helper layers). Since the water pass draws every cell, each was a sheet of water
   where the PC shows nothing. Rift World's pool layer (12 water cells in a 28 x 28 layer in the void) drew 772 hole cells
   around it.
4. **Water vanishing near the camera.** A hole vertex far above or below the water near the camera makes the engine drop
   the water around it: in the Ruined Village temple pool with the before pack Rynn swims with no surface drawn around her
   (`runs/rv3-before1/p0_04.png`, `p2_02`, `p3_02`). Proof that this is the vertex distance: the Alwarren cistern drew
   normally with its hole vertices at the water height; with all of them 2048 below, part of the pool vanished; 8000 or
   24000 below, or 2048 above, all of it near Rynn (`runs/cis-al_2048`, `cis-al_8000`, `cis-al_up`, `al2-after1`). The
   exact rule in the engine (a guard band or a batch test) was not found.

## Fix (formats)

* `inertWaterLayer`: a PC water layer with no visible water triangle becomes an empty floor layer (all cells holes with
  0xc0, no draw data, no collision).
* `cropWaterLayer`: a water layer is cut to the rows and columns around its water triangles (same index and lists).
* `waterHoleHeights`, per vertex that no water **triangle** uses (each half of a shore cell counts on its own):
  under another layer's water within 600 of its own nearest water it sits 16 under that surface (seams inside one sea);
  over a bank (floor no more than 2048 under the water) 64 under the floor as before; over a drop 24000 under the nearest
  water, so the cell hangs almost straight down into the rock under the edge; with no floor at all (a basin made of models,
  the Alwarren cistern, the Succubus baths) at the water height as before. Only drops go deep because of item 4.

Sheet-in-the-air area (sampled area of drawn hole cells with no floor above them, `tools/holevis.ts`, all 13 campaign levels):
583 cells before, 49 after. Per level before / after: Intro 60 / 5, Ruined Village 50 / 1.6, Wartok 50 / 0.1, Islands
115 / 26, Alwarren 39 / 1.6, Islands Night 49 / 7.6, Volcanic 9 / 2, Rift World 212 / 5.4. What is left are drops deeper
than 24000 (the sea's edge over the Islands abyss) and samples on steep cliff faces.

Tests: `pc.test.ts` (bank, drop, no floor, seam, other water too far, both diagonals of a shore cell, crop, the inert
layer). `npm test` in `editor/packages/formats`: 61 files, 627 tests; `tsc` clean.

## Played (after = packs/after2, before = packs/before1, the same tree without the change)

| Where | Played | Before | After |
| --- | --- | --- | --- |
| Ruined Village sea by the village | placed in the sea, swim, dive, surface, swim to the rocks, climb out | works | works (`runs/rv2-after1`, the same hole rule for the sea) |
| Ruined Village temple pool under the church | placed above it from land, fell in, turned around, swim, dive, surface | no water surface drawn around Rynn | surface drawn all round, swim and dive work (`runs/rv3-*/p*`, `a*`) |
| Ruined Village church bridge, looking down | placed on the bridge | pale sheet over the drop below the bridge | gone (`runs/rv-*/views.png`, `rv3-*/v1_06.png`) |
| Ruined Village headland west of the village | placed on the rock | Rynn treads water on a sheet 840 above the sea | she lands on the rock (`runs/rv3-*/track.jsonl`, `rv-*/v2_*`) |
| Ruined Village River 18 | placed over the river | jagged sheet edges beyond the river | edges follow the water (`runs/rvs-*/b0_03.png`) |
| Alwarren cistern | placed in it, swim, dive, surface, swim to the edge, climb out | works | works, surface drawn (`runs/al3-after2`, compare `al2-before1`) |
| Islands Night sea | placed in it, swim, dive, surface, swim | works | works (`runs/in4-after2`) |
| Rift World pool | placed below, looked up | sheets of the 28 x 28 layer | layer cut to the pool; the view is dominated by a translucent model, so not a clear picture |
| Volcanic | loaded, placed | | loads and plays, no missing-target (`runs/vo5-after2`) |

Contact sheets: `runs/rv3-views.png`, `runs/al3.png`, `runs/cis.png`, `runs/in4.png`, `runs/rw4.png`.

## Water bodies of the campaign (PC data) and their state

Surface heights include the PC water cells that slope up the bank. "Swim": the PC water kind of the vertices (0 = Normal
does not make Rynn swim on the PS2, unchanged). Sheet-in-air columns are cells before / after.

| Level | Body (layers) | Place (x / z, world units) | Cells | Surface | Kind | Swim | Sheets |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Intro | sea (5) | 159744..366592 / 133120..225280 | 3244 | 200000..208234 | sea | yes | 60 / 5 |
| Intro | stream (4) | 274432..309248 / 219136..249856 | 65 | 200000..206020 | river | yes | 0 / 0 |
| Intro | pool "layer 57" | 190464..210944 / 219136..225280 | 30 | 209000 | pool | no (kind 0 on the PC) | 0 / 0 |
| Ruined Village | sea, coast, temple pools (34) | 169984..440320 / 26624..172032 | 6673 | 10384..21608 | sea | yes, played | 25 / 0.9 |
| Ruined Village | rivers above the dam (23) | 292864..425984 / 155648..249856 | 431 | 11000..25856 | river | yes | 15 / 0 |
| Ruined Village | upper rivers 40..55 (14+2) | 407552..462848 / 182272..258048 | 320 | 24000..44000 | river | yes | 1 / 0 |
| Ruined Village | rivers 01..07 (8) | 200704..292864 / 212992..256000 | 183 | 17500..22240 | river | yes | 5 / 0 |
| Ruined Village | west streams (9) | 126976..215040 / 198656..239616 | 180 | 17500..24496 | river | yes | 0 / 0 |
| Ruined Village | rivers 33..36, 20..21 | 387072..425984 / 167936..276480 | 124 | 19500..31216 | river | yes | 4 / 0.6 |
| Ruined Village | well, "water ceiling", layer 224/246 | 223232..249856 / 147456..184320 | 108 | 11000..16000 | well / pool | yes | 0 / 0 |
| Ruined Village | layer 267/268 | 200704..223232 / 256000..276480 | 45 | 12500..23002 | stream | yes | 0 / 0 |
| Wartok Canyons | 9 river systems (105) | 59392..364544 / 4096..497664 | 2424 | 3000..65000 | rivers, falls pools | yes | 50 / 0.1 |
| Grimstone Mines | mine stream (2) | 149504..182272 / 262144..272384 | 50 | 27680..34848 | fast stream | yes (currents) | 0 / 0 |
| Grotto | "Ice Room", 3 layers | 319488..362496 / 178176..264192 | 73 | 64000..65772 | ice (snow material) | no (kind 0) | 0 / 0 |
| Islands | sea (81) | 217088..575488 / 208896..782336 | 24353 | 50276..64272 | sea | yes | 114 / 26 |
| Islands | 3 rivers (17), pool | see `bodies-state.txt` | 1170 | 20000..42252 | river / pool | yes (pool: no) | 0.6 / 0.2 |
| Alwarren | 20 canals, cisterns, pools (37) | 176128..530432 / 354304..522240 | 1758 | 47142..127146 | canals, cistern, pools | yes, cistern played ("endwater": no) | 39 / 1.6 |
| Rift Crystal | 2 pools | 81920..106496 / 153600..190464 | 122 | 219656..224000 | pool | yes | 0 / 0 |
| Islands Night | sea (46), pool | 217088..602112 / 350208..692224 | 13967 | 50276..64272 | sea | yes, played | 49 / 7.6 |
| Succubus Lair | dw1/dw2 channels (8), two baths | 83968..256000 / 210944..335872 | 508 | 204800..222704 | channel, baths | yes | 0 / 0 |
| Volcanic | layer 938 and 4 more (5) | 268288..346112 / 466944..524288 | 647 | 149996..161806 | river (water material) | yes | 9 / 2 |
| Volcanic | 43 layers with no visible water | | 0 | | helper layers | no | drew (mostly inside the ground), now none |
| Rift World | pool in the void (1) | 632832..645120 / 538624..544768 | 12 | 63276 | pool | no (kind 0) | 212 / 5.4 |
| Rift World | 23 layers with no visible water | | 0 | | helper layers | no | drew (mostly inside the ground), now none |

Full per-body list with layer indices: `build/review/ootf-water/bodies-state.txt`. No PC level has lava as a water layer
(the 2026-11-08 lava mapping still has no case).

## Not verified

* Splash and swim sounds, and the camera at the water line: runs were played with audio off; the camera was only seen in the
  shots above.
* Wartok Canyons and Islands water were not entered by Rynn (both start on Arokh); their sheets were measured from the data.
* The Intro (a cut scene) and Volcanic were not looked at for sheets in game; Rift World's picture is unclear.
* The engine's rule that drops water near a far-off vertex is inferred from four pictures; a drop vertex 24000 down near
  the camera (Rynn swimming right at a ledge pool's lip) may still hide some water there. The temple pool was fine.
* `place` while Rynn swims sets her height to garbage (NaN-like) in both packs: a test aid limit, not a game fault.
* PC water cells that slope up into the bank (the sea's surface reaching 21608 at the temple) are drawn as on the PC and
  were left alone; one let Rynn tread water at 21400 above the temple pool (`runs/base` track), as the PC data says.
