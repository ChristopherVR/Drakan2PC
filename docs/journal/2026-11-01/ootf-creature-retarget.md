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

## Converted-skeleton creature bisect (2026-11-01, afternoon): Scavenger still invisible, but most suspects cleared

Probes in `build/ootf-cr/*.ts` (private), runs `build/ootf-run/cr*`, `sl*` (Wartok Canyons `^Scavenger$`, Succubus Lair pulls, `rtrun3.sh` with `XENV1=GSR_DRAWLOG=a-b`).

**Disc facts found (all PAL, checked on the 154 skinned models):**
* The 64 byte "model block" of a class record (body 16..80) is a byte copy of the first 64 bytes of the model's 0x0208: sphere (centre, radius), AABB min,
  extents (Scavenger 5055: `-0.009 0.001 -0.090 0.388 | -0.351 -0.157 -0.275 0.699 | 0 0 0 0.271 | 0 0 0 0.459`). There are NO joint-index tables in it. Clones now
  carry their own model's block (`OotfAssetBuilder.skeletonBounds` / `modelBounds`, `patchRecordModel(.., bounds)`).
* Channel 0 (joint 0) carries one cap per LOD, `{0, 0, firstPoly, polyCount, 0}` (Scavenger 1693 + 223, troll 1229 + 606 + 302); other channels mostly none. The
  converter dropped all caps; channel 0 now gets its cap back. (Earlier note "mostly with no caps" was about the other channels.)
* Channel-typed class fields (Head / Neck / Upper Body / Jaw / Weapon, Choppable / Dmg Inflicting lists) hold JOINT ids of the rig (Scavenger Head 3 = `Head`, Jaw 4).
  The PC class holds PC joint ids that are valid for the converted rig (PC Scavenger Head 6, Neck 5, Upper 3, Jaw 7, Dmg [8, 22, 11]); `mergeDefaults(.., copyChannels)` now
  copies them for converted models. Known gap: a template can name a field twice (`Jaw Channel` at 52 and 234), the name map only reaches the last, so field 52 keeps the
  PS2 joint 4 (not a channel of the PC rig). Writing both made the run `cr30` lose even the shadow (confounded: the pack also carried another session's changes), so it was NOT committed.
* PC bounding spheres name their centre by a vertex that sits AFTER the LOD's own vertices (Scavenger 165..174 of 180); the converter read the sliced list and wrote
  every centre as (0,0,0). Now read from the unsliced list.
* Skinned-model textures are written 8 bpp (`addTexture(.., indexed)`), like the disc's creature textures (the default 32 bpp truecolor draws on static models).
  This did not change the invisibility.

**Controls (Wartok Canyons, 4 pulled Scavengers, screens `a3.png`):**
| Run | Model of the cloned rfl-41 class | Result |
| --- | --- | --- |
| cr5 | template's own PS2 model 5055 db 0 (clone keeps the template record) | PS2 Scavengers draw |
| cr7 | PS2 5055 records verbatim in RESOURES.MOD (db 2) + its textures copied | draws (so cloning, new ODB id, db 2 hosting, converted anim fields are all fine) |
| cr4 | the same without its textures | nothing, not even a shadow (missing textures kill the creature) |
| cr11 / cr13 | cr7 + 23 channels / + only 10 spheres | draw |
| cr12 / cr9 | cr7 + the skeleton clip list replaced by the converted ids (43 track clips on a 54 joint rig) | nothing, no shadow |
| cr6 | real conversion (caps, bounds, channel fields, sphere centres, sorted clip list) | shadow, no body (as before) |
| cr17 | real conversion, empty skeleton clip list | nothing, no shadow |
| cr2order / cr2first / cr2rev / cr2ps2 | real conversion, clip list in PC order / one id / descending / the PS2 template's ids | nothing, no shadow |
| sl1 | Succubus Lair: converted Crimson Knight (rfl 68, model 6657) x4 | **bodies DRAW** (dark armour, idle) |
| sl2 / sl3 | Succubus Lair converted Scavenger (rfl 41) / Death Mage (rfl 82) | not visible (dark level, no sign of either) |

Reading: the converted skeleton itself is accepted and draws for a creature class (rfl 68), so "converted skeleton + creature class" is not the failure. The Scavenger (41) and
probably the Death Mage (82) classes fail: whatever they do with the model differs from rfl 68. What is known: the model's own clip id list matters (only the ascending list of the
converted ids leaves a shadow; any other order, subset, the empty list or the PS2 ids make the creature vanish completely); a PS2 rig with the converted list also vanishes, so clip
and rig must agree (clips 43 tracks / 23 channels vs rig 43 joints / 23 channels agree in the real conversion). Spheres (10 vs 25), channel count (23 vs 8), texture format, shading
word, LOD count are not the cause. A GS draw log (`GSR_DRAWLOG`) of the Scavenger run could not be matched to the body (scenes differ run to run, TBPs move).
Not tried: running the rfl-68 pull with the Scavenger's template record (`ScavengerTest`/`Scavenger` rfl-41 body flags), the Death Mage in a lit level, 2 LODs, the engine code
`FUN_002d2d00` (creature init, error string "does not have a character model assigned!" when the handle has no skeleton) and `FUN_0030ef50` (reads class fields 0x28.. and model records 0x205..0x208).
Because the Scavenger is invisible, the new-clip-id case (a converted clip id reached through a class animation field) is still not shown playing; the Crimson Knights of `sl1` use them
(their class fields map to converted ids) and stand in their idle, but no frame sequence of a changing clip was taken.

