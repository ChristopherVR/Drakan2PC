# Performance round 2: what could be settled on a busy machine, the end-of-frame waits, start-up, exit (2026-11-12)

Follow-up to the three notes of the same day ([CPU](perf-cpu.md), [renderer](perf-gpu.md),
[loading](perf-loading-hitches.md)) and to [DLSS speed](dlss-speed.md). The task had two halves: settle the
frame-rate and load-time effect of the first round's changes on a quiet machine (Part A, with the foreground
question), and continue with the next costs outside the renderer backends (Part B). Scratch data (runs, logs,
frozen play folders, queue scripts) is under `build/review/perf2/` and is not tracked.

## Result in short

- **Part A was not measured.** The machine was never quiet: from 17:15 to the end of this session every check
  found 2 to 5 other `drakan.exe`, up to 4 `gsr_replay*` and up to 5 `cl.exe`. The benchmarks are written, queued
  and gated (at most 1 other game, no replay tool, no compiler); the gate did not open once. No frame-rate or
  load-time verdict for any first-round switch exists yet. Nothing was reverted or turned off.
- **The foreground question is not answered** for the same reason (a foreground run on a loaded GPU answers
  nothing). What the code says is in the section below: every world frame waits for its own host present, so a
  paced present limits the world rate directly.
- **Part B, proven with counts and CPU time (not with frame rates):**
  1. The end-of-frame wait is three waits, not one. In the views that read the 32 row probe image back, the EE
     makes 0.98 to 5.1 million FINISH polls per world frame; in every view it also waits for the VU1 worker once per
     flip, in the flip routine's own readiness test and in the display commit. A frame queue and a late read-back
     (both opt-in, default off) take all three away: FINISH polls per frame go to 1, the EE's wait for the VU1
     worker from 0.65 to 3.1 ms per frame to 0.00 to 0.01 ms, EE thread CPU per world frame from 19.2 to 22.0 ms to
     7.2 to 7.4 ms in Surdana and the arena (77.5 to 10.6 ms in the Ruined Village, which ran at 10 to 16
     world fps under the load). Of 8034 reads verified over the day, 18 would have returned different bytes one
     read late (4 of 1838 with the final build). Whether the frame gets shorter is **not established** (GPU shared
     with other games in every run).
  2. Start-up: the mod packs are opened and read while the window and the renderer start. The "mod packs" phase of
     the start-up fell from 0.615 s (0.537 to 0.673) to 0.242 s (0.200 to 0.319), four interleaved pairs, ranges
     disjoint under load; process start to first screen 1.857 s to 1.409 s (medians).
  3. Exit: the hypothesis "the 1.9 s after the window closes are DLL detach calls" is **wrong**. Ending the process
     without them was not faster in four interleaved pairs, and was withdrawn. The game's own shutdown takes 0.14 to
     0.20 s; the rest is Windows releasing the process.
- Tools: `scripts/perf/scene_ab_bench.py` (interleaved variants, a live setting cycled inside each run, quiet gate,
  foreground switch, one benchmark game at a time), `scripts/perf/memory_session.py`, wait counters in the
  `PS2X_PERF` line.

## Conditions (read this before any number)

- Machine: Ryzen 7 9800X3D (8 cores, 16 threads), RTX 5080, one 3840x2160 60 Hz display, Windows 11.
- Load: other agents' games, replay tools and compilers ran during every run of this session. Each run records
  the other `drakan.exe`, `gsr_*` and compilers before it and the peak during it; the count columns below are the
  only figures that do not depend on that. Frame rates of this session are listed where a table needs them to make
  sense of the per-frame figures, and are not results.
- Settings: the owner's real `drakan.ini` (copy of `build/review/baseline-play`, only `elf =` changed): borderless
  3840x2160, `scale = auto`, DLSS quality, HD textures, vsync off, world rate uncapped, ray effects off. Harness
  differences as in the earlier notes: audio output off, New Game on an empty card, god mode, the window never
  activated.
