# Order of the Flame: the Ruined Village "exactly 60 fps" with DLSS (2026-11-14, round 5)

Round 5, frame pacing agent. Scratch: `build/review/ootf-60fps/` (play folder, runs, tools, evidence; not tracked).
Play folder: a copy of `build/review/baseline-play` (owner's `drakan.ini`: 3840x2160 borderless, DLSS quality, HD
textures, vsync off, frame limit off, world rate uncapped), a private ELF copy, the `dist` pack
(`dist/DrakanPC/mods/ootf-levels.zip`, manifest build `f006ec09`), and `main` built into it with
`Build-LocalGame.ps1 -Incremental` under the build lock. Follows [ootf-cpu-frame.md](ootf-cpu-frame.md) and
[ootf-terrain-gpu.md](ootf-terrain-gpu.md).

## 1. Result in short

* **The 60 has a cause, and it is a 60 Hz clock: the guest vblank.** The game's flip worker (guest thread `1d78f0`)
  only looks at a handed-over frame when something wakes it. In the uncapped mode that is either the main thread's
  next request (`1d8660`, the `waitHook` of `drakan_frame_rate.h`) or the guest vblank, which is a host timer of
  16,667 us (`EeScheduler.cpp`, `kVBlankPeriod`). The hand-over itself (`1d86d0`, which sets the object's word +4 to 0
  under its semaphore) wakes nobody. When the main thread blocks before its next request (on the probe read-back,
  which waits for the GS worker, which waits for the present of the frame that has not been flipped yet), only the
  vblank breaks the chain, and the frame lands on the next vblank. With a natural frame time a little under 16.7 ms
  this snaps the rate to the vblank: in round 4's DLSS mode sweep (`ootf-cpu/runs/diag5-modes`) **50 % of the flip
  intervals with DLSS quality are 16.67 ms within 0.2 ms**, against 14 % with performance, 19 % with balanced, 1 % with
  DLAA. Round 4's audit trace (`diag3-ruined`, `DRAKAN_FRAME_AUDIT=4`) shows the chain frame by frame: `q` sees the
  previous flip done (`n 1 0`), the next frame is handed over, and the flip comes right after the next `V` (`s 3 0`,
  `w`, `f`, `F`), 16.67 ms after the previous one.
* **Not the cause:** vsync (off), the frame limit (off; the host loop's 16,667 us fallback timer only runs when the
  host is not completion driven), the display (59.997 Hz, no vsync), DWM (round 4: same in the foreground), DLSS
  evaluate (no fence or 60 Hz wait in it), the swap chain's frame latency (round 4: latency 2 changes nothing).
* **Today it no longer holds the Ruined Village at 60.** With the current exe and the current `dist` pack the start
  pose runs **68.6 fps with DLSS quality and 95.9 without on the old path** (quiet machine, 3 runs each, overlay and
  frame times agree). The whole frame got faster since round 4 (DLSS off too: 11.33 to 10.07 ms; EE 13.8 to 12.3 ms,
  GS worker 8.3 to 7.4 ms per frame), so the natural frame is now 14.6 ms, well under the vblank period, and the
  snapping stops. Round 4 used pack build `de6a9246`, this round `f006ec09` (formats `b6ff168` / `d937581` changed the
  terrain cells; the Ruined Village now draws about 2,130 draws a frame against 1,820); which change made it faster
  was not separated (the old pack is gone).
* **The flips still wait for vblanks** about a third of the time on the old path (34 to 35 % of flips within 1 ms
  after a vblank in every mode, round 4 as now), which makes the frame intervals uneven: the Ruined Village with DLSS
  quality shows intervals spread from 6 to 22 ms around a 14.6 ms mean.
* **What was built: `[video] flip_on_submit`** (default 0, env `DRAKAN_FLIP_ON_SUBMIT`, `world_rate = uncapped`
  only). A hook on `1d86d0` and its three resume points wakes the flip worker as soon as the frame is handed over;
  the worker still runs its own readiness tests (VIF1, GIF and DMA status) before it flips. Flips after a vblank fall
  from 34 % to 4 %, the intervals become even (13 to 15 ms), and the Ruined Village 1 % low improves from 23.1 to
  17.3 ms (DLSS quality) and 16.5 to 13.4 ms (off), but the median does not move there, and **Surdana gets 13 to 15 %
  slower** (3 of 3 runs), so the default stays the old path. `DRAKAN_FLIP_ON_SUBMIT_TOGGLE=<s>` (test) switches it
  every s seconds inside one run.

## 2. Measurements

Tool: `scripts/perf/scene_ab_bench.py` with its turn lock, owner's ini, `--cycle video.dlss=quality,off`, start
poses, no input. Two kinds of run:

* `pl-ootf-ruined`: variants `old:DRAKAN_FLIP_ON_SUBMIT=0` / `new:DRAKAN_FLIP_ON_SUBMIT=1`, 3 interleaved pairs.
* `tg-<scene>`: one variant with `DRAKAN_FLIP_ON_SUBMIT_TOGGLE=4` (off and on every 4 s inside each 40 s window, the
  first 0.5 s after each switch dropped), 3 runs; `tools/toggle.py` splits the `[ft] F` intervals by state. Both
  states see the same machine and the same pose, which is what made a comparison possible while other agents' games
  were running.

Quiet machine: every run below had 0 other games, replays or compilers before and during it. Earlier runs of the
same queues ran beside 1 to 2 other agents' games and a replay (GPU 85 to 99 %) and are not used (`diag1`,
`ab-ootf-ruined`, `tg-ootf-ruined` runs 0 and 1).

