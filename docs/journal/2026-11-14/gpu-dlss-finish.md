# DLSS speed, part 2: velocity passes that change nothing, terrain batching A/B, GPU vertex path state (2026-11-14)

Round 7, GPU and DLSS agent (worked into the night of 15 November). Owner's request: "Finish the GPU performance
rework so DLSS runs better." Scratch: `build/review/gpu-finish/` (play folder `play` with the owner's `drakan.ini` from
`build/review/baseline-play`, a private ELF copy `elf/`, the `dist` packs of the 21:30 rebuild in `mods/`, worktree
`wt`, benchmark runs `runs/`, played runs `played/`, replay hashes `hashes/`, tools `tools/`, pictures `evidence/`; not
tracked). Follows [dlss-speed.md](../2026-11-12/dlss-speed.md), [ootf-terrain-gpu.md](ootf-terrain-gpu.md),
[ootf-terrain-join.md](ootf-terrain-join.md) and the GPU vertex notes of 12 and 13 November.

## 1. Result in short

* **What made DLSS slow, and what was wrong with the old explanation.** "The port is per-draw bound and DLSS adds a
  velocity pass per draw" was right about the pass, but not about what it was for: with DLSS on, every world draw was
  followed by a velocity pass into the object-motion target, including the draws that cannot move (ground, rocks,
  houses). Those draws have no previous position (`motion::Previous` all zero), so their pass writes only "no object
  motion here" entries, which the guide pass ignores (it takes object motion only where the entry is valid and uses
  the camera motion everywhere else). Writing such an entry where nothing valid lies yet changes nothing DLSS reads,
  but each one cost a zero-stream upload (`Map`), a switch to the motion target and back, its own programs and states,
  and a draw call. 58 to 88 % of all velocity passes are of that kind (table 5.2).
* **Built: `[video] velocity_skip`** (env `GSR_VELOCITY_SKIP`, default **1** since `970d213e`, `0` = the old path; built default-off in `b1a4a2ba`, test
  switches `13650083`).
  Per world a 64 x 64 tile map of the motion target records where a draw that may write a valid entry has drawn (its
  vertex box plus a margin). A draw with no previous position anywhere skips its upload and velocity pass when its box
  touches no marked tile. Exact for what DLSS reads by construction (`runtime/gs/src/motion_coverage.h`).
* **Frame time with DLSS quality, same run, skip switched every 4 s** (`GSR_VELOCITY_SKIP_TOGGLE`, 3 runs a scene, all 18
  runs on a quiet machine): Wartok Canyons 17.73 to 13.91 ms (-22 %), Islands 10.23 to 7.23 ms (-29 %), Ruined Village
  10.67 to 7.34 ms (-27 %), Grimstone Mines 8.98 to 7.14 ms (-20 %), Surdana 6.58 to 6.45 ms (-2 %), Ravenshold 4.92 to
  4.49 ms (-7 %); faster in 3 of 3 runs everywhere. With the skip, DLSS quality is now **faster than DLSS off** in all
  four Order of the Flame scenes at the start poses in a background window, as the bench runs it (off: 16.09, 12.14, 10.35, 8.25 ms), and within 0.5 ms (Surdana) and 1.1 ms
  (Ravenshold) of off in Drakan 2, where the rest is the upscaler's fixed GPU cost (evaluate 1.3 ms, guides and
  write-back 0.6 ms per frame).
* **Same motion for DLSS.** `GSR_VELOCITY_SKIP_VERIFY=<n>` keeps a shadow motion target that receives every pass (as
  without the skip) and compares the region the guides read, every n-th world: 0 differing pixels in every compared
  world of the start poses and of the played runs (section 4). The wrong-on-purpose mode (`GSR_VELOCITY_SKIP_DEBUG=1`,
  skip regardless of the map) does report differences, so the comparison can fail.
* **Terrain batching A/B: do not flip.** With DLSS quality it helps the Order of the Flame scenes (-11 to -13 % in
  canyons, Islands, Ruined Village) in the cross-run A/B, but that A/B ran beside other agents' games or compilers in 38
  of 72 runs, Surdana has no clean pair, and once the skip is on it adds little (section 6). `terrain_batching` stays 0.
