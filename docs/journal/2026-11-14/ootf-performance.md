# Order of the Flame performance: where the time goes, what the converter can and cannot change, the frame queue (2026-11-14)

Round 3, performance agent. Scratch: `build/review/ootf-perf/` (play folder, runs, packs, tools; not tracked). Owner's real
`drakan.ini` (3840x2160 borderless, DLSS quality, HD textures, internal scale auto) copied from `build/review/baseline-play`,
a private ELF copy, the installed pack (`dist/DrakanPC/mods/ootf-levels.zip`, build `1b9f70fdd8171211`) and the exe of
`main` at `7e3aeaa5` built into the play folder (incremental build, nothing to recompile; same exe as `dist`).

## 1. Result in short

* **The cause is the converted terrain, and it is structural.** Order of the Flame ground changes texture nearly every
  cell. The engine draws terrain one pass per texture per chunk, with at most 8 textures per chunk, so the converter
  has to cut converted layers into chunks of 1 to 7 cells with about 7 passes each. The disc levels have chunks of
  50 to 80 cells with 2 to 3 passes. Per drawn cell that is 1 to 2.4 passes on the converted levels against 0.03 on
  the disc (table 3.1), and every pass is its own draw because the texture changes between them.
* Measured per world frame at the start poses: **Wartok Canyons 5,200 to 6,000 draws, 16,600 VU1 runs, 20,000 GIF
  packets; Ruined Village 1,600 to 2,800 draws; Grimstone Mines 700 to 840; Surdana 232; Northern Tundra 285**.
  In the Ruined Village recording 91 % of all VU1 runs are terrain (Surdana: 21 %). Objects and actors are not the
  problem: Surdana has more resident model instances (1,215) than the Ruined Village (548).
* **No converter fix with a real gain keeps the picture.** The best re-cut of the chunks that keeps every triangle's
  blend order (built, tested, measured, not landed) removes 1 to 4 % of the passes per level and 0.2 to 0.8 % of the VU1
  runs in play. A free re-cut would remove about 20 % but changes the blend order at chunk seams; merging duplicate
  textures removes 1 to 5 % of the textures and changes the blend where both meet. Details in section 4.
* **Where the gain is: the renderer and the guest per pass**, not the content: one draw per terrain pass on the GS
  worker (`drawSubmit` about 10 us per draw in the canyon, about half the worker's time) and the EE building a VU1
  packet per pass. Section 5 lists what would pay, with estimates; none of it is in this agent's remit (renderer backend
  files) or proven.
* **The huge terrain texture coordinates are not from the converter's 16-bit UVs.** They are produced in VU1 memory by
  the microcode itself (section 6). What is drawn is correct; after the 12 November VU1 change they cost nothing.
* **Frame queue** (`PS2X_FRAME_QUEUE=1 PS2X_READBACK=late`) on a quiet machine: no frame-time change in any Order of
  the Flame scene (the canyon, mine and tundra poses do not read back at all; the Ruined Village is held at 60 fps
  with DLSS and is not EE bound without it), EE CPU minus 32 to 47 % and 1 % / 0.1 % lows 20 to 33 % shorter in the
  Ruined Village, Surdana without DLSS 8 % faster. Keep it off until a contention check and a played session
  (section 7).

## 2. Conditions

* The machine was shared with the four other round-3 agents throughout: one to three other `drakan.exe` (4K games of
  the water, corpses, items and spells agents) and at times a replay tool; the GPU sat at 98 to 99 % utilisation from
  those games in most runs. Counts per world frame (draws, VU1 runs, GIF packets, texture creations) do not depend on
  the load and are used for the ranking. Frame times from runs with other games are reported but not used for a
  conclusion; the gated quiet-machine runs are in section 7.
