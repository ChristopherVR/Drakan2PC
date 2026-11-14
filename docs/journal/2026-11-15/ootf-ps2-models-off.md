# Order of the Flame: rope bridges, hanging skeletons, bodies and palms in Drakan 2 style (round 8)

Owner request: continue the Drakan 2 / PS2-style models for the props left off in round 7
([Drakan 2 models](../2026-11-14/ootf-d2-models.md), sections 4, 6 and 7): tall and short palms, the hanging skeleton,
rope bridges and broken bridges, bodies on spikes and in cages. Scratch, tools, packs and frames are under
`build/review/ps2-models-off/` (not tracked). Nothing from either game is quoted here, only class and model names.

## What was wrong (why they were left off)

* **Hanging skeleton:** the Drakan 2 "Skeleton 04 - Hanging from chain" hangs from its own cable and has its origin at
  the chest, about 0.6 units below the OotF chain point, so the size and origin check refused it.
* **Bridges:** the Drakan 2 span pieces of the same name are no more detailed than the OotF ones; stretched onto them the
  bridge only got darker planks.
* **Bodies:** no Drakan 2 model of the same name or pose was found, and smoothing the OotF meshes looked the same.
  Round 7 missed WORLDCON's rigid dead men "Deadguy 01" to "Deadguy 03" (a legless torso, a torso with arms, a whole
  hanging man; 320 to 840 triangles against 100 to 190), which are what this round uses.
* **Tall and short palms:** the fan palm builder treats every frond polygon as one flat card, but these palms' fronds are
  already curved strips of several triangles; with it they came out thinner, not fuller.

## What changed (formats `ba19875`, editor app)

Every model is still collided with as the converted OotF model: the builder draws a new strip display list over new
vertices appended after the OotF ones, and the OotF polygon (0x0204) and bounds (0x0207) records stay byte for byte.
Disc textures are copied byte for byte, so the owner's Drakan 2 HD textures replace them.

* `ootfpropcompose.ts` `composeBody` (row field `compose`): OotF parts named by their texture-list entries are kept
  (gallows beam and rope, cage and chain, spike, the skeleton's chain) and the rest is replaced by a disc model, turned
  by `yaw`, scaled uniformly to the replaced part's height and placed on its box centre, or with `align: 'top'` hung by
  its topmost point from the replaced part's top. `bend` folds the disc model forward about a level axis, blended so the
  bend has no crease (the dead man folded over the spike).
* `ootfbridgemodel.ts` `bridgeMesh` (row field `bridge`): every connected flat walkway sheet (walkway texture, faces
  turned within about 8 degrees of each other) becomes separate boards, one fifth of the walkway width apart with gaps,
  each board's top corners on the OotF walkway surface and no wider than it, with two round rope stringers under the
  board ends. Each flat rope strip becomes a 6-sided round rope. Broken pieces hanging down and posts stay flat and are
  drawn with the matching Drakan 2 texture (LEVEL2RE "SusBridge02" carries Drakan 2 versions of the OotF plank, post and
  rope pictures with the same layout). New triangles follow the disc winding: clockwise seen from their normals.
* `ootfpalm.ts` `palmCrownMesh` (refine mode `crown`): trunk rounded (`refineMesh` `round`), every frond kept, and a
  second ring of the same fronds turned half a frond step, a little shorter and drooping more.
* `OotfAssetBuilder.composeDrawing` builds these drawings; the disc texture copy shared with `useDiscModel` is now
  `copyDiscTextures`.
* `ps2ModelRowName`: prop rows can be switched per PC model as `class|PC model`, so "Large Tree" (a pine in Ruined
  Village, a palm in Islands) can have its palm and pine turned on and off apart. The editor's import dialog ticks and
  stores rows that way; a plain class name still switches every row of the class.

Rows (Drakan 2 model table, category Props):

| OotF class (PC model) | Drawn as | Default |
| --- | --- | --- |
| Skeleton Hanging, Skeleton04 (Skeleton04) | MOUNTAON "Skeleton 10 - Hanging no cable" hung from the OotF chain, as tall as the OotF skeleton | on |
| Hanging Guy (HangingBody2) | WORLDCON "Deadguy 03" on the OotF gallows rope | on |
| Hanging torso (HangingTorso2) | WORLDCON "Deadguy 02" on the OotF rope | off (its head thrown back, it reads as falling off the post: `evidence/wc_torso_zoom.png`, runs `wc_b4` / `wc_a4`) |
| Cage Body(L) (Cage02wPerson) | WORLDCON "Deadguy 03" in the OotF cage, legs out of the bottom as before | on |
| Spiked Body (Body on Spike02) | WORLDCON "Deadguy 03" folded 125 degrees over the OotF spike | on |
| Spiked Torso (Torso on Spike01) | WORLDCON "Deadguy 01" on the OotF spike | off (not seen in the game) |
| Bridge 01, Bridge 03, SusBridge01, SusBridge03 (end pieces) | boards and round ropes; rock pillars and posts kept, posts with the Drakan 2 post picture | on |
| Solid Mid Piece, Solid Bridge (span) | boards and round ropes | on |
| Bridge 02 (broken span) | boards and round ropes; the hanging broken pieces flat with the Drakan 2 plank picture | on |
| Bridge Rope (break-away bridge ropes) | round ropes | on |
| Large Tree (PalmTree_Tall01), SmallTree (PalmTree_Short01) | rounded trunk, doubled crown | on |

The round 7 refined rows of these five bodies are gone; Torture Gyro Victim and the dead villagers keep their refined
rows (off). Turn any row off in the editor or with `--ps2-model-map` (`{"ootf": "Bridge 01", "ps2": null}`).

## Evidence

Owner's ini (copy of `build/review/baseline-play`, private ELF), the dist exe of `bacb3eaf` (converter-only change, no
exe build). Packs: before = the dist pack (`dist/DrakanPC/mods/ootf-levels.zip`, formats `e5f395c`, wide chunks);
after = `packs/on1`, a full 23-level campaign pack from my formats tree on `e5f395c` with `DRAKAN_TERRAIN_CHUNKS=wide`,
the subtitle files and every new row on. Direct level starts on an empty card, god mode, placements to reach each spot,
the same poses for both packs.

