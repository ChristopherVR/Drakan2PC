# Northern Tundra (4B) weak spots and the rarer dips elsewhere (2026-11-07)

Goal: find out what the GPU/driver waits on at the 4B spots from `clean-playtest.md` and fix what is worth fixing.
Result: no fix was justified. The weak 4B spots are a steady GPU-throughput limit of the full effect stack, not a stall,
and the machine never became quiet enough to attribute the cost to one effect by A/B toggles. Numbers and method below.

## Method

* Own exe copy in `build/fps4b/play` (copy of `dist/DrakanPC`; later the same folder with `build/final-game/bin/drakan.exe`
  and its `.map` so that `scripts/perf/hostprof.py` resolves). Runs through `play_run.py --exe ... --level 4b` with a
  `place` recipe (`build/fps4b/rec.txt`: baseline spot A 972721,135100,1360235 then B 928600,131148,1406745, C 751616,140546,1224542,
  D 1028690,141355,1361681, E 330712,125922,1354503, 28 s each, no walking), `PS2X_PERF=1`, `DRAKAN_PLAYTEST_GOD=1`.
* Before each run the script polled `tasklist` every 30 s for up to 20 min for no other `drakan.exe`. The machine
  was never quiet except for one window: other sessions ran 1 to 10 games the whole time.

## Runs

| Run | others at start | A (base) flips2 | B | C | D | E | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| base | 1 (contaminated) | 66 | 37 | 47 | 88 | 124 | vblank 60 throughout, 0 `perf-stall` |
| diag (`GSR_RAY_GPU_TIMING=1 GSR_PROFILE=2`) | 0 (clean start; 2 appeared near the end) | 160 | 115 falling to 55 | 45 | 65 | 64 | vblank 60, 0 `perf-stall`, no slow GS record |
| noray (all three ray effects off) | 6 (contaminated) | 28 | 63 | 46 | 54 | 53 | not comparable with the rest |
| C-only runs with 6 to 8 others | | | | 7 to 53 | | | `noray` 34, `noshadow` 13, `nowater` 34, `nodlss` 53, control 7: pure contention noise |

flips2 = rendered world frames per second; vblank (display presents) stayed at 59 to 61 in every run, so the screen
never dropped below 60 Hz presentation, the world rate did. Spot ranking differs completely between base and diag
(B 37 vs 115, D 88 vs 65) and A is 66 vs 160, i.e. the spread between the two runs is larger than the spread between
spots. Only the diag run is usable as a spot profile, and only for the one clean window.

## What the clean (diag) run shows

At the weak stretch (C, 45 fps; later D/E 46 to 65) against the fast stretch (A, 160 fps):

* GS worker `presentCall` (the D3D11 Present, which blocks while the GPU is behind): 618 ms per 2 s over 336 presents (1.8 ms each)
  against 810 ms over 95 presents (8.5 ms each, max 14 ms). Draw count per frame is the same order (15k vs 15k),
  `syncRT`, `resolveTexture`, `drawObjectMotion`, `drawVertexUpload`, `rayScene` per frame are all flat or smaller.
  So the extra frame time is GPU time, not CPU submission cost.
* DXR pass GPU time (`GSR_RAY_GPU_TIMING`): total 3.0 ms at A, 5.5 to 9.4 ms at the weak stretch; the growth is nearly all
  the `compose` stage (1.7 ms to 7.2 ms), with `rejected_frame_triangles` rising from 8k to 21k. Build and transport stay under 1 ms.
* EE thread: ee_ms/flip 4.6 to 9.3 and ee_thread_cpu 88 to 97% in both, so it is also near saturation (the world rate is a
  two-flip frame). Pipelined, the slower of the GPU chain and the EE sets the rate.
* No `[perf-stall]` line, no slow GS record, no shader compile or texture upload spike in the clean window. The 99
  `perf-stall` lines of the earlier playtest were taken with 2 to 3 other games sharing the one GPU; they show the same
  `nvwgf2umx` wait but it is the Present/Map wait of a saturated GPU, not a distinct stall source.

## Why nothing was changed

* PSO/shader warming: nothing to warm. No first-use compile showed up as a hitch in the clean run (stalls are zero).
* Buffer pooling / async uploads: `syncUpload`/`syncRT`/`drawVertexUpload` are flat between the fast and the weak stretch.
* Capping an effect at the spot: the only effect-sized candidate is the ray `compose` stage (up to 7 ms of a 21 ms
  frame). I could not measure its share of the frame with the toggles (`GSR_RAY_SHADOWS=0`, `GSR_MODERN_WATER=0 GSR_RAY_REFLECTIONS=0`,
  `DRAKAN_DLSS=off`) because every toggle run had 4 to 8 other games on the GPU and flips2 swung between 7 and 124 for the
  same configuration. A change without a clean A/B would only be a guess; it is left open.
* The rarer dips (3A, 4A, 5, 2, 8) were not re-profiled: the earlier recheck already showed them vanishing, and the machine
  gave no clean window to test them.

## Open, for a quiet machine

