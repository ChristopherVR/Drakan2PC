# Pacing follow-up: CPU candidate and measurement contract

Integrated live validation is complete: the final candidate improved the matched
late sample from **19.950 to 29.893 unique world submissions/s**, suppressing
180 redundant handoffs in the selected windows. See
[the measured comparison](pacing-benchmark.md) for scope and limits.
The candidate/proposal notes below retain their original validation stages.

Runtime candidate: nested PS2Recomp commit `132c295`, based on `2c863bd`.
Outer additions are CPU tests and a read-only log summary script. No GPU/game
launch, generated-game compilation, dist modification or deployment was done.

## Concrete findings

- The old `[gsr-pacing] host presents` field increments after every host tick,
  including neural ticks that return without presenting. It cannot establish
  unique world FPS.
- The old overlay `Game` value uses `ps2perf::g_flips`. That counter receives
  DISPFB1 register-write changes and combined DISPFB1/2 changes sampled by
  `GS::vsyncHost`; the same change can contribute twice. It is not an engine
  update counter. Existing guest execution/flip instrumentation is preserved.
- `tickWindow` previously released the renderer lock before sampling the latch
  serial to acknowledge. A worker publication in between could be acknowledged
  without being presented. The candidate captures under the renderer lock and
  acknowledges only that captured serial.
- With neural rendering active, no explicit frame cap, and the settings overlay
  closed, a completed latch now wakes the host's condition-variable wait.
  This removes the unconditional wait for the next host timer tick. Waiting
  remains outside renderer locks, uses the queue mutex to avoid lost wakeups,
  and times out after 16.667 ms so window messages continue to be pumped.
  Native pacing, explicit limits, minimization and overlay timer paths remain.

## Counters

The overlay shows `World` and `Present`; detailed mode adds `Flip events`.
World counts completed positive camera/world scenes with actual draw calls,
excluding invalid cameras, GUI previews, duplicate EndWorld markers and worlds
with no draws. It measures rendered world submissions, not EE logic iterations
or physical display scanouts. Menus without positive world metadata report zero.
Present measures the existing presentation callback cadence, not scanout.

`PS2X_GS_PACING=2` emits separate rates for `hostTicks`, `presentCalls`,
`worldUpdates`, `worldPresents`, and `guestVsyncs`. `worldPresents` counts present
calls carrying a different completed-world serial, associated at GS vsync;
multiple world updates coalesced before presentation count once here.
The bridge has no successful-scanout return value, so these are submission
counts. `tickMs` is average/min/max host interval. Millisecond totals per window:

- `waitMs`: host timer/condition-variable wait.
- `lockMs`: host acquisition of the bridge renderer mutex.
- `presentMs`: resize synchronization and renderer presentation call.
- `drainMs`: producer queue-drain wait.
- `handoffMs`: worker wait for neural host acknowledgement.
- `fairMs`: worker yielding to a waiting window thread.

These totals can overlap across threads and must not be added as frame time.
Counter initialization precedes worker startup; exchanging the vblank counter
avoids losing increments during window reset.

## Scene barrier audit

`sceneSink` ordering barriers are retained. `processPendingTransfers` may itself
wait for VU1 when GIF/VIF0 work is pending; the subsequent `vu1async::sync` is an
atomic fast-path no-op if no VU jobs remain. Camera duplicates are already
filtered before reaching the sink. Removing these barriers without observing
the pending work could reorder world/HUD draws.

Set `PS2X_SCENE_PACING=2` to collect bounded CPU summaries grouped into phase
begin/end, canonical EndWorld, camera, preview and GUI markers. Each row reports
calls, markers with pending VU work before/after transfer processing, transfer
processing milliseconds, final VU wait milliseconds and maximum combined time.
The transfer-processing bucket includes any waits internal to that function.
It does not independently identify a disk or cache stall. Disabled profiling
does not take timestamps or print per-marker logs.

## Evidence and limits

Read-only summaries of existing logs:

- `inventory-final-clean-perf/runtime.log`: 29 old pacing windows, median
  60 host ticks/s and 60 GS vblanks/s, no screenshot indicators.
- `inventory-final-srnr/stderr.log`: 53 old pacing windows, median
  60 host ticks/s and 25.3 GS vblanks/s, three screenshot indicators.

