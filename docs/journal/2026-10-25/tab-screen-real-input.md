# Modern Tab screen with real keyboard input: closed on open, then stuck — 2026-10-25

User report: with `inventory_screen = modern` (switched in F1 during the session), Tab showed only Rynn facing the
camera and the game looked frozen. Reference: `docs/reference/mouse-menus.md` section 13, "Real-input rules".

## The user's log (`dist/DrakanPC/drakan.log`, copied before any run)

```
[tab-screen] frame=36406 modern screen bound to 134fc10
[in] t=209.56 frame 36407: gui screen -1 -> 21 (gameplay=1)
[tab-screen] overlay: session 1 opened
[tab-screen] frame=36407 selection 2 valid=1 busy=1 details 0:
[tab-screen] frame=36407 action close(-1) handle 0          <- Close on the opening frame
[tab-screen] frame=36497 action close(-1) -> refused        <- 90 frames later
... screen 21 for the rest of the session
```

The setting had been switched through Options > PC Settings (screens 23 -> 9 -> 23 -> -1); the switch itself
worked (the next Tab bound the modern view).

## Causes

1. **The opening Tab reached the host screen as Close.** The game's keyboard is Raw Input on the input module's own
   message-only window and thread (`HostInput`, `RIDEV_INPUTSINK`). The Tab press is in the game's next sample and
   the screen opens a frame later, while the main window's `WM_KEYDOWN` for the same press is dispatched by the
   present thread's pump (the EE ran at ~130 fps, presents at ~60). When the screen layer already had the input,
   that message was fed to ImGui as a fresh press: `IsKeyPressed(Tab)` -> Close. Second path: the layer clears
   ImGui's keys when it takes the input (`ClearInputKeys`), so the next auto-repeat `WM_KEYDOWN` of a held Tab is a
   new press too.
2. **The close was dropped and left the screen invisible.** 0x15 ignores Select while its open animation runs
   (`screen+0x15c` > 0; `431d10` sets -2 to start the close, `42fd18` closes at its end). The executor tapped Select
   immediately, the tracker waited 90 frames for `S+0x34`, reported "refused" and left `Shared::closing` set, while
   `InventoryScreen` had already closed itself on Back. Native page hidden (the view's removals), host screen not
   drawn, input owned by the host screen: nothing on screen but the world crop, and no key did anything.
3. **"scripted 1"** in the F1 log line is the game's flag `0x4fbafc` (set while 0x15 is open; `42f758` clears it,
   and `42f640` only closes when it is set). It is not the input module's script mode (`InputModule::scripted()`
   was false: the overlay used the ImGui path).

## Fix

* `SettingsOverlay::handleMessage`: key / button presses stamped (`GetMessageTime`) at or before the moment the
  screen layer took the input are dropped (equal ticks count as before; the tick is ~16 ms).
* `gameui::KeyGate` (`runtime/gameui/include/gameui/Input.h`): reset when the screen opens and when it gets the input
  back; a key / mouse button / pad button down at that moment counts only after its release (keyboard and mouse state
  from `GetAsyncKeyState`, pad state from the ImGui backend's XInput poll). The scripted path gates the script's edges
  the same way.
* `InputAccumulator::releaseAll`: keys still down are "held over"; their Raw Input repeats are ignored until released
  (a Tab held through the close does not reopen the screen; a combat key held through F1 does not re-fire).
* `gameui::CloseExecutor` (`Actions.h`) with `NativeState::animation` (`screen+0x15c`): Close waits for the open
  animation, taps Select, repeats an ignored tap (45 frames, 3 taps), waits for the close animation; Failed restores
  the native page. A refused Close clears `closing`; `InventoryScreen` reopens where it was; the overlay reopens the
  host screen whenever it is bound, not closing and closed.
* Watchdog `tabWatchdog`: no host draw (`Shared::hostDrawn`) for 90 EE frames and 2.5 s without a native box ->
  log and `tabFallBack` (native page back for this opening, host input mode off). Switching `inventory_screen` to
  Original while the modern screen is open uses the same fallback. Test aid `DRAKAN_DEBUG_TAB_HIDE=1`.
* Unit tests: `testKeyGate`, `testCloseExecutor`, `testScreenCloseRefused` (gameui), `testHeldOverKeys` (input).

## Runs

Private copies only: `build/harness/polish-save/mc0` card, private `drakan.ini` / `drakan_input.ini` / log dir
(`DRAKAN_INI`, `DRAKAN_INPUT_INI`, `DRAKAN_MEMCARD_DIR`, `DRAKAN_LOG_DIR`); the user's ini, saves and log untouched.

| Run | Input | Result |
| --- | --- | --- |
| `build/harness/tabreal/repro1` | real (SendInput) | not run: another application held the foreground, so no input was sent |
| `build/harness/tabreal-scripted1` | script: Tab + Tab 3 frames later, Tab held 270 frames, Tab / Esc / right click closes, 4 rapid taps | the rapid case is the user's sequence: `close deferred: the open animation is running` -> `Select tap 1 (deferred 55 frames)` -> screen closed; held Tab kept it open (`s3_tab_held.png`); Esc and right click closed; world and HUD back (`s10_rapid.png`) |
| `build/harness/tabreal-watchdog` | script, `DRAKAN_DEBUG_TAB_HIDE=1` | `native page restored: watchdog: host UI not drawn for 271 frames (2.5 s ...)`; the native page shows (`w1_after_watchdog.png`); Tab closed it natively |
| `build/harness/tabreal-series1`, `-series2` | script | the overlay never presented (`[gsr-presentation] host handoff timed out`, the GPU was busy with another game): the watchdog restored the native page after 2.5 s, as intended |
| `build/harness/tabreal-series3` | script, shots every 4 frames | before the transition fixes: on close (Map page) the host faded at once, the map area then showed the world, then a dark rectangle (opaque crop at low fade); on open a frame of native crop / cleared page window |
| `build/harness/tabreal-series9` | same, after the fixes | open: host at 60 % from the first frame, HUD gone; close: host steady through the native close animation (no native map), then a cross-fade over a still copy, HUD back mid-fade (`sheet_open.png`, `sheet_close.png`) |
| `build/harness/tabreal-series4`, `tabreal-hot1` | script: hover the elixir, F, close, Q / E | `toggle-hot-slot(2) -> confirmed`; RAM (`ram_h1..h4`, `ram_q0..q2`): elixir `hot 1` before and after the close; Q shows the elixir in the quick inventory bar; Q + E, Q twice, E held: no drink (HP 84 / 100, quantity 100) |

## Follow-up reports folded in

* **HUD pop.** The views hid the HUD from the activation until the screen was gone. Now it follows the host's
  opacity (`Shared::hostFade` >= 0.5), and the host starts at 60 % on open and stays through the close animation.
* **"The map sliding across the screen" on close.** The host faded out when Close was requested; the native close
  animation then ran with the Map section's sprites at their native place (the host's map area was no longer
  published). Now the host stays up and keeps its placement until 0x15 is gone, the native map pieces are skipped
  whenever the host has no map area, and the fade-out runs over a held frame copy.
* **Quick slots.** Applied natively (record `+4`), visible in the quick inventory bar. How the native game consumes
  the bar's item was not established; keys 1..5 do nothing natively. Not fixed.

Real `SendInput` runs were not possible in this session: the user was playing another game in the foreground the
whole time (the input would have gone to it), and idle waits of 15 minutes never saw 90 s without input.
