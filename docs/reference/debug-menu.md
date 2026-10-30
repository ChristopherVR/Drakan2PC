# The engine's developer debug menu

Status: working on PAL (SCES-500.06), verified in play on 31 October 2026 (harness runs below). USA is not audited.
Addresses are PAL EE virtual addresses.

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

## The port's integration

`drakan.ini [general] debug_menu = 1` (Settings > Gameplay > Developer > Developer debug menu, applied live; env
`DRAKAN_DEBUG_MENU`) enables it. Region feature `DebugMenu` (PAL profile only; greyed out elsewhere and in the browser
build). Code: `ps2recomp/ps2xRuntime/src/lib/drakan_debug_menu.cpp`, called from the PadMgr_Update hook in
`drakan_input_glue.cpp`.

- **F3** (in play, settings overlay closed) opens and closes it: `menu+0 = 1` and the callback slot `PadMgr+0x410 =
  0x131ff0`; closing restores the saved callback. While open the slot is re-asserted every frame.
- Input reaches the native handler through the normal bindings: Up / Down arrows (D-pad) pick an option, Left / Right
  arrows (D-pad) change the value, Enter (Cross) runs it, Q / E (R1 / L1) +-10, F / R (R2 / L2) +-100. A hook on
  `0x131ff0` turns Cross into DIK Enter and Start (Esc) into "close". PgUp / PgDn inject Square / Circle taps (page);
  the mouse buttons (Square / Circle bindings) also turn pages. Sticks (WASD, mouse look) still move Rynn, as on the
  original, since the axes do not go through the callback.
- After PadMgr_Update the current page is copied (names, and values from the native formatter `0x1d49f0`) and drawn by
  the overlay HUD (`HudSlot::DebugMenu`, `dsettings::drawDebugMenuPanel`) at the top right.

Verification (`scripts/harness/play_run.py`, New Game into Surdana):

- `--recipe scripts/harness/recipes/debug_menu_pages.txt --env DRAKAN_DEBUG_MENU=1`: opens with F3, moves the cursor,
  pages through all 11 pages (wraps to 1), closes. The log shows `[debug-menu] open (callback 0x23ce18)`.
- `--recipe scripts/harness/recipes/debug_menu_cheats.txt` with a run INI holding `[general] debug_menu = 1`
  (`--env DRAKAN_INI=...`): page 11, Right on Invincible -> `ON` and the game's "The Gods Favor You"; Enter on Level Up
  -> "You're a bit older and a bit wiser" and the level-up effect; Esc closes.

Caveats: options such as Quit, Suicide, Load Buffer and the cache tests do what the developers made them do; save before
experimenting. A gamepad can navigate an open menu, but only F3 opens it.
