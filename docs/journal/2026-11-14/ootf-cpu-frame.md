# Order of the Flame, CPU side: the Ruined Village "60 fps cap", the VU1 terrain stage, the frame queue (2026-11-14)

Round 4, CPU agent. Scratch: `build/review/ootf-cpu/` (play folder, runs, played sessions, tools; not tracked). Play
folder: a copy of `build/review/baseline-play` (owner's `drakan.ini`: 3840x2160 borderless, DLSS quality, HD textures,
vsync off, frame limit off, world rate uncapped), a private ELF copy, the installed pack
(`dist/DrakanPC/mods/ootf-levels.zip`, the 14 November build) and the exe of `main` built into it with
`Build-LocalGame.ps1 -Incremental` under the build lock.

## 1. Result in short

* **The Ruined Village is not capped at 60 fps.** Nothing in the pacing holds it there: vsync is off, the frame limit
  is off, no sleep or fixed step runs at 60 Hz, the DLSS frame generator is not involved (no neural rendering, no
  frame generation in this build). The world frame is a chain that runs one step after the other: the GS worker
  submits the frame (about 8 to 9 ms of CPU with DLSS), the window thread presents it, and `IDXGISwapChain::Present`
  blocks for about 5 ms (frame latency 1: it waits for the GPU), and the GS worker waits for that present before it
  takes the next frame's work (`GsrBridge::Impl::waitForHandoff`, by design: the latched frame must be shown before it
  is replaced). The EE waits for the GS worker in turn (the per-frame probe read-back). With DLSS quality at 4K the
  sum lands at 16.6 to 16.8 ms. Changing only the DLSS mode in one run moves the rate and the time `Present` blocks
  together (section 2.2): performance 65.5 fps (Present 4.4 ms), balanced 63.7, quality 60.0 (5.4 ms), DLAA 37.7
  (12.0 ms), off 84 to 86 (4.3 ms). So it is not a fault in the pacing; the lever is the time the GPU needs per frame
  and the GS worker idling while `Present` blocks under the renderer lock, both on the renderer side.
* **The VU1 terrain texture stage is not free.** In a replay of 16,000 Ruined Village VU1 runs (single thread, current
  exe, 0 mismatches against the reference) the texture-matrix stage (`0x0468`, flag word 0x126c, 225 entries in
  place) is **40 % of the VU1 engine time** (2.81 of 6.94 ms, 0.70 us per pass, 1,620 instruction pairs per pass).
  That is about 1.0 ms of the VU1 worker per world frame in the Ruined Village and, by run count, about 3 ms in
  Wartok Canyons. 86 % of these passes transform a vertex set in which not one entry changed since the previous pass.
  Not changed (section 3): the frame time would not move (no scene is bound on the VU1 worker alone), and skipping
  it exactly needs a proof that neither the set nor the flags the loop leaves are read later.
* **GPU vertex path for terrain:** not taken further. The terrain stages are the batch families, which the path
  covers exactly (tuning journal of 13 November: records test, 0 differing pixels; live Ruined Village toggles 24 of
  24 one picture), but measured there it moves the work onto the GS worker (Ruined Village GS worker plus 4 to 7.6 ms,
  VU1 worker unchanged), and the GS worker is the thread that binds Wartok Canyons. Its default stays `cpu`.
* **Frame queue:** now a `drakan.ini` switch (`[video] frame_queue = 0 | 1`, default 0, environment still wins).
  The played session passes (Surdana walk, quick save, quick load, pause menu, inventory; the logo and intro movies
  and a level load into the transition cut scene; the Order of the Flame opening, quick save and load, pause menu).
  Under GPU contention it is not safe: with a deliberate second GPU load the Ruined Village with the queue ran 20.3 to
  39.4 ms per frame against 16.8 to 33.4 without it, one pair 2.3 times slower; with one other game running Surdana
  was slower with the queue in 3 of 3 pairs (+3 to +13 %). **Recommendation: keep it off by default.**

## 2. The Ruined Village "60 fps cap"

### 2.1 Timeline

`DRAKAN_FRAME_AUDIT=2` / `4` (`[ft]` lines; `4` is new and traces the display worker hand-shake, see
`docs/reference/env-flags.md`), Ruined Village start, DLSS quality, runs `runs/diag1-ruined`, `diag2-ruined`,
`diag3-ruined`:

* Host ticks 59.7 per second, `tickMs` 16.02 to 17.44, `waitMs` (host waiting for a latched frame) 11.5 ms per
  frame, `presentMs` 5.2 ms per frame, `handoffMs` (GS worker waiting for its present) 5.2 ms per frame.
* Per frame, in order: the main thread asks the display worker (`q`, `0x1d8660`), the worker is woken at once (`w`),
  flips (`F`), the window thread's `Present` returns 4.5 ms later (`P`), and the next engine tick (`U`) starts right
  after that present (the EE was waiting for the GS worker, which was waiting for the present). Main thread work from
  `U` to `q` about 11 ms, plus the present about 5 ms: 16.6 ms.
* In some phases the flip lands right after a guest vblank (the worker's sleep took the vblank path, `s 4 0`), which
  makes the 60 look like a vblank lock; it is not the general case (most flips follow the wake request directly).
* `--foreground` (the window activated, as the owner plays): still 60.6 fps with DLSS quality and 87.3 without (one
  other agent's game ran beside it), so it is not the desktop compositor.
* EE samples (`DRAKAN_FRAME_AUDIT=3`): the EE thread spends its time in the FINISH poll (`PS2Memory::read64`,
  `f_0x1dbfa8`) and in the blocking read (`GsrBridge::Impl::drain`), i.e. waiting for the GS worker.

### 2.2 DLSS mode sweep (one run, live switch, same pose)

| DLSS | world fps | `Present` per call ms | conditions |
| --- | --- | --- | --- |
| performance | 65.5 | 4.41 | `diag5-modes`, `m-modes`: other agents' replays or one other game ran during these runs |
| balanced | 63.7 | | |
| quality | 60.0 | 5.37 | |
| DLAA | 37.7 | 11.97 | |
| off | 84.2 to 86.4 | 4.26 | |

With the CPU work the same, the rate follows the mode and the `Present` wait follows the rate: `Present` blocks
until the GPU has finished the previous frame (`SetMaximumFrameLatency(1)`), so the frame is the GS worker's CPU time
plus the GPU's tail. The GPU pass timer cannot say how busy the GPU is (its last pass, `overlay`, carries the idle time
until the next frame: 12 ms with DLSS, 10 ms without), and `nvidia-smi` read 90 to 99 % in these runs but other
agents' replays and games shared the GPU in every one of them, so whether the GPU alone is saturated at 60 fps was not
separated.

### 2.3 What would raise it (renderer side, not done here)

* `Present` blocks inside the renderer lock (`GsrBridge::Impl::tickWindow` holds `rmtx`), and the GS worker waits for
  that present before the next frame's records (`waitForHandoff`). Letting the worker build the next frame while the
  present waits (the swapchain's waitable outside the lock, a latched copy that the next frame cannot overwrite) is
  the CPU/GPU overlap that is missing. It touches the D3D11 present path and the latch, i.e. the renderer agent's
  files; not attempted.
* Less GPU time per frame with DLSS (the per-draw motion and guide work multiplied by 1,600 to 2,800 terrain draws):
  the renderer agent's terrain draw merging.

## 3. The VU1 terrain texture stage (`0x0468`, recheck of round 3)

Replay of `build/review/perf-cpu/vurec/ruined.vur` (16,000 runs recorded 12 November in the Ruined Village) with the
current exe: `DRAKAN_VU_BENCH=...ruined.vur,5 DRAKAN_VU_BENCH_RUNS=...csv`, 0 mismatches, 7.57 ms best of 5 (0.6 ns per
pair). Per stage (flag word, entry pc), share of the engine time:

| Stage | runs | ms | share | ns per run | pairs per run | vertices drawn per run |
| --- | --- | --- | --- | --- | --- | --- |
| texture matrix over 225 entries (0x126c, 0x1018) | 4,034 | 2.81 | 40.4 % | 696 | 1,620 | 0 |
| terrain strips (0x106c, 0x0e70) | 4,668 | 0.76 | 11.0 % | 163 | 241 | 9.1 |
| terrain set-up (0x10ec, 0x0e70) | 4,034 | 0.65 | 9.3 % | 160 | 175 | 0 |
| clipped batch (0x000d, 0x1580) | 162 | 0.55 | 7.9 % | 3,394 | 5,421 | 42.1 |
| everything else | | 2.17 | 31.4 % | | | |

* So the stage costs real time: about 1.0 ms of VU1 engine time per world frame in the Ruined Village (5,690 runs a
  frame, 25 % of them this stage) and about 3 ms in Wartok Canyons (16,700 runs). Round 3's "costs nothing since
  12 November" holds only against the old scalar path; the 12 November lanes rule made it 4 times cheaper, not free.
* Between two consecutive passes of this stage, the 225 entries of the set (VU 516 to 740) are unchanged in 3,532 of
  4,112 passes (86 %); in the rest 4 to 209 entries were uploaded. So most passes transform the same stale data again
  (the cause of the growing values of round 3, section 6).
* Not changed, for two reasons. (1) No frame-time gain: the Ruined Village is not bound on the VU1 worker (section 2),
  and Wartok Canyons is bound on the EE, the VU1 worker and the GS worker at once (EE 19 to 22 ms, VU1 17 to 20, GS
  18 to 24 per frame), so 3 ms off one of the three does not shorten the frame. (2) An exact skip needs proof that
  nothing reads the transformed entries or the MAC / sticky status flags the loop leaves (the stale entries overflow,
  so the sticky flags carry their trace); the replay restarts every run from its recorded memory, so it cannot show
  that by itself. Tool for a later attempt: `build/review/ootf-cpu/tools/live.py` (which entries change between passes).

## 4. The frame queue

### 4.1 The switch

`drakan.ini` `[video] frame_queue = 1` turns on `PS2X_READBACK=late` and `PS2X_FRAME_QUEUE=1` together
(`ps2x_readback::configuredFrameQueue`, set in `drakan_frame_rate::install` before the guest runs); each environment
variable, when set, still decides for itself. Default 0: nothing changes for players. Checked: with the ini switch the
log shows `[readback] late ...` and `[readback] frame queue ...`, and the exit line counts late reads (Surdana 1,902 of
1,962, 0 different).

### 4.2 Played session (owner's ini plus `frame_queue = 1`, `played/`)

| Run | What was played | Result |
| --- | --- | --- |
| `surdana-fq1` and `surdana-fq0` (same recipe, queue off) | Drakan 2 New Game to Surdana; run and turn, F5 quick save ("successful" on screen), run on, F8 quick load (back at the saved spot), Esc pause menu, Esc, Tab inventory (its first-open help box), walk | both exit 0; the same pictures at every step (`played/sheet-surdana-fq1.png`, `sheet-surdana-fq0.png`) |
| `transition-fq1` | logo movie, New Game, the intro book movie (HD, 3,059 frames, subtitles), level load (screen 29), the cut scene that follows | exit 0, every movie frame HD, no fallback, cut scene plays (`played/sheet-transition-fq1.png`) |
| `ootf-fq1` | Order of the Flame campaign New Game, the Ruined Village opening in full, run, F5 (successful), run, F8 (back at the saved spot), pause menu, walk | exit 0 (`played/sheet-ootf-fq1.png`) |

What was placed rather than played: nothing was placed; the input was scripted (`tools/played_*.txt`). Not covered: a
level change by walking through a level exit, a save from the pause menu's Save Game screen, the settings overlay.

### 4.3 Contention (`runs/fq-*`, `runs/fqload-ootf-ruined`)

`scene_ab_bench.py` (its turn lock), 3 interleaved pairs per scene, DLSS quality and off in every run, `poll` = queue
off, `pipe` = `PS2X_READBACK=late PS2X_FRAME_QUEUE=1`. The machine was not quiet for any of these runs; what ran
beside each run is in the table. `fqload` started a deliberate second GPU load (`tools/gpuload.sh`: `gsr_replay` of the
Order of the Flame capture at scale 4 in a loop) before the first run and kept it running throughout.

| Scene | DLSS | frame ms poll -> pipe (median, range) | 1 % low ms poll -> pipe | EE ms poll -> pipe | beside the runs |
| --- | --- | --- | --- | --- | --- |
| Ruined Village, deliberate GPU load | quality | 33.35 (16.78 to 33.41) -> 36.36 (20.25 to 39.41); pairs 33.41->20.25, 16.78->39.41, 33.35->36.36 | 50.0 -> 47.8 | 23.3 -> 8.6 | the load, and one other agent's game in 5 of 6 runs |
| | off | 20.92 (11.98 to 25.61) -> 23.99 (15.88 to 25.27); pairs 25.61->15.88, 11.98->25.27, 20.92->23.99 | 39.6 -> 34.1 | 16.0 -> 8.2 | |
| Ruined Village | quality | 33.35 (16.68 to 50.01) -> 36.01 (16.78 to 37.83); quiet pair 16.68 -> 16.78 | 50.0 -> 46.1 | 24.1 -> 9.0 | pair 1 quiet (5 compilers), pairs 2 and 3 one game and 2 replays |
| | off | 22.05 (11.53 to 36.46) -> 25.81 (11.63 to 27.53); quiet pair 11.53 -> 11.63 | 38.8 -> 34.4 | 16.1 -> 8.0 | |
| Wartok Canyons | quality | 64.01 (63.92 to 64.79) -> 63.74 (63.65 to 64.74) | 75.2 -> 79.3 | 21.7 -> 23.7 | one other game in every run (GPU 86 to 91 % before) |
| | off | 28.39 (28.21 to 28.49) -> 27.85 (27.79 to 28.45) | 39.0 -> 40.1 | 22.3 -> 23.7 | |
| Grimstone Mines | quality | 20.13 (19.91 to 20.43) -> 20.73 (13.85 to 20.85) | 26.5 -> 26.9 | 7.6 -> 7.8 | one other game in 6 of 6 runs |
| | off | 11.28 (7.24 to 11.46) -> 11.55 (11.23 to 11.94) | 16.0 -> 17.0 | 6.7 -> 6.4 | |
| Surdana (Drakan 2 guard) | quality | 13.90 (13.34 to 14.30) -> 14.67 (14.33 to 14.96), pipe slower 3 of 3 | 26.7 -> 21.8 | 10.2 -> 5.2 | one other game in every run (GPU 75 to 95 % before) |
| | off | 8.87 (8.45 to 9.43) -> 9.57 (9.56 to 9.78), pipe slower 3 of 3 | 17.7 -> 15.8 | 7.6 -> 4.0 | |

Reading: where the game reads back every frame (Ruined Village, Surdana) the queue halves the EE's CPU and shortens the
1 % lows, but when another program loads the GPU the median frame gets slower (Surdana 3 of 3 pairs, +3 to +13 %) and
in the deliberate-load test one Ruined Village pair went from 16.8 to 39.4 ms; another went from 33.4 to 20.3 ms. That
spread, and round 3's two busy runs at 41 and 49 ms, are the reason to keep it off: a player with a browser video or
a recorder on the GPU would be exposed. Where the pose does not read back (canyon, mine) it changes nothing. Why the
queue loses under contention was not traced (a guess: the EE runs ahead by up to the VU1 worker's 256 jobs and a GPU
stall then holds a longer queue).

## 5. Frame-time table (Order of the Flame and the guard)

The rows of 4.3 are this round's frame times; none was taken on a quiet machine (the other three round-4 agents ran
games, replays and builds all session; the turn lock kept two of our own games apart, not other programs). The
quiet-machine reference remains round 3 (`ootf-performance.md` 7.1). No new speed path was landed, so there is no
before/after of a change of mine to table; the only switch added is the frame queue's, default off.

## 6. Tests and builds as they ran

* Game builds: five incremental `Build-LocalGame.ps1 -Incremental -WorkDir build/final-game` into
  `build/review/ootf-cpu/play` under the build lock; two failed to compile on my own typo in a trace line (fixed),
  three exit 0. The play folder's exe is the build of `12020e7e`'s tree (other agents' committed work included).
* `python scripts/run_tests.py`: 561 tests, OK (2 skipped). `scripts/repository_guard.py`: 0 prohibited.
  clang-format 19: clean on the changed C++ files.
* VU1 replay: `DRAKAN_VU_BENCH` on `ruined.vur`, 16,000 runs, 0 mismatches.
* Game runs: 3 diagnosis runs, 1 foreground run, 2 DLSS mode sweeps, 4 played runs, 30 benchmark runs; all exit 0.
  One game of mine at a time; one replay tool of mine (the GPU load) during `fqload` only.

## 7. Not verified

* Whether the GPU alone is saturated at 60 fps with DLSS quality in the Ruined Village (every run shared the GPU).
* Any frame time on a quiet machine this round.
* The renderer-side overlap of section 2.3 (not built).
* An exact skip of the VU1 texture stage (not built; no proof that its outputs and flags are dead).
* The GPU vertex path's terrain stages: not re-measured here; the numbers quoted are the 13 November tuning journal's.
* The frame queue: a level change through a level exit, the pause menu's Save Game screen, input latency, vsync on.
