# Input: pad -> game data flow and mouse + keyboard hook plan

Status: static analysis only (Ghidra 12.1.4 headless, no runtime verification yet). Confidence labels:
**[H]** read directly from decompiled code, **[M]** inferred from code + data, **[L]** guess, must be confirmed at runtime.
All addresses are EE virtual addresses in `SCES_500.06`. Function names are applied in the Ghidra project and listed in
`analysis/input/names.csv`; decompiled C of every named function is in `analysis/input/*.c`; data tables dumped to
`analysis/input/tables.txt`.

Note: `docs/reference/iop-and-io.md` quotes libpad entry points 4 bytes off for some functions. Ghidra function entries are:
`scePadInit` 0x4b0948, `PortOpen` 0x4b0ba8, `Read` 0x4b0ef8, `GetState` 0x4b0f78, `SetMainMode` 0x4b1490, `SetActDirect` 0x4b1548.
Use the `functions.csv` entry addresses for hooks.

## 1. Big picture

The game engine is a PC-lineage engine (DirectInput-style key ids, "L-MOUSE" ids, command codes `0x40xx`). On PS2 a thin
"pad manager" object translates the DualShock into the same abstractions the PC build used:

```
padman DMA ring --scePadRead(0x4b0ef8)--> PadMgr_Update (0x1e3970, once per Engine_Tick 0x135c40)
   |  per sample: 32B -> button mask (active high) + 4 stick bytes
   |
   |-- sticks --> PadMgr_ProcessStickAxis (0x1e4b30) --> float axes[19] at padmgr+0x44c        (analog path)
   |-- buttons -> PadMgr_DispatchActionEvents (0x1e40f0) -> device callback -> Game_OnInputCommand (0x23ce18) (digital cmds)
   |-- buttons -> PadMgr_DispatchGuiKeyEvents (0x1e3f78) -> GUI event manager (DAT_005565d8) keydown/keyup  (menus)
   '-- cheat-sequence detector (0x1e4528)

Gameplay consumers
  analog:  Player_GatherInput (0x44c2f8) reads axes via InputDev_GetAxis(vslot +0x54) -> float out[5]
           -> Controller_Update_* (0x271e18 / 0x274c18 / 0x311220) movement
           Camera_UpdateLook (0x44e4a8) reads axes 0,1,5 -> camera pitch/yaw + player turn
           Player_AxesToDigitalCommands (0x243f98): axes 2,3,4 -> also emits digital move commands
  digital: Game_OnInputCommand (0x23ce18)  (cmd, actionId, pressed, timeMs)
```

The pad manager is the single global object `PadMgr` at **0x4e90c0** (`PadMgr_GetInstance` 0x1e48a8). The game context
pointer lives in the variable at **0x5565d4** (`PTR_DAT_005565d4`); the input device is `*(ctx+0x358)` (== &PadMgr).
Every access is a C++ virtual call: `vt = *(u32*)(dev+0x434); fn = *(vt+slot); fn(dev + *(s16*)(vt+slot-4), ...)`.
The vtable for PadMgr is at **0x51f148** (pairs of {s16 this-adjust, fn}); base class `InputDev` ctor 0x131da8.

## 2. The raw pad sample (a)

`scePadRead(port, slot, buf)` copies 32 bytes (length in padman ring entry +0x60) and returns the length (<=0 = no data). [H]
`PadMgr_Update` requires `scePadGetState(0,0)==6`, otherwise it resets. First time it calls `scePadSetMainMode(port,0,1,3)`.
Sample layout used (standard Sony layout) [H]:

| byte | meaning |
|---|---|
| 0 | status (0), 1 = mode id (0x73 DualShock analog), not checked |
| 2,3 | buttons, **active low**: mask = `(~((b2<<8)|b3)) & 0xffff` |
| 4 | right stick X (0x80 centre) |
| 5 | right stick Y |
| 6 | left stick X |
| 7 | left stick Y |
| 8.. | pressure bytes - **not read by the game** |

Button mask bits (Sony SCE_PAD*): L2 0x1, R2 0x2, L1 0x4, R1 0x8, Triangle 0x10, Circle 0x20, Cross 0x40, Square 0x80,
Select 0x100, L3 0x200, R3 0x400, Start 0x800, Up 0x1000, Right 0x2000, Down 0x4000, Left 0x8000.

Samples are pushed in an 8-deep ring per port (0x20 B each, `padmgr+0x714` port0 / `+0x720` port1, counts at +0x71c/+0x728,
guarded by semaphore at +0x72c). Port 1 is polled too but is not required (missing sample => zeros/0x80). Per-port button
mask stored at `padmgr+0x700/+0x704`; last stick bytes at `+0x4c8 rx, +0x4c9 lx, +0x4ca ry, +0x4cb ly`.

Stick processing `PadMgr_ProcessStickAxis(this, byte, |delta|, stick)` (stick 0=lx 1=ly 2=rx 3=ry) [H]:
`v = byte-128`; if `byte in [0x51,0xaf]` and `|byte - lastbyte| < 4` then `v = 0` (jitter filter, **not** a real dead zone);
`f = clamp(v/128, -1, 1)`; **`out = sign(f) * pow(|f|, 2.0) * scale`** with `scale = padmgr+0x4a0+12*stick` (sensitivity set by
options, see 3.2); `out` stored to `padmgr+0x49c+12*stick`. Then, per sample, for each of the 4 sticks
`axes[ map.axisId ] += out` where the 4 records at `padmgr+0x498` are `{axisId (-1 = unused), out, scale}`; `axes` = floats
at `padmgr+0x44c` (19 slots, cleared each sample by vslot +0x2c = `PadMgr_ClearAxes`).

So the squared response, the jitter filter and the per-axis sensitivity are all applied before any gameplay code sees a value.

## 3. Virtual action / binding layer (b)

### 3.1 Is there a PC keyboard/mouse binding system compiled in?  Partly.

* **Key/mouse/joy name tables** at 0x521724 (DIK keys: "ESCAPE","BACKSPACE","L-SHIFT","NUMPAD7" ...; 0x20 B records
  `{u32 DIK scancode, u16 char, u16 shifted char, ..., name}`), 0x522450 mouse (`L-MOUSE R-MOUSE M-MOUSE MOUSE03..15
  MOUSEX+ MOUSEX- MOUSEY+ MOUSEY-`) and 0x5226b8 joystick (`JOY00..JOYRZ-, JOYPOV*`) are **dead data**: a lui/addiu/lw scan
  of the whole .text found **no references** into 0x521700-0x522b00. There is no DirectInput/keyboard/mouse polling code; no
  USB keyboard/mouse (libusbkb/libmouse) IOP modules either. The strings `UserControl.h`/`PlayerControlState.h`/`FlankingAI.h`
  are in the ELF header page (file offset <0x1000), not loaded, so they give no code anchor.
