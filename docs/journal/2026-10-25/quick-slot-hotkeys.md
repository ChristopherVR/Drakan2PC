# Quick-slot hotkeys: keys 1..5 use / equip the quick inventory — 2026-10-25

User request: "make 1-5 triggering it to equip it". In gameplay, key N uses or equips the Nth quick-slotted item. Items
are marked as quick slots in the inventory (Cross natively, F on the modern Tab screen and the I strip), which sets the
slot record's hot flag `+4`.

Sources: Ghidra decompile `build/recomp_ui/alldecomp/all_00206aa0.c` (`23cae0`, `Game_OnInputCommand` `23ce18`),
`all_00335b70.c` (`33f630`, `33fa20`, `33fc88`, `33fe60`, `3412a0`, `3412c8`, `341388`, `3414a8`, `341670`, `341ce8`,
`341fd8`), `all_00414280.c` (`434288`). Notation as in [tab-rework-data-model.md](../2026-10-24/tab-rework-data-model.md):
`ctl = player + 0x584`. **[dec]** = read in the decompile, **[RAM]** = checked in a dump.

## 1. The native Quick Inventory

| Field | Meaning | Evidence |
| --- | --- | --- |
| `ctl+0x10` | bar shown (`33fc88(ctl, shown)`) | [dec], [RAM] 1 after Q with one elixir |
| `ctl+0x214` | list: array of 8-byte records `{cell, 0}` (heap, grown in steps of 5) | [dec] `33fa20`, [RAM] |
| `ctl+0x218` / `+0x21c` | capacity / count | [dec], [RAM] |
| `ctl+0x220` | the bar's current index (reset to 0 by `33fc88(ctl, 1)` only when it is >= count) | [dec] |
| `ctl+0x224..+0x234` | scroll animation (`+0x228` = scrolling, `+0x230` direction; `3412a0` next, `3412c8` previous) | [dec] |
| `ctl+0x238` | dirty: the next open rebuilds an empty list (`341388` -> `33fa20`) | [dec]; [RAM] count 0 / dirty 1 right after a load |

