# Issue 5: Volcanic cave-mouth landing, the lava drain and the sanctum bridges

Follows [Volcanic route](../2026-11-06/ootf-volcanic-route.md) ("November 7 follow-up", "Still open" 2) and
[invisible walls](ootf-invisible-walls.md). Runs, recipes and probes are private under `build/harness/issue5-route/`
(`run.py` as before but on the shared play folder `build/harness/issue5-dist/drakan.exe` of main `c8d511ea`; it takes
`GAME_SLOT.lock` only when free and never waits; `recipes/`, `runs/<run>/`; `rpos.py` Rynn's angle, radius and height
around the sanctum pillar, `head.py` her heading, `mat.py` an object's matrix, `pcobj.mts`, `pckeys.mts`, `steps.mts`,
`cover.mts`, `pcext.mts`, `findmodel.mts` PC and pack data probes, `scanref.py` ELF reference scan, Ghidra exports under
`dec/`). Packs: `pack/` from formats `7392049`, `pack2/` from formats `73f41dc` plus the fix below (`f5d3c6b`). Nothing
from either game is quoted here.

## 1. Why Arokh drifted at the cave mouth and did not land

Not the detector, not air currents (there are none near the sanctum), and not landing data. Two things, both in the
route rather than in the converter:

* **The placement was inside a tunnel too low for him.** Cave Entrance detector 2846 (Task 7, as on the disc, where it is
  the "meet Rynn at the cave entrance" logic, not a landing aid) is centred in the sanctum tunnel, and the earlier recipes
  put Arokh at its centre (117781, 54900, 214016). The terrain there has a floor at 53220 and a ceiling layer at 55268:
  2,048 units, a Rynn tunnel. The engine pushed him out, up through the tunnel roof (`vr14` "landed" dump at y 56594, on
  the roof surface) or sideways into the sanctum (`vr15`, x 115422, then 60704). He only landed when a later placement
  happened to leave him on the open apron in front of the tunnel (z 211696, no ceiling below 80,000).
* **The lava was never drained, so he could not come down onto the apron from the air.** The whole basin in front of the
  sanctum is the PC "lava" mover 2257 (model "Lava", 1,485 polygons; its PC 0x0207 record is a single box with no polygons,
  a slab at y 59462 to 60690 over 65,000 x 63,000 units, kept as it is by the converter). With it in place Arokh hovers on
  the slab (the look-ahead trace `DRAKAN_DRAGON_RAY_TRACE` names the lava as the hit object at y 60680; `land2`, `land3`).
  The PC level drains it: Key Lock Invis 2939 (Lock Code 2, beside Key Lock 2229 at 115744, 62711, 193600) slides the lava
  down 9 lu over 11 s and a timer then hides it; the key is the **Lava Rune** 1042 in the Evil Ruin. The earlier route
  skipped this step, which is why the apron could only be reached by placement.

The Lava Rune in game (`rune*`, `lava3`): walking into the ruin detector 1505 opens the sarcophagi (Death Mages) and, through
Timer 1473, the strong box lid and the Mechanism Object 2903, whose object group 2900 lifts the rune 512 units out of the
box. Items are taken with Use (E) only when in reach and in line of sight (`0x31a278`), so E beside the box works once the
rune is lifted (`rune9`); before that, or from inside the box, nothing happens. With the rune carried, E at the lock: the
lava mover goes from y 59912 to 41480 and stays there (`lava3`, `mk3`).

### Flown landing, 5 of 5 (`landD1` to `landD5`)

Card `card-drained` (`mk3`: rune, lock, cage key, scene 9.6a, mount, breath on 2259, F5 while mounted). Each run: F8, Arokh
placed out of the closed sanctum dome into open air 11,000 units south of the cave mouth at (117600, 58000, 200000) facing
north, W held: he flies north until his own look-ahead stops him 3,000 units in front of the rock over the tunnel, then
Ctrl (Cross) held: he descends, lands and, with Cross still held, Rynn dismounts. No timing decides where he stops.

