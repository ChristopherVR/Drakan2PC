# Order of the Flame: Delon's model and mouth, Rynn's ponytail, the miners' shoulder piece (2026-11-15)

Owner report, Ruined Village and its scenes: (1) Delon's model looks really bad and his mouth does not move when he
talks; (2) Rynn's ponytail sometimes goes horizontal when she just walks, even in scenes; (10) the miners with pickaxes
wear a shoulder armour piece they should not have. Scratch, tools, packs, runs and frames are under
`build/review/ootf-models/` (not tracked). Nothing from either game is quoted here, only class, model and field names.

Play folder: `build/review/ootf-models/play` (a copy of the `ps2-models-off` play folder, the owner's `drakan.ini` with a
private `elf =` line, `build/final-game/bin/drakan.exe` as of 11:00, md5 `c14c9d9d...`; the same binary for every
before / after run). Packs:
* before = `dist/DrakanPC/mods/ootf-levels.zip` (`cur`), the pack the owner plays;
* `base1` = formats `d5f9ba6` (HEAD), unchanged;
* `fix1` / `fix2` = the same tree with the class change below; `fix3` = fix2 plus the Intro change (section 2), the
  final pack.

`base1` and `fix` were converted with `tools/conv.sh`: the converter command of `extras.py ootf` without its cover and
HD-source flags, plus the two subtitle files. They hold 12,211 files against the dist pack's 12,223, so only `base1`
against `fix2` is a like-for-like diff.

## Summary

| Item | Cause | Changed | Seen in game |
| --- | --- | --- | --- |
| 10 miners' shoulder piece | Template inheritance: the mine slaves are cloned from a Drakan 2 Green Trog class, and the trog's own left claw ("Trog HandClaw_L", Left Weapon Object) stayed in the record on joint 18. Joint 18 is the right upper arm of the slave rig. Not the 12 November weapon child-part change. | formats `ootfclass.ts` | yes, before and after |
| 1 Delon, model | Not a conversion fault. The converted model is the PC model: one LOD of 265 vertices and 526 triangles, smooth normals, the same five textures, and the HD pack plays no part. The one difference is the fringe's graded alpha, cut to on/off for every skinned texture since the Intro owl. | nothing | yes (HD on / off) |
| 1 Delon, mouth | The PC game never moves a human's mouth. No PC character rig has a jaw joint, and `Jaw Channel` exists only in the dragon class (bite) and the acid-spitting creature class. On the PS2 side the scene Delon is a generic prop (engine class 17), which has no `Jaw Channel`. | nothing (enhancement options below) | his mouth does not move, as on the PC |
| 2 ponytail | Two things. (a) A regression of the 9 November fix, found on the campaign path through the Intro at world_rate 30: the converter detached Rynn from Arokh on the same scene tick as the GUI trigger that ends the Intro. When the trigger won, Ruined Village started with her "attached" bit set and the ponytail simulation never ran, so the ponytail stood out as the rigid rest-pose bar for the whole level. Uncapped it won by 0.2 s. (b) Walking or running raises the ponytail to 60 to 90 degrees in Drakan 2's own Surdana as well, at 30 Hz and uncapped. That is the engine's own simulation. | formats `ootfseq.ts`: detach 2 s ahead of the trigger | (a) RAM, 30 Hz and uncapped, before and after; frames below; (b) RAM, 8 runs |

## 10. The miners' shoulder piece

The Ruined Village "male slave" / "male slave2" (engine class 49, PC model `slaveM (h)`, 42 joints) are cloned from a
Drakan 2 class with the same field list. Read back from the pack (`tools/packcls.ts`, `tools/packscan.ts`):

* `Left Weapon Object = 141763`, which is disc RESOURES class 10691 "Trog HandClaw_L" (engine class 14, a weapon model).
  Its `Left Weapon Channel` is 18.
* The PC slave class has no left weapon field at all; its own `Weapon Object` is "slave pick" on channel 21.
* Disc NPCS classes with exactly that pair: "Green Trog : Flanker / Center", "Red Trog : Flanker / Center" and
  "Old Copy of Toadfist AI". `mergeDefaults` keeps every template field the PC class does not supply, so the trog's claw
  came along.
