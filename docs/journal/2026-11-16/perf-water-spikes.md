# Modern water: the pass spikes while walking and the static world rebuild in flight (2026-11-16)

Follows [perf-water-hitches.md](perf-water-hitches.md). Owner's settings (`build/review/perf-cpu-jitter/user.ini`:
3840x2160 borderless, scale auto, uncapped, modern water on, HD textures), D3D11, RTX 5080. Evidence:
`build/harness/spk-*`, `fin*-*`, scripts `build/review/spikes/` (untracked). Other agents built and ran the game on
the machine at the same time; runs are marked where that showed.

## 1. Where the pass thread's time went

New `[ray-pass]` line with `GSR_WATER_LOG=1` (per-step milliseconds, allocations, model BLAS and attribute rebuilds,
staging bytes; [env-flags](../../reference/env-flags.md)). Surdana walk, 631 passes on main's code: pass median
6.3 ms, p99 22.0, max 28.4; GPU time of the pass median 1.6 ms, max 2.3. The slow passes were:

* **Dynamic attribute re-upload.** 544 of 631 passes copied 12 to 14 MB to the GPU (`upload` up to 16 ms). The
  dynamic attributes were laid out `[frame triangles][native models]` and any change of the frame's triangle count
  (every frame while moving) moved the ~6.6 MB of model attributes and forced a full upload.
* **Material generation changes.** A change of any model texture's flags rebuilt all ~87 resident model meshes'
  attributes, welding and smoothing their normals again: `models` 9 to 12 ms (`attrs=87`).
* Buffer growth by exact sizes: about 15 % of the passes allocated (0.3 to 1.4 ms each, one 10.9 ms).
* `wait` (a frame slot) of 5 to 47 ms in some passes: the GPU queue waits for D3D11's input fence, so this is the
  game being late, not the pass; it is counted in the pass time but does not lengthen the frame.

The static world rebuild (rebase) happens 8 times in 27 s of Arokh flight from the Ravenshold mounted start (one about
every 3.4 s). Such a pass took 16 to 21 ms: positions (`static`) 6 to 9 ms, a new 6 MB upload buffer plus its copy
(`upload`) 6 to 8 ms, three new BLAS buffers (`record`) 1.5 to 1.8 ms; its GPU build costs about 1 ms more than a
normal pass (1.6 against 0.55 ms).

## 2. Changes (all default on, each with its switch)

* `GSR_RAY_STABLE_UPLOAD`: dynamic attributes `[native models][frame triangles]`; the dirty compare runs over what
  both versions have and uploads the grown tail, so a changed count no longer re-uploads the models; buffers that
  grow get a quarter more room; each model mesh keeps its welded normals (tagged with the version of the welds they
  came from) across material generations and only repacks its materials.
* `GSR_RAY_REBASE_REUSE` (2 = alternate per rebuild for an in-run A/B): rebase positions written in one pass in slot
  order; the static vertex upload buffer and the three static BLAS of the rebuild before are reused once the GPU
  has passed them (one extra set of static BLAS stays allocated).
* `GSR_RAY_REBASE_DISTANCE`: measurement hook only (frequent rebases on foot).

## 3. Results

Variants: old = `GSR_RAY_STABLE_UPLOAD=0 GSR_RAY_REBASE_REUSE=0`, new = defaults; runs alternated.

**Walking** (Surdana, 22 s walk with three turns), quiet pair `fin-walk-*` (one compiler ran before old1), upload
change only (before the normals cache):

| run | passes | pass median / p99 / max ms | passes > 15 ms | frame p99 / p99.9 / max ms |
| --- | --- | --- | --- | --- |
| old1 | 720 | 5.1 / 18.1 / 20.0 | 17 | 10.5 / 18.8 / 20.5 |
| new1 | 782 | 5.0 / 8.6 / 20.6 | 1 | 9.4 / 13.3 / 17.6 |
| old2 | 719 | 5.1 / 18.1 / 25.5 | 17 | 11.0 / 19.0 / 21.6 |
| new2 | 597 | 7.0 / 12.6 / 20.7 | 5 | 13.5 / 18.3 / 25.6 |

