# Editor UX overhaul: a docked level editor (2026-10-31)

Goal: a modern click / drag-and-drop level editor (engine-editor style), not a copy of the 1999 OotF editor (that note
was only a feature checklist). Code: `editor/packages/app/src/editor/*`, `level.ts`, `levelui.ts`;
`editor/packages/editor-core/src/{snap,arrange,clipboard,catalog,schema,validate}.ts`; `formats/src/lvl.ts` additions.

## What the Level tab is now

* **Layout**: toolbar, left Outliner, centre viewport, right Inspector, bottom dock (Asset Browser, History, Validation, Mod),
  status bar. Splitters resize, double-click collapses, sizes persist (localStorage, optional). Dark and light themes
  (`data-theme`, OS preference by default). F1 or `?` opens the shortcut overlay (`editor/shortcuts.ts` is the single table).
* **Viewport**: click select, box select (drag on empty space, Shift/Ctrl adds), hover highlight + tooltip, move / rotate
  (yaw) / scale gizmos, world/local, grid snap (x/z, 32..2048 units) and angle snap, snap to ground, alt-drag duplicates,
  align / distribute, frame (F), top view, camera bookmarks (Ctrl+Shift+1..9 save, Shift+1..9 jump), fly camera (right mouse +
  WASD/QE, Shift fast, wheel = speed, slider), alt+left orbit, middle pan. Link lines (selection / all / off). Hide and
  lock per object or group. `LevelView` keeps the old controls by default (`setEditorControls(true)` turns the new ones on) so
  the Save tab is unchanged.
* **Asset Browser**: every class of the level's dependency ODBs (12 in the synthetic test, ~1000 on a real level), grouped by
  ODB category with Favourites and In this level, search by name/group/id, tile size slider, per-class details (OotF class id =
  low 16 bits of the runtime class). **Drag a tile into the viewport** (ghost marker while dragging, grid-snapped) or
  double-click to place at the view centre. Ground under the cursor: terrain meshes/height function when provided via
  `LevelView.setGround({ meshes, height })`, else the first object marker hit, else a plane at the height of the nearest
  object. Thumbnails: `levelUi.assets().setThumbnailProvider(cls => canvas | img | dataUrl | null)`; icon by kind until then.
* **Inspector**: transform (drag a label to scrub), scale where the object has flag 0x100, per-instance fields from the LVL
  parameters, widgets by `field_def` type (int, float, bool, enum with observed values, colour ARGB + alpha, class id with
  name, object reference with pick-in-viewport, plain id; strings and arrays read only), multi-edit (mixed values show
  "mixed"; one undo step), reset to class default, link list with add (pick) / remove / clear.
* **Undo history** (click to jump), dirty indicator (compares the stack top with the saved one), **autosave** to IndexedDB
  (`editor/autosave.ts`, try/catch everywhere, offers Restore when the level is opened again), **Validation** panel (dangling
  links, self links, duplicate ids, objects outside max_w x max_h cells, non-finite transforms, unresolved classes; click to
  locate), **Mod** panel (id, name, version, author, changed entries with click-to-locate, size vs whole-entry limit, Export
  zip / Write to mods folder, Play-in-game steps and a PowerShell command).

## Format additions (`formats/src/lvl.ts`, `editor-core/src/lvl-doc.ts`)

* `addObject(list, {classId, dbIndex, pos, rot, scale?, id?, templateId?})`: body copied from an instance of the class
  (flags, radius, events, scale, data and parameters; links emptied) else a minimal default body (flag 0x01, one initial event,
  no parameters). Id = max + 1, so ids stay ascending. **Limit:** class defaults are not decoded, so a default-body object
  (a class with no instance in the level) is unverified in game; export warns.
* `setObjectLinks`, `paramValue`, `setParamScalar`, `removeParam` (reset). Reset is refused when the object has string/array
  parameters (their offsets point into the data area; shifting them is unverified). Strings and arrays are not editable.
* `diffDocument` gained `linked` and `params`; `applyDocumentToLvl` writes new classes via `addObject`, remaps links between
  new objects to their exported ids, and applies link / parameter edits. A same-level copy still exports as `duplicateObject`.
* Copy / paste uses the browser clipboard (JSON). Same level: exact duplicates. Another level: each class is retargeted by
  ODB stem + class id (then name) through the target level's catalog and exported as a new instance; classes the target level
  does not have are skipped and reported; links are dropped.

## Verified

* vitest: 44 new tests (`editor-core/test/editor.test.ts`): add-object, multi-edit, copy/paste incl. cross-level, snapping,
  align/distribute, validation, catalog, history/dirty; 136 tests in the workspace (vitest 3.2.7).
* Playwright (Chromium, dev server) on a synthetic 400-object level injected through `levelUi.openLoaded`: layout in both themes,
  help overlay, asset drop (synthetic `DragEvent`s: ghost shown, object added at the snapped ground point, templated), inspector
  edit and multi-edit, Ctrl+Z / Y, W/E/G keys, Ctrl+D, copy and paste events, box select, hover tooltip, click pick,
  validation badge (15 dangling links), mod panel. Screenshots were kept out of the repo.
* Not verified: native HTML5 drag from the Playwright MCP (it does not start native drags; the handlers were driven with
  synthetic drag events), real disc levels in this UI build, the File System Access write, alt-drag and the gizmo with a mouse
  (typecheck and logic only), rotation beyond yaw, snap to ground against real terrain (no terrain API yet; the buttons say so).
* Render hooks left for the other agent: `LevelView.setGround`, `setThumbnailProvider`; markers are still instanced pyramids.
