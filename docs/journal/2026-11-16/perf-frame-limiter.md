# What limits the frame at about 185 fps (2026-11-16)

Follow-up of [perf-vertex-path-gs.md](perf-vertex-path-gs.md) section 3 and 4: with `DRAKAN_VERTEX_PROCESSING=gpu` and
`GSR_MODERN_WATER=0` the frame reached about 185 fps in Surdana at the owner's settings and then cpu and gpu came out
level (5.27 to 5.29 ms in every repeat). Settings: a copy of the owner's ini (`build/review/perf-cpu-jitter/user.ini`:
borderless 3840x2160, scale auto = 8, vsync 0, frame_limit 0, world_rate uncapped, anisotropic 16, HD textures, DLSS
off), HD textures from `dist/DrakanPC/textures`, Surdana start, `scene_ab_bench.py --foreground`. Ryzen 7 9800X3D,
RTX 5080. Evidence: `build/limiter/` of the worktree (untracked). Two other agents built and benchmarked on the machine;
the bench recorded what ran before and during every run, and only the runs marked quiet are used below.

## 1. The limiter: the GPU

| check | result |
| --- | --- |
| `nvidia-smi` once a second during one gpu run (`smi1`) | 92 to 100 % utilisation through the whole gameplay window at 2.8 GHz |
| internal scale (`scale1`, gpu, 2 interleaved repeats) | scale 8: 5.29 ms (5.25 to 5.33); scale 4: 4.27 ms (3.75 to 4.78); scale 2: 4.34 ms (3.81 to 4.87). Only scale 8 sits on a fixed frame time; below it the time varies with the CPU side |
| settings (`gbis1`, gpu, one run each) | `GSR_ANISO=1` 5.26 to 4.36 ms; HD textures off, texture upscaling off, FPS counter off: within 0.1 ms; `sharp_present = 0` 5.15 ms (single runs) |
| raw GPU marks (`GSR_GPU_PASS_TIMING=2`, `raw1`) | per gameplay frame about 4.9 ms of the game's own drawing, 0.08 ms display, 0.23 ms window blit: the whole 5.2 ms interval is GPU work |
| CPU at that point (`final1`, gpu, 3 repeats) | EE 3.25 ms, VU1 worker 3.41 ms, GS worker 3.26 ms per world frame of 5.31 ms: no thread above 65 % |

The fixed 5.27 to 5.33 ms is the GPU drawing the world at 5120x4096 (scale 8). The largest single part found is the
anisotropic filtering of the pixel shader (`sampleTexture` in `gs_shaders.h`): at `anisotropic = 16` a minified,
mip-mapped, Q-driven pixel takes up to 16 taps, each a manual bilinear (four `Load`s, eight with trilinear levels). In
one interleaved run (`final1`, below) `GSR_ANISO=1` took the gpu frame from 5.31 to 4.39 ms (-17.3 %, 3 of 3 pairs).
That is a picture setting of the owner (16 taps against none) and was not changed.

Two side findings of the raw marks: the world phase marks come out as `*world:0.000 ... world:0.000`, so the world
raster is booked to the pass carried over from the previous present (`overlay`) and the `GPU world ms` column of the
bench is not the world raster. The `presents/s` column of the bench is the EE's `flips/s` (about 1.25 per world
frame), not presents; `presentLatched` calls equal the world frames.

### How the GPU limit shows on the CPU side

Every frame waits for its own present, and the present blocks while the GPU queue is full:

* `presentCall` (the DXGI `Present` alone): 0.51 ms per call with cpu, 0.75 ms with gpu (`prof1`, `GSR_PROFILE`);
  2.03 ms with modern water on (`water1`).
* The GS worker waits for that present after each display commit (`waitForHandoff`, `DRAKAN_RUNTIME_PROFILE`):
  0.47 ms per world frame with gpu at 185 fps (`prof1`), 2.15 to 2.28 ms per world frame with modern water on at 118
  fps (`water1`). The EE in turn waits for the GS worker's commit retirement (`commitRetirement` 0.73 ms, water on 2.2
  to 2.6 ms per world frame).
