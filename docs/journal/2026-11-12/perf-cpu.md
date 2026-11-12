# CPU side of a gameplay frame: where the time goes, three changes, one failed experiment, what is open (2026-11-12)

Scope: the recompiled guest code (EE), the VU1 path, the guest scheduler, DMA / HLE services and everything else on the
game threads that is not the renderer backend. Renderer and loading findings are listed at the end for the sessions
that own them. Scratch data: `build/review/perf-cpu/` (runs, recordings, benchmark folders; not tracked).

## 1. Conditions (read this before any number)

* Machine: the shared 8-core / 16-thread box with an RTX 5080. **Between 3 and 10 other `drakan.exe` and several
  compilers were running during every measurement** (other sessions). Each benchmark table lists the number of other
  games at the start and end of every run. Wall-clock world fps moved between 9 and 62 for the same exe and scene, so
  fps is reported but conclusions rest on CPU time per world frame (thread CPU time divided by frames), on sample
  shares and on the single-threaded VU1 replay, which are far less sensitive to the load.
* Settings: the owner's real `drakan.ini` (uncapped world rate, vsync off, `frame_limit = 0`, DLSS SR quality, ray
  effects and neural rendering off, HD textures on, foliage wind on, `tick_snap = 1` which is inactive without vsync),
  with one change: a 1280x960 window instead of 4K borderless (`build/review/perf-cpu/ini/owner-windowed.ini`), which
  gives internal scale 2. At 4K and scale 8 on this shared GPU the first Surdana run made 18 to 45 world fps with the
  EE thread half idle: a renderer measurement, not a CPU one. Audio output off (`play_run.py` default; the SPU2 is
  still mixed, see 3.7), god mode.
* Exe: incremental builds of the working tree into a private play folder (`build/review/perf-cpu/play`, ELF read from
  a private copy). All A/B pairs below use **one exe and environment switches**, because other sessions' uncommitted
  changes enter every build. "old" = `DRAKAN_VU_LANES_STRICT=1 DRAKAN_SCENE_CAPTURE_FAST=0 PS2X_DMA_CHAIN_POOL=0`
  (the three changes of section 4 off), "new" = defaults.
* Scenes (`scripts/perf/cpu_scene_bench.py --scene ...`, New Game on an empty card, still view): `surdana` = Level 1A
  start (meadow at the village edge, 1215 model instances resident); `tundra-c` = Northern Tundra spot C;
  `arena-1e` = the Inquisitor's Lair arena spot (interior); `ravenshold` = Level 4A start, mounted on Arokh over the
  valley; `ootf-ruined` = Order of the Flame campaign start (Ruined Village, installed pack); `ootf-volcanic` = OotF
  Volcanic by direct start with the pack of `build/review/issue5/play/mods` (the installed pack crashes there).
  **Not measured: a combat scene and the Surdana town centre** (no save-free recipe reaches them; see section 7).

## 2. Baseline per scene ("old", same exe)

Median of the runs, range in brackets; frame times are intervals between world flips (`[ft] F`), "1 %" and "0.1 %" are
the 99th and 99.9th percentile intervals. CPU columns are thread CPU time per world frame.

| Scene | runs | other games | world fps | frame ms | 1 % ms | 0.1 % ms | worst ms | EE CPU ms | VU1 worker CPU ms | all other threads CPU ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| surdana | 3 | 6 to 8 | 37.0 (31.4 to 52.9) | 24.5 | 73.7 | 128.8 | 140 (one run 292) | 12.5 (11.7 to 13.5) | 7.5 | 20.7 |
| tundra-c | 2 | 4 to 8 | 60.5 (59.1 to 61.8) | 16.3 | 33.2 | 45.9 | 57 | 12.4 | 8.0 | 25.6 |
| arena-1e | 2 | 8 to 9 | 31.8 (29.2 to 34.4) | 29.5 | 64.6 | 90.6 | 91 | 16.9 | 8.9 | 31.0 |
| ravenshold | 2 | 7 to 8 | 58.5 (56.1 to 60.9) | 15.0 | 70.4 | 122.2 | 124 | 7.3 | 4.7 | 18.0 |
| ootf-ruined | 3 | 4 to 10 | 29.0 (18.0 to 34.4) | 28.8 | 66.2 | 78.8 | 83 (one run 244) | 18.3 (18.0 to 21.8) | **18.7** | 60.6 |
| ootf-volcanic | 2 | 10 | 12.2 (10.6 to 13.9) | 82.7 | 236 | 904 | 904 | 28.0 | **22.6** | 75.3 |

