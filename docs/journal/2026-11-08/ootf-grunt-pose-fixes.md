# OotF grunts: killed grunts stood back up, sitting grunts hovered, the "missing arm" (2026-11-08)

User report on the OotF campaign: in Ruined Village some killed grunts end their death standing upright, some grunts float a
bit, and the Wartoks with spears seem to miss an arm. Fixed in the converter (formats `ootfpipeline.ts`, `ootfretarget.ts`,
`ootfcreature.ts`, `ootfassets.ts`, `ootfanim.ts`); nothing in the runtime changed.

Runs are private under `build/ootf-run/grunt-*` (Ruined Village alone, `--campaign 1`, template LEVEL1NA, from a private copy of
the play folder `build/grunt/play`, god mode, `DRAKAN_DBG_NOOBJ` with pulled grunts). Probes are in `build/grunt/*.ts`. Nothing
from either game is quoted here.

## Which classes

Ruined Village's grunts are the PC `Orc Leather (purple)` (21 placed) and `Orc Plate (yellow)` (22), name-mapped through
`PC_CLASS_ALIASES` onto the PS2 `Purple Orc Center` / `Yellow Orc : Center` (PS2 models 4396 / 4395, both the 55-joint hueges
rig). Their PC clips are retargeted onto that rig (`retargetCreature`, 26 of 34 PC joints matched) and replace the 29 PS2 clips of
the same name in `NPCS.ADB`. The PS2 classes keep their own weapons: the purple and yellow orcs carry the PS2 trident, so these
are the grunts with spears. Other creatures in the level are converted with their own PC rig (Scavenger, male slaves) or are
scene actors (`Wartock Leather` / `Chain` / `Plate`, `Heuge Purple` / `Yellow`, `orc01`: rfl 17 props with converted PC
models, parked at y 200,000 until a scene uses them). Wartok Canyons uses the same orc classes (7 + 4) and the trolls
(`Troll Leather` x39 and others -> `LeatherWartock` / `ChainTock` / `PlateTock`, 30 of 30 joints matched).

## Killed grunts stood up: every converted clip looped

A PS2 clip's 0x0501 flags word says whether it plays once and holds its last frame (1) or loops (0); the disc's orc death
clips (`death1`, `death02`, `death_fall_forward`, `death_fall_rear`) are all 1, its idles and walks 0. PC clips carry no such
flag (0 on 1,626 of the install's 1,722 clips, 256 on the rest); the PC engine decides by use. `convertPcAnimToPs2` copied the PC
word, so every converted clip looped. The retargeted `death1` that replaces the disc's ends lying on the ground (root up vector
(-0.39, -0.13, 0.91), hips 0.15 lower, the same as the disc clip), but written looping it started over from its standing first
frame, and the corpse stood back up.

Fix: the retarget stage writes each PC clip with the flags of the disc clip it replaces (`ps2AnimFlags`), so the death clips hold
and the idles still loop. Creatures converted with their own rig get new clip ids and no disc clip to copy from; their death
clips (the clips named by the class fields `Death`, `Forward Death` and `Sleep Death`, `deathClipIds`) are set to play once
(`OotfAssetBuilder.holdLastFrame`; 4 clips in Ruined Village).

## Sitting and leaning grunts hovered: the PC pass-through joint was dropped

The PC orc rig hangs everything on a joint `root` under the top joint `hueges`; the PS2 rig has no `root` and hangs the legs,
spine and tail straight on `hueges`. `matchJoints` leaves `root` without a partner, and an unmatched source joint was simply
dropped. Its keys are where the PC clips lower the body: the sitting, dice and drinking clips (`WP_Sitidle`, `WP_Throwdice`,
`WPgrabdice`, `WPsit2stand`, `WP_Stand_to_Sit`) lower it by 0.135 of the 0.46 rig, `WP_Lookout` by 0.026, `WPleverpull` by 0.02
to 0.03. Without them the legs fold as if seated while the hips stay at standing height: forward kinematics of the retargeted
`WP_Sitidle` put the lowest joint (a toe) at -0.035 against -0.181 for the standing idle, so the seated grunt floated about 0.15
of the rig (about 300 world units) above its seat, and a lookout grunt about 50.