* On the converted slave rig, joint 18 is `Ruparm`. The engine hung the claw's golden spikes on the miner's right
  shoulder (frames below).

Not the 12 November change: the slave records and "slave pick" have 0 in the auxiliary reference (body +10), and the
9 November baseline pack (`build/review/baseline-play/mods`) already has the claw on both slave classes.

**Fix** (formats `src/ootfclass.ts`, `mergeDefaults` -> `dropTemplateAttachments`, `attachmentChannelName`):
* Applies only to a creature that keeps its converted PC model and rig (`copyChannels`).
* Clears every template object field that attaches something to a joint, unless the PC class supplied it:
  `<X> Object(s)` with a matching `<X> Channel(s)` (Weapon, Left Weapon, Shield, Body Attachment), plus
  `Head attachment object`. The paired channel becomes -1, or an empty list. A template field that attaches nothing
  (object 0) is left as it was.
* Object fields without a joint of their own ("Sound Effect Object", "2D Explosion Object", generators) are untouched.
* Creatures shown with a Drakan 2 model and rig keep the template's attachments, which belong to that body.
* Test: `ootfclass.test.ts` "drops the template creature's own attachments from a converted rig".

**Pack diff** (`base1` against `fix2`, same formats tree but for the change): 3 class records out of 12,211 files differ,
plus `mod.toml` (build stamp). Every other record and every level is byte-identical. (The first form, `fix1`, also turned
the channel of an empty attachment from 0 to -1 in Goblin, Goblin Grenadier, Navy Hueges (purple) and male slave3; the
final form leaves those records as they were.)

| Class | Change | Effect |
| --- | --- | --- |
| male slave, male slave2 (Ruined Village) | Left Weapon Object 141763 -> 0, channel 18 -> -1 | the claw is gone |
| Ebon Knight (Volcanic) | Body Attachment Objects: 8 parts of the disc "Savage Leader - Maulgak" -> none, channels (clamped to 8, 24, 0, 25, 9, 0, 0, 3) -> none | Maulgak's armour parts no longer hang on the converted knight's joints; **not seen in game** (see Open) |

The third slave class ("male slave3", Wartok Canyons) is cloned from another template without a claw and is unchanged.

**In game** (`tools/look.py` placements by the Ruined Village slave 2529, god mode, direct level start, runs
`slaves_cur` / `slaves_fix1`): before, two to three golden claw spikes stand out of the miner's right shoulder in every
view. After, the miner shows the pickaxe only. Frames: `evidence/miner_claw_before_after.jpg`. Slave 2563 was not in
view at its placed spot in either run (it walks a waypoint path).

## 1. Delon

### The model

Offline (`tools/pcskel.ts`, `tools/pose.ts` + `tools/prender.py` from the harpies round, `tools/nrm.ts`):

* **PC model `Delon(h)`** (NPCs 3727): one LOD (265 vertices, 526 polygons), 37 joints and five textures (face 64,
  hair 64, shirt 256, legs 128, fringe 64 with 16 alpha levels).
* **Converted model** (pack 7504): 268 vertices and the same 526 triangles, shading 0x20002, the same five textures.
  Posed PC and converted side by side, walk and the walk-talk clip from three sides, they are the same picture
  (`scratch/delon_render.png`).
* **Normals:** the stored vertex normals agree with the area-weighted smooth normals of the welded mesh to 1.2 degrees
  on average. The sign is opposite, which is the disc convention. Only two vertices sit on a seam. No faceting from
  lost smoothing.
* **Textures:** identical texel for texel except the fringe. Its 16 alpha levels became 2, because every skinned texture
  is cut at 50 % (`hardAlpha`, needed since the Intro owl: blended creature polygons do not write depth).
* **HD textures** play no part: frames of the same scene shots with `DRAKAN_HD_TEXTURES=0` and with the HD pack look the
  same (`evidence/delon_hd_on_off.jpg`).

