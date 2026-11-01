# OotF creatures beyond the spider: retarget onto a different PS2 rig (2026-11-01)

Code: `editor/packages/formats/src/ootfretarget.ts` (import by path, like `ootfcreature.ts`), tests `test/ootfretarget.test.ts`
(synthetic). Builds on the solved rotation basis in `2026-10-31/ootf-animation.md`. Probes (not committed): `build/ootf-anim/r*.ts`, `pack2.ts`.

## Census: which PS2 rig for which PC creature (name correspondence, 49 PC skeletons vs 116 PS2)

Only spider 3228/3717 has an identical tree. Everything else shares a *family*, with different joint counts, so the identical-tree
path of `convertCreatureAssets` never fires. Joint-name coverage (PC joints with a same-named PS2 joint):

| PC model (joints) | closest PS2 model (joints) | matched | note |
| --- | --- | --- | --- |
| goblin 3680 (31) | goblin 4550 (31, 1 anim) | 31/31 | same names, bind differs 0.108; PS2 4127/4156 "Goblin" (45, 70+ anims) is another rig |
| wargiant 3679/3764/3694 (25) | goblin 4550 (31) | 25/25 | same skeleton family, scale 5x (size 2.07 vs 0.38) |
| hueges 3713/3714/3840 (34) | hueges 4395/4396/4400/4675 (55) | 26 | PS2 adds ears, fingers; PC `root` and 6 flail joints have no partner |
| Troll 3508/3534/3536 (30) | Troll 4418/4422/4427 (50) | 30/30 | PS2 adds fingers, eyelids |
| deathmage 3912/3926 (44) | deathmage 4368 (46) | 20 | PC tattered tails/fingers differ |
| batdragon 3671/3676 (44), primgiant 3684 (44), Dragon 3685 (50), bladedragon 3932 (36) | same-name PS2 models (41, 39, 54, 39) | 36..50 of 50 | re-authored but 85-100 % of PC joints have a partner |
| villagem/f, wrynn, julian, fynn (humanoids) | PS2 Villager (57) / rider (55-60) | 25-33 of 34-40 | names `upperbody,torso,chest,neck,head,Lcollar,Luparm,Lforearm` shared |
| wizard 3726, succubus 3687, Queen 3688, CrimsonKnight, farmer/warrior | none above 50 % | | need `convert` (PC rig) |

## Method (`ootfretarget.ts`)

A PC key is a rotation in model axes applied after the joint's bind world orientation (rig independent), so retargeting is:
match joints -> feed the partner's keys through the *target's* bind basis (`convertPcAnimToPs2` with the target `bindBasis`).

* `matchJoints(srcRig, dstRig)`: stages (1) identical normalised name, (2) side + part synonyms (`Lbicep`/`Luparm`/`ArmUpL`/`Left_Upperarm`
  -> `l:upperarm`, forearm/lowarm/radius, thigh/femur, calf/shin/tibia, jaw ...), (3) the single root of each, (4) tree walk: the only
  unmatched child under matched parents. Only unambiguous keys match; the result is one-to-one.
* Unmatched target joints (fingers, ears, twist bones) hold the bind rotation (one key); unmatched PC joints are dropped.
* Root translation scaled by target/source `rigSize` (largest edge of the joint bounding box). The wargiant->goblin pair shows why
  (5x); troll 1.42, hueges 1.09.
* `chooseRetargetTarget(pcSkel, candidates)`: score = source coverage * (0.5 + 0.5 target coverage), refuses < 50 % source coverage.
* `retargetCreatureAssets`: picks the target, keeps the target's own same-named clip (id map), converts the rest under the PC id when free in the ADB, else next free id.

## Offline validation (PC clip retargeted vs the disc's own same-named clip of the PS2 model)

Local-rotation difference, mean over joints and 40 samples, then mean over the same-name clips. This is *pessimistic*: where the PS2
model was re-authored with another bind pose (troll Jaw1 23 deg, R_hand 25 deg, legs 5-20 deg) the local rotations differ even for the
same visible motion.

