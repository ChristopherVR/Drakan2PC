# Issue 5: the Volcanic rift box, the one-process run to the credits, Delon with the carried blade, Arokh's flights

Follows [the rift sequence note](ootf-rift-sequence-delon-tunnel.md) (section 3, "the rift box does not open after the
Runeblade") and item 8 of [outstanding work](../2026-11-07/outstanding-work.md). Private runs under `build/ootf12/h/` (own
executable copies in `build/ootf12/play`, sha256 `5c63cef4...` the 16:27 build, and `build/ootf12/play2`, `81a206d8...`
the `build/final-game` build), other agents' games left alone.

## 0. Inputs rebuilt

The clean-up had deleted `build/ootf/cab_disc` and `build/odbx/data`. Both were rebuilt from the user's own files:

* the PC install: `python analysis/scripts/ootf_cab.py "<Downloads>/Compressed/Drakan Order of the Flame/Drakan.zip" --out
  build/ootf/cab_disc` (337 files, the retail disc image `DRAKAN.mdx` in the zip, no patch applied);
* the PS2 inputs: `extract()` of `scripts/harness/ootf_level_run.py` on the ISO in the game folder (template `LEVEL1NA`,
  89 files in `build/odbx/data`, the host MOD / TXD in `build/odbx/hosts`).

Packs (all `--template LEVEL1NA --campaign 1`, **no `OOTF_TEST_*` variable set**), each converter tree a `git archive` of
the main commit that pinned that formats commit, with its own `node_modules` junctions:

| Pack | formats | main (editor-core, scripts) | Files |
| --- | --- | --- | --- |
| `pack-v9` | `918de7e` (2026-11-06 route) | `6a2b8414` | 10,698 |
| `pack-f5` | `f5d3c6b` (2026-11-07 `mkrw4`, `full2`) | `6fdd97eb` | 10,698 |
| `pack-c` | `9e1856f` (current pin) | `HEAD` (`a87d79aa`) | 10,751 |

## 1. The rift box: no pack or executable regression, the blade was never taken

