# Issue 6, items 1 and 4: object draw distance cost and culling at the screen edges (2026-11-15)

Round 8, `issue6-view` agent. The report on GitHub issue #6 (official installer v0.1.17, 144 Hz monitor, Drakan 2):
item 1, "even moderately increasing the render distance for characters and objects causes an unusually large
performance drop (Vulkan and DX11); GPU vertex processing is a bit faster but still feels off"; item 4, "at 16:9 a field
of view above 70 makes the occlusion culling visible at the edges of the screen", tested after the broken bridge near the
start, looking towards Surdana, at default render distance (he plays at 90). Scratch (not tracked):
`build/review/issue6-view/` (play folder `play` with the owner's `drakan.ini`, ELF copy `elf/`, runs `runs/`,
benchmarks `bench/`, pictures `evidence/`, the tester's clip `video/`).

All runs: own exe built from main at 298230c9 plus this change, owner's `drakan.ini` (4K borderless, aspect auto =
16:9, DLSS quality, `lod_distance` 4, `draw_distance` 1, `fov` 99 unless a recipe set it), no mod pack
(`DRAKAN_MODS` = an empty folder), Drakan 2 Level 1A on a fresh card through `play_run.py`. Every pose was reached by
placement (`place`), and the camera was turned by mouse input or by the placement's heading. Nobody walked to these spots.

## Item 4: what culled the edges

**The defect.** The terrain draw `0x152330` first calls `0x1535b8` (a leaf, once per frame). It returns the
axis-aligned rectangle, in cells, around the camera and the four far corners of the view at `max(+0x388, +0x390)`
cells, plus 2 cells. Each layer then clips the cells it draws to that rectangle (`0x15dff0`, clamped to the layer's own
size). The corners come from the renderer's frame width / height / focal (`*(0x5565c8) +0xec / +0xf0 / +0xf4`: 64,
44.8, -76.27), not from the projection scale at `+0x48 / +0x4c`. The widescreen and field-of-view code changes only the
projection scale, and it corrects the CPU frustum `0x199ff0`, which every object and layer test uses
(`docs/reference/widescreen.md` section 3). This one rectangle still described the 4:3 view at 80 degrees. Anything
outside it was never drawn: ground, cliffs and the buildings modelled as terrain layers at the left and right edges.

How much shows depends on the heading. Looking along a world axis, the rectangle reaches 63 + 2 cells to each side, so
at 16:9 and 90 degrees nothing within the view distance is lost (the edge ray needs 100,000 units of depth to leave it).
Looking about 45 degrees off the axes, one edge of the rectangle runs through the camera: at 16:9 and 90 degrees,
anything beyond about 30,000 units at the outer edge of the picture is cut. That is a pop-in at the edges that comes and
goes as the camera turns, which is what the clip shows.

**What the tester's clip shows.** Frames at 71 degrees show a castle wall piece at the left edge appearing and
disappearing with the idle camera sway; at 70 it stays (`evidence/tester_video_left_edge_71deg.png`). Slider 70 at
16:9 gives a horizontal half-angle of 43 degrees, against 40 for the 4:3 corners plus the 2-cell margin.

**Fix** (`drakan_widescreen.cpp`, `hookViewCells`; rule `detail::viewCellFrame` in `drakan_widescreen_policy.h`). For
that one call the frame width and height are replaced by the size the real projection shows at the same focal length,
`|focal / sx|` and `|focal / sy|`, times 1.1 (margin), never smaller than the original, and put back after the call. It
only applies in a world render with an aspect other than 4:3 or a field of view other than 80. A narrower view keeps the
original rectangle. Setting `[video] wide_view_cells` (default 1, env `DRAKAN_WIDE_VIEW_CELLS`, 0 = the original
behaviour). At 16:9 and the owner's 99 degrees the log shows frame 64 x 44.8 replaced by about 98 x 69 at the first
world frames (`DRAKAN_WS_LOG=1`).

**Pictures** (fix off / on in two runs of the same recipe, DLSS off for a clean difference):

* `evidence/fov90_vista_edges_off_on.png`, top row: 16:9, 90 degrees, a raised spot above the town at 475312 180300
  456557, heading 45 degrees. With the fix off, the ground at the lower left edge is missing (sky shows through); with it
  on, the ground is drawn. The other headings of that run (`evidence/fov90_vista_sheet.png`: towards the castle, the
  castle at the left, the opposite diagonals and an axis) differ only in Rynn's idle animation.
