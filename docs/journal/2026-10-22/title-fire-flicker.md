# Title menu fire flicker at uncapped frame rate

Symptom: with `world_rate = uncapped` the selected title entry ("Load Game") showed the retail fire-text
highlight on roughly one flip in seven and the intended gold text on the others.

## Evidence

- `GSR_WINSHOT` over consecutive flips: some frames have crisp gold text, others the blurry fire sprite.
- Draw log per flip (temporary diagnostic): the fire pair (T8 128x128 heat map `tbp=16148` + CT16 overlay
  `tbp=16216`, ALPHA 0x48) replaces the selected row's normal text pair, and only on every 7th-8th flip
  (the fire function's own 43.6 ms tick, `0x292838`, `*(0x4fae38+0x10010)`), never on the others.
- A diagnostic hook on `0x292838` showed its only caller in that case is `0x2c8948`, i.e. the retail text-widget
  routine `0x2c8728`, although `crispSelectedText` (`drakan_menus.cpp`) wraps that routine and clears the focus
  flag (`widget+0x10 & 0x40`) so the fire path is never taken. The wrapper was entered and judged "selected" every
  flip, with no misses.

## Root cause

`crispSelectedText` edits guest memory (focus flag and the four vertex colours) around a call to the original
function and restores them afterwards. A scheduler checkpoint inside the original unwinds the host stack
(`EeScheduler::checkpointDue`, resume by `lookupFunction(context.pc)` with a mid-function pc), so the wrapper
returned and restored the flag while the guest later resumed the original mid-way. The resumed original then saw the
focus flag set and called the fire routine. At 30 FPS the guest idles between frames so a preemption inside this call
is rare; uncapped, the menu loop is busy and time slices expire inside the draw often enough to be visible. It is a
latent bug, not an animation or presenter problem: the display commits were complete, and the fire frames were
fully drawn frames.

The same pattern is already handled for nested guest calls by `ps2xNoPreemptEnter/Leave`
(`drakan_input_glue.cpp`, `drakan_menus.cpp`).

## Fix

`crispSelectedText` now runs the original with preemption disabled (`ps2xNoPreemptEnter/Leave`) so the temporary
edit is always restored by the same call that made it. No frame-rate cap was needed.
