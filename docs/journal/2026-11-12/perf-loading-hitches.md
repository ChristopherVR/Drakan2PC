# Start-up, load times, hitches, memory: measurements and three fixes (2026-11-12)

Scope: everything that is not the cost of a normal frame. Start-up, level loads, level change, save and load, single
long frames during play, memory over a session, exit. The CPU cost of a frame, the renderer and the Vulkan option
belong to other sessions; findings for them are in section 8.

## 1. Conditions (read this before any number)

* Machine: 8 cores / 16 threads, RTX 5080, NVMe disc (about 900 MiB/s cold, 2000 MiB/s cached for a 157 MiB file).
* **The machine was never quiet.** Between 3 and 10 other `drakan.exe` and several compilers ran during every
  measurement (the count at the start of each run is in the run's `.out` file under `build/review/perf-load/runs/`),
  total CPU load was at 100 % whenever it was sampled, and the one GPU was shared. In the hitch runs the median frame was
  27 to 33 ms (the quiet-machine figure of earlier journals for the same walk is about 5 ms). Wall-clock times therefore
  vary by a factor of up to 25 between identical runs (the same code installed the mod packs in 0.7 to 7.7 s in ten
  runs). Every time below is "under this load". What does not depend on the load is stated as such: counts of file
  opens, reads, bytes, resource batches, thread hand-overs, and process CPU time.
* Settings: the owner's `drakan.ini` (borderless 3840x2160, `scale = auto`, DLSS quality, HD textures on,
  `texture_upscale = auto`, world rate uncapped, vsync off), mod packs `ootf-levels` (11234 members, 157 MiB zip) and
  `drakan-character-motion` (6149 members, 27 MiB zip). Audio off (harness default).
