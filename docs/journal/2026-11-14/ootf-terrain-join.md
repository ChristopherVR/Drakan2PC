# Order of the Flame terrain: joining the ground passes that differ only in their texture (2026-11-14)

Round 5, terrain join agent. Scratch: `build/review/ootf-terrain-join/` (worktree `wt`, runtime-tests tree `rt`,
replay binaries `bin_base`/`bin2`, hashes, play folder `play` with the owner's `drakan.ini`, a private ELF copy and the
installed pack `ootf-levels.zip` of `dist/DrakanPC` (sha256 `ddf06d6d...`), played tours `played/`, benchmark runs
`runs/`; not tracked). Follows [ootf-terrain-gpu.md](ootf-terrain-gpu.md) section 5 and
[ootf-performance.md](ootf-performance.md).

## 1. Result in short

* **What a converted ground pass looks like to the renderer.** In a Ruined Village frame (draw log of the 12 November
  capture, frame with 1,538 draws) the terrain is long runs of draws with the same GS state except TEX0: PSMT8
  128 x 128 tiles with mips, each with its own image address, its own palette (CBP) and CLD 1, so every pass loads a
  palette; 6 to 12 vertices (two to four triangles) per draw is typical, up to about 190. The palette load and the
  TEX0 change each ended the core's batch, so every pass went through the whole per-draw path of the backend
  (`gs_draw.inl`: classification, texture resolve, constants, bindings, velocity pass with DLSS).
* **What was built: `[video] terrain_batching`** (env `GSR_TERRAIN_BATCHING`, default **0** for now, commit
  `5753cfa6`). The core keeps such passes in one batch as segments (`runtime/gs/src/draw_join.h`): the next
  primitive joins when its state differs from the open batch only in the TEX0 image and palette address fields
  (TBP0, TBW, CBP, CSA, CLD), the mip addresses and the palette serial; a palette load between them keeps the batch open
  and gives the segments drawn so far a copy of their palette (not when the palette's blocks lie in the batch's frame
  or depth buffer pages). The backend gets the segments together (`BackendApi::drawSegments`); the shared draw code
  (`gs_draw.inl`) classifies the draw once, resolves the texture of every segment, binds and uploads the constants
  once and draws the segments in order, switching only the texture between them (Direct3D 11: one SRV bind and one
  `Draw(count, first)` per segment, the velocity pass the same way after the colour pass; WebGPU: `setTexture` and a
  `draw` per segment). Same code for both backends; the two backends add only the per-segment draw loop.
* **Why the picture stays the same.** The GPU blends the primitives of one draw call in their order, and a joined
  draw issues the same triangles with the same texture each in the same order. The shared code only joins when that is
  exactly true, and otherwise draws the segments one by one through the old path: fixed-function blend (the shader
  blend reads a copy of the target taken before the draw, which would miss the earlier segments), no alpha-failure
  pass, no DATE, no frame mask, a depth-tested draw, no HUD mapping, loading-screen or world-tail rule that looks at the
  vertices, no ray, water or surface consumer, no terrain-detail layer, no object-motion scope that would be tracked,
  and every segment's texture resolving to a non-target texture with the same upscale factor and mip count and no
  menu, font or material artwork. Each segment is clipped as its own draw was; one its draw would have left out is
  left out (no texture resolve, no draw), and the dirty rows are the union of the kept segments' boxes.
* **Same pixels.** Replay hashes, `--texdir build/review/gs-share/textures_frozen`, batching off against on, every
  frame of every capture identical (table 4). The new exe with batching off hashes identically to the exe before the
  change (Ruined Village, scale 1). In the Ruined Village capture 673,683 passes (83 % of all draws) are joined into
  103,265 draws (6.5 passes each), 139 batches refused (drawn pass by pass).
* **In the game** (Wartok Canyons start, DLSS quality and off, one run): 4.95 million of 6.34 million draws joined
  into 677,000 (7.3 passes each), 1,778 refused, so the DLSS velocity pass and the object-motion bookkeeping do not
  stop the joins. A played Wartok Canyons tour with batching on and the same tour with it off (section 5) look the
  same.
* **Frame time: not measured yet** (section 3); the default stays 0.
* **The remaining per-pass cost** is the texture resolve (about 0.9 us per pass on the GS worker: 425,000 resolves
  per 2 s in the canyon, 390 ms, of which the coherence flush is 170 ms and the target sync 80 ms) and, on the EE and
  the VU1 worker, the guest's own per-pass work, which this change does not touch.

## 2. Bug found by the unit test

The first version gave a palette copy only to the newest segment when a palette load arrived. Segments that joined
without a palette load between them (two textures sharing the live palette) kept pointing at the live palette and
were drawn with the next one. The replays did not show it (every converted pass loads its own palette), the GPU-free
test did: 538 to 605 of about 3,990 draws per random stream differed. Fixed before the commit (`keepPalette` gives the
copy to every segment since the last copy); 0 differ.

## 3. Measurements

