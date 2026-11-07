# Issue 5: a closed-loop play-test driver, the Volcanic stair, the remount from behind and the war giant

Follows [landing and stair](ootf-volcanic-landing-and-stair.md) and [giant, Islands, rift](ootf-giant-islands-rift.md),
whose open items were all blocked by open-loop input scripts. Runs, recipes and helpers are private under
`build/harness/issue5-loop/` (`runs/<run>/drive.jsonl` is the per-decision log of each run, `result.json` its summary;
`sanctum.py` and `ledge.py` are private missions built on the driver, `sumfight.py` a fight trace). Pack: formats
`4d21976` (HEAD, a git archive resolved through the loader hook as before), all 23 levels; its level data is byte for
byte that of the `7392049` pack (only the shared resource files differ). Executable: `build/harness/issue5-loop-dist`,
incremental builds of the working tree on main with the live channel below (the last one `a36b06e7...`, after
`dd756965`; other sessions' uncommitted renderer and HLE edits were in the tree). Direct starts, god mode
(`DRAKAN_PLAYTEST_GOD`), placement hold 30 (`DRAKAN_PLAYTEST_HOLD=30`) where a run places Rynn. Nothing from either
game is quoted here.

## 1. The driver

* **Live input** (`DRAKAN_INPUT_LIVE=<file>`, harness only, commit `f5dab15f`): next to an input script, lines appended
  to the file while the game runs are read before each frame and run like script lines, timed from the sample they
  arrive on. The glue writes the host address of a small status block to `<file>.status` (guest RAM address, sample
  number, gameplay and screen state, live lines applied). Nothing changes without the variable.
* **`scripts/harness/live_drive.py`** starts the game with a setup recipe, waits for a RAM dump the recipe writes as its
  "go" state, then reads Rynn, the controlled actor and a target out of the process every sample (ReadProcessMemory)
  and steers: `face`, `fight` (distance, facing, the target's animation id, grab detection), `path` (pure pursuit over
  waypoints, with a height gate per waypoint), `approach` (walk into an actor from a chosen side of its own facing until
  the controlled actor changes). Every decision is logged. Mouse turning in direct look mode is 0.002 rad per count
  (measured at the start of each run, the sign included); with a gain of 0.35 per sample the facing holds within a few
  degrees.

## 2. Volcanic spiral stair on foot: pass, 5 of 5

Setup: card `vol-melted` (this pack: Lava Rune, lock, key, scene 9.6a, walk into Arokh, breath, flown landing, both
Succ Switch presses, wall button, the arrow hits 2366, melt; F5), F8, Rynn placed once on the east bridge facing the
pillar (127928, 53700, 228603), then `path` over `stair6.json`: from the bridge to the stair foot at 27 degrees, then
10-degree points along the walked radius of the best earlier try (1.07 to 1.29 lu from the axis) for 1.25 turns, the
last point only passed at 1.42 lu height, then the pillar centre. Then E twice.

| Run | Foot to top (samples) | On the top at | Runeblade |
| --- | --- | --- | --- |
| `st1` | 423 | 116888, 56680, 226922 | taken (handle cell 930 to -1) |
| `st3` | 501 | 116898, 56686, 226873 | taken |
| `st4` | 396 | 116891, 56681, 226907 | taken |
| `st5` | 385 | 116890, 56677, 226921 | taken |
| `st6` | 357 | 116889, 56678, 226919 | taken |

`st2` ended in the renderer stop of section 6 before the walk. The speed differences (357 to 501 samples) no longer
matter: the steering follows the position. What the earlier 24 tries got wrong:

* **The stair foot is tangential.** Walked radially at the foot (the old recipes, and `s4` here with a point at 25
  degrees and 1.10 lu) she presses into the column and stays there. Approached along the stair from the bridge end she
  climbs at once.
* **The radius changes along the stair** (about 1.1 lu on the east side, 1.29 on the west); a fixed circle at 1.16 or
  1.22 lu lets her walk round on the platform floor without climbing (`s2`, `s3`, `s5`).
* **The stair depends on the platform's turn.** With the platform not turned (`vol-sanctum`, where the open-loop
  switch presses had not taken) the stair foot is 180 degrees round; the converted geometry is the same.
* The top is reached at about -65 degrees on the second turn (1.45 lu); from there she steps in onto the pillar top.
  No inner-edge sticking happened on the steered path, and no converter change was needed.

Side findings: the open-loop Succ Switch presses failed with the default 240-boundary placement hold (`vcard3`, 5 of 5
presses) and worked first time with `DRAKAN_PLAYTEST_HOLD=30` (`vcard2`, `vcard4`, a state-based wait on the platform
matrix). Arrow Detect Object 2366 shares its resource id with two other objects; the hit shows as that count going
from 3 to 2 (as in `full2`).

## 3. Remount from behind Arokh: approach geometry, not a converter defect

`approach` from 2,600 units on a side of Arokh's own facing (he stays landed on the apron at 117600, 53945, 210065,
yaw pi, `vol-sanctum` card), W held and the facing steered until the controlled actor is Arokh (`0x1001c`) or 40 s:

| Run | From | Closest, xz / 3D | Mounted |
| --- | --- | --- | --- |
| `rm5` | his left | 1393 / 1409 | yes, after 41 samples |
| `rm2` | behind | 1424 / 1440 | yes, after 335 samples |
| `rm4` | behind | 1431 / 1447 | yes, after 1073 samples |
| `rm7` | behind | 1443 / 1459 | no |
| `rm6` | behind | 1463 / 1479 | no |
| `rm3` | behind | 1476 / 1491 | no |