* Host samples of the GS worker (`PS2X_HOSTPROF_STACKS_THREAD=GSWorker`, new): 17 % of its wall time in the handoff
  wait and 13 to 14 % in waits taken directly in `workerMain` (the renderer lock while the window thread presents,
  the fairness wait).

So the serialisation the CPU profiles show (GS worker, then present, then the next frame) is the GPU's back-pressure,
not a CPU limit. With water on the frame time is fixed too (8.43 to 8.46 ms in three repeats) and `Present` takes 2 ms:
the water and ray passes make it GPU-bound at 118 fps.

## 2. The EE thread under gpu

perf-vertex-path-gs.md section 3 had the EE at 3.50 ms per world frame with cpu and 4.29 ms with gpu, in separate runs.
Inside one run, with the vertex path switched live (`--cycle video.vertex_processing=cpu,gpu`, `final1`, 3 repeats) it
is the same: 3.29 ms (3.27 to 3.55) cpu, 3.25 ms (3.24 to 3.47) gpu. Across separate runs the EE figure moved by more
than that (`prof1`: 4.02 against 4.54; `hp1`, with the sampler on: 6.03 against 5.24, the opposite way), so the rise
was run-to-run variation, not the vertex path.

What does run on which thread:

* The hand-over of a run to the shader (`vu1gpu::live::beforeRun` / `afterRun`) is called from the MSCAL / MSCNT
  callbacks, which run inside the VIF1 jobs of the VU1 worker (`vu1async`), not on the EE.
* The VIF1 interpreter's `noteUnpack` / `noteWrite` run on the VU1 worker too. On the EE only the guest's own writes
  into VU1 data memory call `directAccess` / `directWrite` while `following()`; they do not appear in the EE samples
  (all runtime hooks together 2.3 % self in both modes, `hp1`).
* The EE samples differ in where the thread waits: with cpu 33.5 % of its wall time is `vu1async::syncSlow`, reached
  from the flip worker `0x1d78f0` reading the VIF1 / GIF status (`readIORegister`), i.e. it waits for the VU1 worker;
  with gpu that is 4.4 % and the wait moves to the commit retirement in `GsrBridge::commitDisplay` (4.0 %). The game's
  own work per frame is the same (the terrain draw `hookTerrainDraw` 2.63 ms with cpu, 2.25 ms with gpu per frame in
  the sampled runs).

## 3. Changes

* **Present overlap, opt-in** (`GSR_PRESENT_OVERLAP=1`, `gs_gsr_bridge.cpp`). After a native display commit the GS
  worker goes on with the next frame's records and waits for the present of that commit only before the next record
  that can latch a display (a display commit or a vblank). The latch is a copy (`latchDisplay` composes the display
  image), so the next frame's draws do not change what is shown, and every latched frame is still presented. Off by
  default, because it does not pay at these settings (section 4).
* **No clock reads per GS record without the frame trace.** `workerMain` read `steady_clock::now()` twice around every
  record (about 2,400 a frame at 185 fps) to report records over 8 ms, which only `DRAKAN_FRAME_AUDIT=2` prints. The
  reads are now taken only while that trace is on. The behaviour is unchanged. The bench always sets
  `DRAKAN_FRAME_AUDIT=2`, so this saving was not measured; in the host samples `now()` was 2.7 % self plus 1.6 % in
  ntdll of the GS worker (with `GSR_PROFILE=2` adding its own reads).
* **`PS2X_HOSTPROF_STACKS_THREAD=<name>`** (`ps2_perf.cpp`): the call chains of `PS2X_HOSTPROF_STACKS` from the thread
  that named itself so (`GSWorker`, `VU1Worker`) instead of the EE thread, for `scripts/perf/hostprof_stacks.py`.

## 4. Measured inside one interleaved run

`final1`: owner's ini, `GSR_MODERN_WATER=0`, the vertex path switched live between cpu and gpu in every run (the
order rotates per repeat), variants by environment, 3 repeats, 12 runs, all quiet (no other game, replay tool or
compiler before or during any run). Frame = median interval of the world flips; CPU per world frame.