For orientation only: with 4 other games (earlier in the session, exe of 07:55) the Surdana still view made 85 to 119
world fps with the EE thread at 86 to 100 % and, a minute later, 67 to 73 with the EE at 61 to 77 %. The owner's
screenshots (90 to 130 in Surdana, 36 to 65 in the OotF levels at a small window) are consistent with the picture
below: the OotF levels cost 2.5 to 3 times the VU1 time and 1.5 to 2 times the EE time of a Drakan 2 level per frame.

CPU per thread (Surdana, one-second windows of `[perf]`, about 70 world fps at the time): EE thread 61 to 77 % of a
core, GS worker 52 to 61 %, VU1 worker 43 to 54 %, the SPU2 mixer 6 to 9 %, a driver thread 9 to 13 %, the model-motion
builder 4 to 7 %. No thread is saturated: the frame is a chain of waits (3.1). The process has more than 110 threads,
nearly all parked.

## 3. Where the time goes (ranked)

Sampling: `PS2X_HOSTPROF` with `PS2X_HOSTPROF_STACKS=1` and the new `PS2X_HOSTPROF_BUSY=1` (5.1), resolved with the map
of the same build. Shares are of the EE thread's wall-clock samples (waits included) unless stated. Surdana: 7181 EE
samples of the exe of 08:14 with 4 other games; Ruined Village: 3366 and, per variant, 5208 to 5954 samples.

### 3.1 The frame is serialised on a per-frame GS read-back (not fixed)

Every world frame the game reads a 32x32 image back from the GS (chain `f_0x139010 -> f_0x3b2038 -> f_0x133d80 ->
f_0x1d8798 -> f_0x1df670 -> f_0x1e88d0 -> f_0x1dda88`), and before it polls GS CSR.FINISH in `f_0x1dbfa8` (loop at
0x1dc688). FINISH only arrives when the VU1 worker and then the GS worker have processed everything queued so far, so
the EE cannot start the next frame while the workers finish this one. Measured EE time inside `f_0x1dbfa8`
(inclusive): 8.9 % and 25.9 % in two Surdana profiles, 24.4 to 31 % in the Ruined Village ("old"), 66 % in the Ruined
Village once the VU1 work had become cheaper (4.1) and the GS worker (on the shared GPU) was the last to finish. On top
come the blocking parts of the same wait: the read-back drain (`GsrBridge::drain`, 7 to 14 %), the VU1 sync at the
flip (`vu1async::syncSlow`, 3 to 16 %). In sum **40 to 70 % of the EE thread's wall time is waiting for the two
workers**, in every scene, which is why no thread shows 100 % while the frame rate is CPU bound.

The poll is a busy wait: per iteration a CSR load through the slow path and a 32-cycle checkpoint (timers, IOP
accounting, host vblank check). It is where most of the "checkpoint / scheduler / IOP" bucket (8 to 17 %) is spent.

What it would take: find out what the image is for (the caller walks a list at 0x4fde48 with a count at 0x4fde60 and
calls a renderer virtual; candidates are light-source visibility or a small dynamic texture), then serve the previous
frame's pixels and fetch the current ones asynchronously. Estimated gain: frame time towards max(EE, VU1, GS) instead
of EE plus the workers' tail, i.e. up to +30 to +60 % world fps in the scenes above when the GPU is not the limit.
Risk: high (guest-visible data one frame late, needs reverse engineering and visual checks). `DRAKAN_DEBUG_SKIP_FN`
(5.3) was added to make the first step cheap; the experiment itself was not run (no quiet machine, see section 7).

### 3.2 VU1: converted OotF terrain took the scalar path for every UV step (fixed, 4.1)

