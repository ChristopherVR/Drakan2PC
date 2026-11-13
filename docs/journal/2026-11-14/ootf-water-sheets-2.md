# OotF water, round 5: the remaining sheet cells, and swimming in the levels not entered before (2026-11-14)

Brief: fix the 49 water hole cells that still stood in open air after round 3 ([water sheets](ootf-water-sheets.md)) with a
general rule that does not bring back the water lost near the camera, then swim in the Islands, Islands Night, Wartok
Canyons, Rift World and Volcanic. Scratch, tools and every run: `build/review/ootf-water2/` (private). Code looked at:
`editor/packages/formats/src/convert.ts` (`waterHoleHeights`, `cropWaterLayer`). Nothing from either game is quoted here.

**Result: no water converter change.** The remaining cells are water faces at real edges of the PC water, and neither
the per-triangle hole bits nor any hole vertex height hides them better than the round 3 rule. The play found no place
where Rynn cannot swim and no missing surface caused by the water data. It did find a freeze in the Islands that is not
water: a terrain heap overflow, the one play check B fixed in formats `e671be8`; my runs confirm that fix (below).

## How it was played

* Play folder: a copy of `build/review/baseline-play` (the owner's `drakan.ini`: DLSS quality, HD textures, 1280 x 960)
  with the 14 November `dist` exe and DLLs and a private ELF copy. Packs: `packs/before` = the `dist` pack (formats
  `d937581`); `packs/chunk8` = a full campaign conversion at formats `e671be8` (play check B's chunk fix; no water change);
  experiment packs under `exp/`.
* Driver `tools/wdrive.py` (round 3's, live input through `DRAKAN_INPUT_LIVE`, position and animation every 0.1 s). God
  mode, audio off, `DRAKAN_PLAYTEST_HOLD=60`. Islands, Wartok and Volcanic start on Arokh: `land2` **places** Arokh low over
  ground, holds W until terrain stops him, then holds Cross until Rynn is down (the method of `ootf_volcanic_route.py`).
  Spots: Islands (309000, 53800, 503000) facing west, Wartok in the cave at (212256, 22800, 254976), Volcanic (117600,
  58000, 200000). Then Rynn is **placed** about 1,300 above a water spot and **played** with keys: W swim, Ctrl dive, W under
  water, Space surface, mouse look round, W towards the bank. Two Islands runs had no placement after the landing: Rynn
  walked from the landing spot to the sea with W and swam there.
* Harness notes: a placed Arokh stays pinned with the default hold (240 boundaries, which are not counted while mounted),
  hence `DRAKAN_PLAYTEST_HOLD=60`; the natural Islands landing ledge (389638, 62313, 778893) leaves Rynn in a climbing state
  where placement does not hold; Islands Night starts with Rynn swimming, and placing her then gives garbage heights (round 3's
  note), so the Night runs are less clean.

## The remaining sheet cells

`tools/air.ts` (round 3's `holevis.ts` metric per cell: sampled area of drawn water hole cells with no floor at or above
them) on the `dist` pack: 49.5 cells, of which Islands 25.9, Islands Night 7.6, Rift World 5.4, Intro 5.0. Only 78 hole
vertices in the whole campaign take round 3's "drop" branch (24,000 under the water): Intro 34, Islands 32, Alwarren 7,
Islands Night 5; Wartok, Ruined Village and the rest have none. What the cells are:

| Where | What | Seen in game |
| --- | --- | --- |
| Islands south lip (380928..411648 / 768000..778240, layers 185 to 188), 13 cells | the sea ends where a waterfall falls 30,000 to a river; the floor under the cells is 32,000 down | the cells hang behind the PC waterfall model. A test pack with these 26 vertices at the water height (flat) and one with them on the floor below gave the same pictures from below, from the side and from above (`runs/cmp-edge.png`, `crop` pairs); the saturated blue triangles at the lip are there in all three, so they are not these cells |
| Islands west (241664, 456704..471040, layer 236 / Night 50), 3 cells | the sea ends against a rock wall model; the sea bed (45,000) runs on under the land beyond | no sheet in view from the sea or from the land (`runs/is-west-before`) |
| Islands L137 (315392..329728, 724992) | the sea ends in the air over a lower slab of ground 5,000 down | from above the hole cells show as a saturated blue strip along the edge (a steep cell); Rynn swims to the edge and drops onto the slab (`is-l137-b`, `is-l137-a`). The PC water simply ends there too |
| Rift World pool (632832..645120 / 538624..544768) | a 12-cell pool 18,000 above the floor, PC vertex kind 0 | Rynn falls through it to the floor (kind 0 does not make her swim, as on the disc's non-water kinds); the pool draws as a dark plane, its hole cells keep the PC's lower heights (`rw-pool-b`) |
| bank cells everywhere else, about 10 cells in thin slivers | the floor between two vertices dips under the straight water cell | a rule that clamps a bank vertex to the lowest floor sampled round it gets 49.5 to 44.8 but moves vertices up to 2,048 under the water, which round 3 tied to water lost near the camera; not worth it |

**Per-triangle hole bits do not help water.** The water pass (`FUN_00161a78`, `FUN_0015ff38`, `FUN_001626a8`) tests a cell's
flags in one place only, the search for the first cell with a texture (skip when one of 0x10 / 0x40 and one of 0x20 / 0x80
is set); the drawing loop over the grid has no cell test, and its vertex test is 0x100 and the vertex kind. That matches
round 3's in-game test (0xc0 cells raised into the sky still drew). So splitting a shore cell with 0x40 / 0x80 cannot hide a
water triangle. The per-vertex code computes the usual clip codes (near plane at 32, four frustum sides), which is where the
near-camera loss of round 3 most likely comes from (strips with a far vertex dropped), not confirmed.

## Played

`before` = `dist` pack, `after` = `packs/chunk8` (formats `e671be8`, water identical: `air.ts` gives the same counts and the
same hole heights).

| Level, spot | Played | Before | After |
| --- | --- | --- | --- |
| Islands sea by the east shore (323634, 513118) | placed in the sea, swim, dive, surface, W | game froze within seconds of the placement in every run: 3 of 3 on `dist`, 1 of 1 each on round 3's before and after packs, and placed at three nearby spots, one of them in the air over the sea (`is-sea-b`, `cr-*`) | swims, dives, surfaces, no freeze (`is-sea-a`) |
| Islands east shore, no placement | landed, walked W 18 s down to the sea, swim, dive, surface | froze while walking (`cr-walk`; also standing on land at (300000, 501000), `cr-land300`); `runs/freeze-before.png` | walked into the sea and swam, 2 of 2 (`cr-walk-a1`, `-a2`; `runs/freeze-after-walk-swim.png`) |
| Islands west edge (238500, 462000) | placed, swim east, dive, surface | swims to the edge, dives under it and climbs out on the land beyond at 45,500 (`is-west-b`) | the same (`is-west-a`) |
| Islands L137 edge (322000, 719000) | placed, swim south to the edge, look round | swims, dives; past the edge she drops onto the slab | the same |
| Islands south lip (400000, 762000) | placed, swim to the lip | swims, dives, goes over the lip and lands swimming in the river below at 19,800 | the same |
| Wartok cave pool (224256, 254976) | landed in the cave, placed, swim, dive, surface, look round, swim | works, surface drawn round her (`runs/sheet-wc-pool-b.png`) | works (`wc-pool-a`) |
| Wartok river 90 (340000, 326656), river 143 (234496, 201728), river 382 (328704, 205824) | the same | swim, dive, surface; 382 is 1,000 deep, she wades and climbs out | not rerun |
| Volcanic layer 20 (320512, 508928), layer 757 (293888, 478208) | landed, placed, swim, dive, surface, look round | works, surface drawn (`runs/sheet-vo-b.png`) | not rerun |
| Rift World pool | landed, placed over it | falls through to the floor (kind 0, see above) | not rerun |
| Islands Night sea, west edge | placed (after a garbage-height start) | swims; west edge as in the Islands | not rerun |

## The Islands freeze is terrain, not water

On foot near (300000..330000 / 500000..520000) the game froze within seconds in every run on the `dist` pack, mounted on
Arokh it did not: `[memset] bad size ... ra=0x157d7c` (the page builder `FUN_00157a08`), then thread 1 loops in
`FUN_001483e8` over a count read from garbage. Experiments on the Islands LVL alone: water lists cut to 16 cells: still froze;
one layer (246) given one texture: still froze; every floor and ceiling with one texture: no freeze; every layer cut to 8
textures: no freeze; to 48: no freeze; to 64: froze. That is the same fault play check B found in Wartok and on the load into
the Islands ([check B](ootf-check-b.md)): converted layers carried hundreds of chunks (Islands up to 686, disc at most 25),
each texture of each chunk a draw page. Formats `e671be8` keeps the disc chunk grid (Islands now at most 14 chunks a layer)
and removes the freeze in all 7 runs above. A per-layer cap of 48 textures (patch kept as `layer-cap48.patch` in the scratch
folder) also avoided it, but changes more painting and is not needed with `e671be8`; it was not landed.

## For other reports

* Play check B's water notes: at Wartok (190464, 307200) there is no water layer, PC or converted (floors 15000 / 16128 /
  19462 / 23804 under ceilings, a cave); the lilac plane with rings in `wc-chunk/hE_t4..7` is Rynn placed into that cave,
  below the drawn terrain, with the clear colour round her. The Succubus Lair point (168000, 167900) has no water layer
  either (floors and ceilings 200000..220186). The Wartok start river (`wc1/s_t1.png`) is a real water layer drawn with the
  Surdana water material; it reads as flat and pale in daylight. Whether that is the material's normal look or the
  reflection path was not decided here; geometry and swimming there are fine elsewhere in Wartok.
* Rift World pool: Rynn falls through it (PC vertex kind 0). Whether the PC lets her swim there was not checked.

## Not verified

* Islands, Islands Night and Wartok swimming started from placements, except the two Islands walks into the sea.
* The Intro (cut scene) and Alwarren's 7 drop vertices were not looked at in game this round.
* Islands Night runs started with a placement while Rynn swam (garbage heights) and are weaker evidence.
* The near-camera water loss mechanism (clip codes per strip) is inferred from the code, not proven.
* Audio off; frame rates in the shots were taken with other agents' games running and are not measurements.
