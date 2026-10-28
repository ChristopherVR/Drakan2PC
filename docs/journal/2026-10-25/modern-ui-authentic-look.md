# Modern UI screens: fix round and a more authentic Drakan look — 2026-10-25

Follow-up to `modern-views-integration.md` (host-drawn I strip, M map panel, Esc menu) and the Tab screen
(`tab-rework-phase3.md`). Reference: `docs/reference/mouse-menus.md` sections 12-14, `runtime/gameui/README.md`.

## Setup

All runs: `scripts/harness/play_run.py` with the Surdana card copy `build/harness/polish-save/mc0`, private
`build/harness/aui/drakan.ini` (`inventory_screen = modern`, `pause_menu = modern`) and `drakan_input.ini`
(`DRAKAN_INI`, `DRAKAN_INPUT_INI`), 1600 x 900 window, 16:9, logs in the run folders. The user's `dist/DrakanPC` ini
files, saves and log were not touched. Another game held the foreground the whole time: no real input was sent.

`drakan_window.cpp` `show()` used to take the foreground (`SetForegroundWindow`, retried with `AttachThreadInput`);
`play_run.py` now sets `DRAKAN_NO_ACTIVATE=1`, which shows the window behind the others without activating it, and
skips the scripted shot's desktop BMP (it copies the screen where the window is, i.e. whatever else is on top). The
renderer PNG does not need the window to be visible.

## Part 1: fix round

### Elixir cell without its icon (I strip)

The strip's elixir cell showed only "100" and the card's icon box was empty, for the whole opening in every earlier
run (`mvi-v2`, `mvi-strip2..4`, `mvi-eq2`). Not the capture timing, the cell placement or a frame-copy race:

* `DRAKAN_DEBUG_TAB_PEEK=1` (new test aid: the host screen runs but draws nothing) shows the native frame under the
  strip (`build/harness/aui-peek1/p200.png`): the black fill, the bow, the club and a dim dagger, no elixir and no
  native "100" anywhere. Without the black fill (temporary switch, `aui-peek2`) the same; on the Tab screen in the same
  run the elixir is drawn.
* Logging the item draws (`42a4b0` from `434e80`, return `4354f4`; temporary): the elixir is placed in its cell
  (position near the cell, scale 0.637 against 1.015 on the Tab screen), but its `+0x104` reads `0x601d401` (Tab
  `0x601502d`) and `+0x114` is 0 (Tab 1.0). Clearing bit `0x8000` before the draw changed nothing (`aui-peek4`).
* The native grid moved up by 250 GUI units at the strip's scale: no elixir (`aui-pk-dy`). Scaled up around the
  strip's centre: 1.15x (object scale 0.73) a pale elixir after ~40 frames, 1.3x / 1.45x / 1.6x (0.83 .. 1.0) drawn,
  from the first frame at 1.6x (`aui-pk-k*`, `p045`/`p080`). So it is the object's size.
* Decompiled (`build/aui-decomp`, `build/recomp_ui/alldecomp`): `189b38` (the mesh draw, through the renderer slot
  `*0x4fd558` = `13ad48`) and `17c208` skip the draw while the model or one of its textures is not resident
  (`(res+8 & 3) != 3`); `17c208` keeps `+0x104` bit 8 clear then, and `18e0c8` fades `+0x114` in once it draws. The
  elixir's texture did not become resident at the strip's size. Same model handle (`0x31765`) and texture list in
  both dumps (`build/harness/aui-dump/ram_strip.ram`, `ram_tab.ram`).

Fix: the native grid is no longer placed under the host cells. `StripScreen::Layout::nativeGrid` is the Tab screen's
grid proportions fitted into a new opaque plate (prompts, grid and card on one panel), native scale about 0.85 at 16:9;
the overlay publishes that as the `GridTarget`, and the renderer fits each crop into the host cell (it already fitted
crops by aspect). Result (`build/harness/aui-p1-strip`): the elixir icon is in its cell 10 frames after opening
(`s050.png`) and stays; the dagger is no longer dim (`s140.png`). The native quick-slot marker (`section+0x80`, a
quad `434e80` draws at a marked item's cell corner, seen as a white-blue triangle in `mvi-v2/strip_hot.png`) is taken
away during a host view's grid draw and restored after (withdrawn in the hang fix below: a pointer write that can
outlive a yielded draw).

