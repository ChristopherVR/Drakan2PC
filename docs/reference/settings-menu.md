# Game menus and the "PC Settings" entry

Native Options now saves changes automatically at the EE input boundary. The
retail Save Settings button is hidden and disabled, the remaining five entries
are centered, and returning from Options no longer opens a save confirmation.
The twelve audited native option fields are stored in
`drakan_native_options.ini` beside the executable and mirrored atomically into
the native settings file under the configured card root. Startup restores and
applies them through the game's ordinary loader; no blocking guest audio/apply
routine is invoked from a copied input context. Existing native button bindings
and unknown native settings lines are retained. PC overlay settings keep their
existing Apply/OK workflow.

Research for `runtime/settings` (the F1 settings overlay): how the game builds its menus, how a menu item triggers an action, and the most
robust way to put a "PC Settings" entry into the game's own Options menu. Everything here was measured on SCES_500.06 with the Ghidra
decompile (`build/recomp_ui/alldecomp/*.c`, project `tools/ghidra_proj/Drakan`) and the disc data (`analysis/scripts/ui_layout_dump.py`).
Status: static analysis only, the hook below is **not yet wired into the runtime** (the settings library works standalone, see
`runtime/settings/README.md`).

## 1. Verdict

* The Options menu is **screen id 9** of the GUI screen manager (`0x4fb600`), layout resource `0x2340`, event handler **`0x4424e0`**.
* The menu is not a freely editable list: item text and layout come from resources, but **item order, y position and the action of each item
  are fixed in code** (a 6 entry table at `0x54ef48` plus the handler). Appending a 7th item would need a layout edit **and** a patch of that table
  and of two loops in the handler. Too fragile.
* The third entry, **"Adjust Screen"** (widget `0x2345`, screen 0x20), is a TV picture-position tool for CRT televisions. It is useless on a PC.
  The robust solution is to **repurpose it**:
  1. rename the string through the existing XDB text override (`INTERFCE.XDB` id 156) to "PC Settings";
  2. replace the recompiled function `0x4424e0` by a hook that, when message 1 arrives for widget `0x2345`, asks the overlay to open instead of opening screen 0x20.
  The same Options screen is reached from the title menu and from the in-game pause menu, so one hook covers both.

## 2. How the menus are built

### 2.1 Layers

| Layer | What | Where |
|---|---|---|
| Screen manager | global object at `0x4fb600`; screens are created by `FUN_002c4458(mgr, screenId, parentScreen, arg, flag)` (`0x2c4458`, a `switch(screenId)` factory) | EE code |
| Screen class | a C++ object derived from the GUI window class (ctor `FUN_002bb518(this, layoutId, parent)`), vtable in .data; the event handler is vtable slot at `+0x44`: `handler(this, widget, message)` | EE code |
| Layout | resource **type `0x600`, id `0x2300..0x2398` in `ENGLISH/DRAKAN2.RRC`** (SRSC container, 297 entries). Read by the ctor through the resource archive service (`*(DAT_004fbc04 + 0x11c)` vtable +0x84 / +0x8c) | disc data |
| Texts | "User Interface Properties" object of `INTERFCE.ODB` (class schema table at `0x505388`, 316 properties): each widget names a **property index**, the property value is an **XDB string id** of `INTERFCE.XDB` (`Options - Controller` -> id 130 "Controller") | disc data |
| Fonts/sounds/textures | other properties of the same object ("GUI Font", "Button Click Sound", "Options Background" ...) | disc data |

### 2.2 Layout record (type 0x600)

```
+0x00 f32 x0 = 0, f32 -1, f32 -1, f32 2, f32 2     (screen rect in normalised device units; identical in every layout)
+0x14 u32 flags (0,1,2,7)
+0x18 u16 widget count
+0x1a widget[count], 0x20 bytes each:
        u32 widget id      (0x2341 ...; 0x1000 = decoration without id)
        u32 property index (-> "User Interface Properties"; 0 = none)
        u32 0
        u16 widget type    (3 button, 6 label, 8 slider, 2 check box, 1 selector, 9 image/background, 12 list, ...)
        f32 x, y, w, h     (fractions of the screen; a button is 1.0 wide, 0.0625 high)
        u8  flags, u8 flags2
```
`python -I analysis/scripts/ui_layout_dump.py "<iso>" iso/SCES_500.06` prints all layouts with property names and the English XDB text.

