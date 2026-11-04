# Native alpha coverage performance

Continued the shared renderer performance work, focusing on the slow Isles,
Catacombs, Tundra and Shadowmire views. Existing effects and resolution stay
enabled; there are no map-specific renderer branches.

## Exact native cutout coverage

Native material packing enables GREATER-0 alpha testing for several alpha-capable
texture classes, including textures without a transparent texel. Native model
vertices supply alpha 128 and MODULATE, so a complete bilinear source whose alpha
is everywhere positive proves that every cutout sample passes. Nonblended
materials return opacity one after that test. Sampling their texture alpha for
each ray candidate is redundant.

`ModelAlphaCoverage` checks all 128x128 source texels. Its per-backend cache owns
immutable source pointers and verifies identity and flags before reuse. A new
texture object is checked even if it reuses an identity; identity-zero ad-hoc
inputs are rescanned. Missing/incomplete textures receive no proof. A single
zero-alpha texel prevents the optimization. Translucent materials retain their
full alpha evaluation.

The proof occupies a separate private material flag. Original alpha-test and
blend flags remain unchanged, so the shortcut does not alter foliage classes,
reflection eligibility or bilateral material boundaries. Both DXR and WebGPU
check the proof in their alpha evaluation. Replacements changing coverage also
invalidate cached model attributes, independently of geometry or material-flag
changes. `GSR_RAY_ALPHA_COVERAGE=0` forces the old sampling path within the same
executable; coverage preparation still runs for that comparison.

## Initial correctness checks

- CPU scene/packing checks passed, including alpha 1, a single hole, replacement
  with a reused identity, translucent exclusion, mutable identity-zero input,
  incomplete data and clearing the model list.
- DXR tests acquire/revoke coverage while leaving cutout selectors unchanged;
  the WebGPU regression checks the corresponding shadow change. Both ray suites
  passed.
- DXR shortcut-off/on capture streams are byte-identical, 6,881,280 bytes each,
  SHA-256 `691fb5ae19730eb3dcceed12102bcafb09996a5856c2c8bb7f77410f2fb3d0e8`.
  Existing cutout, translucent, reflected-detail, transport and material-change
  checks remain in the suite. Receipts: `build/perf-alpha-coverage/`.
- Standard incremental build deployed executable SHA-256
  `1226e00f057a5ed6602fef4f715db2c94506139bb87f755437dfceafc08cc2d4`.

## Benchmark input correction

The run harness clears its output directory before reading a recipe. The first
new Isles attempt put a custom recipe inside that directory and failed before
launch; it is excluded. The same review found that earlier scratch benchmarks
also placed a native-options seed there. Its recorded pre-launch hash did not
prove that the game loaded that copy after cleanup. The external renderer INI
was unaffected, but those earlier native-settings consistency claims were too
strong. Earlier raw observations remain diagnostic samples, not controlled
native-options comparisons.

Corrected runs put each private native-options copy and recipe outside the
harness output directory. They start from the prior frozen renderer INI
(`977d7059b401db16583dae9dcb9f4140283e647cdf28e32e696fb62a001f5e04`)
and native-options seed
(`b802b6e5b9397d1f820ebebd7fb5c72608aa80bd25b259dc56b30c8b3be22a85`).
Each run records executable hashes before/after, its environment arguments and
concurrent Drakan PIDs. Other sessions are left alone. The native options include
gameplay controls and difficulty, so preserving this input matters even though
graphics quality comes from the external renderer configuration.

## Corrected coverage comparison

Receipt `build/perf-alpha-coverage/bench-054331-gpu.json` contains four
same-executable pairs on the coverage-only build. The previous ALWAYS/NEVER
shortcut and readback fixes stay enabled in both settings. Every pair
completed cleanly with matching executable hashes. Native-options inputs
remain outside the cleaned directory and still match their seed hash after
the runs. The failed `bench-054215.json` setup attempts are excluded.

| Map | Coverage off/on FPS | Off/on p99 ms | Off/on ray GPU median ms |
| --- | ---: | ---: | ---: |
| Isles | 50.74 / 51.37 | 33.63 / 33.37 | 6.707 / 6.558 |
| Catacombs | 59.57 / 62.18 | 25.81 / 22.64 | 4.781 / 3.973 |
| Northern Tundra | 73.50 / 74.91 | 21.90 / 21.61 | 4.288 / 4.146 |
| Shadowmire | 90.08 / 92.84 | 19.30 / 18.48 | 5.236 / 4.873 |

These are observed paired samples, not guarantees of uniform FPS. Live
actors, scene time and other workloads still vary. The largest measured ray
saving is Catacombs (about 17%); Isles saves about 2%, Tundra 3%, Shadowmire
7%. End-to-end FPS changes are smaller. Some maximum gaps increased, so this
does not establish that all frame-time spikes are fixed. Isles uses the
existing Rynn/Arokh survival fixture; its periodic health probe also logs
diagnostic data and can affect timing. Enemies and rendering remain active.

