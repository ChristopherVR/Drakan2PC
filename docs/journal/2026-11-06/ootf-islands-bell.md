# Issue 5: Islands bell, detector cut scenes and the Bell Hammer

Follows [ootf-progression-2.md](ootf-progression-2.md) ("The Islands bell ... and its dropped sequences"). Runs and helpers are
private under `build/harness/ootf-bell/` (`run.py`, recipes, `runs/<name>/`; `seqneeds.mts` lists every sequence ref of the PC
levels, `pobj.mts` PC objects with parameters and incoming links, `ps2params.mts` converted objects with decoded parameters,
`bounds.mts` PC model bounds, Ghidra exports under `dec/`). Packs: `pack/` (the fix), `packt/` (the fix plus the test aid below).

## 1. Why the scenes were dropped

A sequence ref in an object parameter is read from the SSD of the database of the object's own class (db 0 = that database's
own SSD, `ootf-sequences.md`). The converter grouped each level's refs by the stem of the converted class's ODB. Stomp Players
(engine 51) convert to the template's own `LEVEL1ON` Stomp Player, whose `LEVEL1ON.SSD` exists. Detectors (engine 61) that
play a scene themselves (PC "Task" 4 = Play Sequence, "Sequence To Play") clone FX / SYSTEM Trigger Detectors, and the disc
has no `FX.SSD` or `SYSTEM.SSD` (only `LEVEL*.SSD`): the group was dropped with "no FX.SSD target". Neither FX.DB nor SYSTEM.DB
lists a level database, so no ref of those classes can reach a converted scene at all.

Rule now (formats `relayDetectorSequences`, `sequenceSsdStem`): sequences of a class whose database has no SSD go to the SSD of
the template's Stomp Player database (`LEVEL1ON`); the detector keeps its box and links, becomes "Trigger Only" (Task 6), gets its
PC Trigger Message written explicitly (the FX default is 5) and links to a new Stomp Player at its position with that sequence,
"Message To Play Next" = the same message. A detector's links get its message on the PC as well (Islands 4895 also starts the
"Find Rimril" quest and three timers), so the other links behave as before. The detector's own Sequence To Play is written 0.

Per level (object refs to sequences, unique per level; PC `.ssd` = sequences in the level's own SSD file; before = pack6):

| Level | PC `.ssd` | Referenced | Converted before | Converted now | Not converted |
| --- | --- | --- | --- | --- | --- |
| Intro | 4 (+ 4 Common, 9 Objects, 4 Mountain Common) | 4 | 3 | 3 | detector 2516 names id 1, which its SSD does not have |
| Ruined Village | 8 | 6 | 6 | 6 | |
| Ruined Village Book | 1 | 0 | 0 | 0 | |
| Wartok Canyons | 5 | 3 | 0 | 3 | |
| Grimstone Mines | 3 | 1 | 0 | 1 | |
| Grotto | 3 | 3 | 3 | 3 | |
| Islands | 6 | 5 | 1 | 5 | |
| Alwarren | 7 | 7 | 7 | 7 | 6 animation actions have no mapping (as before) |
| Rift Crystal | 17 | 12 | 12 | 12 | |
| Islands Night | 1 | 1 | 0 | 1 | |
| Succubus Lair | 16 | 14 | 12 | 13 | "Detect and Execute" 1689 names id 41, which its SSD does not have |
| Volcanic | 15 | 11 | 4 | 11 | |
| Rift World | 7 | 3 | 2 | 3 | |
| Arenas (10) | 0 | 0 | 0 | 0 | |

18 of the 20 dropped refs are converted now; the other two (Succubus 41, the Intro's id 1) name sequences their PC SSD does not
have.
Sequences no object names (for example Rift Crystal's 5 others) are not converted, as before. The converter prints
`sequences <level>: N referenced, M converted` and `N detector scene(s) given a Stomp Player` per level.

Islands: 3150 "Stomp5.1" (mounted arrival box by the Grotto entrance), 4895 "Rimril first contact", 4909 / 4916 "No Tasks Done"
/ "partial completion" (task gates), and Stomp Player 6136 "Bell Hammer" (already converted before).

## 2. The bell

PC chain (Islands): "Bell Hammer Detector" 4128 is a Key Lock (engine 34, Lock Code 20, Trigger To Send ON) at
(656374, 101088, 474093), a 1.25 x 0.5 x 1.25 cell box about 23,000 units above the tower floor (77832) and 6,800 above the top of
the bell chain; nothing can stand there. Its links: Chain 4127, Bell 4126, Whirlpool 01 / 02, Triggered Sound 6040 and Timer 6138
(-> Stomp Player 6136, message 4, the "Bell Hammer" scene). The Bell links Timer 4129 (1 s) -> Whirl Doors 01-04, Whirl Bell Lock,
Timer 4130 (Off to bell and chain) and Timer 6068 (3 s) -> "hide detector" 6069 (hides doors 02-04). The rift box 4131 lies under
the doors. The only item with code 20 that is not a Key is "Bell Hammer of Alwarren" (engine 117, Activation Radius 0.76 lu);
the Jail Keys (Key, 33) share code 20 and open Succubus Lair's Iron Lock. Alwarren holds the hammer (shown by a Visibility Toggler
after its puzzle); the Islands level also holds one, 6135, floating over a hole west of the tower.

PS2 engine (Ghidra, read-only): the player's Use handler `0x3168a0` (from `Game_OnInputCommand`) takes the nearest usable object
of the player's touch list (`0x31a718`, list at player +0xe34) within 1,024 units horizontally; for a Key Lock (0x10022) it opens
only with a Key class item (0x10021) whose field 22 (Lock Code) equals the lock's (`0x31ac68` walks the inventory), else plays
"No Key Sound". The PS2 engine 117 class (Blade Dragon Egg, runtime 0x10075) is never matched against a lock. So the converted
bell could not ring: the hammer was an egg, and the lock hung far out of reach.

Conversion now:

* Engine 117 is substituted by the Key (33) (`PS2_SUBSTITUTES`, forced because the PS2 has its own 117, model stand-in so the
  clone shows the converted hammer): the hammer becomes a Key class with Lock Code 20 at field 22 (test in `ootfclass.test.ts`).
* A Key Lock whose code belongs to a PC Quest Item (117) and to no Key placed in the same level (`questItemLocks`, formats
  `ootfpuzzle.ts`) becomes a Trigger Detector column centred on the lock, at least two cells wide (2.5 here), from the lowest
  floor under it (74712) to the lock's top: Task Trigger Only, Detect Which Rynn On Ground, Detect Only Once, Trigger Message =
  the lock's Trigger To Send, "Trigger Only If Carrying Item..." = the converted hammer class. The lock's links move to it (the
  lock stays, unreachable, without links); objects linking to the lock reach it too. Ringing = entering the tower's centre
  carrying the hammer. The class-ref value is written in the frame of the detector class's own database (SYSTEM.DB 1 =
  Resources: `0x1424f` for class 16975), as the disc's carry-gated detectors do (LEVEL1NA SYSTEM detectors name Resources
  db 1, LEVEL2RE / LEVEL4ON ones their own .DB index of Resources).