| pair | clips compared | mean rot | mean joint pos error |
| --- | --- | --- | --- |
| Troll 3508 -> 4418 | 17 | 6.1 deg (idle 8.9, deathforward 3.1, bodycheck 3.3) | 0.07 |
| hueges 3713 -> 4395 | 29 | 2.3 deg (WP_Waving 0.3) | 0.03 |
| deathmage 3926 -> 4368 | 8 | 19.6 deg | 0.03 |

(spider same-tree reference: 0.8 deg.) The deathmage error is dominated by root/waist/Dangle joints at exactly 180 deg: the PC clips
(`hover`, `rushing`, ...) key the root with a half turn about the vertical axis that the PS2 clips do not have (binds are
equal, 0.0-0.2 deg). A per-creature facing correction would be needed; not guessed. Single joints with large errors at equal bind
(troll Lhand/Weapon_L 60 deg) are re-authored hand poses.

## Not done / next

* `convert` mode for creatures with no PS2 rig above 50 % (wizard, succubus, Queen, CrimsonKnight): needs the 0x0211 display list of the
  skinned mesh (decoded by someone else) and the class side.
* Enumerating which class/animation fields reference a PC clip id (the class mapper owns that); `animIdMap` is the hand-off.
* Goblin 88 -> PS2 id-49 substitution (engine-gaps) is a class change; the goblin PC rig retargets exactly onto PS2 4550 (31 joints), but that
  model has a single disc clip, so all 33 PC goblin clips would be added under new ids.

## In-game (LEVEL1NA, PS2 Leather Wartock class 5174 = troll model 4418, five placed around Rynn)

Pack generator `build/ootf-anim/pack2.ts` (private): the 17 clips of PS2 troll 4418 that have a same-name clip on the PC troll 3508 are
overridden in `NPCS.ADB` with `retargetCreature(pcTroll, ps2Troll)` output (30 of 30 joints matched, scale 1.42; all 61 PC clips
converted, 0 skipped). Run with `play_run.py` as in `ootf-animation.md` (screens `build/ootf-anim/out/rt_{base,conv}/a*.png`). Result: the
engine accepts the clips (50-joint skeleton, fingers/eyelids held at bind) and the trolls attack and roar with natural, unmangled poses,
the same silhouettes as the disc's own clips. Not done: pixel-diff of the two runs, clips under new ids (override only), a creature whose
model is not on the disc.

## Wired into the converter (2026-11-01, later)

Code: `ootfpipeline.ts` (`retarget` option, `OotfPs2Context.mods` / `adbs`), `ootfclass.ts` (`modelOdbFiles`, `keepOwnModel`,
`templateSkinned(cls, file)`), `ootfassets.ts` (`creatureSource`). Converter flag `--retarget 0` turns it off; `ootf-convert.ts` loads
every non-RESOURES `*.MOD` / `*.ADB` of `--ps2` (the harness now extracts `NPCS.MOD` / `NPCS.ADB`).

