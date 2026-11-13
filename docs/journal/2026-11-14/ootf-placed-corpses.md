# Order of the Flame: dead bodies standing upright (round 3)

Owner: "we've seen NPC models standing upright instead of being flat on the ground being dead." Private runs, probes and
evidence under `build/review/ootf-corpses/` (`runs/`, `sheet_*.png`, `tools/`, `packs/`); no game data in this note.

## Every placed body of the PC levels

A census of all 23 PC levels (`tools/census.ts`, `tools/bodymodels.ts`, `tools/props.ts`) found two kinds of bodies.

* **Creatures placed dead** (an object `Initial Health` of 0 or less; the PC has no other dead flag, death frame, pose
  index or start state on these classes). Eight objects, all already handled by `placedCorpsePose` (formats `6ab2b0f`,
  `docs/journal/2026-11-08/ootf-grunt-pose-fixes.md`), which sets the object's `Base Idle` to the class's `Idle Death`,
  else `Death` clip:

  | Level | PC object | PC class | PC value | PS2 class played |
  | --- | --- | --- | --- | --- |
  | Ruined Village | 2798 | Orc Plate (yellow) | Initial Health -50 | disc Yellow Orc : Center |
  | Ruined Village | 2563 | male slave | 0 (hidden until a trigger) | converted (own rig) |
  | Wartok Canyons | 820 | male slave3 | 0 | converted (own rig) |
  | Grimstone Mines | 2174 | male slave2 | 0 | converted (own rig) |
  | Grimstone Mines | 2255 | male slave3 | 0 | converted (own rig) |
  | Grotto | 483, 500 | Troll Chain | 0 | disc ChainTock : Center |
  | Islands | 5226 | Goblin | 0 | converted (own rig) |

* **Body props** (engine class 17 scenery and one class 35 mover): `deadpose1..3` and `dead04` (Islands Night), `Hanging
  Guy`, `Hanging torso`, `Cage Body(L)`, `Spiked Body`, `Spiked Torso`, `Skeleton*` (Wartok Canyons, Grimstone Mines,
  Alwarren, Volcanic), `Torture Gyro Victim` (Mines), `Victim` (Volcanic), `Body` (Wartok Canyons, class 35). Their pose is
  baked into a rigid model: no class or object field names a clip. In Wartok Canyons' torture room the hanging man, the
  cage bodies and the hanging skeletons show as on the PC (`runs/wc-before/cage813_2.png`). The scene victims (the
  Wartok's spear victim, the lying Rynn) are scene actors, fixed earlier in `9797724b`.

## What stood upright and why

Seen before the fix (installed pack of 13 November, `packs/before`):

* Wartok Canyons slave 820 stood upright holding its pickaxe (`runs/wc-before/slave820_1_b.png`).
* Grimstone Mines slave 2255 the same (`runs/mines-before/slave2255_1.png`, `sheet_mines_before.png`).
* Ruined Village orc 2798 and Grotto troll 483 lay down (`sheet_rv_before.png`, `sheet_grotto_before.png`).

The slaves are converted with their own PC rig, so their class is a clone of a PS2 creature template whose fields are
overwritten by name from the PC class (`mergeDefaults`). The PC slave classes name only `Death`; their `Forward Death`,
`Sleep Death` and `Idle Death` are empty. Template clip fields the PC class does not fill name clips of the template's
rig, which do not fit the converted rig, so they were all replaced by one stand-in: the PC class's `Base Idle`, the
standing pickaxe swing. `placedCorpsePose` prefers `Idle Death`, so a placed dead slave got the pickaxe idle as its pose.
A second effect: the converter makes every clip named by a death field play once and hold (`holdLastFrame`), so the
slaves' working idles (`pickingIDLE1` of male slave and male slave2, `picking1` of male slave3) were written to stop after
one swing; living slaves froze. A slave killed in a way that makes the engine pick `Sleep Death` or `Forward Death` would
also have played the idle instead of dying.

## Fix (formats, `ootfclass.ts`)

A template death field (`Death`, `Forward Death`, `Sleep Death`, `Idle Death`) that the PC class leaves empty now gets the
PC class's own converted death clip (its `Death`, else the first of the other three that converted); other unfilled clip
fields keep the Base Idle stand-in. Rule, not per body: it applies to every creature converted with its own rig. Diffing
the packs before and after shows it changes exactly the three slave classes (male slave, male slave2, male slave3), their
placed objects' `Base Idle` (now the converted `death`) and the three idle clips that no longer hold. Test:
`ootfclass.test.ts`, "fills a template death field the PC creature leaves empty with its death clip, not its idle".

## Proof in the game

Own play folder (copy of `build/review/baseline-play`: the owner's `drakan.ini`, HD textures, DLSS; exe copied from
`dist/DrakanPC`, private ELF), pack `packs/fix1` converted from the working tree with the subtitle files (the before pack
is the installed one of 13 November). Direct level starts on an empty card with god mode; one placement next to each body
(Arokh landed with Cross held first where the level starts mounted), then real walking keys and mouse steering, mouse
attacks, F5 quick save and F8 quick load (`plans/walk.py` on `scripts/harness/live_drive.py`).

| Level, body | Before | After |
| --- | --- | --- |
| Wartok Canyons, slave 820 | upright with pickaxe | lying; Rynn walked past and around it, swung beside it, F5 / F8: still lying (`sheet_wc_after.png`, `sheet_wc_walk4.png`) |
| Grimstone Mines, slave 2255 | upright with pickaxe | lying; Rynn walked over its legs to the far side unblocked, swung beside it, F5 / F8: still lying (`sheet_mines_walk2.png`) |
| Grimstone Mines, slave 2174 | not visible from the placement | lying; a living troll fought Rynn next to it; after F5 / F8 still lying (`sheet_mines_walk.png`) |
| Ruined Village, orc 2798 (third level, on foot) | lying (disc class, unchanged by the fix) | lying; Rynn walked straight over it to the far side, fought the Scavengers that came at her beside it, F5 / F8: still lying with Rynn standing on it (`sheet_rv_walk.png`) |

Readings of the body objects (registry class `0x10031`) never left their spot through walking, fighting and the load. The
slaves stayed at 0 hit points. The Ruined Village orc went from -50 to -170 while the fight went on next to it: blows land
on a corpse and lower its hit points, but it stays down and does not move.

## Not verified

* PC reference: the PC game was not run. Whether the PC lets Rynn walk over a corpse is assumed from the data (the bodies
  are dead creatures, the PS2 engine does not collide with them in these runs).
* Wartok Canyons slave 820 lies about 290 units from its placed spot: the death clip moves the root as the body falls. The
  PC plays the same clip; its resting spot on the PC was not compared.
* Islands goblin 5226 and Grotto troll 500 were not seen (the Islands run could not land Arokh near the goblin); their
  packs are unchanged by this fix and their `Base Idle` is a real death clip.
* Ruined Village slave 2563 is hidden until a trigger shows it; not seen.
* Whether a dead body can be locked on as a target was not tested with a lock-on key; swings beside the bodies did not move
  them or change their hit points.
* The F5 / F8 check reloads the same level in one process; a load from the title was not run.