| path | variant | world fps | frame ms | 1 % low ms | EE | VU1 | GS |
| --- | --- | --- | --- | --- | --- | --- | --- |
| cpu | base | 183.6 | 5.26 (5.24 to 5.27) | 7.7 | 3.29 | 5.20 | 4.17 |
| cpu | overlap | 189.6 | 5.19 (5.17 to 5.20) | 7.1 | 3.16 | 5.01 | 3.88 |
| cpu | aniso 1 | 187.5 | 5.07 (4.99 to 5.13) | 7.7 | 3.21 | 5.16 | 4.06 |
| cpu | aniso 1, overlap | 198.0 | 4.88 (4.81 to 4.91) | 6.6 | 3.08 | 4.89 | 3.87 |
| gpu | base | 188.0 | 5.31 (5.12 to 5.32) | 5.9 | 3.25 | 3.41 | 3.26 |
| gpu | overlap | 188.3 | 5.30 (5.27 to 5.30) | 6.8 | 3.58 | 3.61 | 3.28 |
| gpu | aniso 1 | 227.7 | 4.39 (4.38 to 4.39) | 5.2 | 3.17 | 3.31 | 3.12 |
| gpu | aniso 1, overlap | 228.1 | 4.36 (4.36 to 4.38) | 5.8 | 3.37 | 3.44 | 3.36 |

* gpu at the owner's settings: overlap -0.2 % (2 of 3 pairs), 1 % low 5.9 to 6.8 ms: nothing gained, the GPU limits.
* cpu: overlap -1.4 % (3 of 3 pairs, ranges disjoint); there the VU1 worker (5.0 to 5.2 ms of a 5.2 ms frame) and the
  hand-off are in series, and the hand-off is the part overlap removes.
* gpu with aniso 1 is still at a fixed time (4.36 to 4.39 ms, GPU total equal to it): the GPU again.

Modern water on (`water1`, the owner's real setting, 3 repeats; one overlap run had a replay tool and two compilers
during it):

| variant | world fps | frame ms | 1 % low ms | 0.1 % low ms | EE | VU1 | GS |
| --- | --- | --- | --- | --- | --- | --- | --- |
| base | 118.1 | 8.44 (8.43 to 8.46) | 10.0 | 11.9 | 4.86 | 5.59 | 5.97 |
| overlap | 95.9 | 9.75 (8.96 to 10.96) | 18.6 | 29.3 | 7.35 | 7.59 | 9.11 |

Overlap is slower with water in 3 of 3 pairs (+15 %): the present then runs beside the next frame's drawing, the GS
worker waits for the renderer lock instead (`fairWait` 0.3 to 1.6 ms per frame) and the CPU per frame of every thread
rises. So it stays off.

## 5. Tests

| what | result |
| --- | --- |
| ctest `runtime-tests` | 206 of 206 passed (7 data-dependent tests skipped) |
| `python scripts/run_tests.py` | 562 tests OK (2 skipped) |
| clang-format on the two changed files, `scripts/repository_guard.py` | clean; 0 prohibited |
| game build (`Build-LocalGame.ps1 -Incremental`) and the 42 bench runs of this note | built, every run exited 0 |

## 6. Open

* The GPU limit at scale 8 and anisotropic 16: the anisotropic taps of `sampleTexture` are the largest part found
  (-17 % frame time without them). Not changed: an exact cheaper form was not attempted (the hardware sampler's
  bilinear weights are not the shader's float weights, so it cannot simply take over), and no GPU profiler was used to
  split the shader's cost further. Options with a different picture (fewer taps at high internal scales, where a
  pixel is already an eighth of a native pixel) are the owner's choice.
* The world phase marks of the GPU pass timer book the world raster to `overlay`; the bench's `GPU world ms` column is
  wrong for that reason. Not fixed.
* With water on the frame is GPU-bound at 8.44 ms; which of the water and ray passes costs what on the GPU was not
  measured here (see perf-water-spikes.md for the CPU side).
* The saving of the clock-read change was not measured (the bench forces the frame trace on).
