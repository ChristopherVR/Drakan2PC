# Combat performance and frame-limit review

Investigated the current deployed executable `c2ed9a75babf249422b7c594ed6feab14fcf783cea51aa3af1512f449c8ded27`.
This is a later build than the earlier ray-query benchmarks; their absolute FPS cannot be treated as this build's baseline.
No runtime code or user settings were changed during this investigation.

## Live combat sample

Isles redirected start, active enemy projectiles, audio and frozen scale-four/DLSS Quality graphics settings. Existing
health aids keep Rynn and Arokh alive. Twenty-second stationary samples are diagnostic combat-load samples, not an
attack correctness test. Other sessions were left running as requested.

| Display cap | World FPS | p99 frame ms | Readback wait ms/present | Median ray GPU ms | Engine-time / wall-time |
| --- | --- | --- | --- | --- | --- |
| 30 | 29.73 | 34.20 | 4.36 | 4.41 | 0.999 |
| 60 | 30.83 | 36.13 | 8.75 | 4.49 | 0.994 |
| Off, repeat | 39.48 | 48.11 | 10.46 | 4.66 | 0.993 |

Receipts: `build/perf-combat-review/bench-102226.json` and `bench-102650.json`, plus corresponding GPU summaries.
The 30-cap run overlapped other game processes for part of the run. The 60-cap and uncapped repeat recorded only
their own game process, but other machine load is uncontrolled. The first uncapped launch timed out before gameplay;
it is excluded. Screenshots of the successful capped runs show active gameplay and enemy effects.

## Findings and limits

- GPU ray work is about 4.4–4.7 ms, substantially less than the 25–34 ms world-frame interval. Ray shaders alone
  therefore cannot explain the remaining frame cost. The measured readback wait is a major serialization cost;
  it includes waiting for preceding GPU work and must not be added to GPU timing as independent work.
- `GsrBridge::hostTick` applies the display cap before presentation. `waitForHandoff` blocks native completion until
  the host acknowledges the published frame, with a 100 ms timeout. Thus the display cap also back-pressures the
  producer. A missed host presentation opportunity can delay the next world frame. This is a source-supported
  explanation to investigate for the 60-cap throughput loss, not a fully isolated causal measurement.
- Display cap, `world_rate`, and VSync tick snapping are separate mechanisms. The measured engine clock remains
  near real time at both tested caps. There is no evidence here of global slow motion or a cap-induced freeze.
  Individual hit windows, attack/jump transitions, projectile collision and animation correctness remain unverified.
- Previous world-rate audits already found and fixed rate-sensitive velocity estimation and attack/jump animation
  behavior. Their limited action comparisons do not establish correctness of all combat sequences and frame limits.
- The timer audit's `localCapProxy` is a diagnostic calculation, not proof that gameplay universally clamps at 30 ms.

Next causal checks should measure producer handoff wait versus GPU/readback wait in alternating capped/uncapped combat
runs, then exercise repeated attacks, attack+jump, spells and projectiles across world-rate and display-cap settings.
A safe pacing change must preserve image ownership and completion ordering; simply removing the handoff wait risks
overwriting a frame before presentation. No speculative timing patch is warranted by this sample alone.
