# Ray-query work pruning

Continued generic performance work after native alpha coverage. Three exact shortcuts apply in both DXR and the
portable WebGPU ray renderer:

- After traversal accepts a material's alpha test, a nonblended hit has opacity one. Avoid evaluating its material
  alpha a second time. Blended opacity and the independent instance fade still run normally, including EQUAL-zero
  cutout materials. Candidate validation still evaluates the original test.
- Skip ambient-occlusion queries only when every sanitized ambient-light channel is exactly zero. AO multiplies that
  term alone. A single positive channel retains AO; direct lighting is unchanged.
- Reconstructed opaque frame geometry has `shadowGeometry=false`, and receiver, shadow and AO queries already reject
  every such triangle. When an entire frame batch has this property, exclude its instance from those queries. Keep it
  in water-reflection queries, which can use reconstructed geometry. Mixed batches retain the original per-hit
  filtering. Static/native geometry retains its original eligibility and direct-shadow ownership.

No sample counts, render resolution, visual settings or update frequency are reduced. Comparison switches are
`GSR_RAY_ACCEPTED_ALPHA=0`, `GSR_RAY_ZERO_AMBIENT=0` and `GSR_RAY_PRUNE_REJECTED=0`. Delayed GPU timing additionally
reports `rejected_frame_triangles` so an active mask can be distinguished from an inactive shortcut.

## Regression evidence

The DXR and WebGPU ray suites passed after adding zero-ambient and single-positive-channel cases. The CPU scene
geometry suite covers all-rejected, mixed and water-only eligibility. An added cold-start water-reflection case marks
the complete opaque frame batch ineligible for receiver/shadow queries and verifies that its reflection is unchanged.
DXR snapshot streams with batch pruning disabled/enabled have identical SHA-256
`d31630a69bfeed4a9315d023dfe95d4ac4ba1bc2f2d1e661c7ac1b3c6a1b1071`.
Raw tests, snapshots and benchmark receipts are ignored under `build/perf-accepted-alpha/`.

Final validation rebuilt and passed all nine focused CTest suites in 199.40 seconds: `ray_trace`,
`synthetic_webgpu`, `world_readback`, `scene_geometry`, `partial_upload`, `ray_water`, `rt_upload`, `swizzle` and
`synthetic`. There were no skips or failures. With all three new switches off versus all three on, the final DXR
snapshot streams are byte-identical (7,372,800 bytes each, the same `d31630...b1071` SHA-256 above). Formatting checks,
`git diff --check` and the repository source audit passed. The deployed game executable remained the all-map-tested
`30bc5fdb...d4ee3c`; subsequent changes were regression tests and documentation only.

## Measurements

The accepted-hit shortcut alone was measured on deployed executable
`1166a2da54a5fff7a427686defa4e866e089ea4e9175d844057592df69cd321c`, using same-executable off/on pairs.
Frozen graphics INI SHA-256: `977d7059b401db16583dae9dcb9f4140283e647cdf28e32e696fb62a001f5e04`.
Native-options seed: `b802b6e5b9397d1f820ebebd7fb5c72608aa80bd25b259dc56b30c8b3be22a85`.
Inputs remain outside harness output directories because the harness clears those directories at startup.

| Map | FPS, off → on | Median ray GPU ms, off → on |
| --- | --- | --- |
| Isles | 51.76 → 51.27 | 6.375 → 6.206 |
| Catacombs | 62.22 → 62.68 | 4.083 → 4.038 |
| Northern Tundra | 74.38 → 76.24 | 4.165 → 4.113 |
| Shadowmire | 93.31 → 93.91 | 4.871 → 4.790 |

Receipt: `bench-062921.json` and regenerated `bench-062921-gpu.json`. These modest GPU-time savings do not establish
an Isles FPS gain. None of these fixed-view samples had zero ambient, so the zero-ambient shortcut contributes no
measured gain in this table. Lower GPU milliseconds means less GPU time, not lower FPS.

The user explicitly retained permission for their other game session to run. Benchmark processes record concurrent
game PIDs rather than closing them. All measurements are shared-machine diagnostic views, not isolated campaign-wide
performance guarantees. The redirected Isles fixture keeps both Rynn and Arokh alive using existing playtest health
aids while enemies, rendering and audio remain active.

