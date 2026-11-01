# Still-view fps jitter (uncapped, 4K scale 4, vsync off), 2026-11-01

Report: fps jumps around even when looking at one spot. Measured with
`play_run.py --recipe fps_steady_wall.txt --env DRAKAN_FRAME_AUDIT=2 --env PS2X_PERF=1` on Surdana (the player's
installed `drakan.ini`; another game instance was running, so absolute numbers are pessimistic). Last 20 s of a 30 s still view.

## Findings

* World fps 123-150 from one second to the next (`[perf]` flips2/s), present gaps mean 7.3 ms, SD 2.4 ms, p99 17.4 ms.
  `ee_thread_cpu` 95-100 %, VU1 worker 55-75 %, GS worker 50-70 %.
* **A 16-19 ms present gap recurs every 32 world frames (about 4 per second, ~2.7 % of frames)**: the world serial
  (`P` value) of consecutive hitches differs by exactly 32; the wall-clock spacing varies 0.23-0.28 s. It is not the FPS overlay
  (`DRAKAN_SHOW_FPS=0`: 78 hitches in 20 s against 86, same spacing).
* In a hitch the engine update `U` runs on time, but the EE display flip `F` is held until the next guest vblank `V`
  (F follows V by <0.1 ms), and that `V` is delivered 10-15 ms late (the lateness column of `[ft] V`). `[sched-vblank]
  maxDelayMs` is 12-19 ms per 2 s window. The next present then follows within ~0.2-2 ms (catch-up), so the visible effect is one
  long frame in 32. `[frame-uncapped]` shows ~1/3 of flips go through the flip worker's retry path (`retries`).
* Outside the hitches the gaps are bimodal (mostly 5-9 ms, a 10-11 ms shoulder), lag-1 autocorrelation -0.23: short/long alternation.

## Not yet known

Why the guest waits for a vblank once per 32 frames in the uncapped/host-vblank policy (EeScheduler host vblank deadline vs the
flip worker 0x1d78f0 / 0x1d8660 wake logic in `drakan_frame_rate.h`), and why that vblank is up to one period late.
Next step: trace the flip worker's wait (PS2X_TRACE_SCHED) across one hitch.