* **What IS live is the action/command layer** built for it:
  * *Action ids*: PC-style ids. `0x01..0xd3` are DirectInput scancodes (Esc=1, Enter=0x1c, Up=0xc8, Down=0xd0, Left=0xcb,
    Right=0xcd, Home 0xc7, Del 0xd3, PgDn 0xd1, '\\' 0x2b ...); `0x100,0x101,0x102` = mouse buttons L/R/M (matching the mouse name
    order); `0x114/0x115` extra; `0x120..0x12c` and `0x13c..0x13f` = DualShock buttons (see tables.txt); `0x130..0x135` =
    synthetic "stick as dpad" (from `Player_AxesToDigitalCommands`).
  * *Keymap*: `InputDev` keeps `u16 keymap[512]` at device+8 (`SetKeymap` vslot +0xfc, `GetKeymap` +0x104) mapping action id ->
    **command id** (`0x4002..0x4069`).
  * *Callback*: device+0x410 = `Game_OnInputCommand` (0x23ce18), registered by `Input_RegisterGameCallbacks` (0x243e88)
    via vslot +0xec (second callback +0x414 = 0x2cc7c0 via +0xf4). `InputDev_InvokeCallback` (0x131fa8, vslot +0x144) calls
    `cb(command, actionId, pressed, timeMs)`; time = `FUN_004af7f8()` (ms timer).
  * *Binding tables* applied by `Options_ApplyButtonScheme` (0x3fa258):
    1. **Default keyboard map** (0x54b6c8, 56 entries `{DIK, command}`) is loaded into the keymap first. This is a complete PC
       keyboard binding, still shipped (Up/Down 0x4019/0x4020, Left/Right 0x4013/0x4014, Space/Enter 0x4009, A 0x4007,
       LShift 0x4010, RShift/T 0x4011, RCtrl/G 0x4012, Del 0x400c, PgDn 0x400b, '[' ']' 0x4034/0x4035, digits+Backspace
       0x403a-0x403f, Z/X 0x4058/0x4059 ...; full list tables.txt).
    2. Then the DualShock scheme (A/B/C; ground & air; normal or alt) overrides pad ids 0x122..0x129,0x13c-0x13f (12 entries each,
       tables 0x511ed8 / 0x511ee8).
  So **any host key can be injected as its DIK action id** and the shipped default keymap resolves it to a game command.

### 3.2 Stick -> axis mapping and options (b/c)

`Options_ApplyStickAxisMapping` (0x3fa4a0): options object (global `0x4fb6d0`) `+0x7c..+0x88` = 4 signed ints `cfg[stick]`;
for stick i: `SetStickAxisMap(2, i, |cfg|)`; scale = `Options_LookSensitivity` (0x3fa8d0 = `10^(opt+0x78*x - 1.45)`) for
axisId 0 and 1, else 1.0; negated if `cfg<0`; axis 1 also negated if "invert Y" (opt+0x64). Tables at 0x511ef8 (ground=Rynn) and
0x511f08 (air=Arokh), selected by controller scheme A/B/C (`opt+0x2e8`) and loaded by `Options_LoadStickScheme` (0x3fa830).
Records `{axisId, stickSlot}`:

| scheme | lx | ly | rx | ry |
|---|---|---|---|---|
| ground A,B | axis0 | -axis2 | axis5 | axis1 |
| ground C (twin stick) | axis3 | -axis2 | axis0 | axis1 |
| air A,B,C (C: lx axis3) | axis0 | -axis2 | axis0 (A,B) | axis1 |

Axis meaning [M]: **axis0 = turn (yaw) rate; axis1 = look pitch; axis2 = forward/back; axis3 = strafe; axis4 = extra (sidestep/
vertical, fed by L2/R2-type digital commands); axis5 = camera orbit yaw (right stick X in A/B)**.

## 4. Player controller input (c)

* `Player_GatherInput` (0x44c2f8): `(player, float out[5])` [H]: `out0 = -GetAxis(0)`, `out1 = GetAxis(1)`, `out2 = GetAxis(2)`,
  `out3 = clamp(GetAxis(3),-1,1)`, `out4 = GetAxis(4)`; if scripted input flag `DAT_004fbafc` is set all outputs are 0.
  Scripted nudges (`DAT_004fbb0c/10` -> `SetAxis(0,+-0.6)`, `AddAxis(1,+-0.2)`) when `DAT_004fbb34&0x40`. Divides out0/out1 by
  time-scale `_DAT_00512db4`, multiplies all by product of `player+0x4bc..` (movement modifier floats). Called by three
  controller update functions: 0x271e18, 0x274c18, 0x311220 [M: ground vs dragon variants; not yet identified which].
* `Player_AxesToDigitalCommands` (0x243f98): axes 2/3/4 thresholded (+-0.1 / +-0.2 / +-0.25) -> synthesizes commands via
  `Game_OnInputCommand`: axis2 -> 0x4019(+)/0x4020(-) ids 0x134/0x135; axis3 -> 0x4014(+)/0x4013(-) ids 0x130/0x131; axis4 ->
  0x4011(+)/0x4012(-) ids 0x132/0x133. (So movement is both analog and "key" driven; double-tap detection <350 ms in the
  0x4011-0x4020 handlers triggers dodge/roll states `player+0xc4`.)
* `Game_OnInputCommand` (0x23ce18): giant switch; held-state flags kept in `player+0x504/+0x510` (bit0/1/8 per direction).
  Command guesses [L]: 0x4006 primary attack (Square), 0x4007 secondary/block (Circle; sets player flag 0x20), 0x4009 jump/use
  (Space/Enter/L1), 0x400a mount/fly toggle (R3), 0x400b/0x400c (R2/L2, PgDn/Del) target/aim modes (0x400c calls
  `Input_SetAimAxisMode` 0x457390), 0x4011/0x4012 sidestep L/R, 0x4013/0x4014 turn L/R, 0x4019/0x4020 forward/back, 0x4015/0x4016
  strafe?, 0x4030 inventory/map, 0x4034/0x4035 prev/next weapon, 0x403a-0x403f item/weapon slots, 0x4056 abort, 0x4058/0x4059
  camera zoom. Confirm with a runtime log of (cmd,pressed) while pressing each pad button.
