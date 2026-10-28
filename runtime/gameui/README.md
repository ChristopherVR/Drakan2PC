# drakan_gameui: modern Tab screen core

Static library `drakan::gameui`, **std only and platform free** (no ImGui, D3D, Win32): everything the host-drawn
full-screen inventory needs except the drawing and the game hooks. Phase 1 of the plan in
`docs/journal/2026-10-24/tab-rework-rendering.md`; the guest layouts are in
`docs/journal/2026-10-24/tab-rework-data-model.md`. Selected by `[general] inventory_screen = native | modern` in
`drakan.ini` (default `native`; also on the F1 General page). Phase 2 (the game glue, the ImGui renderer
`dsettings::GameUiRenderer`) is in `docs/reference/mouse-menus.md` section 13.

```
include/gameui/GuestMemory.h  IGuestMemory (read-only guest view), FlatGuestMemory (rdram copy / RAM dump), Latin-1 -> UTF-8
include/gameui/Dialog.h       readDialog: native message-box text / choices; drawDialog: modern presentation, native input
include/gameui/Addresses.h    every guest address / offset used, named, with the note's evidence markers
include/gameui/Model.h        Snapshot (items, spells, skills, stats, quests, regions, shop, native state), categories,
                              compareItems, ModelExchange (double buffer: game thread writes, UI thread reads)
include/gameui/Reader.h       readSnapshot(mem, ReaderState&, Snapshot&) and the pieces (findRoots, lookupString, ...)
include/gameui/Actions.h      UiAction, ActionQueue / ResultQueue, Expectation + evaluate(), ActionTracker
include/gameui/Draw.h         DrawCmd / DrawList (rects, panels, text, images by opaque ImageRef, world-view hole)
include/gameui/Layout.h       1920x1080 design canvas -> 16:9 safe area, text measure / wrap / ellipsize, Theme
include/gameui/Input.h        UiInput: device-neutral keys, pointer, clicks, wheel
include/gameui/Screen.h       InventoryScreen: pages, focus, navigation, hints, drawing
include/gameui/StripScreen.h  StripScreen: the compact "I" inventory (OotF placement, modern style)
include/gameui/MapScreen.h    MapScreen: the "M" map view's objectives overlay and marker ring
include/gameui/PauseScreen.h  PauseScreen: the modern Esc menu (phases 1-3), PauseAction / PauseActionQueue
include/gameui/PauseOptions.h PauseOptionsState: the seven Audio / Controller option values, readPauseOptions(mem)
include/gameui/SettingsSchema.h SettingsSchema: pages / sections / rows and key binding layer views (data only;
                              dsettings::buildSettingsSchema produces it, the F1 overlay and SettingsPages draw it)
include/gameui/SettingsPages.h SettingsPages: PC Settings as modern pages (landing, categories, key bindings, cards)
include/gameui/SaveSlots.h    SaveSlot / SaveSlotsState (the native slot list), readSaveSlots(mem, screen), date formats
include/gameui/SaveLoadScreen.h SaveLoadScreen: the modern Save / Load slot browser, SaveLoadAction / queue
include/gameui/TitleScreen.h  TitleScreen: the modern title menu (Options = PauseScreen's pages), TitleAction / queue
include/gameui/UnifiedOptions.h foldGameOptions: the game's seven options folded into the PC settings' Audio / Controls
src/Paint.h                   internal drawing helpers shared by all four screens (labels, focus / cell selection, lattice, crest,
                              ornate rule; InventoryScreen uses the free-function forms)
tests/gameui_test.cpp         unit tests over FakeGame.h (synthetic guest memory built from the documented layouts)
tests/gameui_dialog_test.cpp  help / confirmation reader, native focus, disabled choices, invalid widgets, output geometry
tests/gameui_screens_test.cpp strip / map / pause tests over ScreenFixture.h
tests/gameui_pause_test.cpp   pause phase 2 / 3: Options / Audio / Controller pages, sliders, cards, options reader
tests/gameui_settings_test.cpp SettingsPages over SettingsFixture.h (synthetic schema) and the PauseScreen hook
tests/gameui_saveload_test.cpp save / load screen, slot reader, title menu, PauseScreen's embedded root page
tests/gameui_preview.cpp      offline preview: dumps the screens' draw lists (3 aspects) as JSON lines
tests/render_drawlist.py      renders those to PNGs (Pillow; approximates GameUiRenderer, Georgia for Gelasio)
tests/DrawListJson.h          the JSON lines writer (also runtime/settings/tests/settings_pages_preview.cpp)
tests/gameui_ram_test.cpp     reader over the local RAM dumps (build/, never committed); exits 77 (skipped) without them
```

Build / test alone: `cmake -S runtime/gameui -B build/gameui -A x64`, `cmake --build build/gameui --config Release`,
`build/gameui/Release/gameui_test.exe`. In the tree it is part of the `runtime-tests` preset (`ctest -R gameui`).

## Data flow

