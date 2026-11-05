# Capped completion pacing and remaining readback stalls

After the cap deadline expires, the host now waits for the next completed latch instead of polling only at cap
intervals. A frame finishing slightly late can wake presentation immediately. The producer still waits for the
captured latch acknowledgement; image ownership, renderer lock ordering and guest timing remain intact.
Uncapped mode retains its existing completion-driven wakeup and has no added frame interval.

The deployed and tested pacing executable is
`28968fab5e7b3a7935572d0d2cd489fa5db2847dfa837413c2e65069cb00904e`.
Receipts are under ignored `build/perf-pacing-fix/`.

## Measurements

The initial Isles combat-load sweep (`bench-105255.json`) measured uncapped 37.37 FPS, cap 30 at 29.86 FPS and
cap 60 at 38.20 FPS. The preceding investigation measured cap 60 at 30.83 FPS. This supports the pacing change,
but these are shared-machine, different-build samples, not a controlled same-executable A/B proof. The cap-60
readback wait remained about 8.7 ms/present; its p99 frame time was still 51.19 ms. Stalls are not solved.

Explicit action scripts (`bench-105809.json`) use wall-clock input timing:

| Scene/action inputs | Uncapped FPS | 30 cap FPS | 60 cap FPS |
| --- | --- | --- | --- |
| Catacombs: repeated attack/jump, then movement | 49.01 | 29.75 | 40.70 |
| Isles: two held primary-fire inputs amid enemy projectiles | 40.90 | 29.63 | 44.66 |

All six runs exited through the scripted quit. Engine-time/wall-time ratios were 0.9848–0.9961. Contact sheets
were inspected. Rynn's post-action attack state was zero and health 100 in all three ground runs; subsequent
movement occurred. One-second movement distances varied from 2072 to 2411 units, so these tests do not establish
identical movement integration. Dragon captures show active firing/effects and continued gameplay. Health aids
protect the redirected Isles fixture. These are action/recovery smoke checks, not exhaustive hit detection,
projectile trajectory, damage or animation-timing validation. Long frames remain in every action mode.

`pacing_bridge`, `partial_upload` and `world_readback` CTest suites passed (3/3, 8.50 seconds); diff whitespace
checks passed. Other running sessions were preserved.

## Readback experiment and outstanding work

The trace shows repeated reads of a 64-pixel-wide scratch target with only rows 0–4 dirty. Matching-version early
staging copies already precede those reads. Waiting for earlier GPU work dominates this small transfer.
An opt-in `GSR_RB_SUBMIT_EARLY=1` experiment flushes the D3D11 command buffer after the predicted copy; it retains
all version/row/scale validation and is OFF by default. It compiled, but its deployment initially failed on an
`avcodec-61.dll` held by another session. The prior pacing executable remained intact and was used for the action
checks above. The experiment has not been benchmarked and must not be described as a readback performance fix.

At the end of these checks there were no owned game/build processes. The coordinated issue-1 session then took
the canonical deployment and functional-test window, including the current sources. Outstanding: benchmark the
submission experiment once that window is released, retain it only if beneficial, and extend isolated cap/action
comparisons. Uncapped throughput and readback stalls remain explicit requirements, not completed fixes.


## Exclusive-window readback follow-up

The issue-1 task released the GPU/game window. Four Isles runs used the same deployed executable
`4ce9dd0d611d595d0c1aa5b4e8bf0ba9e723e5f66152bfaad258349576ecf278`, frozen graphics/native options,
uncapped world/display cadence, and only the benchmark game process. Receipts:
`build/perf-pacing-fix/bench-121038.json` and `bench-121304.json`.

| Order | Early submit | FPS | p99 ms | Readback ms/present |
| --- | --- | --- | --- | --- |
| 1 | off | 48.78 | 39.34 | 8.08 |
| 2 | on | 36.74 | 56.32 | 9.94 |
| 3 | on | 33.25 | 60.14 | 11.76 |
| 4 | off | 33.47 | 70.10 | 11.66 |

All four scripted runs exited successfully. The repeat pair shows no useful benefit; the large run-to-run
variation prevents treating the first pair as an isolated causal regression estimate. Removed the speculative
Flush/environment switch from source. The tested executable with the switch absent/0 is behaviorally equivalent
to that removal. No readback performance fix is claimed from this experiment. Required current-frame GPU data
still blocks Map; returning old pixels or dropping producer acknowledgement would invalidate correctness.


## Native melee cap acceptance

On the same 4CE9 executable, ran `directional_attack_check.py` through an ignored scratch wrapper which changes
its forced `DRAKAN_WORLD_RATE=30` to `uncapped` at process launch. Display caps were 0, 30 and 60. The native
PAL Surdana card was copied privately for each run and all source hashes remained unchanged. Receipt:
`build/perf-pacing-fix/native-caps-121526.json`; run folders `build/harness/perf-native-caps-121526-{0,30,60}`.

| Display cap | Native checks passed | Median positive-world present interval (ms) |
| --- | --- | --- |
| Uncapped | 59/59 | 9.050 |
| 30 | 59/59 | 33.341 |
| 60 | 59/59 | 16.683 |

Each run checks neutral attack, all four directional arrow/WASD attack clips, clip advancement, release,
stable idle and movement afterward. All scripted exits succeeded and the logs confirm uncapped world cadence.
An uncapped attack screenshot also shows 74.9 world/present FPS. These are native state acceptance checks;
the frequent 32 MiB RAM dumps perturb timing, so their intervals verify the active limiter rather than establish
throughput gains. This safe-town fixture does not prove enemy-hit damage, projectile trajectories or identical
wall-clock animation timing. Earlier combat-load/dragon smoke checks remain separate evidence.

The existing Release `world_readback`, `partial_upload` and `pacing_bridge` suites passed 3/3 (7.46 seconds).
An initial CTest invocation omitted the required multi-config `-C Release` and ran no tests; corrected above.
No rebuild was needed to validate the retained pacing behavior: 4CE9 already contains it, and disabling the
removed experiment selects the unchanged baseline path. The shared window was explicitly returned with no
owned game processes. Remaining work: reduce required GPU readback waits without stale data; no new FPS gain
or comprehensive combat correctness is claimed by this follow-up.


## Subsequent readback work

The later [constant-color scratch sprite investigation](solid-sprite-readback.md) identified a bounded exact
CPU mirror that removes the dominant recurring wait in Isles and Catacombs. It supersedes the earlier
"readback unresolved" status for that specific path; it does not skip required GPU-dependent reads.
See that note for matched measurements and final validation.