### The bow's card showed "..." (I strip)

`mvi-v2/drakan.log`: the strip's first `select-item(0)` reached the cell after 43 EE frames (`native selection old=1
target=0` at frame 12600) with empty details (`valid=0`). The executor's rebuild (`33eaf8` invoked again) was allowed
only while `wait < 40`, counted from the action's start, so the action was confirmed without a rebuild; the lines came
only through the screen's 30-frame resend (frame 12631), after the screenshot. The Tab screen does re-invoke the
selector the same way (same executor); its steering was just shorter. Now the window counts from the frame the
selection arrived (`TabState::selectedAt`): `aui-p1-strip/drakan.log` frame 4229 select, 4237 "details of cell 0
empty: selector run again", lines present (`s140.png`).

On the Tab screen the first steer was refused after 60 frames in the same run (`aui-p1-tab`): it ran during the open
animation, which ignores the arrows. Item steering now waits while `screen+0x15c` > 0 (at most 600 frames):
`aui-p1-use` confirms the first select after 72 frames.

### "Paused" under the fps counter (Esc menu)

The title sat at design x 96 in the header band, where the fps counter's box is. It is now centred in the header band
(the Tab screen centres its page row): `aui-p1-strip/pause.png`.

### Using the elixir (after the time-out fix `ae4dd7e`)

* Strip (`aui-p1-strip`): Right, Right, Enter -> `use-or-equip(2) -> confirmed` after 58 frames; the card then reads
  "Consumable · Equipped" (`s_used.png`).
* Tab screen (`aui-p1-use`): the same, confirmed after 49 frames (`t_used.png`). HP on the modern pause screen: 84 /
  100 before (`hp_before.png`), 100 / 100 about 600 frames after the screen closed (`hp_after.png`). The native Use
  readies the elixir; it heals in game time once the inventory is closed (the pause taken 60 frames after the strip
  closed still showed 84). The card's charges stayed 100 / 100.

## Hang after a right click on the Tab screen (fix before Part 2)

The user's session (copy `build/aui-hang/user_drakan.log`, Shadowmire): hover on the bow (`select-item(0)` confirmed at
frame 11094), right click (`close(-1)`, Select tap at 11101), then `[guest-branch:missing-target] ... source=0x17a068
target=0x0` and a dead game. `17a030` is the teardown called by the item class's destructor `178f28` (vtable
`0x51b8b0`, the inventory item objects): it calls through `*(obj+0xfc)+0xc`, which was 0. The same crash had appeared
once in my experiment run `aui-peek4` right after a Tab close, without any selection change.

Cause: the inventory screen and its sections are allocated per opening (`10715f0`, `10f8240`, `10aff70` in the user's
log). `tabPlaceGrid` changes the section's `+0x60..+0x6c` for the Equipment draw and the hook restores them when the
draw returns, but the grid draw's black-fill invocation (`InvDrawBatch::run`) unwinds the hook every frame, so the
restore happened at the next frame's entry, and after a close at the next opening: into the freed section of the
previous screen. A log line added for it fired at the end of every session in the stress runs (44 of 44 closes in
`aui-rclick3` / `aui-rclick4`). Whatever had been allocated there got four floats written over `+0x60..+0x6c` (an item
object keeps its texture list at `+0x60`); a later teardown of that object crashed.

Fix (`drakan_tab_screen.inl`): the placement is written back only into the section being drawn (or the open screen on a
fallback) and dropped at `tabBegin` / `tabEnd`. The coordinator's race hypothesis (close during a selection change) is
covered too: `CloseExecutor` taps Select only after the controller has been idle for 3 snapshots (at most 120 frames;
"waited N for the controller" in the log), and a queued or steering `SelectItem` is dropped once a close was asked
for, so a right click right after a hover closes without moving the native selection. The I strip and the M map go
through the same executor. Recipes `build/harness/aui/rclick_recipe.txt` (30 Tab cycles: right click right after
opening, after hovering two items, after a double-click equip) and `rclick_views.txt` (strip and map):
`aui-rclick5` (22 Tab openings and closes) and `aui-rclick6` (16 strip, 8 map): no missing target, every close done,
both scripts reached `quit`. The crash itself was not reproduced before the fix in these runs (it needs the freed
address to be reused by a live object); the corrupting writes were.

## Part 2: a more authentic Drakan look

Goal: keep the modern layout and usability, make the art direction Drakan's. Sources: the game's own `INTERFCE.TXD`
(contact sheet `build/ui_polish/sheets/INTERFCE_INTERFCE_0.png`, exports `build/aui-art/`) and Order of the Flame's
inventory (`build/ootf-reference/moby-6121063-*.png`: gilded relief frame with a pale gem on top, bone crescent
wings, dark red-brown cells on bronze lines, a thin red selection trace).

Art from the disc (read at runtime by the host, `kArtIds`; optional in `GameSkin`, procedural fallbacks):

| Tex | What | Used for |
| --- | --- | --- |
| 94 | inventory icon atlas (128x128): chest, stars, Rynn's face, map, book, handshake, stone, "R1 L1", gold bag | page tab icons (the native tab row maps chest = Equipment, stars = Spells, face = Skills, map, book = Journal; `build/harness/polish-off/strip.png`), gold |
| 129 | pale blue-grey orb (64x64) | the crest's gem |
| 86 | cream cross-hatched tile | journal parchment |
| 118 / 88 | brass message-box frame / brown stone (already in `GameSkin`) | `Main` panels |

Seen but not used: 93 (stone medallion around the native tab icons; too busy at 34 px next to a label), 127 (the
HUD's bone thorn pieces), 178-182 (spell school icons: the mapping from spell index to icon is not known), 62 (map
scroll).

Design decisions:

* **Palette** (`gameui::Theme`): cells `#1f0b05` (OotF `#1b0800..#220500`), lattice dark bronze with a gilt
  (`#e2b65c`) outer edge, selection trace `#d62216`, ivory accents, text in parchment ivory, headings gilt ivory,
  labels bronze.