```
EE thread (glue, phase 2)                                  present thread (overlay, phase 2)
  readSnapshot(mem, state, exchange.back())                  auto s = exchange.acquire();
  exchange.publish();                                        FrameOutput o = screen.frame(*s, input, viewport);
  while (auto a = actions.pop()) execute + tracker.begin()   for (a : o.actions) actions.push(a);
  if (auto r = tracker.update(snapshot)) results.push(*r)    while (auto r = results.pop()) screen.onResult(*r);
                                                             renderer draws o.draw (ImGui, later WebGPU)
```

* **Reader**: plain reads, never writes, never requests string loads. Guards: context and player present (player from
  the top screen when it is 0x15, else `*(*(ctx+0x4b0)+0xfc)+0x10`), riding Arokh (`ReadStatus::Dragon`, stats only),
  controller busy (`ctl+0` != 0: details not trusted). `res = *(ctx+0x320)` and the string manager `*(ctx+0x344)`
  are pointers (the RAM test caught the note's "`ctx+0x320`" shorthand). Strings are read only when resident; the
  name falls back to line 0 of the cached details.
* **Details**: the nine native buffers describe the native selection only (the spell `ctl+0x4b4` when the pane is
  Spells, the stock item on the shop pane, else cell `ctl+0x20`). `ReaderState` caches them per item handle / spell so
  every item shown keeps its last lines (comparison needs the equipped item's). A name mismatch is treated as stale.
  The screen steers the native selection to the focused item with `SelectItem` (once per handle) to fill them.
* **Categories** (no game field): `categoryFor(class, type)` from the note's class table; unknown type 10 counts as
  Melee, type 11 as Consumable. Filters: All, Weapons (melee, bow, ammo), Armour, Consumables (incl. scrolls), Other.
* **ModelExchange**: two shared buffers; `publish()` swaps and recycles the old front unless a reader still holds it.
  Readers keep an immutable `shared_ptr<const Snapshot>` for the frame.

## Actions and verification

Native refusals are silent, so every action has an `Expectation` built at execution time from the snapshot before
(`makeExpectation`) and checked against each later snapshot (`evaluate`): `Confirmed` when the effect shows, `Refused`
after the time-out (`defaultTimeout`, in snapshots), `Aborted` when the snapshot becomes invalid or the screen closes.

| Action | Native route (note section 5) | Effect watched | Time-out |
| --- | --- | --- | --- |
| SelectItem(cell) | arrows / `33eaf8` substitution | `ctl+0x20 == cell`, controller idle | 15 |
| UseOrEquip(cell) | Square, pane 0 (`33f630`) | equipped flag or quantity changed, item gone, or a box opened | 30 |
| Drop(cell) | Triangle (`33f2a0`) | handle left the grid (or a box opened) | 30 |
| ToggleHotSlot(cell) | Cross, pane 0, not in a shop (`341ce8`) | the record's `+4` flag flipped | 15 |
| SelectSpell(i) / CastOrSummon(i) | `3422c8(ctl, i, 0/1)` | selected == i / summon state, mana or screen changed | 15 / 30 |
| Spend / ReturnSkillPoint(row) | Skills Right / Left (`445898`) | points moved by -1 / +1 or the row's level changed | 30 |
| ShopBrowse(dir), Buy, Sell(cell) | pane 2 arrows, Square (`341268`), Circle (`33f520`) | selection / stock moved; a Yes / No box opened or gold changed | 15 / 30 |
| SwitchSection(n) | R1 / L1 (`4309d8`) | `screen+0x154 == n` | 15 |
| Close | SELECT path | `S+0x34 == 0` | 90 |

`ActionQueue` coalesces consecutive `SelectItem`s. Items carry their handle: the executor should refuse when the cell
no longer holds it. One action in flight at a time (`ActionTracker`); native Yes / No boxes are answered natively.

### Feedback sounds

Every frame output carries one `UiSound`: `Move` (focus moved), `Page` (a page / section / tab flipped), `Confirm`
(a row, button or card activated, a page or card opened), `Back` (a page or card left, Back / Resume), `Slider` /
`Toggle` (a value stepped / switched). The glue plays them through the game's own UI sounds (UI entry properties
`0x2a` tick, `0x24` click, `0x2d` back, `0x27` / `0x28` slider / check box, as a guest invocation of the EE thread at
the next PadMgr update) and skips a frame whose actions it carries out with a native widget press or tap (the widget
or START plays its own sound then), so each action is heard once. Mapping and the native-sound list:
`drakan_settings_host.cpp` (`hostUiSound`, `nativeSound`); the Tab screen, strip and map play their Move / Page ticks
from their own screen draw.

## Screen

Design canvas 1920 x 1080 mapped into the largest centred 16:9 area (`computeFrame`; pillarboxed on 21:9 / 32:9,
letterboxed on 4:3), header / footer bands and the backdrop span the whole viewport. Pages: **Merchant** (only in a
shop), **Inventory** (Rynn's live column on the left as a `WorldView`; in the centre, inside the native 4:3 window
where the glue places the native 3D icons under the cells, filter tabs, the 8 x 3 grid with real footprints, the
equipped cards and the quick inventory; on the right the detail card with the large icon, stat rows, quantity bar
and comparison against the equipped item of the same kind), **Character**
(Rynn left as on the Inventory page; in the centre column, opaque over the native grid, the equipment cards with
icons and stat lines and each kind's pack items beside them, Right / Confirm to equip, a comparison table; attributes
right), **Spells** (focus selects natively, Confirm summons), **Skills** (level pips, spent-this-visit pips lighter,
+ / - buttons; values are the Skills section's working copies), **Journal** (Primary / Optional lists, parchment
page), **Map** (the native map image cropped by `ImageRef` into the panel, the focused marker ringed when
`ScreenConfig::mapImageW/H` is known, locations list).

Navigation is one model for all devices: every page publishes `HitTarget`s; directional input picks the nearest
navigable target in that direction (`pickDirectional`), so grids, lists and cards need no per-page code. The pointer
hovers (focus follows), single click on tabs / buttons / spells / hints, double click on grid items and cards, right
click closes (native parity). Recommended glue mapping:

| UiKey | Keyboard | Gamepad |
| --- | --- | --- |
| Up / Down / Left / Right | arrows, WASD | D-pad, left stick |
| Confirm (equip / use / select / spend / buy / sell) | Enter, Space | A / Cross |
| Back / Close | Esc, Backspace / Tab, I | B / Circle / Select |
| Secondary (drop / cast / return point) | X, Delete | Y / Triangle |
| Tertiary (quick slot) | F | X / Square |
| PagePrev / PageNext | Q / E | LB / RB |
| FilterPrev / FilterNext (shop: browse) | Z / C | LT / RT |

Skills: Left / Right on a row return / spend a point like the native page. The footer hints are clickable targets.

**Look.** `Theme` (`Layout.h`) carries the Order of the Flame palette (red-brown cells, bronze lattice, gilt, the red
`select` trace, ivory, parchment text); every screen draws through it and `src/Paint.h`, so a style change is made once.
`ImageKind::SkinCrest` (the gem-and-crescent ornament), `UiIcon` (`UiIconId`: the game's inventory icon atlas) and
`PageIcon` (a page's native section icon) are resolved by the renderer from the disc's art; `DrawCmd::tracking`
letter-spaces section labels. Details: `docs/reference/mouse-menus.md` section 13 ("Drakan look").

**Drawing contract** (`Draw.h`): commands in back-buffer pixels, colours `0xRRGGBBAA` straight alpha. `Panel` is a
semantic style (stone + brass frame, inset slot, card, parchment, focus) so a skinned renderer can use `GameSkin` and
a plain one fills with the tint. `Image` refers to an `ImageRef {kind, index}` (`handle()` packs it into a `uint64_t`):
item icon crops of the hidden native page by cell, category / page / spell icons, prompt glyphs per device (the
direction prompts `Navigate` = up / down and `Adjust` = left / right are drawn as arrow shapes: the vendored Gelasio has
no arrow glyphs), the map
background per region, skin art. Unknown images draw nothing; an image command may carry a `fallback` (`DrawList::imageOr`)
drawn only when the renderer has no picture for it: item icons fall back to their category silhouette. `WorldView` marks the rectangle that must stay clear for the live world. Text is single
line, clipped to its rect and vertically centred; the screen ellipsizes and wraps with `ScreenConfig::measure` (real
font metrics from the renderer; the default estimates 0.5 em per glyph).

## Strip, map and pause screens

Three more host screens share `HitTarget` / `pickDirectional` navigation, `Hint`s, `FrameOutput` and the drawing
contract with `InventoryScreen`, and draw with its panels, focus, bars and fonts (`src/Paint.h`). Preview them
offline: `gameui_preview build/gameui-preview` then `python runtime/gameui/tests/render_drawlist.py build/gameui-preview`.

**StripScreen** (the compact "I" inventory). Order of the Flame's placement, the modern style: one plate (8 x 3 grid +
item card) centred at the bottom (before 2026-10-25 stage 2 the grid alone was centred, which put the plate's centre
184 design units right of the screen's), the grid flush with the bottom (cell 60 design units = 0.056 H; the canvas is scaled by the viewport height, narrowed only below
4:3, centred, its bottom on the viewport's bottom) on one opaque plate with a prompt row above the grid (Equip / Use,
Drop, Quick Slot, Close) and the item card right of it (icon, name, kind, stat lines, quantity bar). The plate is
opaque because the native grid lies under it at about the Tab screen's scale (`Layout::nativeGrid`): the game skips
small item meshes whose textures are not resident (`docs/reference/mouse-menus.md` section 12). No backdrop, only a low gradient:
the world stays visible. Cells carry the Tab grid's marks (equipped E + brass outline, quick-slot number, stack count,
durability strip) and icons through `ImageRef{ItemIcon, slot}` with the category fallback. Glue: feed `frame(snapshot,
input, viewport)`, push `o.actions` (SelectItem steers, UseOrEquip, Drop, ToggleHotSlot, Close) into the
`ActionQueue`, results back through `onResult`; `layoutFor(vp)` / `layout()` give the pixel rects of every cell
(`cell(n)`, `item(origin, w, h)`), `nativeGrid` (where to place the native 3D icons), `plate`, `bar`, `card`, `hints`,
`bounds()`. Input as the
Tab screen: arrows / stick move, Confirm uses, Secondary drops, Tertiary quick slot, Back / Close / right click close,
hover focuses, double click uses.

**MapScreen** (the "M" view). The map image stays native; this screen draws an objectives panel docked to the viewport's
right edge (`panelRect()` / `panelRectFor(vp)`, so the glue can fit the map left of it) and a ring with a name tag on
the selected record's marker. Panel: the location (`MapInput::location`, else `locationName(snapshot)`), Main and
Optional objectives (`orderedObjectives`: open first; completed ones dimmed and struck through), Places (enabled
markers that are not journal entries). Objectives and markers are linked by quest record: focusing a row rings its
marker, focusing a marker (pointer, Left from the list, Confirm on a row) highlights its row; `selectedQuest()` is that
record. The glue passes the visible markers' screen centres in `MapInput::markers` (`markerToScreen(q, imageRect,
imageW, imageH)` converts `mapX / mapY`). Back / Close / right click set `wantsClose` (no UiAction). Missing strings:
`objectiveLabel` falls back text -> caption -> region name -> location name; uncaptioned places read "Location n".

**PauseScreen** (Esc, phase 1 of `docs/journal/2026-10-25/modern-pause-menu.md`). Backdrop + header ("Paused"), the
menu column (Resume, Save Game, Load Game, Options, Quit; disabled rows greyed, "Unavailable", skipped by navigation,
the focus moves off a row that becomes disabled; a one-line description of the focused row) and the status strip
(location, up to three open objectives, Rynn's level, health, mana, gold). Glue: fill `PauseState` (`canSave`,
`canLoad` from `432518`'s states, optional `location`), push `o.actions` into a `PauseActionQueue` and execute them
(Resume: START tap; OpenSave / OpenLoad / OpenOptions / Quit: focus the native row + Cross). After a row that opens a
native child the screen is `suspended()` (input ignored) until `reactivate()` (pause message 4) or `suspendFrames`.
Back / Close / right click = Resume on the root page (`wantsResume`, the screen closes). `PauseConfig::hostOptions`
(phase 2 structure) turns Options into a host page (Controller, Audio, PC Settings, Default Settings, Back) whose
entries emit OpenController / OpenAudio / OpenPcSettings / DefaultSettings.

**PauseScreen phase 2 / 3** (`PauseConfig::hostPages`, `PauseConfig::quitCard`; both default off, so phase 1 is
unchanged). Pages: **Options** (Controller, Audio, PC Settings, Default Settings, Back), **Audio** (Master Volume and
Music Volume sliders 0..100 with the value as text, Show Subtitles and Stereo Sound toggles, Back), **Controller**
(Control Scheme selector A / B / C, Vibration, Reverse Pitch, Key Bindings, Back), the **Default Settings** confirm card
(Restore Defaults / Cancel, Cancel focused) and the **Quit** card (Quit to Title / Quit to Desktop / Cancel). A card is
drawn over the page it came from. Back (Esc, B, right click, the Back row, Cancel, the footer prompt) pops one level of
a page stack (`pageStack()`) and restores that page's focus; Back on the root resumes. The header shows the page title
and a breadcrumb. Input: Up / Down / wheel move; on a slider Left / Right step by `sliderStep` (5), clicking the track
jumps to the pointer and starts a drag that follows the pointer while `UiInput::leftDown` is set (anywhere on screen;
a new press or the release ends it), clicking the label only focuses; toggles flip on Confirm / click / Left / Right; the
scheme selector steps with Left / Right (wrapping), Confirm or a click on the row steps on, a click on its "<" steps
back. Values: `PauseState::options` (`readPauseOptions(mem, st.options)` each snapshot; out-of-range values make it
`valid = false`, which greys every value row out, shows "--" and moves the focus off them). A value set from the screen
is shown at once and kept until the guest reports it or `optionHoldFrames` (120) pass (`optionValue()`). Glue input
change: set `UiInput::leftDown` (left button held) for slider drags; without it a click still positions the slider.

### Pause menu actions: what the glue executes

Actions arrive in a `PauseActionQueue` (FIFO; a Set* pushed right behind the same Set* replaces its value, so a drag
leaves one pending value). Execute one at a time on the EE side, never as nested guest calls from the host path
(note section 2 / 7: posted guest calls only for the slider apply, taps for buttons). Screen ids, widget ids and the
functions are in `addr::gui`; the option fields in `addr::options`. "Tap" = focus the native widget (`2be528(screen,
widget)` or Up / Down taps until `+0x84`) and inject a Cross tap (route 1); "confirm" = what to read back before taking
the next action (time-out as phase 1, 120 frames). With `hostPages` the native Options / Audio / Controller screens are
**host owned while hidden**: keep their page hidden (the `2bd1b0` wrapper with `+0x08 = 0`, children `+0x3c = 0` and
cursor `+0x40 = 0`, as for the pause), keep host-screen input on and do not treat them as "a native child is on top"
(no fade, no `reactivate`). "Tap" in the glue: focus with `2be528` (a plain setter, called nested) and inject Cross
once the focus has held two frames. Implemented in `ps2xRuntime/src/lib/drakan_pause_screen.inl`
(`pauseExecuteStage2`); live results in `docs/journal/2026-10-25/modern-pause-stage2.md`.

| Action (`value`) | Glue executes | Confirm |
| --- | --- | --- |
| `EnterPage` (`Options`) | On the pause (0x17): tap `0x2313` -> screen 9 opens; hide it | active screen = 9 |
| `EnterPage` (`Audio`) | On screen 9: tap `0x2342` -> screen 13 opens; hide it | active screen = 13 |
| `EnterPage` (`Controller`) | On screen 9: tap `0x2341` -> screen 10 opens; hide it | active screen = 10 |
| `LeavePage` (`Audio` / `Controller`) | START tap on 13 / 10 (key 1 = back) | active screen = 9 |
| `LeavePage` (`Options`) | START tap on 9 (or tap `0x2346` Back) | active screen = pause (0x17) |
| `SetMasterVolume` (0..100) | Screen 13 = `*(0x4fb610 + 4*13)`, widget `0x2351`: call `2c6c80((v + 0.25) / 100, widget)` (pure write: clamps to `+0x7c..+0x80` = 0..1, marks dirty; `441440` stores `int(value * 100)`, so the quarter keeps the truncation on v), then **post** `2bcf80(screen13, 0x2351, 1)` through `IGuest::post` so `441440` applies it on its own guest call (sound mgr `vt+0xa4(v, -1)`; may wait for a sound resource, `audio-slider-freeze.md`) | `*0x4fb75c == v` |
| `SetMusicVolume` (0..100) | Same with widget `0x2352` (apply `vt+0xa4(v, 2)`) | `*0x4fb760 == v` |
| `SetSubtitles` (0 / 1) | If `*0x4fb740 != v`: tap `0x2353` on screen 13 (the press flips the check box, flag `0x80`; message 1 stores the flag into the field: `441440` reads `widget+0x10 >> 7 & 1`, verified live, stored 2 frames after the press) | `*0x4fb740 == v` |
| `SetStereo` (0 / 1) | If `*0x4fb744 != v`: tap `0x2354` on screen 13 (apply `vt+0xc4()`) | `*0x4fb744 == v` |
| `SetControllerScheme` (0..2) | Tap `0x2367` on screen 10 `(v - cur + 3) % 3` times, re-reading `*0x4fb9b8` after each tap (the button cycles 0 -> 1 -> 2 -> 0 and runs `Options_ApplyButtonScheme`, the stick scheme and the axis mapping) | `*0x4fb9b8 == v` |
| `SetVibration` (0 / 1) | If `*0x4fb73c != v`: tap `0x2368` on screen 10 (applied through `ctx+0x358 -> +0x434 vt+0x124`) | `*0x4fb73c == v` |
| `SetReversePitch` (0 / 1) | If `*0x4fb734 != v`: tap `0x2369` on screen 10 | `*0x4fb734 == v` |
| `RestoreDefaults` | On screen 9: clear the screen's "asked" flag `screen9+0x9c`, then tap `0x2344`: `4424e0` message 1 then restores at once (`442328`, `441198`, `445380`) instead of opening the Yes / No box. (The box's Yes only clears that flag and arms a timer `+0x48` whose message 9, sent from the screen's draw, repeats message 1: the apply would run inside the hidden draw, which runs without preemption.) | the seven fields = 100 / 80 / 1 / 1 / 0 / 1 / 0 |
| `OpenKeyBindings` | Without `pcSettingsPages` (else the modern pages open, see below): open the F1 overlay on its Controls page (`drakan_settings_host`); `reactivate()` when it closes | overlay open |
| `OpenPcSettings` | Without `pcSettingsPages`: `drakan_settings_host::requestOpen()` (unchanged); `reactivate()` when it closes | overlay open |
| `QuitToTitle` | Set a one-shot flag that suppresses the `quit_exits` box -> title conversion, tap `0x2315` on the pause -> box (owned, hidden), tap Yes `0x238b` (the pause tears the level down and opens the title, screen 8). Quit is on the root page, so no Options page is open | active screen = title (8); unbind (the manager's pause slot keeps the old object, so the glue unbinds on the title) |
| `QuitToDesktop` | `drakan_win::requestClose()` (as F1 > Quit to Desktop) | window closing |
| `DefaultSettings` / `OpenController` / `OpenAudio` | Only without `hostPages`: the native screens shown (phase 1 behaviour) | as phase 1 |

Order guarantees from the screen: an `EnterPage` precedes every Set* of that page, and the matching `LeavePage` follows
them; Set* actions never arrive while the page they belong to is closed. A Set* the game did not carry out is not
retried: the screen's shown value falls back to the guest's after `optionHoldFrames`. `RestoreDefaults` clears the
screen's held values, so the next snapshot's `PauseState::options` shows the defaults. `QuitToTitle`, `QuitToDesktop`,
`OpenKeyBindings` and `OpenPcSettings` suspend the screen (`suspended()`) like the phase 1 native children. If the
modern pause closes while hidden Options screens are open (Resume is only reachable from the root, so this means the
watchdog or an external close), close them with START taps from the top down before resuming.

**Model / reader additions**: `QuestEntry::textResident / captionResident / done()`, `Snapshot::mapRegion` (native map
section +0x70, range checked, -1 outside screen 0x15), quests and region names read whenever the context exists (also
on the dragon), helpers `currentRegion`, `regionName`, `locationName`, `orderedObjectives`, `objectiveLabel`.
`TargetKind::MenuRow` was added for the pause rows.

**In the game (2026-10-25).** The glue binds `StripScreen` / `MapScreen` to the I / M views and `PauseScreen` to the
pause screen 0x17; the overlay draws whichever is bound through one screen layer. Details, measurements and the native
side: `docs/reference/mouse-menus.md` sections 12-14, `docs/journal/2026-10-25/modern-views-integration.md`.
`ActionTracker::begin(a, s, framesPerTick)` stretches the snapshot-counted time-outs when the game takes more than ~60
snapshots per second.

## PC Settings pages (modern)

When the modern pause menu is on, **PC Settings opens as pages inside it** (`PauseConfig::pcSettingsPages` and a schema
in `PauseState::pcSettings`) instead of the F1 ImGui overlay. The overlay stays as it is for the traditional / native
menus, and **F1 still opens the overlay everywhere** (also over the modern pause menu): it is the one settings UI that
works in every state (gameplay, the native menus), so F1 keeps one meaning. Previews: `settings_pages_preview <dir>`
(the real schema, `runtime/settings`) and `gameui_preview` (`pcsettings_*`, synthetic), rendered with
`render_drawlist.py`; notes in `docs/journal/2026-10-25/modern-pc-settings.md`.

**One schema.** `SettingsSchema` (`SettingsSchema.h`) is plain data: pages (`display`, `audio`, `controls`, `gameplay`)
of sections of `SettingRow`s (`key`, `kind` Toggle / Choice / Slider / Info / Size, label, help, `unavailable` / `needs`,
`enabled`, `restart`, `confirm`, `currentValue`, choices with labels and enabled flags, slider range / step / format /
log scale, `scope` Both / OverlayOnly / ModernOnly) and the two key binding layers (`BindingLayerView`: groups, actions,
slots with display name, conflicts, reserved reason). `dsettings::buildSettingsSchema` (`runtime/settings/Schema.h`)
builds it from the settings and the host's capabilities; the F1 overlay draws its Display, Audio, Controls (mouse and
gamepad) and Gameplay rows from the same schema (`SettingsMenu::drawSchemaPage`), so a row added there appears in both
(`settings_schema_test` fails when a field of the store has no row).

**Pages.** A landing page in the pause menu's column (Display, Audio, Controls, Gameplay, Key Bindings, Back) with the
help panel where the pause menu has its status strip; a category page is a wide panel with page tabs (the categories
plus Gameplay keys / Menu keys), section headings and rows; the list scrolls to keep the focus (and its section heading)
in view, the wheel scrolls without moving the focus, a scroll bar shows the position. The help panel explains the
focused row (its help, "Unavailable: ..." with the reason, "* Takes effect after a restart", "You can keep or revert the
change") and its footer lists the settings that wait for a restart. Rows: toggles (Confirm / click / Left / Right),
choices as `<  value  >` (Left / Right step over the enabled choices, clicking an arrow steps, Confirm or a click on the
value opens a list card, up to 8 visible, scrolling), sliders (Left / Right step, a click on the track jumps there and
drags while `UiInput::leftDown`), info lines (not focusable). Q / E (LB / RB) switch tabs, Back goes to the landing page
and from there back to the menu that opened the pages (`wantsClose`). Opened onto one page (`open("game")`: the
Controller page's Key Bindings row) Back returns straight to the caller. Tertiary (F / Square) asks to restore the page's
defaults.

**Key bindings.** One row per action of the catalogue with three slot chips (the overlay's Primary / Secondary / Extra);
amber = the key is also used by another action, red = reserved by the port, `-` = empty. Confirm / click on a chip opens
the capture card ("Waiting for input...", Bind Esc / Clear / Cancel, gives up after 12 s); a key, a click in the box
(left / right button, armed only once the button that opened it was released) or the wheel binds; a key that another
action uses asks Replace / Use for Both / Cancel. Secondary (X / Delete) clears the focused slot, Tertiary restores the
layer's defaults. Unknown tokens of the file (the overlay's "Other bindings in the file") are not shown.

**Cards.** Keep display settings (rows with `confirm`: window mode, window size, render scale; 12 s countdown, Keep /
Revert; Back and the time-out revert), Restart required (from the glue's feedback; Restart Now / Later, focus on Later;
shown after Keep when both are due), Something did not work (the glue's error), Restore defaults (focus on Cancel).

### PC Settings glue contract

Everything runs on the thread that draws the overlay (the present thread, where `SettingsMenu` applies too):

```cpp
static dsettings::ModernSettings g_pcs(host, overlay->store());   // share the F1 overlay's host and store
// when the pause screen is created: cfg.pcSettingsPages = true;
// every frame, before PauseScreen::frame:
if (pause.pcSettingsOpen() && !g_pcsOpen) g_pcs.open();          // re-read the files when the pages open
g_pcsOpen = pause.pcSettingsOpen();
g_pcs.tick(now);                                                  // capability refresh (DLSS / ray tracing status)
st.pcSettings = &g_pcs.schema();
st.now = now;                                                     // seconds
st.capturedKey = pause.pcSettings().capturing() ? firstPressedKeyIniName() : "";   // "LCONTROL", "F", "MOUSE3" ...
PauseFrameOutput o = pause.frame(snapshot, st, input, vp);
g_pcs.applyAll(o.settings, pause.pcSettings());                   // apply each action at once, feed the result back
```

While `capturing()`, keyboard keys go to `capturedKey` (the `drakan_input.ini` source names, `dsettings::isKnownSource`)
and not to `UiInput::keys`; Esc and the pad's B stay `UiKey::Back` (cancel); the pad's D-pad / A still move on the card.
The left / right button and the wheel come through `UiInput`; the middle / side buttons as `capturedKey` "MOUSE3" ..
"MOUSE5". Call `g_pcs.open()` again after the F1 overlay was closed over the pages (it may have saved). Without a schema
the PC Settings / Key Bindings rows emit `OpenPcSettings` / `OpenKeyBindings` exactly as before.

| SettingsAction | `ModernSettings::apply` | Feedback |
| --- | --- | --- |
| `Change` (key, value) | `applySettingChange` on a copy of the applied settings (field keys through the store's field table, ini spelling; pseudo keys `video.window_size` "WxH", `ui.link_sensitivity`), then `applySettings`, the overlay's Apply: the host's `applyVideo / applyAudio / applySubtitles / applyGeneral` against the previous section (world rate never live), the minimal-diff save of `drakan.ini` / `drakan_input.ini`, `reloadBindings`, `regeneratePrompts` | restart keys (`HostCaps::restartKeys`, always `video.world_rate`) and a host `needsRestart` -> `restartReasons`; host / save errors -> `error` |
| `Change`, `final = false` (slider held) | live apply only, no save; the release (`final = true`) saves | as above |
| `Change`, `revert` (keep card Revert / time-out) | the same path with the previous value (what the overlay's `revertVideo` does) | |
| `Bind` (layer, action, slot, source, replace) | `bindSlot` (the overlay's capture commit; `replace` first removes the source from the other actions) + apply | |
| `ClearBinding` (layer, action, slot) | `clearSlot` + apply | |
| `ResetBindings` (layer) | `resetLayer` to `SettingsStore::defaults()`, keeping the file lines + apply | |
| `ResetPage` (page) | `resetPageDefaults` (the overlay's "Defaults for this page": the window size is kept, bindings untouched) + apply | |
| `Restart` (reasons) | `ISettingsHost::requestRestart(reasons)` | |

## Save / Load and title screens

Research: `docs/journal/2026-10-25/modern-save-load-title.md`. Selected by `[general] save_menu` / `title_menu`
(`runtime/settings`, default `native`). Wired in `ps2xRuntime/src/lib/drakan_menu_screens.inl` (EE) and
`drakan_settings_host.cpp` (overlay); reference `docs/reference/mouse-menus.md` section 15. Both screens share the drawing contract, the
`HitTarget` navigation and the prompts with the other screens; previews as above (`load_*`, `save_*`, `title_*`).

**SaveLoadScreen** (`open(mode, context, focusSlot)`, `frame(SaveSlotsState, input, viewport)`). The glue fills
`SaveSlotsState` each EE frame with `readSaveSlots(mem, *0x4fb60c, {page, pageCount, noSpaceCaption})` (page =
`drakan_menus`' pager page, the no-space caption = the string of property `0xfb`), sets `busy` while a native box, the
Saving screen 0x1a or a rescan is up, `inGame` when a level runs, `card` from `cardStateFromAnySave(37e810(0))` when
that says no card / unformatted, and `message` for an error line. Input: Up / Down / wheel move over the four cards,
Left / Right / PagePrev / PageNext (Q / E, LB / RB) and the page arrows change the page, Confirm / click act, Back /
Esc / right click leave (a card: cancel). Slot pictures are `ImageRef{SavePreview, n}`: the renderer loads the HD
companion `<save>.rsg.<fnv64>.preview.png` (`drakan_save_previews` naming) or the native 128 x 128 thumbnail (entry
`+0x164`); without either the crest shows. Actions arrive in a `SaveLoadActionQueue` (SelectSlot coalesces):

| Action (`value`) | Glue executes on the hidden screen 4 / 5 | Confirm |
| --- | --- | --- |
| `SelectSlot` (slot n) | `+0xa4 = (n - 1) % 4` (as the native hotspot hover) | `+0xa4` reads back |
| `ChangePage` (page p) | `drakan_menus::changePage` to p (folder cells + bit 3 of `+0x9c`) | `readSaveSlots` shows page p, count 4 |
| `ConfirmSave` (n, empty) | Cross tap (key `0x1c` -> `444460` -> `443ac0` writes) | screen 0x1a, then no screen (saved) or box `0x104` (failed: show it natively, `reactivate()` after) |
| `ConfirmOverwrite` (n, used) | the host card asked: arm what the overwrite box's Yes arms, without the box: `+0x44 = 0`, `+0x48 = 1` (the screen draw sends message 9 -> `443ac0`); clear `+0x48` once `+0x44` moved | as ConfirmSave |
| `ConfirmLoad` (n, used) | title: Cross tap (`43d028` -> `43b4d0`); in game (the host card asked): `+0x9c |= 0x10` (the load question's Yes: the next message 10 runs `43b4d0`) | screen 14 (loading); failure: message `0x105` and the title |
| `Back` | START tap (key 1); level change: the callback answers No | the parent (pause / title) is active again |

Posting `443ac0` / `43b4d0` as separate guest calls (`IGuest::post`) was rejected: the posted call runs on the HLE
dispatcher thread at priority 6, which preempts the GUI thread at any checkpoint, and both routines close screens and
free the slot list the GUI thread may be walking. Armed as above they run in the screen's own draw, as after the
native Yes; the glue's hidden draw of the slot screens is not a no-preempt section for that reason.
Every Confirm* is preceded by a SelectSlot when the native selection differs, and the screen is `suspended()` until
`reactivate()` (the slot screen is active again, e.g. after a failed save's box) or `suspendFrames`. From the frame of
a Confirm* the screen shows its progress card ("Loading..." / "Saving...", `progress()`), takes no input and keeps it
until `reactivate()`, `open()` or `close()`: the native load / save runs synchronously and the EE does not come back
for one to three seconds. `SaveSlotsState::loadingSlot` (the glue loads that slot on its own: the title's Continue)
shows the same card from the first frame, before the list is read. Hiding: keep the
grid widget `0x2331` drawable (its draw sends the screen's per-frame message 10), set `+0x08 = 0` and `+0x3c = 0` on
`0x2332` / `0x2333`, wrap `43bfb0` to run only its thumbnail requests, and turn the native pager off. Unbind when the
screen goes (a save closes every screen).

**TitleScreen** (`open()`, `frame(TitleState, input, viewport)`). `TitleState`: `canLoad` (native `0x2302` enabled),
`continueSlot` / `continueLocation` / `continueTime` (the newest save, e.g. the newest `newerThan` of the slots the
glue has seen or of the card folders; 0 = no Continue row), `canQuit` (`title_quit`), `options`
(`readPauseOptions`), `backdrop` (None = the native art shows: keep `0x2301` drawn and hide only the panel `+0x08`
and the five rows). Actions (`TitleActionQueue`); the screen is `suspended()` after each until `reactivate()`:

Continue shows a "Loading..." card in place of the menu column from the frame it is chosen (`loading()`) until
`reactivate()` / `open()`.

| Action | Glue executes on the title (8) | Confirm |
| --- | --- | --- |
| `Continue` (slot n) | as LoadGame, then SelectSlot n (page change first if needed) and ConfirmLoad n | screen 14 |
| `LoadGame` | focus `0x2302` + Cross -> screen 4 (modern: bind SaveLoadScreen(Load, Title) over it, hidden) | active screen 4 |
| `NewGame` | focus `0x2304` + Cross (first level, loading 0xe, intro movie) | screen 0xe; unbind |
| `Credits` | focus `0x2306` + Cross -> 0x13 shown natively | 0x13; `reactivate()` when 8 is active again |
| `OpenPcSettings` | `drakan_settings_host::requestOpen()` | overlay open; `reactivate()` on close |
| `QuitToDesktop` | `drakan_win::requestClose()` (the host Quit card asked) | window closing |

`TitleFrameOutput::optionActions` are PauseActions from the Options pages (the pause menu's own component, embedded
with `PauseConfig::rootPage = Options`, centred, no status strip): execute them as in "Pause menu actions" with one
difference: `EnterPage` / `LeavePage` (`Options`) tap `0x2305` on the title / START on screen 9 (back to 8) instead of
`0x2313` on the pause. Audio / Controller, Set*, Restore Defaults, Key Bindings and PC Settings are unchanged.

## One Options screen (`PauseConfig::unifiedOptions`)

With `pcSettingsPages`, `unifiedOptions` and a schema, the modern pause menu's and the title's **Options** row opens the
SettingsPages (titled "Options") instead of the Options / Audio / Controller pages, and there is no PC Settings row
(`TitleConfig::unifiedOptions` drops the title's). `foldGameOptions(pc, options, out)` (`UnifiedOptions.h`) inserts the
game's options as rows with keys `game.<option>`: Audio gets "Game sound" (Game volume, Music volume, Show subtitles,
Stereo sound; the PC master volume is labelled "Output volume"), Controls gets "Game controller" (Control scheme,
Vibration, Reverse pitch). PauseScreen keeps them out of `PauseFrameOutput::settings` and emits the stage 2 actions
instead, so the glue executes them as in "Pause menu actions":

| Pages event | PauseActions |
| --- | --- |
| Options opened (pause root) | `EnterPage(Options)` (the title emits it itself, as before) |
| Audio / Controls page shown, left (tab, Back) | `EnterPage(Audio / Controller)`, `LeavePage(...)` |
| a `game.*` Change | `Set*` (one per option and frame; a drag queues its last value) |
| `ResetPage` of Audio / Controls | the ResetPage (to ModernSettings) and `Set*` of that page's game defaults (`gameOptionDefault`) |
| Back from the landing page | `LeavePage` of the open sub page, then `LeavePage(Options)` (pause) / `wantsBack` (title) |

Every other SettingsAction goes to `dsettings::ModernSettings` as before. Tests: `gameui_settings_test` "unified".