* Test aid `OOTF_TEST_MOVE="<title part>|<object id>|x,y,z;..."` moves converted objects (used to put 6135 on the tower floor).

## 3. In game

`dist/DrakanPC/drakan.exe` (sha256 `604aef63...`), private ini / card / logs per run, god mode, `DRAKAN_STOMP_PROBE=1`, pack
`packt` (the fix plus `OOTF_TEST_MOVE="Islands|6135|656384,78000,479300"`: the level's own Bell Hammer put on the tower floor
south of the bell dais). `cam.py` (`build/harness/lc-mount/`) reads the controlled actor from the RAM dumps, `handles.py` the
live objects.

| Run | Start | What happened |
| --- | --- | --- |
| `bell-a` | Islands direct start | Cross held at the start (Arokh hovering in the cave): no dismount; placed mounted into the tower, Arokh does not move. Not a bell test |
| `bell-b` | Grotto, walked exit, Islands on Arokh | W 2 s, Cross held: Rynn on foot (`0x10009`, `+9c` 1). Placed on the tower floor, W to the hammer, E: the hammer leaves the world cell (handle 2350 cell 65 -> -1). W then stops at the dais step at z 477,314, outside the column: nothing fires |
| `bell-c` | same | with jumps onto the dais: Rynn enters the column at (656418, 78735, 476445) and the scene `0x00080011` (LEVEL1ON.SSD id 17 = "Bell Hammer") runs for 10 s with 14 actions (camera on the bell platform from above, `bell1`, `bell3`). After it the whirl doors stand open: doors 01 / 02 slid to z 472,062 and 03 / 04 to z 476,162 (one cell each way), the Whirl Bell Lock rose to 81,251, doors 02-04 changed visibility (hide detector), both whirlpool movers run (`f9c` 0 -> 0x42 / 0x2) |
| `bell-d`, `bell-e` | same | Arokh stayed airborne under Cross (`+9c` 0): dismount needs him landed. Not bell tests |
| `bell-f` | same, Arokh placed on the arrival ground before Cross | dismount, hammer, column, scene (69.7 to 79.9 s), doors open as in `bell-c`; W into the open centre: Rynn drops through the doors (`walk_in1`) into the rift box, touch `1001`, message box, save screen. The second save on that card did not complete under the recipe's taps (slot screen, an earlier save on the card) |
| `bell-g` | same | Circle at the save screen skipped the save: back in the Islands basin under the doors (75,210), the queued exit stays dead (known) |
| `bell-h` | Islands direct start, Arokh placed on the ground, Cross | full route on one card: dismount, hammer (E), dais, scene at 48.9 s, doors open, drop through, prompt, save, "exit taken in 'OotF 06 - Islands'", `rider controlled 1, dragon follows 0`; Rift Crystal loads with Rynn on foot at its start (95232, 220938, 185237); W 5,000 units, world drawn at 159 fps, clean scripted quit |

So the intended route works on the PS2 engine: with the hammer in the inventory, entering the bell tower's centre rings the
bell, the "Bell Hammer" scene plays, the whirl doors open and the way down into the rift box (and on to Rift Crystal, on foot)
is free. Without the hammer the column does not fire (`bell-b`, standing south of it, and every earlier tower run).

## Not covered

* The hammer was moved by the test aid; acquiring it in Alwarren (its puzzle, the Visibility Toggler) and carrying it to the
  Islands through Alwarren's exit was not played. The Islands' own 6135 floats over a hole west of the tower in the PC data.
* The other converted detector scenes (Wartok farmer, Volcanic 7, Grimstone 3.1, Islands Rimril / tasks, Islands Night,
  Succubus, Rift World) were checked in the converted data only, not in game.
* Ringing is entering the column, not a Use press; a PC player may have to use the hammer from the inventory there. The PC
  executable was not examined.
* The dismount from a hovering Arokh is unreliable in the harness (`bell-d`, `bell-e`); placing him on the ground first works.
