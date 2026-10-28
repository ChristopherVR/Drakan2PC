# Combat, texture coherence and upload performance — 18 October 2026

## Changes

Framebuffer uploads now expand each native row once and copy its repeated
scanlines. This preserves the existing nearest-neighbor pixels, including alpha,
while removing per-output-pixel integer division. A CPU regression compares
14,524,800 pixels against the previous formula at scales 1–8, with odd widths,
single rows, empty dimensions and destination guard words.

Decoded texture cache checks now flush overlapping GPU writes before consulting
the RAM write clock. GPU draws do not themselves advance that clock, so testing
the clock first could reuse pre-draw texture pixels. This is separate from the
previous shifted render-target row-addressing fix; that fix remains enabled.

The combat harness can explicitly enable the audio device and capture its mixed
output. Audio remains disabled by default for isolated performance comparisons.
Its evidence records the selected audio mode and checks private save copies.

## Performance method

Both profiling runs use the existing private early-game save fixture, the same
input sequence and configuration, scale 4, SR Quality plus neural rendering,
and `GSR_PROFILE=2`. They disable screenshots, RAM dumps and verbose DLSS logs.
Profile buckets are nested: inclusive durations must not be added together.
The final gameplay pacing windows measure completed world submissions, not
confirmed display scanouts. Short runs do not establish whole-game performance.

Baseline executable:
`77AF378BC98F14757262C902B995C115D37B2DB025DC78CBD565D5A9BE52298E`.
Baseline completed normally in 68.277 seconds; the final three pacing windows
were 27.95 / 28.43 / 27.99 world updates per second. In the final three profiler
windows, `syncRT` accounted for 1,081.69 ms of CPU time.

The combined build is
`54F082B55C3A768391EC5B39DB48033F2AA77DEA6B51CDFBBE1E81A61D92D557`,
from production commit `83b2562`. Its corresponding upload CPU time was
181.43 ms (83.2% lower), with 1,782.5 MiB uploaded versus 1,574 MiB before.
The other profile buckets cover different amounts of completed work, so this
is not an isolated microbenchmark. The decoded-cache fix is included too.

Longer comparisons disable the profiler and extend the same gameplay sequence.
The final ten pacing windows cover approximately 20 seconds per build:

| Measure | Baseline 77AF | Combined 54F082 |
|---|---:|---:|
| Completed world submissions/s | 28.633 | 29.976 |
| Measured window duration | 20.081 s | 20.115 s |
| Full script duration | 132.556 s | 120.459 s |

That is a **4.69% sustained cadence gain** in this scene. Initial catch-up
windows exceeded 30/s and are deliberately excluded. The settled result is
approximately the game's 30-world/s cadence, not a 60-FPS claim. Both runs
quit normally, retained the executable hash and preserved original, fixture
and private saves. Recipe/input hashes and selected settings match; there is
one run per build. [Machine-readable receipt](data/performance-20261018.json).

The texture coherence regression passed 18 checks after three failures before
the fix. Full renderer suites passed **118/118 at scale 1 and scale 4**.
[Details and source identities](texture-coherence.md).

## Validation status

The exact combined executable passed **23/23** category, Equipment clicking,
movement, bow-ammo and Quick Inventory checks in each final run:

- Native, audio enabled: normal exit in 94.501 seconds.
- SR Quality + NR, audio enabled: normal exit in 102.335 seconds;
  **1,794 successful SR and 1,692 successful NR evaluations**, with four
  scripted NR off/on changes.
- Separate combined SR/NR melee/audio encounter: enemy HP **40→18**, five
  left-button press/release pairs and right-button release established;
  six named attack events have nonzero following mixed-audio windows.
  This run records 420 successful SR and 420 successful NR evaluations.

Original, fixture and private saves remained unchanged; each run retained the
expected executable hash and exited via its script. Functional run times include
diagnostics/captures and are not performance measurements.
[Combat/audio evidence and limits](combat-audio.md).

Native and SR/NR inventory screenshots retain the corrected orb texture, with
visible item icons and character preview. Thin white HUD-edge lines remain.
The final SR/NR view also shows flat gray shapes beneath the canopy in the sky
region; there is no proved cause or fix for those pixels. Earlier combat views
show occasional detached preview geometry/displaced mana outline. Smoke and
the original gate-guard shadow issue are not established as fixed.

Successful timed parry, full combos, Arokh combat and exact subjective Order of
the Flame parity remain unverified. One late attack-audio window is silent while
Rynn has reached zero HP; its cause/death recovery remains unverified. Existing
low-resolution artwork is not replaced with authored HD assets. Moving-object
guides remain partial; Frame Generation and Ray Reconstruction are absent.

The Windows build retains existing compiler and ShowCursor/FORCE linker warnings.
Linux compiler tooling exists; Linux gameplay and browser rendering remain future
work. Raw saves, captures, audio, generated game code and executables stay local.
Deployment identity, protected-file hashes and rollback are recorded in local
`dist/DrakanPC/BUILD_INFO.json`. No whole-game texture or performance claim is made.