* **GPU vertex path: not the lever for DLSS, and not provable today.** Its gain is on the VU1 worker, which bounds no
  scene; with DLSS it was even or slower in every earlier table. Its proof set has shrunk to one capture (section 7).
* **Owner's premise, measured on current main at start poses:** DLSS quality was slower than off only in Wartok
  Canyons (+1.6 ms), Grimstone Mines (+0.7), Surdana (+0.6) and Ravenshold (+1.5); in the Islands it was already
  faster, in the Ruined Village even. The wide chunks, the draw state cache and the 12 November upscaler sizes had
  already removed most of the old loss. With the skip, the loss is gone in every converted level.

## 2. Where the DLSS frame goes (old path, start poses)

From the 4-variant A/B (`runs/ab-*`, `tools/dlss_loss.py`: DLSS quality minus DLSS off, medians over 3 runs; these
runs are across processes and noisy, see section 6, so the thread and driver columns are indicative only) and the
frame times of the toggle runs (same process):

| Scene | frame off -> quality ms | GS worker | EE | driver threads | GPU evaluate | GPU guides, entry, write-back | velocity passes ms/s on the GS worker |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Wartok Canyons | 16.09 -> 17.73 | +1.6 | +1.1 | +5.5 | 1.39 | 0.63 | 10.5 |
| Islands | 12.14 -> 10.23 | +1.1 | +0.6 | +0.5 | 1.40 | 0.61 | 8.1 |
| Ruined Village | 10.35 -> 10.67 | +1.3 | +0.8 | +3.2 | 1.35 | 0.57 | 7.5 |
| Grimstone Mines | 8.25 -> 8.98 | +1.1 | +0.9 | +2.9 | 1.35 | 0.59 | 6.4 |
| Surdana | 5.98 -> 6.58 | +0.5 | +0.0 | +0.8 | 1.33 | 0.63 | 5.7 |
| Ravenshold | 3.43 -> 4.92 | +1.1 | +0.8 | +3.2 | 1.30 | 0.56 | 6.1 |

* The GPU work DLSS adds is about 1.9 ms per frame (evaluate 1.3 to 1.4, guides, depth into the render rectangle and
  write-back 0.6), against a smaller world raster (render size 2560x1440 instead of 4096x4096). Where the frame is
  bound by the GPU's tail (`Present` waits, the Islands) DLSS already won.
* The CPU work: the GS worker spends 0.5 to 1.6 ms more per frame (object motion tracking, the velocity passes, the
  zero-stream uploads) and the driver's own threads ("all other" CPU) 0.5 to 5.5 ms more: that is the per-pass target
  switch and state change, the part the GS worker profile does not show. The velocity pass bucket itself is small on
  the GS worker (5 to 10 ms per second); its cost lands in the driver and in the `Present` wait.
* **How much each part explains, with the skip as the measuring stick:** leaving out the invalid-only passes removes
  more than the whole DLSS loss in the converted levels (canyon loss +1.6 ms, skip -3.8 ms; Ruined Village +0.3, skip
  -3.3): those passes cost more than DLSS's whole saving on the raster. In Drakan 2 it removes 0.1 ms (Surdana, few
  draws) and 0.4 ms (Ravenshold) of 0.6 and 1.5 ms; the rest there is the upscaler's fixed GPU cost (1.9 ms) on a
  4 to 6 ms frame and the object-motion tracking of the characters on the GS worker and the EE.
* Terrain batching attacks the same pass switches (the joined segments draw all colour passes, then all velocity
  passes) but only inside joined ground batches; the skip removes the passes everywhere. Together (`both`) they did
  not beat the skip alone outside the Ruined Village (section 6).

## 3. What was built: `[video] velocity_skip`

* `motion_coverage.h` (GPU free): `bounds()` (vertex box in target pixels plus margin, clipped; a non-finite vertex
  covers the whole target) and `Grid` (64 x 64 bits, `reset`, `mark`, `markAll`, `touches`).
