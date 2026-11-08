# Ruined Village: the outhouse ambush, floating barrel and crate, invisible rocks and carts (2026-11-08)

User report on the converted OotF campaign in Ruined Village: enemies meant to run out of a house show outside it while the
house door opens as Rynn gets closer; some boxes and crates float a bit; some objects are invisible. Fixed in the converter
(formats `ootfpropdraw.ts`, `ootfassets.ts`, new `ootfdoors.ts`, one call in `ootfpipeline.ts`); the runtime is unchanged.

Runs are private under `build/ootf-run/rvamb-*` (Ruined Village alone, `--campaign 1`, template LEVEL1NA, a private copy of
the play folder `build/rvamb/play`, god mode, `place` recipes, `DRAKAN_PLAYTEST_WATCH` for positions and yaws). Packs were
converted from a private worktree (`build/rvamb/wt`) so other sessions' uncommitted work stayed out. Probes are in
`build/rvamb/*.ts`. Nothing from either game is quoted here.

## 1. Invisible objects: rigid models without a strip display list

A converted rigid model draws through its strip display list (0x0211). The converter gave one to generic props (engine 17),
melee weapons and bows, trees, and models hosted in a template ODB; every other converted rigid model in RESOURES had none.
On the disc every model of a drawn class carries the list: generic props 81 of 86, boulders and push blocks (16) 134 of 134,
doors (19) 17 of 17, flames (20) 32 of 32, keys and books (33) 46 of 47, mechanisms (35) 39 of 39, also items (10), switches
(13), locks (34), armour (45), power-ups (57), ropes (73), soul crystals (117). Only effect classes have none (blurs 60,
lightning 58, explosions 134, arrows 39).

In Ruined Village 103 converted rigid models lacked the list and were drawn by placed objects: the Village Waterfall 04 and
its rocks, the Dragon Falls rocks, Rocks / Rocks #2, swing blades, the temple's floor, cogs and platform stairs, the Rising
Floor Ring, the Lift, the Push Block, the Boulder for orc, the Priests Key, the flamebook, the Silver Lock and altar lock,
the switch wheels, the Fire Crystal, the Fireball power-up, Heron's Soul Crystal, the Chain Mail, the Fire Fly models and
the flame models. Their collision stayed, so the waterfall rocks were invisible obstacles.

**Fix:** `requireGenericPropDrawLists` now asks a display list for every synthesized class whose PS2 engine id is in
`DRAWN_RIGID_RFL` (10, 13, 16, 17, 19, 20, 33, 34, 35, 45, 57, 73, 117). Ruined Village: 211 converted models without a
list before, 103 after; the rest are detector and sound boxes, lights, gibs and effect sprites.

The doors were not among them: their classes are cloned into template ODBs (WORLDCON, LEVEL1ON), whose models already get a
list.

## 2. Floating barrel and crate: the cart under them was invisible