**Order.** Both builders (`33fa20` and the Equipment section's close `434288`) walk the 8 x 3 grid **column by column,
top to bottom**: cells 0, 8, 16, 1, 9, 17, 2, ... Only top-left cells of an item (not the continuation markers of a
1x2 item) with the hot flag count. `434288` resets the list and rebuilds it on every inventory close, so toggles made
in the inventory take effect when it closes; the list is empty after a load until the first open (dirty flag). Toggling
a slot does not touch the list (`341ce8` only flips `+4`); `33fe60` removes an item (used up, dropped). **[RAM]** with
the bow (cell 0, 1x2), the club (cell 1, 1x2) and the elixir (cell 2) marked: list `[0, 1, 2]` after the close.

**Capacity.** No limit was found: `33fa20` grows the array as needed. `3414a8` (the refusal `0x1af` of `341ce8`) is not
"full": it is true when the item's quantity decodes to 0 (`*(instance+0x14) == key`, a spent item). `0x1ae` is the
requirement check `341858`. The Tab and strip screens' refusal text now says "used up, or its requirement is not met".

**Pad path** (`Game_OnInputCommand`, Standard scheme; the live table at `0x4e9800` maps the pad actions):

| Command (pad) | Bar closed | Bar shown |
| --- | --- | --- |
| `0x403a` R1 / Q / 6 | `23cae0(1)`: count 1 and a weapon -> `33eaf8` + `33f630` at once (no bar); otherwise `33fc88(ctl, 1)` opens the bar at `+0x220`. Refused (returns 0) on Arokh, in the `23f5a0` states 1 / 2, while `320ab0`, with a summon running (`player+0x1040` not 0 / 5), or with `player+0xab0 & 0x8000` | next item (`3412a0`), unless still scrolling |
| `0x4015` / `0x4016` D-pad Left / Right | (their own moves) | previous / next item |
| `0x4006` Square (Attack) | attack | **use / equip** `list[+0x220]`: `33eaf8` + `33f630`, then `33fc88(ctl, 0)` closes the bar (unless `ab0 & 0x8000`) |
| `0x4012` Cross | crouch | the same as Square |
| `0x4011` Triangle | jump | close the bar without using |

Start (pad action `0x120`) is bound to `0x4006` as well in the live keymap (it is the pause path in
`Game_OnInputCommand`): a resolver must not pick it. **[RAM]**

**Use / equip** (`33f630`) for a record that is not equipped posts message `0x1045` with the cell (deferred: weapons are
put away and drawn by the animation, potions drunk); on an equipped record it posts `0x1045` with -1, i.e. it puts the
item away. **[dec]**

## 2. Route chosen

`runtime/input/include/input/QuickSlots.h` (`QuickSlotDriver`), glue `quickSlotsTick` and the `hookPadRead` overlay in
`drakan_input_glue.cpp`. Everything runs through the native PadMgr dispatch on the game's own thread, the same kind of
route as `drakan_quick_inventory.h`; no guest function is called from the host.

1. On a QUICK*n* press (EE thread, before the PadMgr update; free gameplay only: no GUI screen, no cut scene flag
   `0x4fbafc`, inventory closed, not on Arokh, focused, not suspended, no inventory view pending): read the live list
   (or, after a load, the column-major order of the hot flags), take cell `list[n-1]`. An empty slot, a summon or a busy
   controller does nothing; an equipped **weapon** is left alone (Use would put it away).
2. Write `ctl+0x220 = n - 1` while the bar is closed.
3. Overlay the scheme's Quick Inventory button (resolved from the live keymap, never Start / Select): 2 samples held
   released (an edge even when the key is physically held), then 2 pressed.
4. If the bar is shown afterwards (and the list still has the cell at `n - 1`): overlay the scheme's Attack button the
   same way -> the native confirm uses / equips and closes the bar. If the bar did not open: with one quick weapon it
   was used directly; otherwise the game refused it (e.g. a weapon swap or a drink still playing) and the press is
   retried up to 6 times, 15 frames apart, then dropped with a log line.
5. Any loss of free gameplay cancels and clears the overlay. A press while one is running (key repeat, a second key) is
   ignored.

Log lines: `[in] frame N [quick-slot] slot 2 -> cell 1, item a6b5b0` ... `confirmed, cell 1 equipped=0` (the equip
flag follows the deferred message a few hundred EE frames later).

Bindings: `[bindings] 1 = QUICK1` .. `5 = QUICK5` (`QUICK1..QUICK9` accepted), 6 stays `ACT:0x0e` (Quick
Inventory). `Config::migrate` turns `ACT:0x02..ACT:0x06` (the old defaults, keymap commands `0x403b..0x403f` with no
native handler) into `QUICK1..QUICK5` on whatever source carries them; the F1 Controls page shows them as "Quick slot
1..5" (`settings` `parseLayer` maps the same tokens, untouched lines stay as they are in the file).

UI: the I strip's and the Tab screen's quick-slot numbers now follow the native order (`gameui::quickSlotItems`); the
Tab screen's grid also shows the number in a pill on each quick-slotted item, like the strip.

## 3. Runs

`scripts/harness/play_run.py`, Surdana card copy `build/harness/polish-save/mc0`, private copies of the `aui` inis
(`build/harness/qsk/drakan.ini` with `inventory_screen = modern`, `drakan_input.ini` with the **old** `1 = ACT:0x02`
.. lines, so the migration ran too), `DRAKAN_NO_ACTIVATE`. The user's `dist/DrakanPC` ini files, saves and log were
not touched. RAM read with a scratch probe (slots, list, HP `dec(S+0xc)`, `player+0xEAC` / `+0xED0` channels).

| Run | Result |
| --- | --- |
| `qsk-run1` | first build: the Attack resolver picked Start (`0x120` -> `0x4006`), the pause menu opened. Fixed (Start / Select excluded), unit test added |
| `qsk-run2` | equips worked; a key pressed while a weapon swap or the drink still played was refused natively ("no bar"): retry added |
| `qsk-run3` | the final behaviour, below |

`qsk-run3` (`build/harness/qsk-run3`, recipe `build/harness/qsk/recipe3.txt`):

| Step | Log | RAM / screenshot |
| --- | --- | --- |
| 1 right after the load, nothing marked | `slot 1: empty (0 quick items)` | `ram_0`: count 0, dirty 1 |
| Tab, F on bow / club / elixir, Tab | 3 x `toggle-hot-slot -> confirmed` | `ram_1`: hot 1 on cells 0, 1, 2; list `[0, 1, 2]`; `tab_hot.png` (pills 1 2 3, Quick Inventory row 1 2 3) |
| 1 | `slot 1 -> cell 0` ... `confirmed, cell 0 equipped=1` | `ram_k1`: bow eq 1, EAC -> bow handle `a6f930`; `k1_bow.png` (bow out, HUD bow, 181 arrows) |
| 1 again | `slot 1: cell 0 already equipped` | unchanged |
| 2, then 1 50 frames later (mid-swap) | slot 2 confirmed; slot 1 confirmed after the retries (99 frames) | `ram_k21`: bow eq 1, EAC -> bow |
| 2 | confirmed | `ram_k2`: club eq 1, bow 0, ED0 -> club `a6b5b0`; `k2_club.png` (club in hand, HUD club) |
| 3 | `slot 3 -> cell 2` ... confirmed | `ram_k3a` (+80 frames): HP 84; `ram_k3` (+480): **HP 100 / 100**, the elixir is gone (single use, removed from the grid and the list, count 2); the club was put away; `k3_elixir.png` (drinking, HUD elixir) |
| 3 again, 5 | `empty (2 quick items)` twice | unchanged |
| 1 held 100 frames | one `slot 1 ->` / one confirm | `ram_k1h`: bow eq 1 |
| I | | `strip.png`: bow "Equipped · Quick Slot", pills 1 2 |

Tests: `quick_slots_test` (input; bindings, migration, column order, live list, pad resolver incl. the Start case, the
driver against a model of the bar: open + confirm, held buttons, empty slot, single weapon used directly, already
equipped, repeat, refusals, retry and give-up, cancel, rebuild after a load, a bar already shown), `gameui_test`
`testQuickSlotOrder`, `settings_test` (ACT:0x02 shown as QUICK1), `input_test` (the generic ACT test moved to key 6).

## Not verified

* Swimming, climbing, a running summon and cut scenes in game: refused by the native `0x403a` checks and by the
  driver's gates, covered only by the decompile and the unit tests. Arokh: gated by the controlled-actor class.
* Pressing a key while attacking: the driver releases and re-presses Square for the confirm, which also delivers an
  attack release to the game; not checked against a live combo.
* The modern bow aim (LMB held with the bow out) together with a key press.
* Other controller schemes (B / C): the buttons come from the live keymap, but only Standard was run.
* A quick slot on armour, spell scrolls and other consumables (`0x10059`, `0x100ab`): no fixture.
* The post-load path in game (hot items saved in the card, list rebuilt by the first open): unit test only; the
  fixture card has no quick slots.