* Builds: "old exe" = `build/review/play/drakan.exe` (12 Nov 05:30). "new exe" = `build/review/perf-load/play/drakan.exe`
  (tree of 12 Nov 08:5x with every session's uncommitted work). The resource changes are compared on the new exe alone
  with their switches (`DRAKAN_RES_CHAIN=0 DRAKAN_RES_LATENCY_ELAPSED=0` = old behaviour); the mod pack change is
  compared with a bench built from `HEAD` and from the tree (`build/review/perf-load/bench*`).
* Cold runs: not done as full game runs. `scripts/perf/evict_file_cache.py` drops files from the Windows cache without
  admin rights; on this disc a cold read of the 157 MiB pack costs 0.17 s against 0.08 s cached, so the cold penalty
  of the files a start-up or a level load reads (packs 184 MiB, about 30 MiB of the ISO per level) is a few tenths of a
  second and below the noise above. A cold shader cache was measured once (section 2).

## 2. Phase times

`python scripts/harness/play_run.py --timestamps ...` and `python scripts/perf/load_phases.py <runs>` (section 9).
Seconds.

Old exe, New Game to Surdana (the harness taps through the menus every 3 s, so only the phases below are comparable):

| Phase | old exe, 4 other games (`base1`) | old exe, 7 other games (`ng_base_1`) |
| --- | --- | --- |
| start-up: process start to the first guest screen | 7.28 | 9.67 |
| of it: renderer ready | 0.79 | 0.86 |
| of it: mod packs | **6.25** | **8.36** |
| loading screen 1 (Level 0 - Intro) | 0.92 | 1.72 |
| loading screen 2 (Level 1A - Surdana) | 1.87 | 7.56 |
| exit: scripted quit to the end of the process | 2.01 | 10.06 |

New exe, same recipe, the resource switches alternated (5 runs each, 4 to 9 other games; median [lowest .. highest]):

| Phase | old resource path | new (default) |
| --- | --- | --- |
| loading screen 1 (Intro) | 1.66 [0.70 .. 15.28] | 1.60 [1.31 .. 4.29] |
| loading screen 2 (Surdana) | 2.63 [0.78 .. 19.89] | 1.01 [0.82 .. 3.83] |
| level loader, Intro (`beforeLevelLoad` to `afterLevelLoad`) | 0.86 [0.26 .. 2.22] | 0.80 [0.63 .. 1.61] |
| level loader, Surdana | 2.24 [1.30 .. 2.83] | 1.70 [1.44 .. 1.79] |

Other scenarios on the new exe, 2 runs per side, 3 to 6 other games. All 12 runs reached their scripted quit. With two
runs per side and this noise no difference between the sides is established for any of them:

| Scenario | Phase | old resource path | new |
| --- | --- | --- | --- |
| Order of the Flame, game choice `d1` | loading screen 1 (OotF 00 - Intro) | 0.99, 1.29 | 0.78, 0.95 |
| | loading screen 2 (OotF 01 - Ruined Village) | 0.37, 0.61 | 0.12, 0.30 |
| | level loader, Ruined Village | 0.56, 0.69 | 0.58, 0.59 |
| Level change Surdana to City of the Damned (`DRAKAN_LEVELCHANGE`) | save written before the change | 0.86, 1.06 | 0.81, 1.20 |
| | level loader, City of the Damned | 0.35, 0.53 | 0.35, 0.72 |
| | loading screen 3 | 0.27, 0.53 | 0.17, 1.41 |
| Quick save F5, quick load F8 in Surdana | save write | 1.00 to 1.05 | 1.12 to 1.96 |
| | save restore (F8) | 1.18, 1.32 | 1.41, 1.87 |

Cold shader cache (new exe, first run of a new play folder, 9 other games): `renderer start-up 2794 ms: 16 compiled
(5267 ms), 0 from the disk cache`; with the cache 222 to 305 ms. That is the existing design of 2026-11-08 and happens
once per install.

Start-up of the new exe: the game logged `pack files installed in 718 to 1582 ms` in most runs (up to 7.7 s in the
worst) against 6.1 to 8.4 s for the old exe. A quiet-machine start-up figure does not exist; from the parts it is
about 0.8 s (renderer) + 1.0 s (packs) + 0.3 s.

## 3. Where the time goes

1. **Start-up: 86 % was the mod packs.** `ModManager::installFiles` read 17126 pack members one after another; for
   every member `ZipArchive::read` scanned the member list from the start, opened the zip file again, inflated bit by
   bit (a decoder after zlib's `puff`) and ran a byte-wise CRC. Under load the 17126 file opens alone took over 20 s of
   wall time (bench, section 4). Then 20 containers are rebuilt in memory with an entry scan per patch.
2. **Level loads are hand-overs between two threads, not disc or CPU work.** A Surdana load is about 520 resource
   batches with 1150 requests and 12 MiB (Intro: 340 / 630 / 5.8 MiB); the disc reads of the whole load take 2 to 5 ms.
   Every batch was served by the resource worker with one wait for 50 us of guest time and then, per request, a blocking
   call into the guest for the buffer (0x1303) and one for the delivery (0x1301): about 2800 round trips with the EE
   thread per level, each two thread wake-ups, each waiting until the EE thread is back from whatever host call it is in
   (during a loading screen: the present hand-over). Measured on the loading screen (`[io]` lines): the worker was busy
   the whole time, 45 % in the latency wait and 42 % in the EE calls; under heavy load one request took 25 ms and the
   screen 20 to 28 s while the game process used under one core. That is why earlier journals saw "55 s to load while
   three other instances ran".
3. **The level loader itself (`Level_Load`, before the loading screen appears) is 1.3 to 2.8 s for Surdana with almost
   no I/O** (about 180 disc reads, 55 batches) and an EE thread at roughly half a core. In one run it took 21 s while
   the guest completed 1 flip per second: the loader's file calls advance between display flips and each flip waited
   for the starved host presenter. This is the pacing / present hand-over, not the HLE services (section 8).
4. **Hitches in play are GPU readbacks and the present, not file or resource work** (section 5).
5. **Exit: the window closes 0.16 s after the quit; the process then lives 1.9 s more** (10 s under load) inside
   `std::_Exit`, i.e. in Windows' process teardown (DLL detach, GPU and memory release). Not changed (section 7).

## 4. Fixes

### 4.1 Mod packs at start-up (`runtime/hle/src/ModPack.cpp`)

* `ZipArchive`: member index (hash map) instead of a scan per read; the file stays open for a batch of reads
  (`ModArchive::holdOpen`, closed again after the batch, so the pack is not held open while the game runs); reads are
  safe from several threads.
* Inflate: 64 bit bit buffer and a 10 bit lookup per symbol, the bit-wise walk only for longer codes; CRC-32 eight
  bytes per step.
* `installFiles` reads all members on up to 8 threads (one on the browser build), then does the same steps in the same
  order as before. `patchSrsc` finds entries by (type, id) and returns the reserve of in-place patches.
* A log line with the cost: `mods: pack files installed in N ms: 17126 members (303.9 MB) read in ..., whole files ...,
  containers rebuilt in ...`.

Bench (`build/review/perf-load/bench`: ISO + both packs, `ModManager::load` + `installFiles`, 5 alternating runs each,
machine as loaded as described; the 100 files served from memory hash to the same FNV-1a value `a47a780cc6507471` and
the same 407,969,696 bytes before and after):

| | `HEAD` | tree |
| --- | --- | --- |
| install, wall s (the five runs) | 28.6, 34.0, 31.6, 31.1, 30.1 | 1.08, 1.08, 2.07, 0.86, 1.33 |
| install, wall s, earlier and less loaded (3 / 4 runs) | 5.98, 6.37, 9.11 | 0.81 to 1.16 |
| process CPU s | 6.7 to 6.9 | 2.0 to 2.2 |
| zip file opens | 17126 + about 60 | 2 per pack and reader thread |
| private bytes after install | 458 to 463 MiB | 413 to 418 MiB |

The CPU time and the open count do not depend on the load; the wall times do. Tests: `mod_test` (new: streams with long
codes, a stored block inside the bit stream, fixed codes, overlapping matches, every truncation, 4000 damaged streams,
120 members read from 8 threads with and without the held file, the file can be renamed after `holdOpen(false)`).

### 4.2 Resource batches run on the EE thread (`ResourceService`, `EeRpcPoster::run`)

`ResourceService::handle` now does the disc work of a whole batch first (`prepare`: lookup, override layer, read, ADPCM
fix-ups) and hands its EE side over as one `hle::IEeCallSequence` (`BatchCalls`: the calls 0x1303 / 0x1301 per request
with the memory and SPU2 writes between them, then the 0x1302 completions). `IEeRpcPoster::run` posts them one by one by
default (tests, other hosts). The game's `EeRpcPoster::run` issues the first call from the worker and every further one
from the completion of the one before, on the EE thread: one hand-over per batch instead of two per call.
`DRAKAN_RES_CHAIN=0` restores call by call.

### 4.3 The resource latency counts from the submission (`drakan_overrides.cpp`)

The 50 us of guest time a completion must stay behind its request (the requester's bookkeeping, see
`2026-10-17/crash-listener.md`) were waited after the worker took a batch, also when the batch had been queued behind
a hundred others for a long time. The batch now records the EE cycle count at submission; a batch whose latency plus
16384 cycles of slack has passed is served at once. `DRAKAN_RES_LATENCY_ELAPSED=0` restores the wait.

Numbers for 4.2 + 4.3 together (new exe, switches alternated, section 2): loading screen 2 median 2.63 s to 1.01 s; the
lowest runs are equal (0.78 and 0.82 s), the highest 19.9 s against 3.8 s. Honest reading: **on a lightly loaded
machine no gain is shown; the gain is that a load no longer multiplies when the machine or the GPU is busy.** Load
independent: latency waits skipped for 356 to 560 of 406 to 563 batches of the Surdana loading screen (88 to 100 %);
calls per batch unchanged; hand-overs per level from about 2800 (one per latency wait and per call) to about 520 to 600
(one per batch plus the remaining latency waits). The two switches were not measured separately.

**Guest-visible timing.** Both changes alter when the guest sees completions, inside what it could already see: the
calls of one batch now run back to back on the EE thread (before, guest code of the main thread could run between
them when the worker was slow to come back), and a queued batch completes without a further 50 us of guest time. No
completion arrives earlier than 50 us of guest time after its own submission, and the order of calls is unchanged.
Checked after the change, all on the new path: New Game with the scenes skipped by the harness, to player control in
Surdana, 5 of 5 runs; **the scenes unskipped** (`--keep-cutscene`, `cutscene_new2`): the Intro level's scene played for
2 minutes, Surdana loaded, its opening played 85 s and ended with the player in control; Order of the Flame Intro and
Ruined Village 2 of 2; level change to City of the Damned 2 of 2; quick save and quick load (a save made in the run) 2
of 2; three hitch walks; 13 minutes in Surdana (section 6). A first unskipped run (`cutscene_new`) did not finish in
its 420 s: another of my runs shared the machine and the guest completed 1.5 flips per second; it was still streaming
when the limit ended it. **Not checked:** saves made by an older build (the cards at hand are refused by the save guard
before any load, `different build (pack 'ootf-levels' ...)`, which is independent of this work), the whole intro movie
(`skip_intro_videos = 1` in the owner's settings), the USA disc, the browser and Linux builds, the session of section 6
that ended at the title after a fired level change (not explained).

Tests: `resource_sequence_test` (new, no disc: calls, payloads, order and memory writes of a mixed batch; a host that
runs the sequence on another thread makes identical calls; failed allocation; 64 completions per call; bad handle),
`hle_test` with the ISO 509 checks, `override_test`.

### 4.4 Measurement aids

`DRAKAN_IO_STATS=1` (`[io]` lines), `[ft] R` lines with `DRAKAN_FRAME_AUDIT=2`, `IsoFs::readStats`,
`ResourceService::stats`, `play_run.py --timestamps`, `scripts/perf/load_phases.py`, `hitch_survey.py`,
`evict_file_cache.py`, recipes `perf_walk.txt` and `perf_walk_surdana.txt`.

## 5. Hitch survey

`DRAKAN_FRAME_AUDIT=2`, frame = time between two EE display flips in free gameplay, `scripts/perf/hitch_survey.py`.
Walks: `perf_walk_surdana.txt` (three legs of 44 s running with a turn every 2 s, two teleports) and `perf_walk.txt`
(60 s from the level start). Each walk was run once; 3 to 9 other games ran. The medians (19 to 47 ms) show the load:
these are not the frame times of the game, and the counts over 2x / 4x the median are dominated by the other
processes. What the survey does establish is which mechanisms produce the long frames and which do not.

| Walk | exe | frames | median ms | p99 | longest | over 2x / min | over 4x / min | first cause of the frames over 2x |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Surdana, 126 s | old | 3995 | 28.3 | 122 | 248 | 136 | 26 | present 181, gs 104 |
| Surdana, 130 s | new | 5561 | 19.2 | 81 | 217 | 259 | 31 | present 366, gs 116, unattributed 43, late vblank 33, res 5 |
| Ravenshold (4A), 63 s | new | 2195 | 24.9 | 100 | 174 | 172 | 21 | present 123, gs 55 |
| OotF 01 - Ruined Village, 63 s | new | 1120 | 47.3 | 277 | 350 | 85 | 30 | gs 61, present 28 |

The two Surdana rows are different moments of machine load, not a comparison of the builds.

Causes (`gs` = one GS worker record over 8 ms, `present` = a host present tick over 8 ms, `res` = a resource batch):

* **Every frame over 100 ms is a GS worker record of 100 to 320 ms, and that record is a GPU readback.** `GSR_PROFILE`
  in the same runs: Surdana 332 `readbackMap` calls, 7222 ms, longest 194 ms (the longest frame: 217 ms with a 182 ms GS
  record); Ravenshold 33 calls, 2618 ms, longest 154 ms (longest frame 174 ms); Ruined Village 182 calls, 13159 ms,
  longest 289 ms (longest frame 350 ms). In the old exe's Surdana walk the long records sit 0.14 s after each scripted
  camera turn. Renderer finding, section 8.
* `present` (the larger count, 8 to 50 ms each): the host's present call. With the GPU shared this is the other games.
* **Resource batches are not a cause.** 401 batches during the Surdana walk (10.9 MiB: music and sound streaming, level
  streaming after the teleports), median service time 0.10 ms, on the worker thread; they appear in 5 long frames
  with 0.1 to 0.2 ms each. One batch took 39.6 ms (worker thread, under load).
* **Disc reads are not a cause**: they are inside those batches (a read of 82 KiB takes 0.02 to 0.07 ms cached).
* **Shader or pipeline creation: none seen.** No `[gsr-shader] late` line in any walk (the 2026-11-08 prewarm holds).
* Texture work: `textureCreate` (GPU texture creation on the GS worker) 7349 calls / 2026 ms / longest 40 ms in
  Surdana, 5067 / 1225 / 65 ms in Ravenshold: tens of ms in single frames, below the readbacks. Pack decodes and
  upscales run on the pool (47 to 149 ms per job under this load) and are logged in some long frames; cause or
  coincidence is not established. `texup` at exit, Surdana: replaced 415, upscaled 133, cache peak 370 MiB of 6144.
* Teleports (`place`): 19 to 31 frames over 4x the median per run, longest 134 to 203 ms; the same readback pattern.
* 43 frames over 2x in the new Surdana walk have no logged cause (38 to 60 ms each); `DRAKAN_FRAME_AUDIT=3` with
  `scripts/perf/ee_samples.py` would name what the EE thread ran, not done under this load.
* Audio underruns, allocation spikes, log flushing: not measured (audio was off in every run; the log is written by its
  own thread since `drakan_log.cpp`).

## 6. Memory over a session

New exe, owner's settings, sampled every 5 to 10 s with `Get-Process` and the `GPU Process Memory` counter
(`build/review/perf-load/memrun.py`, a scratch script). Memory figures do not depend on the machine load.

Session A (`mem13`): New Game, then 13 minutes of running through Surdana with a turn every 2 s (no level change),
scripted quit reached after 896 s. 79 samples from 2 minutes on:

| | lowest | highest | mean of the first third | mean of the last third | trend per minute |
| --- | --- | --- | --- | --- | --- |
| private bytes, MiB | 4464 | 5847 | 5204 | 5042 | -26 |
| working set, MiB | 2052 | 4562 | 3536 | 2480 | -126 |
| GPU dedicated, MiB | 2113 | 3717 | 2298 | 2965 | **+69** |
| handles | 973 | 1003 | 981 | 989 | +1 |
| threads | 96 | 104 | 98 | 97 | 0 |

Session B (`mem15`): New Game, 200 s in Surdana, level change to City of the Damned (`DRAKAN_LEVELCHANGE`), meant to
run 13 minutes with four level changes. It ended after 300 s: 20 s after the arrival in the City the game was back at
the title (W was held; fired exits are known to be unreliable, `2026-10-31/level-change.md`) and the script's key
presses then chose Quit. Samples: private bytes 2311 MiB at 15 s (title), 4897 at 44 s, 4492 to 4879 during Surdana,
5030 after the level change (highest 5511); GPU dedicated 1116 at the title, 3458 to 3751 in Surdana, 2693 to 3100 after
the change (highest 4041).

Reading:

* Private bytes do not grow over 13 minutes in one level; handles and threads are flat. No leak is visible on the
  CPU side at this length.
* **Private bytes are 4.5 to 5.8 GiB in play**, 1.2 GiB at start-up and 2.3 GiB at the title. 0.41 GiB of it is the
  packs' files (section 7); what makes up the rest (the texture cache reports only 99 to 370 MiB of its 6144 MiB budget)
  was not broken down.
* GPU dedicated memory rises by about 0.7 GiB over the 13 minutes (2.3 to 3.0 GiB as thirds, single samples 2.1 to
  3.7). Whether it levels off, and at what, is not established: the session is too short and the walk kept entering
  new ground. Renderer area.
* Growth across several level changes is **not established** (session B ended at the first one).

## 7. Found, not fixed

| What | Estimated gain | Risk / why not |
| --- | --- | --- |
| The packs' files stay in memory for the session: 408 MiB for these two packs (100 files, e.g. NPCS.ADB 74 MiB, PS2MUSIC.SDB 78 MiB, RESOURES.TXD 39 MiB), and installing them is still about 1 s of start-up (0.5 s reading on 8 threads, 0.2 to 0.5 s rebuilding). Serving rebuilt containers as segments (disc ranges + pack members read on demand) would remove both. | about 400 MiB and most of 1 s | a new file representation in `IsoFs` and every reader of `Node::mem`; not a small change |
| Pack install and renderer start-up run one after the other (0.8 s + 1.0 s). | up to 0.8 s of start-up | start-up order across `main.cpp`, `ps2_runtime.cpp` and `drakan_overrides.cpp`, all with other sessions' edits |
| `ModOverride::apply` opens the zip and inflates the member on every request of an overridden entry (57 per Surdana load, 107 per New Game run). | a few ms per load | small; a size-bounded cache would do |
| Several queued batches could run as one sequence (a load queues about 100 at a time). | hand-overs per level from about 550 to under 100 | changes how many completions the guest sees between two instructions; needs its own test round |
| Process teardown after the window is gone: 1.9 s (5 to 10 s under load) in `std::_Exit`. `TerminateProcess` would skip the DLL detach. | 1 to 2 s until a relaunch is possible; nothing visible | drivers flush their caches at detach; not measured which part of the 1.9 s is detach |
| Title menu streams (movie frames, music) pay 30 to 40 ms of wall time per batch for the 50 us latency while the menus run at a few percent of guest speed. | menu responsiveness on first visits | the latency rule itself; `2026-10-22/options-black-screen.md` already cut it from 2 ms |

## 8. Findings for the other areas

**Renderer (D3D11 `runtime/gs`).**

* Every `readbackRT` stalls the GS worker in `Map` for as long as the GPU queue is long. Surdana walk with a turn every
  2 s, old exe, `GSR_PROFILE=2`, 60 s: 46 readbacks, 1801 ms in `readbackMap`, mean 39 ms, longest 96 ms (the journals
  give 0.5 to 0.8 ms on a quiet machine). They come in the frames after a camera turn (new textures: CLUT and image
  uploads over the scratch targets) and are the longest frames of every walk: one GS record of 100 to 190 ms. On a GPU
  that is busy (another game here; neural rendering or a weaker card for a player) this is the hitch a player feels
  when turning. The numbers are from a GPU shared with other games and say nothing about a quiet one.
* `syncRT` at the title: `syncUpload=9640 MiB` in one 2 s profile window (1005 calls, 740 ms), the pixel replication
  at scale 5 that `2026-11-08/perf-review.md` describes, here on the menus.
* Texture upscale jobs (`[texup]` lines: 47 to 95 ms for a 64x64 x8 smooth, 52 to 149 ms for a 256x256 x4 pack image,
  on the pool) appear in the log of several long frames but on worker threads; under this load it cannot be told
  whether they lengthen the frame by taking cores. `texDecodes=9248` for a 150 s walk.

**CPU / pacing.**

* The level loader advances between display flips. With the host presenter starved, `Level_Load` of Surdana took 21 s
  at 1 completed flip per second with no resource batch outstanding (`qs_new_2`, `[frame-uncapped] completedFlips=2`
  per 2 s, main thread in guest code at 0x4a2308, flip thread at 0x1d7a04). Whatever the EE thread waits for in the
  present hand-over during a load is waited for once per flip; a load does not need the flips to be shown.
* Posted guest calls and delayed calls are served only when the EE thread is outside a host call. The resource path no
  longer depends on that per call (4.2), the sound pump and every `postGuestCall` user still do.

## 9. How to measure again

```sh
# phases: start-up, loading screens, level loader, save / restore, exit; counts per loading screen
python scripts/harness/play_run.py --exe <play>/drakan.exe --name ng1 --timestamps --shots 0,100 \
    --env DRAKAN_IO_STATS=1 --env DRAKAN_LIFECYCLE_LOG=1
python scripts/perf/load_phases.py build/harness/ng1 build/harness/ng2 ...
# old against new resource path on one exe, alternating (add to every second run):
#   --env DRAKAN_RES_CHAIN=0 --env DRAKAN_RES_LATENCY_ELAPSED=0
python scripts/perf/load_phases.py --label old build/harness/o1 build/harness/o2 --label new build/harness/n1 build/harness/n2
# other scenarios: --env DRAKAN_GAME=d1 (Order of the Flame), --env "DRAKAN_LEVELCHANGE=City of the Damned@300"
# with --shots 0,100,900, --recipe scripts/harness/recipes/quick_save_load.txt (F5 / F8)
# cold files: python scripts/perf/evict_file_cache.py <iso> <play>/mods <play>/textures before the run
# mod pack install alone: the game logs "mods: pack files installed in N ms" at every start

# hitches
python scripts/harness/play_run.py --exe <play>/drakan.exe --name walk --timestamps --timeout 400 \
    --recipe scripts/harness/recipes/perf_walk_surdana.txt \
    --env DRAKAN_FRAME_AUDIT=2 --env DRAKAN_PLAYTEST_GOD=1 --env GSR_TEXUP_LOG=1 --env GSR_PROFILE=2
python scripts/perf/hitch_survey.py build/harness/walk
# other levels: --level 4a --recipe scripts/harness/recipes/perf_walk.txt; OotF: --env DRAKAN_GAME=d1
```

Check `tasklist | findstr drakan.exe` first and write the count down with the numbers.

## 10. Not established

* Any quiet-machine time: start-up, loads, hitch counts, frame medians.
* That 4.2 / 4.3 shorten a load on a lightly loaded machine (the lowest runs are equal).
* The separate effect of 4.2 and of 4.3.
* Cold start as a whole game run; only single files were read cold.
* Whether the texture pool's jobs lengthen frames; which part of the exit time is DLL detach.
* Level change, save and load, Order of the Flame: they work on the new path; their times are inside the spread.
* Saves of older builds, the intro movie, USA / Linux / browser builds on the new resource path. The unskipped
  opening scenes were run once.
* Why the 13 minute session with level changes ended at the title 20 s after its first fired level change.
* Memory across level changes; whether the GPU memory growth of section 6 levels off.
* The root `runtime-tests` preset was not built (machine load); the `runtime/hle` tests were built and run standalone.