* Pad -> command default scheme A (normal): Square 0x4006, Circle 0x4007, L1 0x4009, L2 0x400c, R1 0x403a, R2 0x400b,
  Triangle 0x4011, Cross 0x4012, Up 0x4019, Down 0x4020, Left 0x4015 (full 12-entry lists for all schemes in tables.txt).
* Rumble: `PadMgr_UpdateRumble` 0x1e4278 -> `scePadSetActDirect`.

## 5. Camera (d)

`Camera_UpdateLook` (0x44e4a8) `(player, float limits[1..], bool dragonMode)` [H]; called by `Player_CameraLookTick` 0x44de40
(foot: limits[0]=0.6283 rad, dragon: 1.0472 rad) and 0x327700. Let `ctx=*(u32*)0x5565d4`, `dt = *(float*)(ctx+0x24)` (seconds,
already time-scale adjusted, see 7), `camHandle=*(u32*)(ctx+0x4b4)`, `cam = *(*(camHandle+0xfc)+0x10)` (`Cam_GetObjFromHandle` 0x283ac8):

| field | meaning |
|---|---|
| cam+0x84 | camera/look **pitch** (radians, float) |
| cam+0x90 | camera yaw offset relative to player heading (radians) |
| cam+0x94 / +0x98 | alt yaw/pitch accumulators when mode (FUN_00423790) active |
| cam+0x9c | smoothed pitch rate (`+= min(1,dt*player+0x38)*(axis1*player+0x28*... - cur)`) |
| cam+0xa0 | smoothed turn rate (axis0 * player+0x2c * 0.75) |
| cam+0xa4 | smoothed orbit rate (axis5 * player+0x2c * 0.5) |
| cam+0x48 | flags (bit5: "direct/first person" -> no smoothing; bit 0x1000, 0x80000 ...) |

Integration (no libm; plain floats): `pitch += cam9c * dt * 2.2 * ...`; `yaw_off += (cam a0 + cam a4) * dt * 2.2`; the **player
heading is rotated by `FUN_0013ee90(cam+0xa0 * dt * modifier, *(player+0x80)+0x14, 0x4e8c48)`** (axis-angle -> matrix via
`FUN_0016ffc8`, uses sinf 0x4b2518 / cosf 0x4b2430); pitch clamped at the end to
`[-limits[0]*1.0, +limits[0]*0.7]` (foot) or `[-limits[0]*0.45, +limits[0]*0.55]` (dragon). Then `Player_CameraLookTick`
applies `cam+0x84`/`+0x90` to head/torso bone matrices (`FUN_0016ffc8` at player+0x3b4/+0x390). Angles are radians; units
and 2.2 rad/s factor consistent. Camera follow/collision uses the property block "Camera Horz/Vert Distance, Tilt Factor"
(defaults in table 0x500260) elsewhere (not input related).

"Look" modes: lock-on/aim (`Input_SetAimAxisMode` 0x457390 remaps stick 2 to axis 3 or 0), dragon flight (limits 1.047),
cutscene (`DAT_004fbafc`, forces inputs off), `cam+0x14c==6` = player-controlled camera state.

## 6. Menu / UI cursor (e)

* GUI buttons come from `PadMgr_DispatchGuiKeyEvents` (0x1e3f78): table 0x4e98d0 maps Cross/R1/R2 -> id **0x100 (L-MOUSE)**,
  Circle -> **0x101 (R-MOUSE)** plus DIK ids (Up/Down/Left/Right/Enter/Esc/Home/Del/PgDn/Backspace/'\\'), delivered to the GUI
  event manager `DAT_005565d8` (vslot +0x4c keydown(id), +0x54 keyup(id|0x10000)). Menus are therefore already driven by
  "key + mouse button" events.
* `Gui_PollMouseState` (0x3af500) [H] builds a mouse state: `InputDev_GetCursorPos` (vslot +0x114) -> x,y at 0x51013c/0x510140;
  `IsActionDown(0x100/0x101)` -> flags `DAT_00510108` (bit0/1 down L/R, 2/3 pressed 4/8, 0x10/0x20 released, 0x40 sticky);
  drag-start at 0x510144; consumers at least 0x33d9d8 and 0x3b1af0. The cursor position lives in the device (`InputDev_SetCursorPos`
  0x131e88 clamps to the screen size at `DAT_005565c8+0xf8/+0xfc`; set to (W/2, H-128) when a cutscene/map starts at 0x33c660).
  Nothing in the pad path moves it, so **on PS2 this cursor is effectively unused**. **Update 2026-10-17: the GUI screens DO have a full mouse path (`FUN_002cc7c0` -> hit test, press / release on the widget); it was just never called. Now driven by `runtime/input` `MenuMouse`, see `docs/reference/mouse-menus.md`.** Circle alone is not "back" in the GUI (it is the R-MOUSE id 0x101); START (= Esc id) is. The "Mouse Cursor" sprite (resource name
  string 0x53d4a8, table entry 0x5053e8) exists. Whether the pointer is rendered and hit-tested in real menus is **not verified**.

## 7. Timing dependencies

* `PadMgr_Update` runs once per `Engine_Tick` (0x135c40; vslot +0x1c via `gp-0x7f68`), reads at most one sample per tick (ring
  supports up to 8 per tick if the poller thread ran faster). It is frame driven, not vblank driven.
* Gameplay uses `dt = *(float*)(ctx+0x24)` (seconds; written by 0x43dee8/0x43df50 from timer `ctx+0x2b0`, scaled by slow-mo
  `_DAT_00512db4`==1.0 normally). Analog axes are **rates** (multiplied by dt downstream). Mouse deltas are **angles**: do not multiply by dt.
* Double-tap windows and command timestamps use ms from `FUN_004af7f8` (0x15e = 350 ms); keep that clock monotonic.
* Stick jitter filter compares with the previous *sample*; if you fake sticks at high frame rates keep per-tick output stable.
* The game polls `scePadGetState` before every read; return 6 forever. Vibration (`SetActDirect`) can be mapped to XInput/ignored.

## 8. Hook design

Hook framework terms: *replace* (host function instead of EE function), *after* (run after original returns), *before*.

### (i) Minimal: replace the pad read, synthesize DualShock (configurable bindings)