A PC key is a model-space delta transform: its rotation is the model-axes delta of `ootfcreature.ts`, and its translation is the
pivot term `P - P * K` (P = the joint's bind position) plus any real travel. On the orc rig 35,702 of 39,092 keys fit that
within 0.003; the rest are real travel, on `hueges`, `root` and the joints hanging on `root`. Deltas of a chain compose child
first, so a pass-through joint folds exactly into its parent: `M_parent' = M_child * M_parent`, translation included.
`foldPassThroughJoints` (in `retargetPcAnim`) does this for an unmatched joint that is its matched parent's only child and sits on
the parent's bind pivot (the orc `root` is both). With it the retargeted seated clips put the toes at -0.180 to -0.181, on the
ground like the standing idles.

The fold showed a second defect: `convertPcAnimToPs2` rebased the root translation on the first key (`k - k0`) and wrote the bind
translation as the track base. On the disc a key position is the joint's absolute local translation and the base is the first
key (all 1,228 root and 2,820 other moving tracks of `NPCS.ADB`). So a clip that starts away from the bind (the seated clips
after the fold, the Death Mage, whose PC root starts 0.13 below its bind in 17 of its 25 clips) lost that offset. The root track now
keeps the absolute PC positions with the first key as base. For a root whose first key is at the bind (the orcs, trolls, Rynn)
nothing changes.

Placement is not a cause: PC grunts stand 375 units over the ground (some yellow ones up to 800), the same as the disc's 374 for
these models (model bounds minimum -0.183 x 2048), and grunts pulled to Rynn's height (500) settled to about 370 within
seconds (world dump of `grunt-base`). Rynn is not affected by either change: every one of her 240 PC clips starts with its
root exactly at the origin, and her rig has no unmatched joint, so her retargeted scene clips are unchanged (her sunken feet
in scenes had their own cause, `ootf-rynn-scenes.md`).

## The "missing arm" (not found, still open)

Not found, and a later user report on a pack without these changes says the spear grunts "still don't always show their arm". On the grunts the arm joints are matched one to one (`rshoulder` .. `rhand`, `rweapon`, the left side) with equal
bind positions, and forward kinematics of the retargeted `ready`, `walk` and `idle_lookingaround` put shoulders, elbows and hands
within 0.006 of the disc's own clips. The converted troll model of the scene Wartoks (`Wartock Leather`, PC 3508) keeps vertex
weights on every arm joint (both arms 6 to 13 vertices per joint, against 7 to 14 on the PC LOD 0). In the screenshots
(`grunt-fix1/run/t07.png`, `t08.png`, `t11.png`, brightened crops `build/grunt/fix1_t0*_z.png`) the trident grunts raise the
trident overhead with one arm or hold it level with both. What is left unexplained: a grunt seen from behind with the trident in
the hand on the camera's left whose other arm is hidden by its body (`grunt-fix1/run/t00.png`), and the scene Wartoks, which
the harness skips (the autopress taps through the opening scenes), so they were not seen. Also checked: for every one of the
29 replaced clips the closest approach of each forearm and hand to the spine-to-neck axis matches the disc's own clip of that
name within 0.008 (no retargeted pose buries an arm in the torso that the disc pose does not), no key is a non-unit or
non-finite quaternion, and the PS2 orc models (4395, 4396) weight 21 to 89 vertices on each arm joint on both sides. The PS2
classes keep their own weapon, weapon channel (32, `rweapon`) and an empty "Choppable Chan's (alive)" list, and no PC object
parameter or kept PC class default touches a channel or body-part field. Not yet checked: the second LOD of the PS2 orc model,
the disc's own attack and hit clips that these classes still play (`Attack_Mid`, `Attack_Low`, `HitRxn*`, never replaced),
and whether the arm is hidden by the trident's own geometry.

## Placed corpses still stand (open)

Ruined Village places one grunt already dead (object 2798, `Initial Health` -50) and Wartok Canyons and Ruined Village dead
slaves (`Initial Health` 0); the disc never places a dead creature. The PS2 engine spawns such an object as dead (no hit
points, out of the AI lists) without playing any death clip, so it stands in its idle frame, with and without the loop fix
(`grunt-single-turn`, `grunt-single-base-turn`). A placed corpse needs its own handling (for instance a prop posed at the last
frame of its death clip); not done.

## Evidence (private, under `build/`)

* Killed by Rynn: `build/ootf-run/grunt-sword/run` (pack `build/grunt/sword`, pulled Atimar Blade picked up with E and
  equipped through the inventory, three pulled grunts with `Initial Health` 1 via the new converter aid `DRAKAN_DBG_PULLSET`).
  Two grunts die (world dump: hit points -21 and -31, out of the AI lists); `p2.png`, `k00.png`, `k01.png` (crop
  `build/grunt/sword_k00_z.png`) show a killed grunt lying on its side next to its dropped loot for at least 1.5 s.
* Same run with the death clips' loop bit cleared again (the old behaviour, pack `build/grunt/sword-base`):
  `build/ootf-run/grunt-sword-base/run` kills one grunt (hit points -11); no lying grunt appears in its frames and a grunt of
  the killed one's model stands motionless in the same pose over `p2`, `k00`, `k01` (crop `build/grunt/swordbase_z.png`). It
  is not proven from the frames that this standing grunt is the killed one.
* Offline (probe `build/grunt/cmp.ts`): the retargeted `death1` ends lying like the disc's (root up vector (-0.39, -0.13,
  0.91) against (-0.39, -0.14, 0.91)) and now carries flag 1; seated clips put the feet on the ground (`build/grunt/feet.ts`).
* Not seen in game: a seated, dice or lookout grunt (those clips play from waypoints and scenes; the scene actors are skipped by
  the harness), Wartok Canyons, a converted creature's death (Scavenger).
* Tests: formats `ootfretarget` (fold, absolute root), `ootfcreature` (absolute root, first key as base), `ootfanim`
  (flags read and rewrite), `ootfpipeline` (`deathClipIds`); editor `npm test` 1203 passed, `python scripts/run_tests.py` 401
  passed.
