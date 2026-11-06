# Issue 5: Stomp Player play order, class-default scenes and Volcanic scene 9.2

Follows [ootf-mounting-volcanic.md](ootf-mounting-volcanic.md) ("Still open" 2 and 3). Runs and helpers are private under
`build/harness/ootf-stomp/` (`run.py` as in ootf-mount with the PID in the slot lock, `audit.mts` every PC Stomp Player and
Play Sequence detector of the 23 levels with its effective values and senders, `eff.mts` effective field values of PC
objects, `ps2cls.mts` PS2 class defaults, `ssdname.mts` sequence names of an SSD, `gpc/` a scratch Ghidra project of the PC
`Dragon.rfl`, `dec/` and `decpc/` the exports read; pack `pack/`). Code was read in Ghidra; nothing verbatim here.

## The two engines

The handlers come from the class tables: PS2 runtime class `0x10033` -> handler `0x4091a0` (instance 0x14 bytes); PC
`Dragon.rfl` class 0x33 -> `0x10040450` (instance 8 bytes, table at `0x10174790`, triples {class, handler, size}). The field
order is the same on both (Sequence List, List Play Order, Initial State, Message To Play Next), but the enum is not:

| Value | PC (`Dragon.rfl` resources) | PS2 (ELF property table) |
| --- | --- | --- |
| 0 | In Order Once | In Order Once |
| 1 | In Order Loop | In Order Loop |
| 2 | Randomly | One Scene Per Trigger And Looped |
| 3 | All at Once | Randomly |
| 4 | | All at Once |

**PC.** Only a message equal to "Message To Play Next" does anything, and it plays one step. "Initial State" On plays one
step on the first tick after load; the player then leaves the update list. A step:

* 0 In Order Once: stops this player's previous scene if it still runs, plays the next one; after the last one the player
  flags itself for deletion (later messages are ignored).
* 1 In Order Loop: plays the next one and wraps to the first. Every trigger plays a scene, for good; nothing plays by itself.
* 2 Randomly: a random scene per trigger.
* 3 All at Once: every scene of the list together, then the player deletes itself.

**PS2.** Messages map to engine codes through the table filled at `0x2cd648` (Off 0x1002, On 0x1001, Blow Up 0x10a3, Load
0x1174, Play Sequence 0x10a1 ...). The player handles four of them itself: Off takes it off the update list, On puts it on,
Blow Up deletes it (the deferred delete `0x1413c0`) and Load preloads its scenes. Only other messages are compared with
"Message To Play Next". That message plays a step at once and then puts the player on the update list, except for order 2.
On the update list a player steps by itself whenever its current scene has finished:

* 0 In Order Once: the trigger plays the first scene, the rest follow back to back, the player deletes itself after the last.
* 1 In Order Loop: the same, wrapping: one trigger replays the list for good.
* 2 One Scene Per Trigger And Looped: a trigger plays the next scene once the previous one has ended, and wraps. Ticks do
  nothing, so On (or Initial State On) never plays anything.
* 3 Randomly: a random scene, and the update list starts more by itself.
* 4 All at Once: every scene together, then it deletes itself.

Initial State On puts the player on the update list at load (and preloads the first scene for orders 0 to 2).

So PC 1 "In Order Loop" (next scene on each trigger) is PS2 2, not PS2 1. The converter copied the number, and PS2 1 replays
the list by itself: Volcanic "Arrow No Flame" every 5 s with the camera held. PC 3 "All at Once" landed on PS2 3 "Randomly".

## Mapping (formats `ootfstomp.ts`, `ps2PlayOrder`)

| PC | PS2 | Exact? |
| --- | --- | --- |
| 0, one scene | 0 | yes |
| 0, several scenes | 2 | one scene per trigger as on the PC; differences: a trigger while a scene runs is ignored (the PC stops the running one) and the list wraps after the last (the PC player is gone). Kept 0 with a warning when the player starts at load or by Off / On / Blow Up / Load, since PS2 2 would never play |
| 1 | 2 | yes for the per-trigger behaviour; same fallback to 0 (with a warning) when started at load or by one of the four messages |
| 2 | 3 | the PS2 player also starts scenes by itself once triggered (no PC level uses 2) |
| 3 | 4 | yes |

The converter now also writes the effective PC "Initial State" and "Message To Play Next" (object value, else the PC class
default) and, for a player that relies on its PC class's default "Sequence List", that list converted. The PS2 template class
(LEVEL1ON 129) defaults to list `[472]` "Heuge Thieves", order 0, Triggered, message 4, so those players ran a Drakan 2 scene
(or nothing) with PS2 defaults. A class list that does not convert becomes `[0]`, which plays nothing.

## Volcanic scene 9.2

