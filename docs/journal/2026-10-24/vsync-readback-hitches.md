# Micro-stutter at high FPS: vsync'd flips queued behind GPU readbacks

Report: "the game feels like it micro-stutters everywhere even if the FPS is high". Play-folder settings:
`world_rate = uncapped`, `vsync = 1`, `frame_limit = 0`, DLSS and NR off, borderless 4K on a display reported as
59 Hz (measured 59.996 Hz from present timestamps, 59.9970 Hz from the display configuration).

## Measurement

`scripts/harness/play_run.py` with a walk-and-turn script (W held 8 s, mouse 15 px per 100 ms) in Surdana and
`DRAKAN_FRAME_AUDIT=2` (new per-frame `[ft]` timeline, see `docs/reference/env-flags.md`). The aggregates already
hinted at it (`[frame-time]`: windows of 121 updates whose `dt` sum equals wall time but with 6-9 updates over 30 ms;
`[sched-vblank] maxDelayMs` ~45), but only the timeline shows the shape:

- Normal frames are well paced (present intervals 16-17 ms, `dt` 16/17 ms; the engine clock has 1 ms resolution).
- About once a second the whole pipeline goes silent for 49-55 ms: no present, flip, vblank or update.
- Recovery: three late guest vblanks are delivered back to back (the scheduler allows the host deadline to fall two
  periods behind), so the engine runs one 55 ms tick and then three ~4 ms ticks. On screen: a 3-refresh freeze, then
  a jump.

Attribution (`W`/`H` lines, then `GSR_PROFILE=2`): each gap is a single GIF record taking 45-49 ms on the GS worker
while it holds the renderer lock; inside it `readbackRT` -> `readbackMap` takes the same time. The readbacks are of
small CT32 targets (128 wide, dirty rows 0-128, `bp` 0x3740-0x3f00), triggered by CLUT loads and host->local uploads
over them (the class described in `../2026-10-21/cpu-gpu-readback.md`). HD textures off: unchanged. Vsync off with a
60 FPS limiter: the same 14 readbacks, none over 8 ms, no gaps, `dt` SD 0.90 ms. So the copy is cheap; the `Map` waits
behind queued vsync flips.

## Cause

`d3d11_device.cpp` creates the swapchain (flip discard, 3 buffers) with a frame-latency waitable object and
`SetMaximumFrameLatency(1)`, but nothing waited on the object: `Renderer::waitForPresent` took the renderer mutex for the
whole DXGI wait, so the bridge had stopped calling it ("Pace outside renderer locks"). With the waitable flag DXGI does
not throttle `Present` itself, so flips queued until the buffers ran out (~3 refreshes, also visible as `presentMs`
~800 ms/s). Any GPU->CPU sync waits for all of them.

## Fix

- `Renderer::waitForPresent` honours its documented contract (block without the renderer lock): under the lock the
  backend hands out a duplicate of the waitable (`beginPresentWait`), the wait runs unlocked (`waitPresentHandle`), and
  the result is recorded (`endPresentWait`). A consumed slot is kept until the next `Present`, so a tick that has no new
  frame does not make the following wait block. NR presentation keeps its locked wait (`beginPresentWait` returns false).
- `GsrBridge::hostTick` waits (at most 50 ms) before each present when vsync is on and NR is not requested.

## Result (same script, vsync on)

| run | present gaps > 30 ms | slow GS records | engine dt SD |
| --- | ---: | ---: | ---: |
| before | 11 | 13 (all `readbackRT`, 15-49 ms) | 5.05 ms |
| after, run 1 / 2 / 3 | 0 / 0 / 0 | 0 / 0 / 0 | 0.92 / 0.68 / 1.88 ms |
| after, Ravenshold (~38 readbacks/s) | 0 | 0 | 0.77 ms |

Runtime tests pass. One run made while another session compiled collapsed to ~7 FPS with 250-500 ms GS records and no
readbacks; repeated after the compile finished it was clean, so it is contention, not this change.

## Remaining