* `d3d11_world.cpp`, `prepareObjectMotion`: after the history is tracked (so object ordinals advance exactly as
  before), a draw whose previous stream has `valid != 0` anywhere marks its box; one without returns no velocity pass
  when its box touches no marked tile, before the upload. Margin: 2 GS pixels times the scale plus 2 target pixels,
  which covers the jitter, the rasteriser and the only two places that move world vertices after this point (the menu
  highlight, +2 GS pixels, and the inventory font shadow, -1); HUD and menu remapping only happens outside the engine's
  world, where a draw that may be valid marks the whole target instead and nothing is skipped by box.
* `ensureMotionTarget` resets the map with the clear at a world's start; every other clear of the motion target
  (`invalidateMotion`, the vertex-program fallback) goes through `clearMotionTarget`, which resets it too. A vertex
  program batch with motion (GPU vertex path) marks the whole target.
* Setting: `[video] velocity_skip` in `drakan_config.cpp`, `Config::velocitySkip` in `gsr.h`, the bridge in
  `gs_gsr_bridge.cpp`; env `GSR_VELOCITY_SKIP=0|1` wins. Direct3D 11 only (WebGPU has no velocity pass).
* Diagnosis: `GSR_VELOCITY_SKIP_VERIFY=<n>` (shadow target, comparison of the guide region every n-th world: same pixels
  valid, a valid pixel's motion and depth equal to the bit; `[velocity-skip] verify` lines), `GSR_VELOCITY_SKIP_TOGGLE=<s>`
  (switch at a world start every s seconds), `GSR_VELOCITY_SKIP_DEBUG=1` (ignore the map, wrong on purpose); with
  `GSR_PROFILE` a `[velocity-skip]` counter line. The raw object-motion dump (`GSR_TEMPORAL_DUMP_*`, stage
  `object-motion`) differs in invalid entries by design; the guides do not.

## 4. Proof

* Unit test `gsr_test_motion_coverage` (GPU free, in `runtime-tests`): grid rules, bounds, and 400 random worlds of
  draws through a model of the motion target, every pass against the skip: 0 of the read entries differ (30,032 passes
  left out of 48,697).
* In the game, start poses, verify every 15th world (`runs/verify-*`): Wartok Canyons 72 worlds, Surdana 122 worlds,
  0 differing pixels, 54,000 to 67,000 valid pixels a world in the canyon (Rynn, Arokh, creatures), 79 % of passes left
  out.
* Played runs with skip and terrain batching on, verify every 10th world (`played/`, recipes in `tools/`):

  | Run (what was played) | worlds compared | valid pixels | differ | passes left out |
  | --- | --- | --- | --- | --- |
  | Surdana, 150 s run with turns and jumps and two placements (`tools/walk_surdana.txt`) | 1,897 | 20.4 M | 0 | 2,392,942 of 3,232,193 (74 %) |
  | Surdana, F8 without a save, F5, walk, F5 again, F8 (a reload), Esc pause menu and back (`quick_save_load.txt`) | 313 | 19.2 M | 0 | 419,862 of 526,568 |
  | Ravenshold on Arokh: climb, forward, turn, both attacks (`disc_flight.txt`) | 80 | 5.9 M | 0 | 76,826 of 109,179 |
  | Surdana walk again with the default exe (`970d213e`, no skip variable, the line removed from the ini so the game
  wrote `velocity_skip = 1` itself), both packs loaded | 2,286 | 22.8 M | 0 | 2,442,121 of 2,998,945 |
  | Order of the Flame campaign start, Ruined Village at night, 45 s run with turns and jumps (`tools/walk_ootf.txt`) | 520 | 45.0 M | 0 | 230,416 of 728,747 |

  The first three Drakan 2 runs had a broken `DRAKAN_MODS` path (unquoted folder with spaces in my script), so they ran
  without the character motion pack (the default-exe Surdana run has it); the Order of the Flame run was repeated with the path fixed (`ootf-walk2`; the
  first attempt silently started Drakan 2). Contact sheets: `evidence/played_sheet.png`, `evidence/ootf_walk_sheet.png`
  (looked at: running Rynn, terrain, buildings, pause menu, Arokh in flight, the Ruined Village fire; nothing missing
  or torn). Not played with the skip: a level change through an exit, inventory, cut scenes
* Negative control: the same Surdana run with `GSR_VELOCITY_SKIP_DEBUG=1` (the map ignored): 802,419 differing pixels
  in 1,095 compared worlds. The comparison sees a wrong skip.
* Replay hashes (`hashes/`, `gsr_replay` of the new code, frozen HD textures, scale 3): skip off against on identical
  for the Ruined Village (4,074 frames), Surdana (2,585) and cave (5,558) captures; the new code with the skip off
  identical to its parent commit `ecdf69d7` (Ruined Village, cave). Against the terrain-join agent's morning hashes all
  three differ, and so does the parent: the texture-limit commits of round 6 (`bc849484`, `9b57acae`) changed those
  pictures before this work. The replays have no
  engine world and no DLSS, so they only show that nothing else changed; the motion proof is the in-game comparison.
* Pictures: the bench takes the same pose with each variant (`runs/ab-*/<variant>-<n>/b_quality_0.png`);
  `evidence/canyons_quality_old_vs.png` (old, skip, batching, both): same terrain, textures and HUD; creatures and the
  health orb animate between runs (mean absolute difference 0.8 to 0.9 levels, 0.5 to 0.8 % of pixels over 32 levels,
  all on the animated parts).

## 5. Frame times

### 5.1 Velocity skip, toggled inside each run (`runs/tg-*`, `tools/toggle_split.py`)

Owner's `drakan.ini` (3840x2160 borderless, DLSS quality, HD textures), `scene_ab_bench.py` with its turn lock,
`--cycle video.dlss=quality,off --settle 2 --window 24`, `GSR_VELOCITY_SKIP_TOGGLE=4` (the first 0.5 s after each switch
dropped), 3 runs a scene, start poses, no input. Every run: 0 other games, replay tools or compilers before and during.
Frame ms = interval between world flips, median of the pooled intervals (mean in brackets).

| Scene | DLSS off | quality, skip off | quality, skip on | change per run (mean) |
| --- | --- | --- | --- | --- |
| Wartok Canyons | 16.09 (16.13) | 17.73 (17.93) | 13.91 (13.91) | -23.6, -21.8, -21.6 % |
| Islands | 12.14 (12.19) | 10.23 (10.29) | 7.23 (7.31) | -29.4, -29.1, -28.2 % |
| Ruined Village | 10.35 (10.43) | 10.67 (10.65) | 7.34 (7.59) | -29.2, -26.5, -26.4 % |
| Grimstone Mines | 8.25 (8.37) | 8.98 (9.20) | 7.14 (7.33) | -20.9, -20.2, -19.9 % |
| Surdana | 5.98 (6.25) | 6.58 (6.91) | 6.45 (6.74) | -2.8, -2.3, -2.1 % |
| Ravenshold | 3.43 (3.60) | 4.92 (5.01) | 4.49 (4.66) | -7.2, -7.0, -6.9 % |

99th percentiles move with the medians (canyon 26.5 to 24.7 ms, Ruined Village 16.8 to 13.4, mines 15.1 to 12.0) except
Ravenshold (6.21 to 6.47).

### 5.2 Share of velocity passes left out (`[velocity-skip]` counters of the same runs)

Wartok Canyons 79 %, Islands 88 %, Ruined Village 72 %, Grimstone Mines 58 %, Surdana 78 %, Ravenshold 63 %.

### 5.3 Interleaved A/B across runs (`runs/ab-*`, variants old / tb / vs / both, 3 repeats)

Kept for the record and for terrain batching; 38 of 72 runs had another agent's game (30) or a compiler running during
the run (listed in the `ab-*.txt` files), and the DLSS-off windows, where the skip cannot act, show run-to-run swings of up
to 18 % (Ruined Village, same exe, same pose). Median frame ms, DLSS quality:

| Scene | old | tb | vs | both |
| --- | --- | --- | --- | --- |
| Wartok Canyons | 17.81 | 15.86 (3 of 3 faster) | 14.29 (3 of 3) | 13.96 (2 of 3) |
| Islands | 10.69 | 9.73 (3 of 3) | 8.00 (3 of 3) | 8.41 (3 of 3) |
| Ruined Village | 10.73 | 9.38 (3 of 3) | 7.98 (3 of 3) | 6.47 (3 of 3) |
| Grimstone Mines | 9.06 | 16.70 (1 of 3) | 7.05 (2 of 3) | 7.67 (2 of 3) |
| Surdana | 6.42 | 6.87 (0 of 3) | 6.44 (1 of 3) | 6.35 (3 of 3) |
| Ravenshold | 4.84 | 4.85 (2 of 3) | 4.34 (2 of 3) | 4.31 (2 of 2) |

## 6. Terrain batching A/B (brief item 3)

Not run as the literal `abqueue.sh ab2` command: that script runs the terrain-join agent's morning exe and narrow pack,
and its canyon result (`ab2-ootf-canyons`, -31 % with DLSS) predates the wide chunks, which cut the canyon's draws from
4,357 to 966. Run on the current exe and the wide `dist` pack instead, twice:

* In the 4-variant queue of 5.3 (old / tb / vs / both): with DLSS quality the converted levels gained 11 to 13 % in the
  medians, but 38 of 72 runs had another game or a compiler beside them and Surdana has no clean old/tb pair, so that
  queue settles nothing about batching.
* Once the skip was the candidate default, the question that matters is batching on top of it: `vs` against `both`,
  both variables explicit, 3 interleaved repeats, all 36 runs on a quiet machine (`runs/vb-*`):

| Scene | DLSS quality, vs -> both (median of pairs) | pairs faster | DLSS off | pairs faster |
| --- | --- | --- | --- | --- |
| Wartok Canyons | 13.53 -> 13.60, -0.4 % | 2 of 3 | 15.55 -> 15.79, +1.6 % | 0 of 3 |
| Islands | 7.18 -> 7.20, -0.1 % | 2 of 3 | 12.11 -> 12.11, +0.2 % | 1 of 3 |
| Ruined Village | 8.03 -> 6.49, -19.2 % | 3 of 3 | 10.81 -> 9.16, -14.4 % | 3 of 3 |
| Grimstone Mines | 6.90 -> 7.00, +1.5 % | 1 of 3 | 8.06 -> 7.98, -1.2 % | 2 of 3 |
| Surdana | 6.53 -> 6.36, -0.4 % | 2 of 3 | 5.83 -> 6.22, +4.4 % | 0 of 3 |
| Ravenshold | 4.46 -> 4.34, -0.7 % | 2 of 3 | 3.42 -> 3.18, +0.1 % | 1 of 3 |

**Default stays 0**: it pays a lot in the Ruined Village (why only there was not traced) and nothing
elsewhere, and Surdana with DLSS off is slower in 3 of 3 pairs. A player of the converted campaign
can turn it on. Also: the owner's `dist/DrakanPC/drakan.ini` already holds `terrain_batching = 0` (the game appends
missing keys with their default), so a changed default would not reach the owner without removing that line.

