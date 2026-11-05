# Sharing geometry work at the renderer join

## Cause and change

When the GS renderer needs current-world geometry, it must see every eligible triangle and resident material
update in submission order. In the earlier hitch trace, hundreds of draws were waiting behind model-pose
preparation on the same worker. A starved worker could take hundreds of milliseconds of wall time to finish.
The starting workspace was checkpointed at `8ae9614`, including formats submodule `f212662`.

The surface worker now consumes up to eight FIFO jobs per lock acquisition. It builds lighting and opacity
attributes only for triangles that enter the frame list. A resident surface that needs a material update still
receives its original texture coordinates. The producer only wakes the worker on an empty-to-nonempty transition.

D3D11 model motion uses immutable current/previous snapshots and can run independently of the surface draws.
The worker normally prepares poses while the renderer draws. After a join takes over at least one full batch,
the next pose is staged for the renderer to prepare before joining. This lets the worker start reconstructing
surfaces sooner during contention while keeping pose work off the render thread on ordinary frames. A newer
snapshot supersedes unused staged preparation. Each task uses the floating-point mode captured at submission, and work on the
renderer thread restores the caller's mode afterward.

If there is a backlog at a drain, the worker finishes its already claimed batch and the joining producer
takes the unstarted FIFO tail. There is one owner of the camera cache, target lists and material updates
at a time. Small queues already being serviced stay on the worker. Level changes, target destruction and
all other consumers use the same drain. The path continues to use the current world's geometry.
`GSR_SURFACE_OVERLAP=0` selects the worker-only join/model scheduling for comparison. Profiling distinguishes
actual condition-variable wait (`surfaceWait`) from reconstruction performed at the join (`surfaceAssist`).
The enclosing `surfaceDrain` includes both. The common FIFO batching and attribute changes are active in both
modes on the comparison executable.

## Regression checks

Scene geometry, threaded construction and model motion tests passed. The new threading case blocks an ordered
job on independent producer work, supersedes a pending task, checks the captured rounding mode and its restoration,
and drains a backlog of 256 ordered jobs exactly once. A separate old-versus-new CPU oracle compared 137 synthetic
worlds, 63,094 triangles (20,282 water) and the projected resident materials. It matched the checkpointed
reconstruction and the synchronous path. The reference comparison used the same ordered reconstruction path as the deployed build.

A trial that switched all geometry to inline execution after a slow join was discarded. In the uncapped stress pair
it reduced reported waits but frame p99 regressed from 41.9 to 127.6 ms. A second trial always prepared poses on the producer. It significantly reduced waits but uncapped and 30 FPS
whole-frame p99 regressed in the stress comparison. For example, its uncapped wait fell from 2.179 to 0.270
ms/world but frame p99 increased from 72.2 to 81.3 ms. The retained design chooses producer preparation only
following an observed backlog. The earlier receipts are `geometry-stress.json`, `geometry-overlap.json`
and `geometry-final.json` under `build/perf-hitches/` (the last is the always-deferred trial).

## Contended combat test

The same Isles fixture repeatedly fired during a 32-second timed interval with no screenshot or RAM dump inside
it. The executable, native card/options, frozen graphics settings and input recipe were held constant. For
repeatable contention, each game inherited affinity to the same six logical processors; three bounded CPU loads
shared those processors. Game affinity was checked by PID during every run. The CPU loads exited at the end.
The order was worker-only then work-sharing for uncapped, reversed for 30, then worker-only first for 60.
Each cell is a single run, so maxima and end-to-end FPS also reflect scheduler variation. The p99 is the 99th
percentile of presented-world frame intervals. Receipts and load samples are in
`build/perf-hitches/geometry-adaptive-final.json`, with individual run names pointing to `build/harness/`.

The final benchmark executable SHA-256 is `ad0515ed45f48c2ff38251d1a4dfcc8ad7bfad2f438c7d46c754c4a7df1abb30`.
A very long frame in the uncapped worker-only control illustrates the distinction between geometry waits.
The frame spanned 4,866 ms. The separate `rayEnqueue` bucket measured 4,847 ms, and the ray watchdog twice
identified pass 748 waiting for a D3D12 frame slot. CPU surface reconstruction and the GPU frame-slot fence
have separate completion requirements. A shorter CPU join cannot unblock an unfinished GPU frame.


| Cap | Scheduling | FPS | p99 frame ms | Max frame ms | Mean blocked ms/world | Max blocked ms | Total join ms/world |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Uncapped | Worker only | 11.63 | 420.78 | 4866.13 | 6.069 | 133.83 | 6.070 |
| Uncapped | Shared work | 36.04 | 93.94 | 293.23 | 0.533 | 29.11 | 1.155 |
| 30 | Worker only | 28.78 | 84.33 | 174.68 | 3.421 | 130.49 | 3.422 |
| 30 | Shared work | 29.94 | 33.90 | 83.80 | 0.436 | 2.68 | 0.790 |
| 60 | Worker only | 42.96 | 153.84 | 318.92 | 2.237 | 77.73 | 2.237 |
| 60 | Shared work | 56.92 | 24.01 | 50.90 | 0.133 | 15.53 | 2.309 |


The median utilization on the six assigned processors was 97.5–100% across the measured windows.
All six fights exited by script, used the same executable hash and private options, and had only one
game process sampled. With shared work, the engine-time / wall-time ratios were 0.9842 (uncapped),
0.9997 (30) and 0.9997 (60). Average blocked time per world
fell in all three comparisons. At 60 FPS, counting the useful reconstruction performed on the joining thread,
the total join cost was similar (2.24 / 2.31 ms per world). A single pair per cap on a shared machine cannot isolate an expected
average FPS gain, especially given the GPU-frame-slot outlier in the uncapped control.

An earlier separate melee attempt on intermediate executable 4AE441D9 exited during startup with a bad allocation
and zero rendered frames. Other City integration processes launched around this attempt. It supplied no melee
acceptance results and is excluded from performance comparisons.

## Ground combat acceptance

After the coordinated issue #1 window, the integrated play-folder build
`6bfec79362a133ad83fee77c706bbf327b361571e4f699e1444f28698871fbf7` included the geometry fix. A private
native Surdana card placed Rynn beside hostiles. All three runs detected an active native sword swing, observed
the same nearby NPC lose HP between the pre-attack and third-hit snapshots, and exited with Rynn alive.
Source cards and executable hashes remained unchanged throughout each case. These runs verify gameplay;
the RAM dumps perturb timing. Receipt: `build/perf-hitches/geometry-hostiles.json`.

| Cap | Native swing | Enemy HP before / after | Rynn final HP | Median shown-world interval during encounter (ms) |
| --- | --- | ---: | ---: | ---: |
| Uncapped | Active | 40 / 19 | 44 | 6.73 |
| 30 | Active | 40 / 21 | 44 | 33.34 |
| 60 | Active | 40 / 21 | 34 | 16.67 |

The surface-builder concurrency suite also passed 20 consecutive runs.