* Skinned clones may go into any ODB whose `.DB` lists the host (NPCS.ODB lists Resources as dependency 2): the template must be skinned
  in its own container, the model ref uses the host's `db` in that ODB (`hostDbForStem`). Only skinned classes use this; static
  clones still stay in RESOURES.ODB. Converted clips get ids past every ADB's max and ship to RESOURES.ADB and to the ADB of each
  ODB that received a clone (class animation fields carry plain ids, resolved in the class's ODB ADB, not db-qualified).
* A PC creature class with a same-named PS2 class whose model is skinned: PS2 rig covers >= 50 % of the PC joints -> stays name-mapped,
  the PC clips are retargeted (`retargetCreature`) and replace the same-named PS2 clips (`overrides/<STEM>.ADB/0501+0502_<id>_<sub>`).
  Otherwise the class goes through `keepOwnModel` and is cloned with the converted PC model (convert path). Wartok Canyons: Spider
  (37/37, 10 clips), Troll x3 (30/30, 17+1+2), Blade Dragon (36/36, 15) retarget; Scavenger (PS2 5055) converts.
* In game (`build/ootf-run/rt1..rt6`, build/final-game exe, `DRAKAN_DBG_NOOBJ/PULL` are converter-side env): the level with 323 new
  classes (21 in NPCS.ODB) and the retargeted ADB overrides loads and plays; a converted skinned humanoid clone draws and poses.
  NOT verified: a converted/retargeted clip visibly playing (the pulled Troll AnimWay Point and Scavenger objects never showed up
  near Rynn, so no screenshot sequence of one exists yet).
* The test "maps same-name classes ..." now asserts the intended range: synthesized ids sit above the ODB maximum (fffdffc in the formats repo).

## Follow-up run (2026-11-01, evening): pulled creatures still not visible

Wartok Canyons has real creature classes (`Troll Leather` x39, `Troll Chain` x7, `Troll Plate` x3, `Spider` x50, `Scavenger` x13,
`Blade Dragon` x9; the `*AnimWay Point` classes are way points, and `^Troll ` also matches the static `Troll Axe` prop, which is the
floating axe in earlier screenshots). New converter test aids: `DRAKAN_DBG_LIST=1` prints the class histogram of the level,
`DRAKAN_DBG_PULL` now logs each pulled object (`pull <class> obj <id> from <pos>`). `rtrun.sh` honours `RECIPE=` (e.g.
`rt_turn.txt`, which holds D for 2 s to turn Rynn 90 degrees).

Runs `t1`/`t0` (`^Troll `) and `s1` (`^Spider$`, `--retarget 1`, turn recipe) load and run cleanly, but no creature body is visible in
any frame: the pulled objects land at the player start's height 1200 units away (behind ridges, or not spawned without their
level triggers), and no clip comparison can be made from frames that contain no creature. So a visible converted/retargeted
clip is still NOT shown in the converted level. Next try: pull to a flat spot a few hundred units in front of the start camera, or
place the object at the start itself with `DRAKAN_DBG_NOOBJ` off for the AI/spawn objects. The only in-game proof remains the
PS2-level run above (trolls attacking with retargeted clips). Not tested: ids not on the PS2 model (new ids) used by classes, and the
`0x0103` field alignment for ids below the ODB maximum (`OOTF_IDS_BELOW_MAX=1`); the default places every synthesized id above the
maximum, where file, index and id order agree, so the question only matters for that opt-in mode.

Formats suite: `tsc` clean, vitest 266/267; the one failure (`ootfseq` "AI resume zeroed") is a pending uncommitted edit of
`src/ootfseq.ts` by another session (`keepAiResume` now defaults to true; the test still expects 0).

## Third attempt (2026-11-01, late): the pulled trolls spawn, but their bodies do not draw

New converter aids (`ootfpipeline.ts`): `DRAKAN_DBG_PULLDIST=<d>,<dy>` (ring distance and height offset of pulled objects; default 1200,0),
`DRAKAN_DBG_PULLP=1` (log object params of pulled objects); `rtconv.sh` forwards `PULLDIST=`. Another session added `DRAKAN_DBG_CLSINFO=<regex>`
(template, model ref, data of the synthesized classes), which gave the key finding below.