### 2.3 Screens we care about (all verified in the factory and the handlers)

| Screen id | Layout | Handler (vtable `+0x44`) | Purpose |
|---|---|---|---|
| 8 | `0x2300` | **`0x43e4c8`** | Title menu. Widget `0x2302` Load Game -> screen 4, `0x2304` New Game -> screen 0xe, **`0x2305` Options -> screen 9**, `0x2306` Credits -> screen 0x13 (`0x2303` Quit is not handled) |
| 0x17 | `0x2310` | **`0x431f00`** | In-game pause menu. `0x2311` Save -> 5, `0x2312` Load -> 4, **`0x2313` Options -> screen 9**, `0x2314` Resume, `0x2315` Quit (confirm box) |
| **9** | **`0x2340`** | **`0x4424e0`** | **Options menu** (vtable `0x54eef0`, size 0xcc bytes) |
| 10 | `0x2360` | ctor `0x442398` | Controller screen (Rynn/Arokh, scheme A/B/C, vibration, reverse pitch) |
| 13 (0xd) | `0x2350` | `0x4416a0` ctor | Audio screen (master/music volume, subtitles, stereo) |
| 0x20 | `0x2398` | ctor `0x4452c8` | **Screen Adjust** (TV position), opened by Options widget `0x2345` |
| 1,2,3,0x1b | `0x2380` | `0x428448`/`0x429920` ctors | Message / yes-no alert boxes (widgets `0x238b` Yes, `0x238c` OK, `0x238d` No) |

Message codes seen in the handlers (third argument): `1` widget activated (Cross / Enter / click), `3` screen opened, `4` probably close / refresh (the Options handler calls `FUN_002bedf0`), `9`/`10`
per-frame / resume ticks, `0xb` Back (Circle / Esc), `0x10` an alert box button was pressed (widget `0x238b` = Yes). The second argument is the
**widget object pointer**; the widget id is the u16 at `widget + 0x14`.

### 2.4 The Options menu in detail

Layout `0x2340` (items in file order):

| Widget | Property (index) | XDB id / English text | What the handler does on message 1 |
|---|---|---|---|
| `0x2341` | Options - Controller (112) | 130 "Controller" | open screen 10 (table entry `{0x2341, 0xa}`) |
| `0x2342` | Options - Audio (113) | 131 "Audio" | open screen 13 (`{0x2342, 0xd}`) |
| `0x2345` | Options - Screen Adjust (116) | **156 "Adjust Screen"** | open screen 0x20 (`{0x2345, 0x20}`) |
| `0x2343` | Options - Save Settings (114) | 132 "Save Settings" | memory card save flow (shown only in some states) |
| `0x2344` | Options - Default Settings (115) | 155 "Default Settings" | confirm box ("Revert all options to their default settings?") |
| `0x2346` | Options - Return (117) | 133 "Back" | parent screen vcall `+0x3c(1)` |

The action table is **in guest memory, in .data, at `0x54ef48`**: six `{u32 widgetId, i32 targetScreen (-1 = handled in code), f32 spacing}`
records (`0x2341 0xa`, `0x2342 0xd`, `0x2345 0x20`, `0x2343 -1`, `0x2344 -1`, `0x2346 -1`). On message 3 (open) the handler walks the table to
**position the items** (`y = (screenH - (6*lineH + 0x4b)) / 2 + index * (lineH + 15)`; the y values of the layout are overridden), and on
message 1 it walks it again to find the target screen. The loop bounds are the constants 6 and `0x54ef8f`.

## 3. Ways to get a "PC Settings" item

