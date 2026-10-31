# The engine's developer debug menu

Status: working on PAL (SCES-500.06) and USA (SCUS-971.28), verified in play on 31 October 2026 (harness runs
below). Addresses are PAL EE virtual addresses unless marked; the USA ones come from the disc profile.

## What the disc still contains

Surreal's engine has an in-game debug menu: 125 options on 11 pages (cheats, teleport, render and profiling switches).
The retail ELF keeps nearly all of it:

| Part | Address | Retail state |
| --- | --- | --- |
| Menu object (static) | `0x4e8d48` | Built at start-up by the constructor `0x1ccd68` (static init `0x1d5d40`, vtable `0x51ec40` at `+0x110`) |
| Option names | `0x51d738`..`0x51e188` | Table of `char*` indexed by option ID at `menu+0xc0` (`"Engine Debug Messages"`, `"Teleport"`, `"Single Step (L2)"` ...) |
| Option values | `menu+0xcc` | `u32[126]`; `SetOptionValue(menu, id, v)` `0x1d4238` applies side effects (renderer flags etc.) |
| `GetOptionValue(menu, id)` | `0x1d5de8` | Live: read by Engine_Tick, the renderer and others |
| Run option (Enter) | `0x1d4580` | Live: switch over the IDs (jump table `0x51e450`). Teleport builds `"TELE %d %d %d"` and calls the console command function `0x240480`; other entries call `"drop"`, `"mrclean"`, `"lvlup"`, `"shaman"`, `"heal"` ... |
| Value +n / -n | `0x1d5190` / `0x1d5488` | Live |
| Value text | `0x1d49f0` (menu, id) -> `char*` | Live (`ON`/`OFF`, `0.00 MB`, `Do It`, `WHITE` ...) |
| Per-frame update | `0x1d4948` | Called by Engine_Tick `0x135c40` while `IsOpen` (`0x1d5d80`, `menu+0`) |
| Key handler | `0x1d4e88` (menu, command, action, pressed, time) | Live, but only reachable through the callback below |
| Key callback | `0x131ff0` (command, action, pressed, time) | **Dead**: nothing references it |
| `Open` / `Close` | `0x1d5d88` / `0x1d5d98` (`menu+0 = 1 / 0`) | **Dead**: no callers |
| Text output | - | **Compiled out**: the update loop calls `0x1d49f0` and drops the result, and the key handler `sprintf`s `"%s %s"` into a stack buffer that is never printed |

Object layout: `+0` open flag, `+8` current page (0..10), page `p` at `+0xc + 16p` = `{cursor, u32* option ids, ?,
count}`.

The key handler takes PadMgr action IDs (`docs/reference/input.md` section 3.1):

| Action | Effect |
| --- | --- |
| DIK Up `0xc8` / pad Up `0x13c` | previous option (wraps) |
| DIK Down `0xd0` / pad Down `0x13d` | next option |
| DIK Left `0xcb` / Square `0x124` | previous non-empty page |
| DIK Right `0xcd` / Circle `0x125` | next non-empty page |
| DIK Enter `0x1c` | run the option (`0x1d4580`) |
| pad Right / Left `0x13f` / `0x13e` | value +1 / -1 |
| R1 / L1 `0x128` / `0x126` | value +10 / -10 |
| R2 / L2 `0x129` / `0x127` | value +100 / -100 |

The PC development build presumably swapped PadMgr's command callback (`PadMgr+0x410`, normally Game_OnInputCommand
`0x23ce18`) to `0x131ff0` while the menu was open: the callback has exactly that signature, and
`InputDev_InvokeCallback` (`0x131fa8`) reloads the slot after every call.

## Pages (as read from RAM)

1. Messages Colors, Font Size, Engine Debug Messages, Enable Engine Stats, Display Messages, poly counts, render timings
2. Do Rendering Performance Test, Profile Info / Data Loading / Filters, Object List / Links, Log Stats, Turbo Mode,
   Disable HUD / Sky, Take Screen Shot
3. Output Memory Usage, data cache tests, texture / model / animation / splat cache sizes
4. Draw Occluders, Enable Object Update, Temporary Flag (fDebugFlag), animation system switches, Skin Normals
5. Enable Fog, Fog Value, Render Objects / Instance Objects / Layers / Water, Distance Cull
6. Anti-Aliasing, Disable VU / Core Renderer, Sync DMA, texturing, filter, mips, wireframe, strips, render groups,
   landscape spans, hardware clipping, splat switches
7. Display lights / bounding volumes / vertex normals, shadows, lens flares, screen warp, hardware lighting
8. Disable Sound, Display Playing Sounds, Display Mode, NAX trace, Show Memory Map, View Buffer Address, Load Buffer,
   Output SPU Buffers
9. Teleport, X, Y, Z
10. Show Damage, Show AI State Changes / States / Animspeed / Sight Direction, Single Step (L2)
11. Invincible, Fly, Drop, MrClean, All Experience, Level Up, Unlock Spells, Full Health/Mana, Increase Gold, Speed,
    Pause, Fly PAUSE MODE, Half Speed, Suicide, Quit, Crosshair, Print Lines

## USA disc