* *replace* `scePadInit` 0x4b0948 (return 1), `PortOpen` 0x4b0ba8 (return 1; remember buf), `GetState` 0x4b0f78 (return 6),
  `SetMainMode` 0x4b1490 (return 1), `SetActDirect` 0x4b1548 (rumble -> host), `Read` 0x4b0ef8:
  for port 0 write 32 bytes `{0x00,0x73, ~(mask>>8), ~(mask&0xff), rx, ry, lx, ly, 0..}` and return 32; for port 1 return 0.
  (Simplest: `padmgr` never touches the padman IOP at all; the recompiler's IOP/RPC layer need not emulate padman.)
* bindings: keyboard/mouse button -> pad mask bit (use the scheme in 4; e.g. LMB=Square, RMB=Circle, Space=L1, ...);
  WASD -> left stick bytes (`0x00/0xff` on an axis; note ly is inverted via the `-axis2` mapping, verify sign at runtime);
  mouse delta -> right stick: the response is `pow(|v/128|,2)`, so to get linear control encode
  `v = 128*sqrt(min(1,|desired|))`; outside +-47 of centre the "jitter filter" no longer blanks, inside it blanks if the change
  between consecutive frames is <4 counts, so clamp tiny deltas to 0 or to >= 5.
  Use controller scheme C (twin-stick: rx -> axis0 turn, ry -> axis1 pitch, lx -> strafe) so mouse X turns Rynn; scheme A/B
  maps rx to camera orbit instead.
* Downsides: mouse becomes a *rate* (floaty, extra smoothing from the `cam+0xa0/+0x9c` lerps, squared curve, clamped to 1.0 rate,
  frame-rate dependent).

### (ii) Better: raw mouse angles into camera/player (no stick deadzone/accel)

*after* `Camera_UpdateLook` 0x44e4a8 (args `a0=player`, `a1=float* limits`, `a2=dragonMode`), per frame with accumulated mouse
counts `(dx,dy)` since last call, `sens` radians/count:

```
ctx   = *(u32*)0x5565d4;  camH = *(u32*)(ctx+0x4b4);  if(!camH) return;
cam   = *(u32*)(*(u32*)(camH+0xfc)+0x10);
// pitch (radians). sign: +axis1 (stick up/forward) moves pitch via cam+0x9c; verify; invertY via option
float p = *(float*)(cam+0x84) - dy*sens;
float lim = limits[0]; float hi = dragon?lim*0.55:lim*0.7, lo = dragon?-lim*0.45:-lim;
*(float*)(cam+0x84) = clamp(p, lo, hi);
// yaw: rotate player heading like the game does for stick turn (only if allowed: vcall player->+0x57c +0x174 != 0 && +0x17c == 0)
FUN_0013ee90(-dx*sens, *(u32*)(player+0x80)+0x14, 0x4e8c48);   // call the recompiled function directly
```
Because this runs after the game's own stick code with stick = 0, there is no deadzone, no squaring, no smoothing, and mouse
turn composes with any stick. Also *replace* (or force to 0) cam+0x9c/+0xa0/+0xa4 when the mouse is active to avoid drift.
Airborne/dragon uses the same function (a2=1). First-person/aim and lock-on states that read axes elsewhere (Controller_Update_*
via `Player_GatherInput` out0/out1) can be handled by *after* `Player_GatherInput` 0x44c2f8 (`a1 = float out[5]`): add
`out[0] += -dx*sens/(dt*K)` only if you need turn-based vehicle control (K = the player turn modifier; calibrate), but prefer the
direct angle path where the game exposes one.

For digital input use the command layer instead of faking pad bits where possible: *call* `InputDev_InvokeCallback`
(0x131fa8; `a0=&PadMgr 0x4e90c0`, `a1=command (keymap[id])`, `a2=actionId`, `a3=pressed`, `a4=ms`) or directly `Game_OnInputCommand`
(0x23ce18). Because `Options_ApplyButtonScheme` installs the **PC default keymap**, host keys can be sent as DIK scancode action
ids (`cmd = *(u16*)(0x4e90c0+8+2*dik)`), always sending both press and release (the handlers use release to clear
`player+0x504/+0x510` hold flags; double-tap logic uses `a4`). For analog movement keep W/S/A/D as sticks via (i) or call
`InputDev_SetAxis`/`AddAxis` (vslots +0x64/+0x6c on 0x4e90c0) from an *after* hook of `PadMgr_Update` 0x1e3970 (axes are cleared at
the start of each sample, so writing after Update is the right place; `Player_AxesToDigitalCommands` will then emit the matching
digital move commands automatically). Mouse buttons: ids 0x100/0x101/0x102 can be added to the keymap with
`InputDev_SetKeymap` (0x131e60) (e.g. 0x100 -> 0x4006 attack, 0x101 -> 0x4007 block - names guessed, confirm).

### (iii) Menus / cursor

Feasible but needs runtime confirmation: after `PadMgr_Update`, call `InputDev_SetCursorPos` (0x131e88, vslot +0x10c) with the
host mouse position scaled to the game framebuffer (`DAT_005565c8+0xf8/+0xfc`), and press L/R mouse by sending key events 0x100/0x101
to the GUI event manager (`DAT_005565d8` vslots +0x4c/+0x54, `id|0x10000` for release) and by setting pad bits so that
`IsActionDown(0x100/0x101)` (bitmask `padmgr+0x444` bits 0/1) is true for `Gui_PollMouseState` consumers. Keyboard menus work today by
sending the DIK ids in table 0x4e98d0 (Up/Down/Left/Right/Enter/Esc). Unknown: which screens read `DAT_00510108`/the cursor and
whether they draw/hit-test the "Mouse Cursor" sprite; instrument 0x3af500 (`before` hook log) and 0x33d9d8 / 0x3b1af0.

## 9. Open items / risks

1. Sign conventions of axis0/axis1 -> player turn / pitch need one runtime check each (log `cam+0x84`/heading vs stick).
2. Which of 0x271e18/0x274c18/0x311220 is Rynn vs Arokh vs other (all call `Player_GatherInput`); command ids 0x4006..0x403f roles.
3. `FUN_0013ee90` 3rd arg `0x4e8c48` is zero in the ELF image; it is an up-vector filled at runtime (read it before calling).
4. Locking states (`DAT_004fbafc`, `cam+0x48` bits, `player+0x90` flags) can ignore mouse input; same gating as the stick path
   should apply (use the same conditions as in `Camera_UpdateLook`).
5. Static analysis: ~12k functions; non-virtual callers of `PadMgr_Update`/`Game_OnInputCommand` are indirect (vtable/callback), so
   xref counts in Ghidra read 0 for them.

## 10. Reproducing the analysis

`analysis/input_run.sh [-w] Script.java args...` (run from `decomp/`; `-w` writes to the project) wraps `analyzeHeadless`.
Scripts (`analysis/ghidra_scripts/`): `InputDecompile.java <outdir> addr...` (C + callers/callees per function),
`InputDecompAll.java <outdir>` (every function, ~19 MB; used for the greps), `InputXrefs.java <outfile> addr...`,
`InputRename.java names.csv`. Helper Python: `analysis/input/elf.py` (read ELF by vaddr), `scanref.py lo hi` (finds
lui/addiu/lw references into an address range, needed because Ghidra has no xrefs for string/table addresses here).
Note: paths passed to the headless launcher must not contain spaces; use paths relative to `decomp/`.

## 11. Runtime verification (recompiled game, 2026-10-16)

Everything below was measured in the running native port with scripted host input (`DRAKAN_INPUT_SCRIPT`, see `runtime/input/README.md`):
player position = floats at `*(player+0x80)+0x14` (x,y,z; y is height), heading = rows at `*(player+0x80)+0x20`, camera pitch `cam+0x84`.
`player` is `$a0` of `Camera_UpdateLook`; its address changes between runs (0x7c.... - 0xbd....).

| Question | Result |
|---|---|
| `FUN_0013ee90(float angle, vec3* pos/matrix, vec3* axis)` calling convention | **EABI**: angle in `$f12`, `$a0 = *(player+0x80)+0x14`, `$a1 = 0x4e8c48` (`turn_call_abi = fpr`, the default, works: 400 mouse counts x 0.002 = exactly 0.80 rad of heading change). The function checks `$f12 == 0` itself and calls `FUN_0013c888` first. |
| Value at `0x4e8c48` | `{0.0, 1.0, 0.0, 0.0}` (floats `00000000 3f800000 00000000 00000000`): the **up axis**. The ELF image has zeros there; the game fills it during start-up. |
| Mouse X sign | `yaw_sign = -1` is right: positive `dx` (mouse right) turns the player right, the same direction as the `D` key (heading angle `atan2(row0.z,row0.x)` decreases; `A` increases it). |
| Mouse Y sign | `pitch += pitch_sign*dy*sens`. The game's stick convention (option "invert Y" `*(0x4fb6d0+0x64)` = 0 by default, scheme A `ry -> axis1`, stick up = byte < 128 = negative axis = pitch decreases) makes **`pitch_sign = +1`** (mouse up -> pitch decreases = look up, mouse down -> look down) the default. The 3D world renders now (section 4e of docs/status/recomp-status.md; `build/screenshots/world_neutral|lookdown|lookup|lookup2|walking|turned.png`, script `build/input_tests/nb5.txt`), but a *visual* confirmation of the pitch direction is still inconclusive: as soon as the camera leaves the default orientation polygons that cross the frustum come out as large wedges (open VU1 clipper issue, RECOMP_STATUS 4e), so look-up/look-down frames are dominated by artifacts. The numeric derivation stays: nothing in the screenshots contradicts it (the default view looks slightly downward at the player; after the mouse-up phases the sky shows at the top of `world_turned.png`). |
| Pitch behaviour | The game **re-centres `cam+0x84` within ~20 frames** when no stick is held (Camera_SoftLimitAxis); the module therefore keeps the mouse pitch for `pitch_hold_ms` (default 1200 ms) after the last vertical movement and continues from its own value. |
| Left stick | `W` -> left-stick Y byte < 128 moves the player; `S` moves back (position along the heading reverses). `A` / `D` **turn** (scheme A: `lx -> axis0 = turn`, options `stickCfg = {0,-2,5,1}`, scheme 0 = A, sensitivity 0.8). Use controller scheme C in Options for strafing on A/D. |
| `W`/`S` speed | Full deflection after pre-inverting the squared response (`curve_compensation`): ~85 position units per frame at 40 fps. |
| Walk / run | `LSHIFT = WALK` scales the keyboard left stick by `[keyboard] walk_scale` (default 0.5) and sends native command `0x4010` down/up through `InputDev_InvokeCallback`. That command sets/clears bit 1 of `0x4fbb34`, selecting the native walking pace; analog scaling alone leaves the running profile active. Settings > Controls > Keyboard movement > **Toggle run** (`[keyboard] toggle_run = 1`, default 0) makes each press switch between running and walking. It starts running and resets to running on menus, focus loss, settings ownership and config reload. A physical left stick and Arokh retain their native gait selection. The speed slider scales stick input, not an exact fraction of world movement speed. |
| Jitter filter dither | Not needed for the keyboard (full-deflection bytes are outside +-47); the dither is only exercised by small stick-mode values. |
| Command meaning (default scheme A, measured by logging `Game_OnInputCommand` while tapping each pad button) | Triangle -> **0x4011** (Jump: player y rises ~700 units and returns over ~45 frames), Cross -> 0x4012 (Crouch), Square -> **0x4006** (Attack), Circle -> **0x4007** (Magic/Parry), L1 -> 0x4009 (Call Arokh/Use), L2 -> 0x400c (Next Target/Strafe), R1 -> 0x403a (Quick Inventory), R2 -> 0x400b (Toggle Targeting), R3 -> 0x400a, Select -> 0x4030 (Inventory/Map), L3 -> none (cmd 0). Action ids are 0x120.. (`a1` of the callback: Select 0x121, Triangle 0x122, Cross 0x123, Square 0x124, Circle 0x125, L1 0x126, L2 0x127, R1 0x128, R2 0x129, R3 0x12b, L3 0x12c). The earlier guesses in section 4 (0x4011/0x4012 "sidestep") are superseded: the Controller screen labels say Jump/Crouch. |
| Default ini bindings | `Space` = Triangle (jump), `Ctrl`/`C` = Cross (crouch), LMB = Square (attack), RMB = Circle (magic/parry), `E` = L1, `Q` = R1, `F`/MMB = R2, `R` = L2, `Tab` = Select, `M` = VIEW_MAP (full-screen map, `[menu] map_view`; added to older files that do not mention M), `I` = VIEW_STRIP (Equipment strip along the bottom, `[menu] inventory_strip`; an older file's `I = ACT:0x17` becomes it; both in `docs/reference/mouse-menus.md` section 12), `Esc` = Start. Menus: Enter/Space = Cross, `E` = Square, `Q` = Triangle, `[`/`]`/PgUp/PgDn = L1/R1, Backspace = Circle, `Esc` = Circle + Start. On the inventory / map (screen 0x15) a keyboard/mouse-bound Start is sent as **Select** instead (the screen closes with Select; Start opens the pause menu over it), and stays Select until the key is released so the frame between the screen closing and the layer switch cannot raise a Start edge (`InputModule::fillPadSample`). A controller's own Start is unchanged. |
| Menu / gameplay detection | `Camera_UpdateLook` is only called while the player camera runs: "gameplay" = it was called in the last 3 `PadMgr_Update` frames, otherwise "menu" (titles, cutscenes, pause menu). The module uses it to release the cursor and to switch to `[menu_bindings]`. |
| Which gather-input caller is Rynn vs Arokh | Not determined (Level 0 and the following area are on foot; the dragon is not reachable by script yet). `Camera_UpdateLook`'s third argument (`dragonMode`) selects the pitch limits and is passed through unchanged. |
| Guest call from a hook | `PS2Runtime` runs a recompiled function with `ctx->pc == entry` and expects `pc = $ra` when it returns; a nested call therefore uses a **copy of the context**, `pc = entry`, `$ra = 0` (0 = "finished"), and a thread-local "no preemption" guard in `EeScheduler::checkpointDue` (a callee that unwinds at a checkpoint would otherwise leave a resume pc nobody continues). |

## 12. Controls v2: Order-of-the-Flame style controls (2026-10-17, branch `drakan-ctl`)

Goal: mouse look / strafe like Drakan: Order of the Flame (PC, 1999): mouse X turns Rynn (camera follows behind her), mouse Y looks up / down and the camera stays tilted,
W / S forward / back, A / D **strafe**, Space jump, Ctrl / C crouch, LMB attack, RMB magic / parry (the PS2 game has no separate "block"). Everything below was measured in the
running port with real `SendInput` (mouse_event / keybd_event) and `PrintWindow` screenshots (`build/screenshots/r4_ctl_*.png`).

| Item | Result |
|---|---|
| **Pitch sign (CORRECTION of section 11)** | A **negative** `cam+0x84` looks **down** (screenshot: pitch -0.628 = grass filling the view, +0.44 = sky). The old derivation ("stick up -> pitch decreases = look up", `pitch_sign = +1`) was wrong, so the mouse was inverted. Default is now `pitch_sign = -1` (mouse up = `dy < 0` raises the pitch). Old ini files are upgraded (`pitch_sign = 1` -> `-1`). The foot limits agree: `[-lim, +0.7 lim]` = more range down than up. |
| Yaw | unchanged and verified again visually: mouse right turns right (the scene moves left; 300 counts x 0.002 = 0.6 rad of heading, `FUN_0013ee90`). Raw counts -> angle, no acceleration, no smoothing, independent of the frame rate (the Raw Input deltas are accumulated between frames). |
| Why the pitch snapped back | `Camera_UpdateLook` runs in two modes selected by `InputDev` vslot `+0x3c(2)` = `*(PadMgr + 0x4d4)` (set to 1 in the PadMgr constructor): non-zero ("stick position mode", `uVar7 = 1`) calls `Camera_SoftLimitAxis` and eases `cam+0x84` towards the position given by the stick (0 when centred) every frame; zero = pure rate mode (`pitch += cam+0x9c * dt * 2.2`, `cam+0x9c` follows axis 1), which has no re-centring. The module now clears that word (`addr_stick_mode_flag = 0x4e9594`) **only for the duration of the `Camera_UpdateLook` call** while the mouse looks (direct mode, captured, no pad right stick deflected) and restores it afterwards (pad users keep the stock behaviour). The old `pitch_hold_ms` hack (re-writing the pitch for 1.2 s) is gone. Verified: pitch -0.6283 stayed -0.6283 for 3 s with the mouse idle. |
| Strafe on A / D | Scheme A maps the left stick X to axis 0 (turn). The stick records at `PadMgr+0x498` (`{s32 axisId, float out, float scale}` per stick: lx, ly, rx, ry) are rewritten before every `PadMgr_Update`: lx -> axis 3 (the strafe axis, what scheme C uses), scale 1.0, on foot only; while the last `Camera_UpdateLook` call was in dragon mode, in menus and with `[keyboard] strafe = 0` the game's own record is put back (the options code reinstalls it, the module re-detects that). No change in Options > Controller is needed. Verified: `D` moves the player along the heading's side vector and the tent in view drifts left (`r4_ctl_s1_strafeD.png`), `A` returns. |
| Arokh flight | Camera mode (`dragonMode` argument of `Camera_UpdateLook`, limits 1.047) is exposed to the module. In flight the mouse goes through the game's own right-stick path (`[mouse] dragon_mode = stick`, rate based, same `stick_full_counts` / `invert_y` as the stick look mode) instead of the direct heading / pitch write; `direct` is available but unverified. Not reachable in the first level, so this is **not verified visually**. Which of `Controller_Update_A/B/C` (0x271e18 / 0x274c18 / 0x311220) is the dragon one is still unknown; all three call `Player_GatherInput`, and the stick mapping tables (`Options_LoadStickScheme`, air table 0x511f08) feed the same axes, so the stick path works for whichever it is. |
| Capture vs menu | New `InputModule::setMenuContext`: the mouse is released and the brass cursor drawn only while a game menu / message box (screen ids 1 2 3 4 5 8 9 10 13 0x17 0x1b 0x1c), the inventory / map, or the F1 overlay is up. Cut scenes, loading screens and gameplay (the "menu layer" for the pad bindings) keep the mouse captured and hide the cursor, even right after the mouse moved. |
| Menu clicks | The click-on-empty-area -> Cross fallback of `MenuMouse` is removed: a click that is not over a focusable widget does nothing (it activated the highlighted row on pages without widgets). |

## 13. Test runs without a save (2026-10-22)

`scripts/harness/play_run.py` starts the game from `dist/DrakanPC` on a fresh, empty memory card and reaches
controllable gameplay in any level in about 30 s, without save games and without touching the player's cards or logs:

```
python scripts/harness/play_run.py --name surdana                       # New Game -> intro -> Level 1A
python scripts/harness/play_run.py --name raven --level 4a --shots 0,300 # Level 4A - Ravenshold (Rynn on Arokh)
python scripts/harness/play_run.py --name walk --recipe scripts/harness/recipes/fps_movement_wall.txt
python scripts/harness/play_run.py --list-levels
```

| Piece | What it does |
| --- | --- |
| `DRAKAN_MEMCARD_DIR` | Memory card root (`ps2_runtime.cpp`, `configureIoPathsFromElf`). The runner points it at `build/harness/<run>/card/mc0`, so Load Game is greyed out and the title's Cross is New Game. Without it the card is `<elf dir>/mc0` (here `iso/mc0`, which holds the Surdana save the combat fixtures use). |
| `DRAKAN_AUTOPRESS` + `DRAKAN_INPUT_AUTOSTART` | Start/Cross taps through the title, the memory-card prompts and the intro cut scene. In free gameplay the taps stop (they opened the pause menu every 3 s and kept resetting the control count in slower levels). "Under control" = 100 camera-look calls and 80 gameplay frames, or 240 uninterrupted gameplay frames (mounted on Arokh the on-foot look hook is not called). |
| `DRAKAN_START_LEVEL` | New Game plays `Level 0 - Intro`, whose transition opens `cdrom0:/English/Levels/Level 1A - Surdana/Level 1A - Surdana.lvl`. The HLE file service rewrites that level name in every opened path (`FileService::setLevelRedirect`), so the engine loads another level under the original name; it even builds the follow-up paths (`<folder>/<level>_splats.*`) through the redirect. Verified: Level 1B Kragmor, 2 Shadowmire, 3B Crystal Mine, 4A Ravenshold. Saves made in such a run carry the Surdana name. Level names: `play_run.py --list-levels` (strings in the `.lvl` files on the disc). |
| `shot <label>` | Script command: besides the old desktop BMP (invalid for hidden / covered windows) it asks the renderer for the next presented image (`gsr::requestWindowShot`) and writes `<DRAKAN_SHOT_DIR>/<label>.png`. |
| `DRAKAN_LOG_DIR` | `drakan.log` goes to the run folder; the runner keeps the full log as `stdout_<attempt>.log` and retries when the script never reaches `quit`. |

Not done: placing Rynn at an arbitrary position at run time. Writing the render transform (`*(player+0x80)+0x14`) and
the two physics positions in the player object (`+0xbe4`, `+0xc20`) moved her only part of the way and she drifted
afterwards; a RAM search found the position in at least ten more places (`0x56c738`, `0x996a64`, `0xa58fe8`,
`0xca12cc`, ... in Surdana). A working teleport needs the engine's own relocation routine (level-start / transition
code, e.g. around `0x362d38`, the level-transition function that also prints `NEW LEVEL %s`). Until then, positions
come from save fixtures (`combat_save_fixture.py`, used with `play_run.py --save <card>/mc0`).

## 14. Jump direction (2026-10-23)

Space / Triangle (`0x4011`) -> `FUN_002f90c8`: a strafe bit in `player+0x508` gives a side jump; otherwise the jump is
a **running jump (type 2) only if the measured forward speed `player+8 > 1000`**, else a standing jump straight up
(type 1, `player+0xaf0`). The speed comes from `FUN_00325d30`, which assumed a 30 Hz world; at the uncapped / 60 world
rates it under-reported speed by `30 / rate` and running jumps went straight up. Fixed by a rate correction in
`drakan_frame_rate.h` (see `docs/reference/perf.md`, "World-rate velocity correction", and
`docs/journal/2026-10-23/running-jump.md`). The harness `[in-pos]` log line carries `spd=` (`player+8`),
`mv=` (`+0x504/+0x508/+0x50c` move bits) and `jt=` (jump type) for this.

Jumping during a melee swing keeps the attack on an animation layer (`player+0xaec`, `ab0` bit 2) over the jump; at
60 / uncapped the in-air switch froze that layer in the jump pose (`docs/journal/2026-10-25/attack-jump-freeze.md`,
fixed in `drakan_frame_rate.h`). The `[in-anim]` line next to `[in-pos]` logs that layer's record (`anim`, `spd`, `t`,
`rf` flags), the base layer 0 (`base=anim/speed/time/flags`) and the player flags `+0x90/+0x94/+0xab0/+0xab4/+0xb28`.