* `evidence/fov120_spawn_right_off_on.png`: the Level 1A start meadow at 120 degrees looking north. A whole mountain at
  the right edge is missing with the fix off.
* At 16:9 and 90 degrees, three more spots showed no difference: the start meadow (4 headings), the meadow towards
  the castle, and a slope at 399236 161700 415838 (6 headings). Their screen edges held nothing far enough away.

**Not reached:** the tester's exact place. The game starts at 269484 203204 255696 in the far west of Level 1A, and the
broken bridge he crossed was not found (placements at several suspension-bridge and candidate spots landed in snow or in
the void). The raised spot above shows the same castle with its spire. So it is not proven that the wall piece in his
clip is drawn by this path and not by an object test. The terrain hole at 90 degrees is.

**Cost.** Interleaved A/B, `scene_ab_bench.py` with the turn lock, 3 pairs per scene, the field of view switched live
inside each run:

| Scene | Mode | VU1 Mpairs per world frame, off -> on | Frame ms (median of 3), off -> on |
| --- | --- | --- | --- |
| start meadow | 90 | 4.20 -> 4.25 (+1.3 %) | 6.91 -> 8.02 (pairs: 13.36 -> 8.02, 6.05 -> 8.41, 6.91 -> 5.69) |
| start meadow | 120 | 5.53 -> 5.62 (+1.6 %) | 8.79 -> 11.07 (pairs: 18.32 -> 12.23, 8.29 -> 11.07, 8.79 -> 9.66) |
| raised spot, heading 45 | 90 | 3.28 -> 3.84 (+17 %) | 7.72 -> 8.16 (pairs: 7.63 -> 9.41, 7.73 -> 8.06, 7.72 -> 8.16) |
| raised spot, heading 45 | 120 | 3.23 -> 4.37 (+35 %) | 7.54 -> 8.73 (pairs: 7.66 -> 10.13, 7.48 -> 8.72, 7.54 -> 8.73) |

VU1 work is an exact count. In the meadow benchmark, 3 of 6 runs had other games running (`load_peak` in `runs.json`),
and the frame times spread far wider than the difference between the variants. On the raised spot, 5 of 6 runs had no other game (one had one), and the fix
was slower in 3 of 3 pairs: about 6 % at 90 degrees and 16 % at 120 at this diagonal heading. The VU1 worker is the
thread that grows. The extra work is the ground in the corners of the wider rectangle: a rectangle cannot follow the
wedge of a view seen at an angle to the grid, so part of it lies off screen and the vector unit only rejects it after
transforming it. Along an axis the cost is near zero (meadow). Players who want the original rectangle back can set
`wide_view_cells = 0`. A tighter shape needs a rewrite of `0x1535b8` that caps the corners at the original corner
distance, which saves about a quarter of the added width. That was not done.

**Release.** `drakan_widescreen.cpp` and `drakan_widescreen_policy.h` did not change between the v0.1.17 tag and main
(`git diff v0.1.17 main`), so the release has the same defect. With `wide_view_cells = 0` the exe behaves as the release
does.

## Item 1: where the cost of the object draw distance goes

**What the slider is.** The Display page's "Models" section has "Character detail distance" (`lod_distance`, default
4, the LOD switch of the 57 skinned models) and "Object draw distance" (`draw_distance`, default 1). Only the second one
draws more objects (`docs/reference/graphics.md` section 10). The v0.1.17 code of both is the same as main (one line,
the RAM size constant). The release had no `view_distance`, `shadow_distance` or `model_lights`.

**What the factor adds.** Measured at 399236 161700 415838 facing the castle, 16:9 at 90 degrees.
`DRAKAN_MODEL_DETAIL_LOG=1` now also lists, every 5 s, the objects that the original distance would have hidden but the
factor draws, grouped by model, with their vertex and polygon counts (resident models from the model manager
`0x4dae20`):

