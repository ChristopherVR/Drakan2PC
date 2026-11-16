# A new Delon model in Blender, and dumping Order of the Flame models (2026-11-16)

Owner request: some Order of the Flame characters have no Drakan 2 model (Delon, Julian, Tuiri, the farmer). Draw a new
Delon with an industry-standard tool, show the result, and write a skill plus a script for dumping these models.

## Reference: the 1999 Delon

* The data came from the owner's disc folder: `analysis/scripts/ootf_cab.py` extracted it to `build/ootf/cab_disc`.
* New `editor/packages/formats/scripts/dump-pc-model.ts` writes OBJ + MTL + PNG textures + a skeleton JSON.
  `Delon(h)` (NPCs 3727) has 265 vertices, 526 polygons, 5 textures and 37 joints.
* New `analysis/scripts/blender/import_pc_model.py` opens a dump in Blender, rigged and textured.
* What it shows: a slim young man in a T-pose with short dark-brown hair and a fringe. He wears a beige V-neck tunic
  to the hips with a dark trim, a belt with a large square buckle, brown leggings laced up the outer thigh,
  knee-high soft boots and dark shoes.
* Bind pose, scaled to 1.72 m: shoulders 1.40 m, hips 0.95 m, knees 0.53 m. Joint names are `fynn`, `upperbody`,
  `torso`, `chest`, `neck`, `head`, `R/Lcollar`, `R/Luparm`, `R/Lforearm`, `lowerbody`, `R/Lhip`, `R/Lthigh`,
  `R/Lcalf` and `R/Lfoot`, plus finger joints `jnt*` off the forearm.

Two traps in the format, now in the skill:
* Walking the joint tree by `firstChild` / `nextSibling` by hand loops forever (0 means none); use `jointParents`.
* LOD 0 polygons need `skinnedLodPolygons`.

## The new model

`analysis/scripts/blender/delon_model.py` (Blender 5.3, headless) builds an original model; it reads no game data.
* **Body:** a skin-modifier vertex graph at the PC rig's proportions, then subdivision, a voxel remesh and a corrective
  smooth.
* **Head:** a sculpted-by-function head with eyes, lids and ears.
* **Clothes:** fitted to the body: tunic hem, belt, cuffs, boot tops, a V-neck trim cord and thigh lacing.
* **Hair:** curve strands over a scalp shell.
* **Materials:** procedural Principled.
* **Rig:** 46 bones named like the PC rig, plus hand, finger, thumb, toe and jaw bones, with automatic weights.
* **Game LOD:** per-part triangle budgets.

| | Render mesh | Game LOD | PC original |
| --- | --- | --- | --- |
| Triangles | 593,176 | 3,776 | 526 |
| Vertices | | 1,977 | 265 |

Outputs: `build/delon-model/` (`delon.blend`, `delon_game.glb`, `render_*.png`, `render_lod_*.png`,
`render_posed.png`, reference renders in `ref/`).

Seen in the renders:
* Front, three-quarter, side and back views, and face close-ups.
* A posed check (arms down, knee bent, head turned): body, clothes and cords deform with the rig.
* The game LOD keeps the silhouette, belt, boots and face. Its hair is the scalp shell only.

## Not done / open

* **Hair:** the game LOD needs hair cards (strands do not decimate).
* **Textures:** brows, laces and the cloth detail need baking to textures (base colour, normal, ORM) on a UV unwrap.
  The `.glb` carries flat material colours only.
* **Face:** the face is scripted, not hand-sculpted. It reads as stylised; a sculpt pass would lift it.
* **Into the game:** not converted. Weights would be capped at 4 and the mesh bound to the PC skeleton at conversion
  time, then checked in game against the PC clips (`1_1walktalk`, the walk).
* **L/R:** the PC `R*` joints sit on +X in the dump. Whether that is the character's right was not checked
  (mirror question).

## Head review, second pass

Owner: the head looks different. Compared at the same camera with the 1999 head (`build/delon-model/ref/pc_face*.png`):
* Shape: the 1999 face is broad and short with a square jaw and chin. Mine was a long egg with a pointed chin and a
  large nose.
* Hair: near-black, hanging to the eyes and over the ears to the jaw. Mine was a short brown cap.
* Eyes: green.
* Detail: brows, eyes and lips are painted in a photo-like face texture. Mine were geometry.

The second pass changes the face shape, the hair and the eye colour (`build/delon-model/v2/`). The remaining gap is
the method: D2 humans are low poly with painted textures. The plan for that, measured on 7 D2 humans, and the
inventory of OotF models without a D2 equivalent are in `docs/proposals/ootf-new-models.md`.