Stick-mode mouse (`[mouse] dragon_mode = stick`, `look_mode = stick`): `stick_full_counts` is counts per 1/30 s, not
per update. `FrameInput::frameSeconds` (host time between `beginFrame` calls, one world tick) scales it, so the same
physical mouse motion turns Arokh the same amount at any world rate (`docs/journal/2026-10-23/world-rate-audit.md`).

## 15. Mouse feel review: camera rubber band and pitch gain (2026-10-24)

Review of why direct mouse look did not feel like a PC game (Order of the Flame target). The input path itself is
sound: Raw Input counts accumulate between world ticks, there's no acceleration or smoothing, the native rates
`cam+0x9c/a0/a4` are cleared, and the turn reaches the camera in the same tick (trace: `44de40` -> `13ee90` ->
`41f848`). What was left came from the camera.

Camera chain: the camera object handler `FUN_002180b0` (msg 4) -> tick `FUN_00218350` -> `case 6`: `FUN_0041f848`
(cam+0x48 bit5 clear) or `FUN_0032a608` (bit5 set, a view attached to player +0x54c/+0x558; probably aim, not
confirmed).

| Finding | Detail |
| --- | --- |
| Orientation snaps | `0x41f848` copies the player matrix, rotates it (yaw `cam+0x90`, pitch `cam+0x84`, auto-tilt `cam+0xb4`, collision tilt `cam+0x214`) and writes it to `*(cam+0x148)+0x20..0x40`. The exception is while `cam+0x21c > 0`, when it blends with `cam+0x210 * dt`. |
| **Position lags (rubber band)** | `cam+0x210 += min(1, dt * prop[2]) * (cam+0x20c - cam+0x210)`, with `cam+0x20c = prop[1]` (x0.75 into both after a collision push). Then `k = min(1, cam+0x210 * ctx+0x3c)` (8-frame average dt), or 1 with flags 0x80 / 0x20000, and `pos += (target - pos) * k`. Camera properties (class table `0x509b28`): 1 Rubber Band Strength **2.0**, 2 Rubber Band Acceleration 0.4, 8 / 9 AutoTilt Acceleration / Max Velocity 2.5 / 0.65. A ~0.5 s time constant: the view turns at once, then the camera body slides round behind Rynn. |
| **Pitch gain 1.8** | Plain on-foot branch (`cam+0x168 == +0x170`, `cam+0x1c4 == 0`, bit25 clear, player94 bit2 clear): yaw rotate `-(0.9 / -0.5) * cam+0x90`, pitch rotate `1.8 * cam+0x84 - cam+0xac`. The heading reaches the view 1:1, so equal mouse counts tilted the view 1.8x as far as they turned it. The view range is `1.8 * [-0.628, +0.44]` = about -65 / +45 degrees. Other gains: player94 bit2 1.0; target swing branch 0.8 (0.3 in the alternate channel). |
| Auto-tilt | Updates only while `cam+0x84 == playerObj+0x110`, so it holds its last value while the mouse owns the pitch. |