| draw_distance | objects tested / hidden | added objects | added vertices / polygons | largest groups |
| --- | --- | --- | --- | --- |
| 1 | 550 / 428 | 0 | 0 | |
| 1.5 | 550 / 355 | about 72 (13 models) | 5,900 / 9,670 | 10 objects of a 304-vertex prop with an authored 100,000-unit distance (3,040 vertices); 16 of a 40-vertex prop at 75,000; groups of small props at 25,000 to 30,000 |
| 2 | 550 / 311 | about 120 (25 models) | 13,450 / 23,420, plus 2 villagers | the same, plus two 1,668-vertex characters at 40,000 and 54,000 units (drawn on LOD 1), two 500-vertex props beyond 116,000 |

Characters past their LOD distance use their simple LOD. With the factor 2 the two added villagers stood on LOD 1, so
LOD selection still works at the extended distance. Static props have no LOD on the disc, so every prop the factor adds
is drawn at its only detail level.

**Where the time goes** (`bench/dd-spot`, DLSS quality vs off as variants, draw distance 1 / 1.5 / 2 switched live,
3 runs each, all 6 runs started on a quiet machine, one of them had another game during it):

| draw_distance | frame ms DLSS off (DLSS on) | VU1 Mpairs | VU1 worker ms | GS worker ms, DLSS off (on) | EE ms |
| --- | --- | --- | --- | --- | --- |
| 1 | 11.00 (11.42) | 5.71 | 10.50 | 8.14 (10.40) | 8.08 |
| 1.5 | 12.50 (12.02) | 6.22 | 11.59 | 9.44 (10.31) | 8.75 |
| 2 | 15.70 (16.01) | 7.67 | 14.16 | 11.29 (14.39) | 9.78 |

So 1.5 costs about 14 % and 2 about 43 % frame time here. The frame time follows the VU1 worker, the thread that runs
the PS2 vector unit's vertex program on the processor. That thread costs about 100 to 150 vector instruction pairs per
added vertex, the same as the rest of the scene. The GS worker (the renderer thread) grows almost as fast, more with
DLSS: its per-draw motion-vector work (`drawObjectMotion`) went from 1.85 to 2.65 ms per frame. The EE (game logic)
grows least.

**GPU vertex processing** (`bench/dd-gpu`, `vertex_processing` cpu vs gpu, DLSS quality, 3 pairs). At draw distance 2
the VU1 worker drops from 15.1 to 8.9 ms, but the GS worker (15.4 to 15.8 ms) then bounds the frame: 16.31 vs 16.44 ms.
That matches "a bit faster but still feels off".

**Answer.** No broken LOD and no runaway object: the factor draws what it says, 72 objects at 1.5 and about 120 at 2 in
this meadow. Each costs vertex work on the CPU, where this port emulates the PS2 vector unit, plus draw work on the
renderer thread. Both threads already run close to the frame time at the original distance, so the extra work shows up
almost one-to-one in frame time. The v0.1.17 release also wrote a DLSS motion pass for every draw (the velocity skip
came after it, `970d213e`), so with DLSS on, each added draw cost the renderer thread more there than it does now. That
was not measured on the release exe.

**Not done (options):** a cheaper path for the added far props (for example without per-vertex lighting where the fog
is already strong) changes the look and needs pictures. Lowering the renderer thread's per-draw cost (motion history and
vertex upload per draw) is the GPU / DLSS agents' area. Neither was built in this round. Nothing in item 1 changed
behaviour; only the log grew.

## Tests and builds as they ran

* `widescreen_policy_test` (runtime-tests preset, target built alone): 4621 checks passed, 7 of them new
  (`viewCellChecks`). The full `ctest --preset runtime-tests` run is listed in the commit report.
* Game builds: incremental `build/final-game` under the build lock, 5 builds (one failed on a string literal in the
  log code, fixed). The tree held other agents' uncommitted editor and runtime files at the time.
* `python scripts/repository_guard.py`: clean. `scripts/run_tests.py`: 562 tests, 11 errors, all in `test_build_pages`
  because `editor/packages/editor-core/src/guides.ts` was missing while the editor folders were briefly deleted in the
  main tree around 07:13 (restored by 07:20; not this agent's doing, reported to the coordinator).

## Not verified

* The tester's exact spot and that his castle-wall pop is this path.
* Vulkan / WebGPU: the fix is renderer independent (guest side), but no run used them.
* OotF levels with `wide_view_cells` (same guest path, not run).
* Item 1 on the v0.1.17 exe itself, and on the tester's CPU.