In game (`runs/rv_open_before`, `runs/rv_hdoff`, Ruined Village opening): Delon is the 1999 model at 526 triangles
beside the far denser Drakan 2 Rynn. You see a faceted nose, chin and shoulders, a flat 256 pixel shirt, and the
fringe as hard dark strands. Strong warm scene light washes out his shirt and face, and Rynn's too. That is what the
owner sees, but it is the PC model as authored, not a damaged conversion. Whether the PC renderer lit him less brightly
was not compared (the PC executable was not run).

Options if the owner wants him to look better than the PC (none done; each is a look change, not a repair):
* A refined draw mesh with smooth subdivision, as the PS2-style prop rows do (`refineMesh`). That needs a skinned
  variant, which the converter does not have yet.
* A softer fringe: a lower alpha cut, or alpha-to-coverage in the renderer.
* A Drakan 2 human model with a mouth. That would need its rig, clips and a class change. No disc model of him exists.

### The mouth

* **PC:** no human character has a jaw. Delon, Rynn (Banded and Plate), Julian, the villagers, the farmer and the
  warrior have no jaw or mouth joint. The PC engine's class schemas (`Dragon.rfl`) list `Jaw Channel` only twice, in
  the dragon class (beside Saddle Channel and Bite Damage) and in the acid-spitting creature class (Acid Missile,
  Fist Vertex). Delon's head is 47 rigid vertices on one joint and his mouth is painted. His scene speech plays body
  gestures (`1_1walktalk`) and nothing else. On the PC his mouth never moved.
* **Converter:** scene speech gets the PS2 lip-sync bit (0x20) only for actors that resolve to the player
  (`lipSyncForActor` in `ootfpipeline.ts`). The 0x0305 tracks exist for every converted speech clip.
* **PS2 engine:** the scene sound action with bit 0x20 sends message 0x31 to the actor (`0x1a4c50`, case 3), which a
  class with a `Jaw Channel` binds to that joint. On the disc that field exists in engine classes 9, 28, 40, 41, 49,
  59, 68, 82, 157 (the Drakan 2 villagers and talkers, 80 classes), 173, 176, 188, 189 and 193, never in class 17. The
  scene Delon of the Ruined Village is a class 17 generic prop (PC class "delon"). That class 17 ignores message 0x31 is
  inferred from the missing field: only the sender was decompiled, not the receivers. Either way his rig has no joint
  to turn.
* **Making him talk** is an enhancement over the PC original, with two parts:
  * a synthetic jaw joint in the converted rig, with the lower-face vertices re-weighted to it;
  * a scene class that binds lip tracks: the Drakan 2 talker class 157, or the class 82 that his Rift World boss clone
    already uses. That means a different engine class for a scene prop.
  
  Not done in this round; owner's decision.

## 2. Rynn's ponytail

The ponytail is a vertex cloth simulation in the engine (handle `+0xb4` state, tick `0x11d368`, called from the skinning
pass `0x116990`; [rig audit](../2026-11-09/ootf-rynn-rig-audit.md)). Decompiled (`dec/0011d368_*.c`):
* It integrates 18 particles with springs, damping and gravity by explicit Euler.
* The step is the world tick `0x4de604`, capped at 0.03 s, with 1/dt from `0x4de608`.
* It drags the particles with the object's own velocity, measured as (position - last position) x 1/dt.
* The skinning pass skips it while the handle's "attached" bit (`+4` 0x80) is set. Until it has run once, the simulated
  vertices are drawn at their raw rest positions, a rigid bar standing out behind the head.

Measured from RAM, read only (`tools/hairmon.py` beside `play_run.py`, every 30 ms): the handle bit, the cloth state
flags (bit 0 running, 0x10 teleported / not started), all 18 particle positions, and Rynn's position. The angle used
(`tools/hairspeed3.py`) is that of the three tip particles (12 to 14) against the six top ones (0, 1, 2, 15, 16, 17),
from the model's vertical: 0 = hanging, 90 = horizontal. At rest it is 12 to 17 degrees.

### (a) The Intro hand-over: a race the converter lost (fixed)

