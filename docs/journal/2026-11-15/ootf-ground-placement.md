# Order of the Flame, Ruined Village: objects off the ground (owner items 3, 6, 11)

Owner report: (3) in the opening scene the big Wartoks float a bit above the ground; (6) some trees float, not in the
ground; (11) the villager by the Scavenger near the old man's house looks half buried. Scratch, tools, packs, runs and
frames are under `build/review/ootf-placement/` (not tracked); nothing from either game is quoted here.

## Method

Offline first (`tools/*.ts` on the formats sources): PC object position against the PC terrain under it, the converted
pack's object position and terrain, the converted models' vertex range, the PC and disc rigs posed in every clip the
scene plays. Then real runs of the game (private play folder `play/`: `build/final-game/bin` of `24aa9c51`, the owner's ini
windowed with HD textures and DLSS off, private ELF): the opening played through on New Game (`play_run.py
--keep-cutscene`, a shot every second), and direct level starts with placements for the trees and the body
(`tools/look.py`, `tools/slave.py` on `scripts/harness/live_drive.py`), reading the objects' positions back from guest RAM.

## (3) Scene Wartoks: the scene keys reset their scale (fixed)

* Scene 1.1 (`Ruined Village.ssd`, sequence 381) uses two Wartoks whose level objects (3030, 3031, class Wartock Chain)
  are placed at scale 1.5. From 14.1 s to 51.4 s their transform keys hold them at 822 and 796 over the grass: 1.5 times
  the 530 (548) of a Wartok at scale 1, so the PC scene stands the 1.5 body on the ground.
* A PS2 transform action is the PC one plus a scale (bytes 42..53, `sx sy sz` in `story/ssdactions.ts`). The converter
  wrote 1, 1, 1 there (`convertAction`), so the engine drew both at scale 1 for the whole scene: smaller than the
  level placed them, with their feet about 250 units over the grass at the 822 / 796 keys.
* The disc Wartok rig is not the cause: posed in every clip the scene plays (idle, ready, taunt, walk, run, talking,
  scratch head, signal), the retargeted disc model's lowest vertex is within 20 units of the PC model's (about -545 to
  -590 under the origin in both).
* Fix (formats `ootfseq.ts`, `ootfpipeline.ts`): `SequenceMaps.scaleForActor` gives each actor its level object's scale,
  written into every transform key of that actor (1, 1, 1 when the object has none, and for the player and Arokh as
  before). In the whole campaign only these two objects are scene actors with a scale other than 1
  (`tools/scaled_actors.ts`), so no other scene changes. Test: `ootfseq.test.ts`, "transform key scale".
* In the game (single-level packs of the same tree, before `packs/rvcur`, after `packs/rvfix1`; runs `op_cur`,
  `op_fix1`): `runs/pair_056.png` (left before, right after) shows the two big Wartoks behind the spear line: before,
  small and with their feet in the air; after, 1.5 times as large and standing on the grass. The pack's SSD carries
  ten keys with scale 1.5, the before pack none.
* Open: from 51.4 s to 54.9 s the PC keys put the same two at 530, the height of a scale 1 Wartok, while they run off;
  at 1.5 that is about 330 under the grass on the PC too. The camera is overhead or close on other actors then
  (`runs/op_fix1/o057..o060`); no sinking was seen, but it was not looked for in a frame that shows their legs.

## (11) The dead villager by the Scavenger: not a converter fault found

* The body is `male slave` 2563, placed dead (Initial Health 0), hidden until detector 2710 shows it and Scavenger
  2570. The PC level places it 399 over the ground; a standing slave is placed 527 over it.
* In the game, while hidden it stays at the placed height; once shown, the engine stands it on the ground itself: read
  back at 528 over the ground (264506, 19298, 239931; it slides about 290 units down the slope first), in both packs.
  A converter change that raised the placed corpse to standing height (tried, `packs/rvfix2`) ended at the same
  position, so it was reverted.
