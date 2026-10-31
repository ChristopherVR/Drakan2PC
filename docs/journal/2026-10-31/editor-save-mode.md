# Editor: Save mode (`.rsg` editor in the browser)

UX modelled on AbioticEditor (open-save flow with slot discovery, character panel, world list with "show in 3D",
edits pending until SAVE, `.bak`, compare two saves, EXPORT); ideas only, no code or assets copied.

## What landed (`editor/`)

- `formats/src/save.ts`: port of `scripts/savetool/savetool.py`. Container parse/assemble (gaps, header pad, tail and
  compressed bytes kept; unedited saves re-serialise byte for byte), field table (`hp`, `gold`, `pos`, `skill.N`,
  `skill.N.max`) with confidence and notes, `applyFieldEdits` (final-state skill bounds, untouched entries keep their
  original zlib bytes, edits that change nothing are dropped), `probeField` (ok / unconfirmed / missing),
  CT16 screenshot decode (type 0x23: u32 w, h, bpp, size, then ABGR1555 rows top-down), `summarizeSave` (index plus
  only the screenshot and level strings, for listings), record description, `diffSaves`, `nextBackupName`.
- `editor-core/src/save-doc.ts`: `SaveDocument extends LevelDocument` (positioned `0x102` records become markers; the
  Rynn record is the player), `SetFieldCommand`. Because it is a `LevelDocument`, the existing `CommandStack` and
  `LevelView` are reused unchanged for undo and the gizmo. Pending edits are derived state (field map plus the player
  marker's transform), applied to the loaded bytes only in `build()`.
- App: Save tab (`saveui.ts`, `saveio.ts`). Open one `.rsg` (File System Access picker, else `<input>`/drop) or a
  folder (picker, else `webkitdirectory`): lists slots sorted by number with screenshot, level name, date (file mtime),
  record count. Character panel: hp, gold, position, skills with bars and confidence badges. A field whose layout does
  not match the verified one (savetool `--force` cases, e.g. hp in an entry-id 53 save) shows "unconfirmed" and stays
  disabled until the unlock box is ticked. World tab: groups by class or type with counts, search, per-record details
  (annotated fields, hex of unknown parts), "Show in 3D". The 3D pane is a second `LevelView`; `LevelView.policy`
  (new) disables add/delete/duplicate and rotation and restricts the gizmo to the player marker. Compare tab: field
  table plus per-record changed/unchanged list, against a file, a listed save, or "pending vs as opened".
- SAVE: with a folder or file handle, writes in place after copying the current file to `NAME.bak` (`.bak.1`, ...)
  next to it (with only a file handle the browser asks where to put the backup). EXPORT downloads the new `.rsg`.
  `?nofsa` forces the no-API path.

## Verification

vitest (synthetic fixtures `formats/test/savefixture.ts`), prettier, tsc, `npm run build`. A real harness save
round-trips byte-identically in Node. Playwright smoke (`?nofsa`, copies of two harness saves under `build/`,
deleted afterwards): list, open, edit gold and position, pending list, World search and details, 3D selection,
Compare, unlock of an unconfirmed hp, EXPORT. The exported file read back with `savetool get`, and only the two
edited entries differed in their compressed bytes.

## Caveats

- The File System Access write path (SAVE, `.bak`) was not exercised in a browser (Playwright cannot drive native
  pickers); it follows the documented API and the naming logic is unit tested.
- Not every local save has the player record (some harness saves have none): position and hp show "missing".
- Max HP is unknown, so the HP bar assumes 100. Gold in some saves holds a very large value (second record layout).
- Marker coordinates assume the record floats at +12 use the same units as world dumps (x0.001 in the scene).
- Moving non-player records is intentionally blocked: only the player position is verified in game.

## Second pass: AbioticEditor-style panels (2026-10-31)

- Formats: `arokh.pos`, `progress.KEY`, `listProgress`, `readItemPanel`, `validateSave`, `transferPlan`, `recordLabel`
  (`formats/src/save.ts`, new `save-state.ts`); `SaveDocument` exposes `progress`, `itemPanel`, `issues()`.
  `savetool.py` gained `progress`, `item`, `validate` and the same two fields. Layouts: save-editor.md, "Inventory,
  equipment and state".
- UI (`app/src/save/`: `dom.ts`, `map.ts`, `panels.ts`, `save.css`; tabs added from JS, `index.html` untouched):
  Character stat tiles with bars; World gets a top-down map (click a dot, filtered records emphasised, Rynn and Arokh
  rings) and a per-group "where it is" picture; Items (inspected item with durability bar, Arokh card; the inventory grid is
  explained as not decoded, so no add/remove/stack); Story (state table, filters, edit gated behind a checkbox);
  Transfer (gold, health, skill levels, positions between two saves, pending until SAVE); Repair (validation with
  undoable fixes). `classThumbnail()` had not landed; pictures are map-based for now.
- Tests: vitest `save-state.test.ts` (16), pytest additions (6). Playwright smoke on copies under `build/` (deleted).

## Save mode rework: authoritative stats, inventory grid, quests (same day, later)

- Character tab now reads the stats block (`stat.*`): real HP and mana bars (value / max), gold, level, XP and skill
  points tiles, editable HP, max HP, gold, XP, level, skill points. Mana stays read only (never changed in game).
  The old Rynn-record `hp` and gold-record `gold` sit under "Advanced / raw"; saves with no stats block show a note.
- Items tab: 8 x 3 inventory grid like the Tab screen (multi-cell items span rows from the continuation markers).
  Each cell shows name, art, durability bar and E / Q marks; an inspector edits durability / arrows / quantity, max,
  equipped and quick-slot flags. Add item is disabled ("not supported yet"), removal is not offered. Names: ODB class
  name when Level mode has a level open (`levelUi.classInfo`), else a table of the four proven items, else the id.
  Art: the in-game icons are 3D renders of the item model (the cell `icon` word is not a texture id), so the model
  thumbnail from `classThumbnail` is used when a level is loaded, else a coloured placeholder.
- Story tab: read-only quest rows (`listQuests` in `formats/save.ts`), above the state table.
- Compare lists stats and per-cell fields; Transfer copies the stats block (legacy hp/gold only when no block) and
  item counts of the same item in the same cell.
- Verified: vitest (176 pass), prettier, tsc, build; Playwright on a copy of a harness save; UI edit of max HP 100 ->
  333 exported, `savetool get stat.maxhp` = 333, and only entry 893 differed.

## Item art without a level; add/remove investigation (same day, fourth pass)

- **Item names and pictures need no level.** Every inventory item is a class of the shared `Common\Resources` ODB
  (`RESOURES.ODB`, 1344 classes on the PAL disc; db index 3 in every level except Level 0, and the index every save
  uses). Bow `0x1bc9` "Wood Bow: Short", club `0x217b` "Wood Club", elixir `0x3752` "Health Potion (Red)", dagger `0x3cbc`
  "Rynn's Dagger": the save resource id `0x3xxxx` is `3 << 16 | classId`. New: `app/src/itemdata.ts` (worker side:
  finds any level header that lists Resources, reads class names, decodes models through the existing
  `loadModelData`), worker API `loadItemCatalog` / `loadItemModels` (`rpc.ts`), `app/src/save/itemart.ts` (UI side:
  catalog, per-class model request, thumbnails through `classThumbnail`, cached per session, reset when a disc is opened),
  wired in `main.ts` and `saveui.ts` (`onItemArt` redraws the Items tab). The Level tab's `classInfo` is the fallback.
  Real disc (Node): catalog 1344 classes in 33 ms; the four items' models decode (142 to 184 triangles, 1 to 3 textures)
  in 13 ms. Browser: synthetic disc + save, names read from the ODB without a level. The real ISO could not be
  uploaded (Playwright only reads under `decomp/`), so thumbnails were not seen in the browser on real models; the
  render path is the Level tab's thumbnail code.
- **Add / remove: investigated, not possible yet; the UI stays disabled.** How the load works (decompile
  `FUN_0016bf48` region, `FUN_0016e7c8`): entry `0x21` starts with the total object count (4478 in the sample); entry
  `0x24` starts with the static (level) object count (3704); the difference (774) is the number of dynamic objects, and
  the loader reads `0x102` ids `1..774` into freshly created objects (`id - 1` is the slot). The rest of `0x21` is the
  named-object table (pairs) and four object-index lists. Items 301/361/419/618 are dynamic objects.
  Game runs on a private card (`build/savere/`, never committed), RAM compared with the unedited save:
  1. Clone the elixir record as id 775 with a fresh self handle (both copies), raise the `0x21` count to 4479, put the
     handle in free cell 3: cell 3 is empty after load (also true of the earlier attempt without the count).
  2. Move the elixir handle from cell 2 to cell 5 and clear the club's cell 1 (record left in place): RAM cells are
     exactly the original layout (0, 1, 2, 16 and the markers 8, 9). So the controller cells are **not** restored from
     the saved handles; the game rebuilds the cell layout from the item objects (flags and counts are read back by
     cell index, which is why those edits work). The cell position of an item is not a plain field of its record (no
     offset correlates with the cell across 85 saves for the same class).
  Open question: where an item's cell is decided (the inventory-add routine run when the item object is created). Until
  that is found, add and remove stay off; a cloned record without a cell cannot be proven.

## Closing the open items (fifth pass)

Screenshots and logs: `build/scratch/save2/` (git-ignored; private card copies under `card/`, `card-orig/`, game runs under
`runs/`). The user's `%APPDATA%/dev.drakan.editor/settings.json` was backed up first and restored afterwards (the
app also writes `.window-state.json` on exit; removed again). The debug desktop exe was started by hand with
`WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--remote-debugging-port=9333` and stopped by PID.

