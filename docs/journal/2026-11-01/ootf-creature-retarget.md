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
