# Uncapped world rendering — experimental

Select F1 → Display → World frame rate → Uncapped, apply and restart.
For unrestricted presentation, turn VSync off and set Presentation limiter to
Off. Use weave deinterlacing with the flicker filter off. Bob, blend and flicker
filter modes retain field-paced presentation. Hardware and scene workload still
determine the actual frame rate.

Configuration: `[video] world_rate = uncapped`; process override:
`DRAKAN_WORLD_RATE=uncapped`. Original 30 and experimental 60 remain available.
The default for a fresh configuration remains 30.

## Implementation

The graphics worker retains its original semaphore, DMA/VIF readiness tests,
display writes and completion flag. A pending display request can wake a sleeping
worker independently of guest vblank. If hardware is not ready, a delayed guest
cycle retry yields to other work without imposing a wall-clock frame deadline.
Wake requests do not accumulate extra credits. Other SleepThread callers retain
their behavior.

The complete native display transaction at `0x1e0048` publishes one ordered GS
snapshot after pending transfers and VU1 work finish. The renderer flushes,
latches and ends that completed frame. Subsequent guest vblank signals update
field/interrupt state without ending an unfinished frame. Native presentation
wakes on new latches; neural presentation retains acknowledgement backpressure.
Startup and field-dependent output retain the existing fallback.

The producer can run one frame ahead of the renderer. This bounds queued frames
without serializing CPU and GPU work. Neural swapchain creation, resize and
VSync-off presentation use tearing flags when DXGI reports support; VSync-on
and unsupported systems retain their existing presentation flags.

Uncapped mode schedules guest vblank edges by their original host deadlines,
independently of generated-code cycle consumption. Their frequency stays at
60 Hz. COP0 Count and audio clocks are unchanged. Frame telemetry distinguishes
completed world scenes, completed flips, unique presented worlds and guest
vblank signals. Menu frames and repeated presentation calls are not evidence of
gameplay FPS.

## Validation

The isolated uncapped candidate was validated before the subsequent inventory,
ultrawide menu and mouse refinements. Its executable SHA-256 is
`6D0224EF0305B43C93C24860840586D2A0A39268FE1C0B9CFCAE52D018932084`.
See `docs/journal/2026-10-18/data/uncapped-fps-20261018.json` for the aggregate receipt.

- Native Auto scale and combined SR Quality + NR at fixed 4x each passed all
  23 scripted category, Equipment, movement/look and bow checks, with audio on.
- The matched isolated SR/NR comparison measured 30.000 versus 32.996 completed
  world submissions/sec in its last five positive-world windows (about 10%).
  Guest vblank remained approximately 60 Hz. This is not a scanout measurement
  or evidence of gameplay above 60 FPS.
- A separate one-second backward hold differed by -0.591%; mouse pitch/yaw
  deltas matched and the attack state returned to idle in both modes.
- Original, fixture and private test save files were unchanged.
- The first performance attempt was interrupted by F1 and discarded. The clean
  rerun is the one recorded in the receipt.

Whole-game timing parity remains unverified. Subsequent combined-build UI/input
validation is recorded in `docs/journal/2026-10-18/native-inventory.md`.

## Fullscreen menu resolution

Auto internal scale now recalculates when fullscreen or client dimensions change,
using the actual presentation viewport. It ignores zero/minimized dimensions,
avoids feedback from scaled render targets, and preserves explicit numeric
environment/INI scale settings. At a 3840×2160 output it selects 6× for the 4:3
legacy 4:3 presentation and up to 8× for full-width 16:9 output. Interactive
native menus now use the selected output aspect, with centered foreground
elements retaining their proportions. Borderless fullscreen uses desktop resolution.

This improves rendering resolution, but cannot restore detail absent from the
original bitmap artwork. Auto 8× renders 2.56 times as many pixels as the former
5× selection. Set a fixed internal scale such as 4× if performance matters more
than matching output pixel density. The selected window resolution and internal
render scale are different settings.

## Remaining limitations

Whole-game physics, camera smoothing, all combat combinations and Arokh timing
parity remain unverified. The engine has a local 30 ms timestep clamp and some
per-update smoothing. The existing smoke, shadow, HUD border and texture issues
are separate from removing the frame cadence limit.