Not done on a quiet machine. `scene_ab_bench.py` (turn lock, owner's ini, `--cycle video.dlss=quality,off`, variants
`old:GSR_TERRAIN_BATCHING=0` and `new:GSR_TERRAIN_BATCHING=1`, 3 repeats, runs `runs/ab1-*`) ran for two hours while the
terrain heap and Islands landing agents had games going: 4 runs, every `old` run beside 2 other games (and a replay),
so no pair is usable. The queue was stopped on purpose (it only added GPU load for the others) and waits for a quiet
window. What exists, not a result: Wartok Canyons with batching on, beside at most one other game, 23.8 / 30.2 ms
(DLSS quality) and 20.4 / 22.9 ms (off), GS worker CPU 20.2 to 20.4 ms (quality) and 16.4 to 18.2 ms (off) per world
frame. The GS worker profile of the probe run (`runs/probe-canyons`) shows where the joined path spends its time:
per 2 s 100,000 draws, 427,000 texture resolves (392 ms, coherence flush 176 ms, target sync 80 ms), submit 62 ms.
**Default stays 0** until the A/B is done.

## 4. Replay hashes (Direct3D 11 unless noted; batching off against on; frozen HD textures)

| Capture | scale | frames | joined draws / passes | refused | result |
| --- | --- | --- | --- | --- | --- |
| Ruined Village 30 s walk (`ootf`) | 1 | 4,074 | 103,265 / 673,683 | 139 | identical |
| Ruined Village | 3 | 4,074 | 103,265 / 673,683 | 139 | identical |
| Ruined Village, WebGPU | 2 | 4,074 | 103,265 / 673,683 | 139 | identical |
| Surdana | 3 | 2,585 | 9,996 / 28,839 | 0 | identical |
| cave | 3 | 5,558 | 11,835 / 39,323 | 0 | identical |
| Ravenshold flight | 3 | 1,864 | 5,803 / 15,577 | 0 | identical |
| tab menu | 3 | 3,984 | 12,287 / 31,481 | 0 | identical |
| Ruined Village, old exe against new exe with batching off | 1 | 4,074 | | | identical |

Hashes and logs: `build/review/ootf-terrain-join/hashes/`. The replays have no DLSS and no camera notes (the engine
world phase is never open in a replay, `nativeMenu` stays set from the title), so the velocity pass and the world-phase
rules are covered by the game runs, not by hashes.

## 5. Played

`played/tour1` (batching on) and `played/tour0` (off), the same scripted tour with checker B's tour plan
(`build/review/ootf-check-b/plans/tour.py`, copied), owner's ini (DLSS quality, HD textures), god mode, direct start of
Wartok Canyons as the editor's Play does it, the installed pack: dismount Arokh, walk to two spots (placed 1,600 units
short when the walk did not get there), look round, approach and swing at the nearest creature (class 0x10028; no hit
landed, its hit points stayed 15), F5, walk away, F8 (back at the saved spot), placed in front of the mine entrance,
walk into the level exit, the level-end save screen, Grimstone Mines loads, look round, F5, walk, F8 (back at the
saved spot). Both exit 0. Contact sheets: `played/sheet-tour1.png` (batching on, 15 shots),
`played/sheet-pair.png` (eight shots of the same placements off and on, side by side, brightened). Looked at: same
terrain, textures, blend borders, HUD and save screens; animated characters differ in pose only. What was placed: the
two spots after a failed walk, the mine entrance; what was played: the walks, the fight attempt, the saves and loads,
the walk into the exit.

## 6. Tests and builds as they ran

* `gsr_test_draw_join` (new, GPU free, `runtime-tests`): the join rule (each format, size, function, mip count, blend,
  test, frame, flag field refuses; sprites and untextured draws refuse), the target block range, and 20 random streams
  of 4,000 passes (texture changes, palette loads before 3 of 4 passes, state changes that do not join, sprites, batch
  ends) through a model of the core without and with joining: every segment handed to the backend equals the draw the
  old path made (vertices, state, palette); 62,316 segments in joined batches. Passes; failed 20 of 20 streams before
  the fix of section 2.
* `gsr_test_draw_state_cache`: passes.
* Private runtime-tests tree of the worktree: `gsr_replay`, `gsr_replay_webgpu`, both tests built at every step.
* Game: one incremental `Build-LocalGame.ps1 -Incremental -WorkDir build/final-game` of `main` with the change under
  the build lock into `build/review/ootf-terrain-join/play` (56 s, exit 0), with `-TextureDirectory` pointing at the play
  folder's own `textures` (a junction to `baseline-play/textures`) so nothing was staged into it.
* clang-format 19 on the changed files; `scripts/repository_guard.py`: 0 prohibited.

## 7. Not built, next

* **One draw call for a joined batch on Direct3D 11** (a texture per segment selected in the pixel shader from
  `SV_PrimitiveID` and a boundary table in the constants): would remove the per-segment `Draw` and SRV bind (and the
  driver threads' share of them); the GS worker's per-segment cost is now mostly the texture resolve, so the gain is
  smaller than before this change. Not built.
* **A cheaper texture resolve for a run of segments**: the coherence flush walks the render targets for every mip level
  of every segment's texture; a check per joined draw of which targets are dirty at all would make it one pass.
* The EE and VU1 worker per-pass work (the canyon is bound on all three threads): untouched.

## 8. Not verified

* Frame times: no usable A/B pair (section 3).
* The Islands, Succubus Lair and Ruined Village were not played with batching on; only Wartok Canyons and Grimstone
  Mines.
* The velocity pass of joined draws is exact by argument (segments in order, depth equal test), not by a hash; DLSS
  pictures compared by eye.
* Material detail, terrain detail, ray effects, modern water: joins are refused while they are on, not run.
* GPU vertex path (`vertex_processing = gpu`): joins are refused for vertex-program batches; not run.
