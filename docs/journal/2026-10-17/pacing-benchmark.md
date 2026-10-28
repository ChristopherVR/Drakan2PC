# SR/NR pacing measurement, 17 October 2026

## Result

The final candidate sustained approximately **30 completed worlds/s**, versus
approximately **20 worlds/s** before, in the measured late-gameplay segment.
Using the final three pacing windows of each run, the observed world-rate gain
is **49.84%** and the world interval decreases **33.26%**.
This is a bounded measurement, not a universal FPS or input-latency guarantee.

| Matched late gameplay | Before `ABF0E7CF` | Final `9E622D7E` |
| --- | ---: | ---: |
| Sample duration | 6.015 s | 6.021 s |
| Completed worlds/s | 19.950 | 29.893 |
| Unique-world present calls/s | 20.116 | 29.893 |
| Total present calls/s | 40.227 | 29.893 |
| Guest GS vblanks/s | 40.227 | 59.954 |
| World interval | 50.126 ms | 33.452 ms |
| Presentation time/call | 7.547 ms | 0.793 ms |
| Worker handoff wait/call | 7.495 ms | 0.812 ms |

The earlier **19.97 worlds/s** baseline used five gameplay windows (10.015 s).
The matched three-window baseline is 19.950 worlds/s. The final run's last five
windows include inventory/transition time, so averaging those would mix contexts.
Final pure-gameplay windows are **29.38, 29.95 and 30.35 worlds/s**; their
window-duration-weighted rate is **29.893 worlds/s**.

## Counter evidence

The intermediate guarded-rearm build `C27468C4` remained at approximately 20
worlds/s, with zero duplicate handoffs and a missing-completed-world rejection
on every late vblank. Live category calibration then identified the exact
invalidator: `139010`, flags 0, GUI -1, closed by `147690`, engine phase closed
to open. The hook preceded the original engine function's setup/wait/camera path.

After treating that entry as pending until new content or a validated camera:

- The final three windows suppress **180 duplicate handoffs** in 6.021 s.
- **180** scene/draw/transfer/display change observations remain handed off.
- There are **zero** missing-completed-world, unknown-clear, unsafe-mode or
  setting-change rejections in that late segment. Reason counts overlap.
- `pendingBegin=360`, `pendingContentChanged=0`, `clear_phase=0`; the retained
  closure now clears at the next validated `1e93b8` viewport/camera marker.
- The run also records a positive GUI-21 clear; the GUI guard remains active.
- No handoff timeouts, slow-host-tick warnings or screenshot indicators occur.
- Bridge lock, drain and fairness waits round to zero in the final three windows.

Presentation and worker handoff waits overlap; they must not be added together.
These are CPU-side submission/wait measurements, not SR/NR GPU evaluation timings.

## Recipe, completion and scope

All three runs match their recorded input-script hash, input-configuration hash
and selected environment fields: SR Quality, NR enabled, scale 4, audio and combat/sound debug
logging disabled, pacing summaries enabled, and the same frame-based script.
The actual borderless client is **3840x2160**, internal output **2048x2048**.
Manifest startup dimensions of 1920x1080 precede the borderless transition.
No script PNG or RAM-dump commands occur. Full config-file hashes and inherited
environment were not captured, so those are not asserted identical.

The final executable is
`9E622D7EB0B72E9BAEA16F67B1C156813A94E5DA7FDCC1B48DEF24F55BB734DC`.
It exited 0 with script quit after **66.299 s**, and the after-run hash matches.
Whole-run script time is not a unique-world FPS measurement.

This comparison uses one run per build and short steady gameplay samples.
The final candidate includes other integrated fixes; this is not an isolated
single-patch A/B. Guest vblank rate is not gameplay FPS; present calls are not
confirmed scanouts; camera/input latency and the separate functional/toggle
checks are not measured here.

## Evidence and integration

[Machine-readable summary](pacing-benchmark.json) records full build,
script/input and source-log hashes, the selected windows, aggregate counters,
recipe comparisons and limitations. Raw local evidence remains ignored under:

- `build/t/followup-pacing-srnr`
- `build/t/followup-handoff-srnr`
- `build/t/followup-final-srnr-perf`

This report reads existing evidence only. Production code remains frozen at
main's integrated runtime `e20f1a7`; no game/GPU run or artifact deployment was
performed by this reporting step. Cherry-pick this documentation commit into
the outer source repository; no nested runtime commit is needed.


## 7F alpha-page release candidate: clean performance follow-up

Use only `build/t/followup-release-alpha-perf-clean`. The earlier
`followup-release-alpha-perf` run is excluded: setting `DRAKAN_DLSS_LOG=0`
enabled presence-based verbose logging. The clean helper unsets it; the
manifest omits that variable and stderr contains **zero** `[dlss-sr]` and
`[dlss-scene]` entries.

Exact executable SHA-256:
`7F80774EEBE4634BFEFD7DF60B5E674C9E05BD6EAAC68A1F1E2E2A0EC292F387`.
The after-run hash matches. The run exited 0 with script quit in **66.289 s**.
Script/input hashes match the 9E run. Recipe: SR Quality + NR, GS scale 4,
0.4 sharpening per the coordinator's fixture, 3840x2160 borderless client,
2048x2048 internal output, no scripted PNG/RAM captures. Pacing/scene summaries
remain instrumentation; this is not a completely uninstrumented run.

