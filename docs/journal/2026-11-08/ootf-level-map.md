# OotF: the in-game map of converted levels (2026-11-08)

User report: "the map isn't functioning for ootf and no quest markers being shown". Private probes, packs and captures are under
`build/mapwork/` and `build/ootf-run/map-*` (not committed).

## What was wrong

Baseline run of a converted Ruined Village (`build/ootf-run/map-base/run/s_map.png`): the map screen showed **Surdana's picture**
("Location: Ruined Village", "No objectives yet"). How a native level supplies its map (Ghidra, level-load function `002cae70`
and the map section draw `00439b28`):

* the level's Level Properties object has the field `Map Object` (class ref, index 0x5a); the engine keeps it in `DAT_004fbb68`;
* that class (`Map Surdana`, id 977 in LEVEL1ON.ODB; also Kragmor, Catacombs, City of the Damned) has `Map Texture` (a 256 x 256
  texture of the level's TXD), `Upper Left / Lower Right Level Corner X / Z (lu)` (world rectangle, 1 lu = one terrain cell
  = 2048 world units) and `Upper Left / Lower Right Map Corner X / Z (pixels)` (the rectangle of the picture it covers). The
  3D "you are here" arrow is placed through that calibration; quest markers are drawn at their own `Map X / Y Coordinate`
  (picture pixels) and only when the quest class `Type` is 0 (marker) or 2 (both);
* the converter never touched the field, so every converted level kept Surdana's picture and calibration, and the quest markers
  were written as `Type` 1 (journal only) with coordinates `x * 0.4` of PC values that are 0 in every level, i.e. none on the map.

## What the PC game has

`Dragon.rrc` holds the parchments: type 0x40 records, 16 bit 5:6:5, five 256 x 256 tiles per level (ids `base` to `base + 4`), the
text sheets of the mission bitmaps 5 ids later. The tiles join into one 640 x 480 picture: tile 0 / 1 the top row, 2 / 3 the
bottom row (224 rows used), tile 4 the 128 pixel right edges of both rows side by side. Sets: 4368 Wartok Canyons, 4384 Islands
(and Islands Night), 4400 Grotto, 4416 Ruined Village, 4432 Alwarren, 4448 Grimstone Mines, 4464 Rift Crystal, 4496 Succubus
Lair, 4512 Volcano. The set is told by the `Mission Bitmap ID` default of the level's Map Trigger class (text sheet - 5); Alwarren
and Rift Crystal have no such default (0) and are matched by their picture (green village map, crystal fortress map), which is a
visual identification, not a data link. The PC level data has **no calibration**: its Map Trigger objects have `Map X / Y` 0 and
only name mission text *bitmaps* (no strings), so the world rectangle is estimated.

## What the converter does now (`ootfmap.ts`, `OotfPipelineOptions.levelMaps`)

* per campaign level: the parchment is joined, stretched over a 256 x 256 truecolor texture (the native screen shows a 256 x 256
  texture 4:3, which is the PC picture's own proportion), appended to LEVEL1ON.TXD, and a new class `OotF Map <level>` cloned
  from `Map Surdana` carries the texture and the calibration; the level's Level Properties `Map Object` names it;
* calibration: world rectangle = box of the terrain cells that hold ground (outer 0.5 % ignored); picture rectangle = lit part of
  the parchment. This is an estimate (the PC calibration is not in the data); it can be changed per level in the editor (below);
* quest markers: placed from the object's world position through the same calibration, `Type` 2 (map and journal) for quests,
  0 for plain place labels. PC markers whose class is the generic "Map Trigger" / "map message" have no text anywhere in the
  install (their mission text is a bitmap), so they stay out as before; Ruined Village and Grotto have only such markers, so
  their map shows Rynn's arrow but no marker. Named ones (Islands, Volcano, Succubus Lair, Mines, Wartok) are placed.

## Editor

The import dialog has a "Map screen" section (`ootfmaps.ts`, rules in `levelmap.ts`): per ticked level a live preview of the
parchment with Rynn's start (red) and the quest places (blue), a source (the level's own map, another level's map, a picture
of your own), and the world / picture rectangles as numbers. Only changed levels are sent (`ConvertRequest.levelMaps`), through
the same pipeline option, so the pack entries are identical in shape for every source.

## Verified

Captured in the game (M key, first-visit help closed): Ruined Village (`build/ootf-run/map-v1/run/s_map.png`) and Alwarren
(`map-a1`) show their own parchment with Rynn's arrow at the start point, which is where the editor preview puts the red dot.
Grotto, Grimstone Mines and Wartok Canyons start mounted on Arokh and the map does not open there (the PC manual says the same
for the inventory), so no capture. Succubus Lair (`map-s1`) shows its parchment, the objective "Get Arokh" listed under Main Objectives and its marker on the map, but
the marker sits on the left edge: that quest object lies outside the box of the level's ground cells, so its position clamps to
the picture edge (open: widen the estimated world rectangle to include marker objects). **Not verified:** the arrow away from
the start (the calibration is an estimate).
