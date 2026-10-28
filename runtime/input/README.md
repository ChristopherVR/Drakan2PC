# Drakan input module (mouse + keyboard + XInput)

Standalone C++20 static library `drakan_input` (no PS2Recomp dependency). The runtime owner plugs it into
`drakan_overrides.cpp` through a small `IGuest` adapter. The platform-independent core also
builds on Linux and WebAssembly: `drakan_portable_camera.cpp` reuses its camera hooks while
the portable host sampler delivers pad buttons. Win32 Raw Input/XInput and Windows overlays
remain separate. Static analysis background: `docs/reference/input.md`.

```
include/input/DrakanInput.h    integration API (InputModule + global wrappers init/beginFrame/fillPadSample/...)
include/input/GuestInterface.h IGuest: read/write guest memory, call guest function
include/input/Config.h         drakan_input.ini model + parser
include/input/PadSynth.h       pure logic: keys/mouse/XInput -> 32-byte pad sample, command edge events
include/input/HostInput.h      Win32 Raw Input + XInput (dynamic) + cursor capture
include/input/FrameInput.h     per-frame snapshot + thread-safe accumulator
tests/input_test.cpp           unit tests (fake guest)
tests/*_tests.cpp              more CPU tests (menu mouse); all run via ctest, no game launch
tests/live_window.cpp          interactive window printing live pad samples / mouse deltas (input_live.exe)
```

Build (VS 2022 bundled CMake): `cmake -S decomp/runtime/input -B decomp/build/input -G "Visual Studio 17 2022" -A x64`
then `cmake --build decomp/build/input --config Release`; run `decomp/build/input/Release/input_test.exe`.
Outside the standalone build just `add_subdirectory(.../runtime/input)` and link `drakan_input`.

## What it does

1. **Pad synthesis** (`fillPadSample`): the `scePadRead` replacement. Sample = `{0x00, 0x73, ~btnHi, ~btnLo, rx, ry, lx, ly, 0...}`
   (buttons active low, 32 bytes). Keyboard/mouse buttons become DualShock buttons; WASD becomes the left stick
   (8-way, diagonals scaled by 1/sqrt2, WALK key scales by `walk_scale`). Because the game squares every stick
   (`pow(|v/128|,2)`), keyboard values are pre-inverted (`v = 128*sqrt(x)`) so W gives full speed and a diagonal
   gives 0.707 per axis after the curve (`curve_compensation`). The game's jitter filter (bytes within +-47 of centre
   that changed < 4 counts are zeroed) is defeated by dithering the byte by 4 counts. An XInput pad is merged in
   (buttons OR-ed, a deflected stick overrides, real-pad encoding).
2. **Direct mouse look** (`afterCameraUpdateLook`): raw counts to angles, bypassing the stick curve/smoothing. Pitch is
   written to `cam+0x84` and clamped to the game's own limits (foot `[-lim, +0.7 lim]`, dragon `[-0.45 lim, +0.55 lim]`,
   `lim` = `limits[0]`, fallback 0.6283 / 1.0472), yaw by calling `FUN_0013ee90(angle, *(player+0x80)+0x14, 0x4e8c48)`.
   Camera object chain: `ctx=*(0x5565d4); camH=*(ctx+0x4b4); cam=*(*(camH+0xfc)+0x10)`. The same two vcall gates as
   `Camera_UpdateLook` (`player+0x57c` vtable `+0x174` must be nonzero, `+0x17c` must be zero) guard the turn.
   In direct mode the mouse contributes nothing to the synthesized right stick (no double application); keyboard
   `RSTICK_*` bindings and a physical pad's right stick still work. Delta is consumed once per frame, so the two
   callers of `Camera_UpdateLook` cannot apply it twice.
3. **Command injection** (`afterPadMgrUpdate`): `ACT:<id>` / `CMD:<id>` bindings call `InputDev_InvokeCallback`
   (0x131fa8) `(0x4e90c0, cmd, actionId, pressed, ms)` with `ms` from 0x4af7f8; `ACT` resolves the command via the
   device keymap `u16[512]` at `0x4e90c0+8`. Press and release are always both sent.
4. **Menu cursor** (`menu.cursor`, `menu.clicks`, default OFF): pushes the host cursor into `InputDev_SetCursorPos` via
   vslot `+0x10c`, scaled to the framebuffer size at `*(0x5565c8)+0xf8/+0xfc`; clicks go to the GUI event manager
   (vslot `+0x4c` down(0x100/0x101), `+0x54` up(id|0x10000)) and set `padmgr+0x444` bits 0/1.
