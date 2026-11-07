# Issue 5: Rift World, the last level (fresh pack, fire, Ebon Dragons, gate counter, RibDoor tunnel)

Follows [giant, Islands, rift](ootf-giant-islands-rift.md) and [quick-load LRU](ootf-quickload-lru.md). Runs, recipes and
helpers are private under `build/harness/ootf7/` (`run.py` = the ootf-i5 driver on `build/ootf7/play/drakan.exe`, a copy of the
play folder with `build/final-game/bin/drakan.exe`; `go.sh` wrapper, `pcdump.mts` PC object dump with links, `moveobj.mts`
test aid that moves one object in a pack level, `findcls.py` / `dw.py` / `st.py` objects of a class with hit points or
instance words per RAM dump, `montage.py`, `recipes/`, `cards/` saves). Direct starts, god mode.

## Where the chain ends

The campaign has 13 levels (00 Intro to 12 Rift World); the other 10 of the 23 are arenas. **Rift World is the last level**:
there is nothing to walk past it, the ending is inside it. PC logic of the level (`pc-rw.txt`, links only, no incoming
links from outside):

* four Ebon Dragons (527 to 530) each link the counter 558; 558 links the Detectors 367 / 369, which link the RibDoors 191 /
  192 (the hexagonal tunnel gates, a Morph Plug each); timers 447 to 450 drive the vertical gates 439 to 446;
* Delon 161 links the counter 455 (-> Stomp Player 436) and a Death Mage waypoint; four Dr. Evil heads 108 to 111 link the
  Navros DeathCounter 162 -> Ribs 472 -> Stomp Player 473 (the end sequence); Ribs 474 links the heads (an auto-start timer);
* Trigger 183 (mounted) plays 10.1a (proven earlier).

## 1. Fresh pack