## Converted-skeleton creatures: the cause is non-square textures (2026-11-01, evening)

**Root cause: a skinned (creature) model with a non-square texture draws no body (only its shadow).** The Scavenger's PC texture 6773 is
256x128 with alpha; every one of the 143 disc skinned models checked (NPCS.MOD 108, RESOURES.MOD 35; `build/ootf-cr/dt.ts`) uses only square
textures (256, 128, 64, 32, 16). The Crimson Knight's three textures are square, which is why it always drew.

Bisect (Wartok Canyons, 4 pulled `^Scavenger$`, runs `cs_*`, one change each; 15 s convert + 40 s run):

| Run | Change | Result |
| --- | --- | --- |
| cs_b | class record flags 0x21 instead of 0x20 | shadow only (flags are not it) |
| cs_c | Scavenger cloned from an rfl-68 template | crashed (null vtable): not a usable probe |
| cs_d | Farmer's converted model on the Scavenger class | the Farmer body draws under rfl 41: **the class is fine, the model is the problem** |
| cs_e | every polygon of the model mapped to its first texture | body draws |
| cs_alpha | only the alpha textures (6773 256x128, 6771 128x128) mapped to the opaque 128 px one | body draws (6771 is square, so not "alpha" itself) |
| cs_sq | `encodePs2Texture(.., {square: true})` for skinned textures (6773 becomes 256x256) | **textured Scavenger draws** |

Fix (formats `33030f4`): `Ps2TextureOptions.square`, set by `OotfAssetBuilder.addTexture` for `indexed` (skinned-model) textures; the larger edge is used both
ways (UVs are normalised, so nothing else changes). Before this the clone's record flags, clip lists, channel caps and sphere data (all earlier suspects)
were irrelevant. The engine reads the creature's textures with a square-only assumption; why the shadow still draws is not known (shadow uses the mesh,
not the textures). Reading `FUN_002d2d00` / `FUN_0030ef50` (Ghidra headless: `analysis/input_run.sh InputDecompile.java build/cr-decomp 2d2d00 30ef50`)
showed only AI/controller init (class fields 0x13, 0x14, 0x17, 0x2e, 0x205..0x208 for weapon / body part records), nothing about the body draw.