## 7. GPU vertex path: what is open

State on main: strips drawn by the generated vertex shader on Direct3D 11 and WebGPU; batch records drawn by the
shader on Direct3D 11 only (`DRAKAN_VERTEX_STAGES=all`); `vertex_processing = cpu`, `auto` resolves to `cpu`
(`Capability::proven = false`). What remains, from the 12 and 13 November notes and this round:

* **It does not pay with DLSS.** Every quiet-machine table (13 November handover: Surdana quality 6.20 to 6.12 ms, the
  fight 3.3 % slower, Ruined Village even) gains on the VU1 worker and pays on the GS worker; with DLSS the frame is
  bound elsewhere (GS worker, driver, `Present`). The fixes it needs first are listed in `gpu-vertex-batch-draw.md`
  section 3 and `gpu-vertex-tuning.md` section 4 (one Map per frame instead of four to six per draw, no per-draw
  constant buffer, the strip register tail deferred, per-record bounds, a leaner batch hand-over): days of work for a
  thread that does not bound the DLSS frame.
* **WebGPU**: batch records not drawn by a shader (CPU evaluation), browser path not built (`gpu-vertex-webgpu-draw.md`
  sections 5 and 6).
* **Correctness gaps**: strips in targets smaller than the scissor are covered by the exact bounds since 13 November;
  real motion sequences at 4K with DLSS and `gpu` were never looked at; the batch-path motion history only by unit test;
  device loss in the middle of a frame on WebGPU.