Detector 978 (next to the cage) plays 9.2 itself on the PC (Task Play Sequence) and sends its Trigger Message, Off, to the
dragon. The detector-scene relay gave its Stomp Player "Message To Play Next" = the detector's message, Off, and the PS2 player
takes Off as "stop": the scene could never start. 9.6a worked because its detector sends On, which arms the player (a one-scene
order 0 list then plays once). Now a detector whose message is Off, Blow Up or Load gets a twin (same class, box, parameters and
senders) that sends Play Sequence to the player only; the detector keeps its own links and message. In the 23 levels this
applies to Volcanic 978 (9.2) and Volcanic 1265 (9.1, initially disabled and never enabled, as on the PC). Grotto 175 ("41",
also Off) is not relayed: its class keeps a database with an SSD and plays the scene itself.

## Audit: PC Stomp Players whose PS2 behaviour changes

All 49 placed Stomp Players of the 23 levels (none in the arenas); "before" is what the previous converter produced.

| Level | Player | Scene(s) | PC | Before | Now |
| --- | --- | --- | --- | --- | --- |
| Volcanic | 2527, 2536 | Arrow No Flame | 1 | 1: replayed every 5 s for good after one press | 2: once per press |
| Volcanic | 1373 | Aroch Fly Through (class list) | 0 | PS2 default list: Heuge Thieves | the PC scene |
| Rift Crystal | 340 to 349 | Pure / Freeze / Energy / Liquid / Solid In and Out | 1 (class default) | 0: each scene once, then the player deleted itself | 2: every counter message plays it again |
| Succubus Lair | 2929 | "1" | 1 | 1: endless replay | 2 |
| Succubus Lair | 3222 | ALC1, ALC2, ALC3, away34, ALC5n, ALC6n, ALC hide (7 detectors) | 0 | 0: the first detector ran all 7 back to back | 2: one per detector |
| Succubus Lair | 3225 | 8.0b | 3 | 3 Randomly: replays by itself | 4: once |
| Alwarren | 2017 | Bowling for Crystals | 3 | 3 Randomly | 4 |
| Grimstone Mines | 1253 | Hider (class list) | 3 (class) | PS2 default list, order 0 | Hider, 4 |
| Grimstone Mines | 2770 | positioning the hider (class list, class Initial State On) | 0 | PS2 default list, never started | plays at load |
| Grimstone Mines | 433 | (class list empty) | 0 | PS2 default list | `[0]`: nothing |
| Ruined Village | 1625 | arokh awakening (class list) | 0 | PS2 default list | the PC scene |
| Ruined Village | 2465 | The Book (class list, nothing sends to it) | 0 | PS2 default list | the PC scene |
| Wartok Canyons | 3080 | quickening (class list) | 0 | PS2 default list | the PC scene |
| Intro | 2666 | Intro 2 | 1 | 1 | 2 (no PC object sends to it) |

Unchanged: every other player (PC 0 with one scene and its own list). The detector-scene players added by the relay stay 0.
The converter prints the changed players per level (`sequences <level>: Stomp Players <id> order a->b ...`).

## In game

`dist/DrakanPC/drakan.exe` (sha256 in each `report.json`), private ini / card / logs, god mode, `DRAKAN_STOMP_PROBE=1`, pack
`pack/` (formats `5745259`), direct starts.

* `vol-a` (Volcanic): the opening 9.1 plays from 11.3 s to 93.8 s. Placed north of Detector 978's box and walked in (W 3 s):
  9.2 (`0x0008005a`) starts at 135.4 s and ends at 170.8 s. Then placed by the sanctum wall button, W, E: "Arrow No Flame"
  (`0x00080059`) plays once, 211.6 to 216.7 s, and no scene runs from then to the end of the run (267 s, previously a restart
  every 5 s); D afterwards moves Rynn 5,254 units, so control is back. The screenshots of both scenes are near black (the scenes'
  own lighting and fades); the probe's sequence ticks are the evidence.
* `alw-new` / `alw-old` (Alwarren, PC 3 All at Once, Bowling for Crystals, detector 2018 sends On): placed beside the small
  box and walked through it. New pack (order 4): the scene (`0x00080008`) runs once, 59.6 to 66.6 s (it waits for its resources
  after the trigger), nothing afterwards; W and S then move Rynn. Old pack (order 3, Randomly): from 56.8 s the player restarts
  the scene on every tick until the end of the run (106 probe samples to 162 s, elapsed always about 0) and Rynn never moves
  again: W and S leave her on the spot she was placed on. This was a second soft lock of the same kind as Volcanic's.
* `grim-new` (Grimstone Mines, PC class list "positioning the hider" with class Initial State On): the scene (`0x00080037`) runs
  at load (22.2 s, under a second) and once only. With the old pack it used the PS2 class defaults (Drakan 2 list, Triggered)
  and never ran.

## Still open

* Randomly (PC 2) is unverified on PS2 and unused by the PC levels.
* The multi-scene Succubus player (3222) ignores a detector that fires while the previous scene still runs.
* The other relayed detector scenes and the class-list scenes other than those checked here were checked in the converted data
  only.
