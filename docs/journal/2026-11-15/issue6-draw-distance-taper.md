# Issue 6, item 1: draw distance taper for large props (2026-11-15)

Round 8, `issue6-drawdist` agent, following [issue 6 view distance](issue6-view-distance.md) ("Not done: a taper of
the factor for large own distances"). The tester's report: "even moderately increasing the render distance for
characters and objects causes an unusually large performance drop". That note found the cost is real work, not a bug:
at 1.5, about two thirds of the vertices the factor adds belong to large props whose own distance is already 75,000 to
100,000 units. Scratch (not tracked): `build/review/issue6-drawdist/` (play folder `play` with the owner's
`drakan.ini`, ELF copy `elf/`, runs `runs/`, benchmarks `bench/`, pictures `evidence/`).

## What changed

`[video] draw_distance_taper` (Display > Models, "Keep large objects at their distance"; env
`DRAKAN_DRAW_DISTANCE_TAPER`; live; default 1). With it on, the object draw distance factor extends an object at most
to 70,000 units, and an object whose own distance is already 70,000 or more keeps it:
`max(own, min(own x factor, 70000))` (`world::model_detail::taperedDrawDistance`, applied in `enterDrawScope` of
`drakan_model_detail.cpp`, so the distance test `0x18ba28` and the fade of the render call `0x189b38` both see it).
The level's view distance still caps the result inside the guest code, as before. At `draw_distance` 1 nothing changes
(bit for bit). The LOD hooks (`lod_distance`) are not touched. `draw_distance_taper = 0` is the previous behaviour, which
is also what the v0.1.17 release does (the release has no taper). Diagnostic: `DRAKAN_DRAW_DISTANCE_TAPER_CAP` (units)
moves the cap. `DRAKAN_MODEL_DETAIL_LOG` now also prints the added objects by own-distance bucket and the cap in use.

Why 70,000: the own distances on the disc fall in two groups. At the Surdana meadow and the raised spot above the town
the factor added grass, flowers and small props at 8,000 to 30,000, villagers at 25,000 and 35,000, a few props at
45,000 to 50,000, and the costly group at 75,000, 100,000 and 140,000 (castle details, large rocks and buildings). At
factor 2 a 35,000 villager reaches exactly 70,000, so people keep the full factor up to 2; anything at 75,000 or more is
left alone. Above 2 people stop at 70,000 too (the help text says so). At 70,000 the distance fog (Surdana: starts at
53,760, full at 153,600) already covers part of the picture.

## Exact counts (`DRAKAN_MODEL_DETAIL_LOG`, meadow 399236 161700 415838 facing the castle, owner's ini)

The log's "added" list is not zero at factor 1 (3 objects / 12 vertices here, more at level start and while moving):
its "hidden at 1" estimate is approximate. Counts below are steady 5-second windows.

| draw_distance | taper | added objects | added vertices | by own distance (objects/vertices) | characters on LOD 1 |
| --- | --- | --- | --- | --- | --- |
| 1 | either | 3 | 12 | (baseline) | 0 |
| 1.5 | off | about 76 | 5,896 | under 40k 45/1,916; 75k to 100k 16/640; 100k to 150k 15/3,340 | 0 |
| 1.5 | on | 48 | 1,928 | under 40k 45/1,916; the 3/12 baseline | 0 |
| 2 | off | about 121 | 13,448 | under 40k 69/6,320; 50k to 75k 1/220; 75k to 100k 33/2,956; 100k to 150k 18/3,952 | 2 |
| 2 | on | 73 | 6,332 | under 40k 69/6,320; the baseline | 2 |

Every object under 40,000 (grass, flowers, small props, the two villagers) is still added with the taper on; the two
villagers stand on LOD 1 with the taper on and off, so character LOD is unchanged. At the raised spot above the town
(475312 180300 456557) the taper cut the added vertices at factor 2 from 17,404 to 6,408 (heading 45 degrees) and from
20,796 to 12,168 (heading -135 degrees); the remainder there is mostly villagers.

Order of the Flame: at the Ruined Village start the factor adds only objects under 40,000 (5 objects / 1,184 vertices
at 1.5, 9 / 1,488 at 2), and the taper changes nothing there (identical lists). The converted Islands start adds nothing
at either factor (21 objects tested).

## Frame time

`scene_ab_bench.py` with the turn lock, owner's `drakan.ini` (4K, DLSS quality, field of view 99, `lod_distance` 4),
variants taper off / on by env, `draw_distance` 1 / 1.5 / 2 switched live inside each run, 3 interleaved pairs per scene,
god mode. VU1 Mpairs per world frame: the mean `vu1Mpairs/s` of each measured window divided by its world fps.

| Scene | draw_distance | VU1 Mpairs off -> on | frame ms off -> on (median of 3) | pairs |
| --- | --- | --- | --- | --- |
| Surdana meadow | 1 | 5.63 -> 5.63 | 12.37 -> 11.93 | 11.74->11.49, 12.98->11.93, 12.37->12.08 (control) |
| Surdana meadow | 1.5 | 6.20 -> 5.83 | 14.46 -> 12.26 | 12.97->12.19, 14.54->12.26, 14.46->12.51 |
| Surdana meadow | 2 | 7.61 -> 7.25 | 17.44 -> 16.50 | 16.89->16.60, 19.40->16.34, 17.44->16.50 |
| OotF Ruined Village | 1 | 3.03 -> 3.05 | 8.56 -> 8.80 | 8.75->8.88, 8.56->8.80, 8.56->8.74 |
| OotF Ruined Village | 1.5 | 3.13 -> 3.08 | 8.78 -> 8.89 | 9.92->9.12, 8.62->8.89, 8.78->8.76 |
| OotF Ruined Village | 2 | 3.13 -> 3.12 | 9.78 -> 9.92 | 14.18->9.92, 9.75->10.00, 9.78->9.78 |

Meadow: no run had another game going. Ruined Village: one run (off-0, the 14.18 ms window) had another game during
it; the others did not.

Reading it: at factor 1 the work is identical, yet "on" measured 2.3 % faster in 3 of 3 pairs, so differences of a few
percent are noise. At 1.5 the taper removes 65 % of the vector-unit work the factor adds (VU1 +0.57 -> +0.20 Mpairs over
factor 1) and frame time is 13.5 % lower in 3 of 3 pairs (from +17 % over factor 1 to +3 %): a real result. At 2 the
added vertices halve but the VU1 work falls only 18 % of what the factor adds (+1.98 -> +1.62), because what remains is
the two skinned villagers and dense grass; the frame time -5.3 % is not separable from the control. Factor 2 stays
costly with the taper on. Order of the Flame: no change, as the counts predict.

## Pictures (looked at; DLSS off, foliage wind off for the vista run)

* `evidence/meadow_ridge_crop.png`: the meadow ridge at 1, 1.5 off, 1.5 on, 2 off, 2 on. Grass tufts on the ridge
  appear at 1.5 and more at 2, the same with the taper on and off: what players see pop in is still extended.
* `evidence/vista_h0_castle_crop_15.png`: the castle from the raised spot at 1, 1.5 off, 1.5 on, 2 off, 2 on. With the
  taper off, small window props on the castle towers appear (faint at 1.5, clear at 2, deep in the fog); with it on they
  are not drawn, as at 1. This is what the taper drops. That these windows are among the 75,000+ props of the log is an
  inference from the log groups, not a per-object check.
* Most other differences between shots are Rynn's idle animation and the camera sway; many of the props the factor adds
  at these spots are hidden behind terrain (the vector unit still transforms them).

## Played

Drakan 2 Level 1A, own exe, owner's ini with `draw_distance` 2 and `draw_distance_taper` 1 from the start (env),
`play_run.py`, god mode. Placed at the meadow spot, then the scripted W key ran her 45,000 units north with two mouse
turns, through the grass field, past the tent and the castle view, and into the lake at the end
(`evidence/walk_dd2_taper_sheet.png`). Grass, flowers, trees, the tent and the castle were drawn; nothing flickered or
went missing in the eight frames. The input was the harness's keyboard and mouse path, not a person.

## Tests and builds as they ran

* `world_model_detail_test` (new `testTaperedDrawDistance`: off and factor 1 identity, the disc's own distances,
  cap, no limit, NaN, monotonic in distance and factor) and `settings_test` (default, round trip, changed key): pass.
* Game builds: two incremental `build/final-game` builds under the build lock (`-DistDirectory` into the scratch play
  folder), both exit 0; the first with the taper default off (all runs above used it with the taper set by env or live
  setting), the second with the default on, as committed. `scripts/run_tests.py`: 562 tests OK (2 skipped). `repository_guard.py`: clean. clang-format: clean.

## Not verified

* The tester's machine, and the release exe itself (the release has no taper; `draw_distance_taper = 0` is its
  behaviour).
* Other Drakan 2 levels (Ravenshold, Tundra) and other Order of the Flame levels: only the spots above were counted.
* Whether the dropped castle windows matter to players; a cap a little higher would keep 75,000-unit props too, at
  most of the cost.
* Vulkan / WebGPU (guest side change, renderer independent; not run).