* **The proof the capability needs** (pixel-exact replays of captures with records, live toggles, DLSS inputs) has
  lost its inputs: `build/review/gpu-vertex2/caps2/flight` and `ootf` are empty (deleted with the bulky run data), so
  only the Surdana strip capture (`build/review/gpu-vertex/caps/surdana`) remains. Re-run here with the new code: records
  evaluated on the CPU against the shader (`DRAKAN_VERTEX_EVAL=records` / `gpu`, `GSR_VERTEX_PROGRAM_WAIT=1`, HD off),
  1,423 frames at scale 1 and at scale 3, identical. A proof for
  `auto` needs new captures with records (flight, an interior, an Order of the Flame level) taken by the game.
* Recommendation: leave `vertex_processing = cpu`; spend the next renderer effort on the remaining DLSS CPU cost
  (object-motion tracking and the scene capture on the EE) rather than on the vertex path.

## 8. Tests and builds as they ran

* `runtime-tests` in the worktree, whole preset, run twice (after the change with the default off, and with the
  default flipped): 199 tests, 192 passed, 7 skipped (game data), 0 failed both times (`ctest -C Release -j 2`, includes `vertex_records`, `vertex_records_webgpu_vulkan` and `_d3d12`,
  `draw_state_cache`, `draw_join`, the new `motion_coverage`; WebGPU targets build).
