# Draw distance and detail: the PS2 view limits, and a view distance setting (2026-11-14, round 6)

Brief: remove PS2-era view limits so worlds look complete at a distance on PC, from the host side, each lift behind a
`drakan.ini` setting with the old value one setting away. Scratch (not committed): `build/review/limits-distance/`
(`play` = copy of `baseline-play` with the owner's ini and HD textures, `elf =` on a private ELF; `runs/`, `ev/` cited
pictures, `bench/` frame times, `tools/`, `tours/`, `packs/`).

## Inventory: where each distance and detail limit is decided

| Limit | Decided in | PS2 value | Port setting (default) |
| --- | --- | --- | --- |
| **World view distance (far clip) and distance fog** | world fields written by the fog setter `0x142f70`; readers in `graphics.md` section 10 | far 75 cells = 153,600 units, fog from 53,760 to 153,600, in every level seen (Surdana, Ravenshold, Wartok Canyons, Islands, Grotto, Mines, Ruined Village RAM dumps) | **new** `[video] view_distance` (1) |
| Underwater view and fog | the same setter's twin `0x142fe0` | 47.75 cells | not touched |
| Terrain grid radius | `0x153828`, from the view distance | 75 cells | follows `view_distance` |
| Ground textures fade into the 4 texel per cell far map | material "Splat Fade Distance", `0x158148` | 30 cells (rock 45 to 50); a third of the texture gone at 20,000 units | `terrain_distance` (1) |
| Object draw distance and fade (people, plants, props) | object `+0xf8` (model float, radius x 70.71, or a setter), capped by the view distance in `0x18ba28`; fade in the last quarter | 4,200 units for a small flower, NPCs 35,000 | `draw_distance` (1); the cap follows `view_distance` |
| Character LOD | 0x0208 LOD tables, selector `0x102c90` | villagers 6,000 / 12,000, Rynn 2,300 | `lod_distance` (4) |
| Static near / far model | `0x18b760` | one squared distance per model | `lod_distance` |
| Broken far LOD of the fat villager | disc model | | `CharacterLodFixOverride` (issue #6), unchanged |
| Particles (snow, effects) | `0x193d08`, `0x1941a0`, `0x195878` | half and 0.9 of the view distance | follow `view_distance` |
| Object activity (method `+0x50` of objects near the camera, per layer) | `0x15cba0` | `min(+0xf8, view distance)` | kept at the PS2 value |
| Sound distance | `0x1b1ee0`, `0x1b7f30` from world `+0x38c` | the view distance | kept at the PS2 value |
| Character shadows | engine limits agent (`shadow_distance`) | | not mine |
| Which layers are drawn at all | each terrain layer's `visible` list (level data) | disc: authored for 75 cells; Order of the Flame: the PC lists (authored for 12 to 30 lu) plus the sea within 96 cells (`linkFarWater`) | converter |
| End of the world | level data | Drakan 2 levels end in flat sea planes or cut-off ground just past 75 cells; the converted Islands sea has gaps where the PC water ends | none |
| Terrain geometry at distance | | no vertex reduction found: the chunks are drawn on the full 2048 unit grid at every distance (the far map is the only terrain LOD); not traced further | none |

So the far clip of this engine is the view distance: there is no separate far plane in VU1 (the clipper has five planes,
no far one); the terrain grid radius, the frustum and the object cap stop at 75 cells and the fog is full exactly there,
so nothing pops at the edge on the PS2.

## What was changed

* `[video] view_distance`, 1 (original) to 3, default 1, live (`drakan_view_distance.cpp`, game commit `96796b89`). The
  setter hook calls the original and then writes the far distance and the fog record for far and range times the factor,
  so the fog starts and ends that many times further out and the terrain grid, frustum, object cap and particles follow.
  The sound field keeps the original value and the object activity loop runs with the original far distance, so no
  creature, trigger or sound reaches further than on the PS2 (AI, physics and events are not given more range). A live
  change is written at the next terrain view set-up. Cap 3: the terrain grid table has room for about 470 rows.
* Converter: `DRAKAN_WATER_VISIBLE_CELLS` overrides the sea link reach for a test conversion (formats `7122bd7`); the
  default stays 96 because 160 changed nothing visible (below).
* No default was raised (reasons below).

## What the player sees (pictures)

All with the owner's ini, 3840 x 2160 window (not borderless full screen), direct level starts, god mode, and harness
placements to the views (placed, not played; positions in the run logs). Other agents' games were running.

* `ev/islands_setting_vd1_vd2.jpg` (OotF Islands, on Arokh at 70,000 over the island chain, setting 1 against 2 from the
  ini): at 1 the far castle island is a ghost in the fog, at 2 it is drawn with its towers and the west islands are clear.
  At 2 the far sea shows sky-coloured gaps: the PC sea ends there. `ev/islands_w160.jpg`: linking the sea from 160 cells
  instead of 96 leaves the same gaps (they are where the PC has no water, as the water round 5 note found at the lips).
* `ev/ravenshold_setting_vd1_vd2.jpg` (Drakan 2 Ravenshold, on Arokh 20,000 over the start): at 2 the valley's mountains,
  the tower and the harbour stand out of the fog, but north and west the level ends in a flat dark sea plane with a straight
  edge against the sky. `ev/ravenshold_vd125_150.jpg`: the edge already shows at 1.25 (live poke, see below).
* `ev/ia1_sheet.jpg`, `ev/ia1_vd2.jpg`, `ev/dr1_sheet.jpg`: the first look, made by writing the same fields from the harness
  (live poke) at 1, 2 and 3, which gave the same pictures as the setting.
* Wartok Canyons (`ev/wa1_sheet.jpg`, `ev/wv1_w2.jpg`): from the canyon floor and from the air inside the canyon the walls
  close the view well inside 75 cells; the setting changes nothing visible there.
* Surdana on foot (`runs/ds1`): the view is closed by the village and hills; no visible change. With the setting at 2
  from the ini, Rynn walked 21,000 units with real input from the start (into a rock slope; `ev/surdana_vd2_walk.jpg`),
  the game ran normally and quit clean; no guard was reached in that run.

## Frame time

`scripts/perf/scene_ab_bench.py` (its shared turn lock), the owner's ini from the play folder, the setting cycled
live inside each run (order rotated per repeat), 3 repeats, 8 s windows. The machine was not quiet: one other game
instance was running at the start of 8 of the 9 runs (2 during one Islands run). World frame time in ms, per pair:

| Scene | 1 | 2 | Pairs (1 / 2) | Verdict |
| --- | --- | --- | --- | --- |
| Ravenshold start, on Arokh (`view_distance`) | 7.86 median | 7.62 median | 4.83 / 6.49, 7.88 / 8.27, 7.86 / 7.62 | not established |
| OotF Islands, on Arokh at 70,000 over the islands (`view_distance`) | 14.65 median | 15.50 median | 11.12 / 14.42, 30.44 / 15.50 (noise), 14.65 / 17.01 | 2 slower in 2 of 3 pairs (16 to 30 %), not established |
| Surdana start (`terrain_distance`) | 11.32 median | 11.17 median | 11.32 / 12.31, 13.95 / 11.17, 9.21 / 9.95 | 2 slower in 2 of 3 pairs (7 to 9 %), not established |

Raw runs: `bench/raven-vd`, `bench/islands-vd`, `bench/surdana-td` (summaries in the `.out` files).

## Defaults and why

* `view_distance` stays 1: in Drakan 2 the fog hides where the world ends, and moving it out shows a sea plane edge from
  1.25 on (Ravenshold). In Order of the Flame the Islands gain the far islands but show the gaps of the PC sea. A player
  who prefers the far view can set 1.5 to 2.
* `terrain_distance`, `draw_distance` stay 1 and `lod_distance` 4: no new pictures or quiet-machine frame times
  that would carry a change (Surdana `terrain_distance` 2 was slower in 2 of 3 pairs; the 12 November notes measured twice
  the terrain passes at 2 and 42 % more VU1 work for `draw_distance` 1.5, and converted levels already draw 1 to 2.4
  terrain passes per cell). They stay player choices.

## Not verified

* PC reference pictures (the PC game was not run).
* Gameplay at 2 and 3 beyond the code reading: the activity loop and the sound distance keep the PS2 numbers, but no
  creature count or trigger was compared between the factors; particle emitters (snow, effects) run further out.
* The terrain grid table bound (about 470 rows) is read from the ELF's address map, not from a write watch; factor 3 ran
  (live poke in Islands, Wartok, Ravenshold, Surdana) without a crash.
* The debug menu's fog commands (`0x1d4xxx`) write the fields directly and are not scaled.
* No Drakan 2 guard or creature fight was played with the setting on (Surdana walk and Ravenshold flight only).
* Frame times on a quiet machine (see the table: one other game in nearly every run).
* Levels other than those named; WebGPU (the option is renderer-independent but was only run on Direct3D 11).
