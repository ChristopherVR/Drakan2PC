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