* `python scripts/run_tests.py`: 562 tests OK (2 skipped). `scripts/repository_guard.py`: 0 prohibited. clang-format
  19.1.7 on every changed C++ file.
* Game: three incremental `Build-LocalGame.ps1 -Incremental -WorkDir build/final-game` of main (`b1a4a2ba`,
  `13650083`, `970d213e`) into `build/review/gpu-finish/play` under the build lock, `-TextureDirectory` pointing at the play
  folder's own `textures` (a junction to `baseline-play/textures`, nothing staged), exit 0 all three.
* Game runs: 2 verify runs, 72 A/B runs (one Ravenshold run ended in the guest crash below), 18 toggle runs, 36 `vs`/`both` runs, 7
  played runs; 2 abandoned runs at the start (the pack was not enabled in my first play folder: `mods.ini` missing). One
  game of mine at a time; at most two replay tools.
* **A guest crash in Drakan 2**: Ravenshold `both-0` (skip and batching on) stopped while the level loaded:
  `[guest-branch:missing-target] JALR source=0x10a45c target=0x0`, the wild-call class seen on 1 and 12 November at the
  same addresses (`ootf-full-level-runs.md`, `ootf-volcanic-rift.md`). Once in 135 finished game runs of this round, before the world
  (and so before any velocity pass) was drawn. Not attributed to the change and not explained.

## 9. Not verified

* Moving play beyond the
  recipes in section 4 (fights with many creatures, cut scenes with DLSS, water and ray effects, material detail) was
  not compared; ray effects and material detail use their own passes and were off.
* A foreground (activated window) measurement: all runs used the harness's background window.
* DLSS balanced, performance and DLAA modes, and other resolutions: only quality at 3840x2160.
* WebGPU: the skip is Direct3D 11 only; WebGPU has no motion vectors.
* The GPU vertex path claims of section 7 rest on earlier notes; only the Surdana strip capture was replayed here.
* An ini that was started with an exe built from `b1a4a2ba` to `13650083` holds `velocity_skip = 0` (the game appends
  the then-default); only my play folder is known to have done so. The owner's `dist/DrakanPC/drakan.ini` has no
  `velocity_skip` line yet, so the next exe writes the new default 1.
* Level changes through an exit, inventory and cut scenes with the skip on (not in the played recipes).