Ruined Village and Volcanic spend 18.7 and 22.6 ms of VU1 worker CPU per world frame against 4.7 to 8.9 ms in the
Drakan 2 scenes, for a similar number of VU pairs (4.2 to 5.1 M per frame against 2.2 to 4.2 M). In the profile 17 % of
the VU1 worker's samples (35 % of its busy samples) were `Vu1Step::fmacSlow` and another 9 % the lane fix-up
(`fixLanes`) of loop 0x04D8. That loop is the microcode's texture-coordinate transform (a 2x3 matrix applied to the
ST pair of every vertex of a draw whose flag word has bit 0x200). In a recording of 16000 consecutive VU1 runs of the
Ruined Village, 4112 runs take it (344 in Surdana), nearly all terrain chunks of 225 vertices with flags 0x126c, and
**66 % of their ST components are +-FLT_MAX-like values** (bit patterns 0x7f7ffffe, 0x7f7ffffd, 0xff7fffff; 16 to 213
of the 225 vertices of a chunk), none in Surdana. A result with an exponent field above 246 is "not ordinary" for the
functional engine, so every SIMD step of the loop dropped two of its three FMAC nodes to the scalar engine, which then
recomputed them in double precision: 2.78 M fix-ups for 5.5 M iterations.

### 3.3 Scene model capture on the EE thread (reduced, 4.2)