* Tools: `scripts/perf/scene_ab_bench.py` (shared turn lock, `GSR_PROFILE=2`, `GSR_GPU_PASS_TIMING=1`, `PS2X_PERF=1`)
  with three new scenes in `cpu_scene_bench.py` (`ootf-canyons`, `ootf-islands`, `ootf-mines`: direct starts as the
  editor's Play does them). A direct start runs as Drakan 2, which does not get the campaign's 8 MiB texture cache
  (`drakan_texture_budget`); without it the canyon start stayed on the loading screen for the whole 300 s run with
  140 texture creations per frame, so the scenes set `DRAKAN_TEX_CACHE_KB=8192`, the campaign's value. (The
  `ootf-volcanic` numbers of `perf-cpu.md`, 82.7 ms per frame, were taken without it.)
* Poses: each level's start (Ruined Village after the opening, Wartok Canyons on Arokh looking down the canyon,
  Grimstone Mines inside the entrance). Placed by the level start, no input; nothing was played.
* Counting scripts (scratch): `build/review/ootf-perf/tools/counts.py` (per world frame from a run log),
  `terrstats.ts` (chunk and pass counts of every level, disc and pack), `vurclass.py` (VU1 runs by kind in a
  recording).

## 3. Measurements

### 3.1 Terrain structure (offline, every level)

Passes = texture refs summed over all chunks (one draw each when in range). Disc: the PAL ISO; pack: the installed pack.

| Level | drawn cells | chunks | passes | cells per chunk | passes per chunk | passes per drawn cell |
| --- | --- | --- | --- | --- | --- | --- |
| disc Surdana (LEVEL1NA) | 70,746 | 900 | 2,329 | 78.6 | 2.59 | 0.033 |
| disc LEVEL4LD | 113,654 | 1,497 | 2,672 | 75.9 | 1.78 | 0.024 |
| disc LEVEL2RE (most passes per cell of the disc) | 15,320 | 475 | 1,621 | 32.3 | 3.41 | 0.106 |
| OotF Ruined Village | 20,613 | 3,105 | 22,111 | 6.6 | 7.12 | 1.07 |
| OotF Wartok Canyons | 33,296 | 9,931 | 71,562 | 3.4 | 7.21 | 2.15 |
| OotF Grimstone Mines | 12,588 | 2,203 | 14,949 | 5.7 | 6.79 | 1.19 |
| OotF Islands | 54,559 | 18,542 | 131,965 | 2.9 | 7.12 | 2.42 |
| OotF Succubus Lair | 7,336 | 1,237 | 7,712 | 5.9 | 6.23 | 1.05 |

Every OotF level is in that range (Alwarren 2.97, Islands Night 2.27, Volcanic 1.36 passes per cell). The engine draws
the passes of a chunk while it is nearer than the material's splat fade distance (30 cells for the ground material the
converter uses, as for most disc ground), so in an open view the canyon draws thousands of them.

### 3.2 Per world frame at the start poses (busy machine; counts are load independent)

| Scene | draws | VU1 runs | GIF packets | GIF MB | texture creations | resident instances / meshes / textures |
| --- | --- | --- | --- | --- | --- | --- |
| OotF Wartok Canyons | 5,184 to 6,038 | 16,575 to 16,858 | 20,243 to 20,613 | 14.7 | 9 to 13 | n/a |
| OotF Ruined Village | 1,613 to 2,777 | 5,654 to 5,690 | 8,573 to 8,625 | 6.5 | 2.2 to 5.5 | 548 / 102 / 470 |
| OotF Grimstone Mines | 700 to 842 | 2,972 to 2,980 | 8,024 to 8,090 | 4.9 | 1.5 to 1.7 | 18 / 9 / 191 |
| Surdana (disc) | 232 | 4,314 | 10,241 | 8.4 | 3.1 | 1,215 / 86 / 196 |
| Northern Tundra spot C (disc) | 285 | 2,656 | 11,859 | 8.7 | 1.0 | 28 / 12 / 217 |

The draw count varies between runs of one exe (the renderer joins consecutive draws of the same state, and the
position of animated actors changes what follows what); the VU1 and GIF counts do not.