* **Collision records:** `tools/cmpmod.ts` on all 17 swapped model copies of the pack (both copies of the bridge pieces
  and skeleton): 0x0204 and 0x0207 identical to the before pack, OotF vertices first.
* **Bridges, walked with real input** (`tools/bridgewalk.py`, runs `rv_b` / `rv_a`, Ruined Village bridge 692 / 695 /
  694): from the end piece, W held with mouse steering toward the far end, then back the other way. Before: crossed both
  ways (lowest y 22757 forward, 22832 back). After: crossed both ways (22833, 22829), the y profile along the span the
  same: a second before run (`rv3_b`) gave 22838 forward, so the 22757 of the first one was a one-off. Frames `evidence/rv_bridge_walk.png` (along, middle both ways, end): boards with
  gaps and round ropes and Rynn walking on the boards; `evidence/rv_bridges_other.png`: the broken span 178 (side and
  along) and the break-away bridge 2189. Walked onto 2189 with W in both packs (runs `rv3_b` / `rv3_a`): it gives way
  and Rynn falls into the river in both (after about 3,000 and 3,700 units along the 10,240 unit bridge).
* **Hanging skeleton and bodies** (Wartok Canyons torture room, runs `wc_b2` / `wc_a2`, `wc_b3` / `wc_a3`):
  `evidence/wc_bodies_zoom.png` (brightened crops: the hanged man and the hanging skeleton from the chain, the caged
  man and the hanging torso, the spike pit) and `evidence/wc_spikes_zoom.png` (the dead man folded over his spike, row
  3). The Drakan 2 skeleton hangs from the OotF chain at the OotF height; the dead men hang from the OotF ropes, stand in
  the cage with their legs out of the bottom and lie over the spike with the spike through the body. The hanging torso
  (`evidence/wc_torso_zoom.png`, a closer look in runs `wc_b4` / `wc_a4`) throws its head back and seems to fall away from
  the post; it is more detailed but not clearly better, so its row is off.
* **Palms** (Islands, runs `is_b` / `is_a`): `evidence/is_palms.png` and `evidence/is_palms_zoom.png`: fuller, more
  drooping crowns on the tall and short palms, trunks as before.
* Offline previews of every row (software renders of what the pack draws): `prev/`.

## Incident and recreation

The first commit of this change (formats `3d311b0`, main `298230c9`) was lost with the local formats repository: removing
my private formats worktree with `git worktree remove --force` followed a `node_modules` junction inside it into the npm
workspace links and deleted the formats checkout (with its `.git`) and the app, desktop and editor-core packages. The
coordinator restored the main tree; the change was rebuilt from this session in a plain clone and recommitted as
`ba19875` on top of the recreated round 8 commits. A conversion of Wartok Canyons, Ruined Village and Islands from the
rebuilt tree draws every swapped model byte for byte like the evidence pack (vertex and display list records), and its
polygon and bounds records equal those of the same conversion with the rows off. Rule kept since: no junctions or
symlinks inside any git worktree or clone.

## Not verified

* Spiked Torso: hangs high on a dark wall, never seen clearly in the game; its row is off.
* The break-away bridges: walked onto once per pack and they broke in both, but where the round ropes meet the flat
  "Break Away B/C/D" sections (engine class 44 objects, left OotF by these rows) was not looked at in a lit frame.
* Grimstone Mines, Grotto and Wartok Canyons bridges, Alwarren's bridge pieces and hanging skeletons, and Islands Night
  palms were converted the same way but not looked at in the game.
* The Wartok Canyons views are dark; the crops were brightened for judging. The cages swing, so cage frames differ a
  little between runs.
* Not covered: Human Bridge, Broken Human Bridge, Temple Bridge 01 / 02, Draw Bridge, the Alwarren bridge planks and the
  Mines bridge drop (other models, left as they are).
* Drakan 2 is untouched (converter only); no Drakan 2 scene was played.