With DLSS Super Resolution on (the owner's setting; also with any ray effect or neural rendering)
`drakan_scene_models::Capture::capture` runs on the EE thread at the start of every world render and rebuilds the
snapshot of all resident model instances (1215 instances, 86 meshes in Surdana; 543 and 100 in the Ruined Village).
18.9 % of the EE thread in Surdana (16.4 % in a second profile), 4 to 5 % in the Ruined Village. Inside it: 6.8 % own
code (the walk of the `std::set` of live objects and a pass over every triangle of every used mesh to count
untextured ones), 4.2 % `meshContentSignature` (hashing every source array of every model, about 5 MB per frame),
3.3 % posed meshes, 1.6 % a hash-map lookup per instance for its generation, 1.4 % the sliced texture refresh.

### 3.4 DMA chain copies (reduced, 4.3)

`PS2Memory::writeIORegister` (the CHCR write of a source-chain transfer, guest `f_0x1f1420`) is 10.8 % of the EE thread
in Surdana and 19 to 22 % in the Ruined Village. A world frame has about 470 chains of 9.4 KB on average (4.7 MB;
`dma_chains/s`, `dma_chainMB/s` in the perf line). Each chain was appended piece by piece to a new `std::vector`, i.e.
about twenty grow-copy-free cycles per chain: 5 to 5.7 % of the EE thread in vector growth and `operator new` in
Surdana, 5.4 % (and 10.7 % in the earlier profile) in the Ruined Village. The rest of the function is the tag walk,
the `unordered_map` register file (about ten lookups per kick) and the object-motion tagging (0.7 %).

### 3.5 The busy wait under load (hypothesis, tested in 4.4, not confirmed)

With the VU1 work halved the Ruined Village did not get faster in the first two series on this machine: it was slower
in 5 of 6 interleaved pairs, with the EE thread's CPU per frame up from 18 to 25 ms and its time in the FINISH poll up
from 31 to 66 % (the wait moved from the blocking VU1 sync, 16 % -> 0.1 %, into the spinning poll). A third series
went the other way in 2 of 3 pairs, a fourth (final exe) was 33.5 -> 26.8 and 26.8 -> 27.9. See 4.4.

### 3.6 Guest code and its helpers (not changed)

Generated guest functions are 29 to 38 % of the EE thread (self), spread thinly: the top function (`f_0x116990`) is
2.1 to 3.5 %. JALR / branch dispatch is 0.1 %. Guest memory slow paths outside the poll (`translateAddress`, `inRange`,
`read32`/`write32` for addresses outside the fast-RAM test, 128-bit loads in `f_0x116990`) are about 4 %; they are
real work for scratchpad and I/O addresses and no single call site stands out. Nothing here promised a gain that would
justify a regeneration (a full rebuild of the generated code holds the shared build lock for 20 minutes or more), so
**no recompiler change was made**.

### 3.7 Smaller items seen

* The SPU2 is mixed at full rate with `DRAKAN_AUDIO=0`: 6 to 9 % of a core on its own thread (decode, voices,
  reverb). By design (the guest reads back SPU2 state); only relevant for test runs.
* `EeScheduler::processPendingEvents` / `vsyncHost` 6 to 13 % of the EE thread in the Ruined Village "old" profile is
  again waiting (present hand-off), not work.
* Port features in the EE profile: foliage wind (`drakan_foliage::hookRenderObject`) and the ghost / widescreen render
  hooks wrap the object draws and show only as frames above guest code, under 0.2 % self each; the near-clip hook is
  0.004 to 0.08 % (`clip_ee_pct`). `tick_snap` is inactive without vsync. No debug or logging path was found left on.

## 4. Changes

### 4.1 VU1 loop path: finite results of 2^120 and above stay in SIMD

`vu/aot/ps2_vu1_lanes.h` (`fmacBad`), `vu/aot/ps2_vu1_func.cpp`. The loop path needs only the value of an FMAC result
(loops with observed flags do not run in lanes), and the scalar engine's exact path changes a value only when the exact
result is zero, underflows or overflows. A finite result of 2^120 or more cannot be a zero or underflow, and when the
exact result exceeds FLT_MAX the finite rounded float is FLT_MAX with the same sign, which is exactly what the overflow
rule stores. That holds for one rounding: ADD, SUB, MUL, and MADD / MSUB whose ACC component is zero or whose second
operand is the constant 1. For other MADD / MSUB the product is rounded before the sum (and with round-toward-zero an
overflowed product is the finite FLT_MAX), so a result or product in the top binade keeps the exact path. A MADD / MSUB
component with a zero operand is ACC itself. The class of the product only feeds flags. `DRAKAN_VU_LANES_STRICT=1`
restores the old rule.

Correctness:

* `vu_lanes_tests` now checks, for 200000 random operand sets per rounding mode (toward zero and nearest), both lane
  widths and all ten FMAC forms, that the default rule flags a subset of the scalar classification and that **every
  component it leaves alone has the bit pattern the exact path would store** (computed independently in the test), and
  that the strict rule still equals the scalar classification: 201.6 M checks, 9.38 M components covered by the new
  rule only. The test caught two wrong first versions (the double rounding and the saturated product above).
* Replay of the recorded runs against the reference interpreter (`DRAKAN_VU_BENCH`: registers, Q/P/I, MAC, clip, VU
  memory, every packet): Ruined Village 16000 runs, 0 mismatches, default and strict. Surdana 16000 runs: 1 mismatch
  in both modes and with the unmodified exe of 08:14 (run 0, a resumed program whose clip flags precede the recording;
  an artefact of where the recording starts).
* Live `DRAKAN_VUCHECK=1` session in the Ruined Village with the final exe (default rule): 917504 runs checked, 0
  mismatches (boot, menus and the level at 1 to 2 world fps, which is what the check costs).

Measured (same exe, switch only):

| Measure | strict (old) | default | Conditions |
| --- | --- | --- | --- |
| VU1 replay, Ruined Village, 16000 runs, best of 5 | 42.4, 45.7, 43.9, 43.2 ms | 11.9, 11.9, 14.2, 11.2 ms | single thread, interleaved, 3 to 7 other games |
| same, ns per pair | 3.4 | 0.9 | |
| VU1 replay, Surdana, 16000 runs | 20.0, 24.1, 20.3 ms | 21.1, 20.2, 20.7 ms | no difference (no such data) |
| VU1 worker CPU per world frame, Ruined Village | 18.67 (18.63 to 18.76) ms | 8.73 (8.46 to 9.84) ms | 3 pairs, 4 to 10 other games, all three changes toggled |
| VU1 worker CPU per world frame, Volcanic | 22.61 (22.05 to 23.16) ms | 10.90 (10.74 to 11.05) ms | 2 pairs, 10 other games |
| VU1 worker CPU per frame, Drakan 2 scenes | 4.7 to 8.9 ms | unchanged within 0.3 ms | |
| world fps, Ruined Village | 31.8, 36.4, 29.1 and 29.0, 34.4, 18.0 | 28.6, 23.5, 31.1 and 24.6, 20.0, 17.2 | **not faster on this machine**, see 4.4 |

### 4.2 Scene model capture: flat live list, cached counts, sampled source checks

`drakan_scene_models.h`. (a) The live set and the generations are copied into a flat vector that is rebuilt only when
an object is constructed or destroyed (`liveEpoch`). (b) The untextured-triangle count of a mesh is kept with the mesh
(static cache entry, posed template). (c) A model source is hashed in full until it returned the same signature four
times in a row; after that every capture takes a sampled signature (all metadata, and of each large array the first
and last 64 bytes and every eighth 64-byte block) and a full one when the sampled one changes and on every 16th
capture, staggered by source. `DRAKAN_SCENE_CAPTURE_FAST=0` restores the per-capture work.

Behaviour: the snapshots are identical, with one documented exception: an in-place edit of a long-unchanged source
array that touches none of the sampled blocks is seen up to 15 captures late (a model streamed in over another at the
same address changes nearly every block and is seen at once). Only the renderer's effect inputs (ray scene, model
motion) are concerned, never guest state.