Intro 3 (PC seq 452):
* Arokh puts Rynn on his saddle at t = 0 and has his last transform key at 13.34 s.
* Its last action is the "Gui Trigger" message at 16.386 s, which the PC level ends with.
* The converted Intro ends on a level Timer instead, `INTRO_SECONDS` = 41.4 s of level time (`ootfcampaign.ts`). That
  is the sum of the three scene lengths, so it fires when Intro 3 ends only if the scenes start on time.
* `detachHeldPlayer` placed the detach at the scene's last action, 16.386 s, so it raced the Timer. In both 30 Hz runs
  the level changed at the same 67.89 s, but Intro 3 started 5 s later in the failing one (attach at 51.5 s against
  46.5 s).

Campaign start through the Intro (`DRAKAN_OOTF_INTRO_MOVIE=0`, keep-cutscene), fix2 pack:

| Run | Detach seen | Ruined Village player handle | Cloth state in Ruined Village |
| --- | --- | --- | --- |
| uncapped (`hair3_intro_uncapped`) | 48.97 s, level change at 49.19 s (0.2 s margin) | bit 0x80 clear | 0x1 (running) in 9,815 of 9,829 samples |
| world_rate 30 (`hair3_intro_30`) | never: the level changed with the bit set | **bit 0x80 set** (`0x1b0810c9`) for the whole level | **0x10 / 0x0: never started**, 10,097 samples |

The 30 Hz case is exactly the 9 November bar, back through a timing race: the frozen rest-pose ponytail for all of
Ruined Village, scenes and gameplay. A slow frame on the owner's machine at the end of the Intro would do the same at
any rate. That fits "sometimes".

**Fix** (formats `src/ootfseq.ts`, `detachHeldPlayer`): the detach goes `DETACH_LEAD` = 2 s before the scene's last
action, but never before the carrier's last transform key (so she is not left behind while he still moves) and never
after the end. For Intro 3 that is 14.386 s, after Arokh's last key at 13.34 s, under the title and owl shots. From the detach to the
level change her position stays constant to the unit (`hx/hy/hz` in `hair4_intro_*`): she does not fall. While carried
she bobbed by about 15 units with his hover. The last seconds of the Intro (runs `intro_end_fix2` / `intro_end_fix3`, uncapped, a shot every second up to the
loading screen) are the wide owl shot with neither Arokh nor Rynn in view, the same frames in both packs
(`evidence/intro_end_fix2_top_fix3_bottom.jpg`). Test:
`ootfseq.test.ts` "detaches the held player ahead of the action that ends the level". The fix3 pack differs from fix2
only in that one sequence record (`LEVEL1ON.SSD` 0311 63).

After (fix3):

| Run | Detach seen | Ruined Village handle | Cloth state |
| --- | --- | --- | --- |
| world_rate 30 (`hair4_intro_30`) | 61.34 s, level change 67.89 s | bit clear | 0x1 running (0x11 at scene cuts) |
| uncapped (`hair4_intro_uncapped`) | 47.94 s, level change 50.36 s (2.4 s margin) | bit clear | 0x1 running |

