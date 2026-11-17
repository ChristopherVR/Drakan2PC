# Ruined Village scene 1.3 hang with modern water (2026-11-17)

Owner report: "the game crashed" while playing the Order of the Flame campaign from `dist/DrakanPC`, in the Ruined
Village opening, during scene 1.3 (Julian pinned by the spear). Julian had become an authored Blender mesh that day
(formats 2e249ca) and looked "very broken" there, so his mesh was the first suspect. Scratch, packs and runs:
`build/review/raywater-hang/`, `build/ootf-run/rwh-*` (not tracked).

## The log

`drakan.log` (copy: `build/review/crash-1117/drakan-owner-1859.log`) shows, about 10 s after the 1.3 music stop:

```
[gsr-presentation] host handoff timed out at latch 14773   (x4)
[ray-water] watchdog: pass 5927 not finished after 1048 ms (its signal is queued on D3D12 but the GPU has not
            reached it); consumer fence 5926 of 5930, producer fence 5926 of 5930
[ray-water] pass 5930 submitted in 2308.6 ms on the pass thread (slow): ... 582 resident model instances;
            55107 model BLAS refits
[ray-water] watchdog: pass 5928 is stuck; releasing the shared fences and turning modern water/ray shadows off
[frame-uncapped] seconds=28.050519 completedFlips=1
```

The game did not crash: it froze for about 30 s and then ran on (without modern water) at a third of its frame rate.

How to read it:

* **"55107 model BLAS refits" is the session total**, not per pass (`Impl::modelRefits`, cumulative since the pipeline
  started; `docs/journal/2026-10-23/blas-refit-and-vu-ceiling.md`). Per pass it is about 10 (`GSR_WATER_LOG` `[ray-pass]
  refits=`: 20478 refits over 3784 passes in run `rwh-owner`); 55107 over 5930 passes is the same rate.
* **The 2308 ms is not CPU work.** Three frame slots rotate (`kFrames`); pass 5930 reuses pass 5927's slot, and the pass
  thread waits in `acquire()` for that slot's fence. Pass 5927 had not completed on the GPU, so the time is a wait.
* **The stuck GPU work was not the ray pass.** Each D3D12 pass first waits on the GPU for the D3D11 producer value of its
  frame. "producer fence 5926" means D3D11 had not reached its signal for frame 5927 (`produce11` flushes it), while the
  consumer fence had already reached 5926, so the previous ray pass was done. The D3D12 queue was idle, waiting for the
  D3D11 world render of frame 5927, and the presentation host had already missed four handoffs (100 ms each) before the
  watchdog's first line. The whole GPU was behind; no D3D12 work was long. The device was never removed (`0x00000000`):
  no single submission hit the 2 s TDR limit, which also rules out one long acceleration-structure build or trace.

## The cause: another program saturating the GPU

At 18:59:14 another session started 13 headless Blender 5.3 EEVEE renders at once (character head comparisons,
`build/heads/*/pc` and later `/new`, each rendering front / three-quarter / side and three face close-ups, outputs written
from 18:59:20 to past 19:01). The owner's log ends about 70 s after the freeze began, and it was copied at 19:00:34,
so the freeze started at about 18:59:10 to 18:59:20, together with the renders. After the freeze the game ran at 22 to 60
flips per 2 s instead of 120, while those renders were still running.