Neither records unique world cadence or scene-barrier timings. The second run
is capture-confounded. No cause is assigned to the reported 1.6-second guest
stall from these aggregates.

MSVC CPU policy tests passed with `/W4 /WX`, including a publication after host
capture, condition-variable wakeup, explicit-cap/overlay guards, duplicate
camera/end markers, no-draw scenes, GUI and invalid cameras. The three changed
runtime translation units (`gs_gsr_bridge.cpp`, `drakan_fpsmeter.cpp`,
`drakan_dlss.cpp`) compiled independently with the existing configured public
include paths. Private evidence: `build/pacing-followup/test-policy.log` and
`build/pacing-followup/compile-units.log`. This is not a link or game test.

## Integration and coordinated verification

1. Cherry-pick nested runtime `132c295` into the active runtime; merge the outer
   tests/docs script commit separately. No `d3d11_backend.cpp` changes are needed.
   The DLSS glue hunk only instruments `sceneSink`; preserve any independently
   added developer toggle logic elsewhere in that file.
2. Regenerate the sanitized runtime patch using the existing publication flow;
   include the two new authored `gs_*.h` headers. Do not export private VU data.
3. In a coordinated GPU slot, compare native and SR/NR runs with identical input
   and config, captures/dumps/audio debug disabled, using both pacing variables.
   Inspect world rates, host lock time, worker handoff and scene sync totals.
   Check overlay labels and behavior with settings open, frame caps, resize,
   minimize/restore, feature toggles, movies and GUI transitions.
4. Any measured gameplay gain or regression remains pending that validation.

Read existing logs without starting the game:

```powershell
python scripts/perf/pacing_summary.py path/to/runtime.log path/to/other.log
```

From a VS 2022 x64 developer shell, compile and run the policy test (replace
the runtime path if integrating into a different checkout):

```powershell
cl /nologo /std:c++20 /EHsc /W4 /WX /D_CRT_SECURE_NO_WARNINGS `
  /I../PS2Recomp-pacing2/ps2xRuntime/src/lib/gs /Iruntime/gs/include `
  scripts/tests/pacing_bridge_test.cpp /Fobuild/pacing-policy.obj /Febuild/pacing-policy.exe
./build/pacing-policy.exe
```

## Guarded duplicate-handoff rearm candidate

Nested follow-up `b2aef0a` replaces permanent disarming after a live-setting
change. Four files change: `gs_vblank_handoff.h`, `gs_gsr_bridge.cpp`, and the
settings-host header/source. The latter add a narrow `WindowOps` notification
for deinterlace/flicker setters; no graphics backend changes are required.
Changed-mask bits 0..1 select deinterlace, bit 2 selects flicker. Unchanged
fields retain the actual startup/environment selection.

After any setting/mode transition, the policy always hands off until a new
positive world completes after the observed change. That world's first sample
also hands off. Only a subsequent identical interval in that same closed
world can skip. Every sampled display word, draw/transfer/upload/readback/CPU
write counter, scene revision and live-setting epoch remains part of equality.
Native mode, GUI/movie/unknown metadata, open worlds and field-sensitive modes
always hand off. This is a host handoff policy; guest execution and vblanks are
never skipped. Future direct callers of deinterlace/flicker setters must also
notify the bridge contract, or use a renderer API exposing effective modes.

With `PS2X_GS_PACING=2`, `[handoff-pacing]` emits bounded counts for native,
field-sensitive mode, setting change, awaiting a fresh world, open/no completed
world, scene/draw/transfer/display changes, initial baseline, duplicates and
rearms. Reasons overlap; do not sum them as exclusive vblank categories.

CPU validation: 170 checks pass in `scripts/tests/vblank_handoff_test.cpp`.
They cover repeated settings changes, withholding rearm on stale/unfinished
worlds, every one of the 17 display words and all six content counters,
native/UI/preview transitions, unsafe/unknown modes and partial mode updates.
Changed bridge and settings-host translation units compile successfully.
Private evidence is `build/pacing-followup/test-handoff.log` and
`compile-units.log`. No live improvement has yet been measured for this commit.

