# Issue 5: Arokh did not fly under W in converted levels (fixed)

Follows [mounting and Volcanic](ootf-mounting-volcanic.md) ("Still open" 4) and [Islands bell](ootf-islands-bell.md). Runs,
recipes and helpers are private under `build/harness/ootf-flight/` (`run.py` as before plus `EXE=`, `fl.py` / `yaw.py`
flight words, heading and look-ahead factor from a RAM dump, `sum.sh <run>`, `col.mts` terrain columns, `near.mts` objects
in a box, `obbparse.mts` / `pcmb3.mts` / `leafs.mts` collision hierarchies, `movobj.mts` the object-lift experiment, Ghidra
exports under `dec/`). Packs: `mods/` is the full 23-level conversion from the working tree with the converter fix.

## Not a placement artifact

Same executable, same recipe (wait, W 3 s, Space 2 s, Ctrl 2 s), dragon state from RAM (`+0x08` forward speed, `+0x5c`
velocity, `+0x904` look-ahead factor):

| Run | Start | W 3 s | Climb / dive |
| --- | --- | --- | --- |
| `raven-nopos` | Drakan 2, Level 4A direct start | 36,700 units | +24,200 / -23,700 |
| `raven-pos` | same, placed 8,000 units up | 12,000 units, then a real obstacle | +13,100 / -23,700 |
| `isl-open` | Islands, placed in open air over the island | 28,750 units | climbs to the level's Flight Ceiling 85,000 |
| `isl-nopos` | Islands direct start, not placed (the PC start in the arrival tunnel) | 0 units, forward speed 12,282, velocity 0 | +2,300, then stuck |
| `g05-06v` (earlier) | Grotto to Islands by the walked exit | about 2,000 units in 2 s, S nothing | |
| `isl-ctl` (earlier) | Islands, placed at (331681, 24000, 812000) | 457 units, then pulled back | |

So a placed dragon flies, and an unplaced one in the arrival tunnel does not. The forward speed reaches its maximum in
every run; only the velocity is zero. `isl-ctl` is a different case: that point lies outside every terrain layer (the
tunnel ends at z 815,104) and the engine keeps putting him back; that one is a harness artifact (place over terrain).

## Why: the flight look-ahead

The flight tick (`0x271e18`, flying branch; landed goes to `0x274c18`) builds the velocity from the forward speed and the
heading, then calls `0x274250`: a ray 6,144 units (3 cells) along the velocity through the world (`0x372d38`, terrain cells
and objects, with Dragon Barriers `0x10076` included). On a hit the velocity is scaled by
`clamp((distance - 3072) / 3072, 0, 1)` (`+0x904`). A terrain hit counts only when its triangle faces him; an object hit
always counts, and an object whose box holds the start of the ray is hit at the start (distance 0, when its polygons miss)
or at the far face of its box (double-sided polygons). Then the factor is 0 in every direction and he never gets out.

A trace of that ray (`DRAKAN_DRAGON_RAY_TRACE=1`, new, `drakan_dragon_flight.cpp`) found two such objects in the Islands
arrival tunnel (lifting other suspects out of the way in a copy of the pack, `isl-mA` / `isl-mB`, changed nothing):

1. **The PC tunnel shell "Warp Gate" (model `Grotto2IslandsGate`)**, at (335872, 20000, 829440). Its PC collision is a
   61-shape box hierarchy (51 leaf boxes around the walls; the tunnel inside is empty). The converted model had one box
   around the whole model, so the whole tunnel was solid for the look-ahead: `isl-trace`, 289 of 290 rays hit it at their start. Cause: the November 5 draw-list upgrade of rigid generic props (`rigidDisplayList`, formats) rebuilds the
   polygons and replaced every source box hierarchy with one synthesized box, because the polygon lists of the shapes
   no longer matched. 326 converted props lost their hierarchy that way (arches, bridges, gates, the bell tower, ruins).
2. **A PC "Bounding Box" slab** (invisible blocker, 14,336 x 3,072 x 8,192 units on the tunnel floor at (330752, 21024,
   822144)). The walked exit from Grotto keeps Rynn's offset from the exit ("Maintain Player Position") and Arokh arrives
   1,000 to 6,000 units above the floor depending on where she entered the Grotto box: at 21,000 he is inside the slab
   (`g2i-trace`: 3,945 of 3,961 rays in the Islands hit the slab from inside).

The 0x0207 layout (verified on every OBB block of the disc and of the PC install): `u16 count, u16 1` at +72, `count` pairs
`(first child, next sibling)`, then per shape the box (minimum corner, three edges scaled by the size) and `u16 n, u16
polygon[n]`. The PC lists a subtree's polygons on its inner shapes too; the disc lists them on leaves only. The engine
keeps an object's world box at object + 0xb8 (corner, edges), then its sphere.

## Fixes

