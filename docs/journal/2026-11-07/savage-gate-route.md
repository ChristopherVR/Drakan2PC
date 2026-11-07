# 4B Savage Gate: how exit 5595 is reached (2026-11-07)

Own exe copy `build/gaps5/play` (the 16:27 build, `ootf-levels` mod switched off), recipes `build/gaps5/r_a.txt` and
`r_e.txt` (not committed), runs `build/harness/g5_a` and `g5_e`. Positions come from the decoded LEVEL4RA object list and the
`[in-pos]` log. Follows `savage-gate-shoot-dispatch.md` and `savage-gate-breath.md`.

## The gate is not on the way to the exit

Every earlier test started west of the gate (x 640000) and flew east, so the exit looked "behind the gate". The level data
puts the exit on the other side:

| Thing | Position (x, y, z) | Note |
| --- | --- | --- |
| Level start (Rynn 359) | 839436 145504 985491 | south-east end of the level |
| Arrival from Ravenshold, Level Transition 6451 | 821211 144690 970763 | links Rynn 7771 / Arokh 7772 (829884 / 827276, z about 970800) |
| Exit detector 5595 (Trigger Message 1, scale 9 6 1) | 681024 142708 1765312 | links the Level Transition 5596 |
| Level Transition 5596 | 680960 141204 1760256 | links Rynn 7773 / Arokh 7774 (681003 / 680917, z 1753089 / 1756160) |
| Savage Gate wheel 1959, wall 7527 | 650275 144967 1744595 / 650799 144291 1744614 | the west end, about 30000 units from the exit |

Rynn 7773 and Arokh 7774 linked to 5596 are the arrival markers for coming back from the next level (Shiverbane has the
same pair next to its own transition, 6832 / 6833), so 5595 / 5596 is an ordinary level end with a return spawn. The exit is at
x 681024, east of the gate (x 650275); a player coming from the start (x 839k) never has to cross x 650000.

## Run g5_a: from the hall to the exit (`place 690000 146000 1744655`, W 5 s, four headings)

| Heading | Result |
| --- | --- |
| yaw 0 (towards -z) | stops at z 1737341 |
| yaw 1.5708 (towards -x) | stops at x 674767 |
| yaw 3.14 (towards +z) | reaches z 1763822 at speed 12197 and the game raises gui screen 2 ("game must be saved", the level exit box, `world-model.md`), frame 16726 |
| yaw -1.5708 (towards +x) | game is in the exit box, not interpretable |

So the exit fires from the hall by flying north, with the gate not involved. Not run: the 760000 unit route from the start to
the hall. The scripted mount (E taps near Arokh) is flaky in the harness (two of four runs stayed on foot and `place` then gave
up), so the connection of the start area to the hall rests on the data (start and arrival markers and the hall are one
terrain) and was not flown end to end.

## Run g5_e: what stops Arokh at the gate (`place 640000 <y> 1744655`, W 6 s)

| Height y | Stop x | What it is |
| --- | --- | --- |
| 144800 | 646690 | wall 7527 (x 650799) less Arokh's radius of about 4100; below the barriers' y range |
| 153500 | 642850 | Dragon Barrier 7827 (near face x 645920) less about 3070 |
| 158000 | 642850 | the same barrier |
| 165000 | 656222 | above barrier 7827 (top y 160213) and above the wall (top y 151458): Arokh flies over the gate (x 650275) and is stopped 6000 behind it by something else |

So the earlier stop at 646780 is the wall 7527, not the barrier; the barrier is a second, higher blocker that closes the
corridor from y 151100 to 160213. Above that the corridor is open for about 6000 units.

## Dragon Barriers 7825 / 7826 / 7827 (class 0x10076)

* ODB class "Dragon Barrier" (LEVEL4ON 4877): no fields (`field_count` 0), model 3727. The guest class table entry
  (`0x4fcadc`, handler `0x261e60`) answers only message 1 (create) and 2 (destroy), registering and unregistering the object
  with a manager. No ability, flag, key, kill-all or message gates it: it is a permanent dragon-only solid.
* Extents (2048 units per scale step): 7825 x 646661..656901, y 139882..160362, z 1728692..1738932; 7826 the same box at
  z 1750196..1760436; 7827 x 645920..657388, y 151100..160213, z 1739606..1749846. They box the gate approach (north, south,
  top) so a dragon cannot fly round the wall 7527; they stay clear of the exit (x 681024).

## Conclusion

* The player is meant to reach exit 5595 by flying (or walking) from the start area to the north-west end of the level; it is
  near the arrival marker pair that serves the return from the next level. The Savage Gate, its wheel, the four message-24
  timers, wall 7527 and the Dragon Barriers form a side vault at the far end of the hall (30000 units west of the exit).
* The vault is closed by unfinished data: the timers wait for message 24 and nothing sends it (see
  `savage-gate-shoot-dispatch.md`), the wall is static, and the barriers cannot be disabled. Nothing needs fixing to finish
  the level, and no game-side change would help the vault; opening it would be a content change (for example a sender of
  message 24 or removing wall 7527 and the barriers in the editor).
* Not shown: the full start to exit flight (see above).