* What shows (`runs/slave_before_after.png`, `runs/sl4_zoom_v1.png`, Scavengers dropped from the evidence packs with
  `DRAKAN_DBG_DROPNAME` so the body is visible): the slave lies in his death pose (death clip's last frame, 381 to 532
  under the origin, so the body's underside is about 4 units under the ground at the origin), on a slope that rises
  about 600 units over one 2048 unit cell. The body is flat, the ground is not, and his head and shoulders go into the
  uphill side. Whether the PC tilts a corpse to the slope was not established (no PC run here).
* Open: a fix would have to tilt or lift the lying pose to the slope at run time (the engine overrides any placed
  height when the body is shown); not attempted.

## (6) Trees: no conversion fault; three trees float in the PC data

* Terrain: the pack's terrain equals the PC terrain at all 151 tree positions (largest difference 0.0); positions and
  scales are copied unchanged.
* Models: the Drakan 2 pine stretched onto the OotF tree spans exactly the OotF tree's height (-205 to 3697 units for
  the large pine, the trunk's bottom ring at -205 in both).
* In the game a tree the PC data roots sits on the ground (small tree 397, unscaled, origin 205 over the grass:
  `runs/base9_vs_cur_t397.png`, 9 November pack left, current right).
* PC data, base of the drawn trunk against the PC ground under the origin (`tools/flags.ts`, `tools/objs.ts`): tree 60
  (Large Tree, scale 2, 335424, 208224) 158 above, MedTree 1845 (scale 0.7, 270379, 205952) 61 above, Small Tree 2661
  (scale 1.5) 7 above; no object under any of them. Tree 60 floats the same in the 9 November pack (OotF tree model)
  and the current one (`runs/base9_vs_cur_t60b.png`). Every other tree's base is at or under the ground at its origin;
  unscaled trees placed with the base exactly on the ground float on their downhill side on slopes, as the PC data
  places them.
* Owner's decision (same day): ground the floating trees in the converter anyway.

### Tree grounding (formats `ootftreeground.ts`, stage `tree grounding` in `ootfpipeline.ts`)

* A tree is a ground object (engine class 17) named as a tree, pine or palm (Large Tree, MedTree, Small Tree, SmallTree,
  the snowy kinds, Fan Palm ...), not one with a PC `Push Over Mode` (the push tree) and not a stump, base, log or dead tree
  (`isGroundableTree`).
* Its trunk footprint is taken from its PC model: the lowest point and the vertices within 5 % of the model's height of it,
  plus their centre (`trunkFootprint`), turned and scaled with the object.
* When the trunk base is over the terrain at every footprint sample (by more than 1 unit), the tree drops until the base
  meets the lowest sample, so no side floats and the uphill side sinks a little (`treeGroundHeight`). A tree that touches
  the ground anywhere under its trunk stays; a drop over 1200 is refused (it would stand on something else). Every moved
  tree is logged (`tree grounding <level>: n tree(s) lowered onto the ground: <id> "<class>" down <units>, ...`).
* On by default; `DRAKAN_TREE_GROUNDING=off` (or `0`) or the pipeline option `treeGrounding: false` keeps the PC heights.
  Tests: `test/ootftreeground.test.ts` (synthetic models and terrain).
* All 23 levels (`packs/full1`, full campaign conversion with the subtitle files and wide chunks): 6 trees in 3 levels.

  | Level | Trees moved (id, class, drop) |
  | --- | --- |
  | Ruined Village | 60 Large Tree 193, 1845 MedTree 61, 2661 Small Tree 8 |
  | Alwarren | 1825 SmallTree 52 |
  | Islands Night | 1507 Fan Palm 01 118, 5224 Fan Palm 01 158 |
  | the other 20 | none |

  Largest drop: 193 (Ruined Village tree 60; it is 193, not 158, because the lowest terrain under its trunk ring is
  lower than under its origin).
* In the game (runs `tr_before` on the dist pack, `tr_after` on `packs/full1`, the same placements;
  `runs/trees_before_after.png`, left before, right after): tree 60's trunk ended in the air before and meets the grass
  after. Read back from guest RAM: tree 60 at 20016 then 19823, 1845 at 20205 then 20143, 2661 at 20327 then 20319,
  the drops the log gives. 1845 and 2661 are small moves (61 and 8) and look about the same in the frames. The after pack
  also carries the other session's uncommitted formats changes (black shadow patches in some after frames come from that
  pack, not from the grounding, which moves only these objects).

## Changed files

Formats submodule (uncommitted; `ootfseq.ts`, `ootfpipeline.ts` and `ootfseq.test.ts` also hold another session's
uncommitted work; `build/review/ootf-placement/scene-scale-and-trees.patch` lists only the hunks of this change):

* `src/ootfseq.ts`: `SequenceMaps.scaleForActor` / `scale` and the scale written into transform keys (item 3).
* `src/ootfpipeline.ts`: `scaleForActor` for scene actors (item 3); the `treeGrounding` option, the `ootftreeground.ts`
  import and the `tree grounding` stage before `harmless fires` (item 6).
* `src/ootftreeground.ts` (new), `test/ootftreeground.test.ts` (new): tree grounding and its tests.
* `test/ootfseq.test.ts`: "transform key scale".

`npx vitest run` in `editor/packages/formats`: 68 files, 721 tests passed; `tsc --noEmit` clean; prettier clean on the
changed files (run from `editor/`).
