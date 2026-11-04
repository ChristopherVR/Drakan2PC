# Generic map performance fixes

Follow-up to the all-map sweep and the request to improve the slow maps through
shared renderer changes. No map-name checks, game addresses, per-map effect
disabling or player-setting changes are part of these fixes.

## Readback investigation

Catacombs transfer/read traces exposed small indexed mip uploads overlapping a
dirty CT32 render target. Reading that entire target before an 8x8 upload was
unnecessary. A masked compute update now changes only actually uploaded bits,
preserving every other bit of each high-resolution GPU pixel. Actual transfer
progress determines the mask, including GIF fragmentation and abandoned prefixes.
The bounded path handles at most 4096 pixels and falls back before accepting an
unsupported target or failed resource allocation.

Block-level reader checks were insufficient: a partially uploaded block can
contain both current CPU data and GPU-owned data. Exact texture-mip and
local-to-host rectangles now accompany read requests. A skip requires every
requested bit intersecting dirty target rows to be known current. Unknown or
unwritten bits retain the real readback. WebGPU retains its existing fallback.

The next trace exposed a palette readback: CPU refreshes of disjoint target page
rows discarded the proof that earlier uploaded palette blocks were current.
CT32 row refreshes now establish equality for their own blocks and retain valid
knowledge in other rows. GPU writes still invalidate that knowledge through the
target write version.

Small scratch targets with recent genuine reads can stage their output directly
after a draw. Reuse requires an exact write-version, dirty-row and scale match.
World/effects targets are excluded because their additional writes do not all
pass through the ordinary draw counter. This preserves the synchronous result;
it does not substitute previous-frame query data.

## First same-executable comparison

Executable `56b895b45c6cc065779747b831f98d9f0336e4299a38c7c6bbffb24f6737943c`;
receipt `build/perf-readback-fix/bench-232358.json`. Both settings use installed
4x scale, DLSS Quality, ray effects, audio, uncapped presentation and vsync off.
The user explicitly requested retaining their concurrent game session. Other
threads also launch games; process samples accompany every run. These are paired
observations on a shared machine, not isolated speedup measurements.

| Map | Fixes off/on FPS | Off/on p99 ms | Off/on readbacks per present |
| --- | ---: | ---: | ---: |
| Catacombs | 56.76 / 57.26 | 27.46 / 25.60 | 2.001 / 0.999 |
| Northern Tundra | 73.29 / 72.32 | 22.32 / 22.24 | 1.775 / 1.013 |
| Shiverbane | 101.15 / 108.48 | 17.36 / 15.54 | 1.116 / 1.000 |
| Surdana | 101.58 / 100.64 | 11.06 / 10.54 | no sampled readback stage |

Shiverbane's largest present gap changed from 57.886 to 16.130 ms. This single
pair does not prove repeatable hitch removal. Removing a readback does not remove
the GPU work preceding the remaining genuine readback: Catacombs still spends
about 8.2 ms per present waiting there. Do not claim this change alone fixes its
low FPS or guarantees gains everywhere. Earlier partial-upload-only comparisons
did not solve the remaining palette reader; their superseded receipts remain
under `build/perf-readback-fix/`.

## Validation

- Portable upload masks: 780 whole-VRAM comparisons across 13 formats, fragmented
  prefixes, aborts and coordinate/VRAM wrapping; 14,524,800 existing exact pixel
  expansion checks passed.
- Focused D3D11 scale-4 regression: 67 checks passed. Covers eight upload formats,
  full/abandoned transfers, exact reads versus reads of untouched bits, GPU
  screenshots, full raw VRAM including alpha, latest-draw staging validity, and
  retaining palette knowledge across a disjoint CPU-row refresh.
- Both D3D11 and WebGPU synthetic targets compile with the extended private
  backend API. Further final checks and map receipts follow below.

## Constant alpha-test work

Both ray shaders now bypass alpha interpolation and bilinear texture sampling
when the answer is constant: NEVER rejects, while opaque ALWAYS accepts. All
other alpha tests and all translucent alpha evaluation retain their original
path. `GSR_RAY_ALPHA_FAST_PATH=0` provides a same-executable comparison.

The D3D12 tracing-focused BLAS/TLAS build-policy experiment was discarded:
Catacombs total GPU medians were 6.419 / 6.404 ms and Tundra 5.523 / 5.588 ms.
Its additional build cost cancelled the small traversal saving. No new build
policy or policy toggle remains in source. Receipts: `bench-233303-gpu.json`.

The retained alpha shortcut was measured separately, with readback fixes on in
both cases. Executable `323224f9f641b2fcd849283ef20605f4cf5b29c0ce2719b6d7e407a8dbb2dd00`;
receipt `build/perf-readback-fix/bench-233841-gpu.json`. The native-options seed
was also frozen and copied separately for each run (SHA-256
`b802b6e5b9397d1f820ebebd7fb5c72608aa80bd25b259dc56b30c8b3be22a85`).

| Map | Shortcut off/on FPS | Off/on p99 ms | Off/on ray GPU median ms |
| --- | ---: | ---: | ---: |
| Catacombs | 56.10 / 56.79 | 26.24 / 25.58 | 5.581 / 5.306 |
| Northern Tundra | 69.65 / 72.07 | 23.42 / 22.79 | 5.006 / 4.656 |

