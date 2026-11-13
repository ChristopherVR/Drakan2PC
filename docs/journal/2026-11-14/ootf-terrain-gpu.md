# Order of the Flame terrain draws on the renderer side: where the per-draw time goes, the draw state cache (2026-11-14)

Round 4, renderer agent. Scratch: `build/review/ootf-terrain-gpu/` (play folders `play` and `play2`, runs, replay
hashes, evidence pictures, tools; not tracked). Owner's real `drakan.ini` (3840x2160 borderless, DLSS quality, HD
textures) copied from `build/review/baseline-play`, a private ELF copy, the installed pack
(`dist/DrakanPC/mods/ootf-levels.zip`). Follows [ootf-performance.md](ootf-performance.md), which found that converted
terrain makes about 5,300 draws per world frame in Wartok Canyons and recommended cheaper draws in the renderer.

## 1. Result in short

* **Where the per-draw time goes.** Split profile of the GS worker (`GSR_PROFILE`, new buckets `drawDest`, `drawBind`,
  `drawPass`, `drawVelocity`, `drawEnd`, `drawConstants`), Wartok Canyons start, quiet machine, old path: a draw costs
  about 1.7 us of renderer CPU without DLSS and 3.4 us with DLSS quality. Of that, the texture lookup (`resolveTexture`,
  0.8 us) is the same either way; the Direct3D 11 submit (`drawSubmit`) is 0.46 us without DLSS and 1.6 to 1.8 us with
  it: the bindings (0.5 to 0.6 us), the velocity pass that DLSS adds after every colour pass (0.5 to 0.7 us, a second
  target, its own programs and states) and the release of the bindings at the end of every draw (0.2 us). The driver's
  own threads ("all other" CPU) add 45 ms per world frame without DLSS and 78 ms with it. The constants upload is
  small (0.05 to 0.1 us); there is no vertex or texture upload in the steady state.
* **What was built: a draw state cache in the Direct3D 11 draw path** (`runtime/gs/src/draw_state_cache.h`,
  `d3d11_draw.cpp`). Every draw used to set about 15 pieces of state and unbind its texture and target at its end,
  whatever the previous draw had bound. Now a slot is set only when its value differs from what is bound, and the
  release at the end of a draw is held until something other than the next draw uses the device context. 87 % of the
  binds of the Ruined Village capture are left out. `drawSubmit` per draw: canyon 0.46 to 0.31 us without DLSS, 1.6 to
  1.8 to 1.1 to 1.2 us with DLSS quality; Ruined Village 0.47 to 0.32 and 1.8 to 0.63 us. The driver threads' CPU per
  world frame falls by 6 to 7 ms in the canyon.
* **Frame time, quiet machine, interleaved, 3 pairs each:** Wartok Canyons DLSS quality 29.05 to 26.17 ms (-9.8 %,
  34.4 to 38.2 fps), DLSS off 17.57 to 16.72 ms (-4.9 %); Ruined Village off 12.07 to 11.90 ms (-1.9 %), with DLSS
  still held at 60 fps (1 % low 28.3 to 25.6 ms); Grimstone Mines quality 10.09 to 9.92 ms (-1.7 %), off 7.44 to 7.30;
  Surdana (232 draws) unchanged within its spread. New faster in every pair of every OotF scene with DLSS quality.
* **Same pixels.** Direct3D 11 capture replays with the cache off and on hash identically frame by frame (frozen
  HD textures; Ruined Village at scale 1, 2, 3 and 8, Surdana, cave, Ravenshold flight and tab menu at scale 3; 30,287
  frames in eight comparisons; after the fix of section 2 the Ruined Village and flight again, identical). The
  replays have no DLSS, so the velocity pass was checked in the game: `GSR_DRAW_STATE_VERIFY=1` reads the whole bound
  state back from the context before every draw call (colour and velocity passes) and compares it with what the cache
  believes; 175.8 million slot checks at the start poses of four scenes with DLSS quality and off, 0 wrong.
  Screenshots before and after look the same (section 4).
* **Default: on** (`[video] draw_state_cache = 1`), because the replays are identical, the read-back check found
  nothing and every OotF scene got faster. `draw_state_cache = 0` (or `GSR_DRAW_STATE_CACHE=0`) is the old path.
* **Not built: joining the terrain splat draws into one.** Consecutive terrain passes differ in their texture, so a
  join needs a texture array or atlas of the ground tiles and a shader that picks per vertex; with the draw state cache
  the per-draw submit is now 0.3 us without DLSS, so the remaining per-draw cost is the texture lookup (0.8 us) and the
  guest work, not the submit. Section 5.
