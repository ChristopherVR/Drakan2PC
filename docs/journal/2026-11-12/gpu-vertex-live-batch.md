# The batch path at run time, in the `records` evaluation mode (2026-11-12)

Continues [gpu-vertex-batch-path.md](gpu-vertex-batch-path.md) (the stages, the record and the tracker) and
[gpu-vertex-implementation.md](gpu-vertex-implementation.md) (the increments). Written by the `live` subagent of the
GPU vertex work. Paths under `build/review/gpu-vertex2/live` are untracked scratch. No shader is involved: under
`DRAKAN_VERTEX_EVAL=records` (and `gpu`, until a backend has a batch shader) the renderer evaluates every batch record
on the CPU.

Code: `ps2recomp/ps2xRuntime/src/lib/vu/ps2_vu1_gpu_live.{h,cpp}` (the hook, the VIF's reports),
`vu/ps2_vu1_gpu_batch.h` (tracker additions), `vu/ps2_vu1_gpu_program.h` (the strip hook's question),
`vu/ps2_vu1_check.cpp` (the shadow comparison), `ps2_runtime.cpp` (the two call sites), `ps2_vif1_interpreter.cpp`
and `ps2_memory.cpp` (writes into VU memory), `runtime/gs/src/gs_core.cpp` (`vertexBatch`),
`runtime/gs/include/gsr/vertex_path.h` (two more stage bits, a counter, the worker's flag).

## 1. What happens before a VU1 run

Every VU1 run of the game starts in one of two callbacks of `ps2_runtime.cpp` (`setVu1MscalCallback`: MSCAL and
MSCALF; `setVu1MscntCallback`: MSCNT), whether the VIF stream is executed on the VU1 worker (`vu1async`) or on the EE
thread. Both now do, before `execute` / `resume`:

```
if (gsr::vpath::vuAttention())          // one atomic load; zero under the player's `cpu`
    vertexPath = vu1gpu::live::beforeRun(run);
```

`beforeRun` reads the control word (evaluation mode and stage bits) for this run and drives
`vu1gpu::batch::Tracker::step` with the family mask the stage bits give (`familiesOfStages`; the two families that had
no bit got one: `texgen`, `clipinside`). A run the tracker takes is not executed: the pc is set to where it would have
stopped and, for an index-list run, the record goes to the renderer in the PATH1 stream. A run it lets through
executes on memory the tracker has brought up to date. After the tracker, a strip stage may still leave as a strip
record (section 4). Every decline is counted in `vpath::Fallback` (`clip codes` and `clipper` as `clipper`, a disabled
family as `stage off`, the engine not at a run boundary or micro memory not the compiled image as `engine pending`,
the shapes the recogniser does not follow as `not recognised`, and the new `stale input`), every taken run in
`countTakenStages` (one count per stage family of the run; the vertices of a record are its indices).

Other places a run can start, and what they do:

| Where | Vertex path |
| --- | --- |
| `vu1batch` (`DRAKAN_VU_BATCH`, the parallel executor: private `PS2Memory` and engine per worker, results copied back with `MemoryState::restore`) | its executors have callbacks of their own and never reach the hook. While it is enabled the tracker does not follow at all; batch runs that come through the serial path are counted `engine pending`, strips are handed over as before |
| `vu1check::run` (`DRAKAN_VUCHECK`, both engines side by side) | the tracker does not follow, nothing is handed over |
| a recording (`DRAKAN_VU_RECORD`, `DRAKAN_VU_RING`) | the tracker does not follow (a recording has to hold the microprogram's memory); strips are handed over as before |
| the exact-engine fallback, the lanes engine, the native kernels (`Vu1Func::run`, `DRAKAN_VU_LANES`, `Vu1Step::*Kernel`, `Vu1Fast::nativeFill`) | they execute inside `execute` / `resume`, after the decision |
| micro memory that is not the compiled image | the run executes on memory brought up to date, the marks stay, `engine pending` |

## 2. Switching the mode at any run boundary

The control word can change at any time (the setting, `DRAKAN_VERTEX_TOGGLE` at a frame boundary of the renderer); the
VU1 worker meets the new value at whatever run comes next, in the middle of a batch or between two.

* **Leaving `records`.** Under `microprogram` the hook first calls `materialiseAll` (what was deferred is written to
  VU memory with the CPU evaluation, before an MSCNT and also before an MSCAL, which would otherwise drop it and leave
  the lanes unknown) and then steps the tracker with an empty family mask: every run executes, and the tracker only
  moves its marks (an executed stage with a known input makes its output known again, an EE write makes a lane known).
* **Why it keeps following.** After a batch whose runs were not executed, VU memory holds lanes that are not what the
  microprogram would have left (the tracker's "stale" lanes). They are gone when the EE or an executed stage writes
  them again, which the tracker can only know if it sees the runs and the writes. The alternative in the hand-over
  notes, "stop, and mark everything unknown on re-entry", is not safe with this tracker: a vertex run whose inputs
  are marked unknown is not executed (unknown in, unknown out), so marks that are merely cautious would turn memory
  that was right into memory that is wrong. So the tracker follows for as long as one lane is pending or unknown
  (`Tracker::clean()`), and stops at the first MSCAL at which none is: then `g_following` is cleared, the VIF stops
  reporting and `vuRest()` clears the worker's flag, so the cost under `cpu` is again one load per run and one per
  UNPACK. A later entry into `records` starts from that clean state, which is the tracker's initial state.
* **How long that takes** depends on the scene: on foot in Surdana the tracker stopped within the same microprogram
  phase every time (9 starts, 8 stops in one toggle run); over Ravenshold on Arokh it kept following through the
  microprogram phases (terrain slots that no batch refills).
* **What is not repaired.** A lane left unknown stays what it is until something writes it. A run that draws from one
  is counted (`vpath::Counters::staleDraws`, printed in capitals by `DRAKAN_VERTEX_LOG`); it is the one case that is
  not exact, in either mode after a switch, and it was not observed (section 6).

The policy has a unit test (`vu_gpu_batch_tests.cpp`, `modeSwitchTests`): two VU memories side by side, one in which
every run executes and one whose family mask changes per run (`records`, `microprogram`, toggles with periods of 1 to
64 runs, a random mode, random stage families), 24 terrain-like batches with a run of another program between them.
After every run, every lane the tracker calls real must equal the reference memory, and at the end the packets of all
index-list runs must be the same.

## 3. Who writes VU1 data memory

The tracker needs every write between two runs, lane by lane. From the code (all writers of `PS2Memory::m_vu1Data`):

| Writer | File, function | Handling |
| --- | --- | --- |
| VIF1 UNPACK, fast path | `ps2_vif_unpack.h` `vif_unpack::fast`, called from `PS2Memory::processVIF1Data` (`ps2_vif1_interpreter.cpp`) | reported: `vu1gpu::live::noteUnpack` with the unpack's own arguments. The lanes follow from the format, STMASK and STMOD: a field the format supplies, or one filled from ROW or COL, is written; a write-protected field is not; a field a V2 / V3 format does not supply keeps its value, except in an adding mode, where it becomes old value plus ROW and is marked unknown |
| VIF1 UNPACK, scalar path (fill writes, the V4-5 format, a recording, the batch executor's tracing, `PS2X_VIF_FAST_UNPACK=0`) | the loop in `PS2Memory::processVIF1Data` | reported per quadword: `noteWrite`, with the same correction for lanes without source data as the recorder; the raw-copy fallback reports whole lanes as written and a partly written lane as unknown |
| the EE through its address space (0x1100C000) | `PS2Memory::write8 / 16 / 32 / 64 / 128` through `mapVuMemory` (which first waits for the VU1 worker) | any access, read or write, first writes what was deferred (`directAccess`); whole lanes written are reported (`directWrite`), a partly written lane keeps its state. Counted; 0 in every run so far |
| a VU1 run | `VU1Interpreter::execute` / `resume` and the engines under them (`Vu1Func::run`, the lanes engine, `Vu1Step::packetKernel` / `uvKernel` / `transformKernel`, `Vu1Fast::nativeFill`) | decided before the run. A batch stage that executes moves the marks by its read and write lanes; a run of another program (the object path) is executed on memory brought up to date and its own writes are not followed (marks only stay, which is the cautious side) |
| the tracker | `Tracker::materialiseSlot`, the clip-code entries (quadwords 27 to 43 and 65) of a taken list, quadword 47 of a deferred flat grid | its own writes are what the microprogram would have written |
| the parallel batch executor | `vu1batch::MemoryState::restore` (`vu/ps2_vu1_batch.cpp`) copies a whole VU memory | cannot be followed: the tracker is off while `DRAKAN_VU_BATCH` is on |
| the differential check, a recording, the replay | `vu1check::run`, `vu1check::record`, `vu1check::bench` | tracker off (the replay is its own process mode) |
| start-up | `PS2Memory::initialize` (zero fill) | before any run |

Readers only: the upload hook `drakan_vu1::g_uploadHook` (the temporal upscaler's camera tap, `drakan_dlss.cpp`) and
`vu1batch::memoryWrite` / `trackVif` (the batch executor's trace of what the VIF wrote; they report, they do not
write).

That the list is complete for real play is what the shadow mode measures (section 6): its memory receives only the
reported lanes, and every lane of the game's VU memory that changes between two runs without a report is counted.

Cost of the reports: section 7.3.

## 4. The strip stage's question

The strip stage reads quadwords 54 to 772, which overlap the three batch arrays. Before the hook hands a strip run
over, `stripInputsKnown` walks the strip table of the record just built and asks the tracker for exactly the lanes the
evaluation reads: four lanes of each position, three of each texture coordinate, four of each normal for the lit
stage only, and the length words. An unknown lane declines the run (`stale input`) and the microprogram executes it.
The hand-over notes proposed the whole range (`unknownLanes(54, 719)`); that would decline nearly every strip run,
since the slots a batch did not refill stay marked. Unit test: `stripInputTests`. Live: 0 strip runs declined for this
reason in all runs of section 6.

## 5. The renderer

`Renderer::Impl::vertexBatch` (`gs_core.cpp`, next to `vertexProgram`, called first for every record tag): the record
is parsed with `parseBatch` (a malformed record is ignored; the parser reads nothing outside the record's size), each
vertex is evaluated once with `evaluateVertex`, and the strips go through the ordinary vertex kick exactly as the
packets would: per strip a PRIM write (the tag has PRE = 1) and per index ST, RGBAQ, FOG and XYZ2 with the skip bit;
after the last strip of a run with clip codes the PRIM write of the closing empty tag, which starts a new primitive.
The registers and the vertex queue are therefore the packets' own. Under `Eval::Gpu` each batch record counts
`backend has no path` and is evaluated the same way.

## 6. Proof

All counts are exact; scratch logs under `build/review/gpu-vertex2/live/logs`.

### 6.1 A fault the shadow comparison found, and its fix

The first long shadow run (Surdana, 10,748,155 runs) showed 103 executed runs whose packets differed from the
microprogram's, 8,993 runs before which quadwords 28 and up of the shadow memory differed (206,135 lanes), and one
index-list run refused for a stale input; record vertices: 193,324,625 compared, 0 differing. Cause: an index-list run
of a batch with clip codes takes the "code" of an end index from lane w of quadword 515 + index, which for index 0 is
the alpha lane of the last colour slot (slot 224), whether or not the batch filled that slot. A batch that deferred a
stage on slot 224 (a grid of 15 x 15 with a point light, which replaces alpha) left that lane unknown at the next
set-up, and a later clip-code list, executed by the microprogram, read it and built other strip entries from it. The
sixteen recordings of the replay proof do not contain the case.

Fix (`Tracker::decide`): at a new call the last slot is brought up to date (`materialiseSlot(224)`) before the rest of
the batch's deferred stages are dropped, so that lane is always the microprogram's; and an executed clip-code list
whose code lane is unknown for another reason is now counted in `staleDraws`. Cost: 1,837 slot evaluations in the
sixteen recordings together. Unit test `lastSlotTests`. All numbers below are from the build with the fix.

### 6.2 Counts

| What | Result |
| --- | --- |
| VU1 replay of the 16 recordings with VIF write lists (`DRAKAN_VU_BENCH_BATCH=1 DRAKAN_VU_BENCH_GPUPROG=1`, `live/run_all.sh`), this build | 34,725 vertex runs evaluated against the microprogram's memory, 0 differ; 57,528 records, 2,604,739 vertices, 0 differ; 35,473 runs executed on the tracker's memory, 0 with other packets, 0 lanes other than recorded; 0 draws from an unknown lane; 0 lanes changed without a reported write; strips 708,250 vertices, 0 differ; declined: clip codes 5,059, clipper 7,243, nothing else |
| `vu_gpu_batch_policy` (also `vu_gpu_batch.exact`) | 5,625,396 checks, 0 failed (25,027 new: mode switches, when the tracker may stop, range reports, unknown writes, the last slot, the strip question) |
| `vertex_path` | passes (stage names and bits, the `stale input` counter, the worker's flag) |
| `vertex_records` on Direct3D 11, `vertex_records_webgpu_vulkan`, `vertex_records_webgpu_d3d12` (RTX 5080), scales 1, 3 and 8, identical totals on the three | 78 scene and mode combinations in 102 sequences; 52,683 runs sent as records with 1,560,132 vertices, of them 9,300 batch records with 498,576 vertices; 1,464 colour frames, 0 of 10,354,032,640 pixels differ; 288 depth pictures, 0 of 2,036,858,880 pixels differ; 14,619 records counted `backend has no path` under `gpu`; controls differ as intended (12 of 21 colour frames, 15 of 21 depth pictures); 8,187 checks, 0 failed |
| whole `runtime-tests` preset (private tree, two jobs) | 173 tests, 0 failed, 7 skipped (need game data) |
| `python scripts/run_tests.py`, `scripts/repository_guard.py`, clang-format 19 on the changed files | OK; 0 prohibited; clean |

The batch part of the records test: scene `batch` (every pass kind: transform alone, lights and transform, processed
rows without a pass, texture coordinates from integers, grid terrain, three texture matrix layers with a fade that
leaves vertices out, flat grid with a point light of every exponent, a chain of 24 passes, clip-space positions with
the Project pass and the closing empty tag, a fade after lights and transform) and scene `batchmix` (batch records
between strip records, ordinary packets and texture uploads; strips of one and two vertices with shared vertices; the
skip bit; vertices behind the eye; the vertex queue after a record with and without the empty tag; a record that sends
only the empty tag before a tag without PRIM; colour, texture coordinate, Q and fog inherited by packets that send only
places). Sixteen malformed records (indices, strip lengths, pass kinds, pass masks, an exponent, every count of the
header including values that wrap the size computation, a record cut short) in front of every batch record of the
second renderer change no pixel.

### 6.3 Live, paused, the mode alternating every 64 frames inside one run

`DRAKAN_VERTEX_EVAL=records DRAKAN_VERTEX_TOGGLE=64 DRAKAN_FOLIAGE_WIND=0`, 1280x960 window, DLSS off, 24 screenshots
450 ms apart from second 10 of the pause (`live/tog.sh`, `live/cmpall.py`: whole pictures outside the frame-rate text).

| Scene | Shots under microprogram / records | Different pictures | `stale input`, draws from an unknown lane |
| --- | --- | --- | --- |
| Surdana start | 5 / 19 | 1 | 0, 0 |
| Surdana, the fight spot | 4 / 20 | 1 | 0, 0 |
| Ravenshold, mounted on Arokh | 5 / 19 | 1 | 0, 0 |
| Inquisitor's Lair arena (interior) | 11 / 13 | 1 | 0, 0 |
| Catacombs (interior) | 6 / 18 | 1 with the torch flame (it animates while paused, also in a run under the microprogram alone) and the frame-rate text left out; 23 otherwise | 0, 0 |
| Order of the Flame, Ruined Village | 10 / 14 | 1 | 0, 0 |
| Order of the Flame, Volcanic | 10 / 14 | 1 | 0, 0 |

Stage families switched off in Surdana (`DRAKAN_VERTEX_STAGES`), same procedure, 24 shots each: `-strips`, `-prims`,
`-light`, `-fade`, `-texmatrix`, `-grid`, `-texgen`, `-point`, `-clipinside`, `prims`, `strips,light`, `none`:
12 of 12 runs with one picture (5 to 13 shots under the microprogram, 11 to 19 under records), `stage off` counted in
each (985 to 2,008,557), 0 `stale input`, 0 draws from an unknown lane. In all these runs: 0 direct EE accesses to VU
memory, 0 UNPACKs through the scalar path, 0 lanes written with unknown content.

### 6.4 Shadow mode: records beside the microprogram, in motion

`DRAKAN_VERTEX_SHADOW=1` (`vu1check::shadow*`): the game's VU1 executes every run and draws. A second tracker decides
as under `records` on a memory of its own, which receives only what the VIF and the EE report as written and what the
tracker and the runs it lets through write; a second engine executes those runs there with its packets dropped. Per
run: a record's CPU evaluation (`batchToGif`) against the packets the game's VU1 sent (tag, count, order and every
field a GS reads); an executed run's packets against them; the shadow memory against the game's before an executed
run, in every lane not marked; a vertex run's stages evaluated on the game's memory against what the microprogram
leaves; and every lane of the game's memory that changed since the previous run without a report. This is sound
because the game itself runs unchanged: nothing the tracker defers touches the memory the microprogram reads. What it
does not cover is the strip hook and the renderer (sections 6.2 and 6.3 do).

| Run (`live/shadow.sh`) | Runs | Records | Vertices compared | Vertex runs evaluated | Runs executed on the shadow memory |
| --- | --- | --- | --- | --- | --- |
| Surdana on foot, 179 s: running with turns and jumps, two teleports, 24 s of attacks at the fight spot, quick save, quick load (the level loads again), pause menu | 12,128,899 | 4,229,186 | 211,795,658 | 1,851,859 | 6,047,854 |
| Surdana, then the level change to the City of the Damned and 30 s of running there | 2,867,346 | 974,908 | 48,394,996 | 426,107 | 1,466,331 |
| Ravenshold, 60 s of flight on Arokh with turns and fire | 2,786,877 | 1,477,323 | 73,896,062 | 815,785 | 493,769 |
| Order of the Flame, Ruined Village, 60 s on foot | 6,482,420 | 2,599,061 | 63,758,143 | 3,225,867 | 657,492 |
| the intro cut scene played out (two minutes), then the Andrellian Isles and 30 s on foot | 9,496,846 | 3,339,126 | 161,980,531 | 1,873,452 | 4,284,268 |
| **all** | **33,762,388** | **12,619,604** | **559,825,390** | **8,193,070** | **12,949,714** |

Differing in all five: 0 vertices, 0 records of another shape, 0 runs taken without a record although the
microprogram sent packets, 0 vertex runs (0 lanes), 0 executed runs with other packets, 0 lanes of memory, 0 lanes
changed without a reported write, 0 runs drawing an unknown lane. Declined: clip codes and clipper only.

### 6.5 Captures taken under `records`

`PS2X_GS_CAPTURE` from the start of the game with `DRAKAN_VERTEX_EVAL=records` (`live/cap.sh`: a turn, a run forward,
a turn), replayed with `scripts/harness/replay_hash_check.py` on Direct3D 11 at scale 3 with HD textures under
`DRAKAN_VERTEX_EVAL=records` and `=microprogram` (a record is honoured under both), then deleted.

| Capture | Size | Frames | Replay | Contact sheet (24 frames, every 20th of the last 520) |
| --- | --- | --- | --- | --- |
| Surdana walk | 5.7 GB | 1,657 | no error; the two modes 0 of 1,657 frames differ | ground, walls, trees, Rynn and her shadow, HUD as expected; nothing missing, no cracks seen |
| Ravenshold flight | 6.7 GB | 1,505 | no error; 0 of 1,505 differ | sea, mountains, tower and ship in fog, Arokh, snow; as expected |
| Order of the Flame, Ruined Village walk | 10.5 GB | 1,796 | no error; 0 of 1,796 differ | night scene, walls, palms, fires; Rynn runs into a wall and the last three frames show the camera inside it (the game's camera; not compared with a microprogram run) |

The sheets are judged by eye at 400 x 300 per frame; they cannot show a one-pixel fault. The exact evidence is 6.3
and 6.4.

## 7. Cost

### 7.1 `cpu` against `records`, live

`scripts/perf/scene_ab_bench.py` (one game at a time through its turn lock), `--variant
cpu:DRAKAN_VERTEX_EVAL=microprogram --variant records:DRAKAN_VERTEX_EVAL=records,DRAKAN_VERTEX_LOG=1`, 1280x960
window, DLSS off, internal scale 2 (automatic), a 12 s window per run, runs interleaved. Median (range) over the runs;
CPU milliseconds per world frame of the named threads.

| Scene | Variant (runs) | World frame ms | World frames per second | EE | VU1 worker | GS worker |
| --- | --- | --- | --- | --- | --- | --- |
| Surdana start | cpu (4) | 5.83 (5.16 to 6.57) | 167.54 (151.71 to 184.22) | 5.51 (4.92 to 6.05) | 5.42 (4.95 to 5.83) | 4.39 (3.91 to 4.68) |
| | records (3) | 33.32 (33.31 to 33.33) | 33.18 (32.06 to 33.94) | 29.60 (28.79 to 30.64) | 3.40 (3.28 to 4.22) | 29.51 (28.79 to 29.58) |
| Ravenshold on Arokh | cpu (3) | 3.24 (2.98 to 3.65) | 289.60 (271.37 to 307.73) | 3.35 (3.14 to 3.52) | 3.03 (2.91 to 3.12) | 2.94 (2.80 to 3.10) |
| | records (3) | 16.97 (16.75 to 17.40) | 57.73 (55.60 to 58.98) | 3.53 (3.46 to 4.58) | 2.73 (2.32 to 2.99) | 16.96 (16.77 to 17.59) |
| Order of the Flame, Ruined Village | cpu (3) | 8.15 (7.25 to 9.41) | 107.83 (105.88 to 129.69) | 8.96 (7.63 to 9.38) | 5.69 (5.64 to 6.84) | 7.17 (6.97 to 8.45) |
| | records (3) | 16.67 (16.67 to 16.67) | 56.42 (55.93 to 56.50) | 17.31 (17.02 to 17.39) | 6.49 (6.12 to 6.55) | 16.78 (16.65 to 16.87) |

Conditions, plainly: the machine was not quiet. Of the 19 runs, 4 started on a quiet machine (the first two of each
Surdana variant); during the others two to six replay processes of another agent ran, and during some one other game.
The Surdana rows hold 5 runs of a first benchmark (default waiting for a quiet machine, stopped after about 45 minutes
because the machine never became quiet again) and 2 of a second (`--wait-quiet 45`); Ravenshold and Ruined Village were
measured with `--wait-quiet 45 --keep-busy` throughout.

Reading:

* **The GS worker pays.** It evaluates every record vertex in double arithmetic: 77,900 record vertices per world frame
  in Surdana (246 strip records, 769 batch records), 55,000 over Ravenshold (1,103 batch records), 30,200 in the
  Ruined Village (29 strip records, 1,856 batch records). GS worker per world frame: 4.39 to 29.51 ms, 2.94 to 16.96
  ms, 7.17 to 16.78 ms. The game is then bound by the GS worker: the frame rate falls to 20 %, 20 % and 52 % of `cpu`.
* **The frame times under `records` in Surdana and the Ruined Village sit on 33.3 and 16.7 ms** with almost no
  spread, which is a pacing step, not a compute time; there the GS worker's figure includes whatever it does while it
  is not evaluating. The cost of the evaluation itself is 7.2 below.
* **The VU1 worker gains less than the replay promised**: 5.42 to 3.40 ms in Surdana (minus 37 %), 3.03 to 2.73 over
  Ravenshold (minus 10 %), and in the Ruined Village it is slower, 5.69 to 6.49 ms (plus 14 %). The replay had minus
  66 %, minus 21 % and minus 33 % for the same scenes: it leaves out the reports of the VIF (7.3), building the strip
  check, and the submission of records of 64 bytes per vertex to the renderer queue. Per world frame the Ruined
  Village has 19,034 UNPACKs and 1,856 records.

### 7.2 The evaluation alone, from replays

To separate the evaluation from the pacing: for Surdana and the Ruined Village, two captures of the same input recipe,
one under the microprogram and one under `records`, each replayed three times with `gsr_replay --scale 2 --no-png
--bench` over its last 500 frames (renderer calls on the calling thread, Direct3D 11; `live/repbench.sh`; other load
on the machine as above; the two captures of a scene are not the same frames).

| Capture | Vertices per captured frame | Renderer ns per vertex (three replays) |
| --- | --- | --- |
| Surdana, microprogram | 82,180 | 54.6, 77.0, 85.6 |
| Surdana, records | 32,131 | 375.8, 379.8, 383.9 |
| Ruined Village, microprogram | 48,770 | 281.0, 283.5, 285.9 |
| Ruined Village, records | 30,666 | 508.9, 515.5, 541.6 |

So a record vertex costs the renderer thread about 290 to 330 ns more than a packet vertex in Surdana and 220 to 260
ns more in the Ruined Village: with the vertex counts above, 22.6 to 25.7 ms and 6.7 to 7.9 ms per world frame. The
Surdana figure agrees with the live one (29.51 minus 4.39 = 25.1 ms); the Ruined Village figure is below the live
difference (9.6 ms), which the pacing step explains. This is the amount a fast evaluator (or the shader) has to
remove; the tracker and the hook are not where the time goes.

### 7.3 The VIF's reports

Counted inside the game while the tracker follows (processor ticks around the unpack and around the report, three
tick reads per UNPACK, which are part of both figures), from the `[vertex-path] batch tracker` lines inside the
measured windows of the `records` runs:

| Scene | UNPACKs per world frame | Unpack itself, ms per world frame | Report, ms per world frame |
| --- | --- | --- | --- |
| Surdana (3 runs) | 5,736 | 0.258 (0.250 to 0.262) | 0.307 (0.306 to 0.322) |
| Ravenshold (3 runs) | 5,616 | 0.291 (0.289 to 0.306) | 0.421 (0.417 to 0.445) |
| Ruined Village (3 runs) | 19,034 | 0.423 (0.420 to 0.446) | 0.572 (0.570 to 0.589) |

The report costs more than the unpack it follows (30 to 75 ns per UNPACK against 22 to 52 ns): most UNPACKs are a
quadword or two of constants, for which the call and the range test are the whole cost. All UNPACKs took the fast
path (0 through the scalar path). Under `cpu` none of this runs: the UNPACK then has one flag test more than before
this change, which was not measured (it is below what these runs can resolve). A cheaper report is possible (the
range test inline at the call site, before the call) and was not done.

## 8. Not verified

* No shader draws a batch record: `gpu` on any backend evaluates them on the CPU and counts `backend has no path`.
  With a backend that draws strip records, `gpu` would therefore be slower than `cpu` in batch-heavy scenes until the
  configured word for `gpu` leaves the batch stages off or a batch shader exists. Not decided here.
* The live proofs compare paused pictures (6.3) and, in motion, the records with the microprogram's packets (6.4).
  A game in motion drawn under `records` was looked at only on the three contact sheets; no frame of a moving
  `records` game was compared with the same frame under the microprogram (two runs do not produce the same frames).
* The shadow mode does not exercise the strip hook's question (section 4) or `vertexBatch`; those are covered by the
  unit test, the records test and the paused toggles. `stale input` was 0 for strips in every live run, so the
  declining branch itself ran only in the unit test.
* A lane left unknown after a batch that was not executed is not repaired. Three readers of such lanes exist: an
  executed index list (counted in `staleDraws`), the strip stage (declined and counted as `stale input`; the
  microprogram then draws it from that memory) and a run of another program of the object path that is not the strip
  stage (not checked at all: its inputs are the EE's uploads for that object in every case seen). 0 `staleDraws` and 0
  `stale input` in all live runs and in the 33.8 million runs of the shadow mode after the fix of 6.1; before the fix
  the shadow mode found 103 such runs in one, so the absence of further cases of this kind is an observation, not a
  proof.
* `Tracker::clean()` and the stop of the reports were observed (the tracker stopped following 1 to 30 times per toggle
  run) but the cost under `cpu` after a stop was not measured separately from a run that never left `cpu`.
* The scalar UNPACK path, an UNPACK in an adding mode over the arrays, and a direct EE access to VU memory did not
  occur in any run (0 each): their handling is exercised by the unit test of the tracker (`noteUnknown`) only, the
  lane computation of the scalar path by no test.
* `DRAKAN_VU_BATCH`, `DRAKAN_VUCHECK` and a recording together with `records`: read from the code (the tracker stays
  off), not run.
* Micro memory other than the compiled image while the tracker follows: not run (the game loads one microprogram).
* Not tried: the USA disc, a game with DLSS on under `records`, the WebGPU renderer in the live game (the records test
  covers its core path), the owner's settings file, levels other than those named, spell effects, multiplayer.
* The D3D12 records test lost its device once at scale 8 while a game and other GPU work ran beside it, and passed when
  run again alone and inside the `runtime-tests` run.
* The Catacombs toggle is identical only outside a torch flame that animates while paused.
* The measurements of section 7 were taken while other agents' games and benchmarks ran on the machine (noted there).
