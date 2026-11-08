# Spider-like OotF creatures that melee seems unable to hit (2026-11-09)

Report: the spider-like creatures of Wartok Canyons (50 `Spider`, 13 `Scavenger`) seem impossible to hit with melee. Fix: the
Scavenger now plays as the disc Scavenger by default (formats `a06ddb0`), see the end. Probes, packs and runs are private under
`build/spider/` (packs `pack*`, sweeps `sweep*.py`, runs `ld/`); nothing from either game is quoted.

## Method

Packs pull creatures next to Rynn with the converter's test aids (`DRAKAN_DBG_NOOBJ`, `DRAKAN_DBG_PULL`, `DRAKAN_DBG_PULLIDS`,
`DRAKAN_DBG_PULLSET="Initial Health=200"`), plus the level's Long Sword (damage 15). A setup recipe picks it up and equips it, then
`scripts/harness/live_drive.py` (a private mission on top of it) places Rynn on a bearing and distance from the creature, faces it,
taps the attack 10 times and reads the creature's hit points from RAM. God mode, private game copy `build/ootf-run/play-spider`.
A weapon is needed: unarmed Rynn never dealt damage to anything.

## Results (hit points lost per 10 swings, 12 cells = 4 bearings x 300 / 450 / 600 units)

| Creature | Class | Cells with damage | Per hit |
| --- | --- | --- | --- |
| Leather Wartok (control) | disc `LeatherWartock : Center` | 9 of 12 | 5 (some 1, 6, 7) |
| Spider | disc `Spider` (collision data identical to the OotF model: same 18 spheres) | 11 of 12 | 1 to 2 of its 15 |
| Scavenger, disc class and model (`--ps2-model-map` row on) | disc `Scavenger` | 11 of 12 | about 4 |
| Scavenger as shipped | cloned from `ScavengerTest`, converted PC model and sphere list | 0 of 12 (120 swings, 384 more in a closed-loop run at 371 units) | none |

* Spiders are hit, but for 1 to 2 hit points against a 15 point body, with a sword whose Damage line reads 15 (Wartoks lose 5 with
  50 percent defence). Eight to ten landed hits per spider; at the driver's hit rate (about 1 in 7 swings) that is the
  "impossible" feel. The disc Spider class is untouched by the converter, so this is the disc's own behaviour (zero defence and
  armour, `Damage per second` 1.0, no hit-reaction clips). Not changed.
* The shipped Scavenger (converted PC model, the "walking spider-like" creature) is the one that cannot be hit. Its hit volume is
  the PC skeleton's 10 spheres (body 0.36, nine thin limb spheres) written as the PS2 sphere list, centres resolved from the PC
  vertices correctly (checked against the PC model). The disc Scavenger has 25.
* It is not the sphere data format: the 64 byte bounds, the class cache of them, channels (joint indices, as on the disc) and the
  tree layout all match the disc's form.
* A damage ring around it does exist: in the one pattern that hit, Rynn on the creature's front side at about 630 units (the end
  of her reach) took 4, 14 and 42 hit points off it in 3 cells, killing it; from the sides and rear at 300 to 900 units, and from
  the front closer than 600, nothing. Scaling every sphere radius by 3 made the first cell kill it (22 hit points). Synthesised
  per-joint spheres (centroid and reach of each joint's vertices, 20 spheres, root enclosing them) and a 0.2 radius floor did
  not change the result in the 300 / 450 / 600 sweep (0 of 12); the 630 front distance was not rerun with them.
* Flag words (`+0x104`: cell list membership bit 28, cell 405) are the same for the Scavenger, Spider and Wartok actors.

## Open

* Why a front-only, far-edge hit volume: either the converted sphere hierarchy is posed away from the mesh at run time (the
  engine's per-joint pose of a retargeted PC rig) or only one sphere is live. The next step is a RAM probe of the live sphere
  centres against the actor position (the root sphere's bytes were not found in guest RAM, so the engine keeps another form),
  or testing the 630 front distance on the synthesised sphere variant.
* Arrows were not tested.

## Fix and follow-up (same day)

* Grid of 48 cells (8 bearings x 250 / 400 / 550 / 650 / 750 / 900 units, 5 swings each, creature health reset each cell through a
  private WriteProcessMemory): the converted-model Scavenger took damage in 16 cells, mostly behind it (12 of 16 had Rynn at a
  negative offset along its facing, hits up to 750 units behind), the disc Scavenger in 27, mostly in front (22 of 27). So the
  PC sphere hierarchy lands on the opposite side from where the disc one does, with the creature awake and moving in both, which
  makes a clean offset unmeasurable; a z flip of the centres was prepared but superseded by the decision below. For the PC-model
  option: the PC hit volume is a coarse bound (the PC engine hits on polygons), 10 spheres with thin limbs, and not placed like the
  disc's.
* Decision: the Scavenger row of `DEFAULT_PS2_MODEL_SUBSTITUTIONS` is on by default (opt out with `--ps2-model-map`
  `{"ootf":"Scavenger","ps2":null}` or the import dialog). Default pack grid, same 48 cells: damage in 31, and in 11 of the 16
  cells at 400 units or less, from seven of eight bearings. A Scavenger dies from sword hits (earlier sweeps killed the disc one:
  4 to 12 hit points per 10 swings of its 35 to 40). It keeps its PC placement; health, attacks and clips are the disc class's.
  Animations were not inspected in a screenshot sequence.
* Tick (the small PC spider-like creature, same engine class as Spider): the PC model is hit in 21 of 48 cells; mapped to the disc
  Spider it fell to 3 of 48, so it was left as the PC model (no row).
* Wartoks in play (`Troll Leather / Chain / Plate` and the flankers) already use the disc classes by default (November 8).
  The scene-only `Wartock Leather / Chain / Plate` props (rfl 17, cloned onto a goblin-prop template and moved only by scenes)
  still use PC models: swapping them needs the scene clips retargeted onto the disc Wartok rig and the weapon and impale attach
  channels remapped (`ootfretarget.ts`, the Rynn clip builder in `ootfpipeline.ts`); not done.
* Spiders' low damage per hit (1 to 2 of 15 hit points against 5 on a Wartok) is the disc Spider class's own behaviour; unchanged.
