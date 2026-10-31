# Editor: the level editor shows the real level (2026-10-31)

Wires the terrain/model decoders (`editor-rendering.md`) into the docked editor. Code: `editor/packages/app/src/{sceneload,level,levelui}.ts`,
`scene/{layer,instances,pickmath,viewopts}.ts`, `editor/{assets,toolbar}.ts`; tests `app/test/scene.test.ts`.

## What changed

- **Loading** (`sceneload.ts`): opening a disc level calls worker `loadTerrain`, then `loadModels` in batches of 24 classes
  (status shows progress; the viewport renders between batches). Terrain feeds `LevelView.setGround({height, meshes})`, so
  drag placement, snap-to-ground, "To ground" and the fly camera use real heights. The scene is framed on the terrain bounds.
- **Models** (`scene/layer.ts`, `ModelLayer`): one reusable `InstancedMesh` per class (`resource` string key); markers remain for
  classes with no/undecodable model (Surdana: 109 of 538 classes) and as a toggle (fallback / all / off). Textures with identical
  pixels share one GPU texture (`TextureCache.addShared`).
- **Picking**: `ModelLayer.pick` raycasts the instanced meshes (instance index -> object id), nearest of models and markers wins;
  marker pixel fallback only for markers (models are picked by surface). Box select uses every drawn object. Hover and selection
  = emissive tint of the same instance plus an oriented bounding box (an inverted-hull outline was tried first: it filled the
  alpha-tested foliage quads solid). Link lines unchanged.
- **Thumbnails**: the Asset Browser asks lazily (IntersectionObserver) for tiles in view; the loader fetches that class's model
  through the worker (serial queue) and renders one thumbnail per animation frame (6 ms budget) with `classThumbnail`, cached.
  Tiles update in place (no rebuild). The drag ghost image and the in-viewport ghost use the real model once loaded.
- **Display menu**: terrain on/off and wireframe, textured models, marker mode, helper groups (Triggers, Sound, Cameras, Lights),
  lighting presets (Day / Overcast / Dusk / Flat), fog, draw distance (camera far, default 3000 units), keep fly camera above the
  ground. Persisted in localStorage. Status bar: fps, draw calls, triangles. Helper groups are matched by class name
  (`scene/instances.ts`): "Trigger Detector", "Looping Sound", "Static (Vertex) Light" are real boxes in the data; "Light Chair",
  "Boulder Wide (Light)" are scenery and are not matched. All groups are shown by default.
- **Rough edges**: prettier clean; **drag and drop now uses pointer events** (native HTML5 drag could not be started by a real
  mouse in Playwright: no `dragstart` fired); the placement point re-reads terrain height at the grid-snapped x/z, so drops sit
  exactly on the ground; placing a class with no instance in the level shows a warning toast and a Validation warning
  (`default-body`, only for disc levels).

## Verified (Playwright, real ISO through the worker, dev server stopped afterwards)

- Surdana: 4675 objects, terrain plus textured models, 3356-4179 of them as models, 273-496 markers depending on helper
  options; load to interactive about 1.2 s after the level opens. Screenshots stay in the session scratchpad (game content).
- Real mouse: hover and click selected Birch Tree #9948 (box outline); gizmo X-arrow drag moved it 646092 -> 649984 (grid snapped),
  one undo step; real mouse drag of the Birch Tree tile onto the terrain added an object at y == terrain height (167424);
  Export mod produced `editor.level1na-edit.zip`; Validation listed the default-body warning.
- Performance: whole-level view, all helpers shown: 60 fps (rAF cap) with 1429 draw calls and 1.77M triangles, also while the
  camera moved for 3 s. That is the headless Chromium GPU path on this machine, not a desktop GPU measurement. Per-class
  instancing has no per-instance culling: classes with instances spread over the whole map are drawn whole; splitting batches
  per 16-cell chunk would cut draws further if a slower GPU needs it.
- vitest 3.2.7: 164 tests (14 new: pick math, instance partition/bookkeeping, layer pick/ghost/reset, texture sharing, options,
  default-body validation); `tsc`, `npm run build`, prettier check clean.

## Limits

- Rotation beyond yaw is still unverified (YXZ order); alpha-tested leaves pick by their quad, not their silhouette.
- Models of classes outside the level load on demand (thumbnail or drag) and persist for the session only.
- Terrain lighting is plain Lambert (stored vertex lighting not applied); terrain picking for placement uses the mesh, so
  stacked layers (caves) place on the first surface the ray meets.
