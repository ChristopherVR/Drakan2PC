# New Blender meshes for Delon, the Boss Blade Dragon and the Ebon Dragon (2026-11-17)

Owner request: the OotF models that have no Drakan 2 swap are to be built in Blender (the Boss Blade Dragon and the Ebon
Dragon as new meshes, not recolours of disc models), and Delon finished. Scratch, previews and runs are under
`build/authored/` and `build/ootf-run/{delon-rv4,boss-wc,ebon-isn}` (not tracked). Only names and counts are quoted here.

## How a Blender mesh reaches the game

New formats stage `src/ootfauthored.ts`, hooked in `OotfAssetBuilder.addModel` before the skin upgrade (reference:
`docs/reference/modding.md` "Authored models for converted OotF characters"). The committed file holds geometry only;
textures, UVs and weights are taken at convert time from the player's own PC model, so nothing from the game is in the
repository and the 1999 identity (Delon's face, the boss's green skin) is kept.

* The mesh replaces the PC LOD 0 on the PC skeleton: the PC clips, hit spheres (their centres come from the original
  vertices), scene channels and the synthetic jaw all still work.
* Placement by `fit` (`height`, `extent`, or `joints`: a piecewise per-axis warp onto the PC joints).
* UVs: each triangle projects onto the nearest PC triangle; head triangles along rays from the head joint.
* `keepPcJoints`: Delon keeps the 1999 head, smoothed once (the Drakan 2 method is a simple head with a painted face, and
  his painted face is the 1999 texture).
* Files: `scripts/buildtool/kit/models/*.json`, exported by `analysis/scripts/blender/authored_export.py`; the build passes
  the folder (`extras.py`, `--authored`) and converts again when a file changes. Preview without the game:
  `analysis/scripts/blender/preview_authored.sh`.

Bugs found on the way, all fixed and covered by `test/ootfauthored.test.ts`:
* **Winding.** PC meshes are clockwise from outside (they measure a negative signed volume); the (x, y, z) to (x, z, y)
  swap already mirrors, so the Blender order is kept as it is. Reversed, Delon's new body drew nearly black in game while
  his kept 1999 head was lit.
* **Scaled joints.** The Boss Blade Dragon's inverse binds carry a uniform scale of about 0.79; `-(t R^T)` put its joints
  at 62 % of their place. The origin is now `-t R^-1`. Found by the subagent that built the dragon.
* **Landmark warp.** A landmark far from its PC joint (Delon's clavicle 7.5 cm out against a collar joint 1.5 cm out; his
  neck 4 cm lower than the PC neck joint) made a piece of the warp steep: the waist widened, the arms doubled in
  thickness. Those landmarks are left out.
* **Texture bleed.** UVs extrapolated up to 50 % past a PC triangle reached the gore patches beside the clothes on the
  same sheet (red specks on Delon's tunic). Now 10 %.

## The models

| Model | PC (LOD 0) | New | Script |
| --- | --- | --- | --- |
| Delon | 282 vertices, 526 polygons | 1,367 vertices, 2,638 triangles (new body below the neck, 1999 head smoothed) | `delon_model.py --authored` |
| Boss Blade Dragon | the Blade Dragon mesh, 105 / 181, green skin | 1,592 vertices, 2,656 triangles | `boss_blade_dragon_model.py` |
| Ebon Dragon | 216 / 384 | 1,527 vertices, 2,712 triangles | `ebon_dragon_model.py` |

* Delon's body was reworked against the 1999 model: belt, buckle and boot tops moved to the 1999 heights, the tunic hem
  ends where the 1999 one does and no longer flares, torso, legs and arms slimmed by about 12 %.
* The dragons were built by two subagents in parallel, each to the same brief: original geometry from hand-measured
  proportions (no vertex lists, no textures), about 2,600 triangles like the Drakan 2 dragons, a head with horns, jaw and
  teeth, clawed legs, wing bones and membranes, spines; every slot names its PC textures (the wing textures are cut-outs).

## In game (`ootf_level_run.py --campaign --convert-arg=--authored ...`, harness window settings)

| Run | What the frames show |
| --- | --- |
| `delon-rv4` (Intro + Ruined Village, `DRAKAN_AUTOPRESS_STOP_AFTER_LOAD=1`, the opening plays) | Delon walks and gestures beside Rynn in scene 1.1 with his 1999 face, the new body lit like Rynn (after the winding fix): V-neck tunic, belt and buckle, cuffs, leggings, boots |
| `boss-wc` (Wartok Canyons, boss pulled with `DRAKAN_DBG_PULL`) | the new Boss Blade Dragon flies at Rynn, wings beating with their cut-out feathers, clawed feet, green skin |
| `ebon-isn` (Islands Night, Ebon Dragons pulled) | the dragons attack (their sonic rings hit Rynn); the level opens in a dark chamber and no frame shows a dragon clearly |

## Open

* Ebon Dragon: no clear in-game frame yet (the bind-pose previews are clear).
* Delon: pale patches at the top of the legs (the 1999 hem strip is on the trouser texture) and dark patches where the new
  neck meets the kept head. The body textures are the 1999 ones projected; painted Drakan 2 style atlases would need a
  bake whose result is original art (not done).
* Weights are transferred from the nearest 1999 surface; a dragon's membranes and ears were only seen in flight, not
  checked frame by frame.
* Not checked with the owner's own `drakan.ini`.
* The editor's in-browser converter does not load authored models yet (the CLI and the build do).
