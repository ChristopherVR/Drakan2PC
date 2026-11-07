# Issue 5: break-away floors with their own pieces, per-object pieces in the editor, Arokh facing on remounts

Follows [model-less classes](ootf-modelless-classes.md) (item 5) and [closed-loop driver](ootf-closed-loop-driver.md)
(section 3). Runs, recipes and probes are private under `build/harness/issue5-floors/` and `build/harness/issue5-arokh/`
(`runs/<run>/`, RAM dumps, screenshots; `ents.py` lists registry entities of a RAM dump, `remount.py` is the remount
mission). Executable: `build/harness/issue5-loop-dist` (the driver build). Pack `pack0`: all 23 levels from formats
`04e1ee3` (built from a tree byte-identical to that commit). Nothing from either game is quoted here.

## 1. Break-away floors keep their PC pieces (formats `04e1ee3`)

**Cause.** A break-away floor (engine class 44) has no class model; its `Model List` names the pieces, each modelled in
place around the object's origin. The converter clones floors from model-less PS2 templates, which live only in LEVEL1ON
("Bridge Breakway Pieces", "Breakaway floor") and MOUNTAON ("Dirty" / "Brocl Break Away Floor"), outside RESOURES.
`mapModelFor` only named RESOURES models, so the field was skipped and every clone kept the template's pieces. With
otherwise equal data, the clones folded together: the old pack had 5 floor records for all PC floor classes, every brick
showing 14 or 17 bridge chunks.

**Hosting route.** The pieces now go into the template ODB's own MOD / TXD, the route doors and switches already use
([template ODB hosting](../2026-11-01/ootf-template-odb-hosting.md)): planning runs once, the floors cloned into a hosted
template ODB are listed (`breakawayPieceHosts`), their pieces are converted by that ODB's own asset builder (strip display
list, one collision box each via `singleShapeBounds`, like the disc's one-shape OBB pieces), and planning runs again with
`mapModelFor` naming them with db 0 there. No RESOURES model is named through a template ODB's `.DB`, the pattern that hung
level loads in [props audit](../2026-11-01/ootf-props-audit.md) point 5. Re-homing the class into RESOURES was not needed
(and RESOURES has no floor template to clone).

