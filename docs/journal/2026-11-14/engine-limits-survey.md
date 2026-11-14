# Engine limits survey: which PS2 budgets still hold the game back on PC (round 6)

Owner's request: remove the PS2 engine's shackles now that memory and speed are no longer short. Other round 6 agents
own dynamic guest memory, texture memory and quality, and draw distance and detail; this note covers everything else,
ranks it, records what was lifted and writes the larger items up as briefs. Scratch, runs and evidence:
`build/review/limits-survey/` (private). Static reading used the Ghidra decompile of the PAL ELF (private, under
`re-pools/all/`) and the existing sound decompiles in `analysis/sound/`; nothing from the game is quoted here.

## How it was measured

* Play folder `build/review/limits-survey/play`: copy of `baseline-play` (the owner's `drakan.ini`: 3840 x 2160
  borderless, DLSS quality, HD textures, world rate uncapped, vsync off), `elf =` pointing at a private ELF copy, the
  `dist` pack of 14 November (`ootf-levels.zip`, sha `3b1f6b95...`).
* Runs through `scripts/harness/play_run.py` with a placed pose and scripted input; numbers from logs
  (`DRAKAN_SND_LOG=2`, `DRAKAN_SND_DBG=1`, the new `[engine-limits]` lines) and from RAM dumps of the recipe
  (`dump ram_<label>`).
* Drakan 2 guard scene: Surdana. Fight: three Wartoks at the cave mouth (`surdana-fight` pose of
  `scene_ab_bench.py`), god mode, a swing every 0.7 s for 40 s. Town: the `surdana-town` pose.

## Ranked table

Rank is value to the player on PC against the work and risk of a host-side lift. "Hit" says whether the limit was
actually reached in a measured run.

| # | Limit | Where | Hit on PC? (evidence) | Host-side lift | Risk | State |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Character shadows end at the level's "Max Shadow Depth" (15 / 30 level units squared, x 4194.304; cutoff x 1.1) | `0x1431e8` writes `0x4e0adc` / `0x4e0ad4` / `0x4e0ad8`; read by the shadow builders `0x18b5d0`, `0x1a9xxx`, `0x15d7b8` | Yes, every level: a villager at the edge of the Surdana square has no shadow and floats (`evidence/town_villager_sd1_vs_sd3.png`, left) | Scale the three squared distances by f^2 after the setter | Low: more shadow targets per frame (cost measured below) | **Done**: `[video] shadow_distance` |
| 2 | Model cache budget 5.4 MiB, animation cache 2 MiB (level fields "Model / Animation Cache (MB)") | `0x2cae70` sets them; caches `0x4dae20` / `0x4db050` (+4 budget, +8 in use); `0x12c850` drops LRU entries | Model cache: yes. Surdana fight RAM dump: model cache 5.29 of 5.4 MiB, animation 1.6 of 2 MiB, texture 5.4 of 5.4. The model cache dropped 1.0 to 1.1 MiB already between boot and the Surdana town pose (`[engine-limits] model cache dropped`) | Raise the budget word before each trim | Medium: every MiB is guest heap (25 MB, shared with the level) | **Done** as an opt-in: `[general] model_cache_mb`, `animation_cache_mb` (0 = the level's) |
| 3 | Point lights per object: the sun plus the 3 nearest | `0x1c4148` builds the light set (16 slots), models ask for 4 (`0x1860a4`, `0x13af38`); `0x1e9fa0` uploads exactly 4 slots into VU1 `[13..20]` | Likely in torch-lit rooms and with spell lights (the 4th nearest light is ignored and pops in as the set changes); not counted yet | Needs a host lighting path or a wider VU1 light block; raising the 4 alone overflows the VU layout | High | Brief A |
| 4 | Terrain chunk record holds 10 texture refs (the disc uses at most 8) | chunk record 0x4c bytes, `ref[10]` at +0x14 (`level-format.md` 10.1) | Yes for converted OotF levels: crowded chunks are cut to single cells, 10 to 30 times the disc's chunks per view, 1 to 2.4 terrain passes per drawn cell (`ootf-performance.md`, `ootf-terrain-heap.md`) | Widen the record (about 10 functions) or draw converted ground through a host terrain path | High | Brief B |
| 5 | AI run limit: at most "AI Run Max" (level field, 4) creatures think at once; the farthest is paused | `0x2dc718` loads it into `0x5130bc`; `0x48a4a0` enforces (class field "Controlled by AI Limiter?") | Close: 2 to 3 of 4 active in the Surdana fight (RAM dumps every 5 s) | Scale `0x5130bc` after the load | Low technically, but it changes combat (more enemies act at once): a game rule, not a budget | Not lifted; owner decision (brief C) |
| 6 | Particle thinning on slow frames: a burst keeps every 3rd spawn when a frame takes over 40 ms, every 5th over 50 ms | `0x1951f0` (counter at system +0x80) | Only on hitches (loads, shader compiles); PC frames are 6 to 30 ms | Clamp the dt it tests | Low, but changes the look under real slowdowns | Listed |
| 7 | Particles per emitter: 2,500 live | `0x1951f0` (`slti 0x9c4`) | No: weather 244 to 300, the largest class default 600 | Only matters if densities are raised | Low | Listed |
| 8 | Ground marks of class `0x1005d`: at most 30 live | counter `0x513f50`, spawners `0x4918d0`, `0x491c68` | No: 0 in all seven dumps of the Surdana fight | Patch the 29 / 30 | Low | Not needed |
| 9 | SPU2 voices: 48, the lowest priority is stolen, a lower-priority new sound is refused | `0x1e5910`, victim `0x1e68c8` | No: peak 16 of 48 in the Surdana fight, 17 of 48 in the Ruined Village opening (500 ms voice snapshots) | Steal policy or a host voice bank | Medium / high | Not needed |
| 10 | Sound class limiter | `0x1b83c8` | Not a count: music and movie play one at a time, effects / ambient / GUI unlimited | none needed | | Not a limit |
| 11 | Memory card space | `MemoryCard.cpp` | No: the card reports 8 MB free always; 16 slots via `save_pages` | | | Not a limit |
| 12 | ODB class records per load | fixed 2026-11-01 (`ootf-odb-class-count.md`) | No longer | | | Done earlier |

For the other round 6 agents:

* Texture memory: the texture cache (`0x4db280`) was full in the Surdana fight (5.66 of 5.66 MB with the native
  budget); `DRAKAN_TEX_CACHE_KB` caps at 16 MiB in `drakan_texture_budget_policy.h`. The level field "Texture Cache
  (MB)" is applied by the same `0x2cae70` as the model cache. Procedural textures (the 64 x 64 animated water, the
  128 x 128 menu fire) are generated at PS2 size by the EE.