| Option | Verdict |
|---|---|
| **A. Repurpose widget `0x2345` "Adjust Screen"** (rename text + hook the handler) | **Recommended.** No layout/table edits, localised through the text override, works from the title and the pause menu, keeps keyboard/gamepad navigation and the highlight logic of the game. Screen Adjust is lost (it is meaningless on a PC; the window can be resized, and aspect/stretch are in the overlay) |
| B. Append a 7th widget to layout `0x2340` (override `DRAKAN2.RRC` entry `0600_2340_0`) | Needs: a widget record (new id, an unused text property such as index 91 "<Unused>" = XDB 75, then rename XDB 75), a longer table at `0x54ef48` (it is followed by other data: the next object starts at `0x54ef90`), the constants 6 / `0x54ef8f` in the three table loops of `0x4424e0` (messages 1, 3 and 10) and the focus order. Untested and fragile |
| C. Hook the screen factory `0x2c4458` and intercept `screenId == 0x20` | Works too, but the handler has already run its clean-up call (`FUN_004432a0`) before opening the screen, so the menu is already torn down (what `FUN_004432a0` does was not analysed). Only as fallback |
| D. Patch the table entry `{0x2345, 0x20}` to a new custom screen id | Needs a new screen class. No |

### 3.1 Option A: what the runtime owner has to do

1. **Text.** The text `Adjust Screen` is `INTERFCE.XDB` entry 156 (all five languages). Add to `overrides/INTERFCE.XDB/text.ini`
   (format of `hle::XdbTextOverride`, see `runtime/hle/README.md`):
   ```ini
   156.en = PC Settings
   156.de = PC-Einstellungen
   156.fr = Parametres PC
   156.it = Impostazioni PC
   156.es = Ajustes del PC
   ```
   or register a rule callback that returns the strings (the callback also sees the other ids, so it can do all 5 languages in one place).
   Plain ASCII is used on purpose: the game font (`INTERFCE.TXD` 0x40/96) is a bitmap font with the Windows-1252 glyphs, so accented spellings also work if written in that code page.
2. **Hook.** Replace the recompiled function **`0x4424e0`** (`FUN_004424e0`, 1996 bytes, no callers: it is a virtual) with a wrapper:
   ```cpp
   // Options screen event handler: int handler(Screen* this /*a0*/, Widget* widget /*a1*/, int message /*a2*/)
   static PS2Runtime::RecompiledFunction g_origOptions;          // = runtime.lookupFunction(0x4424e0) before replaceFunction
   std::atomic<bool> g_openSettings{false};                       // polled by the UI thread (overlay must open on the thread that owns D3D / the window)

   void hookOptionsHandler(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
       const uint32_t widget = getRegU32(c, 5);                   // $a1
       const uint32_t msg    = getRegU32(c, 6);                   // $a2
       if (msg == 1 && widget) {
           uint16_t id;
           std::memcpy(&id, rdram + (widget & PS2_RAM_MASK) + 0x14, 2);
           if (id == 0x2345) {                                    // "Adjust Screen" slot, now "PC Settings"
               g_openSettings.store(true);                        // UI thread: overlay->open()
               SET_GPR_S32(c, 2, 0);                              // return 0 = handled, stay on the Options screen
               c->pc = getRegU32(c, 31);
               return;
           }
       }
       g_origOptions(rdram, c, rt);
   }
   // applyDrakanOverrides():  g_origOptions = runtime.lookupFunction(0x4424e0); runtime.replaceFunction(0x4424e0, &hookOptionsHandler);
   ```
   `SettingsOverlay::open()` must run on the UI thread (the one that calls `render`), hence the atomic: poll it from the gsr overlay callback
   (see `runtime/settings/README.md`).
3. While the overlay is open, the game must not run or see input (section 5 of the README: gate in the `PadMgr_Update` hook `0x1e3970`).

Alternative without touching the screen: also keep the **F1** hotkey (works in every state, including gameplay) and the gamepad combo.

### 3.2 Things to know

* The "Adjust Screen" widget id `0x2345` is also referenced by the layout (position y 0.633, overridden by the table order) and nowhere else in code (searched: only
  `0x4424e0` and the table); its text is XDB 156 only (the Screen Adjust screen itself uses property "Screen Adjust - Text" = XDB 157).
* The Options handler runs on the EE game thread; do not call ImGui or D3D from the hook.
* Messages 3/4 (open/close) are also sent when the Options screen is entered/left; no extra handling is needed because the overlay does not change the screen state.
* `FUN_0037e810(1)` (the "menu context": 1 = title menu, 2/4 = in-game) decides whether "Save Settings" is visible; "Default Settings" and "Adjust Screen" are always shown.

## 4. What the settings overlay saves, and when it takes effect