- Exe: incremental builds of the working tree into `build/review/perf2/play`; benchmarks run from frozen copies
  (`play-a` = the tree at 17:25 before this session's changes, `play-c` and `play-d` = with them) so that a later
  build never changes the exe under test. All comparisons are one exe with environment switches.
- Scenes (`scene_ab_bench.py --scene`): `surdana` (Level 1A start), `tundra-c`, `arena-1e`, `ravenshold`,
  `ootf-ruined` as in the earlier notes, and two new ones: `surdana-town` (`place 451816 161119 500435 3.1416`:
  the town square with houses, trees, the bridge and two or three villagers in view; a pose with many villagers on
  screen was not found, the villagers of a New Game stand far apart) and `surdana-fight`
  (`place 441400 174950 384300 3.1416`: three Wartoks attack Rynn at the cave mouth, god mode, she swings every
  0.7 s; pictures in `build/review/perf2/runs/explore1/`).
- One mistake of mine in the first hour: a background benchmark job I believed stopped kept waiting, timed out of
  its quiet gate after 30 minutes and started foreground runs on the busy machine while my start-up and count runs
  were going. Its results were deleted. The start-up pairs below may have overlapped with it; they are compared as
  interleaved pairs for that reason. The tool now takes a machine-wide lock per run and has no way to start two
  games of its own.

## Part A: status

| Change (switch that restores the old behaviour) | Scenes | Old | New | Spread | Verdict |
| --- | --- | --- | --- | --- | --- |
| `DRAKAN_VU_LANES_STRICT=1`, `DRAKAN_SCENE_CAPTURE_FAST=0`, `PS2X_DMA_CHAIN_POOL=0` (CPU round) | all seven | not measured | not measured | | **not measured**, queued (`a-<scene>`, variant `cpu-old`) |
| `GSR_RT_LAZY_SYNC=0`, `GSR_RT_UPLOAD_GPU=0`, `GSR_RB_PREFETCH=0`, `GSR_RB_SOLID=0`, `GSR_MOTION_BATCH=0`, `DRAKAN_SCENE_CAPTURE_MOTION_ONLY=0` (renderer round) | all seven | not measured | not measured | | **not measured**, queued (`a-<scene>`, variant `gpu-old`) |
| `DRAKAN_RES_CHAIN=0`, `DRAKAN_RES_LATENCY_ELAPSED=0` (loading round) | four load scenarios | not measured | not measured | | **not measured**, not queued yet |
| `GSR_UPSCALE_WINDOW_OUTPUT=0` (DLSS) and DLSS with ray effects | one scene | not measured | not measured | | **not measured** |
| Foreground against background window | `tundra-c`, `surdana` | not measured | not measured | | **not measured**, queued first (`fg-<scene>`) |

What exists instead:

- The queue (`build/review/perf2/tools/queue.py`, jobs in `queue.txt`) holds the foreground pairs, then per scene
  one benchmark of three interleaved variants (`cpu-old`, `gpu-old`, `new`), five repeats, DLSS quality and off
  cycled inside each run. Each run waits for the gate; a run that starts without it is marked and left out of the
  tables. Commands are in "Re-measuring".
- The opening scenes with the loading round's switches on (the default): see "Correctness runs".
- A count the loading round did not have: with `DRAKAN_RES_CHAIN` and `DRAKAN_RES_LATENCY_ELAPSED` at their
  defaults the Surdana loading screen made 357 to 557 disc reads, 401 to 619 resource batches and 911 to 1296
  requests in eight New Game runs, the Intro screen 400 to 421, 327 to 347 and 620 to 643 (`load_phases.py`); the
  spread comes from how much of the streaming falls inside the loading screen.

## The foreground question: what the code says

Not measured. Two facts from reading `gs_gsr_bridge.cpp` that the measurement has to be held against:

- In the uncapped mode the GS worker waits after every display commit until the window thread has presented that
  very frame (`waitForHandoff`, up to 100 ms), and the EE's commit waits for the previous commit to retire. So one
  `Present` call belongs to every world frame, and a `Present` that the compositor paces to 120 per second caps the
  world rate at 120 whatever the GPU does. The DLSS note's rows at exactly 120 presents per second with 2 to 13 ms
  inside `Present` fit this.
- The harness sets `DRAKAN_NO_ACTIVATE=1`: the window is shown with `SW_SHOWNOACTIVATE` at the bottom of the
  window order (`drakan_window.cpp`). `scene_ab_bench.py --variant fg:DRAKAN_NO_ACTIVATE=0` runs the same pose with
  the window activated and records, per run, in how many samples the foreground window belonged to the game.

The one gated run that exists (background window, Surdana, 19:31, one other game at its start and two at the peak)
shows the pattern again: 119.9 and 119.7 presents per second with DLSS quality and off, 2.4 and 2.1 ms inside
`Present` per call, 80.1 and 83.7 world fps, GPU `world` time 1.4 and 1.9 ms per world. Its foreground partner did
not get a quiet start before this note was written, so it proves nothing by itself.

Until the pairs exist, every absolute frame rate of today's notes stands as "background window".

## Part B

### 1. The end-of-frame wait

**What the game does** (PAL executable, generated code read in `build/final-game/generated`):

- `1dbfa8` is the engine's GS store-image routine: it flushes the display list, sends the transfer set-up with a
  FINISH register write through the GIF channel, polls CSR.FINISH (first read at `1dc668`, loop at `1dc688`), sets
  the VIF1 FIFO direction, starts a VIF1 DMA with CHCR.DIR=0 for the pixels, and clears FINISH. `1dda88` calls it
  for a 32 row rectangle of a locked surface (4096 bytes per read); the renderer note identified the image as the
  depth probe sprite.
- The flip worker `1d78f0` tests, before every flip, the VIF1 status (`& 0xC00`), a second status register (`& 3`)
  and the STR bits of two DMA channels, and only then calls the flip leaf `1e0048`.

**Where the EE thread waited** (three places, found with the new counters and one stack profile):

1. the FINISH poll: a busy loop until the VU1 worker and then the GS worker have processed everything queued;
2. the read itself (`vu1async::sync` and the bridge's drain), and the VIF1_STAT writes around it, which waited for
   the VU1 worker because every VIF register access does;
3. once per flip, in every view: the flip worker's status reads (each a `vu1async::sync`) and
   `drakan_dlss::commitDisplay` (`vu1async::sync` before the commit record), plus the same at a vertical blank after
   a flip (`GS::vsyncHost`). With the read-back made late but the flip left alone, the stack profile had 2979 of
   3665 EE samples in that one chain (`Load32` in the flip worker, `vu1async::syncSlow`).

**What must be complete, and when.** Nothing the EE reads in these places is data it needs before it can go on:

- the FINISH poll only guards the read that follows, and the read waits by itself;
- the status reads of the flip worker ask whether everything kicked has been taken. Their purpose is that the
  flip shows a finished frame. Here the flip is a record in the same ordered stream as the draws, so order alone
  gives that;
- the probe image is the one piece of data. It has to be the image of a finished frame, but not of this frame:
  the game reads it once per world frame and the same caller reads the same rectangle again in the next one.

**What was built** (all opt-in, default unchanged; `runtime/ps2_readback.h` has the description):

| Switch | Effect |
| --- | --- |
| `PS2X_READBACK=eager` | the FINISH poll of `1dbfa8` succeeds at once; the read still blocks. Same bytes as before, no spinning. |
| `PS2X_READBACK=late` | as eager, and a read of at most 16384 bytes whose caller (a hash of the routine's arguments without the destination) and size were seen before is answered with that caller's previous bytes. A render task queued behind the transfer set-up makes the GS worker take the current pixels when it gets there (`GsrBridge::readLocalToHostLater`); they answer the next read. The VIF1_STAT writes of the routine no longer wait. |
| `PS2X_READBACK_VERIFY=1` | with late: every read is also done the blocking way, the bytes the late answer would have been are compared, the exact bytes are served. |
| `PS2X_FRAME_QUEUE=1` | the display commit and the vertical blank are queued as render tasks behind the VU1 worker's pending work, with the display registers snapshotted on the EE at that moment (`snapshotDisplay`, `commitDisplay(words)`, `vsync(tick, words)`); the flip worker's status reads get the value the register had the last time the worker was idle. The renderer sees the same records in the same order. The EE runs ahead by at most the VU1 queue (256 jobs, about half a world frame of DMA chains). |

`poll` (default) keeps the old behaviour in all of it. Late without the frame queue only moves the wait to the
flip (measured: `vu1_sync_ms` rose to 2.6 to 16 ms per frame), so the two belong together ("pipeline" below).

**Counts** (per world frame; `scene_ab_bench.py`, one run per cell, 8 s window, owner's settings, 2 to 5 other
games; `play-b` = exe with the read-back modes, `play-d` = final exe with the frame queue):

| Scene | Read-backs | FINISH polls, poll | FINISH polls, eager or late | EE wait for VU1, poll (ms) | EE wait for VU1, pipeline (ms) | Read wait, pipeline (ms) | EE CPU, poll (ms) | EE CPU, pipeline (ms) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| surdana (`play-d`) | 1.00 | 1,246,974 | 1 | 1.01 | 0.00 | 0.00 | 21.99 | 7.22 |
| arena-1e (`play-d`) | 1.01 | 977,641 | 1 | 0.65 | 0.01 | 0.11 | 19.18 | 7.44 |
| surdana-fight (`play-d`) | 0 | 0 | 0 | 3.12 | 0.00 | 0.00 | 8.64 | 11.23 |
| ootf-ruined (`play-d`) | 1.00 | 5,104,960 | 1 | 0.00 | 0.00 | 0.00 | 77.49 | 10.64 |
| ravenshold (`play-d`) | 0 | 0 | 0 | 0.09 | 0.00 | 0.00 | 5.86 | 4.64 |
| tundra-c (`play-b`; the `play-d` poll and pipeline runs had no measured window) | 0 | 0 | 0 | 0.38 | | | 8.32 | |
| surdana-town (`play-b`) | 0 | 0 | 0 | 0.25 | | | 10.58 | |

- The probe read is per world frame in three of the seven poses and absent in four (in the measured window; all
  levels read during their first frames). Where it is absent there is
  no FINISH poll at all, so the earlier "40 to 70 % of the EE in the poll in every scene" holds for the views that
  read back, not for every view.
- The number of polls per frame is a measure of how long the poll spins, and so of the load: 126 thousand per frame
  in the quietest Surdana run of the day, 1.25 million in the one above, 5.1 million in the Ruined Village at 9.7
  world fps. In the quietest run the eager mode took 2.3 ms of EE CPU per frame (10.41 to 8.08 ms).
- EE CPU per frame in the fight went up with the pipeline (8.64 to 11.23 ms, one run each, 3 to 4 other games):
  the EE no longer sleeps in the wait and does more per frame when the machine gives it less; no conclusion.

**Verification of the late answer** (`PS2X_READBACK_VERIFY=1`: every read done the blocking way and compared
with the bytes of the same caller's previous read; the counts include the reads at the level start):

| Build | surdana | arena-1e | ootf-ruined | tundra-c | surdana-fight | surdana-town | Sum |
| --- | --- | --- | --- | --- | --- | --- | --- |
| read-back modes only (`play-b`) | 0 of 1638 | 5 of 1270 | 1 of 1213 | 1 of 173 | 0 of 140 | 0 of 107 | 7 of 4541 |
| with the frame queue, first version (`play-c`) | 0 of 726 | 3 of 192 | 3 of 517 | 1 of 116 | 0 of 104 | | 7 of 1655 |
| final (`play-d`) | 0 of 765 | 1 of 500 | 1 of 343 | 2 of 125 | 0 of 105 | | 4 of 1838 |

So the image is nearly, not strictly, the same from one read to the next (the renderer draws the probe untextured
today, see the renderer note): a late read changes what the guest sees in about two reads in a thousand, by one
read. The scenes without a per-frame read (tundra-c, the fight, the town) still read at the level start, which is
where their counts come from.

**Not established, and the risks:**

- Any frame-rate or frame-time gain. The gated pairs are queued (`p-<scene>`: `poll` against
  `PS2X_READBACK=late,PS2X_FRAME_QUEUE=1`).
- Frame pacing with the EE running ahead: the engine measures its tick time between flips on the EE; with the
  frame queue a flip no longer waits for the frame before it to be fed, so tick times can become uneven when the
  EE alternates between running ahead and waiting for queue space. One pipeline run had a single 211 ms frame
  interval that the poll run beside it did not have. Needs the interval spread (1 % and 0.1 % lows) of the gated
  runs, a walk, vsync on and a frame limit.
- Input latency grows by the frames the EE is ahead (at most one).
- Once the probe samples real depth (renderer work), its result arrives one frame late in late mode. For an
  occlusion test of a light that is a common trade; it is a decision for whoever fixes the probe.
- The flip worker's status reads return the last idle value. A VIF1 stall or error state that the worker reaches
  while the EE is ahead is seen one flip late. The game does not use VIF1 interrupts or stalls in play as far as
  the DMA paths show; not proven.
- Menus, movies, the loading screens, save and load, a level change and the Order of the Flame campaign with the
  pipeline on: one unskipped opening run each is queued ("Correctness runs"); nothing else was played.
- USA executable: the addresses are PAL's; on other builds the switches do nothing.

### 2. The level loader and the display flips (measured, not changed)

Between the menu choice and the loading screen (GUI screen 8, which contains `Level_Load`) three runs made 140,
210 and 348 flips in 1.46, 1.52 and 3.49 s, each flip presented (141, 211, 349 presents), with the EE thread at 3
to 15 % of a core and 54 to 160 MiB per second of GIF data: every one of those menu frames uploads its full 512x512
picture again and waits for its own present. The loader's steps ride on those frames, so its time is the number
of steps times the frame time, not disc or CPU work. Not changed: skipping the present wait while a load is in
progress needs a "loading" signal in the bridge's hand-off and its own check of the loading screens and the save
thumbnails; the gain on a quiet machine is bounded by the 0.77 s `Level_Load` of a Surdana load measured at 17:34
(one run, 3 other games).

### 3. Start-up

Timeline of one start (17:34, 3 other games): process start to first screen 1.57 s, of it 0.34 s before the
renderer starts (audio device, window), renderer 0.36 s, mod packs 0.65 s, services and first screen 0.17 s. The
pack work followed the renderer start-up on the same thread.

Change (commit `fbfdb5cd`): `main` calls `drakanStartModsEarly` before the runtime starts; a thread makes the
`ModManager`, loads the packs and reads every member the install will need (`ModManager::prefetchFiles`, the same
8-thread reader). The install itself (whole files, container rebuilds, level list) still runs in the service
start-up and takes the read-ahead when the folder and the language match, otherwise it reads as before.
`DRAKAN_MODS_EARLY=0` restores the old order.

Four interleaved pairs (`old` = `DRAKAN_MODS_EARLY=0`, 1 to 5 other games; `load_phases.py`, seconds, median
[lowest .. highest]):

| Phase | old | new |
| --- | --- | --- |
| start-up: mod packs (`[mods] disc profile` to `[mods] N pack(s) active`) | 0.615 [0.537 .. 0.673] | 0.242 [0.200 .. 0.319] |
| start-up: renderer ready | 0.938 [0.790 .. 3.620] | 0.929 [0.712 .. 0.984] |
| start-up: process start to first guest screen | 1.857 [1.642 .. 4.450] | 1.409 [1.165 .. 1.527] |
| pack install as logged by the game (read time included) | 0.507 [0.441 .. 0.558] | 0.554 [0.507 .. 0.629] |

- The pack phase is shorter in every pair and the ranges do not overlap. The whole start-up is shorter in the
  medians; its ranges overlap because of one loaded `old` run (4.45 s).
- The work itself is not smaller (the logged install is the same or slightly longer beside the renderer's shader
  loading); it overlaps.
- Not done: serving rebuilt containers as segments instead of holding 408 MiB of pack files in memory, and a
  cache for `ModOverride::apply`. The second is 51 to 57 small zip reads per Surdana loading screen (107 per New
  Game run) on the resource worker; no measurement showed it in a load time, so it was left.
- Test: `mod_test` gains a read-ahead section (same served files with the matching language, a read-ahead for
  another language is dropped): 224 checks pass.

### 4. Exit

The game now prints `[run] shutdown: threads N ms, renderer N ms, window N ms`. Eight runs: threads 42 to 47 ms,
renderer 80 to 143 ms, window 3 to 9 ms. From the scripted quit to the `[gsr] exit` line 0.06 s. So of the 0.7 to
2 s between the quit and the end of the process about 0.2 s is the game's own shutdown and the rest follows
`std::_Exit`.

Experiment: end the process with `TerminateProcess` after the log flush, which skips the DLL detach calls. Four
interleaved pairs, script quit to process end: with detach 0.877 s [0.680 .. 5.602], without 4.176 s [0.800 ..
8.499]. Not faster in the lowest runs (0.68 against 0.80 s) and not in the medians; the spread is the machine
load. The change was withdrawn before any commit. What remains after the game's own 0.2 s is Windows tearing down
the process (4.5 to 5.8 GiB of private memory, the GPU allocations); nothing in the game's code shortens that.

### 5 to 8. Not done or not established

- Worst frame intervals in the first seconds after a level start: not looked at.
- Order of the Flame terrain texture coordinates: the converter stores them as eight 16 bit values per cell
  (`cellUv`, `0xffff` = 1.0, `editor/packages/formats/src/terrain.ts`), so the huge values are not in the
  converted data as such; they are produced in the guest or the microcode from it. Where (cells without a texture
  in a layer are the first suspect) is not established.
- Posted guest calls served only outside host calls: not looked at.
- Memory: one 30 minute session, see "Memory" below. No growth while standing in a level; growth across level
  changes not established.

## Correctness runs

Unskipped opening scenes (`play_run.py --keep-cutscene --timestamps`, owner's settings, 1 to 4 other games; a run
passes when both levels load, the opening ends with the player under control and the scripted quit is reached with
exit code 0; no `[bad-resume]`, missing branch target or fatal line in any of them):

| Run | Switches | Levels loaded | Player under control at | Result |
| --- | --- | --- | --- | --- |
| Drakan 2, three runs (`cut-d2-1..3`, exe `play-c`) | defaults, so `DRAKAN_RES_CHAIN` and `DRAKAN_RES_LATENCY_ELAPSED` on | Level 0 - Intro, Level 1A - Surdana | 204.4, 204.9, 205.2 s | 3 of 3 pass |
| Order of the Flame, three runs (`cut-ootf-1..3`, `DRAKAN_GAME=d1`) | defaults | OotF 00 - Intro, OotF 01 - Ruined Village | 257.5, 261.9, 257.0 s | 3 of 3 pass |
| Drakan 2, one run (`cut-d2-pipe`, exe `play-d`) | `PS2X_READBACK=late PS2X_FRAME_QUEUE=1` | the same | 208.7 s | pass; 2095 read-backs, 2081 answered late |
| Order of the Flame, one run (`cut-ootf-pipe`) | the same | the same | 262.7 s | pass; 6826 read-backs, 6787 answered late |

- This is the check the loading round's switches still lacked (three unskipped runs per game with them on). It
  says they do not break the opening scenes; it says nothing about load times.
- The times to control are scene lengths (the Intro scene and the Surdana opening play at their own pace), equal
  within 1 s in a group and 4 to 5 s later with the pipeline on in one run each; not a measurement.
- Not looked at in these runs: the pictures of the scenes (no screenshots were compared), sound, a save and a
  load, a level change by walking.
- A default-settings New Game of the last build (`smoke-final`, exe of build 8) reached its scripted quit with exit
  code 0; its log shows the pack read-ahead and the shutdown line.

## Memory

One session of 30 minutes (`memory_session.py`, exe `play-c`, owner's settings, the player standing still, sampled
every 13 s, 136 samples; memory figures do not depend on the machine load):

| Stay | Seconds | Private MiB, low / high / last | Working set MiB | GPU dedicated MiB | GPU shared MiB | Handles |
| --- | --- | --- | --- | --- | --- | --- |
| Level 1A - Surdana (from New Game) | 52 | 3513 / 4204 / 3514 | 1396 / 1541 / 1399 | 2477 / 2895 / 2477 | 77 / 134 / 134 | 1004 / 1019 / 1006 |
| Level 1D - City of the Damned (after one fired level change) | 1741 | 3440 / 3486 / 3446 | 1222 / 1333 / 1226 | 2439 / 2463 / 2439 | 0 / 88 / 0 | 972 / 1010 / 980 |

- Standing in one level for 29 minutes nothing grows: private bytes stay inside 46 MiB, GPU dedicated memory inside
  24 MiB, handles and threads (94 to 109) are flat. The rise of 0.7 GiB of GPU memory in 13 minutes that the
  loading round saw came from a walk that kept entering new ground; a standing view does not show it. Whether a
  walk levels off is still not established.
- Private bytes were 3.4 to 3.5 GiB here against the 4.5 to 5.8 GiB of the loading round's walk: the difference
  is what a walk streams in. The figure was not broken down further (408 MiB of it are the pack files).
- **Memory across several level changes is still not established.** The session was meant to change level eight
  times; `DRAKAN_LEVELCHANGE` fired the first step (Surdana to the City of the Damned, 54 s after control) and
  then only repeated its arrival report every 100 frames for the rest of the run, although the City has an exit
  back to Surdana in its list. The test aid does not chain here; that is a harness fault to look at, not a memory
  result.

## Tools

- `scripts/perf/scene_ab_bench.py`: any number of environment variants interleaved across runs (order rotated per
  repeat), a live setting cycled inside each run (`--cycle video.dlss=quality,off`), world fps, median, 1 % and
  0.1 % low and longest frame interval, CPU per world frame of the EE thread and of every named thread, presents
  per second, time inside `Present`, GPU time per pass, the per-world counters of the perf line, per-repeat pairs
  for two variants. Quiet gate per run (`--wait-quiet`, `--quiet-games`), load before and peak during each run,
  `--foreground`, a machine-wide lock so that two benchmark games of ours never run together, the own game found
  by its exe path.
- `scripts/perf/memory_session.py`: one long session with `DRAKAN_LEVELCHANGE` steps, process and GPU memory
  sampled, one row per stay in a level.
- `PS2X_PERF` line: `rb/s`, `rb_wait_ms/s`, `finish_polls/s`, `rb_eager/s`, `rb_late/s`, `rb_verified/s`,
  `rb_diff/s`, `vu1_syncs/s`, `vu1_sync_ms/s`; a `[readback]` total and the `[run] shutdown:` line at exit.
- `docs/reference/env-flags.md`: the three DLSS switches that were missing (`GSR_UPSCALE_WINDOW_OUTPUT`,
  `GSR_GPU_PASS_TIMING`, `GSR_UPSCALE_VIEW_DEBUG`) and this session's (`PS2X_READBACK`, `PS2X_READBACK_VERIFY`,
  `PS2X_FRAME_QUEUE`, `DRAKAN_MODS_EARLY`).

## Findings for the renderer sessions

- The world rate is tied to `Present` frame by frame (`waitForHandoff` after every commit). If background
  presents are paced, the fix is not in the upscaler; and with the frame queue the EE no longer hides a slow
  present behind its own waits, so `presentCall` is the first thing to look at in the gated runs.
- While a level loads the menu loop re-uploads its 512x512 picture every flip (54 to 160 MiB per second of GIF
  data at 100 to 160 flips per second).
- The probe image changes in about one read in a thousand between consecutive frames even though the sprite is
  drawn untextured.
- `GsrBridge` gained `snapshotDisplay`, `commitDisplay(words)`, `vsync(tick, words)` and `readLocalToHostLater`
  (in `ps2xRuntime/src/lib/gs`, not in the backend files).

## Tests and builds as they were

- Game builds with the shared lock, `Build-LocalGame.ps1 -Incremental` into `build/review/perf2/play`: eight.
  Build 1 found nothing to do. Build 2 failed (two string literals broken by my edit script: `newline in
  constant` in `ps2_runtime.cpp`). Build 3 failed at the link (`drakanStartModsEarly` unresolved: I edited
  `drakan_overrides.cpp` while it was compiling); it also recompiled all generated code because
  `runtime/ps2_memory.h` changed. Builds 4 to 8 ended with exit 0; the last one was made from the tree as it then was, other sessions'
  uncommitted edits included, and passed a default-settings smoke run.
- `runtime/hle` configured alone into `build/review/perf2/hle` (Release): `mod_test` 224 checks, 0 failed;
  `resource_sequence_test` 24, 0 failed; `override_test` 73, 0 failed. `hle_test` (needs the ISO) was not run.
- `clang-format --dry-run --Werror` (19.1.7) on every C++ file touched: clean. `ruff check` and `ruff format` on
  the two new scripts: clean. `scripts/repository_guard.py`: 0 prohibited files before every commit.
- `python scripts/run_tests.py`: 557 tests, OK (2 skipped).
- Not run: the root `runtime-tests` preset, the renderer tests, any unit test of
  `ps2_readback.h` or of the new bridge functions (they are covered only by the live verify counts and the runs
  below).
- About 85 scripted game runs of this session reached their scripted quit with exit code 0; five count runs
  produced no measured window (the game was still loading when the window ended under load). One gated benchmark
  job died in my lock code (a process list that came back empty freed a live lock); fixed in `d7726716` and the
  job queued again.

## Re-measuring

```sh
# foreground against background, interleaved (takes the desktop focus for the fg runs)
python scripts/perf/scene_ab_bench.py --exe <play>/drakan.exe --name fg-tundra-c --scene tundra-c --repeats 3 \
    --cycle video.dlss=quality,off --variant bg --variant fg:DRAKAN_NO_ACTIVATE=0
# Part A, one scene: CPU round old, renderer round old, new
python scripts/perf/scene_ab_bench.py --exe <play>/drakan.exe --name a-surdana --scene surdana --repeats 5 \
    --cycle video.dlss=quality,off \
    --variant cpu-old:DRAKAN_VU_LANES_STRICT=1,DRAKAN_SCENE_CAPTURE_FAST=0,PS2X_DMA_CHAIN_POOL=0 \
    --variant gpu-old:GSR_RT_LAZY_SYNC=0,GSR_RT_UPLOAD_GPU=0,GSR_RB_PREFETCH=0,GSR_RB_SOLID=0,GSR_MOTION_BATCH=0,DRAKAN_SCENE_CAPTURE_MOTION_ONLY=0 \
    --variant new
# the pipeline of this note against the default
python scripts/perf/scene_ab_bench.py --exe <play>/drakan.exe --name p-surdana --scene surdana --repeats 5 \
    --cycle video.dlss=quality,off --variant poll --variant pipe:PS2X_READBACK=late,PS2X_FRAME_QUEUE=1
# counts only, any machine load: add --wait-quiet 0 --keep-busy --repeats 1 and a variant with PS2X_READBACK_VERIFY=1
python scripts/perf/scene_ab_bench.py --summarise build/bench/p-surdana
# start-up and exit phases, old against new
python scripts/harness/play_run.py --exe <play>/drakan.exe --name s1 --timestamps --shots 0,60 \
    --env DRAKAN_IO_STATS=1 --env DRAKAN_LIFECYCLE_LOG=1 [--env DRAKAN_MODS_EARLY=0]
python scripts/perf/load_phases.py --label old <runs> --label new <runs>
# memory over 30 minutes with level changes
python scripts/perf/memory_session.py --exe <play>/drakan.exe --name mem30 --minutes 30 \
    --levelchange "Level 1D - City of the Damned@6000,Level 1A - Surdana,Level 1D - City of the Damned"
```

The gated queue of this session is `build/review/perf2/tools/queue.py` with `queue.txt` (scratch); it resumes at
the first unfinished line.

## Not established

- Everything in Part A, and the foreground question.
- Any frame-rate, frame-time or pacing effect of `PS2X_READBACK` and `PS2X_FRAME_QUEUE`; whether they should
  become the default.
- A quiet-machine start-up time (the phases above were taken with 1 to 5 other games).
- What the 0.5 to 1.8 s of process teardown after the game's own shutdown consist of.
- Where the huge terrain texture coordinates of the Order of the Flame levels are produced.
- Items 5 and 7 of the task list.