* Dynamic guest memory: raising the model / animation caches spends guest heap; with `PS2_RAM_SIZE` 64 MB, a pool for
  model and animation blocks above the retail RAM (like the terrain page pool) would make the cache raise free.
* Draw distance and detail: the shadow distance is now its own setting (`shadow_distance`), independent of
  `view_distance`; the instance batch of 25 same-model objects (`0x153b70`) only flushes, it drops nothing.

## What was lifted

Both are in `drakan_engine_limits.cpp` (commit `c752e4cf`), rules and tests in `runtime/world/include/world/EngineLimits.h`
(`world_engine_limits_test`). Both read `drakan.ini` at start; the old value is one setting away. No settings-menu
entry yet (the settings files were being edited by the view distance agent).

### Shadow distance, `[video] shadow_distance` (1 .. 4, default 1)

The level gives its projected character shadows a start and an end distance (Surdana and the Ruined Village: 15 and 30,
stored as squared distances 62,915 and 125,829 with a cutoff at 1.1 times the end). Past the end a character gets no
shadow at all, so on a 4K screen a villager across the Surdana square stands on nothing. The wrapper of `0x1431e8`
runs the setter and multiplies the three squared values by the factor squared; nothing else reads them, so nothing but
the shadow fade changes. Log: `[engine-limits] shadow distance: level 15 / 30, squared 566231 / 1132462 (x3.00)`.

Evidence (owner's ini, placed at the `surdana-town` pose, god mode, still): `evidence/town_villager_sd1_vs_sd3.png`, the
villager at the left edge of the square: no shadow at x1, a shadow at his feet at x3. Full frames:
`runs/town-sd1/town_b.png`, `runs/town-sd3/town_b.png`.

### Model and animation caches, `[general] model_cache_mb` / `animation_cache_mb` (0 = level's own, 1 .. 64)

`0x2cae70` sets the caches' budgets from the level ("Model Cache (MB)" 5.4 MiB in Surdana, "Animation Cache (MB)"
2 MiB); `0x12c850` drops the least recently used unreferenced entries until the cache is under budget, and a dropped
model is read from the disc again when it is next drawn. Measured with `DRAKAN_ENGINE_LIMITS_LOG=1`:

| Run | Model cache drops (trims that freed something) | Freed |
| --- | --- | --- |
| Surdana town pose, budget 5.4 MiB (`runs/town-cache0`) | 22, within 0.4 s after the placement | 1.44 MiB |
| same, `model_cache_mb = 12`, `animation_cache_mb = 12` (`runs/town-cache12`) | 0 | 0 |
| Surdana fight pose (`runs/fight-cache`), Ruined Village opening (`runs/ootf-cache`) | 0 (0.5 MiB in use in the Ruined Village) | 0 |

So the Surdana model set does not fit its PS2 budget: arriving in the square drops and reloads about a quarter of the
models in a burst. A frame-time cost of the burst was not measured. The raise spends guest heap (the 25 MB newlib heap,
which also holds the level), so it is opt-in until the dynamic guest memory work gives model and animation blocks a
home above the retail RAM; suggested values once it does: 12 / 8.

## Briefs for further agents

### Brief A: more than three point lights per object

* Goal: every light in range lights a model (torches in rooms, spell lights), without lights popping as the nearest
  three change.