Changes (`runtime/input`, glue hook on `0x41f848`, `DRAKAN_INPUT_SKIP` bit 32 disables it):
`[mouse] camera_follow = 5` (while mouse / keyboard steer on foot, the camera's previous position is turned round
Rynn's anchor by her heading and view-pitch change before each tick, and its travel eases at 8/s with the native collision handling;
0 = native). A rigid follow made the camera inherit every uneven gait step and one-tick collision hit
(microstutter) and is not used and `[mouse] pitch_view_gain = 1.8` (vertical counts are divided by the plain-branch gain, 1 = old
ratio). F1 > Controls > Camera follow edits `camera_follow`. Details and limits: `runtime/input/README.md`,
`docs/journal/2026-10-24/mouse-camera-follow.md`.

## 16. Modern bow aiming (2026-10-24)

`[mouse] bow_aim = modern` (default; `classic` = the game's own bow), `[mouse] bow_zoom = 0.85`, F1 > Controls >
"Modern bow aiming". Code: `runtime/input/src/BowAim.cpp`, glue hooks in `drakan_input_glue.cpp`, crosshair in
`drakan_settings_host.cpp` (`SettingsOverlay::drawCrosshair`). `DRAKAN_INPUT_SKIP` bit 64 disables the launch hook.

Native bow (static analysis, live-checked where noted):

| Piece | Detail |
| --- | --- |
| Bow out | `player+0xe90 == 5`; the bow sits in the `player+0xEAC` channel (live: e90 5, EAC 0x4c0 after equipping the Short Wood Bow). |
| Sight (Circle) | `FUN_003016e8` toggles it: camera flag `cam+0x48` bit 5 (0x20), view `FUN_0032a608` from the bone frame at player+0x54c/+0x558, FOV target `cam+0x60` = bow prop 0x1e (current `cam+0x5c`, eased by the camera tick `FUN_00218350` at camera prop 6 x dt), reticle `FUN_003ac540`. Flag 0x800 / `cam+0x94/98` is not the sight. |
| Shot (Square) | Press sets attack bit 2 (`0x307198`); `FUN_003016e8` plays the draw animation 0xe1 (0xe7 crouched). The arrow leaves on the animation event, not on button release: `FUN_0020e6c8(bow)` launches `*(bow+0x158)` and sets the cooldown `player+0xb38`. |
| Launch direction | `FUN_0020e6c8`: with a locked target (`player+0x4da`) or a homing arrow (prop 0x10) it solves a ballistic direction and rewrites the arrow matrix (column 0 = -dir); otherwise the arrow flies along its own matrix (the hand pose). Live: an arrow whose column 0 was set to -d flew exactly along d with no visible drop over 7400 units. |

Modern mode: with the bow out (on foot, gameplay, sight closed, mouse / keyboard owner), LMB (when bound to SQUARE) no
longer reaches the pad. Holding it aims: `cam+0x60` x `bow_zoom` (restored on release unless the game set its own),
camera target offset `cam+0x10c/+0x114` 200 units along the camera's right axis (over the shoulder; the native easing
slides the camera), and a host crosshair at the centre of the game image. Release sends one Square tap (2 frames).
Within 120 frames, the launch hook points the arrow from its position at the camera's centre ray 8000 units ahead,
keeping the matrix handedness; a locked target keeps the native ballistic aim. Live (Surdana, fixture-bow-item): FOV
80 -> 68 while aiming and back, Square only on release, the arrow converged on the centre ray (101 -> 36 -> 20 units
off it) and stuck where the ray met the ground. Not yet live-checked: the shoulder offset and the crosshair overlay
(renderer screenshots do not include the overlay layer).

