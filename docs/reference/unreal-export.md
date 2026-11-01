# Unreal export (glTF)

`File > Export > Level for Unreal (glTF)...` (palette: "Export > Level for Unreal", command `file.exportUnreal`) writes the
open disc level as a glTF 2.0 binary plus PNG textures, for an Unreal Engine 5 import. Code: `editor/packages/app/src/unreal/`
(`coords.ts`, `glb.ts`, `levelglb.ts`, `collect.ts`) and `src/editor/unrealexport.ts` (dialog). Import helper:
`editor/packages/app/public/ue5/drakan_import.py` (served by the editor, also put into the export zip).

The export contains game content decoded from the player's disc. It is written to the user's chosen location (download or
save dialog), never into the repository.

## Output

A zip `<Level>_unreal.zip`:

| File | What |
| --- | --- |
| `<Level>_unreal.glb` | the scene (meshes, skins, animations, cameras, lights, extras) |
| `textures/T_<Scope>_<ref>_BC.png` | base colour PNGs (omitted with "embed textures") |
| `export_report.json` | counts and warnings |
| `drakan_import.py` | the UE5 helper |

Scene layout (glTF nodes under the single scene):

- `Terrain`: one mesh node `SM_Terrain_<cx>_<cz>` per chunk (default 16 x 16 cells, split by cell position, one primitive per
  texture). Smooth normals over shared positions of a chunk. Holes and invisible cells are left out.
- `Objects`: one node per placed object, named `<Class>_<id>`. Objects with a model reference the class mesh `SM_<Class>`
  (plain nodes, or one `EXT_mesh_gpu_instancing` node per class with the option). Objects without a model (triggers,
  markers, spawn points...) are empty nodes. Editor helper classes (`helperGroupOf`: triggers, sound emitters, waypoints,
  timers, spawns, barriers) are empty nodes with `extras.drakan.helper = <group>` and no mesh unless `helperModels` is on (their
  models are big boxes). Camera-kind objects get a glTF camera, `light` helper classes (Static / Dynamic (Vertex) Light) a
  `KHR_lights_punctual` point light; both are guesses (flagged `cameraGuess` / `lightGuess`: the real camera orientation and
  light radius are not decoded).
- `SkeletalAssets`: one node `SK_<Class>` per skinned class (laid out in a row at z = -6 m, 6 m apart) holding the joint
  hierarchy, the skinned mesh `SK_<Class>_Mesh`, and (root motion `separate`) a `SK_<Class>_RootMotion` node. Placed
  instances of such a class are empty-ish nodes whose extras name `skeletalMesh`; with "bind pose mesh" (default on) they
  also reference a static `SM_<Class>_Bind` so viewers show something. Skins, `JOINTS_0` / `WEIGHTS_0` (4 influences) and
  inverse bind matrices are per class, so a UE importer makes one skeleton and skeletal mesh per class.
- Animations: one glTF animation per ADB clip that fits the rig, named `<Class>@<clip>`, rotation channel per joint, translation
  channels where the clip keys them. All meshes are double sided materials (the editor's models are, polygon flags are not
  applied).

## Coordinates and units

| Space | Axes | Unit |
| --- | --- | --- |
| Game / editor | Y up | 1 game unit = 1 mm of scene; a terrain cell is 2048 units, so 2.048 m |
| glTF (`.glb`) | Y up, right handed, same as the editor scene | metres: position = game units * 0.001; a model unit (= one cell) = 2.048 m, baked into vertices |
| Unreal | Z up, left handed | centimetres |

Unreal's glTF import converts glTF to Unreal as `(X, Y, Z) = (x, z, y) * 100` (axis swap, determinant -1, which also fixes the
handedness). The exporter assumes exactly this mapping (`UE_FROM_GLTF` in `coords.ts`). Resulting scale: 1 game unit = 0.1 cm,
a terrain cell = 204.8 cm.

- Position: glTF metres `(x, y, z)` to Unreal cm `(100 x, 100 z, 100 y)`.
- Rotation: the editor applies stored degrees as Euler YXZ with yaw = stored yaw - 90 (level-format.md section 3). In
  glTF that is the node quaternion. Unreal quaternion of the same rotation: `(x, y, z, w) -> (-x, -z, -y, w)` (the axis
  is swapped and negated because the swap reverses handedness). Unit tests prove it matches the editor matrix and that
  `R_ue (P v) = P (R v)`.
- Scale: object scale `(sx, sy, sz)` to `(sx, sz, sy)` (the local up axis becomes Z).
- Skeleton: joint matrices in the data are row-vector (`v' = v R + t`); glTF is column-vector, so the rotation is
  transposed (`mat12ToQuat` / `mat12ToGltfMat4`) and translations are scaled to metres.
