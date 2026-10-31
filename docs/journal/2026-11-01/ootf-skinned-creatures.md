# OotF skinned creatures, converted end to end (2026-11-01)

Code: `ootfassets.ts` (`OotfAssetBuilder.addModel` skinned path, `convertClips`, `mapAnimRef`, `animEntries`, `pruneSkinned`),
`ootfclass.ts` (`PlanOptions.mapAnimFor`, `mergeDefaults` 4th argument), `ootfpipeline.ts` (host ADB, anim fields, `skinned`,
`preferCreatures`), `ootfanim.ts` (`writePs2Skeleton` pads odd LOD names), `ootfcreature.ts` (`keepChannels`).
Probes: `build/ootf-skin/*.ts` (private).

## PC skinned model layout (checked on spider 3228, goblin 3680, troll 3508)

* Per LOD the 0x0203 vertex list is `vertexCount` LOD vertices followed by "cap" vertices (the `_cap` LOD names; goblin LOD 0:
  162 + 15 = 177 = LOD 1 `firstVertex`). The 0x0204 polygons are likewise `polyCount` followed by cap polygons.
* Weights (0x0208 per joint per LOD), polygon indices and sphere vertices are numbered from the LOD's own first vertex, and the
  weights also cover the cap vertices (spider 149 weighted of 132 + 17).
* PS2 skeletons keep every channel (troll 20, mostly with no caps); only the spider on the disc keeps 4.

## Conversion (single LOD)

1. LOD 0 only: positions `[first, first + vertexCount)`, polygons `[firstPoly, firstPoly + polyCount)`, cap vertices dropped.
2. `pcToNeutral` (textures, normals), then `withDisplayList(model, { vertexJoints })` from the weights (heaviest joint first):
   the disc scheme (joint-cluster groups) when the model does not fit one window, else one group.
3. Skeleton: LOD names removed, weights of LOD 0 only, channels kept with no caps (dismemberment off), spheres of LOD 0,
   `remapSkeletonVertices(source, polygonCount)`, `writePs2Skeleton(.., Infinity)`.
4. Records: 0x0200, 0x0203, 0x0204, 0x0208, 0x0211 (no 0x0207).
5. Clips: the PC ADB (`<container>.adb`) entries listed by the skeleton go through `convertCreatureAssets` (mode `convert`, new
   `keepChannels` so all channels survive), get fresh ids from `firstAnimId` (100.. past RESOURES.ADB's 1..65) and ship as
   `new/RESOURES.ADB/0501_<id>_0.bin` + `0502`; `overrides/RESOURES.ADB/0500_0_0.bin` gets the clip count and highest id + 1.
6. Classes: animation-typed defaults (type 9, scalar or array) of a cloned class are mapped through the PC id -> new id map
   (`mapAnimFor`); unconverted ids keep the template's value.
7. Trees: static models whose name matches /tree|birch/ get a regenerated 0x0211 (`displayList` option).
8. `pruneSkinned` drops the skinned models and clips of classes that did not survive `maxNewClasses`.

Findings: creature classes of OotF (villagers, Wartocks, Hueges, Ebon Knight ...) that have a same-named PS2 class are mapped to
the PS2 class and use the PS2 model; the classes that need the converted skinned model are rfl 17 "Ground Object" clones (fake
julian, orc01, Heuge Yellow, mvillager ... in Ruined Village, Farmer / Warrior / Queen / Web Troll in Wartok Canyons, Rynn /
Aroch / Owl in the Intro), which have no animation fields, so the clips matter only for classes with type 9 fields.

## In game (PAL, Volcano as the replaced level, `ootf_level_run.py` with a custom recipe, game lock held)

Offline checks first (`build/ootf-skin/v1.ts`): on hueges 3713 / spider 3228 / troll 3508 the PS2 records round-trip: rest pose skins every
vertex back to its position (error 1e-7), the 0x0211 decodes to exactly the 0x0204 triangle count, vertex totals match the skeleton.

| Run | Content | Result |
| --- | --- | --- |
| skin3 | skinned conversion off, trees with 0x0211, 2 classes (Ruined Village) | level loads (control) |
| skin2 / 4 / 5 | 2 skinned rfl-17 classes ("orc01", "Heuge Yellow") cloned from static rfl-17 templates, with or without clips, textures pruned or not | **level load fails** (result -6, falls back to the intro) |
| skin6 | all skinned models and 238 clips in RESOURES.MOD / ADB but no class using them | loads: the records, the ADB patches and the 0x0500 counter are accepted |
| skin7 | one skinned class cloned from a *skinned* PS2 template ("Nichol's Tome Stomp") | loads |
| skin13 | Volcano, 4 "Heuge Purple" objects pulled next to Rynn (`DRAKAN_DBG_NOOBJ=1 DRAKAN_DBG_PULL=^Heuge`) | **the converted Hueges draw, skinned**, around Rynn (screens `build/ootf-run/skin13/run/s_start.png`); lighting is the night default, `DRAKAN_LEVEL_TIME=12` did not brighten it |

Reading: a class whose model has a skeleton must be cloned from a PS2 class whose model has one (`modelSkinned` / `templateSkinned` in
`PlanOptions`); the data itself is fine. Only RESOURES.ODB has one skinned rfl-17 template (so one new skinned class); NPCS.ODB has 14
(Lil' Goblin, Historian, Temp Golem ...) and its `.DB` lists Resources as dependency 2, so classes cloned there could reference
RESOURES models with `db 2`: not wired (`modelOdbFile` pins cloning to the host ODB).

Not verified: the clips. The rfl-17 classes have no animation fields, so no clip is played; `mapAnimFor` is covered by a unit test only.
Retarget onto an existing PS2 rig (`retargetCreatureAssets`) is not wired into the builder.

## Harness notes

* Full-object converted levels do not give Rynn control right now (Ruined Village, Volcano: `gameplay=0`, gui screen 23 toggling, no
  `autostart`), also with skinned conversion off; `DRAKAN_DBG_NOOBJ=1` (keeps Rynn, camera, level properties) does. Not caused by this change.
* Test aids: `--creatures-first 1` (with `--max-classes`) keeps classes with converted skinned models first; `DRAKAN_DBG_PULL=<class name regex>`
  moves up to 4 matching objects next to Rynn (and keeps them under `DRAKAN_DBG_NOOBJ`); `--skinned 0` turns the conversion off.
* `ootf_level_run.py` extracts `RESOURES.ADB` now; `ootf-convert.ts` passes it as `hostAdb`.
* The previous commit of `ootfclass.ts` / `ootfpipeline.ts` (4215de7) had interleaved blocks (mixed line endings); this commit stores them whole.
