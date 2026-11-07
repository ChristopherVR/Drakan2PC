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
ticks of W). Not yet known whether the Morph Plug moves its collision, or the tunnel is too tight for Arokh's look-ahead
(the Islands arrival tunnel had the same symptom, see [Arokh flight](../2026-11-06/ootf-arokh-flight.md)), or the plug's
collision hierarchy still wrongly blocks (formats `f5d3c6b`, the collision-list change, was in this pack).

Not reached: the altar, Delon, the four Dr. Evil heads and the ending by natural play (the isolated head fight to credits
of 2026-11-02 still stands).

## Comment-ready summary for issue #5 (not posted)

Pass 3, Rift World (the last level; nothing follows it). A fresh 23-level pack from the current formats head loads and plays.
Arriving from Volcanic carries Arokh's fire breath (direct starts of Rift World have none); the breath does 15 damage per
hit and refills in about 10 s. Real breath kills of the four Ebon Dragons decrement the gate counter 4 -> 0 (with a test aid
that lowers their hit points; at 150 hit points the same open-loop script cannot land enough hits), and the counter's
Detector sets the RibDoor flags when Arokh reaches it. Blocked: Arokh does not get through the hexagonal door tunnel (stops
2,000 to 3,300 units before the door), so the altar, Delon and Dr. Evil are not reached naturally. Next: why the door does
not open for him (plug collision vs flight look-ahead).