| Scene | DLSS | old -> new frame ms (median, range) | pairs / runs | 1 % low ms old -> new | flips after a vblank |
| --- | --- | --- | --- | --- | --- |
| Ruined Village (`pl`) | quality | 14.56 (14.34 to 14.81) -> 14.53 (14.34 to 14.74) | 2 of 3 faster, -0.2 % | 23.1 -> 17.3 | 34 % -> 4 % |
| Ruined Village (`pl`) | off | 10.07 (9.98 to 10.14) -> 10.52 (10.37 to 10.52) | 0 of 3, +3.8 % | 16.5 -> 13.4 | 34 % -> 5 % |
| Ruined Village (`tg`, run 2) | quality | 14.65 -> 14.53 (mean 68.6 -> 68.7 fps) | 1 run | p99 23.5 -> 17.7 | 31 to 35 % -> 3 to 6 % |
| Ruined Village (`tg`, run 2) | off | 10.16 -> 10.31 | 1 run | p99 16.0 -> 13.2 | 22 to 34 % -> 3 to 4 % |
| Surdana (`tg`) | quality | 6.29 / 6.44 / 6.24 -> 7.44 / 7.46 / 7.36 (148.6 / 146.4 / 151.4 -> 131.1 / 131.4 / 132.3 fps) | 3 of 3 slower, -11 to -13 % fps | p99 9.7 / 10.1 / 9.5 -> 10.3 / 10.1 / 10.3 | |
| Surdana (`tg`) | off | 5.77 / 5.92 / 5.76 -> 6.79 / 6.85 / 6.75 (167.4 / 161.5 / 166.0 -> 142.4 / 144.0 / 145.3 fps) | 3 of 3 slower, -11 to -15 % fps | p99 8.2 / 8.9 / 8.7 -> 9.5 / 8.6 / 8.8 | |
| Wartok Canyons (`tg`) | quality | 22.64 / 23.71 / 22.78 -> 22.71 / 23.23 / 22.77 (43.7 / 42.4 / 43.2 -> 43.8 / 42.5 / 43.8 fps) | no change | p99 32.2 / 34.7 / 31.9 -> 30.3 / 35.2 / 32.3 | |
| Wartok Canyons (`tg`) | off | 14.55 / 15.43 / 15.07 -> 15.47 / 15.72 / 15.81 (66.0 / 60.2 / 62.5 -> 60.2 / 61.0 / 60.5 fps) | 2 of 3 slower, -9 to +1 % fps | p99 23.6 / 27.4 / 26.9 -> 25.2 / 25.2 / 25.4 | |

Flip interval spread (`tools/hist.py`, share of intervals within 0.2 ms of 16.67 ms, DLSS quality):
round 4 `diag5-modes` 50 %, `diag3-ruined` 31 %, `ab2` (terrain agent, quiet) 13 %; this round old 5 to 8 %, new 0 to
5 %. On the old path the intervals cluster at 7 ms and at 14 to 16 ms plus a tail at 20 to 22 ms; with the switch
they cluster at 13 to 15 ms.

Why Surdana gets slower with the switch is not traced. A guess: the flip worker runs on the EE thread, and its flip
checks and the display commit wait for the VU1 worker (`vu1async::sync`, see `perf-round2.md`); at the hand-over the
VU1 worker still has the whole frame queued, while on the old path the flip comes at the next frame's request, when
that work is done. In a light scene that loses the overlap of the EE with the VU1 and GS workers.

## 3. Pictures

`evidence/ruined_quality_old_new.png` and `evidence/ruined_off_old_new.png` (old left, new right; same pose, same
time after the start, from the `pl` runs): same terrain, textures, fire, HUD; Rynn's idle animation and the flames
move between runs. The switch changes when a finished frame is flipped, not what is drawn.

## 4. What would raise the Ruined Village further

Not the pacing: on the current pack the chain (EE 12.3 ms, GS worker 7.4 ms, `Present` 6.7 ms per frame with DLSS)
is the frame. The renderer-side overlap of round 4 (`Present` blocking under the renderer lock) and the terrain draw
join of the renderer agent remain the levers. An even flip cadence without Surdana's loss would need the flip worker
woken at the hand-over without the EE then waiting for the VU1 worker inside the flip; not attempted.

## 5. Tests and builds as they ran

* Game builds: two incremental `Build-LocalGame.ps1 -Incremental -WorkDir build/final-game` into
  `build/review/ootf-60fps/play` under the build lock, exit 0 both.
* `python scripts/run_tests.py`: 561 tests, OK (2 skipped). `scripts/repository_guard.py`: 0 prohibited.
  clang-format 19.1.5 clean on `drakan_frame_rate.h`.
* Game runs: 2 diagnosis runs (`diag1`), 6 plain A/B runs (`pl-ootf-ruined`), 9 toggle runs, 4 abandoned A/B runs of
  a busy machine (`ab-ootf-ruined`, stopped). All finished runs exit 0. One game of mine at a time, no replay tools.

## 6. Not verified

* Which pack or exe change made the whole Ruined Village frame faster since round 4 (the round 4 pack is gone).
* The flip-on-submit switch beyond start poses: no walk, fight, level change, save and load, movies or menus with it on.
* Why the switch costs Surdana 13 to 15 % (guess in section 2).
* Other display refresh rates (vsync is off in the owner's ini; the guest vblank is a fixed 16,667 us timer either way).
* GPU timestamps beyond the bench's pass timer; no PIX or Nsight capture.
