# A paced default: frame_limit = auto (2026-11-16)

Follow-up of `perf-cpu-jitter.md`: the owner sees very high CPU use and jitter with `vsync = 0`, `frame_limit = 0`,
`world_rate = uncapped` on a 59.997 Hz panel. Goal: smooth, low-CPU frame pacing by default, explicit values kept.

## What changed

* `[video] frame_limit` takes `auto`, now the default (`drakan_config.cpp` table and `dsettings::VideoSettings`), so
  fresh installs and an ini without the key get it; the game appends the missing key as `frame_limit = auto`
  (checked: both test ini files without the key had it appended).
  * vsync on: no limiter (vsync already paces), the same as `0`.
  * vsync off: the host loop's limiter (`GsrBridge::hostTick`, `preciseSleepUntil`: high-resolution waitable timer and
    an 800 us spin) presents at most once per refresh period, the exact rational rate from the display configuration
    (`drakan_win::refreshSeconds`, 59.997 Hz here, re-read every second; 60 Hz until the first reading). It also
    publishes that period to `drakan_present_clock::g_lockedRefresh`, so `tick_snap` gives the engine whole refreshes
    per tick as it does under vsync.
  * `0` and numbers behave exactly as before (no tick snap with an explicit number). `limiterPeriod` in
    `gs/gs_frame_limit.h` holds the rule; `test_precise_sleep` checks it.
* `vsync` stays default on (it already was; `tick_snap` default on as well). The owner's ini sets `vsync = 0` and
  `frame_limit = 0` explicitly, so nothing changes for the owner until they choose.
* F1 > Display: "Presentation limiter" is now "Frame rate limit" with "Match the display (N Hz)" first; the VSync and
  limit help texts say what each costs in plain words. Checked with `settings_pages_preview` (the defaults show VSync
  On, Frame rate limit "Match the display").
* The portable (raylib) host maps `auto` to `GetMonitorRefreshRate` while vsync is off. Not run.

## Measurements

Setup: the worktree build, owner's ini copy (borderless 3840x2160, scale auto, HD textures from
`dist/DrakanPC/textures`, modern water on, DLSS off), Surdana start, `scene_ab_bench.py --foreground`, interleaved
repeats, variants by environment or ini. Ryzen 7 9800X3D, RTX 5080, Dell U2720Q at 59.997 Hz. Two other agents
built and benchmarked on the machine at the same time: the bench recorded compilers during some runs (noted below).
The harness could not keep the window in the foreground (5 to 80 % of samples).

Percentiles are medians over the repeats. `F` = interval between world flips (`[ft] F`, one per world frame); every variant showed each world frame in exactly
one present (`[ft] P` serials), except one busy vsync run (2.2 % of frames shown twice). CPU = `ee_thread_cpu` +
`other_cpu` of `[perf]`, median per second, in % of one logical processor. "World step" = the engine tick (`[ft] U`,
the time the world advances per frame).

Run 2 (`build/pacing/ab2`, 20 s windows, 3 repeats, ini-driven: what a player gets):

| variant | world fps | F p1 / p50 / p99 / p99.9 ms | longest ms | world step p1 / p99 ms | process CPU | CPU ms per world frame (EE + all other) |
| --- | --- | --- | --- | --- | --- | --- |
| owner (`vsync 0`, `frame_limit 0`) | 80.7 to 118.5 | 7.87 / 9.73 / 20.0 / 42.1 | 11 to 59 | 6 / 21 (6 / 11 in the quiet run) | 308 to 380 % | 37.5 |
| new default (keys missing: vsync on, auto) | 60.0 | 16.33 / 16.67 / 16.95 / 19.6 | 20 to 21 | 16.67 / 16.67 | 157 to 169 % | 27.3 |
| vsync 0, `frame_limit` missing (auto) | 60.0 | 16.25 / 16.67 / 17.0 / 20.6 | 20 to 21 | 16.67 / 16.67 | 162 to 171 % | 27.9 |
| vsync 0, `frame_limit = 60` | 60.0 | 16.36 / 16.67 / 17.0 / 21.0 | 21 to 22 | 15 / 18 | 165 to 168 % | 28.2 |