**Pack.** 2,196 to 2,248 synthesized classes; 57 floor records instead of 5; LEVEL1ON gets 86 converted models (was 11),
MOUNTAON 11 (was 9). Each floor names its own piece ids (Ruined Village "Break Brick floor" one piece each, "Brick Floor 12
(Break)" ten). Tests: `test/ootfbreakaway.test.ts` (synthetic: floors fold into one class with the template's pieces when
nothing can be named; with converted pieces each gets its own list in the template ODB; a floor with an unconverted piece
keeps the template's; the hosting list). Full formats suite passes (457 tests).

**In game** (direct starts, god mode):

| Run | Level, pack | What | Result |
| --- | --- | --- | --- |
| `fl2` | Ruined Village, `pack0` | Rynn placed on the brick floor (477184, 32600, 223232) | 85 floor entities (38 objects, 47 PC pieces); the bricks under her fall (2 pieces from y 31990 to about 20100 in 400 samples), the rest stay; brick floor drawn (`grid.png`) |
| `fl2-old` | Ruined Village, old pack (`4d21976`) | same | 570 floor entities (38 objects, 14 bridge chunks each); a pile of grey chunks where the floor is |
| `fl3` | Ruined Village, `pack0` | same, then F5, walk, F8 | 3 pieces fall, then fade and leave the registry (85 to 82); quicksave and quickload return to gameplay, Rynn back at the exact saved spot, the fallen pieces stay gone |
| `ld-alw`, `ld-rift`, `ld-mines`, `ld-grotto` | Alwarren, Rift Crystal, Grimstone Mines, Grotto | load, F5, walk, F8 | all load, quickload back to the exact saved spot; floor entities 50, 204 (102 objects, one piece each), 16, 16 |
| `ld-isl`, `ld-isl2` | Islands | same | first try: renderer stop before gameplay ("mip level count 8 is invalid", the known wgpu texture stop of the closed-loop note, section 6); retry: loads (Rynn on Arokh), 34 floor entities (17 objects, one piece each), quickload back to the exact spot |

## 2. Per-object `Model List` in the editor stand-in (commit `58202939`)

`scene/standin.ts`: an object whose own parameters name pieces (`Model List`, or a Morph Plug's `Morphing Models`) is
drawn under a key of its own (`standInResource`: class resource plus the refs). Partitioning, batches, outlines and the
worker request (`ModelRequest.standIn`) follow that key; the class default still serves objects without the parameter.
Edited or pasted objects load their variant on the next document change. Test: `app/test/standin.test.ts` (synthetic
ISO: a floor class with pieces 10 and 11, an object naming piece 12; both models load, the objects split into two
batches). The converted PC levels place no floor with its own list, so this only shows on edited or authored levels.

## 3. Arokh facing for remounts: not an Initial Mode gap

* **Native levels.** `Initial Mode` (Dragon field 145) is set per object on the disc: 1 in 1BS, 1ED, 1NA, 4RA (Northern
  Tundra), 8OS; 0 in 1OR, 3ES, 4LD; 2 in 5NE, 6EN; LEVEL0RO's dragon has none. The class default (System "Dragon") is 1.
* **Converted.** The PC class default is 1 as well. Volcanic's PC dragon object sets no `Initial Mode`, and the converted
  object has none either, so it runs with the PS2 class default 1: the same value as Northern Tundra's explicit 1. Islands
  (0), Grimstone Mines, Grotto, Wartok (2) and Rift World (0) carry their PC values. Nothing to map; the earlier note's
  "Initial Mode 0" for Volcanic was wrong.
* **What makes him turn.** `tr1` (Tundra: walk in from the front, mount, Cross until Rynn is back, then the approach from
  behind): after the dismount he stays put (yaw 2.523), and each of the 5 placements behind him makes him turn toward her;
  she ends 36 degrees off his front and mounts. `vr1` (Volcanic, `vol-sanctum` card, the same sequence) shows the same:
  yaw pi after the dismount, then he turns toward each placement (to -2.37) and she mounts from 35 degrees off his front.
  So converted Volcanic Arokh does turn to face Rynn after a dismount. In the earlier `rm` runs he never turned (yaw pi
  throughout) because they started straight from the card's quickload.

`remount.py` runs (W held, facing steered, mounted = the controlled actor becomes Arokh; "turned" = the driver's placement
loop had to place her again because he had turned toward her; old pack `4d21976`, its `vol-sanctum` card):

| Run | Level | Before the approach | Turned toward her | Remount from behind |
| --- | --- | --- | --- | --- |
| `tr1` | Northern Tundra | mount, dismount | yes (5 placements, ends 36 deg off his front) | mounted |
| `tq1` | Northern Tundra | mount, dismount, F5, F8 | yes | mounted |
| `vr1` to `vr5` | Volcanic | F8 of the card, mount, dismount | yes in all 5 (his yaw from pi to -2.56 .. -0.62) | **mounted 5 of 5** (closest 1,212 to 1,401 units) |
| `vq1` | Volcanic | F8, mount, dismount, F5, F8 | yes | mounted |
| `rm2` to `rm7` (earlier) | Volcanic | F8 of the card only | no (yaw pi throughout, 1 placement) | 2 of 5 |

So the facing is the dragon's own behaviour once Rynn has dismounted him in the session, on both games, and it survives a
quickload. Only the `vol-sanctum` card's dragon (restored from that card, no mount in between) never turned; why that
saved state lacks it was not followed further (its save came from a long scripted route on an older executable). No
converter change: the value the brief asked about is already the PS2 default, and a remount from behind works 5 of 5 as
soon as he has carried her once.

## Still open

* The renderer's texture validation stop ("mip level count 8 is invalid") hit the first Islands direct start (`ld-isl`)
  before gameplay; the retry (`ld-isl2`) loads, quickloads back to the exact spot and has no such line, so it is the
  known renderer issue, not the pack.
* Why the `vol-sanctum` card's restored Arokh does not turn (a saved-state question, not the converter).
* Alwarren shows 50 floor entities for 26 placed floor objects with one piece each (52 expected); not checked which.
