# The engine's developer debug menu

Status: working on PAL (SCES-500.06) and USA (SCUS-971.28), verified in play on 31 October 2026 (harness runs
below). Addresses are PAL EE virtual addresses unless marked; the USA ones come from the disc profile.

## What the disc still contains

Surreal's engine has an in-game debug menu: 118 options on 11 pages (cheats, teleport, render and profiling switches; the name table has 126 IDs, the rest are on no page).
The retail ELF keeps nearly all of it:

| Part | Address | Retail state |
| --- | --- | --- |
| Menu object (static) | `0x4e8d48` | Built at start-up by the constructor `0x1ccd68` (static init `0x1d5d40`, vtable `0x51ec40` at `+0x110`) |
| Option names | `0x51d738`..`0x51e188` | Table of `char*` indexed by option ID at `menu+0xc0` (`"Engine Debug Messages"`, `"Teleport"`, `"Single Step (L2)"` ...) |
| Option values | `menu+0xcc` | `u32[126]`; `SetOptionValue(menu, id, v)` `0x1d4238` applies side effects (renderer flags etc.) |
| `GetOptionValue(menu, id)` | `0x1d5de8` | Live: read by Engine_Tick, the renderer and others |
| Run option (Enter) | `0x1d4580` | Live: switch over the IDs (jump table `0x51e450`). Teleport builds `"TELE %d %d %d"` (the fly camera, see below) and calls the console command function `0x240480`; other entries call `"drop"`, `"mrclean"`, `"lvlup"`, `"shaman"`, `"heal"` ... |
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

## Every option, tested (31 October 2026, PAL)

How the menu acts on an option: Enter runs its case in `0x1d4580` (jump table `0x51e450`, by ID); without a case it
toggles the value (`!v`). Right / Left run `0x1d5190` / `0x1d5488` (their own switch; X / Y / Z go to `menu+0x100..`).
Every value change goes through `SetOptionValue` (`0x1d4238`, jump table `0x51e1a0` by ID - 1), whose cases push the
value into engine state. Only IDs 3, 4, 10, 15, 64 (and the hidden 61) are read back (`GetOptionValue` callers
`0x135ea4`, `0x147be8`, `0x150194`, `0x15f800`, `0x1911ac`, `0x1d6950`, `0x1ebdb0`). So an option with neither a case
nor a reader only changes the shown value: the code that consumed it on the development build is not in the retail
ELF. The cheats are console commands (`0x240480` / `0x23f650`): `drop`, `TELE x y z`, `mrclean`, `hendrix`, `lvlup`,
`shaman`, `heal`, `atm`, `halfspeed`, `crosshair`, `bye`, `showdmg`, `switch`, `aistate`, `aianim`, `aisight`.

Method: `scripts/harness/debug_menu_sweep.py` (one New Game run per page; per option two shots, Enter, shot, Enter,
shot; the value log `DRAKAN_DEBUG_MENU_LOG=1`; RAM stats on page 11; picture change outside the panel against the idle
noise) and `scripts/harness/debug_menu_checks.py` (walk distances, Teleport, RAM diffs of the global data against
control pairs). Every option was reached and toggled without a crash or hang, and every toggle changed its shown value
(except the `Do It` actions and the numeric rows, whose text does not change on Enter).

Legend: **works** = the effect was measured; **runs** = the code runs (value, flag or command), but nothing visible or
measurable in this scene; **dead** = no case and no reader in retail, only the shown value changes.

