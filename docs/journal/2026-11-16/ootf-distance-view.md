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
ceilings up to 77 cells from the view layer (box to box, the view distance plus the engine's 2 cell margin) that a line
of sight from the camera reaches over the level's ground. Targets: up to 3 x 3 vertices of the layer plus its 12
highest; the ground is the highest drawn floor at each vertex, holes and invisible cells left out (cell bits 0x50 and
0xa0); a ray is blocked where it passes more than 64 units under it, with 0.5 cell at the camera end and 1 cell at the
target end not tested. Only floor and water view layers get far land (a ceiling view layer is a cave). The terrain is
read once per layout from the guest's layer table (the fields `drakan_scene_geometry` reads); a list is made for each
view layer and camera cell (2,048 units across, 1,024 high) the first time the camera is there, kept in a cache of up to
1,024 lists, and takes 1 to 4 ms (log). The cell lookup keeps seeing the level's own list as before. Part of
`[video] far_scenery` (default 1); `DRAKAN_FAR_LAND=0` turns it off alone; `DRAKAN_VIEW_DISTANCE_LOG=1` prints each
list. `sceneryWidens()` now also holds with far land on, so `game_texture_cache_kb = auto` gives converted levels 16 MB
(the setting's text and the settings page say what far scenery does at 1). Diagnostics: `DRAKAN_FAR_LAND_NEAR`,
`_CUSHION`, `_RISE`, `_REACH`.

How it got there (lists at the owner's spot, view layer 92 with 100 entries of its own, from the log):

| Version | Drawn | What it showed |
| --- | --- | --- |
| Observers over the view layer (3 x 3 vertices, 1,200 and 6,000 up), candidates from 24 cells | 172 | far hills, but the gap behind the bridge stayed (`ev/fl_a1.jpg`): 181 and 183 are nearer than 24 |
| The same from 0 cells | 188 | gap filled (`ev/fl2_a1.jpg`), but 31 against 83 fps at the floating-house spot in single frames |
| Camera, four points 1.5 cells around it, and all 1,500 higher; from 0 cells | 139 to 166 | gap filled (`ev/fl3_a1.jpg`); +38 % frame time (below) |
| Camera alone (committed) | 121 to 126 | the same picture (`ev/rank_a1.jpg`, top against middle) |

Candidates from 24 cells drew 148 here, a reach of 50 cells 135. The camera alone was the largest cut for the same
picture, so it is the default.

Calibration of the line of sight (offline, the TypeScript version over the converted Ruined Village, observers over
the layer): of the pairs the PC lists itself at 20 to 40 cells, 85 % pass (93 % with an eye at 15,000, 96 % at 30,000);
of the disc's own Surdana lists (made for 75 cells), 70 % of the pairs 30 to 77 cells apart pass (88 % with an eye at
30,000). So the test is strict: what it adds is in sight, and some layers in sight from a little higher stay out.

## Seen in game after the fix

Private build of `build/review/ootf-distance2/game` (HEAD `deb41a6a` plus this change; the tree also held other
sessions' uncommitted runtime edits, the ponytail cloth among them), exe copied into the play folder; dist pack
(`mods_before`, the same file as `dist/DrakanPC/mods/ootf-levels.zip`); the owner's ini with `elf =` on a private copy.
Placed, not played, unless said.

* Owner's view, before and after (`runs/fl_off/a1.png`, `runs/rank*` and the committed version's `runs/rk_tight/a1.png`;
  side by side `ev/fl3_a1.jpg`, crops `ev/rank_a1.jpg`): the bank behind the bridge, the cliff and the hills beyond are
  drawn, fading into the haze; no sky shows through and no stripes.
* Floating house spot (`ev/fl3_b0.jpg`, the version with the cushion and rise; the committed one was not shot there in
  that pose): the house stands on its hill, the bridge and the burning house appear below.
* Played: from the ridge with W held, two mouse turns, down to the river and into it (`runs/walk_on`, 33 frames,
  `ev/walk_on.jpg`): no sky gaps in any frame; 67 lists made on the way. Whether layers pop in at the edge of sight when
  the camera crosses a cell was not judged frame by frame.
* Islands on Arokh at (340000, 70000, 610000), three headings (`ev/islands_off_on.jpg`, left off, right on): with far
  land the far islands and the castle stand in the sea where before there was open water or a fogged ghost. The list
  there goes from 120 to 175 (the closure at 1.5 drew 353).
* Volcanic loads and runs (`runs/volc_on`, lists of 32 grow to 41). No `[texbudget]` misses, no VU1 reports, no fatal
  line in any run with the runtime version.

## Frame time

`scene_ab_bench.py`, Ruined Village at the owner's spot, 3 interleaved pairs, `DRAKAN_TEX_CACHE_KB=16384` on both so
only far land differs, one other agent's game running during most runs (the tool's quiet check could not be met), so
frame times are noisy (one "off" window at 30 ms):

| Version | World fps off / on (median) | Frame ms off / on | VU1 ms per frame off / on | GS ms off / on | EE ms off / on |
| --- | --- | --- | --- | --- | --- |
| Camera plus cushion and rise (`bench`, texture cache 8 / 16 MB) | 68.0 / 52.4 | 13.37 / 18.43 (+38 %, 3 of 3 pairs) | 8.57 / 12.31 | 10.57 / 14.01 | 14.45 / 18.68 |
| Camera alone, committed (`bench2`) | 59.0 / 52.9 | 16.52 / 18.71 (+13 %, 2 of 3 pairs slower) | 8.67 / 11.71 | 11.08 / 13.86 | 16.13 / 18.66 |

The VU1 and GS columns do not overlap between off and on: far land costs about a third more vector unit work at this
spot, because the converted levels draw every layer on the full PC grid (no terrain detail level) and the far land is
real ground in sight. The frame time is less clear on this machine; +13 % is the median. `far_scenery = 0` (or
`DRAKAN_FAR_LAND=0`) is the old draw.

## (c) The flat dark mountains: the far colour map at night

Same spot, `terrain_distance` 2 against 1 (`ev/td2_a0_bright.jpg`, the a0 frames brightened 2.5x): at 1 the far peak
is one flat brown, at 2 it has its rock texture. So the far mountains have their textures; past the material's splat
fade distance (30 cells, `graphics.md` "Ground textures fade") the engine draws the level's 4 texel per cell far colour
map, and the night ambient (`0x15131e`) makes it dark. `terrain_distance` above 1 is the player's option (about twice
the terrain passes at 2). Not changed.