The mount fires exactly when the 3D distance to his origin drops under the engine's 1,448 units (squared 2,097,152,
[mounting](../2026-11-06/ootf-mounting-volcanic.md)); from behind his body and tail hold Rynn at 1,440 to 1,490, so
whether she gets in depends on where she slides along him (the logs show her pushed back 300 to 500 units at a time).
From the side she gets to 1,409 at once. The old recipes held W for 150 samples, too short even when it works (335
and 1,073 samples here), hence the "stalls 2,600 short". The dragon is the PS2's own Arokh class with its own collision
and the mount check is engine code, so nothing in the converter decides this. One difference to Drakan 2: in Northern
Tundra (Initial Mode 1) Arokh turns to face a placed Rynn within the settle time, so she never stays behind him (`t-behind1`,
`t-behind2`: 5 placements behind, he turned each time, mounted from the front). Volcanic's converted Arokh has no
Initial Mode (0, as in the Drakan 2 levels where Rynn starts mounted) and keeps his heading. Left as it is: from the
side or the front he mounts at once.

## 4. Alwarren war giant: not killed; the grab is the limit

`fight` from a card on this pack (`alw-card.txt`: sessA's crystals, both-side locks and sword, then Tab, Enter for the
first inventory hint box, E equips, F5 next to the arena; `g1`), F8, then the driver. The giant's base-layer animation
ids, read live each sample: `0x50686` run, `0x503fc` lunge step, `0x502fc` reach, `0x502fd` swipe, `0x502fa` grab,
`0x502ff` throw, `0x5065e` stagger, `0x502fe` stand, `0x51630` and `0x507de` / `0x507dd` others.

* **Hits**: almost all land while he swipes (`0x502fd`) or reaches (`0x502fc`) and Rynn, facing him to within 2
  degrees, attacks at 1,860 to 2,060 units (his arm comes into her reach). Counter-attacking those animations instead
  of closing in took him from 150 to 90 within about 10 s (`g5`, `g6`); closing in on a standing giant to 1,400 units
  gets grabbed.
* **Grabs**: a lunge step (`0x503fc`) at 1,800 to 2,150 units turns into a grab (`0x502fa`) within a few samples; from a
  reach at 1,200 to 1,600 as well. Backing off (S), a sidestep (D), turning away and running, and a jump at the start
  of the lunge all failed to get out of it (`g4`, `g7`, `g8`, `g10`, `g12`, `g13`). After a grab Rynn is thrown, lies
  with player `+0x94` = `0x200`, the glue never sees gameplay again (no on-foot camera look) and she does not get up in
  minutes with god mode; that ends the attempt. Each attempt from the card starts with his charge.
* **A safe spot**: running away once popped Rynn onto a wall top at (245700, 61934, 481000); walked there again on
  purpose (`g11`, `ledge.py`) he reaches for 4 minutes without a grab, but none of her 1,911 attacks lands from there.

| Run | Tactic | Hit points | Ended by |
| --- | --- | --- | --- |
| `g1` | approach and strike | 150 to 120 | grab |
| `g5` | counter `0x502fc`, `0x502fd`; strike a standing giant | 150 to 90 | grab after closing in |
| `g6` | counter, keep 1,950 | 150 to 90 | grab from a lunge |
| `g12`, `g13` | counter, jump at a lunge | 150 to 120, 135 | grab |
| `g4`, `g7` to `g10` | back off, sidestep, run | 150 | grab during the opening charge |
| `g11` | wall top, counter | 150 | time (no hit possible) |

So the fight is not a converter problem (he takes sword damage, staggers, grabs and throws as built), but a kill needs
ten hits without one grab, which these tactics do not reach. The PC reveal "Bellhammer1" (timer 1764) therefore stays
unplayed without the test aid. Not tried: other weapons (only the sword is in this inventory), the giant's own AI state
words (only his animation was read), and a check that a thrown Rynn without god mode gets the load prompt.

## 5. The live channel in practice

44 runs; the driver held the facing within a few degrees, followed the stair within about 0.1 lu and kept every
decision in its log. Missions compose from a private script (`sanctum.py`: walk into Arokh, the flight lines, then
switch presses with a state wait on the platform matrix; `ledge.py`: a path, then a fight).

## 6. Engine and renderer stops seen

* The renderer's validation stop at texture creation ("mip level count N is invalid, maximum allowed is 1", panic in
  `wgpuQueueSubmit`) hit 5 runs, now also mid-level (`g2-renderer-stop`, `s1-renderer-stop`, `s3-renderer-stop`,
  `s4-renderer-stop`, `st2`). Clamping the mip count at texture creation (tried, not kept) only moved the error to a
  later write of mip level 1, so the caller asks for levels a 1x1 texture cannot have; left to the renderer work.
* With Rynn pressed into the stair foot for minutes (`s3`, `s4`), the log fills with "VU1 reserved lower" lines and the
  frame rate drops to 2 per second. Not seen on the steered walks; not followed further.

## Still open

* The war giant kill and with it the PC reveal of the Bell Hammer.
* Whether Drakan 2's Arokh turns to face Rynn because of his Initial Mode or something else (it decides only whether a
  player ever stands behind him).