Test: `runtime/gs/tests/test_scene_models.cpp` (ctest `scene_models`) gains a section that runs a fast and a
per-capture `Capture` over the same memory for 48 frames (a posed and a static model with large arrays, a moving
instance) and requires equal snapshots, then checks the late case (seen within 16 captures; 10 in the test), the
immediate cases (an edit right after a change, a sampled block) and construction / destruction through the hooks.

Measured (Surdana, same exe): capture share of the EE thread 16.4 % (the one "old" run with a usable profile, 964
samples; 18.9 % with 7181 samples in the earlier profile of the previous exe) -> 8.6, 9.8, 8.6 % (3551, 1328 and 1060
samples); as time 3.1 ms -> 1.5 to 2.5 ms per world frame at 17 to 25 ms frames under 6 to 8 other games.

### 4.3 DMA chain copies: scratch gather, one exact allocation, pooled large blocks

`runtime/ps2_chain_buffers.h`, `ps2_memory.cpp`, `vu/ps2_vu1_async.cpp`. The chain is gathered in a thread-local
scratch vector that keeps its capacity; the block that travels to the VU1 worker is made in one allocation of the exact
size, and blocks of 32 KB and more (about 37 per frame in the Ruined Village, `dma_pooled/s`) are reused after the
worker hands them back. The bytes are the same. `PS2X_DMA_CHAIN_POOL=0` restores the growing vector. A first version
that only pooled buffers did nothing (`dma_pooled/s = 0`: the chains are small, the cost was the growth).

Test: `ps2recomp/ps2xRuntime/tests/chain_buffers_tests.cpp` (ctest `chain_buffers.pool` in
`ps2recomp/ps2xTest/vif_unpack`): contents, reuse, the bounds of the pool, the off switch, two threads.

Measured (same exe): allocation and copy below the DMA kick, share of the EE thread: Ruined Village 5.4 % (4.7 to
5.4 %) -> 1.6 % (1.4 to 2.2 %), three runs each; Surdana 5.0 % (one usable "old" profile; 5.7 % in the earlier profile)
-> 2.7 % (2.7 to 3.0 %). About 0.3 ms per world frame in Surdana and 1 ms in the Ruined Village at the frame times of
those runs.

### 4.1 to 4.3 together: EE thread CPU per world frame

| Scene | old | new | runs, other games |
| --- | --- | --- | --- |
| surdana | 12.54 (11.68 to 13.47) ms | 10.17 (9.78 to 10.63) ms | 3 pairs, 5 to 8 |
| tundra-c | 12.38 (12.37 to 12.40) | 11.37 (11.31 to 11.43) | 2 pairs, 4 to 9 |
| ravenshold | 7.25 (7.20 to 7.29) | 6.34 (6.14 to 6.54) | 2 pairs, 7 to 8 |
| arena-1e | 16.90 (16.44 to 17.37) | 21.70 (18.36 to 25.03) | 2 pairs, 8 to 9; one "new" run at 9 fps |
| ootf-ruined | 18.34 (17.96 to 21.83) | 25.50 (25.48 to 27.27) | 3 pairs, 4 to 10 |
| ootf-volcanic | 28.02 (27.60 to 28.43) | 26.56 (25.61 to 27.51) | 2 pairs, 10 |

The EE figure includes the CPU burnt in the FINISH poll, so it falls where the EE's own work was the long pole
(surdana -19 %, ravenshold -13 %, tundra-c -8 %, ranges disjoint) and rises where the EE now waits longer for the GS
worker. World fps of the same runs: surdana 37.0 (31.4 to 52.9) -> 44.0 (40.0 to 58.7), each pair in favour of "new";
tundra-c 60.5 -> 52.5, arena-1e 31.8 -> 19.8, ravenshold 58.5 -> 59.0, ootf-ruined 29.0 -> 20.0, ootf-volcanic 12.2 ->
11.6. With 4 to 10 other games on the machine **none of these fps differences is larger than the spread between
repeats of one variant**; the Ruined Village one comes closest and goes the wrong way in this series (see 4.4 for a
third series that does not).

### 4.4 Experiment: the FINISH poll yields its time slice (failed, off by default)

