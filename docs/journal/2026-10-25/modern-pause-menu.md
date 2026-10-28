# Modern pause menu: research and design — 2026-10-25

Goal: a host-drawn Esc (pause) menu in the visual language of the modern Tab screen (`mouse-menus.md` section 13,
`build/harness/tab3-v3/char_alt.png`, `tab2-final/final_open.png`), selected by `[general] pause_menu = native |
modern` (default `native` until verified live). Static research only: Ghidra decompile (`build/recomp_ui/alldecomp`),
the ELF's .data (read with a scratch script) and the disc layouts (`analysis/scripts/ui_layout_dump.py`). Nothing was
run. **[V]** = read in the decompile / data, **[L]** = already verified live in earlier notes, **[U]** = unverified
inference that needs a live check.

## 1. The native screens

### 1.1 Pause, screen 0x17

* Opened by `Game_OnInputCommand` (`0x23ce18`) on commands 1 / 0x120 / 0x12a (START; Esc is mapped to START |
  CIRCLE) when no inventory is open (`0x4fbafc == 0`) and `ctx+0x4c4` bit 0 is clear:
  `2c4458(0x4fb600, 0x17, parent 0, request 0, activate 1)` **[V]**. With `0x4fbafc != 0` (inventory open) it sets
  `player+0x58c = 1` instead and does not open the pause **[V]** (section 8 of `mouse-menus.md` says START opens the
  pause over the inventory; the two disagree, **[U]**).
* Factory case 0x17: object 0xa0 bytes, ctor `4326a0(obj, layout 0x2310, ...)`, vtable `0x54df98` (screen `+0x98`),
  handler at vtable `+0x44` = **`0x431f00`** (`+0x2c` is the generic `2be670`, which dispatches to `+0x44`) **[V]**.
  The manager keeps screens by id at `0x4fb610 + 4 * id`, so the pause screen object is `*0x4fb66c` **[V]**.
* Layout `0x2310`, widgets (the handler re-positions them in the order of the table at `0x54dff0`, so this is the
  on-screen order) **[V]**, labels from the disc **[V]**, ids hovered live **[L]**:

| # | Widget | Type | Text (XDB) | Message 1 (activation) |
|---|---|---|---|---|
| - | `0x1000` | image (9) | - | (top band image, y 0.029, h 0.075; not focusable) |
| 0 | `0x2316` | label (6) | "PAUSED" (168) | - |
| 1 | `0x2311` | button | "Save Game" (122) | `2c4458(mgr, 5, pause, 0, 1)` -> Save screen |
| 2 | `0x2312` | button | "Load Game" (117) | `2c4458(mgr, 4, pause, 0, 1)` -> Load screen |
| 3 | `0x2313` | button | "Options" (119) | `2c4458(mgr, 9, pause, 0, 1)` -> Options |
| 4 | `0x2314` | button | "Resume" (125) | `432708` (resume, below) |
| 5 | `0x2315` | button | "Quit" (121) | focus cleared (`2be528(scr, 0)`), Yes / No box `427680(-1.0f, scr, prop 0x14a, default No 0x238d, -1)` |

* Handler messages **[V]**:
  * **3 (open):** `43f0b8(0x4fbbb0)`; makes itself the active screen and sends itself message 4; ducks the sound
    manager (`ctx+0x32c`) channel volumes: saves channels 0..2 (`vt+0xbc`) to `0x5125ec/f0/f4`, sets channel 0 to 1.0
    and channels 1 and 2 to 0.5 (`vt+0xb4`); `ctx+0x358 -> +0x434 vt+0x11c(0)`; lays the six rows out
    (`y = (H - (6 lineH + 0x48)) / 2 + i (lineH + 12)`, +12 after the title); focus = `0x2311` (table entry 1);
    `432518` (enable states); screen `+0x40 = 0`, frame size `+0x0c = 0.5`, `+0x08 = 0.4` (the 0.4 W x 0.5 H dim panel
    and brass frame); `ctx+0x520 vt+0x254(1, 0)` (game logic paused, **[U]** name); resets the camera aim mode.
  * **4 (became active again, e.g. back from Options):** `432518`.
  * **6 (closed):** restores the three channel volumes.
  * **10 (per frame):** every 15th call `432518`.
  * **0xb (key):** if the GUI key id at `screen+0x50` is 1 (Esc, which START produces) -> `432708` (resume).
  * **0x10 (box answer):** Yes (`0x238b`) restores volumes, closes itself (`vt+0x34`), `417688(1)` (tears the level
    down), `417340(8)` opens the title as the active screen; No refocuses Save Game.
