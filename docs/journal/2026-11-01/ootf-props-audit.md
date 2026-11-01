# OotF props in the converted levels: audit, causes, fixes (2026-11-01)

Question: some props are missing in the converted Drakan 1 levels. Method: `convertOotfLevels` was run per level (one level per
pack, as in the in-game runs, template LEVEL1NA, no `maxNewClasses`, skinned and retarget on) and the new `classAudit` rows
(`OotfPipelineResult.classAudit`, `ClassAuditRow` in `ootfpipeline.ts`) say what became of every PC class and how many objects it
carries. Probes live in `build/ootf-props/` (uncommitted): `audit.ts` (JSON per level), `report.ts` (tables and the CSV).
All 23 PC levels (13 campaign + 10 multiplayer) are covered. "Before" = commit 6ef175a, "after" = this change.
Full per class rows: `ootf-props-audit.csv` (level, class, engine class, kind, objects, outcome, category, PS2 class, template, reason, model).

## What happens to a PC object

1. Class has a same-named PS2 class of the same engine id -> `name-mapped`: the object keeps position / rotation / scale and shows
   **the PS2 model** of that class (not the PC model).
2. Else the class is **cloned** from a PS2 class of the same engine id (`planClassMapping`): logic / light / sound / waypoint / spawn
   keep the PS2 template's model block; scenery / mechanism / fx / item / creature classes with a PC model get the **converted PC
   model** (`modelFor`), cloned only from templates in RESOURES.ODB (the ODB whose MOD / TXD receive the converted models).
   Engine ids missing on PS2 use a stand-in (`PS2_SUBSTITUTES`: Goblin 88 -> Orc 49, Succubus / Queen -> flier 30, Magic Crystal 91 -> 171,
   Quickening 80 -> 71, locks 123 / 137, Pirate Ship 130 -> 35; new: MP power-up 120 -> potion 10).
3. Otherwise **dropped** by `convertPcLevel` ("unmapped class"): reasons `no-engine-class` (no PS2 class of that engine id in the
   template's ODBs), `needs-model` (no model to take / model conversion failed), `no-raw-record` (no template with a model block in
   RESOURES.ODB), `low-coverage` (< 50 % of the fields exist on the PS2 class), `no-free-id`.
4. Not drops: bounds / terrain-height placement (only Rynn is moved, to terrain + 400), `pruneSkinned` (only affects classes already
   chosen for `maxNewClasses`, which the full runs do not use), `rfl class id missing on PS2` (same as `no-engine-class`), resource
   references zeroed (257 in Ruined Village: sounds and animations of waypoints / ambient sounds; **no object model parameter is
   ever zeroed**, 0 model-typed parameters in use). `objectsIn - sum(class objects)` (objects whose class record is missing) is 0 in all 23 levels.

## Result per level (objects)

Columns after "kept after": where the kept objects went / why the dropped ones were dropped (after this change).

