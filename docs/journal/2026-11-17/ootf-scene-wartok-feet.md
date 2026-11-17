# OotF scenes: the Wartoks' feet off the ground (2026-11-17)

Owner report (Order of the Flame campaign): "The Wartock grunts (bigger ones) are still floating a bit in the cinematics."
Follow-up to `2026-11-15/ootf-ground-placement.md` section (3) (the 1.5 scale kept in the scene keys). Scratch, probes, packs,
runs and frames are under `build/review/wfeet/` and `build/ootf-run/wfeet-*` (not tracked); nothing from either game is
quoted here.

## Method

* Offline (`build/review/wfeet/*.ts` on the formats sources): every PC scene of the campaign whose actor is a Wartok level
  object (`survey.ts`); the lowest foot vertex of the PC Wartok models and of the disc Wartoks (NPCS 4418 / 4422 / 4427)
  posed in the clips the pipeline retargets, against the PC key heights over the PC terrain; the clips the converted pack
  actually holds (`packclips.ts`).
* In the game: `scripts/harness/ootf_level_run.py --campaign "Mountain World/Ruined Village/Ruined Village.lvl"` with
  `DRAKAN_AUTOPRESS_STOP_AFTER_LOAD=1` (the opening plays out: 1.1, then 1.2a), the owner's `dist/DrakanPC/drakan.ini`
  (the harness overrides window size, DLSS, HD movies and texture upscale), a shot every second and `dump ram_<label>` every
  1 to 4 s. `ents.py` reads every registry object near the scene out of the dumps: position, matrix row lengths (the scale),
  clip id and time (`scripts/harness/live_drive.py` readers). Before = the converter as of this morning (the dist pack's
  code), after = this change; same level, same recipe.

## What is not the cause

* Rig and scale. The engine draws the two scene 1.1 Wartoks (objects 3030 / 3031, scale 1.5) at 1.5 (matrix rows 1.5) at
  exactly the key positions (822 and 796 over the grass). The model is scaled about its origin and the clips' root
  translation is about 0 (the PC Wartok root keys stay within 15 units of the origin), so no offset is scaled wrongly.
  The LODs of the disc Wartoks bottom out within 4 units of each other (-537 / -534 / -533).
* Terrain. The converted terrain equals the PC terrain under every scene Wartok (20000 flat under the 1.1 line-up, 18750 in
  the 1.2a grove).
* The disc model (7411634a). Posed in the same clip, the retargeted disc Wartok's lowest foot is 5 to 18 units lower than
  the PC Wartok's (idle 567 against 562, ready 536 to 543 against 524, talking 568 to 588 against 565): it sinks a little
  more, never floats more.
* Other scaled Wartoks. In the whole campaign only 3030 and 3031 are scene Wartoks with a scale (the Grimstone Mines
  interrogation trolls at 0.9 / 1.1 are gameplay classes, not scene Wartoks, and are not changed).

## The cause

The PC keys hold a Wartok's origin at one height for many seconds (530 over the ground at scale 1; 822 and 796 for the
1.5 pair) while the clips under it change, and each clip carries the feet at another depth under the origin:

| clip (disc Wartok, scale 1) | lowest foot under the origin |
| --- | --- |
| ready, taunt | 543 to 545 |
| idle, WPsignal, WPscratchhead | 570 to 574 |
| talking | 588 |
| alpharun | 595 |
| Victory2 (the model's first clip, played before the first scene clip) | 528 |

So the same key sinks the feet in one clip and lifts them in the next, and three places are clearly off:

* 1.1, 51.4 s on (the line-up runs off): the keys of 3030 / 3031 are 530, a height for a scale 1 body; at 1.5 in
  `alpharun` the feet are 360 under the grass (`final_11_runoff_crop.jpg`, before: no legs).
* 1.2a, 2.6 to 11.9 s and 26.1 to 30.6 s: `Wartock Plate1` and `Plate2` (scale 1) are parked at 849 over the grass while
  they play `alpharun`: feet 255 in the air. RAM: `228824, 19599, 177964` over ground 18750 for the whole 9 s.
* 1.1, 14.1 to 15.1 s: 3031 holds Victory2 at 822 (feet about 30 up) until its first scene clip.
* Everything else is small: the 1.5 pair are 5 to 7 units up in ready / taunt at 822 and 40 to 90 under in idle / talking.

The Heuge orcs of the same scenes (also on disc rigs through the scene rows) have the same pattern (keys at 375 while
their clips' feet are 339 to 437 under).

## Fix (formats `ootfseq.ts`, `ootfretarget.ts`, `ootfpipeline.ts`)

* `lowestFoot` (ootfretarget.ts): per retargeted clip, the lowest vertex whose strongest influence is a foot joint
  (`foot|toe|hoof|heel`) over 13 poses of a cycle, or the first pose of a clip that ends in another stance (feet or body
  more than `FOOT_STAND_SLACK` = 48 apart at start and end: a death, a fall). A clip in which another part of the body hangs
  more than 48 under the feet (lying, sitting, the Wartok death clips) gives nothing, so its keys stay as the PC set them.
* The scene creature rows (`sceneTocks`: the Wartoks and Heuges on disc rigs) keep that value per PC clip (`SceneTock.feet`).
* `settleCreatureFeet` (ootfseq.ts), run per actor by `convertActor` when `SequenceMaps.feetForActor` gives one: every
  transform key under a whole-body clip (channel 0) gets the height that puts the clip's lowest foot, times the actor's
  scale, on the ground; where a clip starts while the actor holds still (the next key has no `0x10`) a key with that height
  is added. Left alone: feet more than `FEET_FLOAT_MAX` (320) over or `FEET_SINK_MAX` (250) under the ground (times the
  scale), keys relative to another actor, keys before the first clip, and keys where the ground is not known.
* Ground (`feetForActor` in the pipeline): the PC terrain, or a mesh under the actor when one is higher: the level's drawn
  scenery and mechanism objects (`sceneSupports`, as the ground snap stage reads them, without trees, plants and hanging
  pieces; up to 10 degrees off upright counts as upright). Under a more tilted object's bounding box the key is left (the
  first version took the Ruined Village large trees' spheres as support and changed nothing in 1.1).
* Switch: `DRAKAN_SCENE_FEET=off` keeps the PC heights. Log: `sequences <level> (<container>): <n> scene creature key(s)
  moved or added to stand the feet on the ground`. Full campaign: Ruined Village 216 keys (scenes 1.1 and 1.2a: four
  Wartoks and seven to nine Heuges each), Volcano 86 (scenes 9.1 and 9.3: Heuges only); no other level has scene creatures
  on a disc rig.
* Tests: `test/ootfseq.test.ts` "settleCreatureFeet" (the 1.1 Wartok at 1.5 through idle, ready and the run-off; no key
  inside a move; the parked 1.2a Wartok put down; unknown ground, far, relative, other-channel and close keys left; applied by
  `convertPcSequences` only to actors with feet), `test/ootfretarget.test.ts` "lowestFoot" (lowest foot over a cycle, not a
  lying body, the first pose of a one-way clip, the whole mesh without foot joints).
* `docs/reference/level-format.md`: what the `0x10` option of a transform key does (seen in RAM: hold then jump without it,
  move from the previous key with it).

## In the game (`build/ootf-run/wfeet-b3` before, `wfeet-a4` after)

RAM, after: 3031 at 861 (idle), 882 (talking), 817 (taunt), 815 (ready), 892 (alpharun); 3030 at 882 / 855 / 861; the run-off
at 892 instead of 530; the parked 1.2a Wartok at 19345 (feet on 18750) instead of 19599.

| Picture (`build/review/wfeet/`) | Seen |
| --- | --- |
| `final_11_runoff_crop.jpg` (68 s) | the 1.5 pair running off: before, cut off at the hips by the grass; after, whole legs on the grass |
| `final_11_lineup.jpg` (36, 49, 64, 66 s) | the line-up: the pair stand 40 to 85 higher where they sank (idle, talking); feet mostly hidden behind the orcs, the visible ones on the grass in both |
| `feet_pairs.jpg` | 3031 in taunt at 64 to 66 s: one foot on the grass, the other lifted by the clip itself (same in both) |
| `orc_pairs.jpg` | front-row Heuges: feet on the grass in both, a little higher after |
| `final_12a_park.jpg` (85 to 88 s) | the 1.2a grove: no visible change; the parked Wartok is not in these shots |

## Open

* Not seen in a frame: the parked 1.2a Wartoks (the camera of 2.6 to 11.9 s and 26.1 to 30.6 s shows other places in the
  shots taken); the Volcano scenes 9.1 and 9.3 (86 keys, counts only).
* The red Wartok with a raised spear in the 1.2a grove (85 to 90 s) is not a scene Wartok key (unchanged by this fix, its
  object plays level clips); whether it stands on the grass was not settled from the overhead shots.
* 3031 still holds Victory2 (feet about 30 up) for the first second of its 1.1 part, before its first scene clip; the
  engine plays the model's first clip there. Not changed.
* The Heuge `deathrear` clip ends 155 under the stance it starts from; the PC key keeps the start, so the corpse's end pose
  sinks into the grass until `deathidle` (whose key is now on the ground). Not changed.
* The PC plays these scenes with the same keys; whether its engine moves scene creatures onto the ground by itself was not
  checked (no PC run).
