# Order of the Flame: Drakan 2 models for converted props (skeletons), flames surveyed

Owner request (round 7): "There are models in the ootf converted levels that could benefit from using the Drakan2 one like
flames, skeletons (ones on impaled spikes as well), some props in the world." Scratch, probes, packs and frames are under
`build/review/d2-models/` (not tracked); nothing from either game is quoted here, only class and model names and counts.

## 1. Survey

A census of every placed object of the 23 PC levels (`tools/census.ts`, `census_all.txt`) and a match of every placed PC
prop's model name against every model on the disc (`tools/match.ts`, `match.txt`, MODs read from the player's ISO) gave
three groups.

**Already Drakan 2, nothing to gain.** Barrels (`Barrel`, 155 placed), exploding barrels (79), `Crate` (27) and
`Crate Short` (9) are name-mapped to the MOUNTAON classes of the same name, which use the same model ids and sizes as the PC
ones: Drakan 2 carried these OotF models over. `Troll Crate` / `Grimstone crate` (249) match MOUNTAON "Troll Crate"
(156 against 108 triangles, same size): the same model, left alone. The floor bricks (`floor1` .. `floor17`, about 250) are
the same 12 triangle boxes on both discs.

**Real Drakan 2 remakes of the same prop** (same model name, size and origin, 5 to 10 times the triangles): the Mountain
World skeletons (MOUNTAON "Skeleton 01 - Crucified", "02 - Laying On Ground", "04 - Hanging", "05 - On Spike",
"06 - Torso", "07 - Impaled no spike", about 1,000 triangles each against 131 to 151), the wine keg (MOUNTAON "Wine Keg
(Surdana)", 945 against 72), the brick pile, columns and walls of WORLDCON, the Wartok Canyons cocoons (LEVEL2RE).

| PC class (model) | Placed | Levels | Drakan 2 class (ODB) |
| --- | --- | --- | --- |
| Skeleton, Skeleton02 (Skeleton02) | 51 | Ruined Village, Wartok Canyons, Grimstone Mines, Grotto, Alwarren, Islands, Islands Night, Rift Crystal, Succubus Lair | Skeleton 02 - Laying On Ground (MOUNTAON) |
| Skeleton Torso, Skeleton06 (Skeleton06) | 26 | the same Mountain levels, Islands, Succubus Lair | Skeleton 06 - Torso |
| Skeleton Spike, Skeleton05 (Skeleton05) | 3 | Wartok Canyons, Alwarren (spike pit) | Skeleton 05 - On Spike |
| Skeleton07 (Skeleton07) | 2 | Ruined Village, Islands Night | Skeleton07 - Impaled no spike |
| Skeleton01 (Skeleton01) | 2 | Succubus Lair | Skeleton 01 - Crucified |
| Skeleton Hanging, Skeleton04 (Skeleton04) | 9 | Wartok Canyons, Grimstone Mines, Alwarren | Skeleton 04 - Hanging from chain |
| Wine Keg, winecask (WineBarrel01) | 18 | Ruined Village, Islands | Wine Keg (Surdana) |

**Flames.** 487 placed flame objects of engine class 20 (`Sconce Flame` 192, `Torch Flame` 56, `Hanging Flame` 52,
`SconceFlame` 49, `Standing Torch Flame` 40, `Hanging Torch Flame` 25, ...), each a small crossed quad with an animated fire
texture standing on a separate base prop at the same origin. The converter clones them from the disc's RESOURES
"Wall Torch" (class 20). No PC field name matches that class's fields, so every converted flame record carries the
template's `Fire Particles` (RESOURES "Wall Torch Flame Gen") and `Dynamic Light` ("Wall Torch Light"). In game (entity
census around torches in Wartok Canyons and Grimstone Mines, `runs/wc_p1.out`, `runs/gm_p1.out`) each converted flame has
its light (engine class 133) but no generator (63) and no particles (43) exist, and no Drakan 2 fire shows; only the OotF
quad burns (`evidence/rv_pairs.png`, the brazier left of the impaled skeleton). Drakan 2's own torches put the object
origin at the flame (the torch model hangs below it), while the OotF flame sits 0.6 to 0.76 units above its origin. Making
the Drakan 2 fire show would need the reason the class 20 handler does not start its generator for these objects, and the
objects moved to the flame point. Not done in this round (section 5).

## 2. What changed (formats, `ootfps2model.ts`, `ootfassets.ts`, `ootfpipeline.ts`)

* The Drakan 2 model table (`DEFAULT_PS2_MODEL_SUBSTITUTIONS`, the one table the editor already shows) has a third
  category, `props`. A row names the OotF class, the PC model it must show (`pcModel`: Volcanic's "Skeleton" is another
  model and is left alone), the Drakan 2 class and its ODB (`odb`), a label and the reason.
