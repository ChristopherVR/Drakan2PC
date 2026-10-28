# Borderless fullscreen start-up did not always take

Report: with `fullscreen = 1` / `window_mode = borderless` the game sometimes came up looking windowed.

## Cause

The ini was read correctly. A window probe (poll the `DrakanWindow` HWND every 100 ms: rect, style,
visibility, foreground, `IsHungAppWindow`) showed, on every launch:

```
0.14s  rect=0,0,2560,1392  popup=False  fg=False   shown windowed while the renderer starts
3.50s  rect=0,0,2560,1440  popup=True   fg=False   borderless, never the foreground window
```

`drakan_win::create` showed the window and called `SetForegroundWindow` once, then the renderer start-up
(`GsrBridge::create`: HD FMV index, DLSS probe, D3D11) ran for ~3 s without pumping messages, and only then
applied borderless fullscreen. Windows refuses the foreground when the user clicks anything in that window
of time or the launcher has no foreground rights. A borderless popup that is not the foreground window
stays under the taskbar and the previously active window, which reads as "not fullscreen".

## Fix

- `drakan_win::create` leaves the window hidden; `drakan_win::show()` shows and activates it, retrying with
  `AttachThreadInput` to the foreground thread and falling back to flashing the taskbar button.
- `ps2_runtime.cpp` calls `show()` after `enableD3D11` succeeded, i.e. after the start-up window mode is
  applied.

Probe after the fix (launched from a shell without foreground rights, the case that failed before):

```
0.24s  popup=False vis=False fg=False   hidden during renderer start-up
3.10s  popup=True  vis=True  fg=True    borderless, foreground
```

F11 afterwards restores the framed window at its saved size; `DRAKAN_FULLSCREEN=0` starts framed and in
the foreground.

## Not changed

F11 / Alt+Enter are session-only by design, but the F1 menu shows the mode stored in `drakan.ini`, not the
live one, so after F11 it can show "Borderless" while windowed.
