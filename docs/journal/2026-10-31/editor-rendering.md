# Editor rendering: PS2 terrain and model decoding, three.js scene builders (2026-10-31)

Layouts and tags: `docs/reference/level-format.md` sections 10 (terrain) and 11 (models). Code: `editor/packages/formats/src/terrain.ts`,
`model.ts`; `editor/packages/app/src/scene/*`. Tests: `formats/test/terrain.test.ts`, `model.test.ts` (synthetic only).

## Results

- **Terrain**: PS2 0x0001 = definitions with a variable inline blob, then `u32 1` and one zlib block per layer (same shape as PC),
  block = `(w+1)(h+1)*6 + w*h*22` (vertex `u16, u16 mask, u16 biased height`; cell `u16 flags, u32 texture ref, 8 x u16 uv`). All 13
  PAL levels decode; the 23 PC files decode with the same code (`parsePcTerrain`, 4445 layers). Heights: Surdana objects on the
  ground (flowers) match the surface exactly, 866/4675 within 1 unit, 1437 within 50; trees are placed slightly sunk, sod grass
  +158 (pivot). Not "most objects within epsilon": many classes float or have pivot offsets.
- **Models**: 3700 of 3704 models on the disc decode. The "16-byte vertex" is positions (12 B) plus a separate normal array;
  polygons are index-only and UVs are a block after them; the texture list sits in 0x0200. LOD 0 selected through a signature
  search of the 0x0208 LOD table. Bones are not decoded (bind pose only).
- **Visual proof**: Surdana terrain (33 textures) and 1045 objects (175 models) rendered from the ISO: `surdana-overview.png`,
  `surdana-wide.png` in the session scratchpad. 262 draw calls, 226k triangles, scene build 0.4 s (software GL in headless).
  Harness: Node script packs decode output from the ISO into `build/edrender/demo/scene.json` (gitignored), a Vite config in
  `build/edrender/demo` serves a page that uses `scene/index.ts`. Dev server stopped.

## API (documented in `scene/index.ts`)

`buildTerrain(layers, lookup)` -> `{group, heightAt, dispose}` (merged per 32-cell chunk x texture, so frustum culling works);
`buildModel`, `modelGeometry(model, key)` (cached non-indexed geometry with a group per texture), `buildInstancedModels` (one
InstancedMesh per model), `instanceMatrix`, `classThumbnail(key, model, lookup, size)` (data URL, cached), `groundHeightAt`,
`raycastScene(camera, ndcX, ndcY, roots)`, `TextureCache.add(namespace, textures)` -> lookup. Units: scene = game units * 0.001
(`SCENE_SCALE`), model unit = 2048 game units (`MODEL_SCALE`). Worker side: `scene/decode.ts`, RPC `loadTerrain(dirPath)` and
`loadModels(dirPath, [{classId, dbIndex}])` added to `rpc.ts` / `worker.ts`.

## Integration points for the editor UX agent (not edited by me)

- `LevelView.setGround({ height: terrain.heightAt, meshes: [terrain.group] })` fits `GroundSource` in `level.ts` (height takes game units).
- Replace markers: group objects by `(classId, dbIndex)`, call `loadModels` once with the distinct pairs, build `ModelInstance`
  `{pos, rot, scale}` from the document and call `buildInstancedModels`; keep markers for classes without a model (triggers, timers).
  Picking an instance: `InstancedMesh` raycast gives `instanceId` (index into that model's instance list).
- Asset browser: `classThumbnail(String(classId), model, lookup)`.
- Rotation: `instanceMatrix` uses yaw = rot_y - 90, YXZ order; composition of pitch/roll is still unverified (section 3).

## Caveats / open

- Terrain texture ref has one entry per cell on PS2: both triangles share it. Holes `0xffffffff`, invisible `0xfffffffe` are skipped.
- The inline per-layer blob is skipped by structural resync (all 483 Surdana layers, all 13 levels); a real walk would be sturdier.
- Models: skeleton/animation, 0x0211, polygon flags (double sided is always on), four undecodable tables (section 11). Model texture
  `db` is resolved against the ODB's own `.DB`; a model reached through a non-zero `model_db` uses that container's refs the same way (untested).
- Terrain/model lighting is plain Lambert; the stored per-vertex lighting is not applied.

## For THIRD_PARTY.md (OpenDrakan, GPL-3.0, commit 6c0d499; no code copied)

`editor/packages/formats/src/terrain.ts`, `editor/packages/formats/src/model.ts`, `editor/packages/app/src/scene/terrain.ts`
(UV corner order and cell triangle split), `docs/reference/level-format.md` (sections 10, 11).
Sources: `doc/lvl_format.txt`, `doc/riot_database_format.txt`, `src/odCore/Layer.cpp`, `src/odCore/db/Model.cpp`.
