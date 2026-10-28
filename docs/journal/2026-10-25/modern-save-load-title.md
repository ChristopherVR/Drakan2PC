# Modern Save / Load screen and title menu: research and library — 2026-10-25

Goal: host-drawn Save Game / Load Game and title (main) menu screens in the modern screens' look, selected by
`[general] save_menu = native | modern` and `title_menu = native | modern` (default `native`, not wired yet). Static
research only (Ghidra decompile `build/recomp_ui/alldecomp`, the glue in `ps2xRuntime/src/lib/drakan_menus.cpp`,
`drakan_save_previews.cpp`, `drakan_overrides.cpp`, earlier live notes). Markers as in `modern-pause-menu.md`: **[V]**
read in the decompile, **[L]** verified live before, **[U]** inferred, needs a live check. Library:
`runtime/gameui` `SaveSlots.h`, `SaveLoadScreen.h`, `TitleScreen.h`; glue contract in `runtime/gameui/README.md`.
Previews: `build/gameui-preview-saveload/` (main checkout).

## 1. Load (4) and Save (5)

### 1.1 Layout and widgets

* Load: ctor `43cf38`, handler `43ac40`; Save: subclass ctor `4442e0` (request `{mode, text, callback node}` ->
  `+0x1e4..+0x1ec`), handler `443360`, which forwards most messages to `43ac40` **[V]**. Layout `0x2330`: image
  `0x2331`, label `0x2332` (shown while no card is present), title label `0x2333` **[V]**.
* **There is no list widget for the slots** (the "class 12 list" in `mouse-menus.md` section 4 is not on these
  screens; layout `0x2320`'s widget `0x2321`, handled by `43ea98`, is a level list that copies a level name and opens
  screen 0xe: a debug level select **[U]**). The 2 x 2 grid is drawn by `43bfb0` from the image widget `0x2331`'s
  message 10 (`43ac40` case 10 -> `43d0e8` -> `43bfb0` when the widget is `0x2331`) **[V]**. The native mouse uses
  `drakan_menus` hotspots for the four cards, not widgets **[L]**.
* Keys go to `43b1e8` (screen `+0x50` = key id): Up / Down / Left / Right (`200 / 0xd0 / 0xcb / 0xcd`) move `+0xa4`
  around the 2 x 2 grid with the tick sound; `0x1c` (Enter / Cross) sends the screen message 2; `1` / `0xe` (START /
  Esc) = back: with a parent (`+0x34`) GUI event 0x12 (`2c4318`, message 4 to the parent), else `417520(1)` closes all
  screens and unpauses **[V]**. PgDn / PgUp (`0xd1` / `0xd3`) are taken by the pager hook first **[L]**.

### 1.2 The slot list (per page)

* Built by the scan `43b6f0` (via `43b108`, which frees the old list) when bit 3 of the u16 `+0x9c` is set (message 3
  sets `0xc`; the pager sets 8) **[L]**. For each of the four slot folders `"BESCES-50006" + *0x4fe3b8[i]` it lists
  `*.rsg`; the entry array is `+0xb8`, capacity `+0xbc`, count `+0xc0` (4 once built, 0 before) **[V]**.
