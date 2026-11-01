# Unreal export: level to glTF (2026-11-01)

`File > Export > Level for Unreal (glTF)` (command `file.exportUnreal`, palette). Format, coordinates, extras and the import
script contract: `docs/reference/unreal-export.md`. Code: `editor/packages/app/src/unreal/` (`coords.ts`, `glb.ts`,
`levelglb.ts`, `collect.ts`, `pbr.ts`), dialog `src/editor/unrealexport.ts`, UE5 helper
`editor/packages/app/public/ue5/drakan_import.py`. Tests: `packages/app/test/unreal.test.ts`.

## What was proven (Surdana, player's PAL ISO, own Chromium via Playwright, production build)

- The command ran in the browser (palette, dialog, Export), the zip downloaded and the glb loaded back in three.js `GLTFLoader`
  and rendered (screenshots under `build/unreal-export/`, game content, not committed): textured terrain, buildings, trees;
  Rynn skinned and posed by her `NCIdle` clip.
- Full level (plain materials): 327 terrain chunks (169,348 triangles), 378 static classes, 66 skeletons, 4,263 instances,
  411 markers, 456 textures. First file 128 MB (70 MB JSON, 2,822 animations); now shared time accessors and 24 clips per class by
  default. Export takes about 25 minutes under the current machine load (skeleton and clip reads in the worker dominate).
- Khronos validator (`gltf-validator` 2.0.0-dev.3.10, node): the first file had 90 errors (empty primitives, quaternion
  1.0000001, zero normals, animation keys going back in time); all fixed. Final files (full level with a PBR subset, and a
  skeletons-only export): 0 errors, warnings only (degenerate triangles in the source meshes, generated tangent space, skinned mesh
  not at the scene root, empty nodes, unused accessors).
- Bug found by the load-back, not by the unit tests: the inverse bind matrices were not transposed (the first test asserted the
  wrong column). Rynn came out as shards. Fixed, with a regression test using rotated joints
  (joint world times inverse bind = identity).
- PBR pack: the dialog picked a subset of `build/pbr` (the full 215 MB pack makes the Playwright file chooser crawl); 27 materials
  matched by `<TXD path>#<id>`, the rest fell back to base colour; 55 MASK materials. `window.__unrealExport.pbrMissing` reports
  unmatched sources.
- `python drakan_import.py level.glb --plan` parses the glb and lists the spawn plan (5,002 entries) without Unreal.
- 17 unit tests (`packages/app/test/unreal.test.ts`): conversion, extras, PNG, geometry, terrain winding, instancing, skinned
  classes in all root-motion modes, PBR resolver, regressions.

## Not proven

- Nothing was run in Unreal Engine (not installed). The UE glTF axis mapping `(X,Y,Z) = (x,z,y)*100` is an assumption; the extras
  carry the Unreal transform so actor placement does not depend on it, but mesh orientation does. The import script targets the
  documented UE 5.3-5.5 Python API and is untested.
- Cameras (orientation) and light radius/intensity are guesses. Skinned-mesh weights are the editor's (4 influences).

## Decisions

- glTF space = editor scene space (Y up, metres) so a loaded glb looks like the editor; the model unit scale is baked into vertices.
- Skinned classes are exported once as skeleton assets; placed instances reference them through extras (a bind-pose static mesh
  makes them visible in viewers).
- PBR pack (hd-textures.md section 10) is picked in the dialog; match by `<TXD path>#<id>`, normals green-flipped. `SceneTexture`
  gained `txd`. Models stay double sided (per-polygon flags not exposed by `Model`).
