---
name: ootf-model-dump
description: Dump Drakan Order of the Flame (PC) models (mesh, textures, skeleton) and open them rigged in Blender, or author a new model for one that has no Drakan 2 equivalent (Delon, Julian, Tuiri, the farmer). Use when you need to look at an OotF model, compare a converted or new model with the original, or build a replacement in Blender.
---

# Dump Order of the Flame models and work with them in Blender

Everything this produces from the game is game data: keep it under `build/` (gitignored). Commit only scripts and
docs, never dumps, renders of original models, `.blend` or `.glb` files (`scripts/repository_guard.py`).

## 1. Get the PC data

The converter and these tools read an OotF data folder: the one that holds `Dragon.rrc` and `Common/`.
If no install is around, extract one from the retail disc's InstallShield cabinet (7-Zip cannot open it):

```sh
python analysis/scripts/ootf_cab.py "<disc folder with SETUP/DATA1.CAB, or DRAKAN.mdx, or its zip>" --out build/ootf/cab_disc
```

The owner's copy lives under `~/Downloads/Compressed/Drakan Order of the Flame/Drakan/DRAKAN` (disc folder). Reuse
`build/ootf/cab_disc` if it already exists.

Model containers (`<name>.mod` + `.txd` beside it): `Common/NPC/NPCs` (people: Delon(h) 3727, julian (h) 3728,
tuiri (h) 3750, farmer 3615, ...), `Common/Resources/Resources`, `Common/FX/FX`, and one per level
(`Mountain World/Ruined Village/Ruined Village`, ...). Pass the path without the extension.

## 2. Dump a model

Run from `editor/packages/formats` (Node 22+):

```sh
node --experimental-transform-types --no-warnings scripts/dump-pc-model.ts \
  --ootf ../../../build/ootf/cab_disc --container Common/NPC/NPCs --list            # id, name, verts, polys, skinned
node --experimental-transform-types --no-warnings scripts/dump-pc-model.ts \
  --ootf ../../../build/ootf/cab_disc --container Common/NPC/NPCs \
  --out ../../../build/pcmodels "Delon(h)" 3728                                      # by exact name or id
```

Output per model: `<name>.obj` + `.mtl` (LOD 0), `<name>_tex<id>.png`, and for skinned models
`<name>.skeleton.json` (joint names, parents, inverse bind matrices, LOD 0 weights).

Format facts the script relies on (all in `src/ootfmodel.ts`, `src/ootfanim.ts`, `src/ootfassets.ts`):
* Skinned vertex positions are model space in the bind pose. LOD 0 is `positions[firstVertex .. +vertexCount]`.
  Polygon indices and weights count from that LOD's first vertex. Some weights name vertices past the LOD, the
  bounding-sphere vertices; skip them.
* LOD 0 polygons come from `skinnedLodPolygons`, not `firstPoly + polyCount`. It keeps the limb parts that sit after
  the stump caps and drops the caps.
* Joint parents come from `jointParents(sk)`. Walking `firstChild` / `nextSibling` by hand loops forever, because 0
  means "none", not joint 0.
* `inverseBind` is model-to-joint with row vectors (`p' = p R + t`). A joint's bind position is `-(t R^T)`.
* Units: a 1.72 m human is about 0.33 game units, so scale by about 5.2. PC space is Y up and left handed.
* A texture reference with `db != 0` lives in another container (`resolveContainer` in `ootfassets.ts`). The script
  reports it and skips it.

### Drakan 2 (PS2 disc) models for comparison

`scripts/dump-ps2-model.ts` writes the same files, straight from the ISO:

```sh
node --experimental-transform-types --no-warnings scripts/dump-ps2-model.ts \
  --iso "C:/Games/Drakan - The Ancients Gates/Drakan - The Ancients' Gates.iso" --container ENGLISH/COMMON/NPC/NPCS --list
node --experimental-transform-types --no-warnings scripts/dump-ps2-model.ts --iso <iso> \
  --container ENGLISH/COMMON/NPC/NPCS --out ../../../build/d2-style/dump MediumGuy 5056   # name, id or ODB class
```

PS2 humans are about 0.41 units tall; `--height` handles that. Do not flip the PS2 UV v (the script writes v as
stored). Textures from other containers are found through the MOD's `.DB`. Any new model must match the D2 style:
see `docs/proposals/ootf-new-models.md` section 2 (about 2,000 to 2,600 triangles, a 450 to 500 triangle head, a
128x128 face texture plus two 256x256 atlases at 8 bpp with painted shading, and the 57-joint villager rig with
`Jaw`, `eyelids` and `eyebrows`).

## 3. Open it in Blender

Blender: `winget install BlenderFoundation.Blender`. The executable is
`C:/Program Files/Blender Foundation/Blender 5.3/blender.exe`.

```sh
blender -b --factory-startup -P analysis/scripts/blender/import_pc_model.py -- \
  build/pcmodels/Delon_h.obj --clear --height 1.72 --save build/pcmodels/delon_pc.blend --render build/pcmodels/pc
```