The same final-three-window selection spans **6.023 s**:

| Metric | Clean 7F result |
| --- | ---: |
| World-rate windows | 29.89 / 29.88 / 31.39 per second |
| Weighted completed worlds/s | 30.387 |
| Unique-world and total present calls/s | 30.387 |
| Guest GS vblanks/s | 60.603 |
| World interval | 32.909 ms |
| Presentation time/call | 0.773 ms |
| Worker handoff wait/call | 0.783 ms |
| Duplicate handoffs suppressed | 183 |

Late counters: `pendingBegin=366`, `pendingContentChanged=0`,
`noCompletedWorld=0`, `clear_phase=0`, `clear_unknown=0`. Display/draw/transfer
change observations remain handed off (183 each). There are no handoff
timeouts, slow-host warnings or screenshot indicators. Lock/drain/fairness
waits round to zero. Wait totals overlap and cannot be summed as frame time.

This is consistent with the 9E candidate's **29.893 worlds/s** and retains the
previously demonstrated approximately 30-versus-20 result. The small difference
is **not evidence of another performance gain**. The final window also reports
62.79 GS vblanks/s, consistent with short-window catch-up/variation; no new
31-world/s sustained target is claimed. One run and a six-second sample do not
establish whole-game timing, scanout or input latency. Orb spill remains an
unresolved visual issue outside this performance receipt.

Private evidence receipt (ignored, not published):
`build/pacing-followup/7f-clean-performance-receipt.json`, SHA-256
`2430595dfc4eda02837ab799d86352277520b0247b85ad280f0a1137299dd242`.
It records the exact selected windows, counters, completion/build identity and
hashes for the manifest, stderr, input log and script. The original
`docs/journal/2026-10-17/pacing-benchmark.json` is unchanged. This follow-up only read existing
logs; it did not launch a GPU process or modify production code.

## 77AF orb release candidate: clean performance follow-up

Read-only evidence: `build/t/followup-release-orb-perf-clean`, source fix
`76d8e184d15a635cc37826f7f8cc13420cb49fa2` reported by the coordinator.
Exact executable SHA-256:
`77AF378BC98F14757262C902B995C115D37B2DB025DC78CBD565D5A9BE52298E`.
Exit 0, script quit, **72.356 s** elapsed, matching after-run executable hash.
Script and input-configuration hashes match clean 7F. Both use SR Quality + NR,
GS scale 4, a 3840x2160 borderless client and 2048x2048 internal output.
Sharpening 0.4 is from the coordinator's fixture; full config and inherited
environment equality is not independently established. There are no scripted
PNG/RAM captures. `DRAKAN_DLSS_LOG` is absent from the manifest, with zero
`[dlss-sr]` and `[dlss-scene]` entries.

The final three positive gameplay windows span **6.012 s**:

| Metric | Clean 77AF | Clean 7F |
| --- | ---: | ---: |
| World-rate windows/s | 27.98 / 28.49 / 28.38 | 29.89 / 29.88 / 31.39 |
| Weighted completed worlds/s | 28.283 | 30.387 |
| Unique-world and total present calls/s | 28.450 | 30.387 |
| Guest GS vblanks/s | 56.890 | 60.603 |
| World interval | 35.356 ms | 32.909 ms |
| Presentation time/call | 0.876 ms | 0.773 ms |
| Worker handoff wait/call | 0.894 ms | 0.783 ms |
| Duplicate handoffs suppressed | 171 | 183 |

The measured world rate is **6.9% lower** than clean 7F, with **2.447 ms**
longer per world. This is a short, single-run comparison; 7F includes a final
catch-up window. It does not isolate the orb correction's cost or establish
a whole-game regression magnitude. World updates and world presentations can
straddle diagnostic boundaries, explaining their small rate difference here.
Host ticks average 72.857/s and must not be labeled gameplay FPS.

Duplicate suppression remains effective: `pendingBegin=342`,
`pendingContentChanged=0`, `noCompletedWorld=0`, `clear_phase=0`,
`clear_unknown=0`; scene/draw/transfer/display changes each force 171 handoffs.
No handoff timeouts, slow-host warnings or screenshot indicators occur.
Lock/drain/fairness waits round to zero. CPU presentation rises by 0.103 ms/call
and handoff wait by 0.111 ms/call; these overlap and are not additive GPU costs.

The source correction rejects only nonzero horizontal RT subviews whose
requested width crosses the parent's right edge, routing those textures through
VRAM decoding. Zero-origin and row-contained views retain their direct path.
That is the fallback's verified scope; this run has no per-path hit/readback/GPU
timings, so its share of the observed difference cannot be assigned. The
coordinator separately reports native/SRNR visual and functional checks passing;
this timing receipt does not repeat those checks or infer scanout/input latency.

Private evidence receipt (ignored, not published):
`build/pacing-followup/77af-clean-performance-receipt.json`, SHA-256
`66afb2f30040b372644e76b10430480ee7121b15225c48c1badc9321c50a37bc`.
It records exact windows, counters, manifest/completion and source-log hashes.
Earlier sections and the original benchmark JSON remain unchanged. This append
does not launch a GPU process, modify production code or deploy artifacts.