**Static comparison.** In Volcanic the Runeblade 2239, Timer 2331 and the boxes 2318 / 2961 are identical in `pack-f5`
and the current pack (objects, links, parameters, the Runeblade's class record); `pack-v9` differs only by the PC logic
defaults that `34ddc63` added (the timer's "Time Until Trigger" 1 instead of the PS2 5, "Disable/Re-Enable Message" 4, the
boxes' "Detect Only Once?"). The registry slot of an object is its index in the level's object list (Runeblade 1104,
Timer 2331 1158, box 2318 1147, twin 2961 1685), the same in every pack.

**The same step on the 16:27 executable** (`vrf5` on `pack-f5`, `vrhd` on the current pack; the recipe of the earlier `vr5`):
both fail as `vr5` did, and in both the Runeblade is **not in the inventory** after the E taps (carry cells: only the
cage key `0x346c3`), the blade's handle stays in the world and Timer 2331 stays in the registry. The `took` screenshot
shows why: the blade is still inside the ice on the pillar. The recipe skipped the fire-arrow puzzle (sanctum switch,
button, arrow, melt), so E at the blade reaches through the ice only sometimes.

**Re-reading the earlier runs** with the carry cells and slot 1158:

| Run (2026-11-08, `build/ootf11`) | Runeblade carried after E | Timer 2331 after |
| --- | --- | --- |
| `vrw1` | yes (`0x346a3`, cell 1) | **gone** (fired and destroyed, "Destroy After Timeout?") |
| `vr4` | yes | **gone** |
| `vr2`, `vr5`, `vr6` | no | still there |

So the pickup does start the timer whenever it happens; the runs that tested the box (`vr2`, `vr5`) were the ones in
which the blade had not been taken, and the run that took it (`vrw1`, `vr4`) never reached the box (no Arokh in reach). The
note's "RAM in `vr5` and `vr6`: the pickup never started the timer" was a pickup that never happened, and the "cell 1 the
Runeblade" evidence came from `vrw1`, not from those runs. Both earlier passes that opened the box took the blade outside
the ice: `vr14` / `full2` after the melt, `mkrw4` from a pack that moved the blade next to Arokh. No converter or runtime
change is needed for the box.

**Positive check on the clean pack** (`pack-c`, 16:27 executable, the route of section 2 in segments from quicksaves): after
the melt Rynn took the blade with the first E on the pillar top (`r9`: carry cells `0x346c3`, `0x346a3`; Timer 2331 in slot
783 before, gone after), walked into Arokh from his side, and mounted in box 2318 (`r10`): message box, save screen, one
Cross, Rift World gameplay at (342418, 100119, 319535) with both items still carried.

## 2. The Volcanic route as a checked scenario

`scripts/harness/ootf_volcanic_route.py` (live_drive as a library) plays the whole route with a placement beside each
trigger and then the real input, and checks each step in guest RAM before the next one, so a failure is reported at its
step (`failed_at`) instead of later as a closed box. Steps and what each one checks (`--from` / `--to`, `--save-each` for
resume cards):

| Step | Input | Check |
| --- | --- | --- |
| rune | walk into ruin detector 1505, wait for the lift, E beside the box | the Lava Rune is carried |
| lock | equip, E 280 units from Key Lock Invis 2939 | the lava 2257 drops below y 59,000 |
| key, cage | E at the Rune Cage Key; detector 1994 plays 9.6a | key carried; Arokh landed (`+0x9c` 1) |
| mount | walk into Arokh (closed loop) | controlled actor `0x1001c` |
| breath | fly into box 2898 from outside, breath on 2259 | 2259 leaves the registry |
| land | fly north at the cave mouth until he stops, Cross held | Rynn on foot |
| switch | E at Succ Switch 2243 until the platform settles at identity | platform yaw 0 |
| button | E at wall button 2261 | the arrow kills 2366 (its Timer 2368 is destroyed) |
| blade | E on the pillar top | Runeblade carried **and** Timer 2331 gone |
| remount, rift | walk into Arokh from his side; mounted in box 2318 | message box, save screen, Rift World gameplay |
| fly | the tunnel, hall, shaft and causeway (closed loop on W, SPACE, mouse) | 14 of 14 waypoints |
| heads | breath from the altar side at the living head nearest the altar | all four `0x1003b` heads at 0 or below |
| ending | none | screen changes and a shot every 30 s |

What the first passes taught (each now in the script, test `scripts/tests/test_ootf_volcanic_route.py` for the pure parts:
the carry-cell decoding, the blade check, the placement geometry):

* **Key Lock 2939 needs Rynn within about 330 units.** Use on a target farther than that first walks her to it
  (`0x3168a0`); the Key Lock model 2229 stands on the same spot and stops her short, so from 600 or 900 units E did nothing
  (`r2` to `r4`, 16 positions). From 280 units she stands on the model and the lava drains (`r5`). A trace of the Use path
  (`DRAKAN_TRACE_FN=0x3168a0,0x31a718`) named the lock as the target in every try.
* **The catching fire object only appears once Arokh flies into box 2898.** Placed inside the box nothing fires (no
  crossing); placed outside and flown in, two breath rounds destroy 2259 (`r7`).
* **The sanctum switch turns the platform slowly.** A check right after the press saw a half turn and pressed again, so
  the platform ended a quarter turn off and the arrow missed (`r8`: 2366 at 113406, 223792 instead of 113224, 230683).
  The script now waits for the platform to settle and presses until it is at identity (`r9`: three presses from the
  card's start, then the arrow, Timer 2368, the ice cover morphs and is gone).
* **The heads** die to placed breath rounds: each round Arokh is placed facing the living head nearest the altar, from
  the altar side, 3,300 to 4,800 units away and 600 to 1,300 above it (`r12`: all four in 88 rounds, 130 s, no
  AUTOAIM).

Segment runs on `pack-c` (16:27 executable, god mode): `r1` rune; `r5` lock; `r6` lock to mount; `r7` breath, land;
`r9` switch, button (arrow hit), blade (carried, Timer 2331 gone); `r10` remount, rift (message box, save, Rift World with
both items), fly (14 of 14); `r12` heads, then the end sequence `0x00080052` ticks, a non-gameplay screen from +36 s to
+283 s, and the title ("Fortsetzen: Rift World") at +433 s.

### One process, clean pack: Volcanic to Rift World to the credits (`full2`)

`python scripts/harness/ootf_volcanic_route.py --name full2 --exe build/ootf12/play/drakan.exe --mods build/ootf12/pack-c
--level "OotF 11 - Volcanic" --go ram_go ... --env DRAKAN_PLAYTEST_GOD=1 --env DRAKAN_GAME=d1 --shots --ending-seconds 480`:
one game process, a direct start of Volcanic, the pack built with no `OOTF_TEST_*` variable. **Every step passed**, in this
order and time (seconds of the step): rune 16, lock 16 (lava at 44,617 after the first try), key 11, cage 46, mount 19,
breath 35 (two rounds), land 17, switch 41 (three presses to identity), button 40 (first press, arrow hit), blade 17
(carried, Timer 2331 slot 1158 gone), remount 65, rift 9 (message box, save screen, load), fly 18 (14 of 14), heads 136
(62 rounds, all four at -1), then the ending: the end sequence, the mirror room (Delon falls into the mirror, +30 s), the
campaign end card over the Wartok farm ("Original game: Drakan: Order of the Flame, Surreal Software, 1999 / Engine:
Drakan: The Ancients' Gates, Surreal Software, 2002", +60 s; `[hle] credits: read ... patched entry 8807` and `8808`, so
the campaign credits override served them) and the Order of the Flame title ("Fortsetzen: Rift World") from +70 s on.
Level loads: Volcanic at 12.9 s, Rift World at 429.4 s. Host memory, sampled every minute for 15 minutes: working set
1,176 to 1,277 MB, private 1,437 to 1,540 MB, no growth. No watchdog line, clean scripted exit.

A first attempt (`full1`, same command) stopped at `breath`: on a fresh start the catching fire object 2259 is only
created by box 2898's message, so the check that took its slot before the fly-in saw nothing and called the step failed
although the breath had destroyed it; the check now takes the slot after the fly-in. The call in the `remount` step (E
held, Rynn on the pillar top 27,700 units from Arokh, who had landed in the drained basin at y 47,245) did not move him;
the remount then walked into him from his side.

## 3. Delon on foot with the Volcanic Runeblade, god mode off

From the `r10` quicksave over the altar (natural arrival from Volcanic, the blade carried in), **no
`DRAKAN_PLAYTEST_GOD`**: Arokh placed over the ring, Cross held until Rynn is on foot, `place 342044 100804 184976`, then
the inventory (TAB, RIGHT, ENTER, TAB selects and equips cell 1; the plain TAB, ENTER, ENTER, TAB of the earlier note
equips cell 0, here the cage key), checked in the carry cells (cell 1 `0x346a3`, equipped word 1), then the live_drive
`fight` mission (`--stay --too-close 0 --strike-max 1800 --strike-every 4`, private `dl2.py` around it).

`d2`: **Delon 650 -> 600 -> ... -> 50 -> -1 in 13 blows of 50** (frames 8,943 to 9,549, about 12 s, 55 strike taps).
Rynn's hit points (decoded from the HUD block): 50 / 100 at the load (the saved state; she had been mounted), 35 when the
fight started, **20 after a second hit of 15 at frame 9,187**, and 20 at his death. So his attacks do reach and hurt
her (two hits of 15), and the fight is won on foot without god mode. Whether the two 15s were his spell or his melee was
not separated (he stays in melee range against `--stay`).

## 4. Arokh flying off, and "E does not call him"

Read in the PAL ELF (Ghidra decompile, scratch only) and checked against the class data:

* **Calling is a hold, not a tap.** Command `0x4009` (pad L1 / key E) arms a 0.5 s timer on press (player `+0xb5c`); a
  release inside that window is Use (items, levers, locks, talk). If the key is still held when the window runs out, the
  player tick calls the same routine as command `0x4008`, the call: it plays Rynn's call line and sets the dragon's call
  flags (`+0x590` bits `0x200004`), and the dragon tick then turns him to fly to her and plays one of his "Call Response
  Sounds". The `vr3` / `vr4` missions tapped E (one input sample) ten times: ten Uses with nothing to use (Rynn's "nothing
  here" line), never a call. So "E did not bring him back" was the test, not the game.
* **Flying off is his native follow and wander logic.** The rider-less dragon tick reads the class field "Follow Human
  Distance (lu)" (field 147, times 2,048): when Rynn is farther than that he takes off and flies to her, and lands where
  he can; when she is near he idles and every 15 to 25 s may wander. The converted Volcanic dragon has the PC value, 10 lu
  (20,480 units), which is also the PS2 class default. Detectors with "Does Cave Entrance Teleport?" send him to meet her
  at a cave marker ("Meet At Cave Entrance"). The PC dragon class has the same fields with the same values (Follow Human
  Distance 10, Call Response Sounds, Can't Land Sounds, Meet At Cave Entrance Sounds), so the behaviour is the original
  design carried by the original data, not a converter or engine fault. In the earlier runs Rynn was teleported 290,000
  units to the blade and back; he followed her as far as he could land and was then 26,000 units from the cage platform
  on the ledge at (272,000 155,500 472,000), which she could not walk to.

In game (private `arokh.py` on the `r9` quicksave: Arokh landed at the cave mouth 117,600 53,971 211,512, Rynn moved by
`place`, his position and state words each second; a trace of the call routines `0x23f2a0`, `0x269070`, `0x2691d0`):

| Run, Rynn at | Distance | Arokh |
| --- | --- | --- |
| `ak1`, the cage platform | 322,000 | did not move in 60 s, nor after five taps or a 1.2 s hold (outside the active range: no tick ran) |
| `ak2`, by the key lock | 17,400 | stayed landed (under the follow distance); three taps nothing; the hold reached the call routine |
| `ak2`, the sanctum main entrance | 51,000 | **took off by himself** within 5 s (state `0x94` 11, airborne), flew toward her and landed in the drained lava basin at (119,824 46,871 194,047), still 49,800 units from her; three taps and a hold did not move him on (30 s) |

So the follow is real and native (he leaves when she is beyond his follow distance and lands where the terrain lets
him, which in Volcanic's cave and basin terrain can be far from her), and the call is the held E (the trace shows the
call routine `0x269070` running once and returning 1), but a call does not lift him out of a landing spot he has
chosen inside 30 s. The converter carries the PC dragon fields unchanged and the converted terrain matched the PC level at
every probed column (2026-11-07), so nothing points at a conversion fault; whether the PC engine would have landed him
nearer is not testable without a PC run. For test runs: keep Rynn within about 20,000 units of him, or remount by
walking to where he landed.
