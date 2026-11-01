# Save editor: Add item works (new carried records)

Solves the open item of `save-editor-round3.md`. Proven in game on a copy of the Surdana sample: a Health Elixir and a
Rusty Scimitar added by the editor's code, loaded with F8, shown in the Tab inventory, saved again by the game (F5) and
loaded again (`build/harness/sr_add2`, `sr_add3`, `sr_add4`; builder scripts in `build/savere3/`).

## Root cause of the `[memset] bad size 0xe570a3d0` crash

It was never a missing registration of the item. Two format facts were wrong in every earlier attempt:

1. **The carried-item list is a word stream, not 16-byte entries.** The loader (the player deserialiser `FUN_00380f70`, loop
   at 0x3830b0..0x38314c in the PAL ELF) reads the 24 cells column-major (8 columns x 3 rows, stream position =
   column * 3 + row, cell index = column + 8 * row). For each position it reads one word (the slot = entry id - 1),
   resolves it through the handle table at ctx + 0x58 (`FUN_0016d4f0`, a plain `array[slot]`), and only when the object
   exists it reads a second word (the flag, 1) and calls `FUN_0033cc10(controller, col, row, handle, flag)`. An empty cell is
   ONE word (-1), an item is TWO. The `{slot, 1, -1, -1}` look of the sample is just two empty cells after the item. Writing
   (slot, flag) over an empty cell's word and the next one (what every earlier test did) leaves the record one word short, the
   rest of the player record is read out of step, and the first count read afterwards is garbage: `FUN_0049d3f8` (a counted
   array read) allocates 0xe570a3d0 bytes, `memset` gets a null pointer and the next call jumps to 0. That explains why a
   duplicate reference to the already working potion (slot 327, test `sr_t1`) crashed as well, and why swapping two entries
   or replacing a class in place never did. A real save with one more item has a Rynn record that is exactly 4 bytes longer.
2. **Slot space.** Slots 0..K-1 are the saved 0x102 records (K = their count, entry id = slot + 1); the level's untouched
   ("static") objects follow, each at slot K + its position in the 0x24 table (word 0 of 0x21 = K + 0x24 count; the 0x21
   pairs are `(uid, slot)` sorted by uid and are redundant with the 0x24 order). Appending a record without touching the
   statics shifts every static by one: the game loads, then crashes later (`guest-branch ... target=0x0` at 0x432a78, test
   `sr_u3`). Fix: drop the FIRST static (0x24 word 1, its 0x21 pair) so K + position stays the same for all other statics. In a
   real save a changed level object moves from 0x24 to a saved record exactly like this (new game: 681 records / 3880
   statics, 12 records later 693 / 3869).

Also changed: the u16 item count 8 bytes before the cell table (+1), and the cell table image gets the new handle (the game
rebuilds it; the editor keeps it in step so a second add finds it).

## Add item (`editor-core/src/save-items.ts`, `addItem`)

Body = `replacedBody(donor carried item, template record)` with a fresh heap-looking handle (+0x86 and +0x166, from 0x00a60010
upward, unused by any record) and serial (largest small serial + 1); appended after the last record in index and file order;
ids stay contiguous. `findCellStream` locates the stream in Rynn's record from the items the cell table lists (no fixed
offset). Cell: the first free cell index. Class choice: any class the save has a record for (`itemTemplates`), except the bow
(three cells; the stream layout of multi-cell items is not known).

UI: Inventory tab, "+ Add item..." dropdown (type to filter), dashed "New: ..." tiles in the cells the items will take (click
to remove), tray entries `add.N` ("new item 1 (cell 2)", before/after text, revert), `AddItemCommand` /
`RemoveAddedItemCommand` (undoable), applied after replacements in `SaveDocument.build()`. Checked in the browser (dev server,
`save.html?nofsa`, `build/savere3/ui_add_pending.png`). Tests: `editor-core/test/save-items.test.ts` (record, tables, stream
insertion, second add, document round trip); `npm test` 584 passed.

## Observed in game

- Health Elixir (class 0x33752) in cell 2, 100/100 charges, inspector shows "Consumable" with the game's own "Use" prompt
  (`sr_add2/e_equipped.png`); Rusty Scimitar (0x3372d, damage 7, durability 150/150) in cell 3 (`sr_add3/f_equip.png`).
- In-game F5 then F8 keeps both (`sr_add4/d_cell3.png`). The game rewrote Rynn's record at the same size as the edited one and
  the two items are state-2 carried records in its save.
- The scimitar is a tall item: after the game's own reload it spans cell 3 and the cell below it. Add item puts every item in
  one free cell; a 1x2 or larger class overlaps the cell under it when that cell is occupied. Items are not size-checked yet.
- Not proven: actually using the elixir or equipping the scimitar. The scripted Enter after selecting the cell did nothing
  visible (the prompts "Use" / "Equip Left Hand" show that the game recognises the items), so equipping and drinking were not
  verified.
- The in-game saved file has Rynn's class word `0x1f081069` (a bit set versus the sample's `0x17081069`), which the editor's
  and `savetool`'s exact Rynn match does not recognise ("expected one Rynn record, found 0"): opening a save the game wrote
  after play in the editor needs the class check relaxed (not done here).

Side effect: one level object (the first of the static table, uid 5020 in the Surdana sample) is no longer created. Not seen to
matter; the note box in the tab says so.

## Method (reproducible)

- Real before/after pair: the arena loadout (`DRAKAN_ARENA_LOADOUT=0x3372b`, factory + stow) saved with the new test aid
  `DRAKAN_ARENA_ALLOW_SAVE=1` (lifts the arena checks of quick save in `drakan_quick_save.inl` and the write veto in
  `drakan_lifecycle.cpp`) gives a save with one more item (a 606-byte club record plus a 124-byte child record; Rynn +4 bytes,
  count 2 -> 3, statics -2). Those arena saves do not load in the campaign, so they served only as format evidence.
- `DRAKAN_STACK_ON_BAD_MEMSET=1` (`Kernel/Stubs/LibC.cpp`) prints the code-looking words of the guest stack at a bad `memset`
  size; with Ghidra (`analysis/input_run.sh InputDecompile.java`) that located the player loader.
- Harness notes: F8 only works on the title (screen 8); `play_run.py --env DRAKAN_AUTOPRESS_UNTIL=13000` stops the autopress
  from walking into the Load screen first. To get two saves of one run, copy the file whenever its mtime changes.