- ~~Guest vblank against a 59.94 Hz display, a slip every ~17 s~~: wrong. The display runs at 59.996 Hz, so the
  beat with the 60.000 Hz guest vblank is ~5 minutes. The leftover irregularity was the engine tick time instead (next
  section).
- The readbacks themselves still happen: measured below, they no longer cost throughput.
- NR presentation: same problem, fixed below.

## Follow-up: engine tick time snapped to the refresh

With presents even, the world could still move unevenly. Timeline (`U` against `P`/`V`): the engine tick runs 2-3 ms
after each present (SD 0.7-1.2 ms) and is unrelated to the guest vblank (SD ~7 ms). `1c3028(timer, out)` reads the
1 ms clock `4af7f8`, `out = delta ms * timer+10 * timer+28 (time scale)`; it is called once per tick from `1433c0`
(`a0` = timer 0x4df630, `a1` = engine+24, return 0x1433e8), which then caps the tick at engine+508 and derives
1/dt. Depending on the release phase the tick read 16/17 ms (clock resolution) or alternated 14/19 ms for whole runs
(`stutter-final2`, tick SD 1.88 ms).

`tickTimeHook` snaps the tick (in real seconds, time scale divided out) to whole refreshes while the bridge reports
vsync-locked presentation, carrying the remainder (`snapTickToRefresh`, 41,615 checks in
`scripts/tests/frame_cadence_test.cpp`, now registered as ctest `frame_cadence`; it had no target before). The refresh comes from
`QueryDisplayConfig` for the window's monitor, not DWM (the desktop's rate, wrong on mixed-refresh setups) or DEVMODE
(rounded to 59).

| 40 s walk/turn, vsync on | ticks off the refresh grid | tick SD | present gaps > 25 ms |
| --- | ---: | ---: | ---: |
| `tick_snap = 0` (two runs) | 38.9 / 40.1 % | 2.68 / 0.57 ms | 62 / 0 |
| `tick_snap = 1` (two runs) | 0.0 / 0.0 % | 0.03 / 0.33 ms | 0 / 1 |

Another session ran harness passes on the same `dist` exe during these, so the gap column (62 in the first control,
1 in the second snap run) reflects GPU sharing, not the setting. Game time vs wall over 42 s: -1.9 ms with snapping.
The reported refresh (59.9970) is 2e-5 above the measured one, so the carry crosses half a refresh about every
5 minutes and that tick takes two refreshes.

## Follow-up: NR presentation and the cost of the readbacks

NR (`DRAKAN_DLSS_NEURAL=1`, 40 s walk) showed the original symptoms: 31 present gaps over 25 ms, ticks 14/20 ms
(SD 3.54 ms), and `GSR_PROFILE` put the 31-34 ms GS records in `readbackMap` again (two refreshes this time). The
upgraded D3D12 swapchain in `ps2recomp/gsr_nr` is the same setup (waitable object, `SetMaximumFrameLatency(1)`,
2 buffers) and nothing waited on it: the bridge skipped the wait under NR and the old NR wait path held the renderer
lock. `Bridge::presentWaitHandle` now hands its waitable to `Backend::Impl::beginPresentWait` (duplicated under the
lock, waited without it, slot released by NR's `present`), the locked `Backend::waitForPresent` is gone, and the
bridge waits and enables tick snapping whenever vsync is on.

| NR on, 40 s walk | before | after |
| --- | ---: | ---: |
| present gaps > 25 ms | 31 | 1 |
| engine tick SD | 3.54 ms | 0.78 ms |
| ticks off the refresh grid | 70.8 % | 0.2 % |
| GS records > 20 ms (median of those > 8 ms) | 30 (13.2 ms) | 0 (10.0 ms) |

Readback cost after the fix: in Ravenshold while moving ~60 per second, 0.5-0.8 ms each in `Map`, ~50 ms/s of the GS
worker (vsync on). Uncapped with vsync off the game ran 246-361 FPS; windows with 241-552 readbacks per 2 s were no
slower than windows with 2-29, because VU1 sets that ceiling. The ownership redesign the 2026-10-21 note calls for
is not worth its risk for now.