## 17. Quick-slot hotkeys (2026-10-25)

`[bindings] 1 = QUICK1` .. `5 = QUICK5` (actions `QUICK1..QUICK9`, `[bindings]` only; key / mouse sources). Key N uses or
equips the Nth item of the game's own Quick Inventory: weapons are drawn, potions drunk, as the native bar's confirm.
Items are marked with Cross in the native inventory or F on the modern Tab screen / I strip (slot record `+4`).

* **Order** = the native bar's list (`ctl+0x214`, `ctl = player+0x584`): the hot-slotted items column by column, top
  to bottom (cells 0, 8, 16, 1, 9, ...), rebuilt when the inventory closes. The Tab screen and the strip number their
  quick-slot pills in that order (`gameui::quickSlotItems`).
* **Route**: `QuickSlotDriver` (`runtime/input/include/input/QuickSlots.h`, glue `quickSlotsTick` + `hookPadRead`)
  sets the bar index `ctl+0x220` and overlays the scheme's Quick Inventory and Attack pad buttons (resolved from the
  live table at PadMgr+0x740 and the keymap, never Start / Select) on the next samples, so the use runs in the native
  PadMgr dispatch on the EE thread; no guest call from the input tick. A refused open (weapon swap / drink playing) is
  retried 6 times, 15 frames apart.