Batch-pruning comparisons use executable
`30bc5fdb6a950877e4d296c4d92111dbdc6c9f1d32283e217b625adc57d4ee3c` with the accepted-hit shortcut enabled in both arms.
The initial receipt is `bench-064610.json`; reverse-order Isles/Tundra repeats are in `bench-065425.json`.

| Map | FPS, pruning off → on | p99 frame ms, off → on | Median ray GPU ms, off → on |
| --- | --- | --- | --- |
| Isles, first pair | 51.77 → 57.82 | 32.29 → 27.41 | 6.236 → 4.384 |
| Isles, reverse-order pair | 52.11 → 57.99 | 32.06 → 27.45 | 6.212 → 4.404 |
| Catacombs | 63.15 → 65.15 | 22.69 → 21.41 | 4.034 → 3.191 |
| Shadowmire | 93.94 → 99.47 | 18.43 → 17.01 | 4.830 → 4.220 |

The filtering is active: an Isles sample excludes about 26,000 reconstructed opaque triangles from validated queries
while retaining them for water reflections. GPU time is still spent building this geometry and it remains in the scene;
this change saves query traversal, not geometry capture or acceleration-structure construction.

### Tundra qualification

The initial Tundra pair measured 89.10 → 80.22 FPS, and the reverse-order pair 75.90 → 96.95 FPS. Screenshot review
found a missing/stretched Rynn in the first enabled run and the repeated disabled run. Other runs render Rynn correctly.
This reproduces with the new filtering disabled, but its underlying cause is not established here. These pairs are
not valid evidence of a Tundra FPS improvement or regression. Median ray GPU time fell in both pairs (4.334 → 3.142 ms
and 4.105 → 3.158 ms), but scene differences still limit that comparison. Keep this redirected-start rendering issue
open rather than claiming a clean visual sweep or consistent frame times everywhere.

A further `bench-071100.json` pair briefly walks and turns the camera in both directions, then settles until five
seconds before the 20-second stationary sample. Both arms still show the malformed/missing Rynn, so this fixture did
not resolve the initialization/rendering issue and is also excluded from normal-game FPS claims.

## All-map sweep

`sweep-065822.json` records all 13 playable PAL maps, all 39 phase measurements and exit code zero for every run.
The executable hash remained `30bc5fdb...d4ee3c` before and after every run. Frozen settings are the same as above.
The short fixture samples eight seconds standing, five seconds turned and two seconds moving, with warm-up/edge
exclusions in the frame-gap parser. Isles and Valley skip their entry movie. Crystal and Isles use existing health
aids; Isles also protects Arokh. Every run launches from `dist/DrakanPC` through the normal harness.

| Map | Still FPS | Still p99 ms | Turned FPS | Moving FPS |
| --- | --- | --- | --- | --- |
| Surdana | 137.4 | 7.56 | 162.8 | 151.2 |
| Kragmor | 179.6 | 7.08 | 166.2 | 138.0 |
| Catacombs | 64.5 | 21.34 | 81.3 | 77.0 |
| City of the Damned | 191.9 | 5.42 | 187.2 | 207.7 |
| Lair | 195.5 | 5.29 | 209.0 | 207.8 |
| Shadowmire | 99.1 | 16.90 | 100.4 | 105.7 |
| Isles | 58.5 | 26.73 | 60.0 | 103.2 |
| Crystal | 96.0 | 16.57 | 102.2 | 77.0 |
| Ravenshold | 177.4 | 5.92 | 176.8 | 121.9 |
| Northern Tundra (qualified above) | 75.4 | 20.04 | 148.6 | 154.4 |
| Shiverbane | 116.9 | 13.88 | 116.2 | 119.0 |
| Valley | 86.5 | 18.73 | 84.9 | 89.8 |
| Stratos | 150.6 | 6.86 | 149.7 | 139.9 |

Screenshot contact sheets `sweep-first.jpg`, `sweep-middle.jpg` and `sweep-last.jpg` were inspected. The sweep confirms
loading, sampled rendering and scripted movement rather than full campaign correctness. Tundra's initial Rynn anomaly
also occurs here; the later movement image shows Rynn correctly. These cross-map values are diagnostic samples, not
off/on comparisons. The zero-ambient shortcut was inactive in every stationary sweep sample, so none of these numbers
establish its performance benefit. Remaining readback waits and long-frame tails still need further work.