| level | PC objects | kept before | kept after | same-name PS2 class | new class + PC model | new class, no model | stand-in | no PS2 engine class | no model, no template | model not converted | no model-bearing template | low field coverage | other | missing class record |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Alwarren | 1820 | 1594 | 1621 | 579 | 902 | 115 | 25 | 0 | 46 | 5 | 129 | 19 | 0 | 0 |
| Alwarren of Death | 72 | 65 | 65 | 48 | 17 | 0 | 0 | 6 | 0 | 0 | 1 | 0 | 0 | 0 |
| Arena of Death | 22 | 16 | 16 | 9 | 7 | 0 | 0 | 5 | 0 | 0 | 1 | 0 | 0 | 0 |
| grotto | 528 | 512 | 515 | 214 | 284 | 16 | 0 | 1 | 0 | 0 | 6 | 7 | 0 | 0 |
| Grotto of Death | 59 | 41 | 41 | 25 | 16 | 0 | 0 | 17 | 0 | 0 | 1 | 0 | 0 | 0 |
| Hill of Fire | 65 | 31 | 52 | 12 | 19 | 0 | 21 | 12 | 0 | 0 | 1 | 0 | 0 | 0 |
| Intro | 102 | 101 | 101 | 11 | 82 | 8 | 0 | 0 | 0 | 0 | 1 | 0 | 0 | 0 |
| islands | 2360 | 2253 | 2270 | 625 | 1487 | 81 | 76 | 7 | 11 | 0 | 59 | 14 | 0 | 0 |
| Islands Night | 700 | 688 | 688 | 192 | 451 | 11 | 34 | 0 | 0 | 0 | 6 | 6 | 0 | 0 |
| Island of Death | 152 | 110 | 143 | 41 | 69 | 0 | 33 | 8 | 0 | 0 | 1 | 0 | 0 | 0 |
| mines | 1567 | 1498 | 1501 | 486 | 899 | 114 | 2 | 0 | 0 | 0 | 53 | 13 | 0 | 0 |
| Nemesis | 82 | 72 | 72 | 51 | 21 | 0 | 0 | 9 | 0 | 0 | 1 | 0 | 0 | 0 |
| Relic | 152 | 105 | 125 | 3 | 86 | 16 | 20 | 18 | 0 | 0 | 9 | 0 | 0 | 0 |
| RiftWorld | 149 | 144 | 144 | 39 | 77 | 28 | 0 | 0 | 0 | 0 | 5 | 0 | 0 | 0 |
| rift crystal | 1572 | 1371 | 1477 | 293 | 961 | 222 | 1 | 31 | 20 | 0 | 38 | 6 | 0 | 0 |
| Rise to Power | 115 | 63 | 103 | 30 | 33 | 0 | 40 | 11 | 0 | 0 | 1 | 0 | 0 | 0 |
| Ruined Village | 1858 | 1715 | 1790 | 540 | 1096 | 148 | 1 | 3 | 13 | 0 | 46 | 11 | 0 | 0 |
| Ruined Village Book | 17 | 17 | 17 | 3 | 11 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| Sanctuary | 54 | 24 | 36 | 4 | 19 | 1 | 12 | 15 | 0 | 0 | 2 | 1 | 0 | 0 |
| Succubus Lair | 1011 | 1000 | 1004 | 249 | 645 | 92 | 13 | 0 | 0 | 0 | 2 | 10 | 0 | 0 |
| volcano | 1679 | 1646 | 1651 | 672 | 795 | 182 | 2 | 0 | 0 | 0 | 18 | 10 | 0 | 0 |
| Volcano of Death | 170 | 137 | 162 | 105 | 32 | 0 | 25 | 7 | 0 | 0 | 1 | 0 | 0 | 0 |
| wartok canyons | 2006 | 1940 | 1940 | 740 | 1072 | 119 | 1 | 1 | 18 | 3 | 24 | 28 | 0 | 0 |
| **total** | 16312 | 15143 | 15534 | 4971 | 9081 | 1156 | 306 | 151 | 108 | 8 | 406 | 125 | 0 | 0 |

Totals: 16312 PC objects, **15143 kept before, 15534 after (95.2 %)**; 778 still dropped. `kept before/after` counts objects written
to the PS2 LVL (singletons Rynn / camera / Level Properties / sky added by the converter are in "kept" but not in the class columns,
so a level can show 3-5 more kept objects than the class columns add up to). "No PS2 engine class" are multiplayer spawn pads and
power-ups of the PC-only engine ids 18, 120 (before the stand-in), 126-128, 86 and Pressure Plate 29.

## Dropped objects by class name (after)

