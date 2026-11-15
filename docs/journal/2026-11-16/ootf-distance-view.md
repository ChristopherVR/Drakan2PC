# Order of the Flame: hills missing at a distance (2026-11-16)

Owner's item 9, "graphical issues when viewing from a distance", with a screenshot (taken 15 November 10:41, before
`cf20bc18`): an Order of the Flame level at night, palms, a stone bridge over a river, a burning A-frame house. Three
things were visible: (a) far hills and river banks cut off in straight and stepped lines with the sky showing through,
(b) pink and blue stripes in the sky in those gaps, (c) flat, untextured dark mountains. Scratch, play folder, tools,
runs, packs and pictures: `build/review/ootf-distance2/` (private, not tracked). Nothing from either game is quoted.

## Where

**Ruined Village** (`OotF 01`), on the ridge west of the Human Bridge, looking east along the river: the bridge is the
converted "Human Bridge" at (247808, 18000, 217088), the burning house the "SmallHouseBurnt01" at (248215, 232896) with
two flames on it, the rope bridge in the far centre one of the "Bridge Rope" pairs at x 375,000 to 407,000. Reached by
placement at (222000, 30000, 221000), heading -1.5708 (placed, not walked). The harness picture `runs/vd1/a1.png` (crop
`ev/a1_cmp.jpg`, top) is the owner's view: the left bank behind the bridge is missing in stair steps and the sky shows
through, as in the screenshot. A second spot, (226000, 30000, 210000), shows a house on a floating piece of ground with
nothing around it (`runs/cur3/s236_y-1.5708.png`, `ev/cur4.jpg`).

## (b) The stripes: gone already

The current exe (dist, 15:39) with the dist pack, at and around the same spot, in every run (`ev/cur1.jpg` to
`ev/cur5.jpg`, `ev/vd1.jpg`, `ev/nofar1.jpg`, `ev/td2.jpg`): the gaps show flat sky colour, no stripes. That is the night sky texture kept native by `cf20bc18`
(`2026-11-15/ootf-distant-sea.md`), the same band the far sea showed. Nothing more done for it.

## (a) The gaps: the level's draw lists stop at the PC fog

The terrain draw `0x152330` draws only the layers named in the view layer's `visible` list. The converted levels keep
the PC lists, which the PC editor made for the PC fog (Level Properties fog 12 / 30 lu, 1 lu = one cell), while the
converted level keeps the PS2 template's view distance (fog from 26 to 75 cells: only a few per cent fogged at 30
cells). Counted over the converted Ruined Village (`tools/reach.ts`, every floor or water layer against every floor and
ceiling, by box distance):

| Box distance (cells) | 0-4 | 5-9 | 10-14 | 15-19 | 20-24 | 25-29 | 30-34 | 35-39 | 40 and more |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Listed | 68 % | 46 % | 39 % | 31 % | 28 % | 21 % | 4 % | 0.3 % | 0 |

So inside 30 cells the PC lists are a visibility set made for a camera near the ground (hidden layers left out), and
past 35 cells nothing is listed (`linkFarWater` adds only the sea). At the owner's spot the layers that fill the gap are
the river bank and cliff 181 and 183 (box distance 12 and 22 cells from the view layer 92, not listed although in
sight from the camera on the ridge) and the hills beyond (184, 188, 204 and others, 25 to 31 cells; `tools/near.ts`,
`tools/why.ts`). Proof in the game: the runtime's far scenery closure (`view_distance` 1.01, which turns it on with the
fog unchanged) fills the gap and the floating house gets its hill (`ev/a1_cmp.jpg` bottom, `ev/vd101.jpg`), but its
overlay frame rate at the floating-house spot fell from 52.8 to 27.6 (`vd1` against `vd101`, other games running)
because it draws every layer listed from anywhere nearby, hidden or not.

### First try: longer lists in the level (dropped)