`ootf-convert.ts --ootf build/ootf/cab_disc --ps2 build/odbx/data --template LEVEL1NA --campaign 1`, 23 levels, 10,698 files,
2 min 40 s, from formats `f5d3c6b` (the other session's collision-list fix, plus the test aid below only when its env var is set).
Executable: `build/final-game/bin/drakan.exe` (main `7e017f08`).

## 2. A direct start of Rift World has no fire

Direct starts of Rift World (`rw0` to `rw4`) arrive on Arokh with an empty breath orb and breath taps do nothing. The PC
Dragon 240 links a "Fireball Power Up" (563, parked at y 143,863) and the converter drops that class's weapon fields; in
the natural chain the dragon is carried from Volcanic, whose orb is amber. So the real arrival was replayed: Volcanic key,
cage, Runeblade, mount, rift box 2318, exit, load: **Rift World with the amber orb** (`mkrw3`, `mkrw4`).

The Runeblade step is the flaky part of that replay (the cave-mouth dismount: `mkrw`, the Volcanic recipe `vr14` copy, never
landed Rynn in 900 s and the run stopped at the 900 s cap). For these runs a test aid pack moves the Runeblade handle 2239
next to Arokh (`moveobj.mts`: 265000 152300 497200), so Rynn takes it before mounting; the rest is the real route (message
box, save screen, load). The save of that arrival is the card the later runs start from (F8).

## 3. Breath, mana, the Ebon Dragons

* Arokh's breath does 15 damage per hit (150 -> 135); the orb drains in about 10 taps and **refills in about 500 ticks**
  (`mana1`: drained, then full by the first wait shot).
* The four Ebon Dragons idle (positions nearly fixed, no attack on the dormant start) and the breath is a narrow stream: only
  placements about 3,250 units from a dragon, 500 to 1,300 units above it and within about 100 units sideways hit; open-loop
  scripts need many tries (`fire3` to `kill9`, a dozen runs). Not a converter fault; a fight needs a closed-loop driver.
* Test aid `OOTF_TEST_EBONHP=<hp>` (formats `4d21976`, like `OOTF_TEST_HEADHP`): Ebon Dragons with few hit points. With 15
  the four dragons died to real breath hits over staged runs with F5 between stages (`stage1` to `stage4`).
* **The gate counter counts the real deaths**: counter 558 reads 4 at the start and 2, 1, 0 after the kills (`kill9` 2,
  `stage1` 2, `stage2` 1, `stage3` 0 from `dw.py 10007`).

## 4. The RibDoor tunnel (next blocker)

With the counter at 0, Arokh placed at the tunnel mouth flies into the hexagonal tunnel (floor 92,500, ceiling 94,772: 2,272
units tall; at z 281,000 the ceiling is 96,211) only at height 93,700 (at 96,000 the funnel ceiling stops him at z 281,949).
He then stops 2,000 to 3,300 units before the door (x 334,000 to 336,300, z 274,200 to 282,500) facing a rock slab, with the
ornate gate frame to his right. Detector 367 works: when he nears it the RibDoor 191 changes its flags `0x19001045` ->
`0x19009045` (`door7`, `stage4`), but the passage never opens for him in the runs made (`door5` to `door8`, up to 1,600
ticks of W). Superseded by section 5 (the door is not the route). Not known whether the Morph Plug moves its collision, or the tunnel is too tight for Arokh's look-ahead
(the Islands arrival tunnel had the same symptom, see [Arokh flight](../2026-11-06/ootf-arokh-flight.md)), or the plug's
collision hierarchy still wrongly blocks (formats `f5d3c6b`, the collision-list change, was in this pack).

Not reached: the altar, Delon, the four Dr. Evil heads and the ending by natural play (the isolated head fight to credits
of 2026-11-02 still stands).

## 5. Follow-up: the RibDoors are not the route; the tunnel is a flight-clearance problem

The three causes were separated with the dragon look-ahead trace (`DRAKAN_DRAGON_RAY_TRACE=1`, runs `ray1`, `ray2`, `c1`, `w1` to
`w3`) and terrain probes (`col.mts` on the pack, `pccol.mts` / `pcmap2.mts` on the PC level, `pcmap2` prints floor to ceiling gaps):

* **Not the door plug's collision.** Rays in the tunnel hit terrain (`object 0`), never an object; with both RibDoors moved out
  of the level (`packnodoor`, test aid) Arokh stops at the same place (z 279,400, x 335,000) as with them.
* **Not the converter.** The converted terrain matches the PC level at every probed column (`pccol.mts` against `col.mts`:
  identical floors and ceilings, same textures). The tunnel's far end is sealed by the terrain itself in the PC data too
  (floor equals ceiling at x 334,000 to 337,000, z below 272,000): the RibDoors guard side vaults, not the way north.
* **The route** (PC terrain map): from the arrival the central wall (floor equals ceiling, z 279,000 to 283,000 at x 342,418) is
  solid at every height; the way is down the left (or right) tunnel at z 283,000 to 279,000 (x 332,000 to 336,000, a ramp
  rising from floor 81,000 to 92,500), east along the corridor at z 277,000 to 278,000 (floor 92,500, ceiling 94,600 to 95,300:
  about 2,200 units), then north up the open central shaft (x 339,000 to 345,000, ceiling 108,600). Flown from the shaft
  (`s1`: placed at 342,418 98,000 268,000, W) Arokh reaches z 235,000 in 12 s and stops at the first "big spine" prop
  (object 478 at 341,982 99,452 232,363, ray hits the object), which has to be flown around (x 349,000 clears it).
* **What stops him in the corridor**: Arokh lands. Held at a height under 1,400 above the floor he drops to the engine's
  landed height (floor + 741: `93241` over floor 92,500, state word `+0x9c` = 1, forward speed 0, the look-ahead ray points straight
  down), and a landed Arokh does not walk on under W; held above it the ceiling's wedge (ceiling 93,900 at z 278,600, x 335,300)
  ends the look-ahead at 2,300 units. The window between "lands" and "hits the ceiling" is 500 to 700 units wide, which is
  not a window a player could fly through; whether the PC dragon needs less clearance was the open question; section 6 answers it: no, my height
  holds were the cause.

### New test aids (runtime, `drakan_playtest.cpp`, documented in `docs/reference/env-flags.md`, commits `5c270c0a`, `562d9785`)

* `DRAKAN_PLAYTEST_AUTOAIM=<class hex>`: the controlled actor faces the nearest living object of that engine class every
  boundary. Rift World (`aim1`): three of the four Ebon Dragons died in 12 rounds, against a dozen runs of open-loop placement.
  Unit test `autoaim_policy`.
* `DRAKAN_PLAYTEST_ALT=<y>` (height hold) and `DRAKAN_PLAYTEST_WAYPOINTS="x y z; ..."` with `_CLIMB` / `_WP_RADIUS` (autopilot:
  yaw toward the current point, height moves toward its height, the recipe only holds W). `w3` flew the natural start, the left
  tunnel ramp and into the corridor (waypoints 0 to 2 reached) before the landing above.

### War giant

Not re-run: `DRAKAN_PLAYTEST_AUTOAIM=1003b` is now available for it (the earlier note's "reactive driver"); the Alwarren session
needs the hammer route first and was not replayed in this pass.

## 6. The route flown naturally, the altar, the four heads, the ending and the outro

**The landing question is answered: nothing in the converter or the engine differs.** The 2,200-unit corridor was an artifact
of how the map was read: east of the left tunnel (x 331,000 to 335,000, z 283,000 to 279,000, a ramp whose ceiling is 94,000 to
101,000) the route opens into a wide hall (z 277,000 to 274,000: floor 92,500, ceiling 100,700), and the 2,200-unit gap at
z 278,000 is only a sill. Held at 93,800 to 94,100 inside the ramp's tail Arokh lands (the flight tick `0x271e18` keeps the
"landed" state `+0x9c` = 1 when he is within about 1,400 of the floor and the ceiling wedge cuts the look-ahead; the dive
cushion, `0x271e18` lines near `fVar27 < 16384`, scales the descent by `(height - 1024) / 15360`); flown at 95,000 to
98,500 through the hall he does not. Waypoints that keep to the ceiling (95,000 at z 279,500, 95,500 at z 275,800, then 98,500)
fly the left tunnel, the hall and the central shaft with no converter or runtime change (`w6`, `w8`, `a2`, `a5`: all waypoints
reached; the climb must stay behind the ceiling, so waypoint heights matter, and a run in which Arokh climbed too early hit the
wedge at z 277,600 as in `w5`). The shaft opens onto the causeway (floor 98,500 to 100,000, ceiling 108,600 to 120,000 at
x 342,418, a pit on both sides) with the spine props on it; flying over them at 107,800 to 108,800 clears them, and the
altar is at z 183,000 to 190,000 (floor 100,000 at x 342,000, z 186,000).

**Altar and heads (`mkaltar`, `hf2` to `hf4`)**: from the natural arrival save (fire breath) a `place` in the hall, W held and
the waypoint autopilot reach the altar (waypoint 8 of 9, `WP_HOLD` keeps Arokh over it), F5 there. The four Dr. Evil heads
(Primitive Giant clones, 250 hit points each, not lowered) stand around the altar; with `DRAKAN_PLAYTEST_AUTOAIM=1003b` and
`_ALT=101700` and breath taps (22 per round, places at three spots 5,000 to 8,000 units from the heads) **all four died to real
breath hits at their real hit points in about 2 minutes of game time** (head 23: 250 -> 190 -> 40 -> -1 within 600 ticks; the
dead heads fall into the pit, y 47,066). The Navros DeathCounter (162) reads 0 and the ending starts.

**Ending and outro (`end1b`, `end2`, `end3`)**: quick load of the save made during the sequence: the mirror room scene (Delon
falls into the mirror; the German "Vorgang war erfolgreich" quick-save text shows over it: F5 is accepted during the scene),
then the outro on the Wartok farm landscape (`Out.lvl`, "Surreal Software" and the Drakan 2 credits in German, scrolling at the
engine's rate; the stock credits, not an Order of the Flame list), and **at t = 272 s the game returns to the title** (menu
rows: Load, Continue, Quit; "newest save slot 1 (Rift World)"). So the converted campaign reaches its end and its credits
from the last level.

**War giant (`ag-4`, `ag-5`, Alwarren from the `sessA-3` save, German tree, sword equipped with `1`)**: the old open-loop
fights landed 1 to 6 hits and the giant killed Rynn. With `DRAKAN_PLAYTEST_AUTOAIM=1003b` (range 30,000) and W held the facing
error is 0 to 9 degrees all fight, **the giant (150 hit points) dies by 1,800 ticks** (`ag-5`: 150 -> -16 -> -232, i.e. dead
in the first 15 s) before he grabs and throws her to the same corner as before (`ag-4` with a 9,000 range: 150 -> 9, then she
lay 12,600 away and the range left her unaimed). The converter side of the hammer reveal after the kill was not re-checked.

### Not covered

Delon himself (class 0x1003b like the heads, 650 hit points, found in a direct start at 342,518 103,683 186,366 next to Arokh)
was not in the object list after the quick load and was not fought; the Ebon Dragons were fought at 15 hit points only (150 not
retried with AUTOAIM). The corridor ramp's waypoints reach the hall most but not all runs (the autopilot's timing).

## Comment-ready summary for issue #5 (not posted)

Pass 3. Rift World is the last level, and the chain now reaches the end of the game: a natural arrival from Volcanic (fire
breath carried), the left tunnel and the hall, the central shaft over the spine props, the altar, the four Dr. Evil heads
killed with real breath hits at their real 250 hit points, the mirror-room ending scene, the outro credits (about 4.5 minutes,
the stock Drakan 2 list in German) and the return to the title. The "RibDoor" tunnel was not the way (the doors guard side
vaults and the PC terrain seals them too), and Arokh's stall in the tunnel came from flying too low (the engine lands him within
about 1,400 units of the floor), not from the converter. Alwarren's war giant now dies to the sword at 150 hit points (about 10
hits) with a test aid that keeps Rynn facing him. New test-only aids (env-flags.md): `DRAKAN_PLAYTEST_AUTOAIM`, `_ALT`,
`_WAYPOINTS`, `_WP_HOLD`, and `OOTF_TEST_EBONHP` in the converter. Still open: Delon's own fight, Ebon Dragons at 150 hit points
with the aim aid, the Order of the Flame credits list, and an unaided human run of the Rift World flight.
