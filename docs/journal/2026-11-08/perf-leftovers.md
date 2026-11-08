# Performance leftovers: shader compile stalls, 4B spot C, polyclip preemption, Tracker::freed (2026-11-08)

Follow-up of `docs/journal/2026-11-07/outstanding-work.md` section 4 (items 1, 4, 5) and `docs/journal/2026-11-07/fps-4b.md`.

## Method

* Own exe copies under `build/perf14/` (never `dist/`): `play` (exe of 7 Nov 18:48, the "before"), `new`..`new5` (each
  build of this session) and per-run "cold" folders made with hard links (`build/perf14/cold.sh`), so every cold run
  has a new exe path (cold NVIDIA driver cache for that path) and, unless noted, an empty `shadercache`.
* Every run went through `build/perf14/run.sh`: wait until `tasklist` shows no `drakan.exe` on two checks 20 s apart,
  optionally start N CPU hogs (`python -c "while True: pass"`, killed by PID afterwards), then `play_run.py --level 4b`
  with `PS2X_PERF=1 DRAKAN_PLAYTEST_GOD=1 DRAKAN_RUNTIME_PROFILE=1`. The `*.others` file of each run records the
  count of other games at start (0 for every run below) and at the end. Recipes: `recC.txt` (place at spot C 751616,
  140546, 1224542 for 28 s), `recWalk.txt` (24 walk / turn / attack legs with 4 screenshots).
* Spread: three identical quiet runs at spot C gave world rates of 50, 96 and 91 flips2/s (`ee_ms/flip` 9.6, 8.2, 8.2),
  so single fps numbers are not comparable at the 5 percent level on this shared machine. The profile shares below
  are the stable measure.

## 1. Shader compiles that block the guest

What the old exe did (logs `build/harness/perf14_bq2`, `bh`, `bh48`):

* The 12 uber shader variants (`kHlslMain`, `D3DCompile` at O3) were compiled one after another in `Impl::init`:
  10 to 13 s of CPU on the main thread before the first guest frame, about 225 s of wall time with 48 hogs (the first
  `runtime-profile` window covers 224.7 s against 152 to 166 s for the new exe under the same load).
* Everything else was compiled or created on the GS worker the first time a frame needed it, while the EE waited on
  its commit. With the new exe and `GSR_SHADER_PREWARM=0 GSR_SHADER_WARM_DRAWS=0 GSR_SHADER_CACHE=0` (the old path,
  now with timing) the log shows the cost: `[gsr-shader] late ...` 1276 ms per launch quiet with 16 hogs (`ahoff`) and
  1546 ms with 48 hogs (`ah48off`), of which the DXR ray tracing device and its three compute pipelines are 1173 /
  1432 ms (at the first world frame), the NR guide, SR, readback, movie and backdrop shaders the rest.
* No `[perf-stall]` line in any run on this machine, also with 48 hogs: the 13 to 30 s first-draw driver compile of the
  boot-hang report (`docs/journal/2026-11-06/boot-hang.md`, 12 games plus 16 hogs) did not reproduce with hogs alone,
  and running 12 games is not allowed while other sessions measure. The `[perf-stall]` chain now goes 64 frames deep so
  the exe frames under a driver compile are visible next time.

Change (`4deaf0b0`, plus the title-path fix `1a27a90f` by another session):

* `runtime/gs/src/shader_cache.{h,cpp}`: blob cache, memory then `<exe>/shadercache/<key>.bin` (checksummed, written
  through a temporary file), one compile per key even with concurrent callers. `shader_compile_d3d.{h,cpp}`: the
  `D3DCompile` wrapper every runtime compile now uses, with the late accounting.
* `init`: the 12 variants compile side by side, then every start-up pipeline is drawn once into 8x8 targets and the GPU
  is waited for (drivers translate at the first draw), all before the guest starts.
