# Editor: Level view, document model, mod export skeleton

## What landed (`editor/`)

- `packages/editor-core` (pure TS, vitest): `parseWorldDump` + `classifyType` (thin adapter over the world JSON dump),
  `LevelDocument` (objects with id, class, type, kind, resource, model, transform `pos`/`rot`, free `props`; JSON
  save/load as `drakan-level-document` v1), `CommandStack` with Transform, Delete, Add/Duplicate and SetProp commands
  (undo/redo, stable ids across redo), `buildModZip` + `srscRoundTrips`.
- App: "Level" tab (`level.ts`, `levelui.ts`). One `InstancedMesh` per entity kind (7 draw calls for 4600 entities),
  click pick, shift-click multi-select, three.js `TransformControls` on a pivot (move / yaw rotate, one undo step per
  drag), kind / class / search filters, property panel with editable position, yaw and props, fly (WASD/QE) + orbit,
  frame selection (F). Dump via picker or drag-drop of a `.json`.
- Export mod: writes `manifest.json` + `overrides/<FILE>/<type>_<id>_<sub>.bin` for the first entries of the SRSC file
  open in the Inspector, after the writer self-test (unchanged rewrite equals the source). **Provisional** manifest
  (`format: drakan-mod-provisional`) and decimal file names; replace in `editor-core/src/mod.ts` once
  `docs/reference/modding.md` exists. No level edits are encoded yet (needs the decoded level object format).

## Findings / caveats

- Smoke test with a local Surdana dump (4602 entities): loads, select, move, undo/redo, duplicate work. Headless
  Chromium uses software GL (about 1 fps there), so 60 fps is by design (instancing) and not measured.
- World coordinates are scaled by 0.001 (y up). The dump only has yaw, so rotation is yaw about y; the marker
  tip faces +x at yaw 0, which is unverified against the game.
- Older dumps lack `hp`, so creatures (types 6/7) classify as "other" there.
- vitest stays on 3.2.7.