The margin is now 2.4 s uncapped and 6.5 s at 30 Hz, against 0.2 s and lost before. It is not immune: the Timer runs on
level time, and an Intro 3 that starts more than about 2 s later than in the failing run would still lose. The robust
form is the Book's: Intro 3 takes the exit itself at its last action (`endMessages`), after the detach, and the Timer
becomes a late fallback. That touches `ootfpipeline.ts` (another session's uncommitted work) and the runtime's exit
handler; not done.

Frames: `evidence/intro_handover_30hz.jpg` (runs `intro_vis_fix2` / `intro_vis_fix3`: the Ruined Village opening after
the Intro at world_rate 30, same shot times).
* Before: in the 1.1 crouch (100 s) and the kick (112 s) the ponytail is a straight rigid bar standing out behind her
  head.
* After: it hangs down her back and lies on her shoulder.
* In the walk seen from above (80 to 82 s) it trails behind in both. That is part (b).

### (b) Walking: the engine's simulation, the same in Drakan 2

Median angle per ground-speed bin (standing < 30 u/s; slow 100-700 u/s, the scene walk; walk 700-1500; run > 1500):

| Run | Standing | Slow | Walk | Run |
| --- | --- | --- | --- | --- |
| Drakan 2 Surdana, world_rate 30 (`hair3_d2_30`) | 14 | 53 | 79 (96 % > 60) | 85 (93 %) |
| Drakan 2 Surdana, uncapped (`hair3_d2_uncapped`) | 14 | 67 | 85 (98 %) | 85 (94 %) |
| OotF Ruined Village after the Intro, 30, fixed (`hair4_intro_30`) | 15 | 65 | 81 (88 %) | (13 samples) |
| OotF Ruined Village after the Intro, uncapped, fixed (`hair4_intro_uncapped`) | 17 | 80 | 85 (93 %) | (18 samples) |

* In Drakan 2's own Surdana, at the native 30 Hz, the ponytail hangs at rest and is held out behind or to the side,
  60 to 90 degrees, as soon as Rynn walks or runs. Frames: `evidence/ponytail_d2_and_ootf.jpg`.
* OotF matches Drakan 2 at the same cadence.
* Uncapped raises it further at slow speeds than 30 Hz does (median 67 against 53 in Surdana, 80 against 65 in the
  Ruined Village scene walk). So there is a frame-rate component, but it is the same in both games and walking is
  already horizontal at 30 Hz.
* Standing samples above 60 degrees: 1 to 2 % in Ruined Village (turns, scene cuts). The crouch in scene 1.1 is fast
  body motion within the spot. 14 to 21 % in Surdana, where the standing bin also holds turning on the spot.
* Screenshots cost about 100 ms each at 3840 x 2160 and kick the simulation (one-frame velocity spikes). The measured
  runs took none.

Not established: whether the real console holds the ponytail out like this. No PCSX2 or hardware footage was compared,
so a recompiler fault common to both rates cannot be excluded. A runtime limit on the drag (the rope hook is a
precedent) would change the native look; owner's decision.

## Files

* formats:
  * `src/ootfclass.ts`: `attachmentChannelName` and `dropTemplateAttachments`, called from `mergeDefaults`;
  * `src/ootfseq.ts`: `DETACH_LEAD` in `detachHeldPlayer`. That file also holds another session's uncommitted work;
    this change touches only that function and its comment;
  * `test/ootfclass.test.ts` and `test/ootfseq.test.ts`: one new test each.
  
  Formats suite 69 files / 722 tests (the shared tree, with other sessions' work), `tsc --noEmit` and Prettier clean.
* Main repo: this note only. No executable change.
* Final pack: `build/review/ootf-models/packs/fix3/ootf-levels.zip`. Against `base1` (same formats tree, no change) it
  differs in 3 class records and 1 sequence record.

## Open

* The Ebon Knight change (Maulgak's eight armour parts removed) is still not seen in game. Volcanic started directly on
  `base1` and on `fix3` loads and runs with a clean scripted exit (runs `knight_base1` / `knight_fix3`, four placements).
  No knight was in view at the placed spots in the dark (`evidence/ebon_knight_before_after.jpg`).
* Slave 2563 (Ruined Village) was not looked at after the fix; its class record is the one checked (male slave). The
  Wartok Canyons slave class (male slave3) is unchanged.
* Delon's look against the PC renderer (brightness) and real-console footage of the ponytail were not compared.
* Intro hand-over: the robust fix (Intro 3 takes the exit itself, the Timer as a fallback) is still to do, see 2(a).
* Existing saves: the Intro fix needs a new game. A save made in Ruined Village after a lost race may carry the stale
  attached bit through later levels (the 9 November open item, still untested).
* Walking ponytail: the owner plays uncapped, where the slow-walk angle is measurably higher than at 30 Hz (Surdana 67
  against 53 degrees; Ruined Village scene walk 80 against 65). Unchanged here: it is the engine's simulation.
* Needs a regenerated pack (`extras.py ootf`) to reach `dist`.
