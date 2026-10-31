# Story close-out B: editor integration (2026-10-31)

Scope: camera path in the 3D view, string reference counts with level objects, exit destination editing, one Story area.
Checked with unit tests (synthetic data), `tsc`, `npm test` (307 pass) and `npm run build`. **No browser or in-game run**
for these items (no screenshots in `build/scratch/content2/` from this part).

* **Story area.** `app/src/story/storyarea.ts` + `register.ts`: one dock tab "Story" (command "Show Story editors",
  `story.<tab>` commands "Story: Strings / Quests / Dialogue / Cinematics / Story pack") holding the five editors behind a tab
  strip. Before this, `initContentEditors` (Dialogue, Cinematics) was never called from `main.ts`, so those two panels were not
  reachable at all. Now `initStoryEditors({ call, setStatus, ctx, play, stop })` builds them (`contentregister.ts`
  `createContentPanels`), with the disc read through two new worker calls (`readStoryFile`, `listStoryStems`; `story/storyfs.ts`
  runs in the worker) and the open level's handlers / cut scene players from `story/placed.ts`.
* **Camera path.** `story/pathview.ts`: polyline plus keyframe dots (green first, red last) added to the level scene, fed by the
  Cinematics panel's `showPath` hook (scale 0.001 like the logic overlay, same axes as object positions: **assumed**, not
  compared against a rendered cut scene).
* **String references.** `formats/src/stringrefs.ts` `objectStringRefs` (string-typed, non-array *parameters* of placed objects,
  id = low half when the high half is 0), worker call `scanStringUsage` (every level, seconds), `StorySession.scanStrings` /
  `objectRefs`; the Strings panel has "Scan levels for object strings" and counts class defaults plus objects. Ids are attributed
  to the level's own XDB (first `LEVEL...` dependency); an object string that points into another table would be miscounted.
* **Exit destinations.** `logicdata.ts` now returns `destRef {file, id}` per exit class. The Logic panel's side panel shows a
  destination level picker for an exit (`exitPicker`); it rewrites the class's destination string through `StorySession.
  setExitDestination` (`withDestination`: every used language slot; `exitDestinationText` keeps `Levels\X\X.lvl` form) and leaves
  as an ordinary `overrides/LEVEL1ON.XDB/0400_<id>_<sub>.bin` entry in the Story pack (`exit-dest.test.ts`). Limits: it changes
  every exit of that class (one class per destination on the disc); a per-object override string and creating new exit classes
  are not covered; not run in game (whether a changed string moves the player is expected from section 15.4, unproven).

Tests: `formats/test/closeout.test.ts`, `editor-core/test/exit-dest.test.ts`.