* `432518` enable states **[V]**: Load Game is enabled only when `37e810(0) == 1` (a save exists; our wrapper asks
  every save page, `save-slots.md`); Save Game is disabled when `37e810(0) == 2` (no card **[U]**) or `0x4fbafc != 0`.
  Disabling is `widget vt+0x6c(0)`; if the focus is on a disabled row it moves on (`2bcc38`). Which widget field
  `vt+0x6c` writes is **[U]** (MenuMouse treats `+0x3c` and `+0x40` both non-zero as "takes input"; read those).
* Resume `432708` **[V]**: `2bdf38` (plays UI property `0x2d`, the exit sound), posts GUI event 0x18
  (`2ca958(0x18, ...)`: `417520(1)` closes all screens, `ctx+0x520 vt+0x254(0, 0)` unpauses), unpauses directly too,
  `ctx+0x358 -> +0x434 vt+0x24()`.
* Quit: the port turns the native Yes into an exit: `drakan_input_glue.cpp` calls `requestStop()` when the active
  screen goes box (1) -> title (8) and `[general] quit_exits = 1` (default) **[V]**.

### 1.2 Options (9) and its children

* Options: layout `0x2340`, vtable `0x54eef0`, handler **`0x4424e0`**, table `0x54ef48` of `{widget, screen, spacing}`
  **[V]**: `0x2341` Controller -> 10, `0x2342` Audio -> 13, `0x2345` "Adjust Screen" -> 0x20 (**repurposed as "PC
  Settings"**: `drakan_overrides.cpp` opens the F1 overlay), `0x2343` Save Settings (hidden and disabled by
  `drakan_menus.cpp optionsLayout`, the port saves options automatically), `0x2344` Default Settings (Yes / No box,
  property `0x156`), `0x2346` Back (`vt+0x3c(1)`). Message 1 for a table entry calls `4432a0(scr)` and then the
  factory with parent = Options. Message 0xb forwards the key to `vt+0x3c` (back, `442cb0` -> `2bebe8`).
* Audio, screen 13 (0xd): layout `0x2350`, vtable `0x54eb78`, handler **`0x441258`**, activation `441440` **[V]**:

| Widget | Kind | Field (`0x4fb6d0 +`) | Apply on message 1 |
|---|---|---|---|
| `0x2351` Master Volume | slider (`screen+0x9c`) | `0x4fb75c` (+0x8c), 0..100 | sound mgr `vt+0xa4(value, -1)` |
| `0x2352` Music Volume | slider (`screen+0xa0`) | `0x4fb760` (+0x90) | `vt+0xa4(value, 2)` |
| `0x2353` Show Subtitles | check box (flag `0x80`) | `0x4fb740` (+0x70) | none |
| `0x2354` Stereo Sound | check box | `0x4fb744` (+0x74) | `vt+0xc4()` |

  Slider value = float at widget `+0x84` (`2c6c78` get; `2c6c80(v, w)` set = clamp to `+0x7c..+0x80` and dirty,
  no calls) **[V]**. Sliders may wait for a sound resource; their handlers must run as posted guest calls
  (`docs/journal/2026-10-22/audio-slider-freeze.md`).