### Save in place, `.bak`, tray and Revert (desktop app, WebView2 + CDP)

- The folder picker is a native dialog, so `build/scratch/save2/pickfolder.ps1` drives it: bring the dialog window (found
  by pid) to the foreground, Ctrl+L, type the parent folder + Enter, pick the `mc0` list item through UI Automation,
  click "Select Folder" only when the dialog is the window under the cursor. (Mouse-first attempts landed on another
  agent's Chrome window and typed into it; do not click on screen coordinates without checking the window under them.)
- Slot 1 of a private card: edited Gold 666 -> 4321 and Health 24 -> 77 through the Character inputs. The tray lists both
  with before/after, the dock toggle shows the count badge, per-edit revert removes one row (count 1), Revert all
  returns Gold to 666. SAVE (the tray button) wrote the file and the footer said "the previous file is kept as ... .bak":
  `.bak` is byte-identical to the original (sha256 matched the untouched copy), `savetool get stat.gold` = 4321 / `stat.hp`
  = 77 in the new file and 666 in the `.bak`. A second save wrote `.bak.1`. (`05-open`, `06-pending`, `09-saved`, `10-row-revert`.)
- **Edited save loads in game.** Slot 3 edited in the app (Gold 112 -> 4321, Health 100 -> 55, Potion of Life quantity 100 -> 37),
  saved in place, copied to `BESCES-50006Slot33` of a private card and loaded with F8 at the title through
  `play_run.py --save` (`--env DRAKAN_AUTOPRESS_UNTIL=1 --env DRAKAN_INPUT_AUTOSTART=0`, recipe `clock milliseconds`, `18000 key tap F8`,
  `0 sync play 60`, dumps): RAM stats block read HP 55, max 100, gold 4321, and the Tab screen shows 4321 gold and the potion stack
  of 37 (`runs/load33/tab.png`). Note: with saves on the card the title's autopress loops Load/Back, so use F8.
- Chrome File System Access: not driven. `showDirectoryPicker` is a native dialog in the user's own Chrome and the same
  pick-and-write code path (`saveWithBackup`) is the one the desktop app exercised through `platform.writeSave`; the
  browser branch (folder handle, `getFileHandle(create)`, `createWritable`) stays covered by the `nextBackupName` unit test only.

### UI

- Right dock collapses (toggle button in the header actions, state remembered in localStorage `drakan.save.dockHidden`;
  a count badge on the toggle shows pending edits while the tray is hidden). `shell.ts`, `saveui.ts`, `save.css`, icon `dock`.
  Verified in the desktop app (`11-dock-hidden`, `12-items-dock-hidden`).
- Item descriptions: the game has none (Resources.XDB has names only, string groups "Old Text", "New Shield Names", "New Weapon
  Names", "New Quiver Names"; MAGIC.XDB/SYSTEM.XDB checked: no item description strings). The tooltip is built from the class
  fields instead (`itemFacts` in `app/src/itemdata.ts`: damage, speed, restores health, durability or unbreakable, needed
  skill level, inventory cells, cannot be dropped) plus class name and shop price. Verified in the app: Rynn's Dagger "Damage: up to 4 /
  Speed: 1 / Durability: unbreakable / Needs skill level: 1 / Inventory cells: 1 / Cannot be dropped"; Potion of Life "Restores health: 100".
  Test `app/test/itemfacts.test.ts`.
- Quest pictures: the game has no quest art either (the journal is text). The data does have each quest's map position, so each
  quest row gets a small map pin showing where its marker sits within the chapter's markers (`questPin` in `save/journal.ts`).
- Journal "new" (seen) bits: **not shipped**. The field `quest.N` (savetool and `SetFieldCommand`) already edits the word and
  the file round-trips, but the modern Tab screen draws no visible "new" marker and the quest rows could not be located in a RAM
  dump (searched for the class ids), so neither load nor effect was proven. Left read-only in the UI.

### Add / remove items: still not possible, narrowed down (decompile of `FUN_0016adb8` and callees)

- Load sequence: `0x21` word 0 is the total object count N; `0x24` lists the static (level) objects (3793 in the sample);
  the dynamic count is N - static (762 = the number of `0x102` records). `FUN_00182c80` pre-creates the dynamic slots, then
  `FUN_0016e7c8` calls `FUN_0016c590(world, stream, object, i)` for each: open `0x102` id i+1, read the saved resource id, resolve it to a
  class (an unknown class deletes the object), build the object (`FUN_00183148`) and read its instance (`FUN_00179170`, a plain
  stream read: flags, 0x3c byte transform, handles). Then the four index lists of `0x21` (after the 4413 named-object pairs; here
  13 / 24 / 14 / 70 dynamic slot indices, the "working sets" at world `+0x44c/+0x458/+0x464/+0x470`) are resolved through
  `FUN_0016bf48` (an index with no object is dropped). The player's carried items (ids 301, 361, 419, 618 in the sample) are in none of
  the lists, so the lists are not what puts an item in the inventory.
- **A cloned item record does load as a world object.** Clone of the Potion of Life as id 763 with N + 1, fresh self handle in both
  copies, transform set beside Rynn: in the RAM dump the clone's handle exists (registry handle with back pointer, transform
  at the chosen position found twice), but it is not in the inventory, not drawn near Rynn and `E` at 60 units does nothing
  (`runs/e1`, `runs/e2`). So the old failure was not "the loader drops it"; the object is alive but is a copy of a carried item
  (its carried/owner state is copied too), so it is neither in a cell nor lying pickable on the ground.
- Cell layout: positions are not a field of the item record (a byte-wise scan over 2199 item records of 490 saves found no offset that
  tracks the cell) and the controller image's handles do not decide it (earlier moved-handle test). The controller methods
  are around `0x33c0e0..0x345fc8` (`FUN_0033eba0`, `FUN_00344de0` repacks all 24 cells largest item first from a stack copy,
  `FUN_0033fa20` builds the quick list at `ctl+0x214`; `FUN_0031e460` is the equip message handler with the "cell holds a
  handle" tests). What is still unknown: which routine puts a picked-up item into a free cell (the pickup message path) and which item
  state words mean "carried" versus "on the ground".
- Next step: make the clone a ground item (clear the carried state in its instance data, read from a ground item such as the Axe
  in the level) and let the game's own pickup add it; or locate the pickup routine with a write watch on the cell handle word
  (`PS2X_WRITE_WATCH` build, needs a runtime rebuild, not done because the runtime must not change here). Add and Remove stay
  disabled in the UI and absent from `savetool`.