| class | engine class | objects | levels | why |
|---|---:|---:|---:|---|
| Water Droplet | 144 | 108 | 5 | drop-needsmodel |
| Alwarren Magic Flame | 43 | 93 | 1 | drop-notemplate |
| Rynn Launch Pad | 18 | 77 | 8 | drop-noengine |
| portcullis | 19 | 47 | 3 | drop-notemplate |
| Kill Box | 43 | 34 | 7 | drop-notemplate |
| Plate1 | 29 | 29 | 1 | drop-noengine |
| Burning Flame (Wide) | 43 | 28 | 3 | drop-notemplate |
| DragonLaunch Pad | 127 | 22 | 4 | drop-noengine |
| switch | 13 | 18 | 3 | drop-notemplate |
| Switch | 13 | 15 | 4 | drop-notemplate |
| Map Trigger | 104 | 14 | 3 | drop-lowcov |
| Map Object | 104 | 13 | 1 | drop-lowcov |
| Sky | 26 | 10 | 10 | drop-notemplate |
| Sky Trigger | 143 | 10 | 1 | drop-lowcov |
| Small Gate | 19 | 8 | 1 | drop-notemplate |
| SpiderCaveMusic | 129 | 8 | 1 | drop-lowcov |
| Kill Player Detector | 43 | 7 | 1 | drop-notemplate |
| Portcullis | 19 | 7 | 3 | drop-notemplate |
| Door | 19 | 7 | 3 | drop-notemplate |
| Flame | 43 | 7 | 1 | drop-notemplate |
| death trigger | 43 | 7 | 1 | drop-notemplate |
| Fog + Sky Changer | 143 | 7 | 2 | drop-lowcov |
| Gravitating particle | 71 | 7 | 1 | drop-notemplate |
| Fog Color Changer | 143 | 7 | 1 | drop-lowcov |
| Fall01 | 17 | 5 | 1 | drop-modelfail |
| Pressure Plate 02 | 29 | 5 | 2 | drop-noengine |
| TallLadder | 15 | 5 | 1 | drop-notemplate |
| Small (Iron) Gate | 19 | 5 | 1 | drop-notemplate |
| LairMusic | 129 | 5 | 1 | drop-lowcov |
| map message | 104 | 5 | 1 | drop-lowcov |
| Quickening Effect | 80 | 4 | 4 | drop-lowcov |
| Sky + Fog Changer | 143 | 4 | 1 | drop-lowcov |
| Pressure Plate 01 | 29 | 4 | 2 | drop-noengine |
| Pulley Crank | 13 | 4 | 1 | drop-notemplate |
| Release Lever | 13 | 4 | 1 | drop-notemplate |
| Dr. Evil | 92 | 4 | 1 | drop-notemplate |
| TempleMusic | 129 | 4 | 1 | drop-lowcov |
| Succ Switch | 13 | 4 | 1 | drop-notemplate |
| Dragon Fly Object | 17 | 4 | 1 | drop-lowcov |
| MP : Dragon Invunrability | 126 | 3 | 3 | drop-noengine |
| Snow Object | 97 | 3 | 3 | drop-notemplate |
| Shaft Ladder | 15 | 3 | 1 | drop-notemplate |
| Swing rope | 73 | 3 | 1 | drop-notemplate |
| Brick (Secret) | 13 | 3 | 1 | drop-notemplate |
| Fog Transition | 143 | 3 | 1 | drop-lowcov |
| Pirate Ship | 130 | 3 | 1 | drop-lowcov |
| RedDragon Launch pad | 128 | 3 | 3 | drop-noengine |
| Fire Dragon Soul Crystal | 86 | 3 | 3 | drop-noengine |
| Sky Interior | 26 | 3 | 3 | drop-notemplate |
| Storm Box  (box) | 26 | 3 | 3 | drop-notemplate |
| IronGate | 19 | 2 | 1 | drop-notemplate |
| House Ladder | 15 | 2 | 1 | drop-notemplate |
| Dragon Detector | 17 | 2 | 2 | drop-lowcov |
| Tropical Sky | 26 | 2 | 2 | drop-notemplate |
| Night1 | 26 | 2 | 2 | drop-notemplate |
| Pressure Plate | 29 | 2 | 2 | drop-noengine |
| Ladder | 15 | 2 | 2 | drop-notemplate |
| Dragon Object | 17 | 2 | 1 | drop-lowcov |
| Candle03 (Lever) | 13 | 2 | 2 | drop-notemplate |

## Findings (why props are missing or wrong when they are kept)

Drops are only 5 % of the objects, so most "missing props" are objects that **are** placed but do not look like the PC level. Causes
found and what was done:

1. **Class record caches the model's bounds (fixed).** Body bytes 16..80 of a class record equal bytes 8..72 of the model's 0x0207
   record on 813 of 813 RESOURES classes with a model (bounding sphere + box). A clone kept the **template's** bounds (a spear, a
   barrel ...) while pointing at the converted model (a house, a tree), so culling and hit tests used a sphere of the wrong size at the
   wrong centre; only the skinned path patched it. `OotfAssetBuilder.modelBounds` now returns the converted model's 0x0207 (or the
   skeleton's bounds) and `planClassMapping` writes it for every cloned model (`PlanOptions.modelBounds`, `patchRecordModel`).
2. **Double sided polygons lost (fixed).** PS2 polygon flags carry the same bits as the PC ones (bit 1 = double sided: 29966 disc
   polygons; the disc also keeps the PC editor bits 8-15). `pcToNeutral` set bit 1 only for polygons with an alpha texture, dropping
   it on 17000+ PC polygons (fences, banners, thin walls, cards, open doors), which then vanish when seen from behind. Bit 1 of the PC
   flags is now kept (bits 8-15 deliberately not copied: meaning unknown).