Reproduced (stock 18:49 exe, owner's `drakan.ini` and pack, `PS2X_GS_SCALE=8` so the ray interop is 4096x4096 like the
owner's, `GSR_TEXUP=auto`), `tools`: `build/review/raywater-hang/blender-burst*.sh` starts the renders when the log
shows the third `scene start` (scene 1.3):

| Run | Load | Result |
| --- | --- | --- |
| `rwh-owner`, `rwh-owner8`, `rwh-full` | none (scale 6, 8, 8 + texture upscale) | no watchdog line, no handoff timeout; GPU ray pass at most 26 ms (`GSR_RAY_GPU_TIMING`), heartbeat at most 2.06 s |
| `rwh-cowner` | none; RV converted with the morning kit (authored Julian and Delon, commit f4f0e66a) | no stall; GPU pass at most 53 ms (pass 1) |
| `rwh-burst` | 13 renders, three views each (21 s) | 4 handoff timeouts, a 250 ms slow pass, a 5.3 s frame; no watchdog |
| `rwh-burst2` | 26 renders (pc and new), six views each | **the owner's log line for line**: handoff timeouts, `watchdog: pass 6775 not finished ... signal is queued on D3D12 but the GPU has not reached it; consumer fence 6774 ..., producer fence 6774`, `is stuck; releasing`, then frames of 97, 11, 12 and 23 s |

Julian's mesh is not involved: in `rwh-owner` and `rwh-cowner` his 1.3 pose is whole (no stretched triangles; shots
`t180000.png`, `t140000.png`), and the same freeze happens with any scene once the GPU is saturated. Nothing in the game
can make a frame finish while other processes hold the GPU; the watchdog did what it is for (it released the fences after
3 s and turned the pass off, so the game did not deadlock). **Do not run GPU renders (Blender EEVEE/Cycles, other game
instances, HD movie upscaling) while the owner plays or while measuring.**

## Hardening anyway: broken poses cannot reach a model BLAS

The question was fair: a broken skin can fling vertices across the level. The capture already rejects poses with
non-finite or |v| > 1e7 corners (`drakan_scene_models.h` `posePositions`), but finite slivers thousands of units long
passed, and a deformable slot refitted them into the structure built for a sane pose (60 refits between rebuilds), which
makes every node's box span the level: every ray crossing them would then visit most of the model. Now
(`ray_scene.h/.cpp`, `ray_water.cpp`, `wgpu_ray.cpp`):

* `screenModelPose` runs over every new model pose before its BLAS build or refit. A triangle with a corner beyond 1e6
  units or not finite, or whose bounding box is longer than 32 times the mesh's (lower) median triangle span and longer
  than 2048 units, is made inactive (x = NaN, which DXR skips; on WebGPU it becomes a point at the origin, since the CPU
  BVH needs finite corners). A 1500-unit spear on a 10-unit-triangle body stays.
* `modelRefitFits`: a deformable slot is refitted only if neither the new pose nor the slot's last full build has inactive
  triangles (an update cannot change which are active) and the pose's extent and summed triangle box area stay within
  twice the build's (plus 64 units). Otherwise it is rebuilt in place. A translated pose still refits.
* The status line adds "(N rebuilt for a changed shape, M stretched triangles left out)" when either is non-zero, and a
  `[ray-water] model <id> (...): N stretched or invalid triangle(s) left out ...` line at most every 5 s.

Tests: `gsr_test_scene_geometry` (screening: spike, NaN, infinity, out of range, grown pose, moved pose, long spear) and
`gsr_test_ray_water` (a refit to a pose with one corner 400000 units out is rebuilt with that triangle left out, and the
next sane pose is rebuilt and renders like the fresh build; D3D12 path).

In game with the fix (private exe `build/review/raywater-hang/gbin/drakan.exe`, HEAD plus this change, linking a copy of
`build/local-game`'s `ps2_generated.lib`; run `rwh-fix`, owner's ini and pack, scale 8, texture upscale, no load): the
opening through 1.3 and 2 minutes of play with no watchdog line, no handoff timeout and no slow pass; GPU ray pass at most
28 ms, heartbeat at most 2.07 s. No triangle was left out (no false positive on the converted models); 37 of 62,427 slot
updates were rebuilt for a changed shape instead of refitted. The pass thread's model step averages 2.42 ms per pass
against 2.40 ms before (`rwh-full`, 8058 passes).

## Open

* The fix does not stop the freeze from an external GPU load (nothing in the game can); it only makes sure a broken
  pose cannot cause one on its own. `rwh-burst2` was not repeated with the fixed exe for that reason.
* The owner's exact pack (18:49 build, authored Delon) is gone from `dist`; `rwh-cowner` rebuilds it from the f4f0e66a
  kit, which is the same converter input.
* Julian's look ("very broken") is a mesh question for the session reviewing his model, not this hang.
