# Native slot screens: a refused save is marked and refused on the screen

Follows [save-pack-builds.md](save-pack-builds.md), "Still open": with `save_menu = native` a save made with another
build of a mod pack was listed as loadable, and choosing it went through the catch-all at `0x23f170` ("could not be
loaded", then the title), so a player in a level lost the unsaved progress. Reference:
[modding.md "Saves and pack builds"](../../reference/modding.md#saves-and-pack-builds). Runs are private under
`build/harness/native-save/` (`run.py`, `runall.sh`, `recipes/`, `card/`).

## How the native list is filled and confirmed (Ghidra, `analysis/input_run.sh InputDecompile.java`)

* The scan `43b6f0` lists `*.rsg` of the four slot folders into the entry array (path `+0x000`, caption `+0x100`
  from the level name, `ctx+0x368 vt+0x2c`); there is no damaged state. The grid draw `43bfb0` asks for a thumbnail
  once per entry (`+0x154 == 0 && +0x164 == 0`: card manager `vt+0x1a4`), draws the caption white (save) or lime
  (empty), the date, and for a slot whose caption is the "no room" text (property `0xfb`) the explanation `0xfc` in
  the picture area: grey `0xd0` over a black shadow (alpha `0xc0`) two units down and right, wrapped at `0x9a`, at most
  five lines, centred in the `0x71` high picture. Colours are `129370(r, g, b, a)` = PS2 RGBA halved
  (`0xd0d0d0ff` -> `0x80686868`).
* Load confirm: message 2 -> `43d028(screen, widget)`: in a level the yes / no box `428568(-1.0f in f12, screen,
  0x107, 0x238d)`, `+0x9c |= 2`, box `+0xbc = 2`, box `+0xb4 = screen + 0xc8`, screen `+0xa0 = 5`; on the title
  `43b4d0` at once. `43b4d0` runs `23f170` and on failure shows property `0x105` as an on-screen message and opens the
  title.
* The message box (screen `0x1b`, handler `4280d0`): `+0xbc` is a card condition checked every message 10. With the
  factory's default 0 it closes as soon as a card is present (the "no memory card" box); 2 closes it only when the
  card goes, calling the node at `+0xb4` (`43cfa0`: rescan, grid shown). Its buttons (message 1) send message `0x10`
  to the parent and close it; the slot screen's `0x10` acts on `+0xa0` 2 (close) or 5 + Yes (load) only, and clears
  `+0x9c` bit 2. Default widget `0x238c` gives a box with OK alone (`427c40` message 3).

## What changed

* **Mark** (`drakan_menu_screens.inl` `nativeGridDraw`, the native branch of the grid hook): on a Load screen, rows
  whose save `drakan_save_guard::checkEntry` refuses (`gameui::nativeRefusedSlots`) get `+0x154 = 1` before the first
  draw (no thumbnail) and, after the grid, the `0xfc` look with `Unavailable` and a short reason
  (`gameui::savePackSlotNote`, wrapped by `nativeSlotNoteLines` at `0x80` so it stays inside the picture; the native
  `0x9a` runs over the frame), drawn with the grid's own `286170`, Latin-1 (`gameui::utf8ToLatin1`). Once every entry
  asked for its picture the grid does no card work, so it runs as a no-preempt section and the notes follow it every
  frame (the first run had one in-game frame where a time slice ended inside the grid and the notes were missing).
* **Refuse** (`drakan_save_guard.cpp` `hookLoadAsk`, hook of `43d028`): a refused selection never reaches the question
  or `43b4d0`. As `43d028` does for its question, `+0x9c |= 2`, `+0xa0 = 0`, then `428568(-1.0f, screen, 0x105,
  0x238c)` as a tail call on the real context (it allocates and may block) with `$ra` on a free function table slot
  (trampoline) that sets box `+0xbc = 2`, `+0xb4 = screen + 0xc8` and returns 0 to `43d028`'s caller. The reason with
  the pack's name goes to the notice band. `drakan_menus` no longer hides the pager on Enter for such a slot.
* Stock saves, saves made without content packs and same-build saves take the unchanged path; the Save screen is not
  marked (saving over the slot is allowed); the modern screens are unchanged; `23f170` stays the catch-all.
* Texts: four short reasons in five languages (`MenuText.cpp`); `Unavailable` and 0x105 ("Load Failed!" in English)
  already existed. Tests: `gameui_saveload_test` `testNativePackMarks`.

## In game (`build/harness/native-save-dist/drakan.exe`, `DRAKAN_SAVE_MENU=native`, pack `issue5-save/packnew`)

Card: slot 1 a stock Surdana save (`issue5-save` `st-make`, no record), slot 2 a Volcanic save of the current pack
build (`mk-new`, `.rsg.mods` with `a46d89ce1cbcd218`), slot 3 the old sanctum save (`ootf-lru/card-sanctum`, no
record, made before records: `OlderThanPack`).

| Run | Path | Result |
| --- | --- | --- |
| `pause-ref` | Volcanic direct start, Pause > Load Game | slots 1 and 2 with pictures and dates, slot 3 without a picture and `Unavailable / Made with an earlier / version of a mod / pack.` (also after OK); Enter on slot 3: `[saves] native Load screen: '...Slot3/OotF 11 - Volcanic.rsg' refused, the screen stays`, the native box "Load Failed!" with OK, the band names the pack; OK: the list again, Esc Esc: the level as it was, W walks; then slot 2 > "Leave current game?" Yes: `beforeLevelLoad ... fromSave=1`, walks; clean exit |
| `pause-stock` | Volcanic direct start, Pause > Load Game > slot 1 > Yes | the stock Surdana save loads (`fromSave=1`), clean exit |
| `title-ref` | native title > Load Game > slot 3, OK, slot 1 | box on the Load screen over the title, OK back to the list, the stock save loads, clean exit |
| `title-same` | native title > Load Game > slot 2 | the same-build save loads and walks, clean exit |

Screens: `runs/<run>/*.png` (`loadscreen`, `slot3`, `refused_box`, `after_ok`, `walked`, `question_yes`, `loaded_same`,
`loaded_stock`). `run.py` here does not take `GAME_SLOT.lock`.
