# Save pages (16 slots), title "Quit", text-bound menu hit areas

## Quick save / quick load (2026-10-25)

`F5 = QUICKSAVE` and `F8 = QUICKLOAD` in `drakan_input.ini [bindings]` provide one-press save/load during
controllable gameplay. F8 also works at the title. Both actions are rebindable in Options → Key Bindings;
`Q = QUICKSAVE` / `L = QUICKLOAD` reproduces Order of the Flame's keys, but replaces Q's Quick Inventory binding.
Older input files gain F5/F8 only when those keys are unused and the actions are not already assigned; explicit
`NONE` bindings are respected.

The quicksave is the native `.rsg` in `mc0/BESCES-50006Slot33`, outside the configurable manual slots 1–32.
It survives restarts and repeated quicksaves replace it without an overwrite question. Quickload discards unsaved
progress without a confirmation; a missing quicksave leaves gameplay alone. It does not fall back to a manual save.
The manual page is restored after the operation. Quickload from a death dialog is not currently supported.

Implementation: `drakan_quick_save.inl` opens the real pause screen, focuses its Save/Load row and temporarily
maps the slot-name cells to the reserved page. After the native scan, it selects row zero and uses native Cross
(empty save / title load), the overwrite timer, or the load-confirmed flag. Writes and loads execute in the GUI's
own draw, never as posted guest calls. The operation blocks competing input, ignores held shortcut keys and
returns to gameplay. The presentation overlay displays one progress card across the entire operation, including
with native menus selected. Native error dialogs remain interactive. Cancellation clears deferred writes/loads before
restoring slot names and rescans any surviving slot screen.

The save-exists hook checks the quicksave page too, including with `save_pages = 1`, so F8 can load a card that has
only a quicksave. The title's Continue action still selects the newest **manual** save; F8 selects the quicksave.
Validation notes: `docs/journal/2026-10-25/quick-save-load.md`.

Known issue: one player report of a freeze at F5 (guest stops inside the slot scan, log ends after `flags 9`) is not
reproduced; the game now logs a `[stall]` dump after 8 s without a guest frame, `DRAKAN_MC_TRACE=1` traces card calls and
`scripts/harness/recipes/quick_save_stress.txt` hammers F5/F8. See `docs/journal/2026-11-01/quick-save-freeze.md`.

## Current save browser (2026-10-18)

The pager is now drawn by the PC presentation overlay on every displayed frame,
using the game font and renderer viewport/HUD transform. The EE input boundary
publishes a synchronized layout and the matching mouse hotspots; it no longer
injects pager text into the native alternating draw buffers. Confirming a populated
load slot hides the pager before loading starts. PgUp/PgDn and L1/R1 still use
the native page-switch handler.

Loaded 128x128 CT16 save screenshots are registered by content hash as photographs.
Automatic scaling uses bicubic interpolation with sharpening instead of xBRZ's
pixel-art treatment, including a one-time cache rebuild if identification follows
the first draw. Original saves retain their original low-resolution source detail.
Loading screen 14 keeps the selected presentation aspect and a centered native
foreground, avoiding the widescreen-to-4:3 transition.

Private validation is in `build/save-browser/`: native option persistence/restart,
page 1 -> page 2 -> page 1 RAM checks, repeated presenter screenshots, photographic
upscale logging, and a complete load into gameplay. Original game-save payloads
are preserved. The earlier implementation notes below describe the original pager.

Branch `drakan-menu` of `tools/PS2Recomp` (worktree `tools/PS2Recomp-menu`, build `build/recomp_menu/`). Code: `ps2xRuntime/src/lib/drakan_menus.{h,cpp}` (hooks), `runtime/input` `MenuMouse` (hotspots, label bounds).
Addresses are SCES_500.06. Screenshots: `build/screenshots/r5_menu_*.png`, test scripts `build/recomp_menu/t/*.ps1` (real `SendInput`, isolated card in `build/recomp_menu/elf/mc0`).

## 1. How the game's save UI works (reverse engineered)