1. Run `build/fps4b/rec.txt`-style spots C and D with the four toggles above, back to back, with `tasklist` empty. If
   `GSR_RAY_SHADOWS=0` restores 100+ fps at C, the `compose` stage (per-pixel shadow/AO queries over reconstructed frame
   geometry) is the target; options are tracing at lower resolution under load or pruning more frame batches.
2. A GPU timestamp pair around the D3D11 world pass and DLSS (only the DXR pass has timestamps now) would split the 21 ms
   without needing a quiet machine.
3. Note for `scripts/perf/hostprof.py`: the `.map` must be the one written with the exe under test. `build/final-game/bin`
   changes whenever another session rebuilds, so copy the exe and the map together (my first hostprof used a newer
   map and resolved to wrong symbols, which is how this was found).

## Quiet-machine rerun (deployed exe of 16:27, own copy in `build/fps4b2/play`)

Every run started with `tasklist | grep -ci drakan.exe` = 0 (checked per run, 0 others for all 27 runs), strictly one game at a time,
`PS2X_PERF=1 DRAKAN_PLAYTEST_GOD=1`, recipes `place`d the spot and measured 19 to 23 s of steady state after 6 s settling. The earlier
run "diag" numbers were a fluke of the shared machine only for the absolute values; the rerun reproduces its shape exactly.

Median of 3 repeats (spread = min to max of the three run medians):

| Spot | flips2/s | vblank/s | ee_ms/flip | ee thread cpu | compose ms | presentCall ms | rejected tris |
| --- | --- | --- | --- | --- | --- | --- | --- |
| A | 175.8 (174.0 to 175.8) | 60.1 | 4.30 | 95 to 98% | 1.97 | 1.2 to 1.6 | 8.3k |
| B | 138.9 (138.8 to 141.0) | 59.9 | 5.86 | 93 to 94% | 2.28 | 1.4 to 1.7 | 15k |
| C | 97.9 (95.8 to 98.4) | 60.0 | 8.06 | 96 to 97.5% | 3.34 | 0.3 to 0.8 | 20.9k |
| D | 149.3 (146.3 to 160.2) | 60.1 | 5.38 | 94 to 97% | 2.27 | 0.65 to 1.35 | 5.7k to 10k |
| E | 167.5 (166.5 to 168.3) | 60.0 | 3.58 | 76 to 80% | 2.07 | 2.8 to 3.0 | 8.5k |

So nothing is below 95 fps on a quiet machine (the 45 fps of the shared run was contention); C is the worst spot and the only one near 100.

Toggles at C, 3 repeats each, back to back (flips2/s, run medians):

| Setting | flips2/s | ee_ms/flip | compose ms |
| --- | --- | --- | --- |
| baseline | 99, 100, 100 | 8.06 to 8.18 | 3.35 |
| `GSR_RAY_SHADOWS=0` | 110, 112, 114 | 8.18 | 0.83 |
| `GSR_MODERN_WATER=0 GSR_RAY_REFLECTIONS=0` | 107, 103, 106 | 8.0 to 8.2 | 2.7 |
| `DRAKAN_DLSS=off` | 78, 74, 78 | 6.9 to 7.4 | 7.4 |
| `DRAKAN_POLY_NEAR_CLIP=0` | 99, 100, 92 | 8.0 to 8.1 | 3.3 |

Attribution: the world rate at C is set by the EE thread, not the GPU. ee_ms/flip stays at 8 ms in every setting (it is
a property of the guest workload at that spot), presentCall is the smallest of all spots at C (0.3 to 0.8 ms, the GPU is
waiting for the EE, not the reverse), and killing the biggest GPU cost (shadows, compose 3.3 to 0.8 ms) buys only 12%. DLSS off is
worse (compose doubles because the DXR pass then runs at the larger native size). The near clip hook is neutral.

EE side (host RIP sampler, `PS2X_HOSTPROF=12`, EE thread only, `build/final-game/bin/drakan.map` which is byte-matched to the
deployed exe) at A against C, as share of EE thread samples: guest generated code (`f_0x*`) 45% vs 39.5% (about 1.9 ms vs 3.2 ms per
flip), `PS2Memory`/`EeScheduler` helpers 12% vs 18.6% (0.5 vs 1.5 ms), our capture hooks (`drakan_scene_models`,
`drakan_object_motion`) 20% vs 14% (0.9 vs 1.1 ms, i.e. flat). VU1 runs on its own worker (`vu1async`): 1.06 M pairs/flip at A against
3.45 M at C, so VU1 is busy but off the EE thread. The extra EE cost at C is spread over many guest functions (top one
`f_0x116990` is 4.7% in both) i.e. it is the game doing more per frame there (more visible/animated objects), not one hotspot.
Expected best case from trimming our hooks entirely is about 13% at C.

Decision: no code change. There is no function or effect that explains the 8 ms; the largest single removable item (compose, shadows) is worth
12% at C and costs image quality; the hooks are 1.1 ms of 8. What would move C is a faster guest dispatch (`translateAddress`,
`read64`, `checkpointDue` take 5 to 6% together) or fewer guest instructions per frame, both larger projects. The cheap
micro-fix seen in the profile is `Tracker::freed` (linear scan over `lives` and `topologies` per free, 2.4 to 4.7% of EE), worth under 3%.