VU1 runs by kind, from the 16,000-run recordings of `perf-cpu.md` (`vurclass.py`): Ruined Village 14,557 terrain runs
(flag 0x1000) and 1,443 others; Surdana 3,356 terrain and 12,644 others. A converted terrain pass is a texture
coordinate stage over the chunk (0x126c) followed by strips of 6 and 16 vertices (0x10ec, 0x106c): 12.5 vertices per
terrain run on average.

### 3.3 Frame times seen on the busy machine (not a conclusion)

| Scene | DLSS | world fps | EE CPU ms | VU1 worker ms | GS worker ms | Present ms | other games |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Wartok Canyons | quality | 9.8 | 40.5 | n/a | n/a | 0.09 | 2 (GPU 98 %) |
| Ruined Village | quality | 53.9 | 16.3 | 7.3 | 9.4 | 5.7 | 1 to 2 |
| Ruined Village | off | 48.2 to 83.8 | 14.4 | 7.3 | 8.1 | 9.6 | 1 to 2 |
| Grimstone Mines | quality | 32.0 to 37.0 | 9.4 | 6.2 | 6.2 | 22.1 | 2 |
| Grimstone Mines | off | 43.4 to 58.2 | 7.8 | 5.2 | 4.9 | 14.9 | 2 |
| Surdana | quality | 63.4 | 10.8 | n/a | n/a | 8.0 | 1 + replay |
| Northern Tundra C | quality | 40.3 | 10.0 | 7.8 | 8.7 | | 2 |

In the canyon the GS worker spent 950 to 1,150 ms of every 2 s in `drawSubmit` for 100,000 draws (about 10 us a
draw), and the EE 18 to 23 ms per world frame building about 16,600 VU1 runs: both scale with the terrain passes.

## 4. Converter changes tried

### 4.1 Re-cutting the chunks without changing a pixel (built, measured, not landed)

What a pass draws on a triangle depends only on the cells around the triangle's corners (the weight bytes are counts
of the four cells around a vertex), not on the chunk; but the passes are blended in the chunk's list order, and the
bisection that cuts the chunks is greedy. A dynamic programme over the sub-rectangles of each 12 x 12 base chunk can
find the guillotine cut with the fewest passes, under two rules that keep the picture: every triangle keeps the
texture order it had (a topological order of those constraints must exist), and cells on a layer edge (where the
"outside counts as every texture" rule makes the weights depend on the chunk) keep their exact list. Built in a private
worktree of the formats submodule (`mergeChunks` in `terrainblob.ts`, `DRAKAN_TERRAIN_MERGE`), with a test that reads
both blobs back and checks the same textures in the same order with the same weights on every triangle; the diff is
kept in `build/review/ootf-perf/tools/terrain-merge.diff`.

| Measure | without | with |
| --- | --- | --- |
| passes, Wartok Canyons / Ruined Village / Islands (pack) | 71,562 / 22,111 / 131,965 | 69,813 / 21,847 / 126,587 |
| VU1 runs per world frame, canyon start (same exe, the two packs, one run each) | 16,703 | 16,575 |
| VU1 runs per world frame, Ruined Village start | 5,690 | 5,680 |