* Load = screen id 4 (class ctor `0x43cf38`, handler `0x43ac40`), Save = screen id 5 (subclass, ctor `0x4442e0`, handler `0x443360`), both layout `0x2330` (widgets: image `0x2331`, "Reading..." label, title label). **No widgets for the slots**:
  the 2 x 2 grid is drawn by `0x43bfb0` (called per frame through the image widget's message 10) from an entry array: screen `+0xb8` = entries (0x168 bytes each, x/y at `+0x15c/+0x160`, caption text at `+0x100`),
  `+0xc0` = count, `+0xa4` = selected index, `+0x50` = last key id, `+0x9c` = u16 flags, `+0x1e4` (save only) = mode (1 = the save offered at a level change; it shows the same slot grid, so the pager and mouse hotspots apply to it too).
* The memory card folder of a slot is `"BESCES-50006" + SlotN`. The four names come from a **pointer table at `0x4fe3b8`** pointing at four 8 byte cells at `0x5325c8` (`Slot1`..`Slot4`). Every user of a slot goes through that table:
  scan `0x43b6f0` (`GetDir BESCES-50006SlotN/*.rsg`, thumbnail + date), save class (write, confirm texts), `0x37e810` (is there any save / free space).
* Slot list refresh: screen handler message 10 rebuilds the list (`0x43b108`: frees entries + thumbnails, runs the scan `0x43b6f0`) when **bit 3 of the u16 at `screen + 0x9c`** is set. The screen sets it itself when it opens (message 3 ORs `0xc`). Without it the list is read **once**;
  afterwards the screen only polls `sceMcGetInfo` (card present) every frame, which is why the memory card HLE log is mostly `GetInfo` spam.
* GUI key ids (table `0x4e98d0`): PgDn = R1 = `0xd1`, Delete = L1 = `0xd3`; `[menu_bindings]` already maps `PGDN = R1`, `PGUP = L1`, so keyboard and pad arrive as the same two ids at the screen key handler `0x43b1e8` (shared by load and save).

* Entry fields (0x168 bytes), the native confirm / load / save paths and how a host screen drives them:
  `docs/journal/2026-10-25/modern-save-load-title.md` section 1 (`+0x100` caption, `+0x128` sort key, `+0x130` time
  stamp, `+0x164` thumbnail); `gameui::readSaveSlots` reads them. The slots are not widgets: there is no list widget.

## 2. Save pages

* Page p (0 based) rewrites the four cells to `Slot(4p+1)`..`Slot(4p+4)`; page 1 is the original set, so **old saves stay valid** and no memory card HLE change is needed (`sceMc*` is untouched).
  Folders `BESCES-50006Slot1`..`Slot16` appear on demand. Delete / overwrite confirmations and the save write use the same table, so they operate on the paged slot.
* Page change = write the cells + set bit 3 of `screen + 0x9c` (the screen rescans on the next frame). Verified in the memory card log: `GetDir 'BESCES-50006Slot5/*.rsg'` .. `Slot8` right after the key press, back on page 1 it reads `Slot1` again.
* Slot captions read "5 - empty" on page 2: `sprintf` (`0x4ce688`) is wrapped and adds `4 * page` to the number when the format is the caption `"%d - %s"` (`0x54e3a0`, save confirm `0x54f040`).
* `0x37e810(0)` ("any save on the card", greys out Load Game in the title and pause menus) is wrapped to ask every page (the visible page first), so a save that only exists on page 3 keeps Load Game enabled after a restart.
  Verified: with `Slot1` removed and a save in `Slot5`, the title shows Load Game enabled.
* Controls: **PgDn / R1 = next, PgUp / L1 = previous** (wraps), mouse: the two buttons and the four slots. The pager (`< Back`, `Page 2/4`, `Next >`) is drawn by the game's own text routine (`0x286170`, same font, shadow and lime highlight as the slot captions)
  from a wrapper of the grid draw. Native layout widgets were rejected: button text comes from a property -> XDB id chain with no free text property (the only unused ones belong to the memory card warning screen), and a focusable button would steal the
  slot keys (focus != 0 sends Up / Down / Enter to the widget chain instead of the screen key handler). Localised labels live in `Labels` (en / de / fr / it / es, language from `drakan.ini [general] language`; Back / Next / Page).
* Mouse (`MenuMouse` hotspots): the grid draw publishes the four slot rectangles and the two buttons every frame (`setHotspots`); hovering a slot selects it (tick sound), a click selects + taps Cross (load / save as with Enter), a click on a button changes the page. Hotspots replace
  the widget hit test on these screens (they have no focusable widgets) and expire 4 updates after the last draw.
* Config (`drakan.ini [general]`): `save_pages = 4` (1 = original behaviour, up to 8; env `DRAKAN_SAVE_PAGES`).
* Not changed: the Shift-JIS memory card icon title (`icon.sys`, XDB 102..105 "Gates 1..4") still names slots 1..4 on every page (only visible in a real PS2 browser).

Level-change save (mode 1, 2026-10-24): it was excluded from the pager and the mouse hotspots, so only the keyboard worked there. It shows the same grid, so it is a slot screen now.
Harness: `python scripts/harness/play_run.py --name levelsave --level-save --recipe scripts/harness/recipes/level_save_mouse.txt`
(`DRAKAN_TEST_LEVEL_SAVE` opens the pause menu's save in mode 1). Verified: Next / Back clicks change the page (2/4, 1/4), hover selects, and a slot click closes the screen and prepares `Slot1`.
The real level-exit trigger is `0x361f60` (skipped when bit 4 of the exit's u16 flags is set; the intro -> Surdana transition never calls it).

Verification (real keys / mouse, `build/recomp_menu/t/r3,r7,r9,r10.ps1`): Load Game -> PgDn shows "5..8 - empty", PgUp back to 1..4; mouse Next / Next / Back / Back cycles 2, 3, 2, 1; New Game -> pause -> Save Game -> PgDn -> Enter wrote
`mc0/BESCES-50006Slot5/{icon.sys, FolderIcon.ico, Level 1A - Surdana.rsg, BESCES-50006Slot5}`; after a restart Load Game -> PgDn lists "5 - Surdana 16:23" with the thumbnail and loading it starts the game (screen 4 -> 14 -> gameplay).

## 3. Title menu "Quit"

* The title layout `0x2300` already contains widget `0x2303` ("Quit", XDB 121, localised in all five languages: Quit / Verlassen / Quitter / Abbandona / Salir). The stock handler `0x43e4c8` positions it as the fifth row (table `0x54e768`: Load, New, Options, Credits, Quit)
  and then **hides and disables it** (`widget vslot +0x64(0)`, `+0x6c(0)`). The wrapper undoes that, moves it to the end of the child list (it is the second widget of the layout, so the focus chain would visit it after Load Game: Down from Credits -> Quit, Down from Quit wraps to Load Game)
  and shifts the five rows up by half a step so they stay centred. This post-processing runs on the first message after message 3 (message 3 itself can yield while it waits for the font, so it cannot be post-processed on return).
* Activation (message 1 for `0x2303`): focus is cleared and the game's yes / no box is opened (default No). The stock helper `0x427680` looks its text up in an XDB group that is not resident on the title screen and silently creates nothing, so the box request
  `{text, 0x238d, -1, -1.0f}` is built in an unused page of low RAM (`0xff000`, below the ELF) and the screen factory `0x2c4458(manager 0x4fb600, 1, parent, request, 1)` is **tail-called on the real context** (a nested call on a copied context deadlocks: the factory blocks on the
  allocator semaphore and the thread-switch unwind throws). The box text is the game's own "Are you sure you want to quit?" in the five languages. Yes (`0x238b`, message 0x10 to the title) -> `drakan_win::requestClose()` (same clean shutdown as F1 > Quit to Desktop and the pause menu quit);
  No returns focus to the Quit row. Verified with keys (Down x4, Enter, No, Enter, Up, Enter: process exits) and the mouse.
* Config: `title_quit = 1` (env `DRAKAN_TITLE_QUIT`).

## 4. Mouse hit area = visible text

Menu rows are full-width widgets (x 0..512), so a click left or right of the word used to activate the row. `MenuMouse::hitTest` now narrows **buttons with centred text** (class 3, flag `0x1`, text pointer at widget `+0x5c`, width from the game's own `0x2885c8(str, 1, 0, len + 1)`)
to the text width + the 0x24 px icon + a 12 unit margin around the row centre, for hover and click. Sliders, check boxes and selectors keep their whole rectangle. Verified on the title menu (hover / click left of "Options" does nothing, on the word highlights / activates)
and in Options.

## 5. Merge notes

New: `ps2xRuntime/src/lib/drakan_menus.{h,cpp}`; edits: `ps2xRuntime/CMakeLists.txt` (one source line), `drakan_overrides.cpp` (include + `drakan_menus::install(runtime)` before the HLE RPC hooks), `drakan_config.cpp` (two ini keys).
Shared tree `runtime/input`: `MenuMouse.{h,cpp}` (hotspots, `playSound`, `insideLabel`), `DrakanInput.h` (`menuMouseRw()`), `tests/input_test.cpp` (+9 checks, 641 total). No changes to `runtime/hle`, the memory card stubs or the generated code.

## 6. Highlight clipping of the last letter (round 6)

Cause (`GSR_DRAWLOG`, `GSR_HLDUMP`): the fire highlight of a menu row is two 128x128 additive sprites (T8 heat map + CT16 glyph overlay, ALPHA 0x48, clamp) sized to the measured text width, e.g. Load Game x 208..303 with S 0.0195..0.7617
(texels 2.5 .. 97.5), but the heat map carries content up to column 98, so the right edge of the last glyph ("e" of "New Game") was cut. It is identical with `smooth_menu_highlight = 0`, `texture_upscale` off and `hud_aspect_correct = 0` (not the blur, the upscaler, the HUD
transform or the row re-centring) and is the game's own sprite size. Fix in `runtime/gs/src/d3d11_backend.cpp` (after the vertex copy): for draws matching that sprite state the right edge is extended by 2 texels (x and S together). Crops: `build/screenshots/r6_highlight_base.png` (before) / `r6_highlight_after.png`.

## 7. Modern Save / Load screens (2026-10-25)

With `[general] save_menu = modern` the slot screens 4 / 5 are driven by the host-drawn `gameui::SaveLoadScreen`
(`docs/reference/mouse-menus.md` section 15). The pager stays the page switch (`drakan_menus::showPage`), but its
text, hotspots and thumbnail registration are off while the host owns the screen (`drakan_menus::setSlotsHosted`).
Slot pictures: the HD companion (`drakan_save_previews::previewPath`, the same `<save>.rsg.<fnv64>.preview.png` rule)
or the native 128 x 128 thumbnail read from the entry's image (`+0x164`, rows top down, shown as a 4:3 picture as the
native grid does). The grid draw `43bfb0` only makes its thumbnail requests then, one per call as a tail call on the
real context. There is no play time in the save data (no entry field, nothing known in the `.rsg`): the cards show
the location and the card's time stamp only. The card's stamp (the list's date and time) can differ from the file's
modification time on the host by the time zone (the title's "newest save" orders by the latter).

## Editor notes (2026-11-01)

- Item record state: u16 at +0x56 is 2 for carried items and 0 for ground items; the self handle sits at +0x86 and again at +0x166.
  Cloning a ground item record (new id, fresh handle, count raised) did not produce a visible or pickable item (see
  journal/2026-11-01/save-editor-round3.md); add-item stays unsupported.
- Backups written by the editor: `NAME.bak` first, then `.bak.1`, `.bak.2`; existing backups are never overwritten.
- Rynn's record carries the carried-item list: 16-byte entries `{slot = entry id - 1, 1, -1, -1}` starting near +0x14de (Surdana sample), in
  inventory order; the game rebuilds the cells from it. A carried item record can be replaced in place by another class's record (keep the
  serial, handle copies, +0x52..0x5e, +0x96 and position).
- Add item works (2026-11-01, `journal/2026-11-01/save-add-item.md`). The "list" above is a word stream: Rynn's record holds the 24
  cells column-major (position = column * 3 + row); an empty cell is one word (-1), an item two (slot, flag 1), so one more item makes
  the record 4 bytes longer (the old `{slot,1,-1,-1}` view and in-place writes caused the `[memset] bad size` crash). Slot space: records
  are slots 0..K-1, level statics follow at K + their position in the 0x24 table (0x21 pairs mirror it), so a new record needs the
  first static dropped from 0x24 and 0x21. The u16 item count 8 bytes before the cell table goes up by one. Code:
  `editor-core/src/save-items.ts` (`addItem`, `findCellStream`), UI `app/src/save/inventory.ts`.
- Rynn's class word (record +8) is `0x17081069` in the sample saves and `0x1f081069` in a save the game wrote after play
  (`journal/2026-11-01/save-add-item.md`). The two differ only in bit 27 (`0x08000000`). Nothing in the repo decodes the word, so
  the bit's meaning is not known. Other record words also differ only in their top byte: Arokh `0x1b001041` and a camera
  `0x18001041`, and the gold counter `0x19001255` has bit 27 set. This suggests the top byte holds state flags and is not part of
  the class. The editor (`isRynnClass` / `classMatches` in `formats/src/save.ts`) matches Rynn with bit 27 ignored and every other
  bit exact; other classes stay exact. `scripts/savetool/savetool.py` still matches exactly.
- Item size (2026-11-02): the height of an item in cells is the Resources class field `Icon Slot Size (1-3)`. A tall item spans down
  one column from its first cell (bow 3, Rusty Scimitar 2), and the cells under it hold continuation markers `dx * 10 + dy` in the
  cell table. In the word stream the covered cells stay single empty words: the game saved the tall scimitar with Rynn's record at
  the same size. Add item (`placeItem`, `addItem(save, template, rows)`) puts the item in the first cell where all of its cells
  are free and inside the 8 x 3 grid, and writes the markers. The UI takes the height from the disc's catalog, else from a carried
  item of the same class. When neither is known it reserves a full column of 3 cells. It shows a message when nothing fits. The bow
  stays excluded because it was never tried in game. Not proven in game: a 2- or 3-cell item placed by the editor.