* **Gates**: free gameplay only (no GUI screen, cut-scene flag `0x4fbafc`, inventory open, Arokh, focus loss or
  suspension); an empty slot, a running summon or a busy controller does nothing; an already equipped weapon is left
  alone (the native Use would put it away); a press while one runs (key repeat) is ignored. Swimming / climbing are
  refused by the native `0x403a` handler.
* **Migration**: `Config::migrate` turns `ACT:0x02..ACT:0x06` (the old defaults: keymap commands `0x403b..0x403f`
  that the game never handles) into `QUICK1..QUICK5` on whatever source carries them; the F1 Controls page lists
  "Quick slot 1..5". 6 = `ACT:0x0e` stays the keymap's Quick Inventory (`0x403a`, the same as Q / R1).
* Log: `[quick-slot] slot N -> cell c, item h` ... `confirmed` / `empty` / `already equipped` / `refused`.

Details, native fields and the live runs: [quick-slot-hotkeys.md](../journal/2026-10-25/quick-slot-hotkeys.md).
Also verified in game ([ui-test-pass.md](../journal/2026-10-25/ui-test-pass.md)): a key during a club combo and while
aiming the bow (LMB held; the release then gives one attack with the new weapon), controller schemes B and C, the hot
slots after save -> load (the first press after the load equips), keys ignored on the Tab screen, the strip, the map and
the pause menu, and in water (no quick items can be set there; keys report empty). Not run: climbing, a running summon,
a cut scene.
