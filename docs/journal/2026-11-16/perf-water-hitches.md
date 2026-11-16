# Modern water: the level-start hitch, texture re-uploads and the static repack (2026-11-16)

Follows [perf-cpu-jitter.md](perf-cpu-jitter.md). The owner reported lag and frame drops at level changes with modern
water on. Measured with the owner's settings (3840x2160, scale 8, uncapped, modern water on), Surdana, D3D11.
Evidence: `build/review/perf-water/`, `build/harness/water-walk*`, `build/harness/warm-*` (untracked).

## 1. What the pass thread spends

`GSR_WATER_LOG=1` (every pass) and `GSR_PROFILE=2` (the pass thread's `ray*` buckets), Surdana start, 2,184 passes:
first pass after the level loads 190.1 ms, passes 2 and 3 about 20 ms, then median 3.9 ms, p99 7.9 ms. The first
pass: `rayPack` 87.6 ms (`rayTriangles` 68.6 ms: normal smoothing and packing of the 141,133 resident triangles;
`rayModelAttributes` 19.0 ms for 1,208 model instances), the rest D3D12 uploads (246 material textures) and
acceleration structure builds. D3D11 waits for every pass on the GPU, so the first pass stopped one frame for about
200 ms: a 260 ms and a 263 ms frame in two runs.

Walking around Surdana for 20 s: passes of 17 to 27 ms at irregular times. Most coincided with 79 to 199 material
textures marked changed in one pass.

## 2. Changes

* **Texture slots.** The ray pass uploads a texture slot again whenever its occupant changes. (1) The model textures
  followed the frame's own textures, so every new frame texture moved all of them by one slot; their base now moves
  in steps of 64, the gap holds one constant placeholder texture uploaded once (`RayWater::submit`). (2) The scene
  models' texture list was rebuilt in resource id order, so a newly resident texture with a lower id moved every
  later one; textures already in the set now keep their order and new ones follow (`drakan_scene_models.h`,
  `refreshTextures`). Same walk: textures re-uploaded 1,161 (15 passes) before, 846 after (1), and after (2) only
  passes of 1 to 8 textures besides the first and one 64-boundary crossing.
* **Static attribute repack.** Material projections change a few layers while the camera moves, and the pass compared
  all 141,133 resident triangles with the previous snapshot to find them. The resident scene now keeps a change log
  (`ResidentScene::changedSince`, `forgetChangesUpTo`; versions only grow, cleared at 262,144 entries) and the
  renderer passes the changed indices with each publication (`RayWater::shareStaticWorld(..., changed)`); the pass
  repacks only those when its packed version is the list's start and the texture count is the same, else it compares
  every triangle as before. `GSR_RAY_CPU_CACHE_CHECK=1` (a full pack compared with the incremental one, throws on a
  difference) over a 286-pass walk: no difference. `rayTriangles` 3.34 to 2.61 ms per pass (one walk each; the bucket
  also covers the per-frame triangles). The water triangle count of a publication is taken only when its geometry
  changes (projection changes texture and UV only).
* **Warm start** (`GSR_RAY_WARM_START`, default on). The first pass of a new static world is queued without D3D11
  waiting for it; the frames bypass the pass (plain raster water, as while the ray pipeline compiles) until the pass
  thread's completion fence passes the warm job's value, then passes resume with the static world, normals, model
  structures and textures already built. `RayWater::render` (synchronous) never warm-starts.

## 3. Result at the level start (New Game into Surdana, 12 s, `DRAKAN_FRAME_AUDIT=2`)

| run | the four longest frames after the first pass (ms) | first pass on its thread |
| --- | --- | --- |
| warm start on | 19, 21, 23, 38 | 196.9 ms |
| warm start off (`GSR_RAY_WARM_START=0`) | 20, 21, 21, 260 | 203.9 ms |
| warm start on | 49, 55, 58, 62 | 2,936 ms (recording D3D12 commands 576 ms and more; the game ran beside it) |
| warm start off | 24, 28, 33, 263 | 254.8 ms |

The 260 ms frame is gone. In one warm run the pass itself took almost 3 s while the game kept running at full rate
and several frames reached 50 to 62 ms; the water showed the plain raster for those 3 s.

## 4. Open

* The static world is rebuilt (positions and BLAS) whenever the camera is more than `kRebaseDistance` (16,384 units)
  from its base; on foot this did not show as a spike, flight was not measured.
* The pass thread under load (the 2,936 ms warm pass): its priority and what the recording stage waits for.
* The remaining 17 to 23 ms passes while walking with no texture uploads were not attributed.
