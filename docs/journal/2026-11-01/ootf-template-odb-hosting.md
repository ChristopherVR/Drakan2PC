# Doors, gates, switches, ladders, ropes, flames: models hosted in the template ODB (2026-11-01)

Problem (ootf-props-audit.md item 5): 406 PC objects of engine ids 19 / 13 / 15 / 73 / 43 / 71 / 97 were dropped because the PS2
has no template with a model in RESOURES.ODB. Their templates live in LEVEL1ON / WORLDCON / MOUNTAON / FX. The old `crossOdb` route
cloned them there but named the converted RESOURES model through that ODB's `.DB` index (db 4 ...) and hung the level load.

## What changed

* `OotfPs2Context.hostFiles` (`ootfpipeline.ts`): own MOD + TXD of those template ODBs. The converted static model of a class cloned
  from such an ODB is converted by a **second `OotfAssetBuilder` per template ODB** (ids past that MOD's max model id / that TXD's max
  texture id, textures in that TXD) and appended to that ODB's own MOD / TXD (`files/<dir>/<STEM>.MOD|.TXD`, 0x0202 max id raised
  like the RESOURES host). The class record names it with `db 0`, as disc classes of that ODB do. Planning runs twice: the first pass
  collects the (class, ODB) requests, the models are converted, the second pass writes the real ids. `crossOdb` is now on by default
  when `hostFiles` is given (`OOTF_XODB=0` turns it off, `OOTF_XODB_ONLY=<stems>` restricts it). Skinned models stay on the host route.
* Models in these builders always get the strip display list 0x0211 (`displayList: /./`).
* `editor/scripts/ootf-convert.ts` reads `<ps2>/../hosts/<STEM>.MOD|TXD` (`--hosts` overrides); `scripts/harness/ootf_level_run.py`
  extracts them from the ISO (LEVEL1ON, WORLDCON, FX, MOUNTAON) into `build/odbx/hosts/`.
* `terrainGroundBelow` (`terrain.ts`): Rynn's start height = highest floor layer not above the PC start (+600) instead of the highest
  floor of all layers (`convert.ts`).

## Findings

* **Why the models did not show:** with the host-in-template route the objects spawn (world dump `DRAKAN_WORLD_DUMP_FRAME`: switch
  class 0x1000d with model ref 0x8062d, doors class 0x10013 with 0x8062f / 0xb060e: db part = the absolute container index of
  LEVEL1ON / WORLDCON, id 1581 / 1583 / 1550) but **draw nothing without the 0x0211 display list**; with it the doors draw. RESOURCES
  props draw without one (disc models of RESOURCES that lack 0x0211: 145 of 778) but the template ODB classes do not, and the disc
  has 0x0211 for 261 of 265 LEVEL1ON models. This is the part of the earlier "static props draw without it" that does not hold here.
* No hang with the new route (Ruined Village full pack with LEVEL1ON + WORLDCON + FX hosts; Alwarren converts with all four hosts incl.
  MOUNTAON for ladders / ropes). The cause of the old hang was not isolated: it is not an MOD / TXD / ADB header count (those containers
  have no class / group counts; ModPack only needs the ODB 0x0100 counts, already handled), the remaining difference is the model ref
  naming a RESOURES model with a non-zero `db` from a template ODB. Hosting in the ODB's own containers avoids it.
* Converted pack: Ruined Village 7 of 12 formerly unsupported classes are now cloned (doors, locked door, switch, flame); Alwarren
  Small Gate, IronGate, switch, House Ladder, Alwarren Magic Flame ...
* terrainHeightAt at (272000, 237500) in Ruined Village: only one layer (182, type 0, h 18990) covers the point, so this is **not** a
  layer-choice bug there; the PC start at (220424, 176605) gives 18750 old and new. `terrainGroundBelow` still fixes the real
  multi-layer case (a roof layer over the start point wins `terrainHeightAt`).

## In game (PAL, Ruined Village, `DRAKAN_DBG_NOOBJ` + `DRAKAN_DBG_PULL`, game lock)

| Run | Content | Result |
| --- | --- | --- |
| dd2 / dd4 (no 0x0211) | Door, Inner Door R, Switch pulled next to Rynn | level loads, world dump shows the 3 objects with the right model refs, **nothing drawn** |
| dd6 (`OOTF_DL_ALL=1`) | same | level loads, **doors and switch draw** (wooden doors from WORLDCON / LEVEL1ON, screens `build/ootf-run/dd6/run/c*.png`, brightened) |
| dd7 | same + "Burning Flame (Wide)" (clone of WORLDCON "Part: BaseFire [15]", rfl 43) | **level load hangs** (`beforeLevelLoad` without `afterLevelLoad`, killed by PID) |

So doors / switches (rfl 19 / 13) work; the hang returns with a cloned rfl 43 particle class in a template ODB. Particle classes
(43, 71, 97) are therefore **not** cloned into template ODBs by default (`CROSS_FX_RFL`; `OOTF_XODB_FX=1` opts in). Ladders and ropes
(MOUNTAON, rfl 15 / 73) convert (Alwarren: House Ladder) but were **not** run in game; opening a door through a trigger was not tested
(lock contention with other runs; the dd8 flame-only run never got the lock). Next: flame-only run to confirm the rfl 43 culprit,
a ladder / rope run, and a door trigger run (pull "Door Trigger On" next to a door).
