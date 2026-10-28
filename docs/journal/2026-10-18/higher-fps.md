# Higher world frame rate — experimental

Historical 60 FPS experiment. The subsequent [uncapped implementation and
validation](uncapped-fps.md) supersedes the remaining-work section below.

This removes the original 30 FPS cadence restriction with an optional target of
up to 60 world frames/s. It is **not unrestricted rendering above 60 FPS**, and
does not guarantee 60 FPS in demanding scenes. The default remains 30.

In F1 → Display, choose **World frame rate → Higher (up to 60 FPS, experimental)**,
apply, and restart. The equivalent setting is `[video] world_rate = 60` in
`drakan.ini`; `DRAKAN_WORLD_RATE=60` overrides it for a process. Choose Original
or set 30 to restore the previous cadence. The separate **Presentation limiter**
only limits presentation; selecting Off there does not unlock world updates.

## Implementation

The graphics worker at `0x1d78f0` increments `0x4e8e98`, sleeps, then requires
four wake cycles before attempting completion. Its handler receives both vblank
edges. Four cycles normally span two 60 Hz periods.

The hook intercepts the worker's SleepThread call (`0x4bc080`, return address
`0x1d7a24`) and promotes counts 2 or 3 to the original threshold 4. It verifies
the graphics object and worker thread identity. Semaphore ordering, readiness
checks, flip completion and the original syscall remain intact. Queued wakeups
use the same entry. Other threads and resumed syscalls are unaffected.
Guest vblank remains 16,667 µs; COP0 Count and audio clocks are unchanged.
No generated game source is published or patched.

`DRAKAN_FRAME_AUDIT=1` samples completed timesteps at `0x143448`, including
calculations that yielded. Its local-cap sum is a diagnostic proxy, not a count
of actual physics integration calls. The audit is disabled normally.

## Timing limitations

The engine calculates elapsed dt, but one subsystem caps local dt at 0.030 s,
and camera smoothing uses a per-update coefficient. Full combat, physics,
camera response and cutscene timing parity at 60 FPS remain unverified.

An initial fixed-duration comparison produced identical mouse pitch responses
and released attack states. Movement distances were affected by scenery and RAM
capture stalls; this is not a movement-speed parity pass.

The input-script header `clock milliseconds` schedules actions by monotonic
elapsed time. `sync play` resets the origin. Taps and mouse repetitions still
count input samples. Existing frame-based scripts retain their behavior.

## Final combined executable

SHA-256: `A98444FE822EFDB4B8F03A6966CA2C206B21B4FE324606850C07643B4AA0652E`

Source implementation: `b2165a47296497de6fa6bf3f1c6af12f10350fff`.
Authored runtime: `ba5d57213d9f6efee124439b1916699b6052c1fa`.

- 1,021 input CPU checks, 269 settings core checks, 43 menu checks, 403 cadence
  checks and 12 comparison-tool unit tests passed.
- Native and SR+NR each passed 23 live checks: 7 inventory categories,
  7 Equipment mouse checks and 9 movement/look/bow checks. Audio output was
  enabled. Both exited normally. Original, fixture and private saves were unchanged.
- SR+NR recorded 1,777 successful SR and 1,575 successful neural evaluations.
- Matched capture-free, audit-free SR+NR runs with identical executable, script
  and input hashes measured **30.004 → 33.688 world submissions/s (+12.28%)**
  over their last five positive-world windows. Scale 4, VSync off. Guest vblank
  delivery fell under the increased workload; its configured frequency was not
  increased. This does not establish 60 FPS throughput.
- Native functional gameplay windows ended at approximately 40–42 world
  submissions/s. This was not a matched native performance benchmark.
- The report excludes interleaved startup messages only when a complete
  zero-world field is present. Damaged positive-world rows fail validation.

Results: [higher-fps-20261018.json](data/higher-fps-20261018.json).
Recipe: [fps_steady_wall.txt](../../../scripts/harness/recipes/fps_steady_wall.txt).
`python scripts/frame_rate_report.py BEFORE_RUN AFTER_RUN` validates and compares
private harness directories without launching or changing the game.

## Remaining work

Uncapped rendering above 60 requires separating frame completion and presentation
from guest vblank wake scheduling. Further throughput work is needed to reach
60 consistently in the tested scene. Existing smoke/shadow, HUD border, broader
texture and combat issues are not claimed fixed by this change.