| Run | Stopped at (x, y, z) | Landed at | Landed (`+0x9c`) | Controlled actor after | Rynn after |
| --- | --- | --- | --- | --- | --- |
| `landD1` | 117600, 58000, 211271 | 117600, 53971, 211482 | 1 | `0x10009` (Rynn) | 117591, 53718, 214198 (in the tunnel mouth) |
| `landD2` | same | 117600, 53971, 211501 | 1 | `0x10009` | 117646, 53718, 213634 |
| `landD3` | same | 117600, 53971, 211470 | 1 | `0x10009` | 117575, 53718, 214120 |
| `landD4` | same | 117600, 53971, 211490 | 1 | `0x10009` | 117638, 53718, 212893 |
| `landD5` | same | 117600, 53971, 211480 | 1 | `0x10009` | 117593, 53718, 214209 |

For comparison, a timed approach from the PC "over the sanctum" box 2898 (W for a fixed number of ticks, then Ctrl) landed
in 3 of 5 (`landC1` to `landC5`): the frame rate varied the distance flown by 5,600 units and two runs ended over the
tunnel roof slope, where he hovers without landing. Before the drain, the same descent stops on the lava slab (`land2`,
`land3`).

## 2. The platform bridges had no floor (fixed, formats `f5d3c6b`)

