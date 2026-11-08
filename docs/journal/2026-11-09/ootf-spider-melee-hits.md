# Spider-like OotF creatures that melee seems unable to hit (2026-11-09)

Report: the spider-like creatures of Wartok Canyons (50 `Spider`, 13 `Scavenger`) seem impossible to hit with melee. No fix landed;
this note records what was measured so the next session does not repeat it. Probes, packs and runs are private under
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
