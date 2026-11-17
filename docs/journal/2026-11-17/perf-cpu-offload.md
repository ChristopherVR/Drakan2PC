# CPU per frame at the owner's settings: baseline, first cuts, the water switch (2026-11-17)

Brief: cut CPU per world frame on the EE, the VU1 worker and the GS worker (water and ray-surface code excluded,
another agent owns it), ship work to the GPU where it fits, and find out why `GSR_MODERN_WATER=0` was reported not to
turn water off over an ini with `modern_water = 1`. The session was stopped early by the coordinator (wrap up), so
this note is a baseline, a profile, four small changes and a list of what was not measured.

Setup: the owner's ini (`build/review/perf-cpu-jitter/user.ini` of the main checkout: 3840x2160 borderless, scale
auto = 8, vsync 0, uncapped, HD textures through `GSR_TEXDIR`), copies with `modern_water = 0` and `= 1`
(`build/review/cpu-offload/user-water0.ini`, `user-water1.ini`), both mod packs, `scene_ab_bench.py --quiet-games 0
--lead 10 --settle 4 --window 10`, background window. Ryzen 7 9800X3D, RTX 5080. Evidence and logs:
`build/review/cpu-offload/` of the main checkout (bench folders `bench/base-*`, `bench/waterenv*`,
`bench/prof-canyons`, `bench/slowmem-canyons`). Scratch tools (not tracked): `build/scratch/` of the worktree
(`scopes.py` per-world scope and thread table, `toggle.sh`, `baseline.sh`).

## 0. Conditions, read first

* **Display:** console session (not Remote Desktop); `dxdiag` lists both monitors, the Dell U2720Q at 3840x2160 60 Hz
  among them, on the RTX 5080. WMI lists the 4K mode under the AMD iGPU, as on 16 November; `dxdiag` is the one to
  trust.
* **The GPU was not ours:** `nvidia-smi` read 92 to 97 % utilisation before every run, from another GPU program on the
  machine (a UE game, not a `drakan.exe`, so the bench's quiet check did not see it). `presentCall` took 6 to 19 ms
  per world in some windows. **Frame times and lows of this note are not results**; only CPU time per world frame is
  used, and even that carries some of the waiting (threads that spin while the GPU is late).
* **No DLSS in the baseline:** the first build of the worktree had no DLSS SDK (`[gsr] upscaler unavailable: DLSS
  support not compiled in`), so both windows of `--cycle video.dlss=quality,off` ran with DLSS off. The later builds
  pass `-DlssSdkDirectory <main>/tools/dlss/dlss_sdk` (a worktree has no `tools/`). The two windows of each run are
  listed as two DLSS-off windows below. A DLSS quality baseline was not taken.
* Compilers of another agent ran during two canyon and one Ruined Village run (`load_peak` in `runs.json`).

## 1. Baseline: CPU per world frame (ms), DLSS off, main at 1880c35b

Median of 3 runs (Surdana with water 2 runs; the water-on series of the canyon and the Ruined Village was stopped for
the DLSS rebuild), two 10 s windows per run.

| scene | water | EE | VU1 worker | GS worker | all other threads | process |
| --- | --- | --- | --- | --- | --- | --- |
| Surdana start | off | 5.51 / 6.05 | 7.05 / 7.45 | 5.51 / 5.75 | 19.0 / 20.1 | 24.5 / 26.1 |
| Surdana start | on | 5.82 / 6.32 | 6.05 / 6.84 | 6.59 / 6.82 | 38.3 / 36.1 | 44.1 / 42.4 |
| Wartok Canyons start | off | 17.1 / 15.7 | 15.2 / 13.8 | 15.0 / 13.5 | 37.8 / 48.2 | 55.0 / 61.7 |
| Ruined Village (Order of the Flame start) | off | 14.5 / 16.1 | 6.5 / 7.3 | 6.9 / 7.4 | 33.8 / 32.9 | 48.3 / 49.0 |

"all other" is every thread of the process but the EE (it includes the VU1 and GS workers, the audio thread, texture
workers and, with water, `SurfaceBuilder` and `RayWater`); process = EE + all other. Modern water adds about 18 ms of
CPU per world frame in Surdana, nearly all outside the three main threads.

GS worker scopes (`GSR_PROFILE`, inclusive ms per world, same runs):

| scene | `draw` | `resolveTexture` | `textureCoherenceFlush` | `syncRT` | `drawSubmit` | `drawVertexUpload` | draws per world |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Surdana, water off | 1.46 | 0.51 | 0.06 | 0.14 | 0.13 | 0.49 | about 200 |
| Wartok Canyons, water off | 5.81 | 2.82 | 1.29 | 1.11 | 1.06 | 0.84 | about 2,200 |
| Ruined Village, water off | 2.12 | 0.96 | 0.27 | 0.38 | 0.32 | 0.41 | |

So in the canyon half of the GS worker's draw time is the texture resolve (about 1.5 us per draw), of it the
coherence flush (one pass over the render targets per mip level and one for the alias search) and the target row
refresh check; the D3D11 submit is 0.5 us per draw.

## 2. Sampled profile, Wartok Canyons, water off, DLSS quality (`PS2X_HOSTPROF_BUSY=1`, 25 s, `bench/prof-canyons`)

Wall-clock samples, not CPU time; shares of each thread's samples (the verification of section 3 was on, which
inflates `hashClut`).

* **EE thread:** the guest's memory accesses that leave the generated code's RAM fast path (scratchpad, I/O
  registers): `translateAddress` 5.1 %, `inRange` 3.3 %, `writeIORegister` 2.5 %, `read32` 2.4 %, `write32`, `Ps2Wr32Slow`,
  `Ps2Rd32Slow`, `Load32`, `Store32` 1 to 2 % each, the DMA completion bookkeeping (`consumeCompletedDmacCauses`,
  `drainCompletedDmacHandlers`) 1.9 %: about a fifth of the thread. `__security_check_cookie` 2.4 % (generated code is
  built with `/GS-`; this is runtime code). The posed capture 1.2 %, the object-motion tracker 1.1 %.