The 0x0200 model record's id list is the TEXTURE list (0x0040 ids), not clips; the clips are the skeleton's own `animIds` list (0x0208).

Proof: `build/ootf-run/cs_seq/run/s0..s11.png` (4 pulled Scavengers 90 units around Rynn, one every 250 ms), contact sheet
`build/ootf-run/cs_seq_contact.png` and creature crops `cs_seq_crops.png` (private, not committed): the converted, textured Scavenger stands beside Rynn
and its claw / arm / head pose changes from frame to frame (idle loop of its converted clips; mean frame difference 0.3-0.85 grey levels over the
whole frame).

## Remaining creatures in game (2026-11-01, night; runs `build/ootf-run/cre_*`, private)

Helper fixes: `rtconv.sh` now shows the `pull ...` lines; `cre_run.sh` (private) takes the game lock with a trap so it is released on failure.
Pull ring 150 units (lit Islands) or 250 (dark levels); 4 shots a few hundred ms apart.

| Creature (level) | Result |
| --- | --- |
| Death Mage rfl 82 (Volcano) | draws (robed skeleton mage beside Rynn), converted skinned model; idle only, level very dark |
| Ebon Knight rfl 68 (Volcano) | draws (horned armour) |
| Goblin rfl 88 (Islands) | was a PS2 stand-in (template model, a huge spiked monster). `MODEL_STANDINS` now includes 88, so the PC goblin model is converted onto the rfl-49 clone: green goblins draw |
| Succubus 100 / Queen 101 (rfl 30 stand-in) | with the converted model: live entities (hp 44-50, model 0x319ff) and moving shadows but no body visible in the ring (they hover/fly away; not proven). Left as PS2 stand-ins (NOT added to `MODEL_STANDINS`) |
| Queen (Volcano) | nothing visible in the 400 ring |

Not done: dragons (rfl 30 clones, flying), war giants / orcs / Hueges, attack clips; a flying-creature view needs a camera that looks up (harness input).

### Follow-up: flying creatures, giants, orcs, grenadiers, attack (2026-11-01, night)

Harness aids (private, `build/ootf-run/`): `cre_up.txt` / `cre_both.txt` recipes (`mouse 0 -9 20` looks up, `button tap 1` attacks), `cre_run.sh`
(lock released in a trap), `cre_sheet.py`. Pull regexes see the MAPPED class name: orcs are `^(Yellow|Purple) Orc`, war giants `^Primitive Giant$`
(`^War Giant$` pulled nothing, which is why earlier orc / giant runs showed nothing).

* Succubus (rfl 100, converted model on a rfl-30 clone, `MODEL_STANDINS` now 88 / 100 / 101): red winged bodies draw high above Rynn and flap
  (earlier "shadow only" was just the camera not looking up). Queen (101) uses the same path (loads, not seen: the Volcano is too dark).
* Goblin / Goblin Grenadier (88): green goblins draw and their pose changes between frames.
* Primitive Giant and Yellow Orc (disc models, retargeted clips): draw next to Rynn, idle (orc pose shifts slightly).
* Crimson Dragon (rfl 30, converted model 6725: 44 joints, 8 clips, 2 square 256 textures): draws, and in `cre_icd` it attacks Rynn (fire / magic
  effects, health orb dropping): the AI engages and plays attack clips.
* Ebon Dragon: same geometry / rig / texture format as the Crimson Dragon (verified with `build/ootf-run/x/dd.ts`, `ta.ts`); in the runs only its wing
  shadows were seen on the cave wall (alive, hp 225, near the ground per the world dump, but behind / above the third-person camera). Not
  proven by a picture, no defect found.
* Not run: Death Mage ghost / Hueges (Navy Hueges rfl 49 clone), batdragon / bladedragon, Drone / Tick (queue contention: the game lock was held
  by other agents for 20+ minutes, one run gave up with "no lock").