* Runs `p1`/`p0` (`^Troll Leather$`, `PULLDIST=400,150`, `--retarget 1/0`, turn recipe): four weapon props ("Troll Axe", 2-handed axes)
  appear next to Rynn and tumble / move between frames, identical with and without retarget; no troll body is drawn. So the objects do
  spawn (the weapons are the creatures' held items) and the body mesh is the problem, or the creature dies at once and drops its axe.
* `CLSINFO` shows why retarget cannot be exercised by these classes: `Troll Leather` / `Troll Chain` / `Troll Plate` have NO same-named PS2
  class (PS2 calls them LeatherWartock / PlateTock / ChainTock), so they are plain synthesized rfl-49 classes cloned from the PS2
  template (`LeatherWartock : Center` #5174, `PlateTock : Center`, ...) with the PC model converted to a new skinned model (id 6649/6663/6664, db 2 =
  RESOURES). Only the `...AnimWay Point` classes (way points, not creatures) name-match and retarget. A retarget therefore only
  affects classes whose PC name equals a PS2 class name (e.g. `Spider`); for the trolls a name alias (PC `Troll Leather` -> PS2
  `LeatherWartock`) would be needed in `byName` in `convertOotfLevels`. Next step: confirm whether the new skinned troll models (6649) draw
  at all (compare with the Heuge that drew in skin13), and add that alias.
* The spider run (`rcs1`, `^Spider$`, 300,100) timed out at 180 s on this loaded machine (about 15 node/game processes of other
  sessions), so no frames. Run names must be unique (`q1` was overwritten by another session).
* Still NOT shown: a visibly playing converted/retargeted clip.

## Name aliases (2026-11-01, night)

`ootfclass.ts` `PC_CLASS_ALIASES` / `aliasSquash`: PC creature class -> differently named PS2 class, used as a fallback after the same-name lookup in
`planClassMapping` and in the pipeline's retarget lookup. Table: Troll Leather / Chain / Plate (+ "defense side attack") -> LeatherWartock / ChainTock /
PlateTock Center (Flank for the side-attack variants), Orc Leather (purple) -> Purple Orc Center, Orc Plate (yellow) -> Yellow Orc : Center,
War Giant / starving wargiant -> Primitive Giant. Goblin (engine id 88) has no PS2 class, Hueges are rfl 17 ground objects: no alias.
Converter result on Wartok Canyons: Troll Leather/Chain/Plate and their side-attack variants now `retarget` (30/30 joints onto PS2 4418 / 4422 / 4427,
17 / 1 / 2 same-named PS2 clips replaced); the Orcs retarget onto the PS2 hueges rig (26 of 34 matched). Scavenger still converts.
The pull helpers: `PULLDIST` is read by `rtconv.sh` (converter side), not by `rtrun.sh`; `rtrun.sh` honours `TMO=` (run timeout, seconds).

### In game (runs `tr_f` retarget 1, `tr_g` retarget 0, `tr_h..tr_l`, `tr_j`; Wartok Canyons, `DRAKAN_DBG_PULL` of 4 objects 180 units from Rynn)

* Pull regexes now see the *mapped* class name: `^LeatherWartock : Center$` (not `^Troll Leather$`, which only matched the 76 way points, so
  every earlier "pull Troll Leather" run pulled way points and never a troll). That also explains the earlier "no troll body" runs.
* Trolls (aliased to the PS2 class and model 4418) draw and animate. `--retarget 0` (disc clips, `tr_g`) vs `--retarget 1` (PC clips retargeted
  over the PS2 clips, `tr_f`): mean frame difference between the two runs about 9-11 grey levels in every frame pair, clearly different poses
  (contact sheet `build/ootf-run/tr_contact.png`, private). World dump (`DUMP=<frame>` in `rtrun.sh`, `DRAKAN_WORLD_DUMP_FRAME`) lists the four
  trolls (class 0x10031, model 0x51142, hp 50) beside Rynn. The trolls stand around Rynn in their idle clip; they do not attack from the
  `DRAKAN_DBG_NOOBJ` level, so only idle/turn clips are compared, not combat clips.
* Converted-model creature classes still do not draw: Scavenger (rfl 41, cloned from PS2 Scavenger #2296 with converted model 6675) spawns four
  live entities (hp 35, model 0x31a13 per the world dump) and casts a correctly posed shadow, but no body is drawn. Eliminated: the 0x0200 shading /
  distance / second word (PS2 character models have 0x20002 / 35000 / 1, ours 2 / none / 0; now written like the disc, no change), the converted clips
  (template clip ids kept: still invisible; every anim field set to 0: still invisible), textures (Farmer, Warrior and Queen, converted skinned rfl-17
  clones in the same level and texture container, draw fine in `tr_j`). So the failure is specific to creature (rfl 41 / 49) classes cloned with a
  converted skeleton model; the skinning itself works (the shadow is the posed mesh). Differences still untested: the PS2 skeleton has 2 LODs and
  an `_L` LOD name (ours 1 LOD, no name), 8 channels with caps (ours 23 without caps), and the class record's model block / joint-index tables copied
  from the template (weapon / head joints indexed for a 54-joint rig, ours has 43).
* The new-clip-id case (a converted clip reached through a class animation field) could therefore not be shown: only converted-model classes
  read new ids, and those are the invisible ones.
