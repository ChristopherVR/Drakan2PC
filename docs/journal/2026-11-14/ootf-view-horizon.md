# View distance without a world edge: far scenery (2026-11-14, round 6)

Brief: make a larger `[video] view_distance` look right (the sea ended in a flat plane with a straight edge in Drakan 2,
the Order of the Flame Islands sea had gaps), so it can become the default, with the old value one setting away.
Scratch (not committed): `build/review/limits-horizon/` (`play` = copy of `baseline-play` with the owner's ini and HD
textures, `elf =` on a private ELF; `runs/`, `ev/` cited pictures, `bench/` frame times, `tools/`, `tours/`, `lvl/`).

## What was wrong

**The edge is where the level's draw lists end, not where the world ends.** The terrain draw (`0x152330`) draws only
the layers of the view layer's `visible` list (layer `+0x70` u16 list, `+0x78` count; view layer `0x4dad58 +0x8c`).
The lists were made for the original view distance:

* Ravenshold (Drakan 2): the view distance there is **45 cells** with a fog range of 31.95 (fog from 26,726 to 92,160
  units), not 75 as in Surdana and the converted levels. At the flight spot over the start (1298635, 171325, 670020) the
  view layer (108) lists 64 layers reaching 72 cells; unlisted layers start 48 cells away, while the sea (86 tiles of up
  to 35 x 35 cells at height 128,000) runs on for 160 cells and the union of all 660 layers is 167 cells from the eye
  in its nearest direction. At view distance 2 (90 cells) the fog no longer hides where the list stops, so the listed
  sea tiles show as a flat plane with a straight edge (`ev/r2.jpg`, row 2).
* Order of the Flame Islands and Islands Night: the gaps in the far sea are sea hole cells over islands whose floor
  layers are not in the list. From the layer under (340000, 610000) the listed sea of the Islands has 5,465 hole cells:
  1,905 over listed floors, **3,517 over floors the list does not name**, 43 over nothing (`tools/gaps.ts`); Islands
  Night 3,263 of 4,107. The converter's sea link (`linkFarWater`) adds far sea layers but no land, which is why linking
  the sea from 160 cells changed nothing.

A fog curve alone cannot hide this: the edge sits inside the scaled fog, and moving only the fog start out at the
original far distance (live poke, `ev/r2.jpg` row 3) barely clears the mountains. Fitting the fog to the union of the
layers does not help either (the union is 167 cells away at Ravenshold while the list ends at 72).

## What changed

`[video] far_scenery` (default 1, live, PAL, needs `heap_extend`; game commits `0d29a9a4`, `ee33a3e7` for the diagnostics `DRAKAN_FAR_SCENERY_TYPES` and `DRAKAN_FAR_SCENERY_REACH`). With `view_distance` above 1
the terrain draw walks a wider list: the **closure of the visible lists** from the view layer over layers whose box lies
within the scaled view distance (+2 cells) of the view layer's box (`world::view_distance::widenedList`, tested in
`world_view_distance_test`). So only layers the game itself lists from some nearby place are added; the original
entries come first, the added ones by distance. Numbers: Ravenshold layer 108 lists 64, draws 166 at 2; Islands
layers 105 / 136 / 129 / 249 list 49 / 93 / 89 / 120 and draw 178 / 239 / 315 / 353. Offline the closure at reach 150
leaves 26 of the 3,517 Islands holes uncovered (all at reach 225).

* The list lives in one block from the game's heap (malloc `0x4ac628` through a nested guest call, under the game's
  heap semaphore `*(0x515260)`, polled so a busy heap only skips the frame) and is swapped into the view layer for the
  draw only. It is put back after the draw's layer loop (at its terrain view call `0x153828`), at the draw's return,
  or at the next draw.
* The cell lookup `0x14b6b8` (actor and view layer refiling, which searches the same list) always sees the level's own
  list: the hook puts the original count back for the call (the wider list starts with the original entries).
* Object activity and the sound distance keep the PS2 numbers as before (view distance hook); the per-frame activity
  loop `0x15cba0` has one caller, the world update's loop over the view layer's list, which never sees the swap.
* `far_scenery = 0`, or `view_distance = 1`, is the behaviour before this change.

## What the player sees

All with the owner's ini (copied play folder), 3840 x 2160 window, direct level starts, god mode, the setting from the
ini per run, and harness placements to the view spots (placed, not played): Ravenshold on Arokh at (1298635, 171325,
670020), the Islands on Arokh at (340000, 70000, 610000), four yaws each.

* `ev/raven_compare.jpg` (Ravenshold, rows: 1, 1.5 with far scenery, 2 with far scenery off = the setting as it was,
  2 with far scenery): at 2 without it the sea is a flat plane with a straight edge (north) and a hard line under the
  mountains (west); with far scenery the sea runs into the fog and the west mountains continue behind each other. 1.5
  looks right too. `ev/rv2on.jpg`: all four directions at 2.
