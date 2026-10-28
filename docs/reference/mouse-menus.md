# Mouse on the game's own menus, and the PC Settings overlay over a running game

Status: implemented on branch `drakan-mouse` of `tools/PS2Recomp` (worktree `tools/PS2Recomp-mouse`, build `build/recomp_mouse/`), library code in
`runtime/input` (`MenuMouse`) and `runtime/settings` (cursor layer, open interceptor). Reverse engineering with the Ghidra decompile
(`build/recomp_ui/alldecomp/*.c`) and live memory peeks of a running copy of the merged exe. Addresses are SCES_500.06.

## 1. Result of the research (short)

* **The game ships a complete PC mouse path for its GUI.** The old finding "menus ignore the cursor" was true only because nothing ever called it:
  `FUN_002cc7c0(type, buttonId, x, y)` is registered as the device's second callback (`Input_RegisterGameCallbacks` 0x243e88, vslot +0xf0, device `+0x414`)
  but no PS2 code path invokes it (the DirectInput mouse driver was stripped; the earlier test with `InputDev_SetCursorPos` only moved a variable nobody reads).
  It dispatches to `FUN_002bc618` (hover / drag hit test) and `FUN_002bc428` (button press / release hit test), which walk the widget list of the active screen.
* What the PC code does **not** have: hover does not move the highlight (the focus, flag `0x40`, only changes on key navigation or on a button press). We add
  that (same focus change as the D-pad, same tick sound).
* Item rectangles are **exact**: every widget carries `s16 x, y, w, h` in the GUI coordinate space (the size is in `*(0x5565c8)+0xf8/+0xfc`, **512 x 512**; the
  whole display image maps to it, verified: title row "Load Game" y 191..223 = pixel 360..430 of a 960 px high window). Positions are written by the layout loader
  (`FUN_002bb518`) and re-written by the screen handlers (the Options handler sets the y of its six rows on message 3), so the **runtime values** are read.
* The confirm sound of an activated button is **played by the widget** on press (property `0x24`), not by the screen handler; the "PC Settings" hook skipped only the
  screen switch. What silenced it was the EE hold + SPU2 pause of the overlay (section 6), which this change removes.

## 2. GUI object model (reverse engineered)

Screen manager at `0x4fb600`, active screen pointer variable **`0x4fb60c`** (`0` = no screen). A screen is the "window" object built by `FUN_002bb518(this, layoutId, parent)`.

| Screen object field | Meaning |
|---|---|
| `+0x18` | screen id (8 title, 9 options, 10 controller, 13 audio, 0x17 pause, 1/2/3/0x1b boxes, 4 load, 5 save ...) |
| `+0x1c/+0x20/+0x24/+0x28` | s32 x, y, w, h of the screen (0,0,512,512) |
| `+0x34` | parent screen (a screen stack; the title menu stays below Options) |
| `+0x7c` / `+0x80` | first / last child widget |
| `+0x84` | **focused widget** (0 = none) |
| `+0x88` | hovered widget (written by the native mouse code only) |
| `+0x98` | vtable pointer of the screen's event interface: slot `+0x2c` = `handler(this, widget, message)` (`this` adjusted by the s16 at `+0x28`) |

| Widget object field | Meaning |
|---|---|
| `+0x00` / `+0x04` | previous / next sibling (children are appended at the end, `FUN_002be310`) |
| `+0x08` | parent screen |
| `+0x0c` | s16 class: 1 label, 3 button, 4/5 selector items, 9 slider, 10 image (layout type 3 button, 8 slider, 6 label, 9 image) |
| `+0x10` | flags: `0x40` **focused / highlighted**, `0x80` pressed, `0x100` member of a group (follows another widget), `0x200` visible |
| `+0x14` | u16 widget id (layout id: 0x2302 Load Game ... 0x2345 "PC Settings") |
| `+0x1c,+0x1e,+0x20,+0x22` | **s16 x, y, w, h** in GUI units (512 x 512) |
| `+0x3c`, `+0x40` | both must be non-zero for the widget to take part in input (Quit on the PC title menu has 0 / 0) |
| `+0x50` | dirty flag (set to 1 after a highlight change) |
| `+0x54` | vtable pointer; records are `{s16 adj, fn}` at `slot-4 / slot`: `+0x2c` mouse button handler `(flags, x, y)` (3 = press, 2 = release), `+0x34` mouse move / drag `(flags, x, y)`, `+0x3c` key handler, `+0x74` **can take focus** (button / slider / selector 1, label / image 0), `+0x84` group head |

Native sequences (all verified in the decompile, `FUN_002bc428`, `FUN_002bc618`, `FUN_002bf568` (button), `FUN_002c59e0` (slider), `FUN_002bc8c0` (keys)):

* **D-pad Up / Down** (`FUN_002bc8c0` via the GUI key events `0xc8 / 0xd0`): `FUN_002bcc38` / `FUN_002bcdc8` pick the next focusable widget (`+0x74`, `+0x3c`, `+0x40`, flag `0x100` clear), call the screen
  handler with message `0xc` ("focus leaves widget"), clear `0x40` on the old widget, set it on the new one, set both dirty, then play the UI sound **property `0x2a` (down) / `0x2b` (up)**.
* **Button press** (widget `+0x2c`, state 3): sound property `0x24`, flag `0x80`. **Release** (state 2): clears `0x80` and sends **message 1 to the screen handler** (this is the activation:
  `FUN_002bcf80(parent, widget, 1)`). Enter / Cross do exactly press + release (`FUN_002c0cf0`).
* **Slider** (`+0x2c` state 3 = press: moves the thumb or grabs it, sounds `0x27` / `0x28`; `+0x34` = drag while grabbed; state 2 = release).
  A press left / right of the thumb only **pages** by `+0x88`; it grabs (and then follows the pointer) only when the press lands on the thumb. Fields: `+0x7c` min,
  `+0x80` max, `+0x84` value, `+0x88` page step (floats); skin metrics `0x4fb408` / `0x4fb438` (track margins), `0x4fb3f8` (minimum thumb length).
* UI sound: `entry = FUN_002bec48()`; `obj = *(entry+0xc)`; `idp = obj.vt(+0x1c)(property, -1)`; `snd = *(ctx+0x32c)` (`ctx = *0x5565d4`); `snd.vt(+0x114)(f12 = 0.0, id = *idp, 0x4e8c28, 4)`.

## 3. Design (`runtime/input/src/MenuMouse.cpp`)

Pure guest-memory logic over `IGuest` (portable: nothing Win32 / PS2Recomp specific), called once per game frame from `InputModule::afterPadMgrUpdate` while the menu layer is active.

1. **Coordinates.** The host gives the cursor in client pixels and the rectangle of the game image inside the client area (`MenuView`). gsr reports the rectangle of its present viewport
   (letterbox / pillarbox / forced aspect / stretch) in the overlay callback (`OverlayContext::viewX/Y/W/H`, back buffer pixels); `drakan_settings_host.cpp` scales it to client pixels
   (DPI: back buffer != client) and passes it to `InputModule::setMenuView`. `gui = (client - view.xy) / view.wh * (512, 512)`; outside the image = no hit.
2. **Hit test** like the game's (`FUN_002bc618`): children from the last one backwards, `x <= px < x+w`, `y < py < y+h`, `+0x3c` and `+0x40` non-zero, **and focusable** (`+0x74`): the full-screen
   image widget (class 10, `+0x3c = +0x40 = 1`) would otherwise swallow every click and the native code focuses it.
3. **Hover** (only when the mouse actually moved, so a resting cursor never steals the highlight from keyboard / pad users): if the widget under the cursor is not the focus, run the focus change
   (screen handler message `0xc`, flags `0x40`, dirty) and play the tick sound (property `0x2a`).