* **VU1 worker:** `vu1async::Worker::run` 16.8 % exclusive (its loop and waits), the lane executor and block functions
  of the microprogram, `processVIF1Data` 4.2 %, the VIF unpack 4 %. No single hot spot.
* **GS worker:** spread over the GS core (`gifSubmit`, `kick`, `handlePacked`, `emitTri`, `toDV`: about 1,800 of
  8,800 busy samples, the GIF packets turned into draw vertices), `draw` 4.2 %, `loadClut` 4.1 % (every converted pass
  loads its own palette), `hashClut` 3.8 %, `readRectCT32` 2.6 % (target rows refreshed from memory), texture uploads
  into GS memory (`writeT8Rows`, `pixelAddr`) 3.6 %.

## 3. What was changed

* **`GSR_MODERN_WATER` and the other ray variables keep deciding over a live change** (`drakan_settings_host.cpp`
  `applyVideo`, default, a fix). Finding: the environment variable *does* win at start and through a live DLSS cycle
  (4 runs, `bench/waterenv*`: ini 1 + `GSR_MODERN_WATER=0` runs without the `SurfaceBuilder` and `RayWater` threads, 16.6
  ms of other-thread CPU per world against 27.9 ms with the ini alone; ini 0 + `GSR_MODERN_WATER=1` turns water on).
  The gap: the four water and ray settings share one setter, so a live change of any of them (`ray_shadows`,
  `ray_reflections`, `ray_gi` or `modern_water` itself, from the menu or `--cycle`) applied the ini's values for all
  four and turned water back on over `GSR_MODERN_WATER=0`. Now each of the four takes its environment variable when it
  is set, as `attachRenderer` does at start. Benches can trust the variable unless they cycle a water key, which the
  variable then overrides.
* **Texture resolve and target refresh shortcuts, opt-in `GSR_CPU_CUTS=1`** (GS worker, `gs_texture_cache.inl`,
  `gs_targets.inl`): (1) a cached texture asks the render targets once with the union of its mip spans; when no dirty
  target overlaps it, the alias search and the per-level flushes (which would each have found nothing) are left out;
  (2) `syncRows` asks the whole row range once against the oldest row stamp before the per-row loop; (3) the palette
  hash of the texture cache key runs four FNV lanes instead of one chain of 256 dependent multiplies (a key only).
  `GSR_CPU_OFFLOAD_VERIFY=1` does the old work beside each shortcut: Wartok Canyons, DLSS quality, 6,600 frames: target
  refreshes skipped 363,894 (0 differ), texture flushes skipped 8,160,106 (0 differ), palette hashes 7,349,912 (0
  collide, every new hash stood for one old hash). Same decisions, so the same picture; the gain was **not measured**
  (no same-run toggle bench was run before the wrap-up), so it stays off. `GSR_CPU_OFFLOAD_TOGGLE=<s>` switches it
  old/new inside a run ("[cpu-offload] now old|new"; `build/scratch/scopes.py --toggle cpu-offload` splits the log).
* **Page-run stamps in GS memory** (`gs_mem.h`, always on): `maxStamp` also keeps the newest write of every 32-page
  run, so a whole target range costs one read per run. `test_mem_stamp` now also checks whole-target ranges and ranges
  up to all of memory and past its end against the block-by-block maximum.
* **The guest memory bounds check inlines** (`ps2_memory.cpp` `inRange`, always on): the throw that builds a string
  moved to a `[[noreturn]]` out-of-line function. Same behaviour; the EE-side gain was not measured.
* Tools: `GSR_TERRAIN_BATCHING_TOGGLE=<s>` switches `[video] terrain_batching` live at the vblank (for a same-run
  CPU measurement of the existing joined terrain draws, which the 14 November A/B judged by frame time only; not run);
  `DRAKAN_SLOW_MEM_STATS=1` counts the guest accesses that leave the RAM fast path by region (`[slow-mem]`).

## 4. Tests

* runtime-tests: 207 of 207 pass (`mem_stamp` with the new ranges included).
* `python scripts/run_tests.py`: 563 tests, one error in `test_package_builder_kit` (`git archive` of the
  `editor/packages/formats` submodule commit 86f5ea32, which the worktree's submodule clone does not have: that commit
  is not on the submodule's remote yet; the worktree build used a file copy of the main checkout's submodule). Not
  related to these changes.
* `scripts/repository_guard.py`: 0 prohibited. clang-format 19 on the changed `.cpp` and `.h` files.
* The game build with `GSR_CPU_CUTS` on by default (before it was made opt-in) ran the canyon verification of
  section 3; the final opt-in state was built for the runtime tests, not rebuilt as a game.

## 5. Not measured, next

* The CPU gain of `GSR_CPU_CUTS`, of the inlined bounds check and of terrain batching per world frame (same-run
  toggles exist for the first and the last), all with DLSS quality, on a machine without another GPU load.
* A DLSS quality baseline (the DLSS-off table above is the only one), and the water-on canyon and Ruined Village.
* EE: which region the slow accesses hit (`DRAKAN_SLOW_MEM_STATS`), and a direct scratchpad path in
  `PS2Runtime::LoadN/StoreN` if it is the scratchpad (exact when the alias watch is off and the access is aligned).
* GPU offloads named in the brief and not started: the GIF-to-vertex expansion of the GS core on the GPU (the largest
  GS worker share in the canyon), palette conversion on the GPU, the velocity-skip box test and model projection in a
  shader, the posed build on the builder thread.
