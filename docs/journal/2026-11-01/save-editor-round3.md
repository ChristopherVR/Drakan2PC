# Save editor round 3: backups, browser save, journal seen bits, add item (not solved)

## Shipped (editor, tests green: `npm test` 451, `npm run build`)

- **Backup rotation is visible.** The tray has a Backups card (`#sv-backups`, `saveui.ts`): the rule (copy to `NAME.bak`, later
  `.bak.1`, `.bak.2`, never overwritten, highest number is newest), the backups found beside the file as chips (oldest to newest;
  folder handle only), "the next save writes X" and "last save: previous file is now X". The footer message after a save repeats it.
  Desktop cannot list the folder, so it shows the names written this session.
- **Chrome File System Access.** `saveio.ts`: a single picked file has a handle but no folder, which used to force a Save As
  dialog for the `.bak`. `attachFolder` now asks once for the containing folder (`showDirectoryPicker`, `startIn` the file, checks the
  file is in it), then `saveWithBackup` writes the backup with `getFileHandle(create)` and overwrites the file. Browsers without
  the API: Save downloads the edited file (the button is no longer disabled; the note says so). `?nofsa` forces that path.
  Proven: unit test with an in-memory folder (`app/test/saveio.test.ts`: `.bak`, `.bak.1`, contents). Not proven: the real native
  picker in Chrome (a native dialog the tools cannot drive).
- **Journal seen bits.** Story list rows get "Mark seen" / "Mark new" (`save/journal.ts`): sets or clears the journal bit (2) or
  map bit (4) of `quest.N` through `SetFieldCommand` (undoable, shows in the tray). Raw table stays read only.
  In game: a save with row 0 seen 4 -> 0 and row 1 0 -> 6 loads, the journal page draws the quest normally (`build/harness/sr_q1`).
  Not proven: the game shows no visible "new" marker, and the quest table was not found in a RAM dump (class ids 0x10c7 / 0x1047
  with 0x20 stride not present at 0x4fbb70 nor anywhere), so the bit's effect after viewing was not observed.

## Add item: still not working

Tried ground-item clones: cloned the world dagger record (entry 262, resource 0x33cbc, state word differs from carried items:
u16 at +0x56 is 0 on the ground and 2 when carried, +0x60 word differs too) four times beside Rynn at several heights, new ids, fresh
self handles at +0x86/+0x166, `0x21` count raised. The game loads the save but nothing is drawn or pickable and the inventory is
unchanged (`build/harness/sr_g1`, `sr_g2`, scripts in `build/savere2/`). Records 243/244 (potions) share one position, so they are
not loose ground items (probably container contents). Add item stays disabled; next step is a write watch on the inventory cell
handle to find the pickup routine, or inspecting how the loader links a ground item into the world.

## Add item, second pass: what the load actually reads (build/savere2/, runs build/harness/sr_c*)

Slow machine note: with many game instances running, a run takes 3 to 5 minutes; use `--attempts 1 --timeout 300` and a later F8.

- **Rynn's record holds an item list.** At record offset 0x14de (this Surdana sample) is an array of 16-byte entries
  `{u32 slot, u32 1, -1, -1}`, one per carried item, `slot` = the item's entry id minus 1, then -1 entries. The game restores the inventory
  from this list in order: swapping the two entries (327 and 614) put the potion in cell 0 and the dagger in cell 1 (`sr_c6/c_tab.png`;
  the equipped flag stays on cell 0). The 24-cell table in the same record is NOT what decides (handles written into free cells with
  valid carried records, `sr_c3`, change nothing).
- **In-place class swap works.** Rewriting the potion record (id 328) with the body of the dagger record (id 262) but keeping the carried
  fields (serial +0x48, +0x52..0x5e, handle +0x86 and +0x166, +0x96, position) makes cell 1 show a second dagger (`sr_c2/c_tab.png`).
  So "change item" in an existing carried slot is possible; sizes differ per class and the record is simply replaced.