* `ev/islands_compare.jpg` (Islands, rows: 1, 1.5, 2 off, 2 on): at 2 off the far sea has island-shaped gaps (as found
  on the 14th); with far scenery the far islands, the castle and the towers stand in an unbroken sea. 1.5 shows the
  same with the haze nearer.
* **Texture memory at 2 in the Islands:** with the owner's `game_texture_cache_kb = auto` (8 MB in packs) the game's
  texture cache overflows (`[texbudget] ... 100 texture loads did not fit`): a near island and Arokh were not drawn in
  the first view (`ev/is2onb.jpg`, the same view twice). With 16384 KiB the view is complete (`ev/is2t16.jpg`). 1.5 at
  auto drew everything in this view (`islands_compare.jpg` row 2). Handed to the texture memory work.
* Islands Night: the start has no Arokh, and the placement at the Islands spot put Rynn into the sea, so the runs
  (`runs/in*`) show the underwater view and say nothing. Offline (`tools/gaps.ts`, the level in the current pack): from
  the layer under that spot the list closure at reach 152 covers all 3,263 sea holes over unlisted islands (the
  original list: 781 of 4,107 holes covered).
* Andrellian Isles (second Drakan 2 sea level): the start is a long conversation, so the raised placement only
  photographed the dialogue (`runs/an*`); no picture. Its fog setter had last been called with 16 cells there.

## Frame time

`scripts/perf/scene_ab_bench.py` with its turn lock, the owner's ini, the setting cycled live inside each run (order
rotated per repeat), 8 s windows, world frame time in ms (median of the runs, range):

| Scene | 1 | 1.5 | 2 | Runs | Machine |
| --- | --- | --- | --- | --- | --- |
| Ravenshold start on Arokh (far scenery on) | 5.53 (4.86 to 6.19) | 5.13 (5.02 to 5.24) | 6.09 (5.08 to 7.11) | 2 used of 3 | 1st run busy (a compiler and a replay tool), dropped; 2nd had compilers during it |
| Surdana start (far scenery on) | 6.32 (6.28 to 6.40) | 7.66 (7.34 to 7.97) | 8.01 (7.99 to 8.13) | 3 | quiet |
| OotF Islands on Arokh (far scenery on) | 7.01 (7.00 to 7.06) | 28.08 (27.85 to 29.25) | 37.27 (36.49 to 38.31) | 3 | quiet |
| OotF Islands at 2: far scenery 0 / 1 | 8.40 / 36.98 | | | 2 | quiet |
| same with a 16 MB game texture cache | 12.24 / 45.90 | | | 2 | one run with another game |

Raw: `bench/raven`, `bench/surdana`, `bench/islands`, `bench/isl-vd2`, `bench/isl-vd2t16` (summaries in the `.out`).
All three CPU threads grow together in the Islands (EE 8.8 to 37.5 ms, VU1 8.2 to 34.1, GS 7.1 to 30.9 per world
frame at 2), so it is the amount of terrain drawn. A per-type run (`bench/isl-types`, `DRAKAN_FAR_SCENERY_TYPES`, two
runs each with another game in some of them, so only the order of size holds): adding only the far water costs about
nothing (12.9 ms against 12.3 off in that set), adding the far floors is the whole cost (63 ms), leaving out the
ceilings saves little. The converted levels draw their full PC terrain grid at every distance (no terrain detail
level in this engine) and list up to ten times the chunks of a disc level, so 150 cells of islands is several times
the work of Ravenshold's mountains.

## Defaults and why

* `far_scenery` = 1: it changes nothing at `view_distance` 1 and makes every larger value look right in the levels
  seen; off is one setting away.
* `view_distance` stays 1. In Drakan 2 it now looks right and costs little (Ravenshold within noise, Surdana +1.7 ms at
  2), but in the converted Order of the Flame Islands 1.5 is four times the frame time of 1 (28 against 7 ms) and 2
  five times, and the 8 MB pack texture cache is not enough at 2. A Drakan 2 player can set 2; making 2 the default
  for disc levels only (like `game_texture_cache_kb = auto`) is the obvious next step once a guard fight has been
  played at 2. The help text of the slider now says the cost, not that levels show their edge.

## Not verified

* A Drakan 2 guard or creature fight at 2 with far scenery (Ravenshold flight and Surdana bench only); the cell lookup
  guard is from code reading (the lookup always sees the original count) and was not exercised by a refile test.
* Whether the draw is ever suspended mid-list on another guest thread in play (the hook handles it; not observed).
* Islands Night and Andrellian Isles in game (see above); Wartok, Volcanic and the other converted outdoor levels.
* Interiors added by the closure (ceilings and floors of caves the outdoor lists name) were not seen from outside in
  any picture; they are what the game draws from the nearby places that list them.
* The Islands frame time with the texture cache at 16 MB on a quiet machine; WebGPU; the PC game for reference.
* `heap_extend = 0`: far scenery is off there (not tested with it on).
