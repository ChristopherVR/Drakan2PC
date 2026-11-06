# Sequential playtest of all levels (2026-11-07)

Build: `dist/DrakanPC/drakan.exe` with the boot-hang fix 1f27d1ea. One game of ours at a time via
`scripts/harness/play_run.py --level <code> --recipe ... --env PS2X_PERF=1`, Rynn kept alive with
`DRAKAN_PLAYTEST_GOD=1` for tours. Recipes are generated from `build/lvl-objects/*.json` (derived data, not committed):
11 `place` waypoints per level (the 7 densest 60000-unit cells plus the four extremes in x and z), each 12 s: 6 s hold
after the teleport, then three walk, jump, mouse-turn and attack bursts. Harness files live in `build/harness/cp/`.

Caveat: the machine was NOT quiet. Other sessions ran 1 to 5 `drakan.exe` copies for most of the session (see
"others" below, counted by `tasklist` when each run started). FPS from those windows is contaminated; the rechecks were
also run next to 3 other games.

## Per level

Steady state = perf seconds 4 to 9 after each `place`. vbl = vblank/s (60 = full speed), flips2 = rendered frames/s
(uncapped world cadence, well above 60 in most areas).

| Level | others at start | vbl median / p5 | seconds < 55 vbl | flips2 median | Result |
| --- | --- | --- | --- | --- | --- |
| 1A Surdana | 0 (rerun 5) | 60 / 54 (rerun) | 2 of 66 | 95 | first run HUNG at waypoint 0 (see bugs); rerun clean |
| 1B Kragmor | 0 | 60 / 58 | 1 | 128 | clean |
| 1C Catacombs | 0 | 60 / 59 | 0 | 224 | clean |
| 1D City of the Damned | 1 | 60 / 59 | 1 | 148 | clean |
| 1E Inquisitor's Lair | 1 | 60 / 59 | 0 | 173 | clean |
| 2 Shadowmire | 0 | 60 / 58 | 1 (46 vbl at 75435,474538) | 115 | clean |
| 3A Andrellian Isles | 0 | 60 / 18 | 7 (913524,214378,472058: 10 to 18 vbl, flips2 37) | 74 | recheck with 3 others: 56 to 60 vbl, flips2 60 |
| 3B Crystal Mine | 3 | 60 / 59 | 0 | 115 | clean |
| 4A Ravenshold | 3 | 60 / 17 | 11 (353297,878686 and 1463808,907520) | 102 | recheck: 60 vbl, flips2 540 to 580, one 12 vbl second |
| 4B Northern Tundra | 2 | 60 / 16 | 8 (928600,1406745: ee 36 ms/flip; 330712,1354503; 1028690,1361681) | 55 | recheck: 928600,1406745 still weak (vbl 42/58, flips2 37, ee 9 ms), 751616,1224542 one 14 vbl second |
| 5 Shiverbane | 2 | 60 / 55 | 4 (673168,455096 flips2 5) | 63 | recheck: 57 to 60 vbl, flips2 82 to 107 |
| 6 Valley of the Fallen | 3 | 60 / 59 | 0 | 101 | clean |
| 8 Stratos | 0 | 60 / 54 | 1 (399360,371052: 30 vbl once) | 142 | clean |

The collapses in 3A/4A/4B/5 coincide with the GPU driver waits (`nvwgf2umx` in `[perf-stall]`, 99 stall lines in 4B)
while 2 to 3 other games ran; they mostly did not reproduce on the recheck, so they are treated as contention. Not
cleared: 4B at (928600, 131148, 1406745), flips2 37 and ee 9 ms/flip with 3 other games running; re-measure on a quiet
machine. The last second of every tour (quit and shutdown) shows 0 vbl in the raw logs and is not an in-game stall.

## Death and reload

Method: F5 quick save, `place` 60000 units above the densest cell (a fall), tap Cross. A fall death returns to the
quick save in place: the log shows `gui screen -1 -> 14 -> -1 (gameplay=1)` taking 8 to 25 guest frames (0.1 to 0.25 s of
game time) and no screen 29 (screen 29 only appears at a level's first start, about 3.5 s). Per-second vblank around the
reload stays 59 to 61 in 1B, 1C, 1D, 1E, 2, 3A, 3B, 4B, 8, so the reload is far below the 8 s threshold in every level
where a death happened; no profile was needed. No death happened in 4A (Rynn is on Arokh, no fall) or 5 (landed
alive); those two have no reload number. 1A earlier died on its own at (453143, 161111, 497770), Rynn sinking slowly
(water), which without a save ends at the title screen (screen 8) and a new game.

## Exits (walk-in tests, `place` 4500 units from the Trigger Detector, hold W 8 s, Cross)

| From | Detector | Result |
| --- | --- | --- |
| 1E | 12204 | message box (screen 2), save screen (5), exit taken, next level loaded |
| 2 | 8310 | same chain, loaded |
| 4B | 5592 (to 4A) | same chain, Ravenshold loaded |
| 1C, 1D, 3A, 3B, 4A (6496, 6659), 4B 5595, 5 (4393, 4387), 6 (18190), 1A (12413, 12479) | | not reached by the generated approach (no screen 2 within 8 s); the start side and heading are guessed from the exit position. 1D and 4B/4A exits sit on top of the exit object and need the real approach side; 4A/4B start with Arokh and the 3x levels have scripted openings. Not a game finding. 1A exits were proven by hand earlier (docs/journal/2026-11-01/level-exit-walk-in.md). |

`--level` redirects paths, so every arrival reloads the redirected level name; the chain (exit taken, load) is what
counts. Level 6 Transition to Desert and Level 8's exits were not driven (cut scenes).

## Bugs

* **Intermittent hang, 1A (build/harness/cp_t1a)**: right after the first `place` (465987, 159764, 460554) the log shows
  `[guest-branch:missing-target] tid=1 kind=IndirectCall op=JALR source=0x10aa6c target=0x0 ra=0x10aa74`; the object at
  s0 has an unreadable vtable (`vtableReadable=no`), the scene walker trace goes through 0x1c74d4 -> 0x18a854 ->
  0x153494. The EE thread then waits forever (flips 0 for 350 s, vblank 60, `[perf-stall] ee thread` in a wait at
  exe+0x685738). The identical recipe ran clean on a second attempt, so it is a race with the teleport (`refileActor`
  while the scene walks its list) or a stale object. Not fixed: no deterministic repro, call site 0x10aa6c not yet
  traced. Real play does not teleport, so the player-facing risk is low but unproven.
* **Shadowmire spot (320168, 205764, 503600)**: placed there Rynn drops 23600 units and rests at (324330, 182141, 502128);
  she did not fall out of the map in this run and did not move under W/S afterwards (hold or blocking geometry, not
  analysed). The earlier report may be a different y.
* **4B first ledge**: the level data puts Rynn at (839436, 145504, 985491) with Arokh ("Dragon") 2.8k units away at
  (839353, 145722, 982917), a ring of "Dragon Barrier" objects and "Invisible Static Wall" objects at z 996k to 997k, and
  Generic Detectors north of the yard. So the start is a walled yard and Arokh is meant to be used (mount with E);
  stepping off a ledge on foot is a fall by design. No change needed.
* Nothing crashed otherwise; no `perf-stall` on the ee thread outside the 1A hang.

## Harness notes

* `log DEATH_BEGIN` is `log off`; `log` takes only `on [every]` and `off`.
* A recipe in guest frames runs at uncapped tick speed (a 600 frame waypoint lasts 2 s); use `clock milliseconds`.
* `place` into the void (y - 150000) does not kill; a fall onto ground does.
* Shots queued behind a load can fire together.