PC placement and converted geometry agree: terrain heights under all 91 crates, barrels, kegs and carts are identical on
both sides, and the converted model extents match the PC models (the name-mapped Barrel, Crate and Crate Short PS2 models
have the PC's extents too). In game the Barrel 1561 and Crate Short 1562 stood in the air next to the small house: they
sit in CartA 1560 on the PC, and the cart was not drawn. Zeroing the cart's tilt (a test pack) changed nothing.

The cart model has its display list, but one of its textures is 64 x 128. Like the skinned models (`addTexture` writes
those square for that reason), a model drawn from its display list draws nothing when a texture is not square. Nine
converted draw-list models in Ruined Village had one: CartA, Wreck Cart, HChest (the Chests), RuinedTable, three
skeletons, two Scavenger gib chunks.

**Fix:** `OotfAssetBuilder.squareDrawListTextures` rewrites the non-square textures of a draw-list model square in place
(8 bpp, the existing `upgradeIndexedSquareTexture` of the weapons), keeping texture ids, both when the list is written at
import and when `requireDisplayList` adds it. Square textures of any depth stay as they were. Ruined Village: 0 non-square
draw-list textures left.

## 3. The ambush: the outhouse door opened early

The only Ruined Village door that a trigger opens together with an ambush is the outhouse: Generic Detector 2328 (a
2,048 x 18,432 unit strip along the path) sends On to OutHouseDoor 2326 and to two timers, which show the hidden orc 2327
after 1.2 s (On) and send him to Hueges Way Point 2332 after 1.5 s (Move to WayPoint). The orc stands just outside the
closed door, so on the PC he appears to come out of it.

PC doors (all 37 PC door classes) have no "Auto-Open?" field: they open by message or when pushed. The PS2 classes the
converter clones carry Auto-Open 1 at 2 lu. In game (`rvamb-r5`, `r9base`) the outhouse door swung open as soon as Rynn
came within about 4,000 units, before she reached the detector, with nobody there; the orc appeared only once she crossed
the strip, outside the open door. The hidden-creature chain itself works: walking into the strip fires the detector, the
orc (hidden, AI word 0 until then) is shown and runs to his way point. Teleporting into the strip does not fire it
(`rvamb-r2`); walking in does.

**Fix (`ootfdoors.ts`):** a door gets an explicit Auto-Open 0 when a link source sends it On while the same source reaches
a hidden creature within three links (directly or through its timers). Ruined Village: one door (2326). Holding every
door opened by message was tried first (`fix2`, 7 doors) and rejected: the temple's Door Trigger On boxes float above the
bridge, do not fire for Rynn on the PS2, and the temple doors stayed shut (`rvamb-r7fix`); with the narrow rule they open as
before (`rvamb-r9fix`, door 916 yaw 0 to -0.49).

Other hidden creature groups in the level were checked against their PC chains and behave as authored in the watch runs:
the tavern pair 3306 / 3307 stay hidden (their detector 3305 is enabled only by the Priests Key), the flamebook group 1435,
1436 and 2697 stay hidden until the book is taken. Orc 3301 is active although its only linker, timer 3300 (Start Mode 1),
has no trigger; on the PC 440 of 472 Start Mode 1 timers have a trigger, so 3301 probably comes alive through the opening
scene and was left as it is.

## Verification

Packs `build/rvamb/base` (formats `032509d`, before) and `build/rvamb/fix4` (this change), same recipe, comparison sheet
`build/rvamb/r9_cmp.png`, zoom `build/rvamb/final_cart.png`, waterfall `build/rvamb/cmp_vfall.png`:

| What | Before | After |
| --- | --- | --- |
| Cart 1560 by the small house | barrel and crate float, no cart | cart drawn, barrel and crate stand in it |
| Wreck Cart 378 | not drawn | drawn |
| Village Waterfall 04 and rocks 2425 / 2426 | a thin flat sheet, no rocks | rocks and falling water drawn |
| Dragon Falls rocks 3335 | Rynn placed beside them drops into the water | she stands on the drawn rock (`rvamb-r4fix/dfall.png`) |
| Torch flames by the skeleton 2420 | no flame | flame drawn |
| Outhouse door 2326, Rynn 3,300 units away | opens (yaw 1.48 to -0.15) | stays shut (yaw 1.571) |
| Outhouse after walking into detector 2328 | orc runs from outside the already open door | door opens, orc 2327 appears and runs to way point 2332 |
| Temple inner doors 916 / 917 | open by proximity | still open by proximity |

Every run loaded the level and quit by script (PASS). Tests: `ootfdoors.test.ts`, `ootfpropdraw.test.ts` (drawn engine
classes), `ootftex.test.ts` (non-square texture of a required draw list written square, an ordinary static model keeps
its own). Full formats suite 552 tests.

## Open

* The grunt "floating a bit" report is a different cause (creature clips, `ootf-grunt-pose-fixes.md`); props were not
  offset.
* Not seen in game after the fix: the chests (in caves), the ruined tables, the swing blades, keys and locks; they share
  the same change.
* PC doors that Rynn pushes open rely on the PS2 auto-open stand-in; doors opened only by message but without an ambush
  keep it too, so they may open before their trigger.
* Teleporting into a Generic Detector does not fire it; harness recipes must walk in.