`ps2_memory.cpp` (the CSR read). Hypothesis from the Ruined Village result: once VU1 is cheap the EE no longer blocks
in the VU1 sync but spins in the FINISH poll, and on a machine with more runnable threads than cores the spinning EE
thread takes time from the GS worker it waits for. Test: from the 16th poll in a row that finds FINISH clear, the read
gives up the rest of its time slice (`std::this_thread::yield`). **The test failed, so this is off by default**
(`PS2X_FINISH_YIELD=1` turns it on for further experiments). Same exe, three variants interleaved ("noyield" = the
three changes above without the yield, i.e. the final defaults):

| Scene, repeat (other games) | old | noyield | yield |
| --- | --- | --- | --- |
| ootf-ruined 0 (7 to 8) | 15.2 fps | 23.7 | 14.8 |
| ootf-ruined 1 (4, then 1) | 34.5 | 24.4 | 39.9 |
| ootf-ruined 2 (1 to 2) | 44.2 | 51.5 | **7.6** |
| arena-1e 0 (2) | | 93.1 | **27.8** |
| arena-1e 1 (2 to 9) | | 33.4 | 44.8 |
| arena-1e 2 (9) | | 33.0 | 34.2 |
| surdana 0 (8 to 9) | | 54.3 | 54.6 |
| surdana 1 (6 to 8) | | 53.4 | 61.9 |

In the two quietest runs the yield collapsed the frame rate (93 -> 28, 52 -> 8 world fps with the EE thread at 46 to
49 % of a core): the slice goes to whatever thread is ready on that processor, including background workers of the
game itself, and the EE comes back a scheduler quantum later. Where the machine was saturated it made no consistent
difference. Blocking on the worker's FINISH signal instead of yielding was not tried: the poll's checkpoints also drive
the guest scheduler, the host vblank check and the IOP cycle count, so a blocking wait needs a design for those
(section 6).

What the triple does show for the three changes: the pair "old" against "noyield" in the Ruined Village was 15.2 ->
23.7, 34.5 -> 24.4 and 44.2 -> 51.5 world fps, the last with only 1 to 2 other games (EE thread 70 -> 83 % of a core,
VU1 worker 14.1 -> 8.8 ms per frame). With the two earlier series (31.8 -> 28.6, 36.4 -> 23.5, 29.1 -> 31.1;
29.0 -> 24.6, 34.4 -> 20.0, 18.0 -> 17.2 with 4 to 10 other games) and a last one with the final exe (33.5 -> 26.8,
26.8 -> 27.9 with 4 to 5 other games; Surdana in the same series 105.6 and 77.0 -> 45.8 and 115.7) that is **4 pairs
faster and 7 slower in the Ruined Village** (median of the eleven: 31.8 old, 24.6 new; the first series toggled only
`DRAKAN_VU_LANES_STRICT`, the others all three switches). In the slower pairs the thread
that uses the most CPU after the EE (taken to be the GS worker; it has no name in the perf line) spent more CPU per
frame (19.8 -> 31.0 and 17.4 -> 23.5 ms in the last series) and in the quiet pair less (19.4 -> 15.1 ms), which points
at contention for the shared GPU and cores rather than at the change, but that is an inference. **The fps effect of
the VU1 change in that level is not established, and on this loaded machine the tendency was negative.** It needs
the benchmark of section 8 on a quiet machine; `DRAKAN_VU_LANES_STRICT=1` is the way back. The arena run at 93.1 world fps (median frame 9.4 ms,
1 % interval 24.1 ms, EE 9.1 ms and VU1 worker 5.9 ms of CPU per frame, 2 other games) is the only measurement of this
session that comes near a quiet machine.

## 5. Tools added

### 5.1 Sampler: `PS2X_HOSTPROF_BUSY=1`

The sampler suspended every thread of the process per round; with more than 110 threads that gave about 70 samples per
second and thread. With `PS2X_HOSTPROF_BUSY=1` only threads that used CPU in the last second are sampled (about 350 per
second). Caveat found later: under heavy machine load the sampler itself is starved and the count per run varied
between 1 and 5954 EE samples for the same 15 seconds; check the sample count before reading a profile.

### 5.2 Perf line and benchmark

`[perf]` gains `dma_chains/s`, `dma_chainMB/s`, `dma_pooled/s`, `ee_ms/flip2` (EE CPU per world frame) and the names of
threads that named themselves (the VU1 worker is now `VU1Worker`). `scripts/perf/cpu_scene_bench.py` runs the scenes
of section 1 interleaved for any number of environment variants and prints the tables used here (world fps, frame
interval percentiles, CPU per frame for the EE thread, the VU1 worker and all others, the load). Commands: section 8.