* A background thread compiles and creates the lazy shaders; another builds the DXR device as soon as a ray effect is
  configured (start-up or the host's first live settings). The first world frame takes the finished object.
  `waterRenderingAvailable` / `waterRenderingStatus` no longer wait for it (the settings menu polls every 0.5 s).

After (new exe):

| Run | load | start-up | compiled / from disk | late work on the GS worker |
| --- | --- | --- | --- | --- |
| `aq` (first build) | quiet, cold | 1106 ms | 16 / 0 | 866 ms (ray device: prewarm only ran when the flag was on at init) |
| `ah` (second build) | 16 hogs, cold | 1780 ms | 16 / 0 | 1128 ms waiting for the ray prewarm (settings host asked at the title) |
| `ah48` (final) | 48 hogs | 2052 ms | 0 / 16 | none; ray device 1959 ms on the prewarm thread |
| `aq2`, `aq2_warm` (final) | quiet | 170 ms | 0 / 16 | none |

Unit test: `runtime/gs/tests/test_shader_cache.cpp` (`ctest -R shader_cache`): key stability, file validation
(truncated, flipped, wrong key, empty), one producer for 8 concurrent callers, failures not cached, disk round trip and
recovery from a corrupt file. The D3D11 tests that compile the shaders standalone (`rt_readback`, `sr_sharpness`,
`nr_guides`, `world_readback`) pass with the wrapper.

## 2. Northern Tundra spot C: where the EE thread goes

`PS2X_HOSTPROF=20 PS2X_HOSTPROF_DELAY=24 PS2X_HOSTPROF_STACKS=1` (new: unwinds the EE thread's exe frames per sample into
`hostprof_stacks.csv`; `scripts/perf/hostprof_stacks.py` prints inclusive and self cost per function and bucket).
Two runs at C, 6298 EE samples:

* **23 percent of the EE thread is a guest busy-wait, not work.** `f_0x1dbfa8` (GS/DMA reset-sync, graphics.md 4.1)
  ends in a loop at 0x1dc688 that polls GS CSR (0x12001000) bit 1 (FINISH) with `cpTick(32)` per iteration, before a
  per-frame GS-to-EE transfer (VIF1 FDR, BUSDIR, DMA ch1 normal receive). FINISH is set when the GS worker executes the
  FINISH register, i.e. after the VU1 worker and the GS worker have processed the frame's geometry. The samples inside
  it are `read64` / `Ps2Rd64Slow` (CSR), `checkpointDue`, `accountCycles`, `advanceEeTimers`, IOP `runCycles` /
  `nextEventCycle`, `hostVblankDue` / `steady_clock::now`. So "EE limited at 96 percent busy" is partly pipeline latency:
  the frame is serialised EE build, VU1 + GS workers, readback. Making the poll cheaper only saves CPU, not frame time.
  What would move C: a shorter VU1 / GS worker path up to the FINISH, or not serialising on this readback (needs to
  know what the game reads back; not done).
* Outside that wait: guest generated code self about 35 percent (top `f_0x116990` 3.6 percent), our scene model capture
  about 9.5 percent (`drakan_scene_models::Capture::capture`, `meshContentSignature` hashing via `captureHashWide`,
  `posedMesh`, map inserts, textures), DMA chain assembly about 4 percent (`writeIORegister` growing the chain buffer
  with many small inserts plus allocation), guest memory slow paths outside the poll about 3 percent.
* JALR dispatch (`dispatchGuestBranch`, `lookupFunction`) is 0.0 to 0.1 percent: not a cost.
* Checkpoint / scheduler / IOP time accounting is 12 to 14 percent self, about two thirds of it inside the FINISH wait.

Safe wins made: `Tracker::freed` (below). Tried and reverted: a capacity hint for the DMA chain buffer (the
reallocation share did not drop, `prof3`: insert 2.4 percent, allocation still 1.0 percent; chains of very different
sizes alternate). Not changed: caching `meshContentSignature` across captures (it is the deliberate per-capture
validation against address reuse; needs a design, not a micro fix).

## 3. Polyclip preemption path (4a96154e)

New aids in `drakan_polyclip.cpp` (`dba57f7d`): `DRAKAN_POLY_NEAR_CLIP_GUARD=1` (the hook's own image of the scratch
area at RDRAM 0xf0000; any foreign write and any stack mapping to a slot a preempted draw may still read is reported),
`DRAKAN_POLY_NEAR_CLIP_FORCE_PREEMPT=N` (the first original draw of every Nth clipped fan unwinds at its first
checkpoint), `DRAKAN_POLY_NEAR_TRIGGER=<px>` (lower window trigger so ordinary fans take the clipped path).

* Stratos, natural clips (`pc_guard8`): more than 4096 clipped fans, 0 preempted, 0 scratch violations, 0 collisions.
* Stratos, every clipped fan forced (`pc_force8`): forced 1024, preempted 1023, game continued, no `[bad-resume]`,
  no missing function, no stall, screenshots normal, 0 violations, 0 collisions.
* **Bug found**: with `DRAKAN_POLY_NEAR_TRIGGER=200` the game hung within seconds in 4B (`pcT_ref4b`, no forcing) and 1A
  (`pcT_force1a`): 1.2 million clipped fans per second, no GIF packets, `[perf-stall]` with `hookFanDraw` called directly
  by `EeScheduler::run`. The clipped path returned with pc at its own entry (the fix of 4a96154e kept `c->pc = pc`).
  That is only harmless under `PS2_JAL`; when the scheduler itself calls the hook (resuming a call that unwound at its
  pre-call checkpoint) pc == entry means "call again", so every clipped fan was redrawn forever. In normal play this
  needs a pre-call unwind exactly at a fan that needs clipping (about 1 percent of fans in effect-heavy scenes), a rare
  but real hang. Fixed: a completed clipped fan returns pc = ra like the original's `jr ra`
  (`pcAfterClippedFan`, static_assert in the policy test, `343ce163`). With the fixed exe the same stress
  (`pcF_ref4b`, trigger 200, guard on) plays the 87 s walk recipe to its scripted quit: 454,656 clipped fans, 1 natural
  preemption, 0 scratch violations, 0 collisions, no `[perf-stall]`, world rate 115 to 177 flips2/s.
* Fixed exe, trigger 200 plus forcing every second clipped fan (`pcF_force4b`): 468,992 clipped, 234,468 forced, 33,416
  preempted inside the original, 28 unwinds at the hook entry, played to the scripted quit, no `[perf-stall]` /
  `[bad-resume]` / missing function, screenshots normal, 0 scratch violations. The guard reported 2 possible slot
  collisions (another guest stack, sp 0x1fbf3a0, mapping to a slot pinned by a preempted draw of a different stack;
  slots alias every 16 KB of stack). The guard cannot tell whether the pinned draw had already finished, so this is a
  conservative warning, but it shows two guest threads do clip fans. Open, cheap: pick the next free slot instead of
  the aliased one while a slot is pinned.
* The scratch area: no foreign write in any guarded run (4B, 8, 1A, including forced preemption), so nothing else used
  0xf0000..0xf7fff in those runs.

## 4. Tracker::freed

`object_motion_tracker.h` (`48dd00dc`): ordered range lookups for lives and object-keyed topologies, a small mesh/part
address index for the rest. A differential policy test (`object_motion_freed_policy`) drives the tracker and the old
linear model with the same random sequence (40 x 4000 steps) and requires identical scopes and topology counts; it fails
if the index lookup is removed. Spot C profile: `Tracker::freed` 2.4 to 4.7 percent of the EE thread before, 0.2 percent
after.
