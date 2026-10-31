# Editor: edit a real level and play the change (2026-10-31)

Code: `editor/packages/formats/src/discprofile.ts`, `editor-core/src/lvl-doc.ts`, `editor-core/src/mod.ts` (real
`modding.md` format), `app/src/modio.ts`, `levelui.ts`, `worker.ts` (`loadLevel`). Tests:
`editor-core/test/lvl-doc.test.ts` (synthetic only; 92 tests in the workspace, vitest 3.2.7).

## What the editor does now

* **Open from ISO.** The Level tab has an "Open level" list (names from LEVELLST). `loadLevel` in the worker reads the LVL
  with `readLvl`, opens the ODBs named by the LVL dependency table and `documentFromLvl` builds a `LevelDocument` with real
  object ids, ODB class names (Surdana: 4675 objects, 0 unresolved), resource ids, positions, rotations and scale. Yaw is
  stored as `rot_y - 90` (world dump convention) so a disc level and a world dump line up. A world dump loaded after a
  level is overlaid: objects pair by resource id and position (300 units), unmatched dump entities are drawn as cyan
  outlines (checkbox), counts go to the status line. A dump loaded first still opens as the document.
* **Edits to LVL.** Export is diff based (`diffDocument` / `applyDocumentToLvl`): the document is compared with the list it
  was loaded from, so undo/redo and any command order give the same result and untouched objects keep every byte. Move and
  rotate (whole degrees, mod 360 compare) map to `moveObject` / `rotateObject`; scale (new `SetScaleCommand`, only objects
  with flag 0x100) to `scaleObject`; duplicate to `duplicateObject` from the source id (`LevelObject.origin`), keeping the
  document id; delete to `deleteObject` with policy `unlink` (links to the deleted id are dropped and reported).
* **Export mod.** `mod.toml` (format 1, id, name, version, author, `[target] profile + hash`) and
  `overrides/<FILE>/<type hex 4>_<id>_<sub>.bin` (same naming as `parseEntryName`). Disc profile comes from SYSTEM.CNF
  (`BOOT2` -> serial `SCES-50006` -> saveId `BESCES-50006` -> FNV-1a 64 of `serial|saveId`); the PAL value is
  `b9f60ba118125a54`, equal to `runtime/hle` `profileHash` (tested). Output: zip download, or with File System Access a
  chosen play or `mods/` folder (`<id>/` plus `mods.ini` updated by `updateModsIni`, pack appended as `on`, comments and
  order kept). The export refuses an entry that would cross the 0x40800 whole-entry limit (runtime keeps the original
  there) and an empty edit set.

## In-game proof (private folder `build/level-edit-play`, nothing committed)

Setup: dist top-level files plus `build/final-game/bin/drakan.exe` (the dist exe fails the same way), `play_run.py --exe`,
`DRAKAN_WORLD=2` dump. Unmodded reference: Surdana start (`play_run.py` default; Rynn at 454515,171297,339526, camera
behind-left). Gotcha: `--out-root` must be absolute, otherwise the exe (cwd = its folder) cannot open the recipe and
`sync play` never fires.

1. **A pack in `mods/` does NOT change the LVL.** `[mods] loaded 'e2e.move' ... 1 entries` and the pack validates (profile
   and hash match), but the world dump is identical to the unmodded run. Cause: `LEVEL1NA.LVL` is opened with `file open
   ... handle 1 size 1652778 srsc=1/7` and the guest reads it with plain file reads; no `resource:` (RSRCIOP) request for any
   LVL entry appears in the HLE log (verbose, 3791 resource lines). The override layer only sees RSRCIOP entry requests.
   **Runtime change needed** (not done): apply mod entry overrides to the LVL at the FileService read level (rebuild the
   SRSC view of the file with the replaced entry), or implement the reserved `files/<PATH>` whole-file replacement and have
   the editor write the repacked LVL there. Until then LVL edits cannot be played through a pack.
2. **Proof path used instead:** a private copy of the ISO (`build/level-edit-play/iso_patch.iso`, `DRAKAN_ISO`), with the
   exact bytes the export produced for the 0x0020 entry written in place (same size, or shorter with the index size
   field updated), original region restored between scenarios. The game then reads the edited object list.
3. **Results** (world dump at the same point, compared with the unmodded dump, 4602 entities; screenshots compared by pixel
   diff outside the HUD and Rynn):
   * **Move** Birch Tree 01 id 9379 by 5000 units (451133,169966,349383 -> 448543,169966,345108): the dump has that actor at
     the exact new position and none at the old one. The screenshot differs only in the lower-left corner, where the trunk
     now enters the view (absent in the reference).
   * **Duplicate** (copy of 9379 at the new spot, a far tree with an identical record size removed so the list keeps its
     length for the in-place patch): the dump has a new actor at 448543,169966,345108, the original at 451133 stays, entity
     count unchanged (4602); same corner trunk in the screenshot.
   * **Delete** (the three Birch Tree 01 within 14000 units of Rynn, ids 9357, 9359, 9379, shorter list, index size
     updated): 4599 entities, exactly those three trees missing; the large trunk at the top right of the reference view is
     gone. No crash, the level plays through the first 15 s.
4. A first attempt moved tree 9359 4000 units ahead of Rynn: the dump moved but nothing showed. That tree is about 22000
   units from the camera, beyond what the view shows, so test objects must be chosen from the camera view.

## Not verified

* The browser UI itself (open list, export buttons, File System Access write) was only typechecked, built and unit tested;
  the e2e used the same `documentFromLvl` / `buildLevelPack` through Node.
* Rotation composition for tilted objects, scale edits, link-delete semantics (dangling links are unlinked, game behaviour
  with links to deleted objects not tested), duplicates of objects with triggers or parameters.
* Zip pack and folder pack were both written and the runtime loaded the folder pack; the zip was not loaded.