* **Panels**: the game's brass frame around large panels (pause columns, map panel, detail card, strip plate,
  Character columns); the grid's own panel was dropped (the lattice edge frames it); cards and rows keep thin bronze
  edges. Insets are red-brown, faintly mottled with the stone tile.
* **Selection**: grid cells get the thin red trace over a red-brown wash (no flame); rows and cards keep the game's
  flame low along a softer wash with the trace (the native menus highlight with fire). The first pass was too red on
  the pause rows (`build/harness/aui-v1/pause.png`); the wash and trace were toned down (`aui-v2`).
* **Crest**: one ornament (gem in a gilt diamond between two ivory crescents, drawn by the renderer) on the header
  rules of the Tab screen and the Esc menu (a gilt hairline brightens towards it in steps) and on the top edge of the
  strip plate (over the grid, as OotF's gem) and the map panel.
* **Typography**: Gelasio kept; section labels as letter-spaced capitals (16 design units, 0.14 em tracking) in bronze
  ("EQUIPPED", "QUICK INVENTORY", "LOCATION", "MAIN OBJECTIVES", ...); titles and item names unchanged in gilt ivory.
  Real small caps would need a small-caps face; letter-spaced capitals read as engraved labels without it.
* Clutter check: no new texture over text, ornaments only at three kinds of places (header rule, plate / panel top,
  frame corners); the lattice gaps were darkened after the first pass (bright orange lines, `aui-v1/tab_inv.png`).

Before / after (1600 x 900, 16:9, Surdana save; before = `build/harness/aui-before`, the state after Part 1, after =
`aui-after`), side by side in `build/harness/aui-pairs/`: `strip`, `strip_focus2`, `map`, `map_row`, `pause`,
`pause_row`, `tab_inv`, `tab_char`, `tab_spells`, `tab_skills`, `tab_journal`, `tab_map` (`*_before_after.png`).

Not verified: 21:9 / 4:3 and 4K outputs, the gamepad prompt glyphs with the new look, the shop (Merchant) page, the
fallbacks without the disc's optional art (only reasoned; the art was always present).