* **The Ruined Village 60 fps with DLSS** (the CPU agent's finding: the window thread's `Present` blocks about 5 ms on
  the GPU while it holds the renderer lock): a swap chain that queues two frames was built behind a setting and
  measured; it changes nothing (still exactly 60 fps, section 3.3), so the setting was removed again. Building the
  next frame while `Present` waits on another thread is not safe with one Direct3D 11 immediate context (not built).

## 2. How the cache works and why it keeps the picture

* `draw_state::Cache` (GPU free) holds one value per slot: input layout, the two vertex buffers with their offsets,
  topology, vertex and pixel program, the constant buffers, the two textures, samplers, rasterizer, viewport, scissor,
  targets, blend and depth state. `set(slot, value)` says whether the backend must make the call. With the cache off it
  always says yes, so the off path is the old call sequence (the velocity pass keeps its extra target unbind).
* The device context is wrapped (`TrackedContext` in `d3d11_backend_impl.h`): every use through `->` or `Get()` outside
  the draw path first carries out a held release (texture slots 0 and 1 and the targets unbound, exactly what the old
  `endDraw` did) and drops everything the cache knew. The draw path itself uses `ctx.raw()`, as do `Map`/`Unmap` and the
  motion target clears, which change no binding. Code that keeps its own pointer to the context (the temporal
  pipeline, DLSS, the combined upscaler) is reached through backend functions that now call `foreignContext()` first.
* A draw that samples a render target is bound from a released state and releases at its end, as before, because the
  device unbinds a texture that is bound as a target (and refuses one the other way round).
* A held release belongs to the next draw once that draw starts binding (`Cache::beginDraw`). Passes that use the
  context in the middle of a draw (the GPU vertex path's programs, material detail) then only drop the known state.
  The first version (commit `a53c87c8`, off by default) carried the held release out at such a use and so unbound the
  draw's own target in the middle of the draw: `gsr_test_vertex_records` failed 59 of 10,050 checks with the cache on
  (GPU vertex mode only). Fixed in the commit that turns the cache on; the unit test's model now has such passes and
  shows the fault without the fix (782 of 3,830 draw calls differ). The owner's settings (`vertex_processing = cpu`,
  material detail off) never use the context in the middle of a draw, so the measurements and the read-back check,
  taken before the fix, are of the same call sequence.
* `GSR_DRAW_STATE_HOLD=0` releases at every draw end (diagnosis), `GSR_DRAW_STATE_CACHE_TOGGLE=<n>` flips the cache
  every n frames (test; the `[temporal-dump]` lines name the state), `GSR_DRAW_STATE_VERIFY=1` is the read-back check,
  `GSR_PROFILE` prints a `[draw-state]` line with binds made and left out.
* WebGPU: the shared `gs_draw.inl` passes the new "samples a target" flag to both backends; the WebGPU backend ignores
  it (its `rhi` layer records a render pass per target change already and has no velocity pass). The gain is Direct3D
  11 only, which is the owner's renderer.

## 3. Measurements

Tool: `scripts/perf/scene_ab_bench.py` with its turn lock, `--cycle video.dlss=quality,off`, variants
`old:GSR_DRAW_STATE_CACHE=0` and `new:GSR_DRAW_STATE_CACHE=1`, 3 repeats, order rotated, one exe (main `02bfbb25`).
Medians over the repeats, ranges in brackets; CPU per world frame. Runs: `build/review/ootf-terrain-gpu/runs/ab1-*`,
`ab1b-ootf-canyons`.

### 3.1 Frame time