The before run is `build/t/followup-pacing-srnr`, executable hash prefix
`ABF0E7CF`, exit 0, script quit, no PNG/RAM captures, SR Quality + NR, scale 4,
3840x2160 borderless client (2048x2048 internal output). Its last five pacing
windows total 10.015 seconds: 19.97 completed worlds/s, 20.07 unique-world
present calls/s, 40.13 total present calls/GS vblanks per second. Presentation
averages 7.41 ms/call; handoff waiting 7.38 ms/call overlaps it. Bridge lock
waiting totals 0.02 ms in those windows; drain/fairness waits round to zero.
EndWorld VU waiting totals 45.1 ms across 72.7 measured seconds, maximum
marker 0.259 ms. Scene barriers are not a demonstrated bottleneck in that run.

Main should build the follow-up incrementally and rerun the same benchmark in
the coordinated GPU slot. Compare the same late gameplay segment, world
counts and marker mix, report duplicate/rearm/reject reasons, and verify GUI,
movie, bob/blend/flicker and feature-toggle behavior. The old capture-confounded
run cannot establish an exact speedup. The after measurement remains pending.

### Actual rearm run and marker diagnostic

`followup-handoff-srnr` completed with exit 0/script quit in 80.35 seconds,
executable hash prefix `C27468C4`. Last five windows: 20.00 worlds/s,
20.00 unique-world present calls/s, 40.00 total present calls/GS vblanks/s.
This is no material rate improvement over 19.97 worlds/s. Late windows report
`noCompletedWorld` on every vblank, zero open worlds, zero duplicates/rearms,
and no field-mode/setting rejection. Fixing permanent disarming did not address
the active rejection in this run.

Nested diagnostic-only commit `33a9aa5` records precisely which marker clears a
previously completed world. It retains existing suppression behavior. Pacing
summaries add `clear_phase`, `clear_viewport`, `clear_preview`, `clear_gui`, and
`clear_unknown`. At most four traces per category print source PC/flags, GUI,
the closing PC, and engine phase before/after (unknown=-1, closed=0, open=1).
Positive GUI observations have a separate category, so inventory samples do
not exhaust the gameplay viewport trace budget. 175 CPU checks pass, including
the trace-state transitions; changed runtime units compile.

At the diagnostic stage, `0x139010` flags 0 invalidated the old closure;
`0x147690` is canonical EndWorld; `0x1e97a0` is the
existing known preview exception; `0x430d68` explicitly carries inventory GUI
21. `0x1e8c78` only captures pending camera data and does not itself call the
sink. Published non-preview viewport packets come from `0x1e93b8`; they may
lack World after engine phase closure. The aggregate rejection log does not
prove that this packet is the active invalidator. No new exception is added
until ordered marker evidence identifies it; positive GUI, phase begin and
unknown markers remain invalidating.

### Live-confirmed pending engine entry

`inventory-category-calibration-01/stderr.log` subsequently captured four
identical clearing traces: source `139010`, flags 0, GUI -1, closed by `147690`,
phase 0 -> 1, with no preceding unknown clear. Thus the actual invalidator was
the next engine entry, not a post-world HUD camera. The source hook emits this
marker before executing original `139010`; setup reaches `146968 -> 1d8660`,
which can wait on a semaphore/state flag before later camera setup. Its specific
identity as a vblank limiter has not been established from static code alone.

Nested candidate `5f499bb` treats only that known phase-begin packet, with GUI
negative and an already completed world, as a pending next world. It retains
the previous closure until new content or a validated camera appears. The
bridge captures counters AFTER the marker flushes prior draws: anchoring to
the previous vblank instead would incorrectly classify the recently completed
world's draws as partial next-world work.

New draw/transfer/upload/readback/CPU-write activity after this anchor discards
the retained closure, so stable partial new-world work cannot be suppressed.
Repeated begin markers cannot move the anchor. An early end with no new content
may retain the old completion but cannot manufacture a fresh world for settings
rearming. GUI and unknown markers remain invalidating; all 17 display words,
live-setting versions, content comparisons and unsafe-mode guards remain.
Missing anchor information takes the conservative original path.

The only runtime files changed are the handoff header and bridge. New bounded
reasons are `pendingBegin` and `pendingContentChanged`. 337 CPU checks pass;
changed runtime translation units compile. Before rate remains approximately
20 worlds/s from `followup-handoff-srnr`; candidate live rate/visual behavior is
pending main's coordinated benchmark. No GPU launch was performed here.