4. **Left click (guest calls run in order, not nested):** widgets other than buttons (sliders, check boxes, selectors) may wait for a sound resource, which a nested hook call cannot survive (see
   `docs/journal/2026-10-22/audio-slider-freeze.md`), so their press / drag / release are posted as their own guest calls, one at a time (`IGuest::post`, `MenuMouse::queueOp` / `pumpOps`), and `*0x4fb19c`
   is cleared after the release ran. A slider click presses on the thumb centre and drags to the click, so the value lands under the cursor. Details: press on the hit widget = focus + `widget +0x2c(3, x, y)` and `*0x4fb19c = widget` (the game's "mouse owner"); drag = `widget +0x34(3, x, y)`; release = `+0x2c(2, x, y)` (activation message 1) - unless
   it is a button and the cursor left it (cancel: clear `0x80`). A click on a screen without any focusable widget ("Press X" pages) is a Cross tap.
5. **Wheel:** one D-pad tap per frame: up / down moves the highlight (and scrolls lists); over a slider left / right changes the value.
6. **Right click** = Circle (back) through `[menu_bindings] MOUSE2 = CIRCLE`; Esc = Circle + Start, Backspace = Circle as before. **`MOUSE1 = CROSS` is removed from the menu layer** when `[menu] mouse = 1` (done in `Config::migrate`, also for existing ini files),
   otherwise a click would activate twice.
7. **Cursor drawing:** `SettingsOverlay::setCursorLayer` draws a brass arrow at the OS cursor position (`pointerInBackbuffer`) in the overlay pass, only while a menu is open, the mouse moved in the
   last `cursor_idle_ms` (5 s) and the overlay itself is closed. While it is shown the OS cursor is hidden over the window (`HostInput::setMenuCursorHidden`, the existing `WM_SETCURSOR` subclass).
   Keyboard / pad users therefore never see a cursor until they touch the mouse.
8. **Taps:** `InputModule::injectPadTap(bits, frames)` ORs pad bits into the next frames' sample (wheel, Cross for widget-less pages, START for the pause menu); they work while the input is suspended.

`drakan_input.ini` `[menu]`: `mouse = 1`, `draw_cursor = 1`, `cursor_idle_ms = 5000`; `[advanced]`: `addr_active_screen`, `addr_mouse_callback`, `addr_ui_entry`, `addr_captured_widget`.
Unit tests: `runtime/input/tests/input_test.cpp` `testMenuMouse` (fake screen with three widgets: mapping incl. pillarbox, hover focus + listener message + tick sound, press / release / cancel, wheel, slider wheel,
no screen, widget-less click) and `testMenuMouseModule` (suspension, taps, MOUSE1 migration): 593 checks.

## 4. Where it applies

| Screen | Notes |
|---|---|
| Title menu (8), Options (9), Controller (10), Audio (13), pause (0x17), boxes (1/2/3/0x1b) | widgets of class 3 / 5 / 9: hover + click + wheel verified (see section 7) |
| Load / Save (4 / 5) | list widget (class 12) handles its own rows in `+0x2c`; hover focuses the list, click goes to the widget |
| Inventory / map / shop (screen id 0x15) | one full-screen image widget, the sections are drawn by their own code in 3D: each section's items are captured at their native draw and hit-tested (Equipment section 10, Map, Spells / Skills / Journal / shop section 11); wheel = sections, right click = SELECT |
| Text page (0x10), credits (0x13), movies (0x1d / 0x1e), memory-card warning (0x1c) | no widgets: a left click continues (START, or Cross for the credits and the warning); the cursor is shown on 0x10 and 0x13 |

## 5. Sound: the "PC Settings" confirm

The Options hook replaces only the screen switch of message 1. The button's own press handler (`FUN_002bf568`, property `0x24`) already ran when the message was sent, exactly like for
Controller / Audio; what the player did not hear was the cut-off: the overlay used to hold the EE and pause the SPU2 immediately (the click voice was frozen, then the "stall-safety" muted it). With the new design
(section 6) the game keeps running, so the click and the music play. (If a build still lacks the sound the hook plays property `0x2d`, the generic "screen command" sound, through `MenuMouse::playUiSound`.)

## 6. Overlay over a running game

* `drakan_pause::overlayOpened()` decides when the overlay opens: **a GUI screen is active** (title, Options, pause ...) -> the EE is **not held**, the audio is **not paused**; the game keeps running its menu and its
  music; `InputModule::setSuspended(true)` makes the pad sample neutral and drops all key commands and the menu mouse (the keyboard / mouse belong to the overlay). No screen (cut scene, loading, FMV) -> the old behaviour
  (EE hold + audio pause) remains the fallback. The stall-safety mute (SPU2 heartbeat watchdog) is untouched: it only fires when the EE really stalls.
* **F1 during gameplay:** `SettingsOverlay::toggle()` asks the open interceptor; in gameplay (player camera running, no screen) the interceptor does not open but calls
  `drakan_pause::requestPauseThenOpen()`. The EE thread (PadMgr_Update hook) injects **START** for four frames (the game opens its own pause menu, screen `0x17`) and opens the overlay once that screen is the active one
  (3 s timeout -> open anyway with the EE held). Gameplay is paused by the game itself, music continues, nothing else is stopped.
* **Closing:** the pause menu stays open (the player resumes with Resume / Esc), or the game is resumed with a second START when `[general] resume_on_close = 1` (env `DRAKAN_RESUME_ON_CLOSE`).
* Opening from Options > "PC Settings" or F1 on the title menu: same path, no START needed.

## 7. Verification

Real input (SendInput mouse / keyboard into the topmost game window) + memory peeks of the screen object (`focus` = `+0x84`), build `build/recomp_mouse` (1280x960 and 1600x900 with `aspect = 16:9`):

* Title menu, Options, pause menu (0x17): hovering each row moves the highlight to exactly that row's widget (ids 0x2302..0x2306, 0x2341..0x2346, 0x2311..0x2315 read back); a left click on Options opened screen 9,
  a click on Resume (0x2314) resumed the game (screen -1); right click in Options returned to the title (8). In 16:9 the title is pillarboxed and Options still 4:3 at that moment: the mapping used gsr's reported viewport.
* **Right click / Backspace = Circle did NOT go back**: the GUI treats Circle as its R-MOUSE id (0x101); "back" is the Esc key id, which START produces. The menu layer now maps MOUSE2 and Backspace to `START | CIRCLE`
  (migrated into existing ini files), like Esc.
* The drawn cursor appears in screenshots (`mt1.png`: brass arrow over the Options menu), the OS cursor is hidden over the window.
* Options > PC Settings with a real click: the overlay opens, the game stays in screen 9 behind it (not held), keyboard / mouse inside the overlay are NOT seen by the game (focus unchanged after Down, Down, Enter).
  **Real mouse clicks and keyboard navigation work inside the F1 overlay** (clicked the Aspect ratio combo with the real mouse, Down, Down, Enter picked "21:9 ultrawide"); the earlier "posted keys did not work" report is a test-method
  artefact (posted messages carry no real cursor position / key state for ImGui's Win32 backend); use SendInput.
* Music during the overlay (`DRAKAN_AUDIO_DUMP`, RMS per second of the last 45 s of a title-menu run with the overlay open for ~8 s): 1.4k - 3.5k with no gap before, during or after (`mo2.wav`). The EE is not held any more.
* F1 in gameplay (New Game, Level 0 on foot, 1600x900 16:9): log `F1 in gameplay: pausing through the game's pause menu (START injected)`, 0.1 s later `gui screen -1 -> 23`, `pause menu is up, opening the settings over it`;
  F1 again closes the overlay and leaves the pause menu (screen 23).
* Not verified: `resume_on_close = 1`; the audible tick on hover (the sequence replicates the game's own inline sound code, no audible check); load / save slot lists (no save games); inventory (not a GUI screen).
* Widescreen: UI sprites are compressed about the image centre by gsr's `hudScaleX` while 3D scenes are wide; `OverlayContext::hudScaleX` is passed to `MenuView` and undone in `mapToGui` (clamped, rows are full width).
  Not exercised on a wide Options screen with sprites compressed (the Options backdrop was still 4:3 in the test).

## 8. Inventory and map (controls branch `drakan-ctl`, 2026-10-17)

Reverse engineered at run time (RAM dumps via `DRAKAN_INPUT_DEBUGKEYS=1`, F9 = whole guest RAM) and with the Ghidra decompile:

* Select (the `0x4030` command) opens screen id **0x15 (21)**: a normal GUI window (`FUN_00430b48` constructor, vtable `0x54ddc0`) whose only widget is the full-screen image `0x1000`. Nothing in it is a button / slider, so the native mouse path has nothing to hit.
  `+0x14c` = player, `+0x154` = **current section**, `+0x1a8` = array of the five section objects (count `+0x1b0`), each with its own vtable at `+0x18`: slot `0x1c` update, `0x24` draw, `0x3c` key handler, `0x4c` focus change.
  Sections: **0 Equipment, 1 Spells, 2 Skills, 3 Map, 4 Journal** (verified by their first-time help boxes). The screen handler is `FUN_00430d68(this, widget, msg)` (msg 1 activate, 3 close, 10 per-frame draw, 0xb key -> `FUN_004309d8`). `FUN_004309d8` switches sections on the DIK ids `0xd1` (PgDn = R1) / `0xd3` (Del = L1) and forwards every other key to the current section's slot `0x3c`.
* Equipment forwards keys to the player's inventory state machine (`player+0x584`, vtable at `player+0xa28`, key handler `FUN_00345d08`); Equipment help: arrows select, **Square use / equip, Triangle drop, Cross hot-slot**. The **map** section's key handler (`FUN_0043a588`) only cycles through the list of map regions (`DAT_004fbb70`, 0x20-byte entries, count `DAT_004fbb78`) with the four arrow keys: **the map has no pan and no zoom**, so there is nothing to drag or wheel.
* The slots are 3D objects (spinning icons, `Icon Slot Size`, `Up Axis for Inventory` ...) positioned in a scene that the engine rotates / projects itself; there are no 2D slot rectangles in memory. Hit testing would need the live camera projection of every item, and the inventory scene does not render completely in the current GS build (stale world behind a half-erased help box in `build/screenshots/r4_ctl_inv_*.png`; the first visit of each section shows a modal help box). So a true "hover the slot" is **not implemented**.
* What was implemented first (superseded on 2026-10-24 by section 11: pointer travel no longer steps the selection, `inventory_step` is ignored) (`MenuMouse::updateInventory`, config `[menu] inventory_mouse = 1`, `inventory_step = 36`, `inventory_click = square`):
  pointer travel is converted to D-pad taps (one step per 36 GUI units of travel along the dominant axis, accumulator forgotten after 350 ms of rest), **wheel up / down = L1 / R1** (previous / next section), **left click = Square on Equipment (use / equip) and Cross on the other sections**, **right click = SELECT**: START opens the pause menu over the inventory and Circle does nothing there, while Select closes it (verified: Tab twice, screen 21 -> -1). The module removes the right button from the pad layer while screen 21 is up so START | CIRCLE is not sent twice. The brass cursor is drawn there (menu context). The first-time help boxes (message box, screen 1) are ordinary widget screens: hover + click on OK work, a click beside OK does nothing (verified).
* Verified with real `SendInput` (`build/recomp_ctl/t/inv4.ps1`, log lines `[in] inventory section a -> b`): wheel down moved section 0 -> 1 (Spells help box), wheel up back to 0, pointer travel produced D-pad steps, the click was delivered as a tap.

## 9. Behaviour changes in this round

* A click that is not over a focusable widget does nothing (the "widget-less page = Cross tap" fallback of section 3.4 is gone).
* The cursor is drawn and the mouse released only in a *menu context*: screen ids 1 2 3 4 5 8 9 10 13 0x10 0x13 0x15 0x17 0x1b 0x1c (menus, boxes, text page, credits, inventory / map / shop), or while the F1 overlay is open. Cut scenes, loading screens (screen 14 / 30 ...) and gameplay use the menu *bindings* where the game has no camera but keep the mouse captured and show no cursor.


## 10. Equipment absolute mouse candidate (2026-10-17)

The relative adapter described in section8 did not hit-test Equipment cells:
clicking anywhere used the current selection. Equipment now reads the live
native grid projection from a hook at `437170`, while the UI camera is installed.
The normal world camera has already been restored by the input tick and cannot
be used to project this plane. The math matches `42ab78`, `13e638`, `42ac68`,
`13f498`; cell centres use section `+68/+6c`, spacing `+60/+64`, 8x3 cells.
The grid is 3D geometry, so HUD sprite compression is not undone for this path.

A mouse request resolves continuation cells to their origin (`340510`: marker
column*10+row). It emits ordinary native navigation; the `33eaf8` hook substitutes
the requested destination only on function entry, for the matching Equipment
controller/screen/item. Native selection still updates details/preview. This
avoids synchronous nested calls to potentially yielding native inventory code.
Equip is emitted only after `controller+20` matches. Empty/outside clicks do
nothing; stale projection, page changes, focus/overlay resets, item replacement
and a bounded timeout cancel pending activation. Missing native hooks/capture
fail closed. Wheel paging and right-click Select remain available.

Script cursors remain virtual640x512. InputModule scales them into the real host
client before applying MenuView's client-pixel viewport. Generic menu mapping
also rejects points outside the compressed HUD rather than clamping them onto
an edge. No user saves are modified by these changes.

CPU evidence: `combat_inventory_mouse_check.ps1`:26checks/0failures; production
input unit executable:641checks/0failures. MSVC `/Zs` accepts the scoped glue with
candidate input headers. These are CPU checks, not evidence of live selection
or rendered alignment. No game/GPU run was launched for this candidate.

Integration: cherry-pick the outer input/scripts/docs commit and matching tools
glue commit together. No regeneration or public PS2 ABI changes are required.
Main must build the paired candidate and use a private copy of existing saves.
Run `combat_inventory_mouse_calibrate.txt` using syncplay90, TAB100, ENTER230.
The input log prints the first projected grid bounds in native GUI units. For an
axis-aligned full-viewport image, generate a script with:

```powershell
python scripts/harness/combat_inventory_mouse_recipe.py build/t/inventory-click.txt --grid LEFT TOP RIGHT BOTTOM --pickup-bow
```

Use the existing `build/recomp_combat/encounter/fixture-bow-item` source with
`combat_encounter_run.ps1` (which makes private cards); the optional pickup steps
are the existing successful bow pickup recipe. Requires distinct occupied
cells0/1 and emptycell7. `--view X Y W H` specifies the displayed image rectangle
in virtual640x512 coordinates when pillarboxed; e.g. a real viewport is scaled
by640/clientWidth and512/clientHeight. Never substitute physical4K pixels for
script cursor values. If projected cells are rotated, this bounds-only recipe
generator is insufficient; MenuMouse itself hit-tests individual quadrilaterals.

Read-only RAM assertions:
`python scripts/harness/combat_inventory_mouse_ram_check.py RUN_DIRECTORY`.
They require screen21/Equipment at each snapshot, native selection+preview for
cells0/1, actual equipped handle changes through Rynn+eac and the actor registry,
and no selection/equip change on the empty cell. An unavailable/malformed dump
fails explicitly. Observe native selection logs plus neutral input release;
callback/tap delivery alone does not establish an equip.

Remaining limits: rendered duplicate inventory/world image is independently
reproducible with HUD disabled and is owned by main's GS work. This input change
does not repair that rendering. Live alignment/equip is pending main's GPU run.
Spells/Skills/Map/Journal still use the earlier relative adapter; Equipment's
special equipped slots26/27 and top tab icons do not yet have absolute hit tests.
(Superseded: the tabs have hit tests (42b308 capture), the other sections are covered in section 11, and slots
26/27 are never drawn on the Equipment page: 437170 draws cells 0..23 only.)


### Follow-up contract: inventory page transform metadata

Prepared in the isolated input checkout; requires the renderer phase owners to
agree and wire the producer before it changes live behavior. Input now accepts
`MenuView::inventoryPageScaleX` (default1), independently of `hudScaleX`. The
backend should expose the same value in its overlay metadata and the host
bridge should copy it (`v.inventoryPageScaleX = oc.inventoryPageScaleX`).
Publish the scale actually applied to the displayed inventory geometry, about
the displayed image center: .75 only when that full-page transform really ran,
1 for native/untransformed geometry. Do not derive it from screenID or the HUD
option alone, and do not publish .75 when only sprites were transformed. GS and
settings-host files are owned by the phase integration workers and are not
changed by this input-side commit.

Equipment inverts `u_native = (u_display-.5)/inventoryPageScaleX+.5` after
subtracting the viewport origin/dividing by its width. The script generator
accepts `--page-scale-x .75` and performs the matching forward transform in
virtual640x512 space, before viewport mapping. Omit the flag for identity scope.
Bounds in native grid logs remain BEFORE this backend page transform.

Initial3 grid logs are opening-animation positions, not settled calibration.
Tools commit52ac20e logs every30th capture, including capture/frame/active screen.
Use matching repeated bounds after help dismissal, active screen21. Calibration
now also dumps at310 and quits330. The logs use the same native source as42ac68:
X center is `(screen+118)>>1`, Y center `(screen+fc)>>1`; do not substitute640
for native GUI coordinates. Source projection width and a scaled host/script
client are separate coordinate spaces. World-only projection restoration must
land before collecting the final stable bounds.

CPU-only follow-up checks exercise .75 versus identity, independent HUD scale,
blank margins, pillarboxing and a 4K client with an offset viewport. They do not
establish renderer metadata correctness or live grid alignment. GPU remains
reserved for main; saves remain unchanged.


### Absolute GS viewport placement correction (phase calibration)

Evidence: `build/t/inventory-phase-calibration/input.log` settles at local grid
110.40,99.13 ->414.86,262.23, but the native caller offsets the page to the left.
`42fd18` (inventory draw caller, decomp around004301xx) installs identity UI
camera via430e90, then calls renderer vtable+8c with scale1 and
`int((framebufferWidth>>1)*-.33332998)` before selected-section draw. RAM resolves
screen object6c5d80's vtable51a2f0 slot8c to132f60. At width512, offset=-85.
132f60 writes `+100=2048+offset`, while42ac68 still uses integer `+118>>1` and
omits placement. This is a proven input projection omission, not a guessed
pixel correction or a reason to alter the grid hook's camera timing.

The fix uses the actual GS centres uploaded by1e93b8: native framebuffer
`x=(sc+100)-(2048-framebufferWidth/2)+projectedX`, similarly Y with+104.
This also avoids the subpixel truncation of odd/subscaled viewport width+118.
Native absolute helper1336a8 independently demonstrates viewport placement by
adding `sc+50-(2048-framebufferWidth/2)` to the local X projection (and+58 forY).
The expected corrected grid is25.40,99.13 ->329.86,262.23 BEFORE pageScale.75.
The generator's page-scale/viewport transforms still apply afterward.

The RAM dump after drawing contains Rynn's later preview viewport (+118=221,
+100=2218); it must not replace the active grid draw state. Tools projection
scope diagnostics record those values at437170 for a final cross-check. The
image render_2500 has a pillarboxed displayed viewport: comparing native source
coordinates to full screenshot width without removing its margins yields a
misleading approximate left edge. Actual click/equip remains for main's GPU
candidate. CPU checks now additionally cover native -85 placement, independent
Y offset, combination with.75 page scale, and odd viewport width/GS centre.


### Scripted headless click delivery correction

The live `build/t/inventory-combined-click` run retains selection1 after native
RIGHT, produces no native mouse-selection log and equips neither item. Do not
count its clicks as successful gameplay. Runtime scripted setup explicitly calls
`initHeadless()`, which clears `host_`. The earlier scaling code inside `if(host_)`
therefore never ran: script cursor132,126 remained virtual640x512 while MenuView
contained the real3818x2032 client/3612-wide viewport. With pageScale.75 the
unscaled cursor lies outside that displayed page; all three intended clicks miss.

The scoped fix supplies actual client dimensions from runtime glue each tick,
via read-only `windowClientSize(hostHwnd())` and `setScriptClientSize`. No Raw
Input instance, focus change, window activation or GPU work is needed. InputModule
rescales the original virtual coordinates once, before applying MenuView; missing
window dimensions retain virtual640x512 for CPU-only script use. Real interactive
input continues using its normal HostInput polling path.

Focused CPU test now exercises the production initHeadless/loadScript/beginFrame/
afterPadMgrUpdate path. Virtual132,126 ->physical787,500 in3818x2032; with viewport
103,0,3612,2032 and pageScale.75 this maps to nativeGUI43,125. The synthetic grid
then resolves cell0 and exposes a native selection request. This proves delivery,
not execution of native selection/equip code. Resize3840x2160 maps the same
virtual point to792,531 and preserves release. All43 focused checks pass.

MenuMouse records a click snapshot: cursor/client, GUI dimensions, MenuView,
inside/inGui, raw and resolved hit, grid age/freshness, native selected slot and
pending target. Runtime emits at most32 click logs, once per click frame, also
showing expected current absolute grid bounds. A serial that does not advance
identifies an update that was never sampled, rather than silently reporting it
as a hit. No user saves or shared main files were edited.

Integrate the paired outer input/host helper commit and tools glue commit.
Rerun the SAME existing click recipe and unchanged private-save fixture first;
coordinates should not need another tweak. Expect physical787,500 for the first
click (at the same3818x2032 window), nativeGUI around43,125, raw/item0, age<=4,
absolute1 and native selection0. Empty click must preserve selection/equip, and
cell1 must change native selection/actual equipped handle. Continue to use the
RAM checker; callbacks or pad taps alone remain insufficient. Live candidate
click/equip is pending main's GPU run, and no local game was launched.


### Native equip channels: dagger ED0 versus bow EAC

The checker formerly read only Rynn+eac and falsely failed the dagger in
`build/t/inventory-scaled-click`. Direct whole-RAM evidence:

| Snapshot | Registry count | EAC ref -> handle | ED0 ref -> handle |
|---|---:|---|---|
| before |4633|0 ->none|0 ->none|
| item0 |4635|0 ->none|121a ->a811e0 (exact cell0 dagger)|
| empty |4635|0 ->none|121a ->a811e0 (unchanged)|
| item1 |4636|04c0 ->a608d0 (exact cell1 bow)|11aa ->a74e10 (different object)|

Resource-table inspection identifies cell0 as class1000e (Rynn's Dagger), cell1
as1004f (bow), both native object type10. The game's native weapon-presence
logic321b88 resolves+ed0 first and falls back to+eac; other native weapon paths
also explicitly resolve both through `ctx+7c+(ref-1)*8`. An EAC-only definition
of equipped is therefore incorrect. These fields are not interchangeable for
this fixture: dagger evidence is required specifically inED0, bow specifically
inEAC. This does not generalize unknown weapon classes to arbitrary channels.

The corrected checker outputs both references and resolved handles, validates
fixture resource classes and exact expected cell handles, and requires both
channels (plus selection/preview) unchanged on the empty click. It continues to
fail missing equips, swapped channels, invalid references and unavailable RAM;
no callback, selection or preview is accepted as a substitute for actual equip.
Live logs deliver native selection0/1 and Square at873/973, followed by neutral
pad/release. No extra wait, recipe adjustment, actuator edit or new GPU run was
needed to explain this false-negative. Original user saves remain untouched.


### Combined native final-run evidence, before upcoming canvas fix

Read-only inspection of `build/t/inventory-final-native` and
`build/inventory_final_combat.txt`: mouse checker passes7/7 on the exact run
manifest SHA256CB49628519FF4B67A92A1872BC5D48D590FB801D04AB73E64422E12EE606D131.
Recipe attack holds are520..540 and580..600; Quick Inventory6 is625..645.
The same bow handlea608d0 remains a native type10/class1004f weapon (factory20e130)
with datablockba4220 before and after. Item+14/+18 use the established inventory
XOR/rotate decoding: before-combat200/200; after-combat198/200. Raw current words
change739a0545 ->739a0b45; maximum stays739a0545. This is actual stored ammunition
evidence, not inferred from callbacks. The final default combat reporter omits
selected_item because EAC/ED0 are both0 after Quick Inventory, so directly follow
the previously established bow handle to read its still-valid datablock.

Input audit confirms two Square holds in gameplay, released to neutral pad at
global1132/1192. Quick Inventory ends with GUI absent, global active/mode0,
controller+0c0/child0 and native player104c=-1. The process reaches normal script
quit at global1312; Rynn HP remains100. Both endpoints lack an observed live
arrow factory41a1e8 actor. Do not claim projectile trajectory, enemy bow damage,
or a per-shot200->199->198 timeline from these two endpoints. They bracket both
attacks AND the later Quick Inventory operation, establishing only net200->198
across that sequence. No enemy attack, block/parry, combo, spell or Arokh outcome
is established by this particular run; earlier evidence retains its own limits.

CPU rerun in isolated combat checkout:43 focused and641 input checks pass, zero
failures. No game/GPU process was launched for this inspection and no main
runtime was changed. Original saves are not edited.

For the coordinated final exact-canvas candidate, owned recipe
`scripts/harness/recipes/combat_inventory_final_combat.txt` preserves the existing sequence and
adds only RAM dumps at550 (after first release) and610 (after second release,
before Quick Inventory625). These are private RAM dumps, not save edits. Check
bow quantity by the SAME handle at500/550/610/680; expected200/199/198/198 must
be measured, not assumed. Reuse private existing fixture cards, verify current
viewport/cell alignment, and coordinate GPU ownership with main before launch.
The upcoming canvas build remains unverified until that run is explicitly ready.


### Final canvas candidate: native-v2 live validation and pacing limit

Run `build/t/inventory-final-native-v2`, exact integration-candidate SHA256
2AE7FC64D6AD9C87186925913F8D5F2C35D0AD0F5CA1FA89D994754DF34555FB.
Main explicitly released GPU; one bounded game ran, PID37000, exit0 with script
quit1312 at input-relative~63s (timeout160). GPU released immediately afterward.
Only private copies of existing fixture-bow-item cards were used. Both original
and source fixture hashes remain unchanged (`encounter-manifest.json`). No main
runtime or actuator was edited.

Configuration is recorded explicitly: DRAKAN_DLSS=off, NEURAL=0, SND_DBG=0,
PS2X_GS_PACING=2, scale4, requested3840x2160, capture1900/2500/2750. Verbose DLSS
log/trace/diagnostic overrides were cleared; stderr has no DLSS/NGX/evaluation
entries. Audio is disabled by the existing diagnostic harness, so this run makes
no audio claim. Renderer confirms scale4 (2048x2048 internal), but actual client
is1904x993. Native window creation clamps requested size to the desktop work
area; this must NOT be reported as physical4K output validation.

**Functional assertions pass:**7/7 native mouse checks, including exact dagger
ED0 and bow EAC handles, with both channels unchanged across empty-cell click.
The bow remains handlea608d0/databa4220 across the separate gameplay snapshots:

| Relative recipe dump | Current/max arrows | Equip/state evidence |
|---|---|---|
|500, before combat|200/200|bow EAC04c0|
|550, after first release|199/200|same bow EAC04c0|
|610, after second release|198/200|same bow EAC04c0|
|680, after Quick Inventory|198/200|EAC0, ED00; no extra arrow consumed|

Both mouse attack releases return neutral pad (global1132/1192). Quick Inventory
ends with GUI0, global active/mode0, controller+0c0/child0 and player104c=-1.
No neutral-release or exit failure was observed. Current arrow flight/trajectory
and enemy damage are not established by these endpoint dumps; this run does not
extend prior spell/block/lock-on/Arokh coverage.

Visual inspection: all3 PNGs present, actual1904x993. World capture1900 has image
boundsx69..1834. Inventory2500/2750 has clean black left canvas throughx288,
page startingx289, no duplicated left inventory fragment, and right preview
showing Rynn's full head/torso/both arms (not a full-body framing claim). Bow
highlight/details are coherent, with200/200 shown before combat. The exact
viewport remainsx69,width1765; mouse GUI hits39,125 and78,125 resolve cells0/1.

**Sustained60 guest pacing is NOT passed.** After startup, host samples remain
59.2..60.4/s, but guest samples range27.0..60.0/s; final four guest windows are
34.7,27.0,34.7,57.5. Pacing is a host/guest-vsync counter, not proof of unique
rendered frames. Startup's0.3/0.0 window is excluded from these ranges.

Offline comparison with pre-canvas `inventory-final-native` (CB4962...) shows
roughly30 PadMgr updates/s in its late gameplay, versus intermittent stalls in
v2. Specific input-clock intervals:

| PadMgr frames | Pre-canvas updates/s | Native-v2 updates/s |
|---|---:|---:|
|830..900 (inventory)|29.91|26.62|
|955..1000 (inventory)|30.00|29.80|
|1100..1130 (first attack)|31.25|23.26|
|1150..1180 (second attack)|30.00|12.45|
|1240..1270 (post Quick Inventory)|30.00|30.00|
|1280..1310 (final idle)|30.00|30.00|

The final PNG was written at input-relative48.28s. A later five-update interval,
1175..1180, takes1.60s (55.73..57.33), while the earlier run took~0.17s. It lies
BETWEEN RAM dumps: first-shot file creation54.31..mtime54.67; second-shot
58.37..59.16. PNG writes therefore cannot explain all late slowdown. RAM writes
also have significant spans (~0.36/~0.79s versus older before-combat~0.017s),
and contribute stalls, but are not sufficient proof of the entire cause.
Clean post-Quick-Inventory input recovers30 updates/s. Source profiling is needed
to isolate resource/render/thread/IO contention; no single root cause is asserted.

This comparison is not controlled to one renderer change: v2 uses the newer
canvas executable AND adds two RAM dumps, with potentially different concurrent
machine load. No follow-up game was launched without further GPU coordination.
Evidence summary, per-snapshot RAM reports and7 mouse assertions are saved in
that run directory. CPU checks were rerun before this final run:641 input and43
focused checks pass. No further production input change follows these results.

## 11. Inventory contents and the shop (2026-10-24)

The relative pointer-to-D-pad adapter of section 8 is gone: it drifted the highlight away from the cursor (the
"offset" reported in the blacksmith's shop). Every section now captures its own items at the native draw, while that
draw's camera is installed, and selects through the game's own navigation. A section whose items are not certified
this frame (no capture yet, an empty Spells page, Skills without unspent points) ignores pointer motion and clicks;
wheel = L1 / R1 and right click = SELECT stay. Reverse engineering: `docs/journal/2026-10-24/shop-mouse.md`,
`inventory-content-mouse.md`. Hooks: `ps2recomp/ps2xRuntime/src/lib/drakan_inventory_content_hooks.inl`; logic:
`MenuMouse::capture*`, `updateContent`.

**The shop is the inventory.** Talking to a shopkeeper opens screen 0x15 through `33c660` with `controller+0xc = 1`
(`340508`; `player+0x590` with controller = `player+0x584`, the flag MenuMouse once called "Arokh mode") and three
sections: 0 Equipment (the sell pane), 1 Spells, 2 the shop list (vtable `54f488`). `controller+0x24` is the focused
pane: 0 Equipment, 1 Spells, 2 shop list (`345290`). The category tabs accept the 3-tab state.

| Section | Capture (native draw) | Native selection, steering | Hover | Click | Wheel |
|---|---|---|---|---|---|
| Equipment (section 10) | `437170` cells | `controller+0x20`; `33eaf8` substitutes the target | selects | Square (use / equip); in a shop Circle (sell, "Sell this item?") | sections |
| Spells (`54f3a8`) | `4481e8(section)`: spell i in cell (i % 5, i / 5) of the section frame, known spells only | `controller+0x4b4`; `3422c8(ctl, idx, 0)` from `344de0` substitutes the target | selects | Square (Summon) | sections |
| Skills (`54f1f0`) | `445b78(section)`: three rows, label corners `row+0x28/+0x30` and the level number (`row+0x38`) in each row's frame; only while points are unspent | `section+0x58`; Up / Down taps (3 rows, wrap) | selects | Right (spend a point) | over a row: up spends, down takes back; elsewhere sections |
| Journal (`54e288`) | `438be8`; page clip `4387b0` (return `438e40`), arrows `286170` (returns `438d64/438d90/438ee8/438f10`), `x, y` local pixels -> GUI via `sc+0x50/+0x58` | none | nothing | arrows: Left / Right (region), Up / Down (scroll) | over the page: scroll; elsewhere sections |
| Shop list (`54f488`) | `44a678(section)` starts the frame, item icons `434e80` (return `44aa94`) / `4380b0` (return `44aa0c`): index `sp+0xd4`, size `sp+0xc0/+0xc4`, `{column, row}` floats at `a3` | `controller+0x1c`; `33e960(ctl, idx, 0)` from `34525c` substitutes the target | selects when the whole stock is drawn (a strip scrolls, so it would slide under the cursor) | Square (buy, "Buy this item?") | sections |

A click selects first and presses the action only once the section's own selection equals the target; while the
controller is busy with a selection change (`*controller != 0`, `345d08` drops keys) the action waits (bounded).
`44a678` runs more than once per frame (a pass without icons), so a frame's captures are kept until the next input
update. Native refusals stay native and silent: the blacksmith will not buy potions and Jade's will not buy a bow,
a spell above Rynn's magic skill is not sold, and selection is refused while a summon runs (`player+0x1040` 1..4).

**Verified live** (Slot 5 save on a private card, harness `play_run.py`, `DRAKAN_DEBUG_OPEN_SHOP`, 3840x2160 with
the inventory page scaled 0.75): blacksmith strip (16 items, 8 drawn): hover leaves the highlight, a click selected
item 1 and opened "Buy this item?", Yes bought it (646 -> 436); Equipment tab click, bow click -> "Sell this item?",
Yes sold it (436 -> 655). Jade's (9 spells in rows): hover selected each spell in turn; Fireball and Fear bought; in
Spells, hover moved the selection 0 -> 7 through `3422c8` and a click summoned (`player+0x1040` 1 then 4). Skills
(`DRAKAN_DEBUG_SKILL_POINTS=3`): hover Archery selected it, click spent a point, wheel down returned it, hover + wheel
up on Magic spent one; leaving the page showed the native "apply the skill points?" box. Journal: the page clip
captured at GUI 40..285 x 103..367 (the drawn quest panel); wheel over it scrolled (no section change), wheel beside it
switched 4 -> 3. **Not verified live:** the Journal arrows (the save has one region and no overflow), the click
continue on 0x10 / 0x13 / 0x1d / 0x1e / 0x1c.

## 12. Order-of-the-Flame style views of the inventory (2026-10-24)

Custom views of inventory screen 0x15, opened by their own keys from gameplay; Tab (Select) keeps the original
screen. Hooks: `ps2recomp/ps2xRuntime/src/lib/drakan_inventory_views.inl`; pure rules:
`drakan_inventory_views_policy.h` (tests `ps2xRuntime/tests/inventory_views_policy_tests.cpp`,
`runtime/input/tests/inventory_views_tests.cpp`). Research: `docs/journal/2026-10-24/ootf-map-inventory-ui.md`.

**Host-drawn I and M (2026-10-25, journal `2026-10-25/modern-views-integration.md`).** With the overlay's screen
layer up, the I strip and the M map are drawn by the host (`runtime/gameui` `StripScreen` / `MapScreen`) instead of the
native-drawn bar and full-screen map described below; `[general] strip_screen` / `map_screen = native` (env
`DRAKAN_STRIP_SCREEN` / `DRAKAN_MAP_SCREEN`, default `modern`, read at start) keep the native-drawn views, which are
also the fallback (watchdog) of the host ones. `[menu] inventory_strip` / `map_view` still switch the views on and off.

* Binding: the views open as before (Select tap, binding at `430d68` msg 3); `InventoryView::host` marks the host
  variant and `tabBegin(kind)` binds it like the Tab screen (`drakan_tab::Shared::kind` = Strip / Map), so the Tab
  screen's EE glue runs for it: snapshot + `UiAction` executor at msg 10, host-screen input mode while no box is up,
  `CloseExecutor`, watchdog (`tabFallBack` keeps the native-drawn view for the rest of the opening), UI sounds, HUD
  cross-fade (`hostFade`). `invHostView()` / `invHostDrawing()` replace the Tab-only checks in the removal hooks
  (details text `433850`, cell decoration `435b00`, grid placement / black fill / capture, map placement).
* I strip: the native Equipment grid is placed under the strip's opaque plate (`StripScreen::layoutFor(vp).nativeGrid`,
  the Tab screen's grid proportions fitted into the plate, native scale about 0.85 at 16:9 -> `GridTarget`, the Tab
  screen's `tabPlaceGrid`), drawn over a native black fill and captured for the `ItemIcon` crops, which the host fits
  into its smaller cells. Not under the host cells: at their scale (0.57) the native mesh draw skipped the Health
  Elixir. `189b38` / `17c208` draw nothing while one of the model's textures is not resident (`tex+8 & 3 != 3`; the
  object keeps `+0x104` bit 8 clear, `+0x114` = 0 and bit `0x8000` set until `18e0c8` fades it in), and for the
  elixir that stayed so at a small size: object scale 0.64 never drawn, 0.73 drawn after ~40 frames, >= 0.83 at once
  (`DRAKAN_DEBUG_TAB_PEEK` runs `build/harness/aui-peek*`, `aui-pk-*`). The cell showed only its "100". The native
  quick-slot marker (the object at `section+0x80`, a white-blue quad the item draw `434e80` puts at a marked item's cell
  corner) is not drawn while a host view draws the grid: the mesh draw `42a4b0(section, object, ...)` called from inside
  `434e80` with that object is skipped (`quickSlotMarkerDraw`, `drakan_input_glue.cpp`; nothing is written to guest
  memory, unlike the withdrawn pointer swap that could outlive a draw that yields). Verified on the strip and the Tab
  screen after a quick-slot toggle (`build/harness/ps2-strip/strip_hot.png`, `elixir_zoom.png`, `tab_hot.png`).
  **Off-screen grid capture (2026-10-25, journal `ui-test-pass.md`).** With a letterboxed picture (21:9 / 32:9 on a
  16:9 output) the plate lies partly or wholly below the picture, where the GS front end clips the native window (21:9
  lost the dagger row, 32:9 every icon). With the d3d11 renderer (`OverlayContext::gridCapture`) the strip's native
  grid is now kept whole inside the window (`keepInWindow`, like the Tab grid) and removed from the presented frame:
  at the grid draw `437170`, before its black fill, the EE sends a marker (`drakan_scene_camera::gridCapture`,
  `SceneGuideCamera` `sourcePc` `0x437170`, `reserved` 1, the grid's box in `viewportOffset`); the renderer saves the
  box's pixels (+24 native px, through the inventory page map) at the scope's first display draw, and when that target
  is about to be displayed (a display latch whose circuit shows its `fbp`; at most 8 latches) copies the box into
  `GridCapture::tex` and puts the saved pixels back (`d3d11_targets.cpp` `gridCaptureStart` / `gridCaptureEnd`). The
  overlay crops the icons from `OverlayContext::gridSrv` (native target pixels through the same page map,
  `IconCrop::texture`). Ending the scope when 437170 or 433610 returns did not work: the item draws reach the GS after
  the EE returned (a club piece stayed on the world, `build/harness/lat-s329`), and a display latch of the other buffer
  must not end it. The grid is not drawn while a native box is up over the strip (the box is drawn after it, inside the
  scope). The bridge passes the marker to the renderer only (no vblank / world-phase bookkeeping). The
  native bar / panel draws and the details text are not drawn; the world is whole (scale 1, offset 0, the
  saved camera zoom): the Tab screen's fractional preview offset (commit `5230805`) applies to `View::Modern` only.
  Input: hover / arrows steer the native selection, Enter / double click = Square (equip / use), X = Triangle (drop),
  F = Cross (quick slot), I / Tab / Esc / right click / B close.
* M map: the native map is fitted left of the objectives panel (`MapScreen::panelRectFor(vp)` -> `MapTarget` in GUI
  units, clamped to the native window, which the GS front end clips), through the Tab screen's map path
  (`tabMapLayout`), with the views' world shade kept. Each marker sprite (`1c1e90` from `43a330`, record index in
  `s3`, as the map mouse capture reads it) is recorded at its placed centre and published at the next frame's map
  draw (`MapMarkerGui`, GUI units); the overlay maps them to pixels for `MapInput::markers`, so the ring and name tag
  sit on the native markers. The native selection frame (`4398f8` from `43a22c`) and caption (`1c1e90` from `43a498`,
  `286170` from `43a4e0`) are not drawn: the host's ring and tag replace them. The native selection follows the focused
  record (row or marker) when it has a marker on this map: `SelectMarker` (below, Map page) steers it. Location: the level name from the
  context's level path `ctx+0x98` (`"cdrom0:/English/Levels/Level 1A - Surdana/Level 1A - Surdana.lvl"` ->
  "Surdana"). M, Esc, Tab, I, right click, B close (a `Close` action through `CloseExecutor`).
* Strings: objective texts and the captions of unselected markers are not resident in the string manager outside the
  Journal page (the panel showed the region name for every objective). The missing ids (listed quests' text, enabled
  markers' captions) are requested once per level through the game's lookup `1b9fb0(mgr = ctx+0x344, id, buf, 1,
  0xff)` (flag 1: asynchronous load) as guest invocations of the current thread at `430d68` msg 10 (map) and `2bd1b0`
  (pause); the next snapshots read them (verified: "Find Lady Myschala at the palace in Surdana.", "The Palace",
  "Brekk the Blacksmith").
* **Instant close (all I / M views).** The close animation of 0x15 (`screen+0x15c` = -2, about 0.5 s) kept the views
  up after the close key. `hookViewDraw` sets the progress `+0x158` to 0.001 while it runs backwards, so the same
  frame's native step ends it on its own path (`42f758`: camera restore, screen closed). Measured: the view is gone 2
  EE frames (~10 ms) after the close action; the native close of 0x15 took 0.50 s in the same run. The open animation
  is finished early too, from the third frame after the activation (`+0x158` = 0.999): during it the 3D "you are
  here" arrow sat 3-4 px off for ~8 frames (the jitter when the map opens; markers and the settled view never moved).
  Not from the first frame: Select is ignored while the animation runs, which keeps the opening Select tap (2
  frames) from closing the view again (an instant open closed it at once).

**M = full-screen map** (`[bindings] M = VIEW_MAP`, `[menu] map_view = 1`):

* Opening is native: the key injects a Select tap (the game's own `0x4030`), the inventory's activation (handler
  `430d68` msg 3) binds the view to that screen object, and its first per-frame draw (msg 10) is replaced by one native
  section switch: `+0x154` is preset to 4 and key `0xd3` (previous) delivered through `4309d8`, so only the Map's enter
  handler runs (no Spells / Skills / Journal help boxes on the way). That frame draws nothing.
* While the view's screen draws (`42fd18`, Map section current): page offset 0 instead of `-(W/2)/3` (renderer slot
  `0x8c` = `132f60`, return `430230`), no 3D panels (`42a2c0` from the base draw `42b5c8`), no hint bar (`431a10`), no
  gold count (`42b988`), no tabs (`42b308`) or tab glow (`42faa0`), and the world crop globals set to offset 0 /
  scale 1 after the draw (`DAT_004fb604/608`), so the world behind is full screen.
* The map itself (`439b28`): the background sprite (`1c1e90` from `439d80`, 256 x 256 in Surdana) is drawn with
  `f12 = f13 = s`, the largest uniform scale fitting the native window minus a 12 px margin and a 52 px caption band
  (1.75 at 512 x 512), centred. Markers (`43a330`) keep their map-relative centres and grow by `sqrt(s)` (1.32), the
  selection frame (`4398f8` from `43a22c`) follows them, the caption icon (`43a498`) and text (`286170` from `43a4e0`)
  keep their native distance below the image, and the 3D "you are here" object is rescaled (`13f6a8` from `439f00`) and
  moved by inverting the native pixel -> object formula at `13edd8` (return `439fbc`).
* The renderer centres the view's page: the inventory draw packet (`drakan_scene_camera.cpp`) carries
  `SceneGuideCamera::CentredPage` when its screen is the view's, and `hud::inventoryMap` then maps about the screen
  centre (`x' = W/2 + (x - W/2) * s`) instead of from the left edge. Moving the native viewport right does not work:
  the GS front end culls and scissors at native x 512 before the page map, so the right part of the map was cut off.
* Mouse: `MenuMouse::setInventoryView` (set every input tick) inverts the centred map (anchor 0.5 instead of 0),
  ignores the hidden tabs (no category band either) and the wheel. Marker capture and the native arrow steering are
  unchanged. Verified live: clicks on two scaled markers selected "The Palace" and "Brekk the Blacksmith".
* Closing: M again (a Select tap; only while the inventory itself, not a help box, is active), Esc, right click, or
  **Tab, which closes the view** (Select is the screen's own close; it does not switch to the normal screen). The view
  ends when its screen is neither active nor the parent of an active message box. A later Tab opens the normal
  screen unchanged (activation without a pending request clears the view).
* The world around the map is dimmed: before the map background (`1c1e90` from `439d80`) one solid sprite over the
  whole native window (`2c8e98`, black, GS alpha `0x26`, see "Native 2D draws" below). The renderer does not
  page-map that one sprite (`hud::inventoryViewShade`: a solid full-frame sprite of a centred view frame), so it
  covers the whole output, also at 21:9.
* The gameplay HUD (`3ac540(0x510150, frame)` from the HUD screen's draw `3b2178`: health orb and gauge, mana orb,
  counters) is not drawn while a view's screen is up (`invViewShown`), so no orbs peek out under the map.
* `map_view = 0`: M opens the normal screen directly on its Map page (same jump, no custom layout). Verified live
  (`build/harness/polish-off/map.png`): normal page, orbs and gold as usual.

**I = Equipment strip** (`[bindings] I = VIEW_STRIP`, `[menu] inventory_strip = 1`; the old default `I = ACT:0x17`,
which opened the same screen as Tab, is migrated to it in memory):

* Same opening (Select tap, binding at activation; the screen opens on section 0, no jump), same page-art removal,
  centring and closing keys (I, Esc, right click, Tab). Section keys and the wheel are blocked, so it stays on
  Equipment.
* The Equipment draw `433610`: the grid's plane centre (`+0x68/+0x6c`, cells `+0x60/+0x64`) is moved for the draw
  so its projection through the live UI camera lands centred at the bottom of the window (Newton steps on the
  projected Jacobian, `MenuMouse::projectInventoryPlane`), then restored. The grid draw `437170` and the mouse's grid
  capture read the moved fields, so hover and click hit the drawn cells. The large item preview (`436b78` from
  `437170`) is skipped.
* The bar: at the entry of the grid draw `437170` (grid already moved, before any item) the view queues the game's
  own 2D draws (below): a dark translucent fill over the grid plus 5 native pixels (`kViewBarShade`), per cell a
  light shade and a thin dim-brass slot frame (all 24 cells, empty ones included), and a double brass frame around
  the bar. The cells are the projected grid divided 8 x 3 (`stripCell`, the same rectangle the mouse captures).
* The details panel: the text block (`433850`; name `+0x244`, then `+0x2c4` / `+0x284` / `+0x304` / `+0x450` /
  `+0x344` / `+0x384` of the controller `section+0x70`, `+0x4ac` = an item is selected) is measured from those strings
  (`detailsLines`, about 7.7 / 6.0 native pixels per name / stat character) and gets a dark framed panel standing on
  the bar's left end (`stripPanel`, 8 pixels above the bar, at least 110 wide, never wider than the bar). The text
  origin (`4314a0` from `433850`, return `433a2c`) is put inside it. No item selected: no panel.
* Orbs and gold are hidden in the strip (chosen over moving them to the bar's ends: the bar spans GUI 99..413 and the
  orbs sit at 28..110 and 417..481, so they would touch it at 16:9, and the strip reads as one piece without them):
  the gameplay HUD `3ac540` as for the map, the inventory's gold amount and spinning bag (`4330b0` from `433610`:
  text box `42ae00`, bag `4316c8`) and the gold count `42b988`.
* The world behind a view is whole: at the world crop call (`132f60` from `2c4444`) scale 1 / offset 0, and the
  world camera's open-animation zoom (cam `+0x74/+0x78/+0x70`) is set back to the values saved at activation
  (`+0x160/+0x164/+0x168`). The game draws no sky behind its inventory, so the renderer clears the world target at the
  first world draw after a centred view frame (`InventoryFrame::centredRecently`), instead of showing stale pixels.
* Verified live (Surdana save): grid at GUI 104..408 x 331..494, centred; hover moved the native selection 1 -> 2
  (Health Elixir) -> 0 (bow), a click equipped the bow (Rynn holds it, arrows shown), the wheel did not change the
  section, Esc and right click closed it. New game: the Equipment and Map first-visit help boxes appear centred over
  the views and OK returns to them.
* Bar, panel, hidden orbs / gold verified live at 16:9 and 21:9 (`build/harness/polish-169`, `polish-219`, 1920 x
  1080 output); Tab in the strip closes it and a later Tab opens the normal screen; `inventory_strip = 0` gives the
  normal Equipment page (`polish-off/strip.png`). Not run at a 4K output (no 4K display was attached).

**Native 2D draws from a view hook.** The bar, panel and map shade use the game's own GUI draws, in native window
pixels (centred with the view's page by the renderer):

* `2c8e98(x, y, w, h, t0 = 4 packed colours)`: the native 32 x 32 white tile stretched over the rectangle (the dark
  fill of an equipped item's cell, `435b00` flag 4). Colours are GS vertex colours `0xAABBGGRR` (`0x80` = full).
* `2c8ff8(x, y, w, h, t0 = 4 packed colours)`: the thin slot frame (the selected item's frame, `435b00` flag 2).
* `2c94a8(x, y, w, h)`, the brass message-box frame, never returned when called from the inventory (left out).
* They are queued as guest invocations of the current thread (`EeScheduler::invokeCurrentSequence`, `GuestInvocation`
  per call, `$ra = 0`, stack below the hook's `$sp`, one 16-byte colour block per call above it); the hook then
  unwinds, the scheduler runs them, and the thread resumes its own context at the hooked function's entry, so the
  hook runs again (frame stamp `decorFrame` / `shadeFrame`) and chains to the original. Calling them as nested host
  calls (`RecompGuest::call` style, no preemption) hung the game: a draw that blocks (GIF buffer kick, texture load on
  first use) unwinds the host stack by exception (`EeDispatcherTransfer`) through the nested call, and the copied
  context it ran on is lost. `SET_GPR_U32` evaluates its register index twice (`SET_GPR_U32(c, 4 + i++, v)` is wrong).

## 13. Modern Tab screen (host-drawn, phases 2 and 3, 2026-10-25)

`[general] inventory_screen = modern` (F1 > Gameplay > Inventory screen: Modern / Original; env
`DRAKAN_INVENTORY_SCREEN`; read on every open, so a change applies the next time the inventory opens). `native` (the
default) leaves everything as before; I (strip) and M (map) keep their views in both modes. Core: `runtime/gameui`
(README); research: `docs/journal/2026-10-24/tab-rework-rendering.md`, `tab-rework-data-model.md`; run notes:
`docs/journal/2026-10-25/tab-rework-phase2.md`, `tab-rework-phase3.md`.

**Native side** (`ps2xRuntime/src/lib/drakan_tab_screen.{h,inl}`, bound in `drakan_inventory_views.inl`):

* Tab opens screen 0x15 natively. Its activation (`430d68` msg 3) without a view request binds it as view
  `View::Modern` (only when the overlay's screen layer is installed, `Shared::layerReady`). The first draw checks
  for the five-section inventory (`screen+0x1b0 == 5`); a shop (three sections) falls back to the native screen.
* `42fd18` keeps running (open / close animation, camera, close path). The views' removals apply (panels `42a2c0`,
  hints `431a10`, gold `42b988`, tabs `42b308`, tab glow `42faa0`, HUD `3ac540`, Equipment gold `4330b0`, item
  preview `436b78`), plus, for this view only, the details text `433850` and the cell decoration `435b00` (equipped
  fill, selection frame). Section keys are not blocked (the executor switches sections with R1 / L1 taps). The page
  is centred (`CentredPage`), so the renderer clears the world target as for the views.
* World: at the crop call (`132f60` from `2c4444`) the projection centre goes where the host wants Rynn: offset =
  (x - 0.5) W - projX u, scale s = 1 + 2 |offset| / W (+0.01, +2/W for the fraction). The open animation's zoom is
  kept (Rynn at preview size); a host zoom factor other than 1 eases it towards the saved camera (cam `+0x74/+0x78`,
  saved at `screen+0x160/+0x164`).
* **Preview centring** (`drakan_tab_preview_policy.h`). Rynn's root (`player+0x80`) does not move while she idles;
  her turns, look-backs and bends are all in the joints: `player+0x480` = array of animated joint positions in world
  space (12 bytes each, `+0x488` = count, 33 for Rynn, `+0x484` = capacity 40), player = `ctx+0x4b0 -> +0xfc ->
  +0x10`. u = mean over the joints of view x / depth through the world camera object (`ctx+0x4b4 -> +0xfc -> +0x10
  -> +0x148`: position `+0x14`, 3x3 `+0x20` whose columns are right / up / back; the world view matrix `0x4dad58` is
  its transpose), eased with a critically damped follow (0.35 s, frame-rate independent, snaps on open and after a
  gap > 0.5 s), shift limited to 0.2 W. projX = sc `+0x40` x `+0x48` at the last world camera update (widescreen
  `worldProjectionX()`, recorded in the `1480e0` hook after the aspect / FOV correction): `132f60` scales the
  extent by s and `+0x48` by 1/s, so the product is crop-independent (measured 228.8 frame px at 16:9: 427.1 x
  0.7143 x 0.75). Projected x = W/2 + offset + projX u, positive u = right. `132f60` takes whole pixels; the
  fraction is added to `+0x100` (projection centre) and `+0x50 / +0x54` (crop edges) after it. Without a skeleton
  or a world projection it is the fixed offset. Vertical framing is unchanged. With a pillarboxed picture (4:3 on a
  wide output) the column's centre lies near or outside the picture's left edge and the pillarbox cut her in half: the
  overlay keeps the centre 0.2 x picture height inside the picture (`previewCentreInPicture`). Measured at 16:9 (journal
  `2026-10-25/tab-preview-centring.md`): the spread of her horizontal position over 30 s went from 149 px to 33-50
  px (1080p units).
* Icons: at the Equipment draw `433610` the grid's cell spacing (`+0x60/+0x64`, which also sizes the icons through
  `4346d0`) is scaled by the ratio of the host grid to the projected grid, and its centre (`+0x68/+0x6c`) Newton-solved
  onto the host grid's centre (the strip's solver); restored at the next entry. At `437170` a native black fill
  (`2c8e98`, colour `0x80000000`) is queued under the placed grid (the InvDrawBatch pattern), and the 24 projected
  cells are published (`NativeGrid`, GUI units). Measured: scale 0.909, centre (272, 172.6), target GUI
  128..416 x 101.5..243.7 at 16:9 / 1080p. On a host page without the grid (`GridTarget` invalid) `437170` is
  skipped: no native icons.
* Snapshot and actions: at `430d68` msg 10 the gameui `Reader` fills `ModelExchange` through `RdramMemory`
  (`IGuestMemory` over rdram); then one `UiAction` step. Item actions refuse unless the cell still holds the item's
  handle and the section / pane are 0; they steer the native selection with a RIGHT / DOWN tap every third frame
  whose `33eaf8` destination the hook replaces (`tabSteerCell`), then tap Square (equip / use), Triangle (drop), Cross
  (quick slot) or Circle (sell) and run `ActionTracker` on later snapshots. Spells: the same through `3422c8` (commit
  0). Sections: R1 / L1 taps until `screen+0x154` matches. Close: Select tap (`42f640` -> `42f758`). Skills: the
  section's row `+0x58` is stepped with Up / Down taps (three rows, wrapping), then Right spends / Left returns
  (`445898` -> `4468e0`). Shop browse and buy are refused (shops stay native, below). Nothing waits while a native
  box is up.
* **Skills working copies.** `4468e0(section, row, delta)` changes only the Skills section's copies: unspent points
  at `section+0x5c`, per row (`*(section+0xfc) + row * 0x1b4`, rows in skill-table order) the level at `+0` and the
  level on entry at `+4` (Left stops there; Right stops at the skill's max or when `+0x5c` is 0). The stats block
  (`S+0x2c`, skill table) changes only when the "apply?" box (shown when the page is left) is answered Yes. The reader
  takes the copies while section 2 is current, so the page shows and the tracker verifies each step (RAM dumps
  `build/harness/tab3-v2/ram_k0..4`: points 3 -> 2 -> 1 -> 0 -> 1, rows `[2,1]`, `[3,1]`, ...).
* **Character page icons.** The native grid is placed and captured exactly as on the Inventory page; the host draws an
  opaque centre column over it and crops the equipped and pack items for its cards.
* **Map page.** While the modern screen draws the Map section and the host publishes a map area (`MapTarget`, GUI
  units), the M view's map hooks run with a layout fitted to that area (`tabMapLayout`): background `1c1e90` from
  `439d80` scaled and moved, markers, selection frame, caption and the 3D marker follow (no world shade). The fitted
  rectangle is published (`MapCapture`); the overlay crops it opaquely and passes its on-screen aspect (GUI units are
  not square: the page is compressed by `hudScaleX`), so the host's focus ring lands on the native marker.
* **Targets inside the native window (2026-10-25, journal `ui-test-pass.md`).** The GS front end clips the native page
  at its window; with a letterboxed game picture (`DRAKAN_WS_ASPECT` 21:9 / 32:9 on a 16:9 output) the host's design
  rectangles fell partly outside it: the Map page lost the map's left part (gx -61 at 21:9) and at 32:9 the grid
  (target y -53..231) lost its icons. The overlay now clips the Map page's map area to `[8, W - 8] x [8, H - 8]`
  (`drakan_views::clipToWindow`) and keeps the Tab grid whole inside it (`keepInWindow`: shrunk about its centre if
  larger, then moved in; the crops come from the published cells). The strip's grid is not moved (moved up it showed
  over the open world; it draws fine below the window at 21:9; a 32:9 picture on a 16:9 output still cuts the strip's
  icons; fixed by the off-screen grid capture, section 12). Real 21:9 / 32:9 windows (`aspect = auto`) have no
  letterbox and were never affected.
* **Map page list and markers.** The Tab screen's Map section now requests the missing marker captions like the M view
  (`tabRequestStrings`, while `screen+0x154` = 3), so the list reads "The Palace", "Jade's Magic Emporium", "Brekk the
  Blacksmith" instead of "Location 1 / 3". The native selection frame (`4398f8` from `43a22c`, a bright halo) is not
  drawn on the Map page either.
* **Native map selection follows the list (2026-10-25).** The map section's selection is `section+0x6c`, a quest
  record of `DAT_004fbb70`, cycled by the arrow handler `43a588` (snapshot `NativeState::mapSelection`). Focusing a
  location (Map page) or a record with a marker (M view, rows and markers) sends `SelectMarker(record)` once per focus
  (the queue keeps the newest); the executor taps RIGHT every third frame until `+0x6c` is the record (the map
  mouse's route), waits while the open animation runs, and gives up after one lap over the records plus a margin or 12
  frames without movement. Only taps: nothing is written into the screen, so nothing outlives it; a close drops it.
  Measured: confirmed 1-4 EE frames after the focus change (`build/harness/lat-ui*`); the native caption under the map
  (seen faintly through the backdrop) and the selected marker's own highlight name the same place as the list. The
  native halo stays hidden: the host ring is the one highlight, and the halo would step through the markers between
  while the taps run.
* **UI sounds.** Host focus moves and page / filter changes set `Shared::sound` (`0x2a`, the native focus tick); the
  next screen draw message runs the property lookup (`2bec48`, the property method) as nested calls and the play
  (sound manager `ctx+0x32c`, method `+0x114`, position `0x4e8c28`) as a guest invocation, like the native draws.
* **Shops stay native.** A shop is screen 0x15 with three sections (sell pane, Spells, shop list `54f488`) and
  `controller+0xc = 1`; the modern view ends on its first draw (`screen+0x1b0 != 5`). Routing it would need the shop
  list's stock icons (a scrolling strip, `434e80` / `4380b0`) placed under host cells over a black fill like the grid,
  pane switching between sell and buy (`controller+0x24`), and the Yes / No boxes; the native shop already works with
  the mouse (section 11). Kept native until that is built and run.
* Details: the builder `342f90` (from `33eaf8`) came up empty (`ctl+0x4ac == 0`) for the selection made while the
  screen opened (its labels were still loading), and with a single item no arrow reaches `33eaf8` again. A selection
  whose details are empty is rebuilt by invoking `33eaf8(ctl, cell)` as a guest invocation of the current thread
  (`invokeCurrentSequence`, at most three times); `33eaf8` always calls the builder. The rebuild window (40 frames)
  counts from the moment the selection reached the cell, not from the action's start: the strip's first steer took
  ~45 EE frames, so the bow's card showed "..." until the UI's 30-frame resend (`mvi-v2`); now its lines arrive ~8
  frames after opening (`aui-p1-strip`). Item steering also waits while the open animation runs (`screen+0x15c` > 0,
  arrows are ignored then): the Tab screen's first steer used to run out its 60 frames and be refused.
* Input: `InputModule::setHostScreen(true)` while no native box is on top (neutral pad, bindings and menu mouse off,
  injected taps pass; independent of the F1 suspension). A box (screen 1, 2, 3, 0x1b stacked on it: first-visit
  help, Yes / No, apply points) gets the native input while the modern menu remains visible behind it. The overlay
  renders supported message boxes with the modern menu's `Theme`, stone panel, gilt heading, ivory body text and
  red-focused choice cards (`gameui/Dialog.h`). Labels are read from native widgets and reflowed with the modern
  font; the original captions, focus, enabled state and input behavior are preserved. Choice cards stay within the
  native centred-label click regions. A short label prefix before a colon becomes the title (e.g. Equipment).
  Unknown controls or unavailable content retain the native pixels from `ScreenFrame::frame`. The bounds come from the native brass frame
  draw (`2c94a8`, GUI screen caller `2bd388` or frame-widget caller `427fc8`), including the ornamental border, mapped
  through the presented viewport, HUD scale and rendered page anchor. Native widget bounds are unsuitable: a
  full-window widget (type 10, 0,181 512 x 512) and label widgets narrower than the drawn text. A new / replaced box
  clears the crop; until the native frame draw supplies bounds (or if frame capture is unavailable), the host fades
  aside so the native prompt remains accessible. Host input resumes through the key-release gate after dismissal.

**Overlay side** (`drakan_settings_host.cpp` `tabDraw`, `runtime/settings` `GameUiRenderer`, `SettingsOverlay::
ScreenLayer`):

* The screen layer draws under the F1 menu (background draw list) and takes keyboard / mouse messages while it wants
  input and the menu is closed; F1 still opens the menu over it. Before drawing, the overlay copies the back buffer
  (`ScreenFrame::frame`) and provides two blend callbacks (additive, opaque) through `ImGui_ImplDX11_RenderState`.
* Input: ImGui IO (mouse, keyboard, the Win32 backend's XInput poll) -> `gameui::UiInput` with the README mapping;
  scripted runs read the input module's queued script edges instead (`drainHostScreenEvents`), since scripts send no
  window messages. The last device picks the prompt glyphs (Xbox buttons or key caps) and whether the cursor shows.
* Each frame: the newest snapshot, `InventoryScreen::frame`, actions to the EE queue, results to toasts; the grid
  target (host grid cells mapped back to native GUI units through the page map x = vx + vw (0.5 + (gx / W - 0.5)
  hudScaleX)) and Rynn's target (the screen's `previewRect`) go to the EE.
* Rendering: backdrop = the frame copy blurred (17 additive taps over black) and dimmed, with a feathered sharp,
  lighter hole for each `WorldView`; panels and focus in the Drakan look below; item icons = crops of the frame copy at the captured native cells (footprint union),
  drawn additively (black around the icon vanishes), fitted with their aspect; fonts: screen fonts baked at the exact
  output pixel sizes (`UiFonts::screen`, re-baked when the output size changes) from the vendored Gelasio Bold /
  Regular (`third_party/fonts/gelasio`, OFL; no Windows fonts).
* **Drakan look (2026-10-25, journal `modern-ui-authentic-look.md`).** One palette and one set of `Paint` helpers for
  the Tab screen, the I strip, the M panel and the Esc menu, after Order of the Flame's inventory
  (`2026-10-25/ootf-inventory-map-reference.md`) and the game's own art, read from the user's disc at runtime
  (`INTERFCE.TXD`, never stored): large panels in the game's brass message-box frame (118) over dark warm stone (88);
  grids as dark red-brown cells on a dark bronze lattice with a gilt edge; the selection a thin red trace over a
  red-brown wash (grid cells), rows and cards add the game's flame low along the wash; the shared crest (the pale
  blue-grey gem, tex 129, in a gilt diamond setting between two bone-ivory crescents) on header rules and on the strip
  plate / map panel tops; page tabs with the native section icons of the icon atlas (94: chest = Inventory, Rynn's
  face = Character, stars = Spells, book = Journal, map = Map, handshake = Merchant; Skills, whose native icon is the
  face, gets a gilt glyph) and the gold bag for gold; the journal page on the game's parchment (86); section labels as
  letter-spaced capitals in bronze (`DrawCmd::tracking`). The optional art (94, 129, 86) falls back to procedural
  shapes when absent.
* Close: Tab, I, Esc, Backspace, right click, B / Circle, Select -> Close action -> native close; the screen fades
  out, the view ends when the screen is gone and the host-screen input mode is released.

**Real-input rules (2026-10-25, journal `tab-screen-real-input.md`).** With a real keyboard the screen used to close
itself on the frame it opened and then stay open with nothing drawn (only Rynn over the hidden page). Causes and
rules:

* *The opening key is not a close.* The game reads the keyboard through Raw Input on its own message-only window and
  thread (`HostInput`, `RIDEV_INPUTSINK`), so the Tab press opens 0x15 before the main window has dispatched that
  press's `WM_KEYDOWN`; by then the screen layer has taken the input and ImGui saw a fresh Tab press. Also, the layer
  clears ImGui's key state when it takes the input, so the auto-repeat of a held Tab looked like a new press too.
  Now: (1) `SettingsOverlay::handleMessage` drops key / button presses stamped (`GetMessageTime`) at or before the
  moment the layer took the input; (2) `gameui::KeyGate` (`Input.h`): reset when the screen opens and whenever it
  gets the input back, a key / button / pad button already down (OS state `GetAsyncKeyState`, pad state from the
  backend's XInput poll) counts only after it has been released, so a held key and its auto-repeat never act; the
  scripted path applies the same gate to the script's edges; (3) `InputAccumulator::releaseAll` (every ownership
  change) marks the keys still down as held over: Raw Input's repeats of them are no new press until released, so a
  Tab held through the close does not reopen the screen. The I / M views read the input module's edges (no repeats)
  and keep working as before.
* *A close during the open animation is deferred, not dropped.* 0x15 ignores Select while its open / close animation
  runs (`screen+0x15c` != 0, `431228`; `431d10` sets -2 to start the close). The old executor tapped Select at once,
  waited 90 frames for the screen to go, reported "refused" and left `Shared::closing` set while the host screen had
  closed itself: native page hidden, host screen gone, input owned by the host. Now `gameui::CloseExecutor`
  (`Actions.h`, `NativeState::animation` = `screen+0x15c`) waits for the open animation, taps Select, repeats an
  ignored tap (45 frames, 3 taps), waits for the close animation and reports Done; a box on top pauses it; Failed
  (time limits) restores the native page. A Close that ends refused clears `closing`, and `InventoryScreen` reopens
  where it was (`closePending()`); the overlay also reopens the host screen whenever it is bound, not closing and
  closed.
* *Close waits for a settled controller; grid placement never outlives the screen (2026-10-25, the user's hang).* The
  game died after a right click (close) on the Tab screen: `[guest-branch:missing-target] ... source=0x17a068 target=0`,
  i.e. the item class's teardown (`178f28` -> `17a030`) called through the vtable of an object whose `+0xfc` was 0.
  Cause: `tabPlaceGrid` saves the section's cell spacing and centre (`+0x60..+0x6c`) and restores them after the
  Equipment draw, but the grid draw's black-fill invocation unwinds the hook every frame, so the restore ran at the
  next frame's entry, and after a close at the next opening: four floats written into the previous screen's freed
  section (the inventory screen and its sections are allocated per opening; an item object's `+0x60` is its texture
  list). Logged in the stress runs: the placement was still pending at the end of every session (44 of 44). Now the
  saved placement is written back only into the section being drawn (or the open screen on a fallback) and dropped
  at `tabBegin` / `tabEnd`. Also: `CloseExecutor` taps Select only after the controller (`ctl+0`) has been idle for 3
  snapshots in a row (at most 120 frames, logged as "waited N for the controller"), and a queued or steering
  `SelectItem` is dropped once a close was asked for, so a right click after a hover closes without a selection change
  overlapping the teardown. The strip and the map share the executor. Stress runs (`build/harness/aui-rclick5`: 22
  Tab openings closed by right clicks right after opening, after hovering two items and after a double-click equip;
  `aui-rclick6`: 16 strip and 8 map openings the same way): no missing target, every close done, scripts reached
  `quit`.
* *Watchdog.* The overlay counts the frames it drew the screen (`Shared::hostDrawn`); `tabWatchdog` (every screen
  draw message) restores the native page when the count has not moved for 90 EE frames and 2.5 s while no native
  box is up (log `[tab-screen] ... native page restored: watchdog: host UI not drawn for N frames ...`). The fallback
  (`tabFallBack`) undoes the grid placement, drops the view (`g_invView`, centred page) and ends the host-screen input
  mode; the screen stays open natively and closes with Tab; the next Tab opens the modern screen again. Test aid:
  `DRAKAN_DEBUG_TAB_HIDE=1` keeps the host screen from drawing.
* *Switching at runtime.* `inventory_screen` applies on the next open; switching to Original while the modern screen
  is open restores the native page at once (same fallback).
* *Instant close (2026-10-25).* The modern screen ended ~0.5 s after Tab / Esc (the user's "delay before the inventory
  closes"): the host screen stayed up through the native close animation, then faded. Now `hookViewDraw` finishes the
  close animation at once for it too (`+0x158` = 0.001, `42f758` restores the camera; the saved zoom is written back
  at the same moment), `CloseExecutor::begin(idleSoFar)` counts the snapshots the controller was already idle with no
  action in flight (a settled screen taps Select at once; after a hover / steer it still waits 3 idle snapshots), and
  the host fades out at 24 / s (~40 ms) for every kind. Measured (`build/harness/lat-before` vs `lat-after3`, 1600 x
  900, frame series): Tab close action -> screen gone 8 ms (before ~0.5 s), gameplay with the HUD in the shot 45 ms
  after the key (before ~0.6 s); Esc 12 ms / 49 ms, and Esc does not open the pause menu. What stays visible is the
  game's own camera easing back from the preview zoom and Rynn turning back over ~0.3 s, live and with control (the
  native close shows the same ease under its sliding page). The modern pause menu's Resume: the world shows at once
  and its HUD fade ends ~85 ms after the key (unchanged, no fix needed).
* *Open / close transitions.* The host screen starts at 60 % opacity on open (the first frames under it are the
  native open: page window cleared, crop settling) and stays up, without input, until 0x15 is gone (2 EE frames since the instant close above);
  `InventoryScreen` keeps its page and preview rectangle while `closePending()`, so the grid / map placement and
  Rynn's crop stay put. While closing (and after) the overlay holds its frame copy (`ScreenLayer::freezeFrame`), so
  the screen fades out over a still picture once 0x15 is gone. The modern crop applies from the activation on
  (`hookViewViewport`), the native map pieces (`1c1e90` from `439d80` / `43a330` / `43a498`) are skipped when the
  host has no map area, and "opaque" frame crops blend by the screen's fade (blend factor; before, they turned into
  a dark rectangle while fading). The HUD (`3ac540`) is hidden while `Shared::hostFade` >= 0.5, so it switches in the
  middle of the fade on both sides. Screenshot series every 4 frames: `build/harness/tabreal-series9`.
* *Using the elixir.* Use (Square) on the Health Elixir is confirmed by the tracker (the record's equipped flag; the
  card shows "Equipped") and takes effect in game time after the screen closes: HP 84 -> 100 between the Tab screen
  closing and the next pause about 600 frames later (`build/harness/aui-p1-use`, `hp_before.png` / `hp_after.png`);
  the strip's Use was confirmed the same way (`aui-p1-strip`). The charges stayed 100 / 100 on the card meanwhile.
* *Quick slots.* The modern screen's quick-slot toggle is applied natively: the elixir's record `+4` = 1 after F (also
  after the close), and Q (R1, `0x403a` -> `23cae0`) shows it in the quick inventory bar. Using an item from that bar
  was not established in these runs (Q then E, Q twice, E held left the elixir and HP 84 / 100 unchanged; the
  comparison with the native screen's toggle did not complete). The bar's item is used by Attack / Cross while the
  bar is shown (E is L1, not a confirm); keys 1..5 now drive that bar (`QUICK1..5`, `input.md` section 17,
  [quick-slot-hotkeys.md](../journal/2026-10-25/quick-slot-hotkeys.md)).
* *Action time-outs follow the EE rate.* `ActionTracker` time-outs count snapshots (30 for an equip) and assume ~60
  per second; the uncapped EE runs ~250, so an equip (visible after ~0.1-0.3 s of game time) was reported refused
  ("That can't be used right now") although it happened. `tabFrame` eases the EE frames per 60 Hz tick and passes it
  to `ActionTracker::begin(a, s, framesPerTick)`, which stretches the time-out (at most 16x). Verified: the Wood Club
  equip confirmed on the Tab screen (after 46 frames) and on the strip.
* *One screen layer, four screens.* The overlay's `ScreenLayer` draws through `hostScreenDraw`: the pause menu while
  it is bound or fading (section 14), else `tabDraw`, which serves the Tab screen, the strip and the map by
  `Shared::kind` (shared session, fade, `KeyGate`, close handling; the strip and map fade out faster). M and pad
  START were added to the input mapping (M = Close, START = Back).
* "scripted 1" in the F1 log line is the game's own flag `0x4fbafc` (set while 0x15 is open, cleared by `42f758`),
  not the input module's script mode.

Phase 3 verified live (runs `build/harness/tab3-*`, 1080p, Surdana save; one spell bought at Jade's through
`DRAKAN_DEBUG_OPEN_SHOP=2`, `DRAKAN_DEBUG_SKILL_POINTS=3`): no silhouettes behind the icons, the Equipped row after
equipping (bow, club, dagger), the nameplate under Rynn, Character icons / stats / pack comparison and equipping from
it, Spells list with requirement and Summon (tracker confirmed), Skills spend / return / apply box, Journal quest text,
the native map image with the focus ring on the marker, game-picture aspects 4:3 and 21:9 (`DRAKAN_WS_ASPECT`), the
vendored Gelasio font and the UI tick being played.

Phase 2 verified live (16:9, 1080p window on the iGPU display; no 4K display was attached): open / close with Tab and Esc,
hover and double click equip the Wood Club (Rynn holds it after closing), Enter equips the bow (the preview shows it
on her back), F toggles the Health Elixir's quick slot, the pad (D-pad, A equip, Y drop, LB / RB pages, B / Select
close), Character and Spells pages with the section switch, the new-game Equipment help box over the screen and
`inventory_screen = native` unchanged. Screenshots `build/harness/tab2-*`.

## 14. Modern pause menu (host-drawn, phases 1 and 2, 2026-10-25)

`[general] pause_menu = modern` (F1 > Gameplay > Pause menu: Modern / Original; env `DRAKAN_PAUSE_MENU`; default
`native`, which leaves the game's pause page untouched; a switch applies from the next Esc, switching to Original while
the modern pause is open restores the native page at once). Design and addresses:
`docs/journal/2026-10-25/modern-pause-menu.md`; screen: `runtime/gameui` `PauseScreen`; glue:
`ps2xRuntime/src/lib/drakan_pause_screen.inl` (EE), `drakan_settings_host.cpp` `pauseDraw` (overlay); run notes:
`docs/journal/2026-10-25/modern-views-integration.md`.

* **Binding.** The pause handler `431f00` is wrapped: message 3 binds the opened pause screen (when the option is on
  and the overlay's screen layer is up), message 6 unbinds; `pauseTick` (every PadMgr update) also unbinds when the
  manager's pause slot `*0x4fb66c` no longer holds the bound object. The native screen stays the active screen and
  keeps its own work (volume ducking, game logic paused, enable states `432518`).
* **Page hidden.** `2bd1b0(screen)` (a screen's draw) runs for the bound pause with `+0x08 = 0` (no dim panel / brass
  frame) and every child widget's `+0x3c = 0` (no rows, no title band), under `ps2xNoPreemptEnter/Leave`, restored
  right after; the transition and timers still run. It stays hidden while a native child is on top (the pause is
  drawn as its parent). The HUD is hidden while the host screen is at least half opaque (`hookViewHud`).
* **Rows.** Enable states: Save / Load / Options / Quit = the native widget's `+0x3c` and `+0x40` both set (read each
  frame). Resume = a START tap (the GUI's key 1 on the pause page, `432708`), repeated every 45 frames up to 3 times;
  done when the screen is gone. Save / Load / Options / Quit = Up / Down taps (every third frame, by the native row
  order Save, Load, Options, Resume, Quit) until the native focus `+0x84` is the row, then a Cross tap; confirmed when
  a child becomes the active screen (5, 4, 9, box 1), refused after 120 frames (the host screen takes input again).
  No nested guest calls.
* **Native children.** While the active screen (`*0x4fb60c`) is not the pause, the host screen fades out, the input
  goes to the game (host-screen mode off) and the child is shown natively (Save / Load with the pager and previews,
  the Options tree, the Quit box). When the pause is the active screen again `Shared::reactivated` is bumped and
  `PauseScreen::reactivate()` resumes input; the `KeyGate` resets when the input comes back, so the Esc that closed a
  native page does not also resume the game.
* **Input.** As the Tab screen (section 13, real-input rules): host-screen mode while bound and no child is up; Esc /
  Backspace / B / pad START / right click = Resume (Tab, I and M are the shared Close key and resume as well; the
  inventory does not open from the pause), Up / Down / stick / wheel move, Enter / Space / A / click
  activate; the opening Esc or START counts only after its release (a held Esc keeps the menu open, it does not
  toggle).
* **Header.** The page title ("Paused") is centred in the header band, as the Tab screen centres its page row; at the
  left edge it ran under the fps counter (`build/harness/aui-p1-strip/pause.png`).
* **Status strip.** A gameui snapshot per PadMgr update (`readSnapshot` works outside 0x15): location (level name
  from `ctx+0x98`), up to three open objectives (texts requested as for the map, section 12), level, health, mana,
  gold.
* **F1.** `requestPauseThenOpen` is unchanged: START opens the pause, it binds as modern, the F1 overlay opens over the
  screen layer; closing F1 leaves the modern pause (with `resume_on_close = 1` its START tap ends the pause).
* **Watchdog.** No host draw (`hostDrawn`) for 90 EE frames and 2.5 s while no child is up, not resuming and F1
  closed -> the native page is restored for this opening and host-screen mode ends (`[pause-screen] ... native pause
  page restored: watchdog ...`). Test aid `DRAKAN_DEBUG_PAUSE_HIDE=1` (never drawn) or `=2` (drawn until a host page is
  open, so the watchdog runs over an owned Options screen: the native Options page comes back, Esc returns to the
  native pause and resumes; run `build/harness/uitp-wd2`).

**Stage 2 / 3: host Options, Audio, Controller pages and the Quit card** (journal `2026-10-25/modern-pause-stage2.md`;
action table: `runtime/gameui/README.md`, "Pause menu actions"). With `pause_menu = modern` the overlay always builds
the screen with `PauseConfig::hostPages` and `quitCard`.

* **Owned screens.** Entering a host page opens its native screen (Options 9 from the pause, Audio 13 / Controller 10
  from 9) with a focus (`2be528`, nested) + Cross tap; leaving taps START (key 1 = back). While Options / Audio /
  Controller or a Yes / No box the glue answers (`PauseGlue::ownBox`) is the active screen it is *owned*: drawn through
  the same `2bd1b0` wrapper (page hidden, also the cursor `+0x40`), not a native child (host input stays, no fade, no
  `reactivate`). Native screens the host did not open (Save, Load, their boxes) are children as in phase 1.
* **Hidden draw guard.** The wrapper's edits and its `ps2xNoPreemptEnter` live in a guard object (`HiddenDraw`): a draw
  that blocks unwinds the host stack with `EeDispatcherTransfer`, and before this the restore and the
  `ps2xNoPreemptLeave` were skipped, leaking the no-preempt count (no preemption on that host thread again) and leaving
  `+0x3c = 0` on the widgets. Now the guard restores and leaves during the unwind; the resumed remainder of that one
  draw shows the page (one frame at most). Seen once per Quit to Title: the box's first draw
  (`[pause-screen] hidden draw of ... unwound`). The first full run, without the guard, froze the game at Quit to Title
  (`build/harness/ps2-run1`, after earlier hidden draws); the leaked count is the likely cause, not proven (a short run
  without the guard got through, `ps2-quit1`, where the trace showed the box's first draw entering and never returning).
* **Values.** `PauseState::options` = `readPauseOptions` each PadMgr update. Sliders: `2c6c80((v + 0.25) / 100, w)`
  nested, then `2bcf80(screen13, w, 1)` posted (one post at a time, `postBusy`); check boxes, Vibration, Reverse Pitch:
  focus + Cross; scheme: Cross until `*0x4fb9b8` = v. Each confirmed by reading the field back (2 frames after the press
  in every run). The game persists them itself (`nativeOptionsTick`: `drakan_native_options.ini` and the card's
  `Drakan 2.cfg`), so nothing else is needed; a relaunch read them back.
* **Default Settings.** The host card asks; the glue clears Options' "asked" flag `screen9+0x9c` and presses `0x2344`,
  so `4424e0` restores at once on the GUI dispatch. Through the native box the apply would be the timer message 9 from
  the screen's draw, i.e. inside the hidden draw without preemption (it calls the sound manager).
* **Quit card.** Quit to Title: owned box, Yes, and a one-shot flag (`g_pauseQuitToTitle`, 30 s) makes the box (1) ->
  title (8) change keep the title instead of the `quit_exits` exit. The manager's pause slot keeps the closed pause
  object, so `pauseTick` unbinds when the title is the active screen. Quit to Desktop: `drakan_win::requestClose()`.
* **Key Bindings / PC Settings.** `drakan_settings_host::requestOpenControls()` (F1 on its Controls page) /
  `requestOpen()`; the action waits for the overlay to open and close, then `reactivate()`.
* **Input.** `UiInput::leftDown` is set from the left button (real input) or the script's held state, for slider drags.

Stage 2 verified live (16:9, 1600 x 900, Surdana save, private `drakan.ini` / `drakan_input.ini` /
`DRAKAN_NATIVE_OPTIONS_INI`, scripted input, F1 closed by posted key messages; runs `build/harness/ps2-run4`,
`ps2-audio`, `ps2-quit1`, `ps2-kb`, `ps2-strip`, `ps2-desktop`): Master 50 -> 25 by Left steps and Music 25 -> 67 by a
drag (fields confirmed per step), Subtitles, Stereo, Vibration, Reverse Pitch, scheme A -> B, Key Bindings (F1 on
Controls) and PC Settings and back, Default Settings Cancel and Restore (100 / 80 / 1 / 1 / 0 / 1 / 0), Quit card
Cancel, Quit to Title (title menu, no exit), Quit to Desktop (exit code 0). Audio: gameplay output RMS about 183 before
the pause (Master 50, Music 25) and 37-71 after resuming with Master 10, Music 0 (`DRAKAN_AUDIO_DUMP`). Not run: a
real keyboard / mouse / pad (only scripted input), 4:3 / 21:9.

Verified live (16:9, 1600 x 900 window, Surdana save, scripted input and posted F1 key messages; runs
`build/harness/mvi-pause1`, `mvi-f1`, `mvi-pausewd`, `mvi-native`): open on Esc, Resume with Esc, Save / Load /
Options / Quit open the native screens with the host page hidden and return to the modern pause, Quit box No, rapid
Esc, held Esc (stays open), F1 over the pause and back, the watchdog fallback (native page, then Esc resumes),
`pause_menu = native` unchanged. Not run: real keyboard / pad, 21:9 / 4:3, Quit Yes, Options > PC Settings from the
modern pause, phase 2 / 3 pages.

## 15. Modern title menu and Save / Load screens (host-drawn, 2026-10-25)

`[general] title_menu = modern` and `save_menu = modern` (env `DRAKAN_TITLE_MENU` / `DRAKAN_SAVE_MENU`, F1 > Gameplay
and the modern Options > Gameplay page; default `native`, which leaves the game's screens untouched). Screens:
`runtime/gameui` `TitleScreen`, `SaveLoadScreen`; glue: `ps2xRuntime/src/lib/drakan_menu_screens.inl` (EE),
`drakan_settings_host.cpp` `titleDraw` / `slotsDraw` (overlay); research:
`docs/journal/2026-10-25/modern-save-load-title.md`; live runs:
`docs/journal/2026-10-25/modern-save-title-settings-integration.md`.

* **Binding.** Polled every PadMgr update from the active screen `*0x4fb60c`: the title (8) binds when it is active
  and stays bound while it or a child (its Options screens, a Load screen, the credits) is up; the slot screens (4 / 5)
  bind when active and stay bound while they or their box / the Saving screen (0x1a) are up. Unbound when the game
  starts (0xe, the intro 0x1d, the pause), when the parent is active again (Back) and when no screen is open for 10
  frames (a save closes every screen; the pause unbinds the same way now). The slot screen's context (Title / Pause /
  LevelChange) comes from its parent `+0x34` and the save mode `+0x1e4`.
* **Page hidden.** The `2bd1b0` wrapper (`HiddenDraw`) with one widget kept drawable: the title art `0x2301` (the music
  is the sound manager's, untouched) and the slot grid image `0x2331` (its draw sends message 10: rescan, card check,
  deferred load). These draws do memory card work, so they are not no-preempt sections; a draw that does not finish
  in the call (time-slice preemption returns normally out of the host stack, a block unwinds) keeps its page hidden
  until the next frame boundary (`hiddenDrawRestorePending`) or its next draw. Before this, a preempted title draw's
  remainder drew the native panel and rows for one frame behind the modern menu (the user's "artifacts behind the
  main menu when hovering", `build/harness/msi-dense/d_105_crop.png`; after: `msi-dense2`, no such frame). Widgets the
  screen code shows / hides during the draw (`2bea48` / `2be9a8`, e.g. the "no memory card" label `0x2332` from message
  10) are followed: their new value is the one restored. The grid draw `43bfb0` only makes the thumbnail requests
  (card manager `vt+0x1a4`, then `vt+0xac`) as tail calls on the real context, one per call. The `drakan_menus` pager
  draws no text and offers no hotspots meanwhile. The native menu mouse is already off while a host screen owns the
  input (`InputModule::hostOwned`).
* **Title actions.** Load Game / Continue / New Game / Credits: focus the row (`2be528`) + Cross; Continue then shows the
  save's page, selects it and taps Cross on the Load screen (no question on the title); the host title shows a
  "Loading..." card in place of its column from the click on and the Load screen it opens shows the same card
  (`SaveSlotsState::loadingSlot`), never the list. Options: one settings screen
  (`settings-menu.md`, "One Options screen"), whose game rows run the pause stage 2 executor with the title as root
  (`0x2305` opens 9, START back). Quit: the host card; Quit to Desktop = `drakan_win::requestClose()`. The memory card
  warning (0x1c) and the logo movie (0x1e) stay native. Continue = the newest `.rsg` of the slot folders on the card
  (file time), hidden while native Load Game is disabled (empty card: no Continue, Load Game greyed, focus New Game).
* **Save / Load actions.** SelectSlot = `+0xa4`; ChangePage = `drakan_menus::showPage`; ConfirmSave (empty slot) and
  ConfirmLoad on the title = Cross (message 2). ConfirmOverwrite and ConfirmLoad in game are asked on the host card and
  then **arm what the native box's Yes arms**, without the box (every Confirm* shows the host's "Loading..." /
  "Saving..." card from the click until the screen goes or `reactivate()`): the screen timer (`+0x44 = 0`, `+0x48 = 1`; the draw
  sends message 9, which runs `443ac0`; `+0x48` is cleared once it fired) / the load bit (`+0x9c |= 0x10`; the next
  message 10 runs `43b4d0`). They run inside the screen's own draw exactly as after the native Yes, which is not a
  no-preempt section here. Posting `443ac0` / `43b4d0` as their own guest calls was rejected: an HLE-posted call runs on
  the dispatcher thread at priority 6 and preempts the GUI thread at any checkpoint, and both routines close screens /
  free the slot list the GUI thread may be walking. Back = START (key 1). Native boxes the host did not ask for (no
  card, card changed, save failed) show natively with the host faded out.
* **Pictures.** `ImageKind::SavePreview` resolves in `GameUiRenderer` through the icon source's texture: the HD
  companion PNG (trimmed to its picture inside the black bars) or the native 128 x 128 thumbnail as a 4:3 picture.
  The companions (about 3 MB each) are decoded on a worker (`std::async`, 13-19 ms each measured); the native
  thumbnail or the crest stands in meanwhile, a decode whose slot changed is dropped, nothing waits for one.
  `ImageKind::TitleArt` resolves the same way when a source provides it; the title keeps the native art instead.
  Play time does not exist in the save data and is not shown.
* **Time-outs** are in milliseconds (the EE runs ~1000 updates per second on the title, ~250 in a paused level).
* **Latency (2026-10-25, user report "Continue takes a moment or two to show the Loading...").** Where the time went:
  (1) the slot screen's fade-in `2bd5f0` (timer `+0x6c`, frame time off it, "shown" `+0x14` at 0) gates message 10,
  so the list scan, the card check and Continue's Cross waited ~1.2 s after the title opened screen 4 and again after
  Continue's page change; the page is hidden, so the glue now ends the fade at once (`+0x6c = 0` while `+0x54 == 2`).
  (2) The load itself (`43b4d0` -> `23f170` -> card manager `vt+0x19c`) and the save (`443ac0`) run synchronously in
  the screen's draw: 2.2-2.8 s (title load), 0.9-1.1 s (load in game), 0.8 s (overwrite); the EE does not reach the
  next PadMgr update, so the native Loading screen 14 only appears after it. The host card covers that time (the
  overlay keeps presenting). (3) On the title's first Load screen the GS thread spends ~0.26 s on a render-target
  readback and image uploads while the EE waits in `commitDisplay` (host profile `build/harness/lat-prof`): not
  removed. (4) Before, a "No memory card" message showed while the list was not read (the label `0x2332` is up from
  the layout until the first message 10): it now counts only after the scan. Not causes: the memory card HLE (no
  delays), the PNG decode (now off the present thread anyway), frame pacing. Measured with scripted input
  (`build/harness/lat-*`): Continue click -> host "Loading..." card the same present frame (was: nothing for 3.4 s),
  click -> Cross 0.35 s (was 1.2 s), click -> gameplay 2.6-3.3 s (was 3.9 s); title Load Game -> list 0.30 s (was
  ~1.5 s, with the false "No memory card" meanwhile); slot confirm -> card at once, Loading screen after the native
  load. `[menu-screens] overlay t=...` lines stamp the clicks, `slot screen: shown / flags / entries` the scan;
  `DRAKAN_MENU_SHOTS=<ms>` writes a frame series (`ms_<t>.png`) while a title / slot screen is up.
* **Sounds.** The host menus (title, pause, Options pages, Save / Load) play the game's own UI sounds for their
  `UiSound` (tick `0x2a`, click `0x24`, back `0x2d`, check box `0x28`, slider `0x27`; looked up in the UI entry like the
  native widgets, played as a guest invocation at the next PadMgr update), except for frames whose actions press or
  tap a native widget (`EnterPage` Audio / Controller, `LeavePage`, Resume, the pause rows, the title rows, Set* of the
  toggles / scheme, Restore Defaults, Quit to Title; Save / Load Back = START): those keep the native sound. The pause
  rows are focused with the setter `2be528` instead of UP / DOWN taps (each tap played a native tick on top of the
  host's). Verified with `DRAKAN_SND_DBG=1` (`[play]` per action, runs `lat-sound`, `lat-sound-pause2`): one sound per
  move, page, toggle, Back and confirm; Display / Gameplay / Key Bindings now click like Audio / Controls.
* **Watchdog.** No host draw for 90 EE frames and 2.5 s (title not under a child, slot screen without a box) -> the
  native page for this opening (`DRAKAN_DEBUG_TITLE_HIDE=1` / `DRAKAN_DEBUG_SLOTS_HIDE=1`: runs `msi-w1`, `msi-w2`).
  Switching an option to native while its screen is open does the same.
* **Input.** As section 13's real-input rules: host-screen mode while the host screen is on top (the pause keeps it on
  while the modern Save / Load screen is its child), `KeyGate` reset on open and whenever the input comes back.

Verified live (scripted input, 1600 x 900 and 3818 x 2032, private card copies and inis; runs `build/harness/msi-a3`,
`msi-b`, `msi-c`, `msi-d`, `msi-e`, `msi-w1`, `msi-w2`, `msi-dense2`, `msi-dslots`): title rows, Options (Display keep
and time-out revert, a toggle, game Music volume, PC Output volume, Vibration, a key bound and the layer reset), Credits
and back, Quit card Cancel, Load Game (pages 1 -> 2 -> 1, Back, load from the title), Continue (also of a newly saved
slot), New Game, Save into an empty slot, Overwrite, Load in game, Quit to Title, Quit to Desktop, native paths with
the options off, empty card, both watchdogs, rapid open / close.