| Scene | DLSS | frame ms old -> new | world fps old -> new | pairs | 1 % low ms | EE / VU1 / GS worker ms | driver threads ms | other games |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Wartok Canyons | quality | 29.05 (29.02 to 29.09) -> 26.17 (26.13 to 26.35) | 34.4 -> 38.2 | 3 of 3, -9.8 % (-10.1 to -9.4) | 36.3 -> 35.2 | 20.5 / 17.6 / 23.1 -> 21.6 / 18.4 / 21.8 | 77.6 -> 71.9 | 0 in all 6 |
| Wartok Canyons | off | 17.57 (17.26 to 17.58) -> 16.72 (16.71 to 16.76) | 53.1 -> 55.3 | 3 of 3, -4.9 % (-4.9 to -2.9) | 28.4 -> 28.2 | 18.0 / 16.6 / 18.4 -> 17.8 / 16.3 / 17.3 | 45.2 -> 38.4 | 0 in all 6 |
| Ruined Village | quality | 16.67 -> 16.64 | 59.3 -> 60.5 | 3 of 3, -0.2 % (held at 60) | 28.3 -> 25.6 | 14.5 / 5.3 / 8.8 -> 13.8 / 5.8 / 8.2 | 37.8 -> 32.3 | 0 in all 6 |
| Ruined Village | off | 12.07 (12.03 to 12.17) -> 11.90 (11.84 to 11.94) | 82.4 -> 83.4 | 3 of 3, -1.9 % (-2.2 to -0.8) | 19.6 -> 19.1 | 10.9 / 5.8 / 7.0 -> 10.8 / 5.3 / 6.7 | 23.7 -> 19.7 | 0 in all 6 |
| Grimstone Mines | quality | 10.09 (9.96 to 10.09) -> 9.92 (9.78 to 9.95) | 99.6 -> 101.4 | 3 of 3, -1.7 % | 10.7 -> 10.3 | 6.8 / 4.2 / 5.1 -> 6.6 / 4.0 / 5.2 | 18.7 -> 18.4 | 0 in all 6 |
| Grimstone Mines | off | 7.44 (7.40 to 7.61) -> 7.30 (6.95 to 7.48) | 133.6 -> 136.0 | 2 of 3, -1.4 % (-8.7 to +0.6) | 8.9 -> 9.9 | 5.9 / 3.9 / 3.9 -> 6.4 / 4.1 / 4.0 | 14.1 -> 12.9 | 0 in all 6 |
| Surdana (disc) | quality | 6.34 (6.27 to 6.49) -> 6.29 (6.25 to 6.53) | 150.2 -> 151.8 | 2 of 3, -0.3 % (-3.0 to +3.0) | 10.2 -> 9.5 | | 13.4 -> 13.4 | 0 in all 6 |
| Surdana (disc) | off | 5.78 (5.68 to 5.90) -> 5.71 (5.70 to 5.94) | 167.0 -> 167.6 | 1 of 3, +0.5 % (-1.4 to +0.8) | 8.5 -> 8.5 | | 11.6 -> 10.7 | 0 in all 6 |

A first canyon queue (`ab1-ootf-canyons`) ran beside another agent's game in 5 of 6 runs (`peak during games 1`); its
numbers (quality 61.9 -> 27.1 ms median, off 21.1 -> 26.7) are noise and not used. The table's canyon rows are the
repeat on a quiet machine (`ab1b`). "driver threads" is the bench's "all other cpu" column (threads that do not name
themselves, mostly the graphics driver).

### 3.2 Per draw (us of GS worker time per draw, from the `[gsr-profile]` windows of the same runs)

| Scene, DLSS | path | draw (all) | resolveTexture | drawSubmit | of it: bind | velocity | end |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Canyons, quality | old | 3.35 to 3.44 | 0.78 to 0.91 | 1.60 to 1.81 | 0.50 to 0.60 | 0.53 to 0.74 | 0.22 to 0.26 |
| Canyons, quality | new | 2.72 to 3.42 | 0.76 to 1.03 | 1.09 to 1.18 | 0.33 to 0.40 | 0.37 to 0.46 | 0.02 to 0.03 |
| Canyons, off | old | 1.69 to 1.76 | 0.76 to 0.82 | 0.46 | 0.19 | | 0.05 |
| Canyons, off | new | 1.51 to 1.56 | 0.74 to 0.77 | 0.31 to 0.32 | 0.08 | | 0.02 |
| Ruined Village, quality | old | 3.52 to 3.64 | 0.77 to 0.80 | 1.76 to 1.92 | 0.66 to 0.75 | 0.48 to 0.68 | 0.12 to 0.16 |
| Ruined Village, quality | new | 2.35 to 2.44 | 0.76 to 0.79 | 0.62 to 0.65 | 0.12 to 0.13 | 0.16 to 0.17 | 0.03 |

With DLSS every draw still switches between the colour target and the motion target twice (colour pass, velocity
pass), so the canyon keeps more of its submit cost than the Ruined Village.

### 3.3 Present latency (`[video] present_latency` of `8cbdaa22`, removed again; exe in `play2`, cache on in both)

Asked for by the coordinator after the CPU agent found that the window thread's `Present` blocks about 5 ms with the
renderer lock held in the Ruined Village. A swap chain that may queue two frames (`SetMaximumFrameLatency(2)`) lets
`Present` return earlier. Runs `ab2-*`, quiet machine (0 other games in all 12 runs), 3 pairs each:

| Scene | DLSS | frame ms, latency 1 -> 2 | pairs | 1 % / 0.1 % low ms |
| --- | --- | --- | --- | --- |
| Ruined Village | quality | 16.54 -> 16.60 | 0 of 3 faster, +0.3 % | 27.0 / 29.1 -> 26.5 / 27.4 |
| Ruined Village | off | 11.33 -> 11.31 | 2 of 3, -0.1 % | 17.1 / 19.8 -> 16.8 / 19.3 |
| Wartok Canyons | quality | 26.25 -> 26.29 | 1 of 3, +0.2 % | 34.8 / 37.1 -> 35.9 / 40.7 |
| Wartok Canyons | off | 16.97 -> 17.82 | 2 of 3, -0.0 % (-11.1 to +8.9) | 28.8 / 36.0 -> 29.7 / 30.5 |