1 to 4 % fewer passes and under 1 % fewer VU1 runs in play: not worth a pack rebuild and a new campaign game, so it was
not landed. Without the order rule the same search removes 19 to 21 % of the passes (`tools/partition.ts`), but then
triangles at former chunk seams change their blend order (A over B instead of B over A), which changes pixels; the
current order is itself arbitrary per chunk (first appearance in the chunk's window), so this would be a choice of a
new, consistent order rather than a fix, and it is still only a fifth of the passes.

### 4.2 Other content causes checked

* **Duplicate textures**: 933 converted terrain textures hold 822 distinct images; per level 1 to 5 % of the used
  textures are duplicates (Islands 222 to 211). Merging them changes the blend where both meet (one pass with the summed
  weight instead of two passes); not done.
* **Alpha of converted ground textures**: palette alpha 0x7f, the disc's value for opaque colours; flags word 0 like the
  disc's opaque tiles, so no second (noise) pass. Nothing to fix.
* **Objects**: converted levels draw fewer object VU1 runs than Surdana (1,443 against 12,644 in the recordings); no
  helper or marker objects showed up as a cost. Not looked at per object.
* **Texture cache**: inside the campaign the 8 MiB budget holds the working set (1.5 to 5.5 renderer texture creations
  per frame at the start poses, Surdana 3.1). Outside the campaign (a direct start as Drakan 2) the canyon thrashes the
  native 5.4 MiB cache; that only affects test runs.

## 5. What would pay (not built)

| Item | Where | Expected effect | Risk / effort |
| --- | --- | --- | --- |
| Join consecutive terrain splat draws that differ only in texture into one draw (texture array or atlas of the ground tiles, index per vertex) | renderer backends (`d3d11_*`, `wgpu_*`), the renderer agents | the canyon's 5,000 to 6,000 draws towards the disc's few hundred; most of the GS worker's 50 % in `drawSubmit` | medium: needs the terrain classification (`terrain::classify`) and a texture array path; 2 to 4 days |
| Cheaper per-draw submit on Direct3D 11 (one Map per frame instead of per draw; see `gpu-vertex-batch-draw.md` "cheapest fixes") | renderer | helps every scene; most where draws are tiny | medium; 1 to 2 days |
| Stage 0x126c transforms 225 entries for every converted pass whatever the chunk size | guest (EE sets the count) | VU1 worker time only, which is not the bound | low gain |
| A setting below 1 for `terrain_distance` (pass radius) | runtime option, opt-in | passes fall with the square of the radius; mid-range ground blurs earlier | owner decision; visible change |

## 6. The huge terrain texture coordinates

Read from the Ruined Village recording (`build/review/perf-cpu/vurec/ruined.vur`, scratch tools `vur2.py`, `vur4.py`,
`vur5.py`): the ST values at FLT_MAX sit in VU1 data memory 516 to 740, the third vertex set. The first run of each
converted terrain pass (flags 0x126c, header `[9]` = (516, 225)) runs the texture-coordinate stage at 0x0468 to 0x0518 of
the microcode over all 225 entries of that set, **in place**, with the matrix in rows 23, 24 and 26. The batches that
follow upload only their own few vertices (6 or 16) into the start of the set. So the entries behind them are never
refreshed: they keep what earlier passes left there, and each pass whose matrix is not the identity multiplies them
again. Converted chunks carry such matrices on some passes (seen: scale 33.39 and 32.51, offsets of -6.83 and -16.32,
the offset changing by exactly 1.0 between neighbouring chunks); disc passes in the Surdana recording do not grow
anything. Within a few frames the stale entries reach the clamp (0x7f7ffffe and the like). The GIF packets of the same
runs carry ordinary coordinates (s 0.22, t 0.50, q from the depth), so what is drawn is right. So: not produced by the
converter's 16-bit cell UVs, not visible, and since the 12 November lanes rule (`perf-cpu.md` 4.1) not a cost. Not
traced: which guest routine writes the non-identity matrix for converted chunks and why the disc never has one there.

## 7. Quiet machine: every level, and the opt-in frame queue

From 22:30 to 23:05 the other agents had no game running: 29 of the 30 runs below started and ran with no other
`drakan.exe`, no replay tool and no compiler (`before` and `peak during` both 0 games, GPU 0 % at the start); the first
Ruined Village `poll` run started beside one other game and is kept in the ranges but left out of the pairs. One exe,
the installed pack, `scene_ab_bench.py` with `--cycle video.dlss=quality,off`, three repeats per variant, order rotated,
8 s windows, `poll` = default, `pipe` = `PS2X_FRAME_QUEUE=1 PS2X_READBACK=late`. Runs: `build/review/ootf-perf/runs/q3-*`.

### 7.1 Where each level stands (default `poll`, median of three; CPU per world frame)

| Scene | DLSS | world fps | frame ms | 1 % low ms | 0.1 % low ms | EE ms | VU1 worker ms | GS worker ms | draws per frame |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| OotF Ruined Village | quality | 60.6 | 16.66 | 27.4 | 28.4 | 14.59 | 6.71 | 8.50 | 1,600 to 2,800 |
| OotF Ruined Village | off | 86.2 | 11.62 | 17.3 | 21.3 | 10.36 | 5.79 | 6.69 | |
| OotF Wartok Canyons | quality | 35.1 | 28.46 | 34.4 | 39.2 | 19.35 | 17.08 | 21.05 | 5,270 to 6,040 |
| OotF Wartok Canyons | off | 53.7 | 16.91 | 29.3 | 33.2 | 18.47 | 16.88 | 17.92 | |
| OotF Grimstone Mines | quality | 101.4 | 9.90 | 10.7 | 11.7 | 6.74 | 4.11 | 5.01 | 700 to 840 |
| OotF Grimstone Mines | off | 139.0 | 7.09 | 9.7 | 11.2 | 6.05 | 3.97 | 3.90 | |
| Surdana (disc) | quality | 148.5 | 6.49 | 9.3 | 10.2 | 5.91 | 5.74 | 5.24 | 232 |
| Surdana (disc) | off | 160.8 | 6.00 | 8.8 | 10.1 | 5.72 | 5.70 | 4.48 | |
| Northern Tundra spot C (disc) | quality | 127.2 | 7.79 | 10.8 | 12.7 | 6.73 | 5.91 | 6.20 | 285 |
| Northern Tundra spot C (disc) | off | 112.1 | 8.97 | 10.0 | 11.5 | 6.16 | 5.93 | 4.86 | |

* **Wartok Canyons is the slow one**, and it is bound on all three threads at once (EE 18 to 19 ms, VU1 worker 17, GS
  worker 18 to 21 for 16,700 VU1 runs and about 5,300 draws a frame): what the terrain passes cost. With DLSS the GS
  worker's draw scope rises from 8.3 to 18 ms per frame (the per-draw motion and guide work multiplies with 5,300
  draws), so DLSS quality costs 11.5 ms there against 0.5 ms in Surdana.