* Controller, screen 10: layout `0x2360`, vtable `0x54ec38`, handler **`0x441728`** **[V]**: `0x2363` / `0x2364`
  Rynn / Arokh (which diagram is shown), `0x2367` scheme button (cycles `0x4fb9b8` 0..2,
  `Options_ApplyButtonScheme` + stick scheme + axis mapping), `0x2368` Vibration (`0x4fb73c`, applied through
  `ctx+0x358 -> +0x434 vt+0x124`), `0x2369` Reverse Pitch (`0x4fb734`). The DualShock diagram with labels is drawn
  by `441ac8` / `441c28` on the image widget `0x2361`'s message 10.
* All twelve option fields are persisted by `drakan_menus.cpp nativeOptionsTick` whenever they change, whoever
  changed them **[V]**.

### 1.3 Load (4), Save (5), boxes

* Load / Save: layout `0x2330`, handlers `0x43ac40` / `0x443360`, no slot widgets: entries at `screen+0xb8` (0x168
  bytes, caption `+0x100`, thumbnail `+0x164`), count `+0xc0`, selection `+0xa4`; pager and hotspots in
  `drakan_menus.cpp`; selecting a populated load slot loads (screen 4 -> 14 -> gameplay) **[L]** (`save-slots.md`).
  `.rsg` names are `"%s/%s.rsg"` with the second string copied from `ctx+0x98` (`0x354470` area) — very likely the
  current level name such as "Level 1A - Surdana" **[U]**. HD previews: `drakan_save_previews` captures the last
  gameplay frame when an `.rsg` is written and keys it by the file's content hash.
* Boxes: screens 1 / 2 / 3 / 0x1b, layout `0x2380`, Yes `0x238b`, OK `0x238c`, No `0x238d`; the answer is message
  0x10 to the parent **[V]**.

## 2. Driving the entries from a host UI

Three routes, in order of preference:

1. **Focus + pad tap (recommended for every button).** Put the native focus on the target row, then inject Cross
   (`InputModule::injectPadTap`, which passes in host-screen mode). The game's own key path (`2bc8c0` ->
   `2c0cf0`: press + release -> message 1) then runs on the live guest context, exactly as Enter / Cross does: the
   confirm sound, the factory's allocator semaphore and any yield are all native. Focus: `2be528(screen, widget)` is
   a plain setter the title hook already calls nested **[L]**; or step with Up / Down taps and wait for `+0x84`, which
   is what the Tab executor does for selections. Resume: a START tap (key id 1 -> `432708`), which `resume_on_close`
   already uses **[L]**, or Cross on `0x2314`. Expectations for the tracker: Save / Load / Options -> active screen
   becomes 5 / 4 / 9; Resume -> active screen -1; Quit -> a box (1) with the pause as parent.
2. **Synchronous widget press / release** (`widget vt+0x2c(3)` then `(2)`), as MenuMouse does for buttons **[L]**:
   works for Options today, but it is a nested call into a handler that calls the factory, and the factory waits on
   the allocator semaphore (the title Quit note in `save-slots.md` records a deadlock of exactly that on a copied
   context). Not needed when route 1 exists.
3. **Handler message 1 as a posted guest call** (`2bcf80(screen, widget, 1)` through `IGuest::post`). Right for
   **values**: set the slider with `2c6c80(v, widget)` (pure write), then post `2bcf80(audioScreen, widget, 1)` so
   `441440` applies it on its own guest call; for check boxes toggle flag `0x80` the way the widget's press does and
   post the message, or simply focus + Cross (route 1). Read back `0x4fb75c / 0x4fb760 / 0x4fb740 / 0x4fb744 /
   0x4fb73c / 0x4fb734 / 0x4fb9b8` as the confirmation. Writing the globals directly without the handler would skip
   the sound-manager apply, so do not.

Which entries need native sub-screens:

* **Save / Load:** keep native in the first phases (the pager, mouse hotspots, previews, the level-change save mode,
  overwrite / delete boxes and the load path are verified there). The host fades out while screen 4 / 5 is on top and
  comes back on message 4. A modern slot browser later: the host can list slots itself, read-only, from the card
  folders (`mc0/BESCES-50006SlotN/*.rsg`, name = level, mtime = date) plus `drakan_save_previews` pictures, but the
  action must still go through a hidden native screen 4 / 5 (open it, set the page cells and `+0xa4`, tap Cross;
  answer its boxes natively), the same "native screen kept alive but hidden" pattern as 0x15.
* **Options:** host page with Controller / Audio / PC Settings / Default Settings / Back. PC Settings opens the F1
  overlay directly (`drakan_settings_host::requestOpen()`), no native screen. Audio and Controller as host pages need
  their native screen open (handlers apply through the screen's widgets): open 9 -> 13 / 10 natively, keep them hidden,
  drive with route 3 / route 1, back with START. Phase 1 simply shows the native Options tree.
* **Controller diagram:** the DualShock picture is PS2 specific; the modern page shows scheme / vibration / reverse
  pitch and a "Key bindings" entry that opens the F1 Controls page.

## 3. Hiding the native page while it stays active

What is behind it: the world keeps **rendering** every frame under 0x17 (BeginWorld / EndWorld continue, camera
packets carry screen 23, `docs/journal/2026-10-23/pause-modern-water.md`) while game logic is paused; the gameplay HUD
orbs slide out of the corners in about 6 frames (`2026-10-24/pause-hud-orbs.md`). So the overlay's backdrop (frame copy
blurred with 17 taps, dimmed, `GameUiRenderer`) works unchanged; there is no `WorldView` hole (or one feathered centre
hole if a lighter look is wanted).

How the page is drawn **[V]**: the per-frame GUI draw `2cc8e0(screen)` (recurses to the parent first when `+0x4c & 1`)
calls the screen draw **`2bd1b0(screen)`**: optional timer message 9 (`+0x48`), `vt+0x4c`, open transition
(`+0x54`, `2bd5f0`, sets `+0x14 = 1` when done), then, if `+0x08 > 0` and `+0x0c > 0`, the dim sprite (`2c9e00`) and
the brass frame (`2c94a8`) sized `+0x08 x +0x0c` of the 512 x 512 canvas (matches the drawlog's dim panel 154..358 x
128..384), the screen's HUD object at `+0x10`, then **each child widget whose `+0x3c` is non-zero** (widget `vt+0x44`),
and finally the cursor sprite when `+0x40 != 0` (the pause sets it to 0).