See `runtime/settings/README.md` section "Settings reference". The ini files are read once at process start (`drakan_cfg` caches the parsed file, environment
variables override it), so the overlay both **writes** the files and calls the host to apply what can be applied live.

## 5. How this was found (reproducible)

1. `analysis/strings_vaddr.csv`: strings "Options - Controller", "Main - Options", "InGame - Options" ... at `0x53dc40..` are **property names** (no code references them). The
   table of 16 byte entries `{name*, type, default*, flags}` starting at `0x505388` is the schema of the "User Interface Properties" object (`INTERFCE.ODB`, class entry `0x50ef10`).
2. The ODB object value block holds the XDB ids packed back to back (`0x75 0x79 0x76 0x77 0x78` = Load Game, Quit, New Game, Options, Credits).
3. Screens call `ODB_object->vtable+0x1c(propertyIndex)` to fetch a property (visible in `FUN_0043f570`: indices `0x89 0x8a 0x8f 0x8e` = "Bad MC" / "Full MC" ...).
4. `FUN_002bb518` (window ctor) takes a layout id (`0x2300`...) and reads it from the resource archive; the ids and the screen classes were matched through the factory `FUN_002c4458`.
5. Handlers were located through the vtables (`0x54e6b8`, `0x54dfb0`, `0x54eef0`: handler at `+0x44`) and read in the decompile.
6. `ui_layout_dump.py` prints layouts + text for cross-checking; the options table at `0x54ef48` is plain .data and can be dumped with `analysis/input/elf.py` (`u32`).

## Controls page / overlay input (controls branch, 2026-10-17)

* New Controls rows: **Keep camera pitch** (`[mouse] pitch_persist`), **A / D strafe** (`[keyboard] strafe`), **Mouse in inventory / map** (`[menu] inventory_mouse`); "Invert vertical look" now reads "Off (default): mouse up looks up".
* **Overlay clicks that needed several tries**: the Win32 backend's `WM_*` handlers ran on the window thread while `render()` builds the ImGui frame on the present thread (ImGui is not thread safe, and `ImGui::SetCurrentContext` is process global). Now every access to the overlay context goes through one recursive mutex (`g_imguiM`); the window thread never blocks on it (`try_lock`, otherwise the message is queued and drained at the start of the next frame, in order); `io.ConfigInputTrickleEventQueue = true` so a press and a release that arrive within one frame are delivered on consecutive frames; and a mouse button message queues its own position (mapped to back buffer pixels) ahead of the button, so the click lands where the cursor was when the button went down instead of where the per-frame poll finds it. Works with any host window (the code only needs messages delivered to `handleMessage`).
* Verification: `DRAKAN_OVERLAY_DEBUG=1` logs every left click ImGui registers (`[overlay] click #n at (x, y)`); real `SendInput` run `build/recomp_ctl/t/o1.ps1` (see UI report below).

## Game-style F1 menu and F1 gating (style branch, 2026-10-17)

**Look.** The overlay draws itself with the game's own UI art, decoded at run time from the user's disc (`drakan_settings_host.cpp` reads `INTERFCE.TXD` through `hle::IsoFs`,
`settings/TxdImage.h` decodes the 8/16/24/32 bpp blobs, `settings/Skin.*` uploads and draws them; nothing is ever written to disk). Textures used (INTERFCE.TXD type 0x40):

| id | what | use |
|---|---|---|
| 118 (256x256) | brass popup frame (37,78)-(218,228) and the spear-tipped title bar (1,4)-(255,35) | window frame (9-slice, 26 px corners), plates for tabs / buttons / combos / title (3-slice, 30 px caps, label slot at y 8..24) |
| 88 | dark brown rock | panel and popup backdrop (tiled) |
| 128 | white sphere (28,2)-(88,60) | slider knob, check-box gem (tinted) |
| 197 / 198 | brass triangles | combo arrow |
| 103 | empty check box | toggle socket |
| Generated in `Skin.cpp` | 128x64 per frame, 32-frame RGBA sheet | focused / hovered plate: upright flames with transparent tips and edge fades; replaces the stretched, counter-scrolling 32x32 tile 28 |
| 61 | the star-arrow cursor (blue colour key) | mouse cursor of the menu |
| 96 | the decorative menu font | headings, tabs, buttons |