The same code is in SCUS-971.28 at shifted addresses. Every function below matches PAL over its whole length once
`jal` targets and `lui`/`addiu` address immediates are masked (constructor `0x1ccd08`, handler `0x1d4e28`, update
`0x1d48e8`); the struct offsets the handler and update use are identical. Profile feature `DebugMenu` requires these
translations (`configs/regions/usa.json`), cross-checked against `analysis/regions/usa/function_map.csv` /
`data_map.csv` by `scripts/tests/test_regions.py`:

| PAL | USA | What |
| --- | --- | --- |
| `0x4e8d48` | `0x4e9bc8` | menu object |
| `0x1d49f0` | `0x1d4990` | value text |
| `0x131ff0` | `0x131f90` | key callback |
| `0x23ce18` | `0x23cd60` | Game_OnInputCommand |
| `0x4e90c0` | `0x4e9f40` | PadMgr (already audited) |

## The port's integration

`drakan.ini [general] debug_menu = 1` (Settings > Gameplay > Developer > Developer debug menu, applied live; env
`DRAKAN_DEBUG_MENU`) enables it. Region feature `DebugMenu` (PAL and USA; greyed out in the browser build, which has no
PadMgr hook). Code: `ps2recomp/ps2xRuntime/src/lib/drakan_debug_menu.cpp`, called around PadMgr_Update by the PAL
input glue (`drakan_input_glue.cpp`, `hookPadMgrUpdate`) and the USA basic-input hook (`drakan_overrides.cpp`,
`hookRegionPadMgr`).

- The **`DEBUG_MENU` binding** (`drakan_input.ini [bindings]`, default `F3 = DEBUG_MENU`, rebindable in Settings >
  Controls > Interface > Debug menu; older files gain F3 unless F3 or the action is already configured) opens and
  closes it: `menu+0 = 1` and the callback slot `PadMgr+0x410 = 0x131ff0`; closing restores the saved callback. While
  open the slot is re-asserted every frame.
- On a **controller**, holding the `[gamepad] debug_menu_combo` buttons together for 0.5 s (`kDebugMenuHoldMs`, once
  per hold) opens and closes it. Default `L3+R3` (both stick clicks, no game function); Settings > Controls > Gamepad >
  Debug menu shortcut offers L3 + R3, Select + R3 or Off, and the INI takes any `+`-joined button names. The combo
  buttons are not handed to the open menu, so holding them never runs an option.
- While open the **game input is muted** (`InputModule::setGuestMuted`): a neutral pad, no mouse look, no bindings, so
  WASD and the mouse do not move Rynn. Unlike suspension, the frame's key and controller edges stay readable
  (`hostFrame()`), and the menu turns them into pad taps (injected taps pass the mute):
  Up / Down -> D-pad (option), Left / Right -> D-pad (value +-1; Shift -> L1 / R1 +-10, Ctrl -> L2 / R2 +-100),
  Enter / Space -> Cross, PgUp / PgDn -> Square / Circle (page); held arrows and page keys repeat after 350 ms every
  70 ms. Controller buttons pass through as they are. Esc or Start closes. A hook on `0x131ff0` turns Cross into DIK
  Enter (run). After closing, one `releaseAll` drops the keys that were down, so nothing leaks into play.
- After PadMgr_Update the current page is copied (names, and values from the native formatter `0x1d49f0`) and drawn by
  the overlay HUD (`HudSlot::DebugMenu`, `dsettings::drawDebugMenuPanel`) at the top right.

Verification (`scripts/harness/play_run.py`, New Game into Surdana):

- `--recipe scripts/harness/recipes/debug_menu_pages.txt --env DRAKAN_DEBUG_MENU=1`: opens with F3, moves the cursor,
  pages through all 11 pages (wraps to 1), closes. The log shows `[debug-menu] open (callback 0x23ce18)`.
- `--recipe scripts/harness/recipes/debug_menu_cheats.txt` with a run INI holding `[general] debug_menu = 1`
  (`--env DRAKAN_INI=...`): page 11, Right on Invincible -> `ON` and the game's "The Gods Favor You"; Enter on Level Up
  -> "You're a bit older and a bit wiser" and the level-up effect; Esc closes.
- `--recipe scripts/harness/recipes/debug_menu_mute.txt` with a run `drakan_input.ini` whose `[bindings]` add
  `F12 = DEBUG_MENU` (`--env DRAKAN_INPUT_INI=...`): F3 does nothing, F12 opens; with W held and the mouse moving for
  2 s the centre of the image changes by a mean of 1.3 (PAL) / 1.4 (USA) grey levels (idle animation), after Esc the
  same W hold changes it by 15.4 / 14.8 (Rynn walks).
- `--recipe scripts/harness/recipes/debug_menu_pad.txt`: a 0.2 s L3 + R3 press does not open the menu, a 0.8 s hold
  does; D-pad Down twice moves the cursor, Circle turns the page, another hold closes it (one open and one close in
  the log).
- All four recipes pass on the USA disc too (`--exe dist/DrakanPC-usa/drakan.exe`); the log shows the saved callback
  `0x23cd60`, the USA Game_OnInputCommand.

Caveats: options such as Quit, Suicide, Load Buffer and the cache tests do what the developers made them do; save before
experimenting.