## Exact CPU upload specialization

The common render scales now use constant-bound horizontal replication. At
2x/4x/8x, vertical rows are copied in nonoverlapping doubling runs; other
scales retain the generic implementation. Scale one copies the complete
contiguous image. This keeps every color/alpha bit and upload byte count
unchanged and uses portable C++, without architecture-specific intrinsics.
`GSR_RT_UPLOAD_SPECIALIZE=0` selects the earlier variable-scale loop.

Three warmed CPU benchmark passes, including reversed algorithm order, use
20,000 calls per case and observable checksums. Median time per call for
eight native rows at scale four:

| Native width | Generic ns | Specialized ns |
| --- | ---: | ---: |
| 16 | 145.0 | 109.2 |
| 128 | 1043.6 | 773.0 |
| 512 | 4308.1 | 3046.7 |

All 14,524,800 exact pixel comparisons and 780 whole-VRAM mask comparisons
passed. The warmed scale-four operation saves roughly 25–30% in these cases;
this is not a game-FPS percentage. Same-executable upload-only comparisons
on the final build (`bench-055147.json`) measured Isles 50.90 / 51.55 FPS,
Catacombs 62.72 / 62.76 FPS. Catacombs shows no meaningful FPS gain in this
pair, and some p99 gaps are higher. The kernel improvement reduces CPU work
but does not remove the genuine GPU wait in these views.

## Final build and campaign regression sweep

The standard incremental builder deployed `87b77cd8f4b149579f9cf80a19eab2ed54deb1c277b60141d9d159d3cb9fe6a6`.
All 13 playable maps completed both camera samples and the movement input
on this executable, with matching hashes before/after each run. Capture
contact sheets were inspected and show live gameplay, including Isles.
The native-options files survived launch outside the harness output folder
and still match their recorded seed hashes. Raw receipt:
`build/perf-alpha-coverage/sweep-055543.json`; checked merge:
`final-summary.json`.

| Map | Initial FPS | Turned FPS | Initial / turned p99 ms |
| --- | ---: | ---: | ---: |
| surdana | 106.3 | 122.8 | 9.66 / 8.39 |
| kragmor | 170.9 | 165.4 | 7.43 / 10.73 |
| catacombs | 63.2 | 77.8 | 22.53 / 19.22 |
| city | 162.6 | 158.2 | 6.46 / 6.63 |
| lair | 181.6 | 197.3 | 5.67 / 5.26 |
| shadowmire | 89.7 | 96.2 | 19.35 / 18.00 |
| isles | 51.4 | 51.3 | 32.53 / 33.51 |
| crystal | 91.8 | 92.4 | 17.32 / 18.27 |
| ravenshold | 167.5 | 167.2 | 6.30 / 6.31 |
| tundra | 71.7 | 132.8 | 21.65 / 7.94 |
| shiverbane | 107.3 | 106.9 | 15.30 / 15.43 |
| valley | 82.7 | 81.1 | 19.80 / 20.16 |
| stratos | 136.8 | 134.8 | 7.47 / 7.63 |

This is a short regression sweep, not a full playthrough or a controlled
comparison with the previous sweep. Initial and turned samples last about
eight/five seconds before trimming marker/capture margins; movement input
lasts two seconds. Isles/Valley skip their entry cinematic. Isles and Crystal
use the existing Rynn survival aid; Isles also uses the Arokh health probe
and poke fixture. These test-only variables apply only to the isolated run.
Enemies, effects and the rendering workload remain active. Transition to
Desert is absent from the supplied disc; intro/outro are not playable maps.

Final CTest passed all eight selected suites in 219.48 seconds: D3D11/WebGPU synthetic scenes,
both ray suites, scene geometry/material packing, partial uploads, upload
masks/replication and swizzles. The focused scale-four upload test passed 71
checks. The CPU suite passed 14,524,800 exact pixel checks plus 780 whole-VRAM
comparisons. Formatting, diff whitespace and the tracked-source audit passed.

The slowest selected stationary view remains Isles at roughly 51 FPS.
Genuine GPU readback waits and ray shading still limit this scene. These
changes reduce shared CPU/GPU work without changing visual settings; they
do not establish that every FPS drop or frame-time spike is eliminated.

## Newer concurrent deployment

A concurrent character-artifact task deployed a newer executable after the
13-map sweep, while the final unit suites ran. It includes these performance
changes plus native ray-model lifetime fixes; that build was preserved.
Current executable SHA-256: `c83c93cb518e9dc1963a41131a83dc80aa41bd41e25fdcee731482bc04eddb93`.
Its build/deploy receipt is `build/rynn-transition-deploy.log`.
A further Isles smoke run completed both views and movement on this exact
hash, with captures inspected: `build/perf-alpha-coverage/sweep-061423.json`.
It measured 51.4 / 55.5 FPS in its two views.
The full 13-map table above belongs to the preceding measured executable,
not to this later concurrent deployment. The follow-up is a smoke check,
not a controlled before/after speedup comparison.