5. **Capture**: cursor hidden + clipped to the client rect while playing. Released on focus loss, F10 (toggle),
   actual menu context, and overlay suspension. Esc toggles capture only with `release_on_esc=1` (default 0).
   A left click restores capture after focus loss; that click and its held drag are consumed until release.
   A subsequent click attacks normally. Menu clicks still reach native hit testing after focus restoration.
   While the cursor is free in gameplay, mouse buttons/wheel/deltas are ignored. Suspension replaces cached
   pad samples with neutral input immediately; explicit host-injected taps retain their suspension exemption.

## Default bindings (drakan_input.ini, created next to the exe if missing)

The defaults follow the game's own **Standard controller scheme** (Options > Controller screen; measured command ids in `docs/reference/input.md` section 11):

| Input | DualShock button | What it does in the game |
|---|---|---|
| W A S D | left stick | W/S forward/back; **A/D strafe** (`keyboard.strafe=1`, default; 0 restores native mapping) |
| Left Shift | WALK | native walk command + left stick x `walk_scale` (0.5); hold by default, press to switch walk/run with `[keyboard] toggle_run = 1` |
| Mouse move | look | direct: camera pitch + player yaw (mouse right = turn right, mouse up = look up) |
| Mouse1 (LMB) | SQUARE (0x4006) | Attack |
| Mouse2 (RMB) | CIRCLE (0x4007) | Hold bow sight / Use Magic / Parry |
| Mouse3 (MMB), F | R2 (0x400b) | Toggle Targeting |
| Space | TRIANGLE (0x4011) | Jump |
| Left Ctrl, C | CROSS (0x4012) | Crouch |
| E | L1 (0x4009) | Call Arokh / Use |
| Q | R1 (0x403a) | Quick Inventory |
| R | L2 (0x400c) | Next Target / Strafe |
| V | R3 (0x400a) | |
| Tab | SELECT (0x4030) | Inventory / Map |
| Esc | START | Game Menu (pause) |
| 1 2 3 4 5 | `QUICK1..QUICK5` | use / equip the 1st..5th quick-slot item through the native Quick Inventory bar |
| 6, wheel | `ACT:` ids | Quick Inventory (0x403a), prev/next weapon through the shipped PC keymap |
| Arrow keys | D-pad | Special Moves |
| XInput | A=Cross B=Circle X=Square Y=Triangle LB/RB=L1/R1 LT/RT=L2/R2 Back/Start, sticks, D-pad | merged with keyboard/mouse |

**Enter / keypad Enter = CROSS in the gameplay layer too** (confirms info boxes / popups with an "OK" button; also Crouch like Ctrl if no popup is detected). Files written before this
get the two bindings added at load time (`Config::migrate`) unless Enter is bound to something else. The glue also switches to the menu layer automatically whenever the GUI screen manager
(`0x4fb600`, active screen at `+0xc`, screen id at `screen+0x18`) has an active screen (ids seen: 8 title, 4 load, 0x17 pause, 1/2/3/0x1b message and yes/no boxes, 0x1c memory-card warning,
0x1d / 0x1e movies): real menu contexts release the cursor. Enter / Space confirm; MenuMouse routes left clicks through native hit testing. `DRAKAN_INPUT_SCREEN_LAYER=0` disables the automatic switch.

Menu layer (`[menu_bindings]`, active whenever the game is not running its player camera: title, pause, inventory, cutscenes):
arrows / WASD = D-pad, Enter / Space = CROSS (confirm), E = SQUARE, Q = TRIANGLE (inventory + store actions),
`[` `]` / PgUp / PgDn = L1 / R1 (inventory sections), Backspace / RMB = CIRCLE (back), **Esc = CIRCLE + START** (back, and resumes from the pause menu), Tab = SELECT.
The button-prompt generator (`runtime/ui`) shows the first key bound to every button, so keep rebinding in this file.