No gain anywhere, and the Ruined Village with DLSS stays at exactly 60 fps with two queued frames, so the swap chain's
frame latency is not what holds it there. The setting was removed again (it would only add a frame of input latency).
What holds the 60 is left to the CPU and present sessions; the GS worker spends 8.0 to 8.3 ms per world frame there
with the cache (8.8 before), so it is not the GS worker either.

## 4. Pictures

* Replay hashes, Direct3D 11, `--texdir build/review/gs-share/textures_frozen`, `GSR_DRAW_STATE_CACHE=0` against `=1`:
  Ruined Village 30 s walk (4,074 frames) at scale 1, 2, 3 and 8, Surdana (2,585) at 3, cave (5,558), Ravenshold flight
  (1,864) and the tab menu (3,984) at 3: all identical. Hashes and logs: `build/review/ootf-terrain-gpu/hashes/`.
* Read-back check in the game (`verifyrun.sh`, owner's ini, DLSS quality for 15 s then off, god mode, start poses, no
  input; runs `oracle/v-*`): Wartok Canyons 75.3 million slot checks, Ruined Village 51.1 million, Grimstone Mines 35.4
  million, Surdana 14.0 million; 0 wrong. Two other agents' games ran beside the canyon run (does not matter for a
  state check).
* Before and after screenshots from the benchmark runs at the same pose and the same time after the start (old left,
  new right; animated creatures and flames move between runs): `evidence/canyons_quality_old_new.png`,
  `evidence/canyons_off_old_new.png`, `evidence/ruined_quality_old_new.png`, `evidence/surdana_quality_old_new.png`.
  Looked at: same terrain, same textures, same HUD.

## 5. What was not done and what would pay next

* **Joining terrain passes into one draw** (texture array of the ground tiles, an index per vertex, the passes of a
  chunk in one draw): would remove the remaining per-draw costs of the terrain (texture lookup 0.8 us, submit 0.3 us
  without DLSS, and on the guest side nothing, because the VU1 runs stay). With DLSS it would also remove the target
  switches of the velocity pass per terrain pass. It changes the shader and the blend order of overlapping passes must
  stay exact; estimate 3 to 5 days with a pixel proof, and the canyon would gain at most the GS worker's share, while
  the EE (18 to 21 ms) and the VU1 worker (16 to 18 ms) stay as they are.
* **The texture lookup per draw** (`resolveTexture`, 0.75 to 0.8 us, shared code for both backends): the cache-hit path
  walks the render targets, hashes the palette and flushes coherence per draw. A "same texture as the previous draw and
  nothing written since" shortcut would be the next shared per-draw saving (about 4 ms per canyon frame at most);
  not built.
* **The velocity pass without target switches**: not possible with the current blend (dual-source blending cannot share
  the output merger with a second target, see the comment above `VSMotion` in `gs_shaders.h`).

## 6. Tests and builds as they ran

* `gsr_test_draw_state_cache` (new, GPU free, in `runtime-tests`): six random streams of 3,800 draws with targets
  sampled as textures, velocity passes and other users of the context, through a model of the device's
  texture/target rule, cache off and on: the bound state at every draw call is the same both ways, 0 differ; also the
  unit rules. Passes.
* Private `runtime-tests` tree of a worktree (`build/review/ootf-terrain-gpu/rt`): `gsr_replay`, `gsr_replay_webgpu` and
  the test built at every step. `ctest -C Release -j 2` of the whole tree with the cache on by default: 191 tests, 184 passed, 7 skipped (game data), 0 failed (`logs/ctest2.log`). The first run, before the fix of section 2, failed `vertex_records` only.
* Game: incremental `Build-LocalGame.ps1 -Incremental -WorkDir build/final-game` of `main` under the build lock into
  `build/review/ootf-terrain-gpu/play` (and `play2` for the present latency), 29 to 63 s each, exit 0.
* clang-format clean on every changed C++ file; `scripts/repository_guard.py` 0 prohibited.

## 7. Not verified

* WebGPU: compiled, the only change is an ignored parameter; no WebGPU replay was run.
* The velocity pass in the replays (they have no DLSS); covered by the read-back check, not by a hash of the motion
  target. DLSS output pictures can only be compared by eye (moving scenes).
* Ray effects, material detail, terrain detail and the GPU vertex path with the cache on: their passes use the context
  through the wrapper, so the cache only drops its state; not run.
* Nothing was played beyond the start poses (no walk, no fight, no level change, no save and load with the cache on).
* Islands and Succubus Lair were not run.