### 5.3 `DRAKAN_DEBUG_SKIP_FN`

Makes listed guest functions return at once (diagnostic). Meant for the read-back question of 3.1.

## 6. Found but not fixed

| Item | Evidence | Estimated gain | Risk |
| --- | --- | --- | --- |
| Per-frame read-back serialises EE, VU1 and GS (3.1) | 40 to 70 % of EE wall time is waiting | up to +30 to +60 % world fps where the CPU side limits | high: guest-visible data a frame late, needs RE |
| Capture only what DLSS SR needs when no ray effect is on | capture still 8.6 % of EE in Surdana; the static instances cannot move | most of the remaining 1.5 to 2.5 ms per frame | needs the renderer's model-motion input contract (renderer session) |
| Capture: posed meshes rebuilt and hashed every capture | 3.3 % of EE in Surdana | 1 to 2 % of EE | low, but the identity is compared downstream |
| DMA kick: `m_ioRegisters` is an `unordered_map`, about ten lookups per kick | part of the 1.5 to 1.7 % self of `writeIORegister` | under 1 % of EE | low |
| FINISH poll iteration cost (slow-path CSR load, checkpoint every 8 iterations) | 8 to 17 % of EE in checkpoint / timer / IOP accounting, mostly inside the poll | CPU only, no frame time | low to medium (cycle accounting) |
| Block in the FINISH poll until the GS worker signals instead of spinning | a core's worth of EE spin in every scene; the yield experiment (4.4) shows giving the slice away blindly is wrong | CPU and heat; frame time only on an oversubscribed machine | medium: the poll's checkpoints drive the guest scheduler, the host vblank check and the IOP cycle count |
| Converted OotF terrain sends +-FLT_MAX texture coordinates | 3.2 | none left for VU1 after 4.1; possible visual issue | content / converter sessions |
| OotF levels: EE time per frame 1.5 to 2 times a Drakan 2 level | 18 to 28 ms against 7 to 17 ms of EE CPU per frame (includes the poll) | not analysed further | |

## 7. Not established

* **World fps gains.** No pair of runs had fewer than 3 other games on the machine. The per-frame CPU reductions are
  solid; what they are worth in fps on a quiet machine with the owner's 4K settings was not measured.
* Whether the Ruined Village is faster for the owner after 4.1. Here 4 of 11 interleaved pairs were faster and 7
  slower; the one pair with only 1 to 2 other games was 44.2 -> 51.5 world fps (4.4).
* A combat scene and the Surdana town centre were not measured (no save-free recipe; the existing combat recipes
  need a save near enemies).
* 4K borderless numbers: one exploratory run only (18 to 45 world fps with 5 other games sharing the GPU).
* What the 32x32 read-back is for.
* Gameplay parity was not re-tested with a play-through: none of the three changes touches guest state, guest timing
  or cycle accounting. The yield of 4.4 is off unless `PS2X_FINISH_YIELD=1` is set.
* `python scripts/run_tests.py` and the full `runtime-tests` ctest preset were not run as a whole; the tests that
  cover the changed code were built privately and run (section 9).

## 8. How to re-measure

```sh
# one exe, switches only; --ini keeps the settings identical for every run
python scripts/perf/cpu_scene_bench.py --exe <play>/drakan.exe --out-root build/bench --name ruined \
    --scene ootf-ruined --ini <drakan.ini> --repeats 3 \
    --variant old:DRAKAN_VU_LANES_STRICT=1,DRAKAN_SCENE_CAPTURE_FAST=0,PS2X_DMA_CHAIN_POOL=0 \
    --variant new
python scripts/perf/cpu_scene_bench.py --exe <play>/drakan.exe --name surdana --scene surdana --repeats 3 \
    --variant capture-old:DRAKAN_SCENE_CAPTURE_FAST=0 --variant chains-old:PS2X_DMA_CHAIN_POOL=0 --variant new
python scripts/perf/cpu_scene_bench.py --summarise build/bench/ruined       # the tables again

# VU1 alone (single thread): record 16000 runs in a scene, then replay with and without the switch
python scripts/harness/play_run.py --exe <play>/drakan.exe --name rec --env DRAKAN_GAME=d1 \
    --env "DRAKAN_VU_RECORD=<abs path>\ruined.vur,600000,16000" --recipe <a recipe that stays 30 s in the level>
DRAKAN_VU_BENCH=<abs path>\ruined.vur,5 <play>/drakan.exe                          # default rule
DRAKAN_VU_BENCH=<abs path>\ruined.vur,5 DRAKAN_VU_LANES_STRICT=1 <play>/drakan.exe # old rule
```

