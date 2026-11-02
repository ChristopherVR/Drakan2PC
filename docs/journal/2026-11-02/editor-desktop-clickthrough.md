# Desktop editor click-through: project Play, revert, save editor Add item (2026-11-02)

Goal: click the features committed today (`b54a5cb`, `39fea6d`, `da83d01`) in the real desktop app with the real disc,
not just their unit tests.

Setup (recipe of `docs/journal/2026-10-31/desktop-e2e-playtest.md`): debug build (`tauri build --debug --no-bundle`)
copied to `build/scratch/clickthrough/editor*.exe`, private WebView2 profile, CDP on 9395, real
`Input.dispatchMouseEvent` clicks (`build/scratch/clickthrough/drive.mjs`, `lib.mjs`, step files `s*.mjs`, `r*.mjs`,
`v*.mjs`). Private play folder `build/scratch/clickthrough/play` (top-level files of `dist/DrakanPC`); the shared
`settings.json` was backed up and restored. Native folder/file dialogs were filled with UI Automation + `WM_SETTEXT`
(`pickfolder.ps1`, `pickfile.ps1`: the dialog's controls show up as panes, so the edit is found by control id 1152
(folder) / class `Edit` id 1148 (file) and the button by name). Save files: a copy of `build/final-game/mc0` in
`build/scratch/clickthrough/saves/mc0`, never the originals. Screenshots: `build/scratch/clickthrough/shots/`.

## 1. Project panel: PASS

* New project "Clickthrough Mod" created through the native folder dialog; Level 1A and Level 1B added with the Add
  button (`03-levels-added.jpg`).
* **Play on the Level 1B row**: game log (`game-play-kragmor.log`): `[game] playing d2`, `[playtest] New Game opens
  'Level 1B - Kragmor' in place of 'Level 0 - Intro'`, memory card warning skipped, `level load '.../Level 0 -
  Intro.lvl' -> '.../Level 1B - Kragmor.lvl'`, `autostart: player under control` at about 22 s. Game window grab
  `05-game-kragmor.jpg`: Rynn on Arokh over Kragmor with the HUD. No title, intro or save; the play folder's `mc0`
  stayed empty.
* **Exits + Change destination**: Level 1A's "11 exits, 1 to fix" expands to the list (3A has no exit back). "Change
  destination" on the 1A -> 1B exit opened Surdana, selected #12250 "Transition from 1(A) to 1 (B)" and opened the Logic
  panel with "Destination (LEVEL1ON.XDB string 13)" set to Level 1B and the "Only this exit" box
  (`07-change-destination.jpg`). The picker sits below the fold of the Logic side pane; scrolling is needed to see it.
* Oddity, not fixed: one Surdana exit row reads "-> portal [portal] Portal: arrives correctly" (the destination name
  is "portal").

## 2. Level editor revert: PASS (after a fix to the Details fields)

Object #12250 (the 1A -> 1B exit), X 729088 -> 731088 typed into the Details X field:
* Details shows "Changed from the original level" + "Revert to original" (`08-moved.jpg`); the button put X back
  (toast "Reverted Transition from 1(A) to 1 (B) #12250 to original"); Undo brought the move back, Redo reverted again.