Two of the three owner runs had compilers running (hitches of 42 and 59 ms); the quiet one ran 118.5 fps, F p99 9.2
ms. All paced runs held 60.0 fps with the 1 % low at 17.0 ms under the same load.

Run 1 (`build/pacing/ab1`, 8 s windows, 3 repeats, environment-driven over the owner's ini):

| variant | F p50 / p99 / p99.9 ms | world step p1 / p99 ms | process CPU per run |
| --- | --- | --- | --- |
| owner | 8.38 / 11.5 / 13.8 | 5 to 6 / 11 to 13 | 377, 390, 358 % |
| `DRAKAN_VSYNC=1` (tick snap) | 16.67 / 17.1 / 21.7 | 16.67 / 16.67 (one busy run 33.3) | 178, 165, 229 % (busy) |
| vsync 0, `auto` | 16.67 / 17.0 / 20.1 | 16.67 / 16.67 | 175, 180, 208 % |
| vsync 0, `auto`, `DRAKAN_TICK_SNAP=0` | 16.67 / 17.2 / 20.2 | 11 to 15 / 19 | 151, 172 % (one run lost) |
| vsync 0, `frame_limit = 60` | 16.67 / 17.0 / 20.6 | 15 / 18 | 160, 171 % |

Presents against the refresh: every paced variant presented 60.0 per second on the 59.997 Hz panel (one per world
frame); the owner's settings presented 117 to 119 per second here (233 in the owner's own session), so each refresh
shows parts of about two (here) or four (the owner's session) frames.

## Findings

* Process CPU halves with any pacing (about 360 % to about 165 % of a processor). CPU per world frame drops too
  (37.5 to 27.3 ms in run 2; two of three owner runs shared the machine with compilers, so part of that is load).
* Presentation intervals are equally even in every paced mode (p99 within 0.4 ms of the refresh, worst frame 20 to 22
  ms, from the game side, not the limiter).
* A jitter source inside the paced paths: the engine reads a 1 ms game clock wherever the EE is when the next frame
  starts, so with an explicit 60 cap or without tick snap the world advanced 15 to 18 ms (p1 11 ms) per 16.67 ms
  frame, a +-10 % stutter in motion at an even frame rate. With vsync and with `auto` (both snap) every step is
  exactly 16.67 ms (the carry keeps the game time correct). That is why `auto` publishes its period to the tick snap
  and an explicit number does not (kept as before).
* The limiter's 800 us end spin costs about 5 % of one processor at 60 Hz; not changed (the vsync path does not spin).

## The choice

vsync on with `tick_snap` (the existing default) measures as smooth as anything here, at the lowest CPU, and is the
only mode that cannot tear. `frame_limit = auto` makes vsync off just as even and as cheap: it is the default for
the limiter so a player who turns vsync off still gets a paced game unless they pick Off. Recommended for the owner:
`vsync = 1` (or keep `vsync = 0` and set `frame_limit = auto` if they prefer lower latency and accept tearing).

## Not proven

* Tearing and what the owner perceives as jitter were not measured on the panel itself (no capture of the display).
  With vsync off and `auto`, the present cadence comes from the CPU clock, not the vblank, so a tear line stays
  nearly still or drifts slowly; vsync has none. Phase alignment to the vblank (DXGI frame statistics or
  `DwmGetCompositionTimingInfo`) was not tried.
* Input latency of vsync against `auto` was not measured.
* Variable refresh (G-Sync / FreeSync) panels: `auto` caps at the full refresh; a cap slightly below it would keep
  VRR engaged. Not tested (the U2720Q has none).
* Only the Surdana start; the owner's own session ran about twice this scene's rate.
* Harnesses that set `DRAKAN_VSYNC=0` without `DRAKAN_FRAME_LIMIT` now run at the refresh instead of uncapped; set
  `DRAKAN_FRAME_LIMIT=0` where a harness needs the old behaviour.