The replay prints `[vubench] check: N runs, M mismatches` and the best time of the repetitions. The recordings of this
session are `build/review/perf-cpu/vurec/{ruined,surdana}.vur` (262 MB each, not tracked; they contain VU memory
snapshots, i.e. game data).

## 9. Tests and builds as they were

Builds: five incremental builds of the shared work directory into the private play folder
(`Build-LocalGame.ps1 -Incremental`, lock taken each time): 399 s (other sessions' files compiled too), 231 s (VU1
functional engine recompiled), 85 s, and two more of a few minutes each that were not timed. No regeneration of the
recompiled code. The final exe
of this session is the fifth build (SHA-256 `0f3a8950c0b5b93d...`); two `#include` lines were added to
`drakan_scene_models.h` afterwards and compiled only in the test below. No build failed.

Tests run (private build trees under `build/review/perf-cpu/`, so the shared `build/runtime-tests` tree was not used):

* `ps2recomp/ps2xTest/vif_unpack` standalone, Release: 9 of 10 pass, among them `vu_lanes.exact` (201.6 M checks) and
  the new `chain_buffers.pool`. `vu_async.barriers` could not be built in the standalone configuration (it needs
  `runtime/input`; so before this session) and was **not run**, although `ps2_vu1_async.cpp` changed (one `release`
  call and the thread name).
* `gsr_test_scene_models` (the target's sources and flags in a private CMake project): pass, with the new section.
* VU1 replay against the reference with the final exe: Ruined Village 16000 runs, 0 mismatches (default and strict).
* Live `DRAKAN_VUCHECK=1` in the Ruined Village with the final exe: 917504 runs, 0 mismatches.
* `python scripts/run_tests.py`: 533 tests, OK (2 skipped). `scripts/repository_guard.py`: 0 prohibited files.
  `ruff check` / `ruff format --check` on the new script and `clang-format --dry-run --Werror` on the eleven C++ files
  touched: clean.
* About 90 scripted game runs of 60 to 170 s reached their scripted quit with exit code 0 (every benchmark run of
  section 2 and 4); no crash, hang or `[bad-resume]` was seen. Old and new screenshots of the Surdana still view
  differ no more than two runs of the same variant (mean absolute difference 2.5 against 2.8 to 2.9 of 255; the scene
  is animated).
* Not run: the full `runtime-tests` ctest preset, the D3D11 renderer tests, any play-through.

## 10. For the other sessions

Renderer / GPU:

* With 4K borderless and scale 8 on the shared GPU the EE thread was half idle at 18 to 45 world fps: GPU or GS worker
  bound, as the 2026-11-08 review says. Not looked at further.
* In the Ruined Village the GS worker (or the GPU behind it) is the last to finish a frame once VU1 is cheap: EE time
  in the FINISH poll rose from 31 to 66 %. Worth a `GSR_PROFILE=2` look at that level (467 resident textures, a
  `prepareFrom` model-motion pass, many CLUT loads in the profile).
* The model-motion consumer could tell the capture which instances it needs when no ray effect is on (section 6).
* `runtime/gs/tests/test_scene_models.cpp` was extended (4.2); it lives in the renderer tree but tests
  `drakan_scene_models.h`.

Loading / hitches / memory:

* The worst frame intervals in the tables (140 to 904 ms) all sit in the first seconds of the measured window while
  background work from the level start is still running; they were not attributed.
* A missing `DRAKAN_INI` file ends the game with `[main] fatal exception:` and an empty message.
* `vu_async_tests` (standalone configuration of `ps2recomp/ps2xTest/vif_unpack`) does not build: `input/ThreadBoost.h`
  is not on its include path. It was so before this session's changes.

Content / converter:

* Converted OotF terrain chunks (VU draw flags 0x126c, 225 vertices) carry +-FLT_MAX-like texture coordinates on 16 to
  213 of their vertices with an identity texture matrix. Drakan 2 terrain has none. Whatever computes the layer
  coordinates divides by zero or saturates for those vertices.
