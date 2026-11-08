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

## The "missing arm" (first pass)

Not found in the first pass; see the second pass below. On the grunts the arm joints are matched one to one (`rshoulder` .. `rhand`, `rweapon`, the left side) with equal
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

## Placed corpses stood upright (fixed, formats `6ab2b0f`)

Ruined Village places one grunt already dead (object 2798, `Initial Health` -50), and Ruined Village and Wartok Canyons place
dead slaves (`Initial Health` 0); the disc never places a dead creature. The PS2 engine spawns such an object dead (no hit
points, out of the AI lists) without playing any death clip, so it stood in its idle frame, with and without the loop fix
(`grunt-single-turn`, `grunt-single-base-turn`: both orcs upright). Such an object's `Base Idle` is now the class's
`Idle Death` clip, else its `Death` clip (which plays once and holds), and its spontaneous idles are cleared
(`placedCorpsePose`; 2 objects in Ruined Village). Run `build/ootf-run/grunt-corpse/run` (`t00` to `t05`): the placed dead
grunt lies on the ground beside the living one for the whole run.

## Second pass on the arm (after "still don't always show their arm")

* The working tree now converts the orcs with the disc's own models and clips by default (another session's
  `--enemy-animations drakan2`, formats `d18eb87`), so these runs show the disc's look.
* Combat burst, 73 shots 200 ms apart (`build/ootf-run/grunt-arm/run`, crops `build/grunt/purple_z.png`): a trident grunt
  seen from behind shows its right arm holding the trident and a left arm that ends at the elbow, held still for 2.4 s.
* The original game, same class: `build/grunt/ps2/ps2center` (Drakan 2 Surdana, the PS2 `Purple Orc Center` object 5002,
  Rynn placed next to it with `DRAKAN_PLAYTEST_POS`; crop `build/grunt/ps2center_z.png`). From behind it looks the same: the
  trident in the right hand, the left arm ending at the elbow. The trident stance holds the left forearm and hand in front of the
  belly (forward kinematics of the disc's `ready`: left hand 0.126 in front of the root and 0.059 to the side, inside the
  torso's width), so from behind the body hides it. This is how the disc model and clips look, not a conversion loss; a frame of
  the forearm from the front was not captured.
* Distance: the second LOD of models 4395 / 4396 (100 vertices, from 12,000 units) weights both arms alike (3 and 6 vertices on
  each upper arm and forearm).
* Not reproduced: an arm missing from the front, in an attack or hit clip (the burst caught the grunts swinging pickaxes and
  tridents at each other with both arms), or on the Wartok Canyons trolls (PS2 axes, not spears; not run). The opening scene
  does not play on a direct start of Ruined Village even with `DRAKAN_AUTOPRESS_STOP_AFTER_LOAD=1` (`grunt-open2`), so the
  scene Wartoks are still unseen.

## The missing arm found: converted creatures lost their last polygons (formats `62477e8`)

A New Game run into the Ruined Village ambush (Intro + Ruined Village campaign pack, `DRAKAN_AUTOPRESS=0`, title Enter on New Game
and Solo; frames `build/grunt/runs/camp/s070.png` before, `build/grunt/runs/camp2/s070.png` after, side by side in
`build/grunt/forearm_cmp.png`) shows the scene's spear orcs (the "Heuge" actors: converted PC hueges models, not the
Drakan 2 models) with a hand floating beside the spear and no forearm between it and the elbow.

Cause: a PC skinned model splits its polygons by choppable channel. Each channel cap is
`{stumpFirst, stumpCount, firstPoly, polyCount, joint}`, and the stump polygons (which close the cut once a limb is chopped
off) sit inside the LOD's run. The LOD record's `polyCount` counts the run without them, and the converter took the first
`polyCount` polygons, so the last channels were cut off:

| PC model | LOD 0 run | `polyCount` | lost |
| --- | ---: | ---: | --- |
| hueges 3713 / 3714 (scene spear orcs) | 374 | 361 | the right forearm and part of the right hand |
| troll 3508 (scene Leather Wartok) | 246 | 225 | the right hand and part of the left thigh |
| troll 3534 / 3536 (Chain / Plate Wartok) | 239 | 225 | part of the right shin |
| scavenger 3773 | 332 | 323 | most of the left forearm |

`skinnedLodPolygons` now takes the run up to the next LOD's first polygon (or the last channel cap) and leaves the stump caps
out (they also showed through the body before). After the fix the same frame shows every spear orc's forearm joining the upper
arm to the hand.

## The "headless" scene Wartoks are their real shape

The ambush frame reported as a headless Wartok (`build/ootf-run/title-art/rv_71s_wartok.png`) and the same Wartoks in
`build/grunt/runs/camp2/s070.png` were checked against an offline render of the converted Plate Wartok, posed with its scene
idle and skinned exactly as the PS2 data says (`build/grunt/r_all.png`, `r_views.png`, head polygons red). The OotF troll's
head is not above the shoulders: it hangs forward and low between them, at chest height, in the authored bind pose as well. From
the front it is the oval between the shoulders that looked like a neck stump; from behind the back hides it completely.
Geometry, weights, display list (225 of 225 triangles, the 85 near head height all present) and the head joint's motion are
intact, so nothing is dropped there. The hand the Leather Wartok lost is restored by the fix above.

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