Font 96: 12 lines of 21 rows; the top row of a line carries marker pixels (palette entry `01ffff00`, first marker at x >= 3) at the left edge of each glyph, the last marker is the right edge; the 20 rows below
are the glyph. Glyph 0 is the space, then ASCII 0x21.. in order. `prepareGameGlyphs` smooths every glyph 3x and bakes it into the ImGui atlas as codepoints 0x20..0x7e of a Roboto-subset font (accents fall back to Roboto).
Small text uses Palatino Linotype / Georgia (Windows) or the embedded Roboto. If any texture is missing the menu keeps the old plain style (`GameSkin::fetch` fails, `SettingsMenu::skinned()` false).
Sounds: `ISettingsHost::playUiSound` -> the host queues the game's UI sound property (tick 0x2a "Next Option", confirm 0x24, back 0x2d "Exit Sound", check box 0x25 / 0x26) and the EE thread plays it in the
PadMgr_Update hook with `MenuMouse::playSound` (the game's own sound-manager call).

**F1 gating.** `SettingsOverlay::setAllowGate`: F1 / the gamepad combo only open the overlay when `drakan_pause::f1Allowed()`: the title menu has been seen once (so never during the logos / intro), the last PadMgr_Update
sample is < 700 ms old (movies and loading do not call it) and the game is either in a menu screen (1 2 3 4 5 8 9 10 13 0x13 0x15 0x17 0x1b 0x1c) or in gameplay (player camera running, no GUI screen, scripted-input flag
`0x4fbafc` clear). Measured screen ids: 30 (0x1e) logo movie, 8 title, 14 (0xe) new game / loading, 29 (0x1d) intro movie. Ignored presses are logged (`[settings] F1 ignored (...)`). Closing is never gated; an overlay that is
open when a movie / cut scene / loading screen starts is closed. Debug: `DRAKAN_F1_ALWAYS=1` disables the gate; `DRAKAN_OVERLAY_SHOT_DIR=<dir>` writes `<dir>\state.txt` and a BMP of the presented frame whenever `<dir>\shot.txt` appears.

## PC Settings inside the modern pause menu (2026-10-25)

With `[general] pause_menu = modern` the pause menu's Options > PC Settings (and Controller > Key Bindings) open the
modern pages `gameui::SettingsPages` inside the pause menu (`PauseConfig::pcSettingsPages`) instead of the overlay; they
render the same `SettingsSchema` the overlay draws its rows from (`runtime/settings/README.md`, "One schema") and apply
through the overlay's apply path (`dsettings::ModernSettings`). The native Options > "PC Settings" hook and F1 keep
opening the overlay. Contract: `runtime/gameui/README.md`, "PC Settings pages (modern)"; notes:
`docs/journal/2026-10-25/modern-pc-settings.md`.

## One Options screen in the modern menus (2026-10-25)

In the modern pause and title menus there is no separate PC Settings row any more: **Options** opens one settings
screen with the PC settings categories (Display, Audio, Controls, Gameplay, Key Bindings) and the game's own seven
options folded into the matching pages (`PauseConfig::unifiedOptions`, `runtime/gameui/include/gameui/UnifiedOptions.h`):
Audio starts with a "Game sound" section (Game volume = the native Master Volume, Music volume, Show subtitles, Stereo
sound) above the PC output rows (the PC master volume reads "Output volume" there), Controls with a "Game controller"
section (Control scheme A / B / C, Vibration, Reverse pitch) above the mouse / keyboard / gamepad rows; Key Bindings
are their own tabs. The game's rows apply through the native screens as before (the hidden Options 9 is entered with
the screen, Audio 13 / Controller 10 while their page is shown) and are saved by the game (`drakan_native_options.ini`,
the card's `Drakan 2.cfg`); the PC rows through `dsettings::ModernSettings` (`drakan.ini`, `drakan_input.ini`).
**Restore defaults** (F / Square, per page) restores that page's PC defaults and, on Audio / Controls, the game's own
defaults of the options on that page (100 / 80 / on / on, A / on / off); the native Default Settings row is gone. The
F1 overlay is unchanged (traditional menus, and F1 everywhere). The modern Options > Gameplay page carries the
`save_menu` / `title_menu` choices too (as the overlay's Gameplay page). Live notes:
`docs/journal/2026-10-25/modern-save-title-settings-integration.md`.
