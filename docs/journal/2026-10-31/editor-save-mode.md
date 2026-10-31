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