GPU medians use timestamp samples between the stationary screenshot markers
(1115 / 1126 Catacombs samples and 1379 / 1429 Tundra samples). These observed
reductions are about 5% and 7% of ray-pass GPU time, respectively, not equivalent
game-FPS percentages. Catacombs composition dropped from 4.489 to 3.182 ms, but
its transport interval increased from 0.593 to 1.627 ms; report the total rather
than claiming the composition saving for the whole pass. Concurrent workloads
remain a limitation, and these numbers do not establish uniform frame rates.

D3D11 ray regression runs with the shortcut off/on produced byte-identical
6,815,744-byte capture streams, SHA-256
`86d9867f2a776b4faf9357c8bb145b1bbb9edd86bed65f5e93e74b6249eb0d2c`.
The suite includes alpha cutouts, translucent layers, material changes, sparse
dynamic uploads, colored transport and disabled-effect preservation. The WebGPU
ray suite also passed. Focused scale-4 upload checks passed again with
`GSR_RB_VERIFY=1` and no mismatch reports.

## Final map and renderer validation

Finished after midnight on 2026-11-05 Brisbane time. All selected runs used
executable `323224f9f641b2fcd849283ef20605f4cf5b29c0ce2719b6d7e407a8dbb2dd00`, with matching hashes before/after every run.
Receipts: `build/perf-readback-fix/sweep-234249.json` and the selected Isles
retry `sweep-235913.json`; merged, asserted coverage in `final-summary.json`.
All 13 selected runs quit through the script after both camera samples and
movement. Captures were inspected. Transition to Desert remains absent from
the supplied disc; intro/outro sequences are not playable campaign maps.

| Map | Initial FPS | Turned FPS | Initial / turned p99 ms |
| --- | ---: | ---: | ---: |
| surdana | 100.4 | 114.5 | 10.53 / 9.24 |
| kragmor | 166.5 | 165.5 | 7.59 / 7.21 |
| catacombs | 59.3 | 72.2 | 25.16 / 20.79 |
| city | 153.0 | 148.2 | 6.96 / 7.13 |
| lair | 163.8 | 177.5 | 6.75 / 6.28 |
| shadowmire | 76.0 | 75.7 | 24.10 / 25.13 |
| isles | 49.9 | 51.5 | 32.97 / 31.69 |
| crystal | 71.4 | 73.7 | 22.72 / 23.42 |
| ravenshold | 139.6 | 144.8 | 9.38 / 8.02 |
| tundra | 68.2 | 122.3 | 22.86 / 8.95 |
| shiverbane | 103.8 | 103.9 | 16.63 / 16.02 |
| valley | 73.9 | 66.2 | 22.47 / 25.21 |
| stratos | 120.2 | 128.1 | 9.38 / 8.42 |

These short regression samples use about eight seconds for the first view,
five for the turned view, and a two-second movement input. They are not
full-map playthroughs or isolated before/after speedups. Camera direction,
active actors and concurrent sessions affect the observed workload. User and
other-thread game processes were left running and recorded in the receipts.
Isles and Valley skip their entry cinematic. Crystal uses the existing
`DRAKAN_PLAYTEST_GOD=1` survival fixture.

Image review rejected the first final Isles later samples: a death prompt was
visible despite successful frame markers. The selected retry uses the same
Rynn health aid plus existing `DRAKAN_DRAGON_HP_PROBE=1` /
`DRAKAN_DRAGON_HP_POKE=6b4:10000` in that isolated test process to keep Arokh
alive under the entrance attack. Enemies and effects remain active. The
retry captures show live gameplay through the movement end. This is a
performance/survival fixture, not an ordinary difficulty or campaign-state
validation; it does not establish that the earlier longer redirected-start
return-to-title problem is fixed.

Final CTest: seven suites passed, zero failed, 184.54 seconds: D3D11 and
WebGPU synthetic rendering, both ray backends, partial uploads, upload masks
and swizzles. The focused upload suite now has 71 checks, including readback
during an active CT24 or T4 transfer before the remaining stream arrives.
Full D3D11 synthetic rendering passed 346 checks; WebGPU passed 312.
The live Catacombs `GSR_RB_VERIFY=1` run completed cleanly after reporting
193,551,488 pixel comparisons across 49,000 skip checks and zero mismatches.
Logs: `live-verify-runner.log`, `final-test-build.log`, `final-ctest.log`.
Touched C++ formatting and diff whitespace checks passed; the repository
guard reports 2,297 approved tracked files and zero prohibited files.

The remaining worst selected view is Isles at roughly 50 FPS. Its first-view
ray GPU median is 7.12 ms; the first, rejected survival run measured 8.94 ms
before the camera/state differences. Genuine readback waits and ray shading
remain substantial. The generic fixes reduce redundant work, but this is
not evidence that every low-FPS view or every frame-time spike is resolved.

## Follow-up measurement correction (2026-11-05)

The [continued investigation](../2026-11-05/native-alpha-coverage-performance.md)
found that the scratch harness clears its output directory before launch. Earlier
native-options copies placed there were removed; their seed hashes alone did not
prove runtime use. The external renderer INI remained outside that directory.
Treat the native-settings consistency claims above as unverified; corrected
follow-up comparisons keep every input outside the cleaned directory. This does
not change the recorded raw timings or pixel-regression results.