Staging per pass: median 12.2 MB before, 5.6 to 5.8 MB after (what is left is the frame triangles' own ~5.7 MB, about
44,700 triangles, which differ every pass); `upload` p99 7.9 to 9.0 ms before, 4.4 to 5.1 after. The remaining
20 ms passes were the `attrs=87` passes. With the normals cache (runs `fin2-*`, busy machine) those passes'
`models` step fell from 9 to 12 ms to 3 to 7 ms (the first one after the level start stays at about 20 to 40 ms).
Busy pair `fin3-walk-*` (another game running): pass p99 61.9 and 67.8 ms old, 25.1 and 29.9 new; frame p99.9 66
and 104 ms old, 30 and 41 new. Frame-time differences in the quiet pair are within the run-to-run range; the pass
tail is not.

**Flight** (Ravenshold, Arokh, 27 s forward with three turns, 8 rebases per run; runs `spk-fly-*`, `fin3-fly-*`):

| run | rebase pass median / max ms | static / upload / record ms | allocations | passes > 15 ms |
| --- | --- | --- | --- | --- |
| old (3 runs) | 15.9, 19.5, 21.1 / 20.5, 22.1, 23.7 | 6.2-9.3 / 6.4-7.9 / 1.7-1.8 | 3 | 12, 16, 17 |
| new (3 runs) | 7.6, 8.7, 8.9 / 9.2, 17.6, 10.6 | 4.6-5.5 / 0.8-1.1 / 0.35-0.46 | 0 | 1, 6, 4 |

In-run alternation (`GSR_RAY_REBASE_DISTANCE=300 GSR_RAY_REBASE_REUSE=2`, Surdana, busy machine): 25 old rebases
median 16.6 ms, max 27.8; 26 reuse rebases median 14.1, max 22.6; upload 3.7 to 1.2 ms, record 1.5 to 0.7 ms.

The longest frames in flight (40 to 140 ms, the same in both variants) contain no pass end: the water pass is not
their cause. Passes with a 25 to 47 ms `wait` sit next to them as a symptom.

## 4. Picture identity

* `GSR_RAY_CPU_CACHE_CHECK=1` now also checks that every frame triangle and model mesh finds exactly its own
  attributes at its instance ID, that the GPU mirror equals every dynamic attribute after the copies, and that the
  one-pass rebase positions equal the old three group passes byte for byte. Runs: Surdana with 48 forced rebases (196
  passes), Surdana default (321), Ravenshold flight (733 passes, 15 rebases), Surdana after the normals cache (192):
  no difference, no fault, `[ray-cpu-cache] ... zero mismatches` (model attributes, normals included, against a full
  rebuild).
* The shader reads attributes only at `(InstanceID & 0x7ffff) + PrimitiveIndex`; the TLAS instance order, all BLAS
  inputs and the upload contents are unchanged, so the layout change only moves where the same bytes sit.
* `gsr_test_ray_water` (now 3 more rebase rounds compared with the reference picture) and the `ray_`,
  `surface_builder`, `synthetic` tests: 11 of 11 pass with and without the check, and with the switches at 0 and 2.
* Not proven: an in-game pixel comparison. Four still-shot runs at a fixed water time gave four different pictures
  per variant (the game is not deterministic run to run), so screenshots cannot show identity. A BLAS rebuilt into a
  reused buffer from the same inputs is assumed to trace the same (the unit test's picture matched after every
  round).

## 5. Open

* The frame triangles' attributes (~5.7 MB) are still uploaded every pass while moving; their order changes with the
  draw list.
* A rebase still costs about 5 ms on the pass thread (it reads every resident triangle; 168,889 in Surdana) and 1 ms on the GPU.
* The first `attrs=87` pass after a level start (20 to 40 ms) and new model BLAS allocations (one 10.9 ms) remain.
* `ray_water_lost_signal` failed once while the full game build loaded the machine (its first pass took 1.6 s to
  record against a 300 ms watchdog) and passed in every later run.
