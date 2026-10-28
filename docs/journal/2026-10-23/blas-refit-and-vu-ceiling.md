# Model BLAS refit and the effects-off ceiling (23 October 2026)

Follow-up to [effects-gs-worker](effects-gs-worker.md) and [cpu-ceiling](cpu-ceiling.md).

## Animated model BLAS are refitted

Animated native models get a new `SceneModelMesh::identity` for every pose (same `topology`, same triangle count), so
`RayWater` used to upload each pose into a new committed vertex buffer and build a new BLAS (another committed
resource) every frame.

Now (`ray_water.cpp`, `Impl::Deformable`):

- Meshes with a nonzero topology use a pooled BLAS per topology, built with `ALLOW_UPDATE`. A new pose takes a slot of
  its topology with the same vertex count whose current mesh is not used by this pass and refits it in place
  (`PERFORM_UPDATE`, source = destination). Passes run in order on one queue, as the per-frame dynamic BLAS and the TLAS
  already assume. Every 60 refits a slot is rebuilt in place to bound the loss of trace quality; slots unused for 120
  passes are released.
- New model positions go into the frame's upload buffer (`Frame::models`); a built BLAS does not read its vertices.
- The status line reports the cumulative refit count (`GSR_WATER_LOG`).

Test: `gsr_test_ray_water` poses a mesh A, B, A with new identities and the same topology; the refitted A matches the
fresh build exactly, and the status reports 2 refits (D3D12 debug layer on). In game (Surdana, installed settings):
~10,300 refits in one run, no D3D12 errors.

Installed settings (DLSS SR + NR, ray water/reflections), still view, quiet machine: 79.1-79.5 -> 84.0-84.1 FPS (three
runs each).

## The effects-off ~240 is the VU1 worker, not presentation

`cpu-ceiling.md` attributed the ~238 world FPS cap to each uncapped frame waiting for the host to present it. With
`PS2X_GS_PACING=2` and `DRAKAN_RUNTIME_PROFILE=1` (effects off, still view, 238 FPS):

- Host: `waitMs` 1910 of every 2000 ms waiting for a new latch, `presentMs` 84; presenting is cheap.
- GS worker: `handoff` (waiting for the present) ~90 ms per 2 s, `workerIdle` ~420 ms per 2 s; the EE never waits on
  it (`commitRetirement` 0.04 ms).
- `PS2X_PERF` per-thread CPU: the VU1 worker at 96-100 % (756 M pairs/s), GS worker ~80 %, EE ~75 %.

Removing the present handoff therefore cannot raise the rate; the VU1 worker is saturated. The monitor here runs at
60 Hz and tearing presents are supported, so 238 being close to 240 is a coincidence. Further effects-off gains need
faster VU1 blocks or VU1 work on more cores (the existing `DRAKAN_VU_BATCH` path measured slower than serial on
2026-10-19; see `docs/reference/perf.md`).