3. **Model-less mechanism / scenery / fx classes (fixed).** Break-away floors (189 objects in 6 levels), Fire Fly Axis movers, Dragon Fly
   objects were `needs-model` drops although the PC class has no model either; they are now cloned from model-less PS2 templates of the
   same engine id (`modelless` in `planClassMapping`). 
4. **MP power-ups (stand-in).** Engine id 120 (110 + 41 objects in 6 maps) is cloned from the potion class 10 with the PC model;
   coverage floor 0 for stand-ins of kind `mp` (the clone is a visible pickup, not the PC behaviour).
5. **Doors, gates, switches, ladders, ropes, flames (open, 406 objects).** The PS2 has no template **with a model in RESOURES.ODB** for
   engine ids 19, 13, 15, 73, 43, 71, 97 (their templates live in LEVEL1ON / WORLDCON / MOUNTAON / FX / NPCS). Cloning them there with
   the converted model named through that ODB's `db` index of RESOURCES (`crossOdb`, `OOTF_XODB=1`) was implemented and tested: **the
   level load hangs** in game (Ruined Village, `beforeLevelLoad` without `afterLevelLoad`, main thread spinning at 0x3b5d34; also with
   only WORLDCON / LEVEL1ON / MOUNTAON enabled; the run without it loads). So it stays off. Next step (not done): host those models in the
   template ODB's own MOD / TXD instead (`db 0`), as is done for RESOURES. A stand-in as engine id 35 (Mover) was rejected: a solid
   door that never opens would trap the player where a missing door is merely open.
6. **Not props / not fixable here:** Water Droplet 144 (108 objects, decorative drips; PS2 class 144 is a shield, so no clone), Pressure
   Plate 29 (51; the only PS2 plate is in LEVEL4ON / LEVEL8OS, not reachable from LEVEL1NA), MP launch pads 18 / 127 / 128 / 126 / 86
   (no PS2 engine class), Map Object / Trigger 104, Sky / Fog changers 143, music triggers 129 (< 50 % shared fields).

Checked and **not** a cause: textures (0 polygons dropped for missing textures in Ruined Village; 8 PC models over all 23 levels do not exist:
`Fall01` ...), missing 0x0211 display list (ootf-displaylist.md: static props draw without it; `OOTF_DL_ALL=1` / `displayListAll`
generates it for every static model, no generation failure in Ruined Village, kept off), model parameters zeroed (none), animated /
multi-part props (parts are separate objects linked by id, all kept), billboards / particles (fx classes, see 5.), terrain height
placement (objects keep their PC position; only Rynn is lowered), culling by the old class bounds (that is 1.).

## Open items

* Host door / switch / ladder / rope / flame models in the template ODB's own MOD + TXD (needs extracting those containers in
  `scripts/harness/ootf_level_run.py` and one `OotfAssetBuilder` per host).
* Level control after load is flaky in full-object converted levels (screen 14 / 29 toggling, `autostart` missing, several runs of this
  session): not caused by these changes (the run before them showed it too).
* Test aids added: `DRAKAN_DBG_AT=<x>,<y>,<z>` (Rynn position), `OOTF_NO_BOUNDS=1`, `OOTF_XODB=1` / `OOTF_XODB_ONLY=<stems>`,
  `OOTF_DL_ALL=1`.

## In game (Ruined Village, PAL, game lock held)

* Builds with the fixes (bounds, double sided, model-less clones) load and reach control (`ra1`, `ra2`: screenshots at the start
  point turning 3 x; fence / log props near Rynn are drawn). The level is lit at the night default, so most props are nearly black and
  the old/new comparison at the start point shows no visible difference (`ra2` = same build with `OOTF_NO_BOUNDS=1`). The effect of
  the bounds patch (props popping out when their template sphere leaves the view) therefore could **not** be confirmed visually;
  it rests on the 813/813 byte match above.
* `crossOdb` (doors from LEVEL1ON / WORLDCON / FX templates): level load hangs (3 runs: all ODBs; WORLDCON + LEVEL1ON + MOUNTAON;
  without it the same level loads). Off by default.
* Several placement test runs (`DRAKAN_DBG_AT`) ended in a screen 14 / 29 loop without control; the converter's own terrain lookup
  (`terrainHeightAt`) also returned a height far above the ground at (272000, 237500) in Ruined Village (Rynn in the sky), i.e. it picks
  the wrong layer on this multi-layer level.
* Not done: 13-level regression with the new build (another agent owns that), screenshots of every level.
