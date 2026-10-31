# Mouse-look microstutter (report: RTX 3070 + Ryzen 5 3600), 2026-10-31

Question: where does the stutter while turning with the mouse come from? Measured on the dev machine (not the reporters'
hardware; other game instances were running during part of it, so absolute fps below is not comparable between runs).

## Tools added

* `DRAKAN_MOUSE_AUDIT=1` prints `[mouse-raw]` (every Raw Input packet with its arrival time) and `[mouse-aud]` (the counts
  each engine tick consumed, steady-clock microseconds). Together with `DRAKAN_FRAME_AUDIT=2` (`[ft] P` presents, `U` ticks,
  `W` slow GS records) they share one time axis.
* `scripts/harness/mouse_drive.py <run>`: starts the game on a fresh card, waits for gameplay, focuses the window and
  drives the real mouse with SendInput at 1000 Hz, 2000 counts/s, direction change every 3.5 s (the Raw Input path, not the
  input script, which bypasses it). `scripts/perf/mouse_audit_report.py <stdout.log>` reduces the run to a few numbers.

## Findings

1. **The input path is clean.** Raw packets reach the accumulator one per ms (median gap 1.00 ms, p99 2.7 ms: the sender's own
   jitter); `HostInput` runs on its own message-only window thread, so presenting does not delay them. The counts a tick
   consumes are linear in the tick's sample time (SD 0.4 to 0.7 ms of motion, 2000 c/s steady segments). No mouse
   smoothing, acceleration or dropped packets were found.
2. **With vsync on, delivery is regular**: presents every 16.65 ms (SD 1.3), the tick starts about 2.6 ms after each present,
   and the existing refresh-grid cutoff (`mouseCutoffUs`) keeps the shown angle linear. Remaining hitches there are
   dropped refreshes (ticks of 2 refreshes), i.e. frame cost spikes, not input.
3. **Uncapped / vsync off** (the installed `drakan.ini`: scale 4, DLSS quality): presents average 7 to 10 ms but with SD
   3.1 to 3.6 ms and p99 18 ms; the delay between the engine reading the mouse and the frame being presented has SD 2.6 ms.
   The shown angle is then off a straight line by SD 2.6 ms (p95 5 ms) of mouse motion: a quarter to half of a frame,
   visible as microstutter in motion. This is variance of frame delivery, inherent in sampling at the tick start, not a bug in
   the sampling. About one in five of the longest present gaps lines up with a GS record over 8 ms (`W`: type 8 display
   commit 14 ms, type 1 draw data 12 to 18 ms).
4. **No thread had any priority, MMCSS or power hint.** The chain (EE game thread, VU1 worker, GS worker, host / present
   loop, raw input) runs at default priority. On a busy Windows 11 desktop, or a 3600 (two 3-core complexes, core parking),
   any of them can be preempted for a scheduler quantum in the middle of a frame.

## Change

`runtime/input/src/ThreadBoost.cpp` (`DRAKAN_THREAD_BOOST=0` turns it off): process opts out of EcoQoS / keeps the timer
resolution while covered; the EE, VU1, GS and host threads join MMCSS "Games" at above-normal priority; the raw input
thread runs at highest priority. **Not proven to help**: the A/B runs on the dev machine were too noisy (other sessions' games
running), so this is a low-risk hypothesis, not a measured fix. Reporters should compare with and without
`DRAKAN_THREAD_BOOST=0` using the two scripts above and send `mouse_audit_report.py` output.

## Foliage cost (same day)

The wind hook costs a few loads per drawn object plus the model: 62 ns per object after caching the wind direction per time
stamp and reading the clock once per 32 objects (2000 objects = 0.12 ms per frame; `world_foliage_test` prints it).

## Next, if the reporters' numbers show uncapped irregularity

Pace uncapped presentation (queue one frame and release at the smoothed production rate) or sample the mouse later in the
tick; both trade latency and need the reporters' machine to judge.