A converter step (`linkFarLand`, formats, not committed; kept as `tools/ootfvisibility.ts`) added to each floor and
water layer's list the floors and ceilings 24 to 77 cells away that a line of sight reaches over the level's ground from
1,200 or 6,000 units above the layer. Ruined Village: 13,593 entries added (25,450 to 39,043, longest list 149 to 190,
still under the disc's 41,362 / 221). In the game it broke: two runs of the full converted pack crashed after the
opening scene (a call through a null pointer in the object update `0x140fe4`, then in `0x12d430`). Two runs of a pack
converted without the step, with only Ruined Village's lists replaced afterwards (`tools/patchvis.ts`, `mods_patch`),
logged VU1 packets running past 64 KB and GIF tags with FLG 3 from the opening scene on and ended in a GPU device hang,
the second with a 16 MB texture cache. The same pack without the replaced lists (`mods_nofar`) ran clean, and so did
the runtime closure at 8 MB. The lists are read outside the terrain draw, so they are not changed in the level. (The
`far` and `nofar` packs were converted from a formats tree that also held another session's uncommitted edits, which
changed between the two conversions: 12,213 and 12,221 files. The patched pack isolates the lists from that.)

## Fix: far land in the runtime, for the draw only

`world/FarLand.h` (rules, `world_far_land_test`) and `drakan_view_distance.cpp`. At `view_distance` 1 in a converted
level (`drakan_texture_budget::convertedLevel`: a level from a pack, or a campaign pack as the current game), far
scenery's existing swap gives the terrain draw the view layer's own list plus its **far land**: the unlisted floors and
ceilings up to 32 cells from the view layer (box to box: the PC fog's 30 cells plus the engine's 2 cell margin) that a
line of sight from the camera's cell reaches over the level's ground. A list is made per view layer and camera cell
(2,048 units across, 1,024 high) and depends only on that cell: the observers are the cell's four corners and centre at
the top of its height bin, so it holds wherever the camera is in the cell. Targets: up to 3 x 3 vertices of the layer
plus its 12 highest; the ground is the highest drawn floor at each vertex, holes and invisible cells left out (cell bits
0x50 and 0xa0); a ray is blocked where it passes more than 64 units under it, with 0.5 cell at the observer end and 1
cell at the target end not tested. Only floor and water view layers get far land (a ceiling view layer is a cave). The
terrain is read once per layout from the guest's layer table (the fields `drakan_scene_geometry` reads); lists are kept
in a cache of up to 1,024 and take 0.3 to 1.5 ms each (log). The cell lookup keeps seeing the level's own list as
before. Part of `[video] far_scenery` (default 1); `DRAKAN_FAR_LAND=0` turns it off alone; `DRAKAN_VIEW_DISTANCE_LOG=1`
prints each list. `sceneryWidens()` now also holds with far land on, so `game_texture_cache_kb = auto` gives converted
levels 16 MB (the setting's text and the settings page say what far scenery does at 1). Diagnostics:
`DRAKAN_FAR_LAND_NEAR` (least candidate distance, 0), `DRAKAN_FAR_LAND_REACH` (cells, 32; 0 = the view distance + 2).

How it got there (lists at the owner's spot, view layer 92 with 100 entries of its own, from the log; frame times
below):

| Version | Drawn at layer 92 | What it showed and cost |
| --- | --- | --- |
| Observers over the view layer (3 x 3 vertices, 1,200 and 6,000 up), 24 to 77 cells | 172 | far hills, but the gap behind the bridge stayed (`ev/fl_a1.jpg`): 181 and 183 are nearer than 24 |
| The same from 0 cells | 188 | gap filled (`ev/fl2_a1.jpg`); 31 against 83 fps at the floating-house spot in single frames |
| Camera, four points 1.5 cells around it, all again 1,500 up; 0 to 77 cells | 139 to 166 | gap filled (`ev/fl3_a1.jpg`); +38 % frame time at the spot |
| Camera point alone, 0 to 77 | 121 to 126 | the same picture (`ev/rank_a1.jpg`); +13 % at the spot, **+131 % on Arokh over the Islands** |
| Camera cell (corners and centre), 0 to 77 | 132 to 152 | the same picture (`ev/fin_a1.jpg`); a list no longer depends on where in its cell it was made |
| Camera cell, 0 to 32 cells (committed) | 116 to 119 | the gap behind the bridge filled, the hazy hills past 32 cells left out (`ev/reach_a1.jpg` rows: off, 30, 35, 77 cells; `ev/final_a1.jpg`); Islands +5 % at 30 cells |

From the air the camera sees most of a level within the view distance, which is the closure's kind of list again, so
the reach is held to the PC fog's end. That fills the hole the owner showed (the layers are 12 to 31 cells away), but
**land in sight 32 to 75 cells away is still not drawn**: the haze is only about a tenth at 32 cells and under half at
50, so far hills on the horizon stay missing (`ev/reach_a1.jpg`, the 77-cell row has hills the 30 and 35 rows lack)
and a cut edge can still show elsewhere past 32 cells. `DRAKAN_FAR_LAND_REACH=0` (env only, no ini key) draws the
whole view distance at the cost measured below.

Calibration of the line of sight (offline, the TypeScript version over the converted Ruined Village, observers over
the layer): of the pairs the PC lists itself at 20 to 40 cells, 85 % pass (93 % with an eye at 15,000, 96 % at 30,000);
of the disc's own Surdana lists (made for 75 cells), 70 % of the pairs 30 to 77 cells apart pass (88 % with an eye at
30,000). So the test is strict: what it adds is in sight, and some layers in sight from a little higher stay out.

## Seen in game after the fix

Private build of `build/review/ootf-distance2/game` (HEAD `deb41a6a` plus this change, then the commits after it; the
tree also held other sessions' uncommitted runtime edits, the ponytail cloth among them), exe copied into the play
folder; dist pack (`mods_before`, the same file as `dist/DrakanPC/mods/ootf-levels.zip`); the owner's ini with
`elf =` on a private copy. Placed, not played, unless said. Final exe `eda605ab...`:

* Owner's view, before and after (`runs/fl_off/a1.png`, `runs/final_on/a1.png`, side by side `ev/final_a1.jpg`): the
  bank behind the bridge and the cliff over the river are drawn where the sky showed in stair steps; no stripes.
* Floating house spot (`ev/final_b0.jpg`): the house stands on its hill, the bridge and the burning house appear below.
* Played: from the ridge with W held, two mouse turns, down to the river and into it (`runs/walk_final`, 33 frames,
  `ev/walk_final.jpg`; the same with the camera-point version in `runs/walk_on`): no sky gaps; looked at as a sheet,
  so pop-in at cell changes was not judged frame by frame.
* Islands on Arokh at (340000, 70000, 610000) with the 77-cell version (`ev/islands_off_on.jpg`, left off, right on):
  far islands and the castle stand in the sea. No 32-cell frame was shot there; most of those islands are further than
  32 cells and stay as before.
* Volcanic loads and runs (`runs/volc_on`, 77-cell version; not run with the final exe).
* No `[texbudget]` misses, no VU1 reports, no fatal line in any run with the runtime version, including the six
  `bench5` runs of each scene and `walk_final` with the final exe.
* The first frame of `walk_final` (taken just after the placement) has the camera inside the rock, with the sky through
  an opening; the next frames are normal. Not looked into.

## Frame time

`scene_ab_bench.py`, interleaved pairs, `DRAKAN_TEX_CACHE_KB=16384` on both so only far land differs. Ruined Village
runs had another agent's game running most of the time (noisy); `bench3` and `bench4` over the Islands were quiet,
`bench5` was not.

| Scene, version | World fps off / on | Frame ms off / on (median) | VU1 ms off / on | GS ms off / on |
| --- | --- | --- | --- | --- |
| Ruined spot, camera + cushion + rise, 77 (`bench`, 8 / 16 MB) | 68.0 / 52.4 | 13.37 / 18.43 (+38 %, 3 of 3) | 8.57 / 12.31 | 10.57 / 14.01 |
| Ruined spot, camera point, 77 (`bench2`) | 59.0 / 52.9 | 16.52 / 18.71 (+13 %) | 8.67 / 11.71 | 11.08 / 13.86 |
| Islands on Arokh, camera point, 77 (`bench3`, quiet) | 74.8 / 32.5 | 13.31 / 30.33 (+131 %, 3 of 3) | 11.74 / 27.73 | 10.33 / 23.70 |
| Islands on Arokh, off / reach 40 / reach 30 (`bench4`, 2 pairs, quiet) | 101.1 / 59.7 / 96.0 | 9.78 / 16.70 / 10.27 (+71 % / +5 %) | 8.64 / 15.78 / 9.32 | 7.68 / 13.78 / 8.07 |
| **Committed (camera cell, reach 32)**, Islands on Arokh (`bench5`, another game running) | 64.0 / 54.9 | 15.57 / 17.79 (+14 %, range +2 to +59 %) | 12.62 / 13.67 | 11.41 / 12.46 |
| **Committed**, Ruined spot (`bench5`, another game running, one off window at 33 ms) | 63.6 / 54.0 | 15.77 / 17.86 (+13 %, 2 of 3 pairs slower) | 8.87 / 10.55 | 10.88 / 12.92 |

The frame times of the last two rows are noisy (the machine was busy); the vector unit and GS times are the steadier
measure: the committed version adds about 8 % VU1 work over the Islands and 19 % at the owner's spot, against 136 %
and 35 % for the whole view distance.

The converted levels draw every layer on the full PC grid (no terrain detail level), so far land costs what it draws.
`far_scenery = 0` (or `DRAKAN_FAR_LAND=0`) is the old draw.

## (c) The flat dark mountains: the far colour map at night

Same spot, `terrain_distance` 2 against 1 (`ev/td2_a0_bright.jpg`, the a0 frames brightened 2.5x): at 1 the far peak
is one flat brown, at 2 it has its rock texture. So the far mountains have their textures; past the material's splat
fade distance (30 cells, `graphics.md` "Ground textures fade") the engine draws the level's 4 texel per cell far colour
map, and the night ambient (`0x15131e`) makes it dark. `terrain_distance` above 1 is the player's option (about twice
the terrain passes at 2). Not changed.

## Tests

* `ctest --preset runtime-tests`: 202 of 202 passed (6 data-dependent skipped) on the first runtime version and again
  on the final one. `world_far_land_test`: layers in sight added nearest first, listed and out-of-reach layers and ceiling
  view layers left alone, a ridge hides, holes hide nothing, the ground's far edge, the camera cell's observers, a low
  camera cell behind a ridge against a high one.
* clang-format clean on the changed files; `python scripts/run_tests.py` passed; `repository_guard.py` clean.

## Not verified

* Frame times on a quiet machine at the Ruined Village spot (the Islands runs were quiet).
* Other converted outdoor levels (Wartok Canyons, Islands Night, Alwarren, Rift World, Succubus Lair) and the
  multiplayer maps from the pack; far land applies to any pack level, also one whose lists were made for the PS2.
* Pop-in when the camera moves from cell to cell (cells of 2,048 units; a layer that comes into sight over a crest
  appears with the next cell); caves and interiors seen from outside; a ceiling view layer.
* The owner's exact camera: the spot was found from the level data and the picture; the harness frames are near it.
* The flat far mountains: `terrain_distance` 2 was looked at once; no fix.
* WebGPU / Vulkan (the change is in the guest draw list, renderer independent) and the release build.
* Main session: rebuild the exe (`Build-LocalGame.ps1 -Incremental`). The pack does not need rebuilding (no converter
  change). Converted levels now get the 16 MB game texture cache with `game_texture_cache_kb = auto`.