* At convert time the disc model of that class is read with its textures (`discModelOf`: MOUNTAON / WORLDCON from the host
  files, any other ODB's MOD and TXD from the player's ISO). `discModelMismatch` compares its box with the PC model's: on
  every axis the size and the box centre must agree within 20 % of the larger model, or the OotF model is kept and the
  reason goes to the conversion warnings. This is what keeps a skeleton on its spike: the hanging skeleton's disc model is
  refused (its origin is at the chest, 0.59 units above the OotF chain point).
* `OotfAssetBuilder.useDiscModel` copies the model into the pack instead of converting the PC one: vertices, polygons and
  the strip display list are the disc's (its batch textures are list indices, checked on four disc models), the textures
  are copied byte for byte (so the owner's Drakan 2 HD textures replace them) and the texture list is renumbered to the
  copies. The object keeps its OotF class (health, collision type, messages, "Can Arokh land", flammable ...) and its
  placement, rotation and scale.
* Collision: the disc's own bounds of these props are solid boxes; with them a lying skeleton became an obstacle and Rynn
  could no longer walk onto it to the armour that lies on it (run `bh_props1`, Ruined Village skeleton 2496 with the
  "Rusty Chain Mail"). Since the follow-up (section 6) every swapped prop collides with its OotF model exactly.
* A pack converted with the rows on differs from one with them off only in RESOURES.MOD / TXD and the 10 class records of
  the swapped classes (the cached model bounds); every LVL and every other record is byte-identical.
* Option: on by default for the skeleton rows; `--ps2-model-map` with `{"ootf": "<class>", "ps2": null}` turns a row off,
  `--ps2-models 0` turns all Drakan 2 models off, and the editor's import dialog lists the rows with a Props category.
  Off by default: the hanging skeleton (refused by the check anyway) and the wine keg (below).

## 3. Evidence (owner's ini, `play/` = copy of `baseline-play` with the dist exe of 90dff87b, private ELF)

Packs: `packs/off1` (prop rows off) and `packs/props3` (on), both full campaign packs from the same formats tree with
`DRAKAN_TERRAIN_CHUNKS=wide` and the subtitle files. Direct level starts on an empty card, god mode, one placement per
view, the same poses for both packs (`tools/look.py`, `tools/rv_poses.json`, `tools/al_poses.json`).

| Kind | Level | Frames | Seen |
| --- | --- | --- | --- |
| Impaled skeleton (Skeleton07, its spear a separate object) | Ruined Village, by the braziers | `evidence/rv_pairs_final.png`, `evidence/rv_skel07_zoom.png` | the Drakan 2 skeleton hangs on the spear in the same pose and place; ribs, hands and feet modelled instead of slabs |
| Skeleton on a spike (Skeleton05) | Alwarren, spike pit | `evidence/al_pairs_final.png` | the Drakan 2 skeleton sits on its own spike among the pit's spikes, same place |
| Lying skeleton, torso (Skeleton02, Skeleton06) | Ruined Village, cave by the armour | `evidence/rv_pairs_final.png`, `evidence/rv_torso_zoom_bright.png` | Drakan 2 models lie where the OotF ones lay (dark cave; frames brightened in the zoom) |
| Lying skeleton | Alwarren dungeon | `evidence/al_pairs_final.png` | lies beside Rynn, same spot |

Played with real input (`tools/behav.py`, runs `bh_off1` / `bh_props3`): walk with W at the impaled skeleton, the torso,
the lying skeleton and a wine keg, three sword swings beside each, back off with S, F5, a placement away, F8. Rynn stops
at the same places with both packs (impaled skeleton: about 230 units short, at its spear, in both; torso: walks over it in
both), walks onto the lying skeleton and picks up the armour on it in both, nothing blocks where the OotF model did not,
and F8 puts her back at the F5 spot. Swinging beside the skeletons changed nothing (they are scenery with no health).

## 4. Skipped, with reasons

* Flames: no Drakan 2 fire appears from the converted flames although their records name the disc's fire generator
  (section 1); a fix is engine work plus moving the objects to the flame point. The OotF quads stay.
* Hanging skeleton: origin 0.59 units off, refused by the size and origin check; would hang below its chain.
* Wine keg: first off (0.1 units deeper, and with the triangle collision Rynn stood on its rim after backing away); on since
  the follow-up (section 6).
* Barrels, crates, Troll crates, floor bricks: already the same model as on the disc.
* Cocoons, brick piles, columns, walls, jugs, bottles, gravestones, doors: other sizes or origins (jugs and bottles half the
  size, walls 0.4 wider, the cocoon's origin differs), or too few placements for this round. Not in the table.
* Spiked bodies, bloody spikes, cage bodies: no Drakan 2 counterpart.

## 5. Not verified

* Flames were not changed; why the class 20 handler does not start the "Wall Torch Flame Gen" of converted flames is open.
* Wartok Canyons and Grimstone Mines views were too dark or blocked by Arokh (both start mounted); their skeletons were
  converted the same way but not looked at in game.
* Succubus Lair (crucified skeleton) and Islands Night were not run.
* Whether the per-triangle collision of the copied skeletons matches the PC game exactly was not compared with the PC game;
  the comparison is against the OotF model in the PS2 engine (same stops and pickups in the runs above).
* The skeleton class's "Flammable" and "Fall When Dead" behaviour (burning, breaking) was not tried.
* Drakan 2 is untouched (converter only); no Drakan 2 scene was played.

## 6. Follow-up (owner): wine keg, bridges, bodies on spikes and in cages

### Collision is now always the OotF model's

The first version gave the copied model one box over its own triangles. Now `useDiscModel` converts the PC model as before
and keeps its vertex, polygon and bounds records: the engine collides with exactly the OotF shape. The disc model's
vertices are appended after the OotF ones (padded to a multiple of 4), its textures after the OotF texture list, and its
strip display list, which is all a rigid prop is drawn from, replaces the OotF one. The list gets a leading vertex group
over the OotF vertices that no batch loads, and its group and texture indices move behind them (`ootfdiscmodel.ts`,
`shiftDisplayList`, `combineDrawing`). A row can also stretch the disc model onto the OotF model's box (`fit`).
In a pack with the rows on against one with them off, every swapped model's polygon (0x0204) and bounds (0x0207) records
are byte-identical and its first vertices are the OotF ones (`tools/cmpmod.ts`: Skeleton02, 05, 06, 07, WineBarrel01).

### Wine keg: on

The keg row is on and stretched onto the OotF keg (the remake was 0.1 units deeper); with the OotF collision the rim that
Rynn stood on is gone. Frames: `evidence/keg_pairs.png` (the Ruined Village cellar, same poses: round Drakan 2 kegs on
their stands where the OotF kegs lay). Real input in the cellar (`tools/kegwalk4.py`, runs `kw4_off2` / `kw4_props4`,
`evidence/keg_walk_pairs.png`): W into keg 592, S back, D along the row, W into keg 593, S, A past 592, W into keg 591, S.
Every stop agrees within 20 units between the packs (into 592: z 169199 / 169218) and Rynn stays on the floor (y 14452 to
14512) in both. Packs: `packs/off2` (all prop rows off) and `packs/props4` (skeleton and keg rows on), same formats tree.
The skeletons were looked at and played again on `props4` (`evidence/rv_pairs_final.png`, `evidence/al_pairs_final.png`,
run `bh_props4`): drawn as before, and Rynn walks onto the lying skeleton and its armour as with the OotF model.

### Bridges: investigated, left as OotF

Ruined Village, Wartok Canyons, Grimstone Mines and Grotto build their rope bridges from `Bridge 01` / `Bridge 03` (the
anchored ends), `Bridge 02` / `Solid Mid Piece` (the span) and `Bridge Rope` (the break-away ropes). Drakan 2's LEVEL2RE
has bridges of the same names: the span pieces have the same length and height but a walkway 1.0 units wide instead of
0.35; its end piece `SusBridge01` is a 5.6 unit whole bridge, `SusBridge03` another shape; they are not more detailed
(198 to 210 triangles against 269 to 301 OotF vertices). Converted with the three span rows on (stretched onto the OotF
span, OotF collision), the span looks the same but with darker, mossier planks (`evidence/bridge178_side_off_top_d2_bottom.png`,
run `br_props5` against `br_off2`), and the OotF end pieces stay; not a clear improvement, so the rows are in the table and
off. Ladders and ropes have no Drakan 2 model of the same kind and size (`match.txt`). Walking across was attempted in both
packs (`tools/bridge.py`), but my start points missed the walkways, so that run says nothing about walking; the rows are
off, so the shipped bridges are unchanged.

### Bodies on spikes and in cages: investigated, not changed

`Spiked Body`, `Spiked Torso`, `Hanging torso`, `Cage Body(L)` (Wartok Canyons, one or two each) and the body props
`Hanging Guy` / `Body` have no Drakan 2 model. Options weighed:

* (a) Compose from disc parts: the disc's human bodies are LEVEL4ON's four standing frozen corpses (0.44 units, upright,
  arms down), LEVEL0RO's burnt dead, sacks and goblin cages; none has the pose of a man impaled through the chest or
  folded in a cage, and the disc skeleton on a spike changes a body into a skeleton. Not done.
* (b) Re-texture the OotF meshes with disc textures: the OotF body textures are atlases (skin, cloth, face and feet laid out
  for that one mesh); every disc body texture is laid out for its own mesh, so either would scramble the UVs. Only the
  spike wood and cage metal tile, and those already get HD images. Not done.
* (c) Refine the OotF mesh: with the display-list path above the converter can now draw a refined mesh while Rynn collides
  with the original, which removes the collision risk of subdivision. Smoothing and subdivision of the OotF body meshes
  is a separate piece of work (seams of the texture atlases must stay put); not started. This is the recommended next
  step if the owner wants these bodies to look less blocky.