- Textures: the editor's decoded rows are bottom-up; the PNG is written top-down and V is flipped (`v' = 1 - v`).

The `ue` block in every instance's extras holds the same instance transform already in Unreal space, so the import script places
actors from the extras and does not depend on the importer's node-transform conversion. If the importer used another mapping
the imported meshes would not line up with the spawned actors: change `UE_FROM_GLTF`, `gltfPosToUe`, `gltfQuatToUe` and
`gltfScaleToUe` (one place) and re-export. This mapping was NOT verified against a UE install (none on the dev machine).

## Extras (`extras.drakan`)

Every node that came from an object has `extras.drakan`:

```
type        'instance' | 'terrain' | 'skeletal' | 'instances' (instancing option)
id          object id in the level document
class       class name (as shown in the editor)
resource    class resource string (dbIndex << 16 | classId)
kind        player | camera | creature | weapon | usable | static | other
typeId      registry type number
db          ODB stem of the class (when known)
origin      LVL object id it was loaded as (absent for new objects)
links       ids of objects this one triggers
props       document properties (hp, flags, ...)
fields      per-instance LVL parameters [{slot, index, type, name, value}]
gamePos     [x, y, z] game units;  rot  radians as the document stores them (yaw already - 90 degrees)
gameScale   LVL scale triple, when present
staticMesh / skeletalMesh   asset names to look up in Unreal (SM_<Class>, SK_<Class>)
ue          { location [cm], quat [x,y,z,w] Unreal, scale }
```

Animations carry `extras.drakan = {type:'clip', class, skeletalMesh, clipId, clipName, stem, duration, loops, rootMotion,
rootMotionNode}`. The scene and asset `extras.drakan` hold the unit and conversion notes.

## Root motion

The clips key the root joint's translation as the character's motion. Option (dialog, `ExportOptions.rootMotion`):

- `separate` (default): the root joint keeps its vertical movement and the first key's horizontal position; the horizontal
  displacement `(x - x0, 0, z - z0)` goes to a `SK_<Class>_RootMotion` node channel (`rootMotionNode` in the clip extras),
  which a UE importer or script can feed to root motion.
- `strip`: same joint track, no motion track (play in place).
- `keep`: the root joint's translation is exported unchanged.

## Materials

With the PBR pack (`analysis/scripts/pbr_pack.py`, hd-textures.md section 10) chosen in the export dialog ("PBR material pack
folder": pick `build/pbr`), every texture the level uses is looked up by its source `<TXD path>#<id>` (the disc path of the TXD
is carried on `SceneTexture.txd`; language folder stripped, forward slashes, upper case) in the JSON `sources`. A hit writes the
material `M_<Txd>_<id>` with the pack's `T_<Txd>_<id>_BC` (base colour), `_N` (normal, green channel flipped because the pack
is DirectX and glTF is OpenGL), `_ORM` (occlusion R, roughness G, metallic B: one texture used as both
`occlusionTexture` and `metallicRoughnessTexture`, metallicFactor 1), blend mode (`Masked` becomes `alphaMode MASK` with the pack's
`opacityMaskClip`, `Translucent` / `Additive` become `BLEND`), two-sidedness (terrain only) and emissive (BaseColor as emissive
texture). Textures without a pack entry fall back to baseColor only (`M_<Scope>_<ref>` with `T_<Scope>_<ref>_BC`, `MASK` at 0.5 for
model textures with translucent pixels); the number of unmatched sources is in the status line and `window.__unrealExport.pbrMissing`.

Models stay double sided: the per-polygon double-sided flag (0x0204 bit 1) is not exposed by `Model`, so it cannot be ORed in
per mesh. `ExportOptions.resolver` is the programmatic hook (`TextureSource -> ResolvedMaterial`; `unreal/pbr.ts` is one
implementation, covered by unit tests with a synthetic pack).

## Importing into Unreal

1. Enable the Python Editor Script Plugin and Interchange glTF import (UE 5.3 or later).
2. Unzip, then in the editor Output Log: `py "C:/path/drakan_import.py" "C:/path/Level_unreal.glb" /Game/Drakan/Surdana`.
3. The script imports the glb (assets only), then spawns actors from the extras: terrain chunk static mesh actors, static
   mesh actors per object, skeletal mesh actors playing an idle clip, cameras, point lights, and a `TargetPoint` for every
   other object. Actors are tagged `drakan`, `drakan.id=`, `drakan.class=`, `drakan.kind=`, `drakan.extras=<json>` and filed
   under the folder `Drakan/<Level>/...`.
4. `python drakan_import.py Level_unreal.glb --plan` (no Unreal) prints what would be spawned; this is how the contract is tested.

The script was written for UE 5.3 - 5.5 from the documented Python API and has not been run in Unreal.

## Limits

- LOD 0 only. Models are exported double sided; polygon flags and vertex colours are not applied.
- Terrain normals are smoothed per chunk, so chunk borders can show seams in lighting.
- Cameras and lights are guesses (see above); no collision, triggers or navigation data.
- Clips that do not fit a rig (joint count differs) are listed in the report warnings and skipped.
- One skeleton per class, not per model variant of a class.
