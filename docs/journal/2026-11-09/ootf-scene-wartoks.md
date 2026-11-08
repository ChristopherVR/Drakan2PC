# Scene Wartoks on the Drakan 2 Wartok model (2026-11-09)

Request: the scene-only Wartoks (`Wartock Leather / Chain / Plate`, engine class 17, cloned from the NPCS `Lil' Goblin` prop
template) kept their PC models in cut-scenes while gameplay Wartoks were already disc classes. Formats `d052dd4`.

## Approach

* Three rows in `DEFAULT_PS2_MODEL_SUBSTITUTIONS` (`scene: true`, category enemies) name the disc class of the same armour
  (`LeatherWartock : Center`, `ChainTock : Center`, `PlateTock : Center`). Same opt-out as every pair: `--ps2-model-map` with
  `{"ootf":"Wartock Chain","ps2":null}`, `--ps2-models 0`, or the import dialog.
* The scene class stays a class 17 prop cloned from the Lil' Goblin (it is only moved by scenes, no AI, no health), but its
  model reference names the disc Wartok model in NPCS.MOD (4418 / 4422 / 4427, with the disc class's 64 byte bounds).
* Every PC clip of the Wartok model (61 to 63) is retargeted onto the disc rig (`retargetCreature`) into new NPCS.ADB clips (ids past
  the ADB and the converted creature clips; the ADB header override is extended, not replaced). The class's own clip fields and the
  scene animation actions map through that table (`sceneTocks`, `tockOf` in `ootfpipeline.ts`).
* Disc joints with no PC partner (20 of 50) hold the first key of the native Wartok `idle` clip instead of the bind pose
  (`holdPose`, as for Rynn's fingers); name-unmatched attach leaves would be paired by position (`leafFramePairs`, not needed here).

## Channel table, PC scene channel to disc channel

Same for the three armours up to channel 16 (`creatureChannelTable`, matched by joint name):

| PC | joint | disc |
| --- | --- | --- |
| 0 to 6 | Troll, Body, UpperBody, Neck, Head, Jaw1, Jaw2 | same index |
| 7 | Rshoulder | 8 |
| 8 | Rarm | 9 |
| 9 | Weapon_R (spear attach) | 10 |
| 10 | Lshoulder | 11 |
| 11 | Larm | 12 |
| 12 | Weapon_L (villager attach) | 14 |
| 13 | LowerBody | 16 |
| 14 | Rleg | 18 |
| 15 | Lleg | 17 |
| 16 | L_radius | 13 |
| 17 to 19 | leg or spine joints, vary by armour | spine2 to 19 (Chain, Plate); others have no disc channel (kept, with a converter warning) |

`creatureChannelTable(pcSkeleton, ps2Skeleton)` (`ootfretarget.ts`) returns the rows; the pipeline hands it to `channelMapForActor`,
so `remapChannel` rewrites both clip actions and attach actions of a scene Wartok. The impale itself no longer depends on it:
`ootfcarry.ts` replaces the villager attach by world keys computed from the PC rig; the spear attach (PC 9 to disc 10) does use it.

## In game (private copy `build/ootf-run/play-wartokscene`, Ruined Village campaign, shots-only recipe)

Seen: the Wartoks in the opening ambush and the bridge crossing play walk and attack poses on the disc model (manes, bare arms),
nothing in a T-pose; the Wartok in scene 1.2a holds the spear in both hands and, at the impale, the villager hangs on the spear tip
in the air (about 108 to 111 s into the run). Not verified: the Chain and Plate scene Wartoks specifically (all armours share
the code; shown ones looked Leather and Chain), scene 1.3, comparison against PC footage, and the class's own gameplay clip
fields (retargeted with the same table, never played).
