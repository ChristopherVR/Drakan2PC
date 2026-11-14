# Order of the Flame terrain: chunks with up to 32 textures (round 6, Brief B)

Brief B of the [engine limits survey](engine-limits-survey.md): converted Order of the Flame ground changes texture
almost every cell, the PS2 chunk record holds 10 texture references (and the converter kept to 8 with the ring), so
crowded chunks were cut to 1 to 7 cells and a view held 10 to 30 times the disc's chunks, draws, VU1 runs and draw
pages ([ootf-performance.md](ootf-performance.md), [ootf-terrain-heap.md](ootf-terrain-heap.md)). Scratch, tools,
packs, runs and pictures: `build/review/limits-chunks/` (private). Nothing from the game is quoted.

## 1. Result in short

* **The engine's real limit is 9 textures per chunk, in two places.** The 0x4c-byte record has room for 10 refs
  (`FUN_00165840` copies `ntex` of them unchecked), and the terrain mesh drawer `FUN_00158148` keeps one draw flag per
  texture group in a 10-word array on its stack (`memset(sp, 0, 0x28)`, group 0 is the base pass). A chunk with 10
  textures makes the drawer read an 11th flag past the array (stale stack): it draws a group with no triangles, which
  goes out as the VIF `UNPACK NUM=0` of the 7 November "VU1 reserved lower" bug. That is the missing piece of
  [vu1-reserved-lower.md](../2026-11-07/vu1-reserved-lower.md) ("the page builder fills 7 of 10 groups"). The page
  builder (`FUN_00154d98`), its group vector, the per-page slot array and the strip builder are all sized from the
  chunk's texture count at run time; nothing else in the draw path has a per-chunk texture limit.
* **Lifted with recompiled function variants, switchable at start-up.** The recompiler now emits, next to the
  original functions, a second copy of every guest function that uses the chunk record, with a named set of
  instruction patches (`configs/drakan.toml` `[[variants]] wide_terrain_chunks`): record stride 0xa4 instead of 0x4c,
  `ref[32]` at +0x14, the weight-array descriptor at +0x94 instead of +0x3c, and the drawer's flag array moved to 64
  words in a frame 0x100 bytes larger. `drakan_terrain_chunks.cpp` swaps all twelve copies into the function table at
  start-up, all or none, before any other module wraps one of them. The originals stay in the exe: with the setting
  off nothing changes.
* **Converter:** `DRAKAN_TERRAIN_CHUNKS=wide` keeps the disc's 12 x 12 chunk grid and cuts a chunk only above 32
  textures. The pack says so in its `mod.toml` (`terrain_chunk_textures = 32`), and the runtime then uses the wide
  layout whatever `[general] terrain_wide_chunks` says, because the disc functions would overwrite the records and
  hang (1 November). So: old pack = old path; wide pack = wide path; the setting adds the wide functions for old packs
  and Drakan 2 (no visible change there, proved below).
* **Fewer chunks, passes and draws** (offline, the 14 November converter on the same tree):

  | Level | chunks cut / wide | texture passes cut / wide | most textures in a chunk |
  | --- | --- | --- | --- |
  | Wartok Canyons | 9,027 / 648 | 64,528 / 9,033 | 8 / 32 |
  | Islands | 16,855 / 930 | 118,975 / 14,762 | 8 / 32 |
  | Ruined Village (OOTF01GE) | 2,753 / 387 | 19,438 / 3,907 | 8 / 30 |

  In game at the start poses, per world frame: Wartok Canyons 4,357 draws and 14,250 VU1 runs become 966 and 4,077,
  the Islands 2,793 / 9,051 become 533 / 2,495, the Ruined Village 1,557 / 5,021 become 460 / 1,928 (section 4).