* Entry, 0x168 bytes **[V]**: `+0x000` char[256] path `"BESCES-50006SlotN/<file>.rsg"` (empty = no save);
  `+0x100` char[40] caption: for a save `ctx+0x368 vt+0x2c(file)` (the level's display name, "Surdana" **[L]**), for an
  empty slot the text of property `0xfa`, for a slot without room for a new save property `0xfb` (the scan decides
  this from the card's free space, `43cb68`) **[V]**; `+0x127` forced 0; `+0x128` s64 sort key (0 for a save,
  `0x7fffffffffffffff` for an empty slot; comparator `43d178`); `+0x130` 0x24-byte time stamp copied from the
  directory entry: six s32 = second **[U]**, minute, hour, day, month, year (the grid prints `+0x138:+0x134` and the
  date from `+0x13c / +0x140 / +0x144` in the order of `ctx+0x4f8`: 0 = M/D/Y, 1 = D/M/Y, 2 = Y/M/D; `ctx+0x4f4` = 0 ->
  12 hour clock with AM / PM) **[V]**; `+0x154` thumbnail requested, `+0x158` loaded, `+0x15c / +0x160` card origin
  in GUI units, `+0x164` thumbnail image (128 x 128 CT16 at `+0x50`) **[L]**. The thumbnail is requested by the grid
  draw itself (`ctx+0x520 vt+0x1a4(entry, 0, 0, entry+0x100, 0x28, entry+0x164)` the first time an entry is drawn) **[V]**.
* Captions drawn as `"%d - %s"` (`0x54e3a0`, the pager adds the page offset) in white for a save and lime for an empty
  slot; a `0xfb` slot draws property `0xfc` (an explanation, up to five lines) in the picture area **[V]**.
* Play time: no field in the entry; none known in the save either. The model has `playSeconds = -1` (not shown).
* `37e810(0)` **[V]**: card info fails -> **2** (no card); not formatted -> **3**; any of the four slots has a save ->
  **1**; else free space < 0x5de -> **4** (full), else **0**. Load Game in the title / pause is enabled iff it is 1
  (the pager wrapper asks every page **[L]**). `37e810(1)` checks the settings file.
* HD previews (`drakan_save_previews`): when a `.rsg` is closed after writing, the last gameplay frame is fitted into
  the 7:3 aperture and saved as `<file>.rsg.<fnv64>.preview.png` beside the save; the pager's grid draw registers the
  native thumbnail's content hash for slot `page * 4 + i + 1` so the GS swaps in the PNG **[L]**. A host screen can
  load that PNG for `ImageRef{SavePreview, n}` directly (same name rule, `registerSlot`), else decode the native
  thumbnail (`+0x164`, flip as `registerSlot` does); old saves have only the native one.

### 1.3 What select / confirm / back do natively

* **Load, Enter** -> message 2 -> `43d028`: when the entry is a save, if a level runs (`*0x4fd570() & 4`) a Yes / No box
  (property `0x107`, default No) with `+0xa0 = 5`; its Yes sets `+0x9c |= 0x10` and the next message 10 (from the grid
  widget's draw) calls `43b4d0`; on the title it calls `43b4d0` at once **[V]**. `43b4d0` validates the file
  (`23f170`): ok -> path to `0x4fe3c8`, unpause, free the list, the load starts (4 -> 14 -> gameplay **[L]**); failed
  -> on-screen message `0x105` and the title opens (`417340(8)`) **[V]**.
* **Save, Enter** -> message 2 -> `444460`: empty slot -> `443ac0` writes at once; a save -> overwrite box (text
  `0x100` with `"%d - %s"`, default No, `+0xa0 = 1`); Yes arms the screen timer (`+0x44 / +0x48`), whose **message 9
  is sent from the screen draw `2bd1b0`** and runs `443ac0` **[V]**. `443ac0`: refuses a `0xfb` slot and a card that
  changed (`37e6b8` = -1 -> box `0x109`), opens the Saving screen 0x1a (not in mode 1), writes; success -> message
  `0x103`, frees the list, GUI event 0x18 (all screens closed, the game resumes); failure -> OK box `0x104` (`+0xa0 =
  2`), whose answer closes the screen **[V]**. Mode 1 (level change) calls the request's callback with Yes / No.
* **No card**: the Load / Save message 10 checks the card every frame; when GetInfo fails it hides `0x2331`, shows
  `0x2332` and opens an OK box (`0x108`); its answer closes the screen **[V]**.

### 1.4 Driving them from a host UI

Keep screen 4 / 5 open and hidden (as the pause's Options pages), read the list each EE frame (`gameui::readSaveSlots`,
plain reads), and act through the native paths:

* **Select:** write `+0xa4 = row` (exactly what the native hotspot hover does, `drakan_menus onHotspot`) **[L]**.
* **Page:** `drakan_menus::changePage` (rewrite the cells, set bit 3); the list reads Reading (count 0 / rebuilding)
  until the scan ran **[L]**.
* **Save into an empty slot / load from the title:** focus row + Cross tap (key `0x1c`, message 2) **[L]** (the
  native mouse click does the same).
* **Overwrite / load in game:** the host card asks, then **do not** answer the native box: its Yes defers the work to
  the timer message 9 / the next message 10, both of which run inside the screen draw, i.e. inside the hidden-draw
  wrapper's no-preempt section (the same trap as Default Settings, `modern-pause-stage2.md`). Post the routine the
  native Yes ends in as its own guest call instead: `443ac0(screen5)` (overwrite) / `43b4d0(screen4)` (load), after
  `+0xa4` is set **[U]**. Fallback: Cross + box Yes with the screen shown natively for that one action.
* **Back:** START tap (key 1) **[L]**.
* **Hiding:** the list's message 10 comes from the grid widget's draw, so `0x2331` must stay drawable (else no
  rescan, no card check, no deferred load). Hide the page with `+0x08 = 0` and `+0x3c = 0` on `0x2332` / `0x2333`
  only, and wrap `43bfb0`: while the host owns the screen, run only its thumbnail requests (the first block of each
  iteration) and skip the drawing; turn the `drakan_menus` pager text and hotspots off **[U]**. Native boxes the host
  did not answer (no card `0x108`, card changed `0x109`, save failed `0x104`, load failed `0x105`) are shown natively
  (host suspended, as the pause's native children).

## 2. Title menu (8)

* Layout `0x2300`, handler `43e4c8` **[V]**: image `0x2301` (sized to the whole GUI on message 3: the title art),
  rows by table `0x54e768` = Load Game `0x2302`, New Game `0x2304`, Options `0x2305`, Credits `0x2306`, Quit `0x2303`
  (hidden by the stock code, re-enabled by `drakan_menus` **[L]**). Frame `+0x08 / +0x0c` = 0.3 x 0.35 (the panel
  behind the rows), `+0x00` = property 0x43 (its art). No Continue and no PC Settings row natively (PC Settings is
  Options' repurposed `0x2345` **[L]**).
* Message 1 (ignored while `*(*(ctx+0x328)+0x224) > 0` **[U]**): `0x2302` -> factory(4, parent title); `0x2304` -> the
  first level's name (property `0x3f`, through the string manager) into `0x512920`, factory(0xe, parent 0): the
  loading screen, then the intro movie 0x1d **[L]** (no difficulty choice, no confirmation); `0x2305` -> 9 (parent
  title); `0x2306` -> 0x13 credits (widget-less, Cross continues) **[V]**.
* Message 3: focus `0x2302`, row positions, `43f398` (Load Game enabled iff `37e810(0) == 1`, focus moved off it
  otherwise), the music: property `0x3a` played on channel 2 when `2bb310()` says it is not running **[V]**; with
  open flag 1 an OK box `0x104` **[U]** meaning. Message 4 (back from a child): `43f398`. Message 10: `43f398` again
  when `ctx+0x520 vt+0x124(0)` (card changed **[U]**).
* Before the title: the memory card warning 0x1c (`43f570`: shown when `37e810(0)` is 0 / 2 / 4 and there is no
  settings file; Enter -> `43feb8`; the port taps Cross through it, `skip_memcard_warning`) -> logo movie 0x1e -> 8
  **[L]**.
* Hiding: the same `2bd1b0` wrapper as the pause: `+0x08 = 0` (no panel) and `+0x3c = 0` on the five rows, **not**
  on `0x2301`, so the native art keeps showing behind the host menu and the music is untouched (it is the sound
  manager's, not a widget's). `TitleState::backdrop` (`ImageKind::TitleArt`) exists for a host-drawn art instead; the
  art's texture id is **[U]**.
* Reached from: the boot sequence, Quit to Title (pause box Yes `417688(1)` + `417340(8)` **[L]**), a failed load.
  The pause's Save / Load (0x2311 / 0x2312) and the title's Load Game open the same screens 5 / 4; the level exit
  `361f60` opens 5 in mode 1 **[L]**. Existing hooks on these paths: `drakan_menus` (title handler, slot key,
  sprintf, `37e810`, save ctor test aid), `drakan_overrides` (Options `0x2345`), the pause glue.

## 3. Library

* `SaveSlots.h`: `SaveSlot`, `SaveSlotsState`, `CardState`, `SlotState`, `SaveTimestamp` + formatting,
  `readSaveSlots(mem, screen, opts, state)` (counts 0 -> Reading; the no-space caption is passed in by the glue).
* `SaveLoadScreen`: four slot cards per page (thumbnail = `ImageRef{SavePreview, n}` over the crest, slot number,
  location, date and time, play time when known; empty / no space / damaged marked) and a page bar; a large preview
  panel; Overwrite / Load (in game) / optional Save cards; Reading, No card, Unformatted, Error, Full states; Back
  ("Skip" at a level change). Actions SelectSlot, ChangePage, ConfirmSave, ConfirmOverwrite, ConfirmLoad, Back.
* `TitleScreen`: centred column (Continue, Load Game, New Game, Options, Credits, PC Settings, Quit), the focused
  row's description, the Quit card. Options reuses `PauseScreen`'s Options / Audio / Controller pages through the new
  `PauseConfig::rootPage / statusPanel / centred / rootTitle` (one component; its actions are PauseActions). It is
  independent of the data-driven PC Settings pages (those stay the F1 overlay's).
* Tests `gameui_saveload_test` (408 checks); previews `load_*`, `save_*`, `title_*`.

## 4. Risks

* Deferred native work inside the draw (overwrite timer message 9, deferred load on message 10): run the routines as
  posted guest calls; never under the hidden-draw no-preempt section.
* Hiding the grid widget would stop the slot screen's per-frame logic; hide by wrapping `43bfb0` instead.
* `443ac0` / `43b4d0` as posted calls are not verified live; both are what the native Yes runs, on the GUI thread.
* The time stamp's second field and year format, the meaning of the title's message-1 guard and open flag, and the
  title art texture are **[U]**.
* A save closes every screen on success (GUI event 0x18): the host must unbind the pause and the save screen then.