* **The Ruined Village with DLSS quality is held at exactly 60 world fps** (16.59 to 16.66 ms in all quiet runs, 117
  presents a second), but no thread is near that: with the frame queue the EE needs 7.7 ms, the VU1 worker 4.9, the GS
  worker 8.2, and without DLSS the same view runs at 86 fps. The 13 November note called it EE bound; it is not. The
  limiter was not found (not the guest probe wait, which the frame queue removes; the GPU's world passes take about
  1.5 ms). Open item for the renderer sessions.
* Grimstone Mines (interior) runs at 101 / 139 fps: the interior is small, its 700 to 840 draws do not limit.

### 7.2 The frame queue, pairs on the quiet machine

| Scene | DLSS | frame ms poll -> pipe | pairs | 1 % low ms | 0.1 % low ms | EE ms | read-backs per frame |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Ruined Village | quality | 16.64 -> 16.59, 16.66 -> 16.59 | held at 60 either way | 27.2 to 27.4 -> 18.9 to 19.5 | 27.9 to 28.4 -> 20.2 to 20.5 | 14.1 to 14.6 -> 7.4 to 7.9 | 1.0 |
| Ruined Village | off | 11.19 -> 11.72, 11.62 -> 11.64 | +0.1 to +4.7 % | 16.7 to 17.3 -> 13.6 to 13.9 | 21.3 -> 14.2 to 14.7 | 9.9 to 10.4 -> 6.6 to 7.1 | 1.0 |
| Wartok Canyons | quality / off | 28.46 -> 28.49 / 16.91 -> 17.16 | -0.2 to +0.3 % / -2.0 to +1.9 % | no change | no change | no change | 0 |
| Grimstone Mines | quality / off | 9.90 -> 9.86 / 7.09 -> 7.11 | within 1 % | | | | 0 |
| Surdana | quality | 6.49 -> 6.40 | 2 of 3 faster, -2.3 to +2.1 % | 9.3 -> 8.6 | 10.2 -> 10.7 | 5.91 -> 4.02 | 1.0 |
| Surdana | off | 6.00 -> 5.47 | 3 of 3 faster, -7.0 to -13.1 % (ranges apart) | 8.8 -> 8.2 | 10.1 -> 9.3 | 5.72 -> 3.70 | 1.0 |
| Northern Tundra C | quality / off | 7.79 -> 7.88 / 8.97 -> 9.01 | within 3 % | | | | 0 |

