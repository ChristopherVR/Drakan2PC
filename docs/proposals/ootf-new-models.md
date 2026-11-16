# New models for Order of the Flame content without an Ancients' Gates equivalent

Status: proposal (2026-11-16). Owner request: build models for the OotF NPCs and objects that have no Drakan 2
(Ancients' Gates) equivalent or mapping, such as the lava, plants and harpies, and make them look like the D2 game
models. Evidence and working files sit under `build/` (game data, not committed): `build/model-inventory/inventory.md`,
`build/d2-style/style.md`, `build/delon-model/`.

## 1. What is actually missing

A placed-object census of the 23 converted levels, plus a full campaign conversion with the class-plan audit, gives
these counts (distinct models shown by placed objects):

| Category | Models | Placed | How they are drawn today |
| --- | --- | --- | --- |
| Creatures and characters | 63 | 877 | 11 D2 disc creatures, 4 D2 player models (Rynn, Arokh), 18 PC models with the skin upgrade, 28 plain PC models |
| Plants | 22 | 677 | 6 D2 pines, 3 palms rebuilt at convert time, 13 PC models |
| Water, lava, fire, FX | 88 | 582 | 7 flame cards with D2 particle fire, 80 PC models (mostly waterfall pieces) |
| Props | 568 | 3,001 | 22 D2 disc models, 12 same-named D2 classes, 3 composed, 516 PC models |
| Architecture | 439 | 3,162 | mostly PC models, 8 rebuilt rope bridges |
| Items | 81 | 406 | 11 D2 weapons, 3 D2 potions, the rest PC models |

### Fixes that need no new model

* **Dragons the disc already has.** Crimson Dragon (disc model 3671), Blade Dragon and its boss (3252), and Ice Dragon
  (5146) keep their PC meshes only because the class names differ ("Crimson Dragon (Bat)", "Blade Dragon (Crow)",
  "Ice Dragon 1"). Add aliases or enemies rows in the model table, as the Skeleton Dragon and Primitive Giant already
  have.
* **Ebon Dragon:** it belongs to the D2 bat dragon family. A recoloured disc model may be enough.
* **Lava** is mostly terrain, which already draws D2's "Lava (Base)". The meshes are only the Volcanic lava lake
  (Volcano #3817, 1,485 polygons), the Magma Pit, three Magma Shards, a Lava Vent and the Islands lava mechanism. That
  is a shader and texture job plus one large mesh.
* **Water** is the engine's water pass. The waterfalls already use the D2 Surdana falls texture.
* **Flames** are effects. A better flame effect beats a flame model.
* **About 250 effect classes** (gibs, blood and pus pools, fire and magma shots, lens flares, trails) are dropped as
  empty references today. They need mapping onto D2 effects, not modelling.
* **Remake rows that ship off:** the Talon torches (92 placed) and the Succubus pillar (42) already have rows. Turning
  them on is a review, not new art.

### Real modelling candidates, ranked by how much the player sees

1. **Succubus**, the harpy (NPCs #3687): 21 placed in the Islands, Succubus Lair and Volcanic levels, about 88 scene
   lines. D2 has no harpy.
2. **Queen Shillah** (#3688): a boss, 13 lines.
3. **Goblin / Grenadier** (#3680): 120 placed, the most common creature still on a PC mesh. D2's LilGoblin has the
   same rig.
4. **Delon** (#3727): the villain of the opening, Volcanic and the ending. A first pass exists (section 3).
5. **The Dr. Evil heads** (#3701): the final boss.
6. **Crimson Knight** (#3670, 41 placed) and **Ebon Knight** (#3927, 26 placed): same rig, one base.
7. **Tick** (#3689, 29 placed), **Drone** (#3675, 12 placed), **Death Mage** (#3712, 25 placed; less than half of
   the D2 Death Mage rig matches).
8. **Tropical plants** plant01Small, plant02Small and plant02 (183 placed together), and the palms (about 106).
9. **Humans:** Julian (#3728), the Ruined Village villagers (#3829 to #3833), the mine slaves, Tuiri, Rimril, the
   farmer, the warrior, the hiding slave, the wounded wizard.
10. **Props:** the hanging torch HTorch (49 placed), and the Intro owl (Intro Level #66).

## 2. The D2 model style (measured on 7 disc humans)

Dumped with `editor/packages/formats/scripts/dump-ps2-model.ts` and rendered with
`analysis/scripts/blender/import_pc_model.py`. Models: MediumGuy, CurveyChick, Ranwulf2, Mage, Guard1, Dehrimon
and Rynn (Leather).

* **Density:** NPCs have 1,968 to 2,675 triangles and 1,020 to 1,524 vertices, all triangles. Villager heads are 442
  to 500 triangles; the Mage and Rynn heads are about 920. Villagers have 3 LODs (about 2,048, 842 and 198 triangles).
* **Rig:** T-pose. Male villagers and guards have 57 joints: `Villager`, `upperbody`, `torso`, `chest`, `neck`,
  `head`, **`Jaw`**, **`eyelids`**, **`eyebrows`**, collars, arms, `HandLeft`, `Lweapon`, three-segment fingers,
  `Backsheath`, `lowerbody`, legs, toes and sheaths. Female rigs have 65 joints and add a `hair1..3` chain. Skinning
  is mostly rigid (25 to 50% of vertices have more than one influence, never more than 4).
* **Proportions, at 1.75 m:** about 8.5 heads tall. The head is about 0.20 x 0.16 x 0.21 m. Male shoulder joints are
  0.39 m apart, hips sit at 60% of height and hands are 7 to 9% of height.
* **Textures:** 3 per character, 8 bpp with 256-colour palettes. A separate 128x128 face texture (a photo-sourced half
  face, mirrored, with the ear and the hair painted around it) and two 256x256 body atlases split by part. The head
  gets about 400 to 430 texels per metre, the body 230 to 400.
* **Shading is painted in:** folds, quilting, rivets, armour bevels. Unlit renders still look shaded.
* **Eyes and mouth:** a small modelled eyeball patch and a strip of teeth. NPC hair is a solid mesh painted on the
  face texture. Rynn alone has alpha strand cards.
* **Palette:** earthy and warm (rust, tan, brown, off-white), mid saturation; armour near-neutral grey with gold trim.

## 3. Lessons from the first Delon

`analysis/scripts/blender/delon_model.py` builds a dense, smooth, procedurally shaded model: 593k render triangles,
geometric eyes and brows, strand hair, flat-colour game LOD.

* It follows the 1999 outfit and rig, but not the D2 look.
* Compared with the 1999 head, the face was too long, the jaw and chin too narrow, the nose too big and the hair too
  short. The second pass broadens the face, squares the jaw, makes the eyes green, and lengthens and darkens the hair
  over the eyes, ears and jaw.
* What still separates it from D2 is method, not shape. D2 faces are photo-like textures on about 450-triangle heads,
  and D2 cloth shading is painted, not lit.

## 4. Proposed pipeline: a D2 character kit

1. **Rig first.** Humans are built on the D2 villager rig (57 joints with `Jaw`, `eyelids` and `eyebrows`), not on
   the PC rig. OotF clips reach it through `ootfretarget.ts`, whose role matching already pairs PC and D2 human joint
   names (`upperbody`, `torso`, `chest`, `Rcollar`, `uparm`, `forearm`, `thigh`, `calf`). A scene speaker can then use
   the class 157 talker path and get engine lip sync instead of a synthetic jaw. Creatures without a D2 relative keep
   their PC rig and PC clips.
2. **Low-poly mesh authored to budget:** 2,000 to 2,600 triangles, a 450 to 500 triangle head, D2 proportions,
   T-pose, clean quads triangulated at export, with modelled eyeball patches and a teeth strip. LOD 1 and LOD 2 at
   about 40% and 10%. Build them in Blender from a base mesh plus per-character outfit parts, not by decimating a
   sculpt (decimation wrecked eyes, hair and belts in the first Delon).
3. **Fixed UV layout** matching D2: a 128x128 face texture (mirrored half face) and two 256x256 atlases (upper body
   and arms; legs, feet and small parts).
4. **Textures in the D2 manner:**
   * Body atlases are baked in Blender from the detail model: base colour times AO, a soft top-light gradient and
     cavity, so the shading is painted in like D2. Then quantise to an 8 bpp palette.
   * The face comes from the character's own OotF face texture, read from the player's disc at convert time and
     upscaled with the HD pipeline (`analysis/scripts/hd_pack.py`, Real-ESRGAN). It keeps identity and the
     photo-sourced look, and commits nothing.
   * Hair is painted onto that face texture, on a solid hair mesh.
5. **Converter integration:** a model row (like the skin-upgrade and swap tables) that points an OotF class at the
   new mesh and rig. Meshes ship as engine-neutral data generated from committed Blender scripts. Game-derived
   textures are assembled at convert time from the player's own files.
6. **Check in game,** with the player's real settings and evidence images, against the walk and talk clips (the
   `verify-with-user-settings` rule).

### A cheaper first step for humans

Re-texture a D2 villager body that already wears a similar outfit (MediumGuy wears a shirt, a vest, trousers and
boots) with the OotF character's face and colours, at convert time from both discs. It matches D2 exactly and comes
with the jaw rig, but loses each character's own outfit. That is good enough for background villagers and slaves,
but not for Delon, Julian or Tuiri.

## 5. Suggested order

1. Dragon aliases and the Ebon Dragon recolour (table rows, no art).
2. Build the D2 human kit with Delon as the first character: a base mesh on the D2 rig, the UV layout, the bake and
   palette pass, the face from his PC texture, in game in Ruined Village 1.1.
3. Reuse the kit for Julian, Tuiri, Rimril, the farmer, the warrior and the villager and slave sets.
4. Succubus and Queen Shillah (new mesh on the PC rig, wings as cards), then the Goblin upgrade, the knights, the Tick
   and the Drone.
5. Tropical plants and palms as D2-style card plants, then the Volcanic lava lake mesh and a lava shader.
6. Map the dropped effect classes onto D2 effects.

Tools for every step: `ootf-model-dump` skill (`.claude/skills/ootf-model-dump/SKILL.md`), `dump-pc-model.ts`,
`dump-ps2-model.ts`, `import_pc_model.py`, and `delon_model.py` as a Blender authoring example.
