# Issue 5: the Rift World "6 s sequence", Delon on foot, and the tunnel flight

Follows [the rift flight note](../2026-11-07/ootf-rift-flight-and-delon.md). Private runs under `build/ootf11/h/` (own copy of
the 16:27 executable in `build/ootf11/play`, sha256 `5c63cef4...`), god mode, other agents' games left alone. Pack: a fresh
`ootf-convert.ts --ootf build/ootf/cab_disc --ps2 build/odbx/data --template LEVEL1NA --campaign 1` from formats `9e1856f`
with **no `OOTF_TEST_*` variable set** (`build/ootf11/pack`, 23 levels, 10,751 files, 2 min 30 s).

## 1. The sequence that moved Arokh into the rift is the ending test aid

The sequence ref `0x00080052` is PS2 SSD record 82 of `LEVEL1ON.SSD`, in folder 81 "OotF OotF 12 - Rift World". Its body
starts with the name **"end sequence"**: it is the PC "end sequence" (PC SSD id 115, 50 actions on rynn, arokh, camera, Delon,
the mirror room, the faders and the GUI changer; the probe counted 51 with the converter's additions). Records 83 and 84 are
"10.1a" and "10.2a". In the PC level only Stomp Player 473 plays it, after Ribs timer 472, after the Navros DeathCounter 162,
which the four Dr. Evil heads (108 to 111) count down when they die. The scene's first arokh actions place him over the altar
hole (341767 94803 183845), which is where the flights saw him "jump" before he fell into the pit.

What fired it 6 s after the start: the pack the earlier runs used, `build/harness/ootf7/packe`, was converted with
`OOTF_TEST_ENDING` (the test aid of `ootfpuzzle.ts` `testEnding`, introduced for the ending proof of
[2026-11-01](../2026-11-01/ootf-puzzles-ending.md)): four timers at 0,0,0 send the death message to counter 162 a few seconds
after the load. The converted Rift World of `packe` has 157 objects, four of them those timers (568 to 571, class 8/128, links
162); the clean pack has 153 and nothing but the four heads links 162. The credits note of 2026-11-07 used that property on
purpose ("the pack that carries the ending timers"); the flight runs inherited it by accident.

In game, same executable, same idle direct start of Rift World with `DRAKAN_STOMP_PROBE=1`:

| Run | Pack | Arokh over 890 input samples | Sequence ticks |
| --- | --- | --- | --- |
| `watch-packe` | `ootf7/packe` (ending timers) | moved to 341767 94803 183845, then falls to y 83,491 and below | `ref=00080052`, 51 actions, from 34.7 s |
| `watch-clean` | `ootf11/pack` | stays at 342418 100119 319535 | none |

So it is not a conversion bug and not part of the PC level's opening. No converter or runtime change. Recipes that fly or
fight in Rift World must use a pack built without `OOTF_TEST_ENDING` (and without the other `OOTF_TEST_*` aids unless the run
needs them).

## 2. Delon on foot: won with the Runeblade

### Where the weapon comes from

The PC campaign gives Rynn no weapon in Rift World: its player object (239) links nothing and the level's only weapon item is
the Runeblade 512 (engine class 14), which the scenes 10.1a and 10.2a use as a prop. The weapon she is meant to carry is the
Runeblade 2239 of Volcanic (engine class 14, a real weapon: "Runeblade: 50AP (Unzerstoerbar)"), which she takes from its
pillar in the ice cave; Volcanic also places a Lightning Axe (2510) and a second Runeblade (2946). Of all PC players only Ruined
Village's (1740, Atimar 1059) has a starting loadout.

The converted pack does not drop it. Read from guest RAM (Rynn's 24 carry cells, `player + 0x584 + 0x30`, 16 bytes each):

* Volcanic, after E at the blade in its own place (`vrw1`, `vr2`, `vr5`; Rynn placed beside the pillar, the blade not moved):
  cell 0 the cage key (resource `0x346c3`, class `0x10021`), cell 1 the Runeblade (`0x346a3`, class `0x1000e`).
* A Rift World arrival through the rift box (the earlier `mkrw4` run of 2026-11-07, re-read before its folder was deleted):
  the same two cells on the new Rynn after the load (`ram_arrived`, `ram_saved`). The level change carries her inventory.

So the earlier "empty inventory on arrival" (Tab showing no item) was a misreading, most likely Tab while mounted; the cells
were never empty after a natural arrival. Equipping needs the modern inventory: TAB, ENTER (closes the first-use help box),
ENTER (equips the selected cell 0), TAB. The `1` key did not equip it (cell `equipped` stayed 0, `ed0` 0); after the inventory
route `equipped` is 1, `ed0` is set and the blade is in her hand.

### The fight

Because the natural Volcanic exit did not open in this pass (section 3), the fights below take the weapon from the Rift World
Runeblade 512 instead, the same weapon class: a direct start of Rift World, C to dismount, `place` beside the blade (it floats
at 156672 125015 87552, she picks it up on contact), `place 342044 100804 184976` on the altar ring, equip as above. The
placements are the only test aids; the pack is unchanged and the fight is played with real inputs by
`scripts/harness/live_drive.py` `fight` (target class `0x10052` nearest 339328,184192; `--stay --too-close 0 --strike-max 1800
--strike-every 4`: she turns to him and strikes, never walks).

| Run | Policy | Result |
| --- | --- | --- |
| `dl1` | default, `1` key (no sword in hand) | 796 strikes, Delon 650 -> 650 |
| `dl3` | default (steps back when he is closer than 1,150) | he walks into her, she backs into the altar hole (y 58,491) after 79 decisions; 650 throughout |
| `dl4` | `--stay` | 650 -> 600 -> 550, then a hit knocks her into the hole (she stood at x 342,820, the hole's edge) |
| `dl5` | `--stay` | **650 -> 600 -> ... -> 50 -> -1 in 13 blows of 50, frames 2,781 to 3,276 (about 10 s)**, Rynn on the ring all the time (lowest y 100,587) |
| `dl6` | `--stay`, stomp probe | killed at frame 3,231 (43 strike taps); **his death plays 10.2a** (`ref=00080054`, 464 actions, from 64.6 s to past 50 s elapsed: Rynn, Arokh and Dr. Evil scenes) |

Every hit took exactly the blade's 50: his "Defense 100 %" does not stop a sword hit from the front, and nothing on the PS2
side gates his damage (the converter's `Invulnerable? 0` from `5e67f7c` is in force). The death message reaches counter 455
and Stomp Player 436 as on the PC. Delon is beatable on foot.

Not covered: the same fight with the Volcanic Runeblade carried in (blocked by section 3), and his ranged spells (he stayed in
melee range in `dl5` and `dl6`; god mode was on, so the damage he does to her was not measured).

## 3. Volcanic: the rift box does not open after the Runeblade in this pass

Volcanic's exit to Rift World is the mount-only detector 2318 (and its converter twin 2961, both "Initial State" off). The
Runeblade 2239 links Timer 2331 (1 s, Trigger Message On), which switches the two boxes on. Runs `vr2` and `vr5` (`vr.py`, a
private mission on `live_drive`: recipe for key, cage scene and blade, then the closed-loop walk into Arokh, the box, the
engine's own message and save screens): Rynn mounted, Arokh placed in the box (Rynn at 294,142 100,866 137,886), held there
and flown 40 samples through it, three times: **no message box (screen 2)**. RAM in `vr5` and `vr6`: the timer (registry slot
1158, class `0x1003e`, at 118613 65922 241664) and both boxes (`0x1003d`, flags `0x18009140`) are byte-identical before the
pickup, after it, 6 s later at the blade and after standing 6 s next to the timer: the pickup never started the timer.

The same step passed on 2026-11-06 (`vr14`, formats `918de7e`, blade taken on its pillar) and on 2026-11-07 (`mkrw4`, formats
`f5d3c6b`, blade moved next to Arokh by a test aid). Not separated: pack (formats `9e1856f` against those) or executable (this
pass used the 16:27 executable; `mkrw4` used `4f4a59e8...`), nor whether picking up by touch and by E differ. The bisection
needs a pack from an older formats commit, and the inputs it needs (`build/odbx/data`, `build/ootf/cab_disc`) and the old packs
were deleted from `build/` by a clean-up during this pass. Open, with this evidence; first step: rebuild the pack at `f5d3c6b`
and run `vr.py` on both executables.

Other traps met on the way: Arokh does not stay where the cage scene leaves him. If Rynn is away for more than a few seconds he
flies to a ledge about 18,000 units off (272,000 155,500 472,000) that she cannot walk to, and E (Call Arokh) did not bring him
back in ten calls (`vr3`, `vr4`); `vr5` placed her behind him (the `approach` mission's `--start-from`) and then walked in.

A check with the newer `build/final-game` executable (`vr7`, sha256 `81a206d8...`, own copy in `build/ootf11/play2`) was
inconclusive: Arokh was not mounted after five placements, and the blade was not picked up in that run either.

## 4. The RibDoor tunnel flown end to end by the closed loop

With the clean pack the flight is no longer cut off. `fly2`: direct start of Rift World, `DRAKAN_PLAYTEST_HOLD=1`, god mode,
the private `fly.py` mission on `live_drive` (W held, SPACE to climb, mouse x steered on the heading error minus 22 frames of
the measured turn rate, mouse y holding the pitch toward the next point; nothing placed, no height hold, no autopilot). One
change to the mission since 2026-11-07: a waypoint also counts as passed when the next one is nearer than it (in `fly1` Arokh
flew past the ramp point 3,300 units wide, entered the hall, turned back for the missed point and stuck against the hall wall at
339,384 95,107 275,553 for the rest of the run).

Waypoints: the dive and left tunnel of the earlier `fly5` (338500 97500 300000, 335500 96000 288000, 334300 95300 281000, 333800
95000 277500), the hall (337000 95500 276500, 342000 98000 272000), then the shaft and causeway of the 2026-11-07 autopilot
route (342418 at heights 100,500 to 108,800 from z 262,000 to 188,000) and the altar (342100 103000 183200).

| Frame | Position | Waypoint passed |
| --- | --- | --- |
| 3,981 | 339,596 97,620 300,872 | 0 (dive) |
| 4,117 | 334,318 95,283 282,377 | 2 (tunnel mouth, ramp) |
| 4,240 | 332,404 94,919 278,016 | 3 (end of the ramp) |
| 4,310 | 341,081 97,965 272,566 | 5 (hall, into the shaft) |
| 4,582 | 342,778 108,792 226,289 | 9 (over the spines) |
| 4,684 | 342,199 102,440 184,218 | 13 (over the altar) |

All 14 waypoints in 703 input samples (about 14 s); the lowest point was 94,741, the "landed" word `+0x9c` stayed 0 the whole
way, and the slow state `+0x94` = 10 appeared in the ramp and hall (139 of 191 samples between waypoints 2 and 5) without
stopping him. The end picture shows Arokh over the altar ring with a Dr. Evil head beside it. So the tunnel, hall, shaft and
causeway are flyable by a closed loop on a player's inputs from the arrival; the earlier "cut short after 450 frames" was the
test-aid scene of section 1.

## 5. Not done in this pass

* **Volcanic to Rift World to the credits in one process**: blocked by section 3 (no exit from Volcanic with this pack and
  executable). The Rift World half is shown in pieces: the flight (section 4), Delon (section 2); the heads, the end sequence
  and the credits were shown on 2026-11-07 (`ootf-progression-3.md`, `ootf-credits-title-chosen.md`).
* The chain from the opening in segments on one pack (Atimar): not needed for a weapon any more (the Runeblade is the weapon
  the PC campaign gives for this fight), not run.

Private scripts and runs: `build/ootf11/h` (`run.py`, `go.sh`, `fly.py`, `vr.py`, `dl.py`, recipes), RAM readers in
`build/ootf11/x` (`inv.py` carry cells, `objat.py` registry actors at a point, `ps2obj.mts` converted objects of a pack level).