Drop `-b` to open it in the UI. The importer converts axes (`(x, y, z) -> (x, z, y)` with reversed winding). It scales
to `--height`, uses nearest-filtered textures with alpha, and builds an armature with the game's joint names, with
the weights as vertex groups. The character faces -Y. In the dump, the `R*` joints sit on +X.

## 4. Author a new model (pattern: `analysis/scripts/blender/delon_model.py`)

The Delon script builds an original model from scratch. It reads no game data, so the script can be committed.

```sh
blender -b --factory-startup -P analysis/scripts/blender/delon_model.py -- --out build/delon-model --clay --samples 16 --res 700
blender -b --factory-startup -P analysis/scripts/blender/delon_model.py -- --out build/delon-model --samples 128 --res 1400 --lod
```

It produces `delon.blend`, `delon_game.glb` (game LOD) and `render_*.png` / `render_lod_*.png`. Reuse its pieces for
other characters:
* **Body**: a skin-modifier vertex graph (joints plus radii) at the PC rig's proportions, then subdivision, a voxel
  remesh and a corrective smooth. Each face takes the material of the nearest graph edge.
* **Head**: a UV sphere shaped by gaussian features (nose, brows, sockets, lips, chin), plus eyes, lids and ears.
  Lips and cheeks are painted into a `paint` byte colour layer.
* **Clothes**: rings fitted to the body (`ring_band`: hems, belts, cuffs, boot tops) and cords on the surface.
* **Hair**: tapered curves following the scalp, over a scalp shell.
* **Rig**: bones placed and named like the PC rig, so clips can be retargeted by name. It uses automatic weights for
  the body and clothes, and the head bone for the head parts.
* **Game LOD**: a triangle budget per part (`LOD_BUDGET`). The engine allows at most 4,000 vertices and 4 weights per
  vertex; Drakan 2 humans have 1,900 to 2,300 triangles at LOD 0.

Workflow that worked: clay renders first (front, side, face), read every PNG and fix the anatomy, then colour. Pose
the rig once (arms down, knee bent) to check the weights.

Blender scripting gotchas met so far:
* Byte colour layers: set `.color_srgb`, not `.color`, which is linear. Mixing float and byte layers made the face
  and body different tones.
* Linear albedo: skin is about (0.5, 0.3, 0.2); 0.7+ renders near white under AgX.
* Render and save paths: pass absolute paths. Relative ones resolve against the drive root.
* The `.blend` is saved before any render camera is placed. Set the camera yourself when you render from a saved file.
* Curves do not take armature weights: convert clothing cords to meshes before `ARMATURE_AUTO`.
* Decimating many thin tubes (hair) cannot reach a budget. Give the LOD a hair shell or cards instead.
* Cycles on the RTX card: `compute_device_type = "OPTIX"`.

## 5. Put a new model in the game (authored models)

A Blender mesh replaces a PC character at convert time (`editor/packages/formats/src/ootfauthored.ts`, reference
`docs/reference/modding.md` "Authored models for converted OotF characters"). The converter takes UVs, textures and
weights from the player's own PC model, so only geometry is committed.

* Export with `analysis/scripts/blender/authored_export.py` (`export_authored(obj, path, model=<PC model name>,
  label=..., fit="joints", extra={"landmarks": {...}, "keepPcJoints": [...]}, slots={<material>: {"pcTextures": [...]}})`)
  into `scripts/buildtool/kit/models/<name>.json`. Examples: `delon_model.py --authored`, `boss_blade_dragon_model.py`,
  `ebon_dragon_model.py`.
* Build the mesh where the PC mesh is (same bind pose). Landmarks must be consistent: a PC joint whose sideways or
  height position differs a lot from yours (Delon's collar and neck) makes the piecewise warp steep and stretches a whole
  band of the body; leave such joints out.
* Name the PC texture entries per slot when the PC model has cut-out (alpha) textures: the converter leaves them out of
  slots that do not name them.
* Preview the fit as the converter does it: `dump-pc-model.ts ... --out <dir> <model> --authored <json>`, then import the
  OBJ in Blender (`analysis/scripts/blender/preview_authored.sh <json> <container> <model> <name>` does both and renders a comparison).
* In game: `scripts/harness/ootf_level_run.py <level> --campaign --convert-arg=--authored --convert-arg=<abs kit models
  dir>`. To film a scene, add `--env DRAKAN_AUTOPRESS_STOP_AFTER_LOAD=1` (else the harness taps through it) and use a
  recipe without `sync play` (that waits until the player has control). Creatures can be pulled next to Rynn with the
  converter test aid `DRAKAN_DBG_PULL=<class regex>` (`DRAKAN_DBG_PULLFLAGS=1` shows hidden ones).

After the work, write a dated note in `docs/journal/<date>/` (no em-dashes) and commit the scripts only.