* Where: light set builder `0x1c4148` (sun first, then candidates sorted by distance into `0x56b6c0`, added while the
  count is under the caller's limit; set of 16 slots of 0x34 bytes, count at +0x20); callers pass 4 for models
  (`0x185db0` at `0x1860a4`, `0x13af38`) and 16 for terrain (`0x14f708`); upload `0x1e9fa0` and the batch twin near
  `0x1e05f8` write exactly 4 slots into VU1 `[13..20]` (`graphics.md` 3.3, flag bits 0xC000).
* Ways: (1) host lighting: keep the guest's 4-slot upload, but record the full candidate set per object and add the
  remaining lights in the renderer's vertex path (only where the GPU vertex path or records mode already re-evaluates
  vertices; `drakan_vertex_path.cpp`); (2) a wider VU1 light block in the interpreted microcode plus a patched upload:
  touches the VU1 memory map that every program shares.
* Risks: the CPU vertex path is the default and must stay exact; lighting is baked into vertex colours, so a host
  path needs the normals the microcode reads; DLSS motion and ray paths take the same records.
* Proof needed: a counter first (`0x1c4148`: candidates after the sort against the accepted count) in a torch-lit
  interior (Catacombs 1C, Inquisitor's Lair 1E) and OotF interiors; before/after pictures of a model walking past more
  than three torches; replays identical with the option off.

### Brief B: terrain chunks with more than 10 textures

* Goal: converted OotF ground keeps every cell's texture without cutting chunks to single cells, so a view has disc-like
  chunk counts (draws, VU1 runs and draw pages fall by up to 10 times).
* Where: the 0x4c-byte chunk record (`ref[10]` at +0x14, weight-array descriptor at +0x3c), read by `0x165840`, built by
  `0x165f68`, drawn by `0x158148` and the page builder `0x154060`; about 10 functions use the stride (`ootf-texture-limit.md`).
* Ways: (1) a host-side chunk table: wrap the reader to keep up to 32 refs per chunk in a side table above the retail RAM
  and wrap the few readers of `ref[]`; (2) a converter that groups cells by texture set instead of by rectangle (no
  engine change, fewer cut chunks); (3) a host terrain draw path from the height field (the renderer's job).
* Risks: the record is read by collision and the splat cache too; a missed reader corrupts the heap (as 11 and 12
  textures did on 1 November).
* Proof needed: the Islands and Wartok Canyons views of check A / B with chunk and draw counts before / after, frame
  times under the bench lock, F5 / F8 and a level change.

### Brief C (owner decision): the AI run limit

* "AI Run Max" (level field, 4 on the disc levels; `0x5130bc`) lets at most that many creatures run their AI; a
  farther one is paused until a slot frees. In the Surdana fight 2 to 3 of 4 slots were in use. Raising it makes more
  enemies act at once (harder fights, more CPU). Host lift: scale `0x5130bc` after `0x2dc718`. It is a game rule, so it
  needs the owner's yes and a combat check, not only a picture.

### Brief D: model and animation blocks out of the guest heap

* Goal: make the cache raise above free, by serving the model and animation caches' allocations from a pool above the
  retail 32 MB like `drakan_terrain_pages.cpp` does for terrain pages; then turn `model_cache_mb` on by default.
* Proof needed: heap high-water before / after over a campaign level change chain; no `[memset] bad size`; the
  drop counter at 0 in the Surdana square.

## Not a limit on PC (measured)

* SPU2 voices: peak 16 of 48 (Surdana fight), 17 of 48 (Ruined Village opening). No voice was refused or stolen.
* Sound class limiter: a one-at-a-time rule for music and movies, not a count.
* Ground marks (30), particles per emitter (2,500), memory card space, the sound batch ring (128 entries, flushed when
  full; `iop-and-io.md` 3.3 says 64).

## Order of the Flame and Drakan 2 guard

* Drakan 2, Surdana (guard scene): town pose at x1 and x3 (above), the three-Wartok fight at x1 with the new exe
  (`runs/fight-cache`, fight and quit clean), the town pose with both caches at 12 MiB (`runs/town-cache12`, clean).
* Order of the Flame, Ruined Village opening (`runs/ootf-sd1`, `runs/ootf-sd3`, the `dist` pack, no input, frames at
  20 s and 35 s after gameplay start: `evidence/ootf_o20_sd1_vs_sd3.png`, `ootf_o35_sd1_vs_sd3.png`): the level sets the
  same 15 / 30, the scene plays the same way at x3. Its camera stays close to Rynn and the dying villager, so the
  frames show no shadow that x1 lacks; a played OotF scene with characters across a square was not taken (it needs a
  save past the opening on this pack). The 60 s run's last frame was not written (the harness reported the run ended
  without its scripted quit, on both settings and on the earlier sound run, so not from this change).

## Not verified

* Frame time of `shadow_distance` 3 (the bench under the turn lock is listed below if it ran).
* A played (not placed) walk with the new shadows, and OotF shadows of far characters.
* That the model cache drops cost a visible hitch; that a raised cache never exhausts the guest heap over a long
  session (it stays opt-in for that reason).
* The light, terrain chunk and AI items are static readings with addresses; only the AI slot counts were read in game.