| Page | Option | Result |
| --- | --- | --- |
| 1 | Messages Colors, Font Size, Engine Debug Messages | runs: Messages Colors and Font Size hand the value to the renderer's debug text (`+0x38`, `0x1d7030`), Engine Debug Messages only cycles its value; the debug text itself is compiled out |
| 1 | Enable Engine Stats | runs: sets bit 0 of `0x4df37c`, read by the frame code (`0x1d6950`); no stats text in retail |
| 1 | Static Model / Actor Poly Count, Polygon throughput, Rendering VU Time, Texture Upload Time | runs: cases write renderer counters or link to ID 55; no on-screen output |
| 1 | Display Messages, Splat Poly Count, Splat Count, Core / VB / Misc / Idle Time, Object Run / Detect Time, Distance Culled Objects | dead |
| 2 | Profile Info, Profile Data Loading, Filter Profile Info, Filter Profile Threshhold | runs: toggle the menu's profiler flags (`menu+0xd8..`); no output |
| 2 | Turbo Mode | runs: switches Render Water, Engine Debug Messages, Engine Stats and the animation switches with it (seen in the log), read at `0x150194`; walking speed unchanged. Turning it off turns Engine Stats and Debug Messages on |
| 2 | **Take Screen Shot** | **works**: writes `host:ScreenShotNN.tga` (512 x 512, the native frame without the PC HUD) through `sceOpen` / `sceWrite`; the runtime maps `host:` to the boot ELF's folder |
| 2 | Do Rendering Performance Test, Object List, Object Links, Log Stats, Disable HUD, Disable Sky | dead (HUD and sky stay) |
| 3 | all (memory / cache tests and sizes) | dead (the `Do It` rows have no case) |
| 4 | **Enable Object Update** | **works**: off freezes the world (Rynn 0 units in 1.5 s of W; 3368 before, 3399 after) |
| 4 | Draw Occluders, Temporary Flag, Disable Animation System / Interpolation, Skin Normals | dead (Rynn walks animated) |
| 5 | Fog Value | runs: its case calls `0x142f70` with values derived from it; no visible change in Surdana |
| 5 | Enable Fog, Render Objects / Instance Objects / Layers / Water, Enable Distance Cull | dead in practice (Render Objects is read on one side path only; Rynn and the landscape stay) |
| 6 | **Enable Texturing** | **works**: models and HUD sprites draw untextured (the landscape keeps its textures). Rynn shows a noisy pattern, the same on WebGPU and Direct3D 11; it looks like whatever is in GS memory once no texture is bound (not checked against hardware). World fps drops (59 to 18) |
| 6 | Render Wireframe, Show Strips, Show Render Groups | runs: their cases turn Enable Texturing off and set their flag; the picture equals texturing off (no wireframe is drawn) |
| 6 | Texture Filter | runs: cycles None / Bilinear / Trilinear; no visible change |
| 6 | Anti-Aliasing, Disable VU / Core Renderer, Sync DMA, Enable Mipping, Only upload used mips, Color by Mip, Force Texture uploads, Use Landscape Spans, Render objects by model, Hardware Clipping, Splat Base Only / Disable Noise | dead |
| 7 | Enable Shadows | runs: cycles the shadow mode global (`0x4df434`, what `GetOptionValue(46)` returns); no visible change here |
| 7 | Display Lights / Num Lights / Bounding Sphere / Box / Leaf / Cloth Spheres / Vertex Normals, LensFlares, Screen Warp, Hardware Lighting, Approximate Point Lights | dead |
| 8 | Disable Sound | runs: clears the enable flag (`+0x14`) of the object at `*0x4e0b1c`, which gates some sound-manager updates (`0x1b7440` from Engine_Tick and others); the mixed output (`DRAKAN_AUDIO_DUMP`) kept its ambience and Rynn's swing sounds |
| 8 | Display Playing Sounds, Display Mode, Trace NAX Register, NAX Trace Target Voice, Show Memory Map, View Buffer Address, Load Buffer, Output SPU Buffers | dead |
| 9 | X, Y, Z | **work**: Right / Shift / Ctrl change them by 1 / 10 / 100 (`menu+0x100..0x108`) |
| 9 | **Teleport** | **works**: `TELE x y z` puts the camera in fly mode (like Fly) and moves it to `2048 * (X, Y, Z)`; with (224, 90, 168) it stood exactly at (458752, 184320, 344064). Rynn stays where she is |
| 10 | Show Damage, Show Current AI States / Animspeed / Sight Direction | runs: set their flags (`0x5130c0`, `0x4fb714`, `0x4fb718`, `0x4fb71c` 0 to 1); the debug drawing is not in retail |
| 10 | Show AI State Changes | runs (the `switch` command); no flag of its own found |
| 10 | Single Step (L2) | dead: its case only acts while `ctx+0x4c4` bit 0 (a development mode) is set |
| 11 | **Invincible** | **works**: "The Gods Favor You" / "The Gods Decide You Are No Longer Worthy" |
| 11 | **Fly** | **works**: the camera leaves Rynn (mode 7); Space (Triangle) and Q (R1) move it about 25000 units in 1.25 s, W and the mouse do not. The player controller returns early in mode 7, so the left stick has no use there; whether the right stick turned the camera on the PS2 is not established |
| 11 | **All Experience** | **works**: "You feel enlightened", XP 0 to 219623, level 1 to 12, skill points 0 to 11 |
| 11 | **Level Up** | **works**: "You're a bit older and a bit wiser" and the level-up effect (level 1 to 2 from a new game) |
| 11 | **Unlock Spells** | **works**: "Spells Unlocked At Level 1", again "... Level 2" |
| 11 | **Full Health/Mana** | **works**: "Full Health And Mana" |
| 11 | **Increase Gold** | **works**: "You've won the lottery!", gold +10000 |
| 11 | **Pause** | **works**: the world stops (Rynn 0 units while W is held) and resumes |
| 11 | **Fly PAUSE MODE** | **works** (the pause half): the world stops; the camera stays behind Rynn |
| 11 | **Suicide** | **works**: HP to -1 and the game's death screen |
| 11 | Drop, MrClean, Crosshair, Half Speed | runs (console commands); nothing measurable at the start of Surdana (no item to drop, no enemies, no bow; walking speed unchanged with Half Speed) |
| 11 | Quit | runs but inert: it clears `0x4da718`, which nothing in the PS2 build reads |
| 11 | Speed, Print Lines | dead |

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

Caveats: Suicide kills Rynn, Teleport and Fly leave the camera in fly mode (Fly again brings it back), Turbo Mode
changes other options with it, and Enable Object Update / Pause freeze the world until switched back; save before
experimenting. Take Screen Shot writes into the folder the boot ELF is read from.