* Right-click on the Outliner row: the menu has "Revert to original" (`14-rightclick-menu.jpg`); it reverted.
* Package and Play change list: "Transition ... #12250: moved / CHANGED / Show / Revert"; Revert emptied the list.
* Deleted via right-click > Delete: the list shows "Deleted ... #12250 / DELETED / Revert"; Revert restored the object
  with its links (`[12823, 18019, 18026]`; Trigger Detector #12249 still links it) and the object count (4675).
  Undo -> 4674, Redo -> 4675.

**Bug found and fixed**: after an edit, the Details number fields went back to the old values (X field 729088 while the
object was at 731088; after Undo too). `ctx.edit` replaces the edited objects in the document, while the in-place
refresh (`updaters`) read the objects captured when the panel was built. `editor/inspector.ts` now keeps the array the
sections read and swaps the fresh objects into it on each in-place refresh. Re-checked in the rebuilt app: edit, undo,
redo and revert all show the document's value (`r2.mjs`, `r3.mjs`, `33-reverted-field.jpg`).

## 3. Save Game Editor: PASS (after a fix to opening its window from the page)

* Opened from the native menu Tools > Save Game Editor (own window "Drakan Save Editor"), Open folder -> the copied
  `mc0`, slot "Surdana", Inventory. Disc: Open loaded the ISO, names resolve (Rusty Scimitar 0x3372d).
* Add item Rusty Scimitar eight times: each new one takes two cells in one column (under Rynn's Dagger, then columns
  2-8, rows 1-2), the 3rd row stays free. The dropdown then marks it "- no room" and choosing it again says
  "Rusty Scimitar does not fit: it is 2 cells tall and no column has 2 empty cells one above the other." (toast + status
  line, `26-does-not-fit.jpg`). A one-cell Health Elixir still goes into the 3rd row (`28-elixir-fits.jpg`). Nothing
  was saved to the file.
* **Bug found and fixed**: `window_open` (the command the page uses to open the other window, e.g. the main window's
  Save tab and Open .rsg) was a synchronous Tauri command, so the new window was built on the main thread: the save
  window stayed on `about:blank` and the calling page hung. The native menu path did not hit it. Made it `async`
  (`desktop/src-tauri/src/windows.rs`); after the rebuild `invoke('window_open', {which: 'save'})` returns `ok` and the
  save editor loads.
* Small fix: the inventory header said "1 items" and ignored new items; it now reads "1 item, 9 new (not saved yet),
  0 equipped, 0 in quick slots" (`save/inventory.ts`, `27-summary-fixed.jpg`).

## Other fixes

* The Project panel's per-level Play buttons stretched across the whole row: the Package and Play panel's `.play` rule
  (`flex: 1 1 340px`) matched `ui-btn play`. Scoped to `div.play` (`editor/editor.css`, `levelui.ts`); the buttons are
  58 px wide now (`31-project-fixed.jpg`).

## Left over

* After a revert leaves a level equal to the disc, reopening the project still says "with edits to 1 level".
* The save editor's status line keeps the last "does not fit" message after a later Add item succeeds.
* The save list shows the file's copy time as the save time (it is the file modification time).
* After Locate, the Details panel keeps its scroll position, so the header and revert bar can be scrolled out of view.
* The Details sections built for one object (viewport animation, animation, vendor) still take the object captured at
  build time; they rebuild on most changes, but were not checked after an edit.

## Leftovers fixed

* Exit shown as "portal": the exit index carries the destination text of a "Transition To 'portal' object" exit, which is
  the word `portal` (with `portal: false`), and the Project panel printed `e.dest` as is. New `exitDestinationText` and
  `isPortalWord` in editor-core `project.ts` give "Portal inside this level"; the panel uses them and drops the extra badge.
* "with edits to 1 level" after a revert: the level's autosave stayed (undo depth above 0, or the project file already
  held it) although its diff against the disc was empty. `levelEditCounts(deps, p, true)` now clears the autosave of a level
  whose stored edits change nothing (`uneditedTitles`); the Project panel refresh calls it, and the project save calls it
  first (`FolderHost.dropUnedited`), so the file no longer carries the level.
* Details scroll: a different selection now resets the scroll to the top (`scrollAfterRebuild`); the same selection keeps it.
* Per-selection Details sections: the animation and vendor sections read `o.fields` of the object captured at build time.
  They are now built through `rebuilding()` in `inspector.ts`, which rebuilds them in place when the live object's fields
  print changes (edit or undo). Viewport animation depends only on the resource and the animation service, and the
  Fields, Transform, Links and revert-bar sections already read the live array or ids, so they needed no change.
* Tests: editor-core `project.test.ts` (destination text), app `project-store.test.ts` (unedited titles),
  `inspectorsync.test.ts` (scroll, fields print).