- **A new carried record is still not accepted.** Cloning a carried record (new id, unique serial, fresh handle, count + 1) and adding
  `{762, 1, -1, -1}` to the list crashes the game after load (`[memset] bad size 0xe570a3d0 ... transfer dropped`, then a guest branch to 0,
  `sr_c4`). Adding an entry for an existing carried-state record that is not in the list (ids 300 and 462, `sr_c5`, `sr_c8`) crashes the
  same way, so it is not the clone's bytes. Those records have no other reference: a carried dagger (slot 614) sits in the 0x21 working-set
  lists and, like the potion (slot 327, a named-object pair value), in Rynn's record at other offsets (0x34e, 0x154a, 0x1a9a, 0x1b36 for 614).
  Adding a named-object pair (and raising the pair count) as well (`sr_c7`) crashes the same way, so the missing registration is not the
  pair table. (An earlier note here called this a hang; that run never loaded because the recipe's F8 came too early on the busy machine.)
- Open: the missing registration for a new item (candidates: the 0x21 working-set lists for slot 762, the extra Rynn offsets that mention
  slot 614, the pair table with a correct key). Next step is a write watch (`PS2X_WRITE_WATCH`, needs a full game rebuild) on the list entry
  to see which fields the pickup routine `FUN_0033d070` and its callers write, or diffing a real before/after-pickup pair of saves.
- Decompile notes (build/scratch/save2/dec): `FUN_0033d070(ctl, item)` is the pickup routine (called from the equip and pickup message
  handlers `FUN_0031e460`, `FUN_0031f700`, `FUN_00320f70` and the shop `FUN_0040c310`); the controller sits at player data + 0x584, cells
  at ctl + 0x30 (+ 0x10 each, handle at +8), the item state setter is a vtable call with 2 at item + 0x144.

## Backups card in a real Chromium (build/harness/save-bak/)

The File System Access path was driven with Chromium's real handles (OPFS directory standing in for the memory card folder; only the two
pickers were stubbed): Open save (single file) -> edit Gold -> Save asked for the folder once, wrote `.bak`; a second save wrote `.bak.1`;
the Backups card showed `.bak`, `.bak.1`, "next save writes ... .bak.2" and the footer message (`04-after-save2.png`, `05-final.png`).

## Shipped: "Replace with..." on a carried item (Inventory tab)

- `editor-core/src/save-items.ts`: `itemTemplates(save)` (one record per item class found in the save: pickups, shop stock, chest contents;
  a record lying in the world is preferred to a carried one), `replacedBody` (template body with the kept ranges of the carried record:
  position +0x0c, serial +0x48, +0x52..0x5e, handle +0x86 and its copy +0x166, +0x96), `replaceItem` (pure, recompresses only that record).
- `SaveDocument.itemReplacements` (cell -> template entry id), `ReplaceItemCommand` (undoable), a pending edit `item.N` with before/after text
  in the tray, applied first in `build()` so later field edits still apply. Counts of the replaced item are the template's own; the
  durability inputs are disabled while a replacement is pending.
- UI (`app/src/save/inventory.ts`): the inspector has a "Replace with..." dropdown (the app's dropdown, type to filter) and "Undo replacement";
  tiles show the new class; the Add item button stays disabled with the tooltip "use Replace with... on an existing item for now".
  Only classes that the save has a record for are offered; same-size items are advised (a multi-cell class over a 1-cell slot was not tested).
- Tests: `editor-core/test/save-items.test.ts` (synthetic records: templates, kept fields, undo, class swap and back).

### Proof of Replace with... (build/harness/save-bak/)

Browser: Save tab, Inventory, cell 1 (potion), Replace with... "Wood Club" (typed "club" in the dropdown filter), pending edit `cell 1 item`
in the tray (`12-replaced-pending.png`, `11-dropdown-filter.png`); the file exported by the editor (`replaced-club.rsg`) was put on a private memory
card and loaded in the game with F8: cell 1 shows the spiked Wood Club with its durability bar, the dagger stays equipped in cell 0
(`20-replace-club-ingame.png`, run `build/harness/sr_c9`). On a busy machine press F8 late (`45000 key tap F8`, `build/savere2/rec3.txt`).
Not tried: replacing by a multi-cell class (bow) or equipping the new item.