## Tests

* `ctest --preset runtime-tests`: 202 of 202 passed (6 data-dependent skipped) on the first runtime version; after the
  camera observers the changed tests again (`world_far_land_test`, settings, view distance, texture budget: 7 of 7).
  `world_far_land_test`: layers in sight added nearest first, listed and out-of-reach layers and ceiling view layers
  left alone, a ridge hides, holes hide nothing, the ground's far edge, camera observers (alone by default; with a
  cushion and a rise), a low camera behind a ridge against a high one.
* clang-format clean on the changed files.

## Not verified

* Frame times on a quiet machine; the main session should repeat the `bench2` A/B before deciding whether far land
  stays on by default (one flag, `g_farLandOn` in `drakan_view_distance.cpp`, or a setting of its own).
* The Islands flight cost (lists only, no bench), the other converted outdoor levels (Wartok Canyons, Islands Night,
  Alwarren, Rift World, Succubus Lair) and the multiplayer maps from the pack.
* Pop-in when the camera moves from cell to cell; caves and interiors seen from outside; a ceiling view layer.
* The owner's exact camera: the spot was found from the level data and the picture, and the harness frames are near it,
  not the same pose.
* The flat far mountains: `terrain_distance` 2 was looked at once; no fix.
* WebGPU / Vulkan (the change is in the guest draw list, renderer independent) and the release build.
* Main session: rebuild the exe (`Build-LocalGame.ps1 -Incremental`). The pack does not need rebuilding (no converter
  change).