With the platform turned (both switch presses, identity matrix) Rynn walking from the sanctum floor onto the platform
fell into the pit at about 4.4 lu from the centre (`stairA1`, `stairB1`). A placement probe (`floorp`, `floorp2`) on the
four bridge arms (the platform model's floor reaches 4.77 lu at about -85, 5, 95 and -175 degrees) and on its disc:

| Spot (radius lu, angle) | `pack` (formats `7392049`) | `pack2` (with the fix) |
| --- | --- | --- |
| disc 1.6 / 3.0 | stands | stands |
| bridge 4.4 at 6.5, 95, -175 | falls to the pit floor (h -4.0) | stands |
| bridge 4.4 at -85 (in front of the tunnel) | stands (sanctum floor there) | stands |
| ring 5.5 | stands | stands |

Cause: the converter kept the PC box hierarchy of a rigid model with the PC polygon lists, but splits every quad into two
triangles and drops polygons of unmapped textures; the lists then name the wrong triangles (the platform has 568 PC
polygons, 624 after the split, and its root still listed 568). This is the "real mismatch" the November 7 follow-up of the
Volcanic route left open; it did not show there because only the stair and the pillar top were probed. Fix:
`remapObbPolygons` (`ootfmodel.ts`) rewrites the lists through the PC polygon to new triangles map; shapes and boxes are
kept. Test: "points the PC bounds lists at the triangles a split quad or a dropped polygon left" (`ootfmodel.test.ts`). In
the regenerated pack only `RESOURES.MOD` changes from it (the TXD and `mod.toml` differences against `pack` are the other
formats commits in between, `cd48742` and `73f41dc`).

## 3. The stair on foot

Not achieved. The bridges are walkable since the fix above, and Rynn does climb the stair under ordinary input, but the
scripted input has no feedback and none of 24 tries (`stairA1` to `stairN2`; one, `stairF13`, ended in a renderer error) reached the pillar top. Setup: the sanctum
card `card-sanctum` (`mkS`: flown landing, both switch presses, button, arrow, melt; F5), Rynn placed once on the sanctum
floor ring at the east bridge facing the pillar (127928, 53700, 228603), W along the bridge until the pillar base stops
her, then a turn along the stair and W with a steady left mouse turn (the stair rises clockwise seen from above, 0.05 lu
per 10 degrees, 1.2 lu per turn, treads between 1.0 and 1.4 lu from the axis; geometry from `steps.mts`).

* Best try `stairJ13` (pack2, 13 counts per tick): 1.1 turns up the spiral, from the foot to 1.32 lu of the 1.45 lu top
  (y 56405 at -41 degrees), then she drifted past the outer edge at 1.56 lu and fell to the platform floor.
* On the inner edge (about 1.05 lu) she sticks and runs on the spot at fixed places (-27, -57, -91, -117 degrees; the
  same spot (117939, 54230, 225499) in two runs), while the scripted turn goes on and turns her off the stair.
* Off the inner edge the radius is set by speed over turn rate; her speed on the stair varied by a factor of up to three
  between runs with identical input (`stairJ13` 380 degrees in 225 ticks, `stairM1` 128), so a fixed turn rate either
  presses her into the inner edge or lets her drift off the outer edge.
* A second converter change from the earlier follow-up (`obb-remap-descend.diff`: hand polygons the PC lists only on inner
  shapes down to leaf boxes) was tried again (`pack3`) and dropped: it makes an upper stair turn solid where the placement
  probe expected it, but the pillar base stops blocking and Rynn walks through the column (`stairN1`, `stairN2`).

So the walk needs a closed-loop steering aid (position feedback in the input script) or a person at the controls; the
route below still places her on the pillar top.

## 4. Full route on one pack

`full2` (pack2, a direct start of Volcanic, god mode, one run; impact trace `DRAKAN_TRACE_FN=0x1fe3c8,0x39ef90`), then
`rift1` from `full2`'s own quicksave on the same pack. Each step is placed beside its trigger as before, then a real walk,
press, breath or flight; evidence is RAM dumps (`objs.py`, `dragon_report.py`, `mat.py`) and the trace.

| Step | Result |
| --- | --- |
| Lava Rune: walk into ruin detector 1505, wait for the lift, E beside the box | pass: the rune's handle leaves the world (cell -1) |
| Key Lock 2229 / 2939, E | pass: lava mover 2257 at y 41480 (from 59912) |
| Rune Cage Key, W, E | pass: cell -1 |
| Detector 1994 with the key, scene 9.6a | pass: Arokh free, `+0x94` 1, `+0x9c` 1 |
| Walk into Arokh | pass: controlled actor `0x1001c` |
| Breath on 2259 (placed in the sanctum, as before) | pass: 2259 leaves the registry, its flame particles burn |
| Flown landing at the cave mouth (as `landD`) | pass: stopped at z 210562, landed at (117600, 53945, 210702), `+0x9c` 1, Rynn on foot `0x10009` |
| Succ Switch twice | pass: platform matrix identity (180 degrees) |
| Wall button, "Arrow No Flame" | pass: plays once, the launcher fires |
| Arrow on 2366 | pass: the impact (`0x39ef90` from `0x1fe9c0`) names 2366's handle (resource `0x31f25`); 2366 is gone in the next dump |
| Ice melts | pass: the ice Morph Plug appears, morphs and is gone by `c400` |
| Runeblade | pass with a placement on the pillar top (116736, 57000, 227728): she stands at 56670, E, the blade's handle leaves the world (section 3: not walked) |
| Fresh on-foot sanctum quicksave, F5 | pass (on the pillar top, blade carried) |
| That quicksave loads (`remount1`, `rift1`: direct start, F8) | pass: Rynn back at (116736, 56670, 227328) to the unit, blade carried, W moves her |
| Remount | pass when she walks into Arokh from his side (placed at 119300, 54200, 210700 facing west): `0x1001c`. Walking at him from behind (from the south) stops 2,600 units short and does not mount (`full1`, `full2`, `remount1`) |
| Mounted in the rift box 2318 | pass: message box (screen 2), save screen (5), loading (14), Rift World gameplay |
| Rift World | pass: arrives mounted at (342411, 99191, 319532), Cross held lands Rynn on foot, W walks her 1,400 units, clean scripted exit |

`full1` (same pack, a stair attempt first and then the pillar placement from the platform floor) ended with Rynn under
the pillar top after the placement, so no blade and no exit; placed from the switch side (`full2`) or from a load
(`pil-pack2`) she stands on it. Not followed further.

## 5. Arrow Detect Object 2366

2366 is not drawn and is still hit (formats `7553750` and this pack): in `full2` and `mkS` the impact path of the arrow
names 2366's handle and 2366 leaves the registry in the next dump (above). The look from the platform towards it after the
switch (`view2366` in `mkS` and `full2`, placed at 120000, 53800, 226000 facing it) shows the sanctum, the ice-covered
pillar and no black box. A screenshot only shows absence in that view; the converted model keeps its polygons, so the hit
is what proves it is still there.

## Still open

1. The stair walk (section 3): needs closed-loop steering in the input script, or a person. Rynn also sticks on the
   inner tread edge at fixed spots; whether that is a collision quirk of the converted platform was not settled.
2. Remounting from behind Arokh does not work at the apron; from his side it does. Not compared with Drakan 2.
3. The PC lava's collision is one polygon-less box (a slab below its visible surface), kept as it is; with the lava up,
   Arokh cannot come down into the basin, which matches the PC design (drain it first), but the PC behaviour itself
   was not checked.