* The frame queue only acts where the game reads the probe image back every frame (Ruined Village and Surdana at these
  poses); where it does not (canyon, mine, tundra) the two variants are the same within the run-to-run spread.
* **Order of the Flame frame time: no gain.** The Ruined Village is held at 60 with DLSS and is not EE bound without
  it; what the queue buys there is the EE's spin (EE CPU minus 47 % with DLSS, minus 32 % without) and **steadier
  frames**: the 1 % low interval falls from 27 to 19 ms (DLSS quality) and from 17 to 14 ms (off), the 0.1 % from 28
  to 20 and from 21 to 14 ms. Surdana without DLSS is the one frame-time gain (8 %).
* Under load it went the other way: on the busy machine (one or two other 4K games, GPU at 98 %) the two Ruined Village
  `pipe` runs ran at 41 and 49 ms against 16.8 ms for the one `poll` run beside them, with `Present` at 10 ms a call
  (`busy-q1-ootf-ruined`). One `poll` sample is not a result, but it is the opposite of the quiet machine and nobody has
  measured the queue with another GPU load on purpose.
* Risks as known (`perf-round2.md`): the game sees the probe image one frame late, which changes the bytes it reads in
  about two reads per thousand; input latency rises by at most one frame; menus, movies, save and load and level changes
  were exercised only by one unskipped opening run per game (pass). Not re-verified here.

**Recommendation: keep it off by default for now; turn it on after two checks.** It never cost frame time on the quiet
machine, it removes the EE's spin and it gives clearly better 1 % and 0.1 % lows in the Ruined Village, so it is worth
having. Before it becomes the default: (1) the contention case above, interleaved, with a second GPU load started on
purpose (an hour), and (2) a played session with the owner's settings through menus, save and load, a level change and
a movie (an hour). If both are clean it should be on, with `PS2X_FRAME_QUEUE=0` as the way back.

## 8. Tests and builds as they ran

* Game build: one incremental `Build-LocalGame.ps1 -Incremental -WorkDir build/final-game` into
  `build/review/ootf-perf/play` under the build lock (nothing to recompile; exit 0). No game code was changed.
* `scripts/perf/cpu_scene_bench.py`: three scenes added (commit `7e3aeaa5`); ruff check and format clean.
* The chunk re-cut (not landed): `vitest` on the private worktree, `terrainblob`, `terrain`, `terrainwrite` (36 tests)
  and the new `terrainmerge` test (2) passed; `tsc --noEmit` clean; two full 23-level conversions (with and without the
  switch) differ only in the 22 level files and the build stamp.
* Game runs: about 70 scripted runs of 60 to 300 s with the owner's ini (start poses, no input, god mode); all ended
  with exit 0 except the Drakan 2 direct start without the 8 MiB cache (stuck on the loading screen until the run
  limit) and the runs stopped on purpose when a queue was restarted.

## 9. Not verified

* Nothing was played: every number is a level start pose with no input; no walk, no fight, no other pose per level.
* The Islands and Succubus Lair were counted offline only (table 3.1), not run.
* Frame times on a busy machine (section 3.3 and the contention remark of 7.2) are not results.
* Why the Ruined Village is held at 60 fps with DLSS quality.
* That the joined terrain draws of section 5 would give the estimated gain: nothing of it was built.
* Which guest routine writes the non-identity texture matrix of converted terrain passes (section 6).
* The frame queue's input latency, its behaviour with vsync on and a frame limit, and its contention case.