Tunables (`[mouse]`): `sensitivity_x/y` (rad per count, 0.002 in direct mode), `invert_x/y` (both default 0),
`yaw_sign` and `pitch_sign` (both default -1), `pitch_persist` (default 1; `pitch_hold_ms` is obsolete),
`stick_full_counts` (40 counts/frame), `stick_sign_x/y`, `camera_follow` (8 /s, 0 = the game's rubber band) and
`pitch_view_gain` (1.8, 1 = off), both described under "Camera follow and pitch gain" below. `look_mode = direct|stick` belongs in `[general]`.
Direct look applies raw angles without a dt multiplier; stick mode remains a native rate path with smoothing and saturation.
Nonfinite numeric settings are rejected with a warning; the preceding/default value survives.
Explicit legacy signs, inversion and sensitivity are preserved during the version-marker upgrade, including positive `pitch_sign`.
`[advanced]` holds guest addresses and `turn_call_abi = fpr|gpr` (fpr verified).
`[general]`: `release_on_esc` (0; the cursor is released automatically whenever the game is not in gameplay), `toggle_key` (F10).
`[menu]`: `mouse = 1` (default) drives the game's own menus: hover moves the highlight, left click confirms, wheel = up/down (slider: left/right), right click = back (`START | CIRCLE`); `draw_cursor`, `cursor_idle_ms`; the legacy `cursor` / `clicks` stay off. Details: `docs/reference/mouse-menus.md`, code `src/MenuMouse.cpp`. `InputModule::setSuspended` (overlay owns the input), `setMenuView`, `injectPadTap`.

## Wiring into drakan_overrides.cpp

The real API (`PS2Runtime::replaceFunction(addr, void(*)(uint8_t* rdram, R5900Context*, PS2Runtime*))`, `lookupFunction`,
`getRegU32`, `SET_GPR_U32`, `ctx->f[]`, return = `ctx->pc = $ra`) is used below. An "after" hook = fetch the original with
`lookupFunction` *before* replacing, call it, then do the extra work. Save `$a0..$a2` first: the original clobbers them.

```cpp
#include "input/DrakanInput.h"   // add decomp/runtime/input/include to the include path, link drakan_input

namespace {
using namespace drakan_input;

// IGuest on top of rdram + a context. call() runs on a COPY of ctx so the hooked function's state is untouched.
class RecompGuest final : public IGuest {
public:
    RecompGuest(uint8_t* rdram, R5900Context* ctx, PS2Runtime* rt) : rdram_(rdram), ctx_(ctx), rt_(rt) {}
    uint8_t read8(uint32_t a) override { return rdram_[a & PS2_RAM_MASK]; }
    uint32_t read32(uint32_t a) override { uint32_t v; std::memcpy(&v, rdram_ + (a & PS2_RAM_MASK), 4); return v; }
    void write8(uint32_t a, uint8_t v) override { rdram_[a & PS2_RAM_MASK] = v; }
    void write32(uint32_t a, uint32_t v) override { std::memcpy(rdram_ + (a & PS2_RAM_MASK), &v, 4); }
    uint32_t call(const GuestCall& c) override {
        // VERIFIED version (see drakan_input_glue.cpp): the callee wants pc == its entry, returns by storing $ra into pc.
        auto tmp = std::make_unique<R5900Context>(*ctx_);          // R5900Context is large: keep it off the stack
        for (int i = 0; i < c.ni; ++i) SET_GPR_U32(tmp.get(), 4 + i, c.i[i]);   // $a0.. = $4..
        for (int i = 0; i < c.nf; ++i) tmp->f[12 + i] = c.f[i];                  // $f12.. (EABI floats)
        SET_GPR_U32(tmp.get(), 31, 0);  tmp->pc = c.addr;  tmp->in_delay_slot = false;   // $ra = 0: "finished" marker
        ps2xNoPreemptEnter();                                       // thread-local guard checked by EeScheduler::checkpointDue
        for (int g = 0; g < 64 && tmp->pc != 0; ++g) rt_->lookupFunction(tmp->pc)(rdram_, tmp.get(), rt_);
        ps2xNoPreemptLeave();
        return getRegU32(tmp.get(), 2);
    }
private:
    uint8_t* rdram_; R5900Context* ctx_; PS2Runtime* rt_;
};

PS2Runtime::RecompiledFunction g_origPadMgrUpdate, g_origCameraLook;

void ret(R5900Context* ctx, int32_t v) { SET_GPR_S32(ctx, 2, v); ctx->pc = getRegU32(ctx, 31); }

// libpad: the game never touches padman, so stub the library.
void hookPadInit(uint8_t*, R5900Context* c, PS2Runtime*)        { ret(c, 1); }   // scePadInit    0x4b0948
void hookPadPortOpen(uint8_t*, R5900Context* c, PS2Runtime*)    { ret(c, 1); }   // PortOpen      0x4b0ba8
void hookPadGetState(uint8_t*, R5900Context* c, PS2Runtime*)    { ret(c, 6); }   // GetState      0x4b0f78 (6 = stable)
void hookPadSetMainMode(uint8_t*, R5900Context* c, PS2Runtime*) { ret(c, 1); }   // SetMainMode   0x4b1490
void hookPadSetActDirect(uint8_t*, R5900Context* c, PS2Runtime*){ ret(c, 1); }   // SetActDirect  0x4b1548 (rumble: ignore/XInput)

void hookPadRead(uint8_t* rdram, R5900Context* c, PS2Runtime*) {                  // scePadRead    0x4b0ef8
    uint32_t port = getRegU32(c, 4), slot = getRegU32(c, 5), buf = getRegU32(c, 6);
    uint8_t s[32];
    if (!fillPadSample(static_cast<int>(port), static_cast<int>(slot), s)) { ret(c, 0); return; }
    std::memcpy(rdram + (buf & PS2_RAM_MASK), s, 32);
    ret(c, 32);
}

// One game frame == one PadMgr_Update (0x1e3970, once per Engine_Tick).
void hookPadMgrUpdate(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    beginFrame();                                   // snapshot + consume raw input
    g_origPadMgrUpdate(rdram, c, rt);               // calls scePadRead -> hookPadRead
    RecompGuest g(rdram, c, rt);
    afterPadMgrUpdate(g);                           // ACT:/CMD: bindings, optional menu cursor
}

// Camera_UpdateLook 0x44e4a8 (a0 = player, a1 = float* limits, a2 = dragonMode)
void hookCameraUpdateLook(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    uint32_t player = getRegU32(c, 4), limits = getRegU32(c, 5);
    bool dragon = getRegU32(c, 6) != 0;
    RecompGuest g(rdram, c, rt);
    beforeCameraUpdateLook(g, dragon, player);
    g_origCameraLook(rdram, c, rt);                 // sets ctx->pc = $ra when done; $ra itself is preserved
    afterCameraUpdateLook(g, player, limits, dragon);
}
} // namespace

// in applyDrakanOverrides(PS2Runtime& runtime):
//   HWND hwnd = <the window the SDL/GL backend owns>, or nullptr for a private raw-input thread;
//   (if the backend owns the window, forward its WM_* messages to drakan_input::onWindowMessage(...))
//   drakan_input::init(hwnd, "drakan_input.ini");
//   g_origPadMgrUpdate = runtime.lookupFunction(0x1e3970);  runtime.replaceFunction(0x1e3970, &hookPadMgrUpdate);
//   g_origCameraLook   = runtime.lookupFunction(0x44e4a8);  runtime.replaceFunction(0x44e4a8, &hookCameraUpdateLook);
//   runtime.replaceFunction(0x4b0948, &hookPadInit);      runtime.replaceFunction(0x4b0ba8, &hookPadPortOpen);
//   runtime.replaceFunction(0x4b0f78, &hookPadGetState);  runtime.replaceFunction(0x4b1490, &hookPadSetMainMode);
//   runtime.replaceFunction(0x4b1548, &hookPadSetActDirect); runtime.replaceFunction(0x4b0ef8, &hookPadRead);
//   (on shutdown: drakan_input::shutdown())
```

Notes for the integrator
* If the backend window is created by SDL/GLFW and you do not forward messages, pass `hwnd = nullptr`: the module
  creates its own message-only window on a thread and registers Raw Input with `RIDEV_INPUTSINK` (events from other
  processes are dropped by a foreground check). Cursor hiding/clipping then uses the foreground window of the process.
* Menu detection: call `drakan_input::setMenuActive(bool)` from wherever the runtime learns that a cursor menu / pause
  menu is showing (e.g. a before-hook on `Gui_PollMouseState` 0x3af500). Until then Esc/F10 provide manual release.
* All hooks lock one internal mutex, so scePadRead from a different EE host thread (`PadMgr_PollToRing` 0x1e49e0) is safe.
* If a guest call fails in `RecompGuest::call` (stale `ra`, delay slot state, `in_delay_slot` should be false in the copy),
  verify against how `PS2Runtime` itself invokes recompiled functions.

## Live reconfiguration (settings overlay)

```cpp
bool drakan_input::reloadConfig(const char* path);                          // global wrapper
bool InputModule::reloadConfig(const std::string& path, std::string* warnings = nullptr);
void InputModule::reloadConfig(const Config& cfg);                          // already parsed (tests, other hosts)
int  InputModule::configGeneration() const;                                 // number of swaps applied so far
```
* Thread safe, callable from the UI thread any time. The file is parsed on the **calling** thread (a missing file returns `false` and changes nothing; parse
  warnings go to stderr / `*warnings`), the new `Config` is parked and swapped in at the start of the **next `beginFrame()`** under the module mutex, i.e. between
  frames: bindings (`[bindings]`, `[menu_bindings]`), sensitivity X/Y, invert Y, look mode (direct / stick), walk scale, curve compensation, gamepad enable / dead zone,
  capture-mouse flag, toggle key, release-on-Esc.
* Kept across a reload: the capture state (`userReleased_`, focus lost, menu flag), the Raw Input host, the pad synthesis state. Commands held through the old
  bindings (`CMD:` / `ACT:`) are released (a release event is queued) and the held pitch is dropped, so nothing sticks. `[advanced]` addresses are reloaded too.
* `setMenuActive(bool)` is atomic. While active (overlay open, pause menu): gameplay bindings, mouse look (`afterCameraUpdateLook` returns false), mouse clicks as
  attack and the wheel are ignored, the mouse is released (ClipCursor freed, cursor shown), the pad uses the `[menu_bindings]` layer, and commands held in the
  gameplay layer are released on the first frame of the switch; back to gameplay the capture resumes without a stale delta. Tested in `testReloadAndPause`.
* Each frame hook uses one consistent snapshot of the flag (`layerIsMenu_`), so a `setMenuActive` from another thread in the middle of a frame takes effect on the next `beginFrame`.

## Historical runtime observations (2026-10-16; superseded where noted below)

Integration lives in `ps2recomp/ps2xRuntime/src/lib/drakan_input_glue.cpp` (called from `drakan_overrides.cpp`, CMake target `drakan::input`).
This historical list predates controls v2 and native menu hit testing. In particular its pitch sign, menu click and pitch-hold statements are superseded below.

1. Signs: `yaw_sign = -1`, `pitch_sign = +1` (the earlier default -1 was wrong; confirmed with the game's own right-stick path: stick up -> pitch goes to -lim).
2. `FUN_0013ee90` takes the angle in `$f12`, pointers in `$a0,$a1` (`turn_call_abi = fpr`).
3. `0x4e8c48` = `{0,1,0,0}` (up axis) at run time.
4. Left stick: W = forward, scheme A (A/D turn). Options default: scheme A, invert Y off.
5. Dither is only relevant to stick mode; direct mode is the default.
6. `ACT:`/`CMD:` path: `InputDev_InvokeCallback(0x4e90c0, cmd, actionId, pressed, ms)` works (the guest call helper below).
7. The guest-call helper copies the context, sets `pc = entry`, `$ra = 0` and runs until `pc == 0`; a thread-local no-preemption guard in `EeScheduler::checkpointDue` keeps the callee from unwinding at a checkpoint.
8. Menu cursor / clicks: the title menu reacts to the clicks like Cross on the highlighted entry (a click on "Options" started "New Game") and ignores the cursor position (no hover, no hit test) -> `[menu] cursor/clicks` default off, `MOUSE1 = CROSS` in `[menu_bindings]` is the working click.
9. `Camera_UpdateLook` is the only place the foot camera pitch is consumed; the game re-centres it by itself within ~20 frames (mouse pitch is held for `pitch_hold_ms`).
10. One `PadMgr_Update` per `Engine_Tick`; mouse deltas are applied once per frame (two `Camera_UpdateLook` callers cannot double-apply).

Window integration: Raw Input comes from the module's **own message-only window** (`init(nullptr, ...)`, `RIDEV_INPUTSINK`, events of other processes are dropped by a foreground check), so it works with the raylib/GLFW host window and the D3D11 child window without forwarding messages. The host window and its children are subclassed (`setCaptureWindow`) only to hide the cursor (`WM_SETCURSOR`); `ClipCursor` clips to the client rect while playing (verified with real injected input: clip rect = client rect). raylib's default exit key (Esc) is disabled (`SetExitKey(0)`).

## Test mode (no human needed)

`DRAKAN_INPUT_SCRIPT=<file>` replaces the host input by a script (`include/input/Script.h`: `key|button|mouse|wheel|pad|cursor|dump|shot|log|menu|sync|quit`, frame numbers = `PadMgr_Update` calls, `sync play <n>` waits for n consecutive gameplay frames).
Helpers in the glue: `DRAKAN_INPUT_AUTOSTART=1` (with `DRAKAN_AUTOPRESS=4000`: menu -> New Game -> skips the intro cutscene with Start+Cross until the player camera runs; the world then renders black but is fully controllable), `DRAKAN_INPUT_LOG=<n>` (`[in-pos]` position / heading / pitch every n frames), `DRAKAN_INPUT_LOGFILE`, `DRAKAN_WINDOW_POS=x,y` (also makes the window topmost, for `shot`), `DRAKAN_SHOT_DIR` (`shot` writes `input_ingame_<label>.bmp`).
Debug: `DRAKAN_INPUT=0` keeps the stock pad stubs, `DRAKAN_INPUT_SKIP=<bits>` disables hooks (1 PadMgr, 2 camera, 4 bare PadMgr, 8 no after-hook, 16 no beginFrame, 32 no camera-follow hook), `DRAKAN_INPUT_MENU=0|1` forces the mode.
The game stalls intermittently in the memory-card/level-transition dialog (needs Cross taps by wall clock, that is what `DRAKAN_AUTOPRESS` does); the test runner `build/input_tests/run.sh` simply retries.

## Controls v2: Order-of-the-Flame style (branch `drakan-ctl`)

Details and measurements: `docs/reference/input.md` section 12, `docs/reference/mouse-menus.md` sections 8 / 9.

* **Pitch sign default**: `pitch_sign = -1` (a negative `cam+0x84` looks down; mouse up raises it). Existing explicit signs are preserved; positive legacy values are not blindly rewritten.
* **Persistent pitch**: `[mouse] pitch_persist = 1` (default). `beforeCameraUpdateLook` clears the game's "stick position mode" word (`addr_stick_mode_flag`) for the native call. The independent native aim branch can still run `Camera_SoftLimitAxis`. When the glue supplies the actual player argument, vertical mouse input acquires pitch ownership only in an eligible on-foot aim state with a real target. Matching player/camera/target/control state before and after the call permits restoring pre-call pitch, then applying/clamping the raw increment, including on neutral frames. The native target anchor may change naturally; it must remain finite but is not an identity field. Documented motion notifications player90 bits6/7 and transient player94 bit8 are masked from key comparison only; guest flags stay native. Other flag changes, native pitch vetoes, alternate channels, cutscenes and target replacement still cancel ownership. No pitch is held before the first vertical mouse input. Native rates/yaw/target anchor remain untouched. A pad right stick or digital look binding retains native persistence behavior. `pitch_hold_ms` is obsolete (ignored).
* **Strafe**: `[keyboard] strafe = 1` (default): `beforePadMgrUpdate` points the left-stick X record (`addr_stick_map`) at axis 3 on foot (no change in Options > Controller needed); in dragon (Arokh) mode, in menus and with `strafe = 0` the game's mapping is restored.
* **Arokh**: `[mouse] dragon_mode = stick` (default) steers through the game's own right-stick path in flight (rate based); `direct` writes angles as on foot (unverified, the dragon is not reachable early).
* **Capture / cursor**: `InputModule::setMenuContext(bool)` (set by the glue from the GUI screen id): the mouse is released and the cursor drawn only in menus / boxes / inventory / map or while the overlay is open; cut scenes and loading keep the mouse captured.
* **Menu clicks**: a click that is not over a widget does nothing (no more Cross fallback). **Inventory / map** (screen 0x15): `[menu] inventory_mouse`, `inventory_step`, `inventory_click` (pointer travel steps the selection, wheel = sections L1 / R1, left click = Square on Equipment / Cross elsewhere, right click = SELECT).
* Old ini files get a `controls_version = 2` marker block appended (`Config::upgradeFile`); user bindings and sensitivities are never changed. Debug aids: `DRAKAN_INPUT_DEBUGKEYS=1` (F9 dumps guest RAM), script command `dump ram_<label>`.

## Mouse buttons after inventory (21 October 2026)

Right-click suppression for the inventory adapter is limited to the menu layer.
The adapter retains its last observed screen ID after closing; that cached ID
must not suppress gameplay Circle input. Free mouse combat now yields to an
explicit native target lock and to the native alternate aiming camera (flag 0x800).
The runtime no longer intercepts target requests or cancels them every camera
update, so F/MMB can toggle targeting while free combat remains enabled.

The native bow sight toggles on Circle. The mouse adapter remembers a sight
opened while RMB is held and sends a native close press on release, followed
by its release on the next tick. It does not close sights opened by a controller
or a different binding. Menus/focus loss defer a close until gameplay resumes;
a pending synthetic release still reaches the guest during suspension.

## Camera follow and pitch gain (24 October 2026)

Details: `docs/reference/input.md` section 15, `docs/journal/2026-10-24/mouse-camera-follow.md`.

* **Camera follow**: the on-foot camera tick `FUN_0041f848` (mode 6) copies Rynn's orientation every tick (the mouse
  turn shows at once), but eases the camera *position* towards its place behind her by `min(1, cam+0x210 * avg dt)`.
  `cam+0x210` follows the camera property "Rubber Band Strength" (2.0), so the camera swings round about 0.5 s
  after a turn. The glue wraps `0x41f848`. While mouse / keyboard steer on foot in gameplay, `beforeCameraFollow`
  turns the camera's previous position (`*(cam+0x148)+0x14`) round Rynn's anchor point (player `+0x520`, ~240 up)
  by her heading change and by the view-pitch change (1.8 x `cam+0x84` on foot, about the camera's right axis) since
  the last tick, and sets `cam+0x210` to `[mouse] camera_follow` (default 5 /s) every tick. The native value goes back
  after 10 camera ticks without ownership (single ticks failing the checks used to run at the native rate: a 63 %
  camera hitch). Turns are then already done when the native easing runs, and only her travel is
  eased, with the native collision handling. (A rigid follow was tried first: every uneven step of her gait
  and every one-tick collision hit showed as a jolt, i.e. microstutter.) It is left alone when a collision push reset `0x210` inside the call, in cut scenes, for camera flags
  0x20 / 0x80 / 0x100 / 0x20000, while the `0x21c` blend timer runs, on Arokh, in menus and while a controller owns
  the input. `camera_follow = 0` restores the original.
* **Pitch gain**: the plain on-foot branch of `0x41f848` rotates the view by `1.8 * cam+0x84` (`-(0.9 / -0.5)`),
  while the heading (mouse x) reaches the view 1:1. The mouse made the view move 1.8x faster vertically than
  horizontally. `[mouse] pitch_view_gain = 1.8` divides vertical counts by that gain in that branch only (not with
  player94 bit2, target swing `cam+0x168 != +0x170`, `cam+0x1c4`, camera bits 5 / 25, or on Arokh). The native pitch
  limits on `cam+0x84` are unchanged. `pitch_view_gain = 1` restores the old ratio.

* **Mouse sampling on the refresh grid**: while the bridge reports vsync-locked presentation
  (`drakan_present_clock::lockedRefreshSeconds`, the grid the engine tick is snapped to), raw mouse packets are
  timestamped on arrival and each frame takes the packets up to a cutoff that advances by whole refreshes
  (`InputModule::mouseCutoffUs`, never ahead of now, at most 1.5 refreshes behind). The host time between
  `beginFrame` calls wobbles by ~1-2 ms, which made a steady sweep turn ~6 % unevenly per frame.

## Modern bow aiming (24 October 2026)

`[mouse] bow_aim = modern` (default) / `classic`, `[mouse] bow_zoom = 0.85`. With the bow out, hold the left button to
aim (zoom, over-the-shoulder offset, crosshair), release to shoot at the crosshair; RMB still opens the native sight.
`src/BowAim.cpp`, `tests/bow_aim_tests.cpp`; details and measurements in `docs/reference/input.md` section 16.

## Quick-slot hotkeys (25 October 2026)

`QUICK1..QUICK9` (`[bindings]`, defaults 1..5): key N uses / equips the Nth item of the game's Quick Inventory, in the
bar's own column-major order. `src/QuickSlots.cpp` (`QuickSlotDriver`) sets the bar index and overlays the scheme's
Quick Inventory and Attack buttons on the next pad samples (the glue applies the overlay in its scePadRead hook);
`tests/quick_slots_tests.cpp`. Old files' `ACT:0x02..0x06` migrate to `QUICK1..5`. Details in
`docs/reference/input.md` section 17.