* **Every reader is proven by a watch** (section 3): the chunk vectors are handed to the guest through an aliased
  address that only the slow memory path serves, and every guest routine that touched a record was logged. The watch
  found two record users the static scan had missed (the layer destructor `FUN_00159998` and the re-init
  `FUN_0015e418`, both freeing every chunk's weight array at +0x48 with the 0x4c stride); without them a level change
  would have freed garbage pointers.
* **Pictures: same cells, same passes per triangle, a different blend order** (section 5). No cell is repainted and
  every triangle is drawn with the same set of textures and weights (tested), but the passes of a chunk are blended in
  its list order (first appearance in the chunk's window), and a 12 x 12 window gives another order than the old
  1 to 7 cell windows: 30 to 56 % of the drawn triangles get their passes in another order. An order-keeping re-cut
  (the round 3 `mergeChunks`, with the cap at 32) keeps every pixel but removes only 12 to 22 % of the passes
  (Wartok 64,528 to 54,412); a greedy order chosen to agree with the old one changes more triangles than first
  appearance. So the 7x gain and exact pictures exclude each other; the old order is itself arbitrary per small
  chunk (it flips at every former chunk seam).

## 2. How the variants work

* Recompiler (`ps2recomp/ps2xRecomp`): `[[variants]]` entries name a variant and list patches, either a change of the
  16-bit immediate of the instruction in the ELF (`imm_from` / `imm_to`, checked against the ELF so the config holds no
  ELF words; another executable leaves the variant out with a warning) or an instruction written for the variant
  (`value`, one: `addiu a0, sp, 0x130`). A patch may not touch control flow (the original's analysis is reused). Every
  function a variant touches is emitted a second time as `<name>__<variant>` into `register_functions.cpp`, which
  registers it with `ps2RegisterFunctionVariant` (`ps2_function_variants.h`); no other generated file changes.
* Runtime: `ps2InstallFunctionVariant` points every dense-table slot of the function (its entry and its resume labels)
  that still holds the original at the copy. Direct C++ calls bypass the table, so the generated code was checked: no
  direct call or tail jump to any of the twelve functions exists anywhere.
* The twelve functions: `154060` (layer draw block build), `154ba8` (size estimate, no caller), `154d98` (chunk page
  builder), `158148` (mesh drawer), `159998` (layer destructor), `15c948` (chunk stream load), `15cba0` (texture touch),
  `15e418` (layer re-init), `165840` (chunk read), `165a80`, `165cb0`, `165f68` (run-time chunk builder, no caller).
  42 patches. `FUN_00165940` (descriptor read) and `FUN_00166288` take the descriptor or ref pointer and need none.
* `world/TerrainChunks.h` holds the layouts and limits (`world_terrain_chunks_test`).

## 3. Proof of the readers: the alias watch

`DRAKAN_TERRAIN_CHUNK_WATCH=1`: the guest `malloc` / `realloc` called from the chunk vector's two allocation sites
return the block's address plus 0xE0000000. That is a KSEG3 address, so every guest load and store of it takes the
runtime's slow path, which strips the alias, serves RAM and reports the access (`ps2_alias_watch.h`); frees and
reallocs strip it, and the host libc stubs (`memcpy`, `memset`, ...) get the plain address and log the caller. At exit
and every 10 s the log lists each guest pc with the record fields it read or wrote.

| Run | Layout | What ran | Functions that touched a record |
| --- | --- | --- | --- |
| `runs/smoke3` | disc | Surdana start (Drakan 2) | 154060, 154d98, 159998, 15c948, 15cba0, 165840, 165940 |
| `played/wide-watch`, `wide-watch2`, `wide-watch3` (the last on the committed code) | wide | Wartok Canyons tour: walks, a fight, F5 / walk / F8, the walk into the mine exit, the save screens, Grimstone Mines loads, F5 / F8 there | 154060, 154d98, 159998, 15c948, 15cba0, 165840, 165940 |

Every one is a variant (or the descriptor reader). The only stub caller was the stream inflate copying into a
record. The first watch build hung at the level load because the libc stubs resolve guest pointers themselves; they
now get the plain address.

Empty draw groups: an Islands run on the wide pack with `DRAKAN_TRACE_FN=1ed0b8`, `PS2X_VU1_THREAD=0`,
`DRAKAN_VU_BATCH=0` and `PS2X_VIF_REGLOG=1` (`runs/unpack0b`): 0 `[unpack0]`, no `VU1 reserved`, the only `STMOD` the
boot's 0.

## 4. Measurements

`scripts/perf/scene_ab_bench.py` under its turn lock, owner's `drakan.ini` (3840 x 2160, DLSS quality, HD textures),
the exe of this change, `--quiet-games 0`: every one of the 18 runs started and ran with no other `drakan.exe`, no replay
tool and no compiler (two runs saw one compiler start during the run). Three interleaved pairs per scene; `cut` = the
pack converted with the disc limit (as installed today), `wide` = the same converter tree with
`DRAKAN_TERRAIN_CHUNKS=wide` (so the wide layout is on). Direct starts as the editor's Play does them (`ootf-canyons`,
`ootf-islands`), the campaign start for the Ruined Village (`ootf-ruined`). Placed by the level start, no input. Runs:
`build/review/limits-chunks/runs/ab2-*`.

| Scene | pack | world fps | frame ms | 1 % low ms | EE ms | VU1 worker ms | GS worker ms | GPU world ms | draws / frame | VU1 runs / frame |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Wartok Canyons | cut | 54.7 | 16.66 | 30.0 | 18.04 | 15.36 | 16.37 | 2.65 | 4,357 | 14,250 |
| Wartok Canyons | wide | 81.8 | 12.25 | 17.1 | 8.66 | 7.06 | 9.07 | 2.31 | 966 | 4,077 |
| Islands | cut | 77.6 | 12.87 | 14.9 | 9.41 | 8.58 | 9.84 | 0.95 | 2,793 | 9,051 |
| Islands | wide | 84.0 | 11.92 | 12.5 | 4.40 | 4.09 | 4.30 | 0.76 | 533 | 2,495 |
| Ruined Village | cut | 99.7 | 9.85 | 15.2 | 9.07 | 5.06 | 5.94 | 0.78 | 1,557 | 5,021 |
| Ruined Village | wide | 93.7 | 10.40 | 16.8 | 8.40 | 3.51 | 4.39 | 1.55 | 460 | 1,928 |

* Pairs: Wartok Canyons 16.66 -> 12.40, 17.00 -> 11.98, 16.05 -> 12.25 ms (wide faster in 3 of 3, -24 to -30 %);
  Islands 12.99 -> 11.89, 12.84 -> 11.92, 12.87 -> 11.95 (3 of 3, -7 to -9 %); Ruined Village 9.85 -> 10.40, 9.88 ->
  10.29, 9.83 -> 10.40 (0 of 3, +4 to +6 %).
* The three worker threads need half the time or less in Wartok Canyons and the Islands, and the 1 % low interval of the
  canyon falls from 30 to 17 ms. In the Islands and the Ruined Village the frame is no longer bound by them (the
  `Present` wait grows instead).
* **The Ruined Village start is 5 % slower**: its GPU world time doubles (0.78 -> 1.55 ms) while every CPU column
  falls. The splat passes of a chunk are drawn while the chunk's box is within the material's fade distance; a 12 x 12
  chunk is in range as soon as one corner is, so near the fade distance whole chunks get their ground textures where
  the cut layout gave them to single cells. That is the disc's behaviour (and more texture detail at mid range), but
  it costs GPU fill in a scene that is GPU bound at 100 fps.
* Same exe, the cut pack, `terrain_wide_chunks` 0 against 1 (Wartok start, Surdana town): the same draws and VU1 runs
  per frame (14,156 / 14,059 and 3,561 / 3,567 VU1 runs), the variants cost nothing measurable; the frame times of that
  check were taken beside two other games and are not used.

## 5. Pictures

* **Variants alone** (same pack, setting 0 / 1): Wartok start with the cut pack (`evidence/ident_canyon_narrow_off_on.jpg`)
  and Surdana town in Drakan 2 (`evidence/ident_surdana2_off_on.jpg`): the ground is the same; the differences are the
  Wartoks attacking Arokh, Rynn's idle pose and the tree sway (pixels over 16 levels apart: 0.4 to 1.7 %, the same as
  between two runs of one setting).
* **Cut against wide pack, same pose**: Wartok Canyons (`evidence/canyon_cut_vs_wide.jpg`, amplified differences
  `canyon_diff_cut-wide.jpg`), Islands arrival tunnel (`islands_cut_wide_diff.jpg`), Ruined Village start
  (`ruined_cut_vs_wide.jpg`). No changed texture or seam is visible; pixels more than 16 levels apart: 1.2 %, 2.9 % (the
  wing beat of Arokh) and 0.6 %, against 0.6 to 1.6 % between two runs of one pack. More than 8 levels apart in the
  canyon: 3.4 % against 2.3 %; the extra is the blend order on texture borders, a few levels each.
* **Played tour** (`played/cut-tour`, `played/wide-tour`, `evidence/tour_a.jpg`, `tour_b.jpg`): the cave, mine and
  exit views match; the two shots on the ledge differ because the walk stopped at another camera angle.

## 6. Tests and builds

* Formats `vitest run`: 61 files, 639 tests passed (new: the wide limit keeps the disc grid and draws every cell with
  the same passes and weights as the cut layout; the env switch); `tsc --noEmit` clean. Editor-core: 32 files, 367
  tests passed (new: `terrain_chunk_textures` in the manifest); `tsc` clean.
* `ctest --preset runtime-tests` in the worktree: 198 of 198 passed (data or device tests skip), with the new
  `world_terrain_chunks_test`.
* Recompiler: a config test for `[[variants]]` added to `ps2xTest` (`ps2_recompiler_tests.cpp`). The `ps2x-tests` build
  was started and stopped (it was compiling the VU1 AOT units for half an hour and blocked the benchmark); not run.
* Game builds: one full and six incremental builds of the worktree into `build/review/limits-chunks/game` under the build
  lock, the last on main at f4df036d; the generated code is unchanged except `register_functions.cpp` (12 variant
  functions, 42 patches).
* Game runs: the watch runs, two placed identity pairs, the empty-group trace, three played tours, 18 benchmark runs.

## 7. Not verified

* Levels other than Wartok Canyons, the Islands and the Ruined Village were not run with a wide pack (counted offline
  only); Grimstone Mines was entered at the end of the tours.
* A played Drakan 2 session with the wide layout: only placed starts (Surdana town, Surdana start) and the Wartok
  tours; no Drakan 2 level change with the setting on.
* The Ruined Village slowdown (GPU fill near the fade distance) was explained from the columns, not traced per draw.
* Frame times without DLSS, and on a machine with other games running.
* The `ps2x-tests` suite with the new recompiler test.
* The blend order: no order keeps every triangle's old order with large chunks; first appearance in the chunk window
  was kept. A level-wide texture order (as the disc uses) was not tried.