Recommended suppression (EE, exact per frame, no GS timing lag): wrap `2bd1b0`; for the bound pause screen while the
host draws it, temporarily set `+0x08 = 0` and `+0x3c = 0` on every child, call the original under
`ps2xNoPreemptEnter/Leave` (as `crispSelectedText` does, so a scheduler checkpoint cannot unwind the wrapper between
the edit and the restore), restore. The transition, timers and focus state keep running; input is unaffected since
`+0x3c` is restored before any input runs. Caveat **[U]**: where message 10 to the pause handler comes from was not
found (for the slot screens it is the image widget's draw); if it is a widget draw, keep that widget (`0x1000`)
drawable or replace the 15-frame refresh (only matters if the card changes while paused). Fallback: the GS already
classifies pause page draws geometrically (`hud::pauseScreen`, page box 120..390 x 110..400, camera packet screen
0x17); a "page hidden" flag there could drop them, at the cost of the one-frame host / stream lag described in
`pause-hud-orbs.md`.

Also suppress for the modern mode: the gameplay HUD (`3ac540`, as `invViewShown` does) so the orbs do not slide out
under the menu. When a native child (Options, Save, Load, a box) is on top, stop hiding the children of *that* screen
(it is a different object) but keep the pause page hidden if `2cc8e0` draws it as parent.

## 4. Input

* Native semantics **[V]/[L]**: in the GUI, START produces the Esc key id 1 = back; Circle is R-MOUSE (`0x101`) and
  does nothing on the pause page; the port maps Esc / Backspace / right click to START | CIRCLE in the menu layer. On
  the pause page START / Esc = Resume; in Options and its children START / Esc = Back.
* Modern mapping: Up / Down (and the stick, wheel) move between rows; Confirm (Enter, Space, A / Cross, click) activates;
  Back (Esc, Backspace, B / Circle, right click, Start) = Resume on the root page and "back one page" on sub-pages;
  PagePrev / PageNext unused.
* Ownership: `InputModule::setHostScreen(true)` while the pause is bound and no native screen is on top (child screen
  or box): neutral pad, bindings and menu mouse off, injected taps pass. Native child on top -> host input off, the
  host screen fades out, the native screen gets the input.
* **The key that opened the screen is not a close**: Esc opens the pause through Raw Input before the window thread
  dispatches its `WM_KEYDOWN`, so the host would see a fresh Esc and resume at once — the same bug fixed for Tab
  (`tab-screen-real-input.md`). Reuse the three rules: drop presses stamped at or before the moment the layer took
  the input (`SettingsOverlay::handleMessage`), `gameui::KeyGate` reset on open **and every time the host gets the
  input back** (the Esc that closed a native Options page is still down when the pause becomes active again), and
  `InputAccumulator::releaseAll` on every ownership change (held keys repeat no presses). The pad START that opened it
  must be released before START counts. The F1 path injects START for 4 frames: a gate on pad START until released
  also covers that.

## 5. F1 overlay

`requestPauseThenOpen` injects START, waits for screen 0x17, then opens the overlay (`drakan_input_glue.cpp`). With
the modern pause the pause binds as usual and the overlay opens over the screen layer (the layer draws under the F1
menu and gives up input while the menu is open), so nothing changes in that path. On close the modern pause remains
(or `resume_on_close = 1` taps START, which the executor must treat as "screen gone", not as a failure). The modern
Options > "PC Settings" opens the overlay with `drakan_settings_host::requestOpen()` instead of activating `0x2345`.
`SettingsOverlay` has one `ScreenLayer`; make it a dispatcher that draws whichever host screen is bound (Tab or
pause; they cannot both be bound as the pause does not open over 0x15, **[U]**).

## 6. Design

### 6.1 Pages and layout (1920 x 1080 canvas, Tab screen style)

* **Backdrop:** blurred, dimmed frame copy; header band with "Paused" left (Gelasio Bold, the Tab screen's tab font
  size) and the gold / level on the right like the Tab header; footer hint bar with prompt glyphs.
* **Root page:** a left column panel (dark translucent fill, brass hairlines, corner brackets) with large rows:
  Resume, Save Game, Load Game, Options, Quit. Resume first (modern convention; the native order is Save, Load,
  Options, Resume, Quit with Save focused). Disabled rows (Load without saves, Save in a scripted state) greyed and
  skipped, read from the native widgets each snapshot. Focus = the game's flame plate like the Tab tabs.
* **Status strip** (right side cards): location (`ctx+0x98` string **[U]**), current objectives (gameui `Reader`
  quests: primary first, at most three, "Journal" hint opens Tab on Journal later), Rynn's health / mana / level /
  gold bars from the same snapshot (the Reader works outside 0x15: player from `ctx+0x4b0`). Play time: no guest field
  known; omit or show session time only.
* **Options page:** rows Controller, Audio, PC Settings, Default Settings, Back. **Audio page:** two sliders (Master,
  Music, 0..100 with value text) and two toggles. **Controller page:** scheme A / B / C selector, Vibration, Reverse
  pitch, "Key bindings" (F1 Controls).
* **Quit:** host confirm card "Quit to title" / "Quit to desktop" / Cancel. Desktop = `drakan_win::requestClose()`
  (as F1 > Quit to Desktop). Title = activate `0x2315` and answer the native box Yes (focus `0x238b` + Cross) with a
  one-shot flag that suppresses the `quit_exits` conversion. Phase 1: plain native box over the faded host screen.

### 6.2 Code

* `runtime/gameui`: `PauseScreen` beside `InventoryScreen` (`Screen.h` or `PauseScreen.h`): pages Root / Options /
  Audio / Controller / QuitConfirm, the same `HitTarget` + `pickDirectional` navigation, `FrameOutput` with draw list,
  sounds and actions. Model: `PauseSnapshot` (row enable flags, the seven option values, slider ranges, native state:
  active screen id, box up, transition) read by a small `readPause()`; shared `Snapshot` pieces for the status strip.
  Actions: `ActivateRow(widgetId)`, `Resume`, `SetOption(field, value)`, `OpenPcSettings`, `QuitToDesktop`, with
  `Expectation`s (active screen change, field value). Unit tests over a fake pause screen like `FakeGame.h`.
* Glue `ps2xRuntime/src/lib/drakan_pause_screen.{h,inl}` (shared state like `drakan_tab::Shared`): bind on the pause
  handler `431f00` message 3 (only when `pause_menu = modern` and the layer is ready; hook on entry, the stock
  message 3 may yield), unbind when `*0x4fb66c` no longer holds the bound object or on message 6; the `2bd1b0` wrapper;
  the executor and the snapshot at the PadMgr tick (`drakan_input_glue.cpp`, next to `tabTick`); watchdog and fallback
  copied from Tab (host not drawn for 90 EE frames and 2.5 s -> stop hiding, release host-screen input).
* Overlay: `pauseDraw` in `drakan_settings_host.cpp` reusing `GameUiRenderer`, fonts, cursor and the KeyGate path;
  F1 > Gameplay > "Pause menu: Modern / Original"; `drakan_config` key `[general] pause_menu`, env
  `DRAKAN_PAUSE_MENU`, read on each open.

### 6.3 Phases

1. Root page only: bind, hide the page (and the HUD), status strip, rows via focus + Cross / START; every native child
   (Options tree, Save, Load, Quit box) shown natively with the host faded out, host back on message 4. Verify: open
   with real Esc and pad START (no instant resume), Resume, Save -> native save -> back, Load, Options -> PC Settings,
   Quit box, F1 in gameplay, watchdog fallback, `pause_menu = native` unchanged, 16:9 / 21:9 / 4:3.
2. Options / Audio / Controller as host pages over hidden native screens 9 / 13 / 10 (route 3 for sliders, route 1 for
   toggles and the scheme button), PC Settings direct.
3. Modern Quit card (title / desktop), modern save / load browser over hidden 4 / 5 with the HD previews.

## 7. Risks

* **Hiding by edit-and-restore around `2bd1b0`**: needs no-preempt; if the draw blocks (GIF kick, first texture
  load) inside no-preempt it stalls rather than unwinds — the same trade-off `crispSelectedText` accepted. Live check
  first; GS-side fallback exists.
* **Message 10 source unknown**: skipping the wrong widget stops the 15-frame enable refresh (and, for screens 13 / 10
  hidden in phase 2, the controller diagram draw, which is wanted).
* **Opening key / held keys**: Esc and START are both open and close keys here, unlike Tab; the KeyGate must cover pad
  START and the return from native children, not just the opening.
* **Nested factory calls**: never activate rows with synchronous nested calls from the host path; use taps.
* **Quit conversion**: the glue turns box -> title into an exit; a modern "Quit to title" must suppress it once.
* **Volume ducking and pause flags** are owned by the native screen; the design never closes the pause behind the
  host's back, so they stay balanced. A fallback must leave the native page fully usable.
* **Sub-screens hidden in phase 2** inherit the slider sound-resource wait (`audio-slider-freeze.md`): posted calls
  only, one at a time.
* Addresses marked **[U]** (location string, card-state meaning of `37e810 == 2`, `vt+0x6c` field, pause over the
  inventory) need a RAM peek before they are used.