* **Converter (formats):** `rebuildObbHierarchy` keeps the source hierarchy and boxes when the polygons are rebuilt and
  fills the leaf lists again from the new polygons (each polygon goes to the leaf box holding its centre, else the
  nearest one; inner shapes that listed polygons list their leaves' again). `rigidDisplayList` uses it before falling
  back to one box. Tests: `OBB collision hierarchy kept for rebuilt polygons` (`test/ootfmodel.test.ts`). In the
  regenerated pack the Warp Gate has its 61 shapes back and 326 models change only in their 0x0207 record.
* **Runtime:** `drakan_dragon_flight.cpp` wraps `0x372d38` at the look-ahead's call only: a hit on an object whose box
  holds the start of the ray is dropped (`drakan_dragon_flight_policy.h`), so he can fly out of an object he arrived or
  was placed in; anything in front of him still counts once he is out. Test: `dragon_flight_policy`
  (`ps2recomp/ps2xRuntime/tests/dragon_flight_policy_tests.cpp`). Drakan 2 flight is unchanged (`raven-check`, `raven-low`).

No input mapping change was needed: W, Space, Ctrl, A / D and the mouse go through the game's own stick path in flight.

## Verification (`mods/`; executable per run, see the note below the table)

| Run | Executable / pack | What | Result |
| --- | --- | --- | --- |
| `isl-fix` | dist (no runtime change) / fixed pack | Islands direct start, not placed: W 3 s, Space, Ctrl, Cross held | flies out of the tunnel: 38,400 units along it in 3 s; climbs; Ctrl held lands him (`+0x9c` 1) at (389638, 62313, 778893); Cross held 3 s: control goes to Rynn (`0x10009`) |
| `isl-fix2` | trace build / fixed pack | same | the same flight (38,700 units in 3 s, +13,400 climb), landing and dismount |
| `g2i-fix` | dist / fixed pack | Grotto to Islands by the walked exit, W | arrived at height 21,004, inside the slab: still 0 (look-ahead hit inside the slab) |
| `g2i-climb` | trace build / fixed pack | Grotto to Islands, Space 2 s, A 1 s, W 4 s, Ctrl, Cross | arrived at 25,836; climbs 2,200, turns to the tunnel exit, flies 44,400 units out of the tunnel in 4 s; lands; Cross: control to Rynn |
| `g2i-fix2` | trace build / fixed pack | Grotto to Islands, W, D, W, A, W | arrived at 22,165 facing the slab 24 units ahead (blocked, as the engine should); A / D turn him (90 and about 165 degrees) |
| `isl-placed` / `isl-slab` | trace build / fixed pack | placed inside the slab (332800, 21017, 822000) | the look-ahead no longer stops him (`+0x904` 1.0), but while his body overlaps the slab the dragon stays in state `+0x94` bit `0x10` with no thrust and drifts out at about 500 units/s; Space climbs only 700 in 2 s |
| `wartok-placed` | trace build / fixed pack | Wartok, landed Arokh placed in the mine tunnel (earlier "does not move") | walks 7,000 units in 3 s |
| `raven-check`, `raven-low` | trace build, no mods | Drakan 2 Level 4A: W, climb, dive; W at the hover floor | unchanged: 36,700 units in 3 s; at the hover floor W 32,000 units in 3 s |
| `isl-dis` | trace build / fixed pack | land, Cross held, then wait 4 s, S, A | dismount works; at that landing spot Rynn ends up standing on Arokh and S / A move her about 50 units |

The trace build ran from `build/final-game/bin` with the private ini of each run copied from `dist/DrakanPC`: while this
pass ran, another session's perf harness kept `dist/DrakanPC/drakan.exe` running without the game slot, so the play
folder could not be refilled (the first refill failed on a locked DLL and left the previous executable in place). The
converter half was checked from `dist` as well (`isl-fix`, `g2i-fix`).

## Still open

* **Arriving inside the slab.** Where Rynn enters the Grotto exit box decides Arokh's arrival height (kept offset): 21,000
  to 25,800 in these runs. Inside the PC "Bounding Box" slab (below 22,560) he now gets out, but slowly (no thrust while
  his body overlaps it, `isl-slab`). Not traced: what sets `+0x94` bit `0x10` (`0x2826d0` runs while it is set). A fix
  would either lift a mounted arrival clear of an object it lands in, or make the slab not block a dragon.
* **Walking after the dismount at the landing spot** (389638, 62313, 778893): Rynn stands on Arokh's back and does not walk
  off (`isl-dis`, Rynn `+0x94` 0x10); on flat ground (Wartok) she does. Landing elsewhere was not tried.
* **Hover dismount** is not a harness problem: the dismount needs him landed (`+0x9c` 1, mounting-volcanic notes), and in
  the arrival tunnel Ctrl only lowers him to his hover floor without landing (`isl-low`). Land first (Ctrl held over open
  ground), then hold Cross.
* The flown route to the bell tower was not tried; the bell route stays placed (`bell-h`).
* The other 325 props with restored hierarchies were not walked; their collision is now the authored PC one instead of a
  single box.
