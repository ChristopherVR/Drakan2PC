# OotF puzzles and the ending: links are positions, Dr. Evil, plates, locks, quest markers (2026-11-01)

Follows [ootf-campaign-walkthrough.md](ootf-campaign-walkthrough.md) (breaks 8 and 9) and [ootf-dropped-classes.md](ootf-dropped-classes.md).
Code: `editor/packages/formats/src/pc.ts` (link fix), `ootfclass.ts` (`PS2_SUBSTITUTES`, `CLONE_ADJUST`), new `ootfpuzzle.ts`
(plates, boss, messengers, quest markers, test aids), stage `puzzles` in `ootfpipeline.ts`. Probe scripts: `build/ootf-end/` (private).

## 1. Finding: a PC object link is the target's position in the object table, not its id

Rift World has 149 objects with ids up to 565. Read as ids, 17 of its 78 links hit an object; the Dr. Evil heads linked to "object 25",
the DeathCounter to "object 71", and so on. Read as 0-based table positions all 78 hit, and every one lands where the level design needs it:

| object | link (position) | target |
| --- | --- | --- |
| Dr. Evil x4 (ids 108..111) | 25 | #162 Navros DeathCounter |
| #162 Navros DeathCounter | 71 | #472 Ribs (Timer, 5 s, Trigger Message 4 = play sequence) |
| #472 | 72 | #473 Stomp Player, sequence 115 "end sequence" |
| gate timers #447..#450 | 49, 50, 53, 54 ... | the "vertical gate inside / outside" next to them |
| Ebon Dragons x4 | 120 / 142 (here: the death counter #558) | counter 558, which opens the RibDoors |

The same holds for every level (Alwarren 487/487, Ruined Village 749/749, Volcano 695/695 links hit by position; mean distance between linker
and target 13000-27000 units by position against 110000-220000 by id). Until now the converter read them as ids, so **every converted link of
every level pointed at a wrong object or was dropped** (only links where position and id happened to agree, mostly in small levels, worked).
`parsePcObjectList` now translates positions to ids (`pc.ts`), so all consumers (conversion, music triggers, campaign exits, the editor) see ids.
Out-of-range links are dropped with a warning. This is the biggest fix of this pass: detectors, counters, timers, doors, spawners and sequences
in all 13 levels now reach the objects they were built for. (Duplicate ids, e.g. Alwarren id 77, no longer matter for links.)

## 2. How the PC game ends

Chain, now reconstructed from the correct links and the sequences of `RiftWorld.ssd`:

1. Four Ebon Dragons (counter #558, Decrement message 8 = death) open the two RibDoors (detectors 367 / 369).
2. Delon (Death Mage #161) dies: counter #455 (init 1, decrement 8) plays Stomp Player #436 = sequence 280 "10.2a" (54 s). Its last action sends
   ON to Timer #474 (Ribs), which wakes the four Dr. Evil heads and the altar pillars ("Emerge only when triggered?").
3. Each Dr. Evil head dying counts down #162 (init 4). At zero: ON to the Ribs timer #472, 5 s later Stomp Player #473 plays sequence 115
   "end sequence" (32 s: Rynn, Arokh, camera, mirror, faders).
4. The last action of sequence 115 (t = 31.99 s) sends ON to actor "Guithing" = object #560, a **GUI Changer with `Launch What?` = 2**.
   The PS2 engine's field name table says: `Main Menu,Map Screen,Roll Credits` (string at 0x53f078), so the PC ending is the **credits roll**.
   PS2 has the same class (141) and the credits screen (GUI screen 0x13 from the title menu); LEVEL8OS ends with a "User Interface Launcher"
   (value 0, main menu).

So the ending needed nothing invented except the missing boss: the converter already kept the counter, the timers, the Stomp Players, the sequence
(converted into LEVEL1ON.SSD) and the GUI Changer; it dropped the four Dr. Evil objects, so the counter could never reach zero.

## 3. Dr. Evil stand-in

`PS2_SUBSTITUTES[92] = 59` (forced: the PS2 has an unrelated engine class 92, "Repo ..." creature movement presets). The clone is cloned from the
**Primitive Giant** (largest humanoid with an NPCS.ODB model; the PC model is a hydra head and would need a retarget that has no giant
rig). It takes the PC stats by field name (Initial Health 250) and `CLONE_ADJUST[92]` forces `Wait For Trigger = 1`. `ootfpuzzle.ts`
(`bossHeads`): each head is put on the altar floor under its PC position (the PC heads rise from the rift pit 40000 units lower), scaled 1.6, and the
counter(s) the heads link to get `Decrement (-1) Message = 8` (the PS2 creature's death message; the PC engine class sends ON).

## 4. Pressure plates

42 plates in 6 levels (Ruined Village 3, Wartok 1, Grotto 1, Islands 7, Rift Crystal 30; the older "8 objects" figure counted only the
unconverted ones). The PS2 plate classes are in LEVEL4ON / LEVEL8OS, which no template lists, so the plate stays a static prop
(Ground Object clone, PC model) and `ootfpuzzle.ts` (`platesToDetectors`) adds a **Trigger Detector** box over it (4096 x 3072 x 4096, centre 1000 above
the plate): `Task` 6 (trigger only), `Trigger Message` = the plate's "Message To Send At Bottom", `Detect Only Once?` = "Stay Down When
Pressed?". The detector carries the plate's links; links between plates and links from other objects to a plate also reach the detector.
"Send Message When" never / reached-top plates get none. Not modelled: `Initial State`, `Triggered by Rynn Only?` (any foot / mount),
release messages (a plate stays "down" for a detector that fires once and re-fires on every entry otherwise).

## 5. Combination Lock, Class Messenger

`123 -> 224` and `137 -> 224` (Message Relay, LEVEL1ON). The Rift Crystal lock links six counters (initial 1..6, decrement ON) and a 2 s reset timer, and
the two PuzzleSwitches send ON to it: it is a relay that forwards every switch press to all counters, which stage the lightning show
("Recover Rift Crystal"); `CLONE_ADJUST[123]` = Incoming 1 / Outgoing 1. The Class Messenger relays its PC "Message to send"; its receiver class
(3235 "Tornado Room Tornado") has no placed objects in the level, so it stays unlinked (warning).

## 6. Quest markers (class 104)

Map Object / Map Trigger are pure journal and map entries (mission text bitmaps on the PC): nothing links *from* them, so no gate depends on them
(the earlier note was wrong). Equivalent: the PS2 quest class of System.ODB (same engine id 104: Type, SubQuest?, Journal Entry, Map Text, Map X/Y).
`CLONE_ADJUST[104].reset` folds all PC classes into one record (template "Farmers Quest"); the per-object values are object parameters
(`Journal Entry` / `Map Text` = XDB id of the object's name via `TextPlan`, new strings in SYSTEM.XDB; Map X/Y = PC coordinates x 0.4). Quests announce
themselves (PC `New Quest Sound` != 0) and become journal entries (Type 1), icons only label the map (Type 0). Generic "Map Trigger" objects (no
text, 17 of them) are left out. The links to them (detector, item pickup, Queen death counter) now fire the PS2 quest start as they did on the PC.

## 7. Keys and pickups

All item classes convert (cloned, PC model): Rune Cage Key, Mystic Key, Priests Key, the Elemental Amulet, Bell Hammer, Rift Crystal, Dragon Armor
Key ... and the key locks (engine 34) carry the PC `Lock Code`, which the PS2 key lock compares by name. Not verified by picking one up in game.

## Results

Harness: `build/ootf-end/run.sh` (own lock handling), `OOTF_TEST_ENDING=<s>` (four timers send the death message to the boss counter after `s`
seconds; `OOTF_TEST_DIRECT=1|2` skips the counter / counter and end timer), `OOTF_TEST_WAKE=<s>` (ON to the object that wakes the heads),
`OOTF_NO_PUZZLE=1` (stage off).

* **Ending, in game (run `e2`, Rift World, `OOTF_TEST_ENDING=3`)**: timers stand in for the four head deaths. 41 s after control: the counter
  reached zero, the 5 s timer ran, the end sequence played (fade, cinematic, fade to white) and the log shows `gui screen -1 -> 19`
  (0x13 = the credits), the screenshots show the credits roll over the title backdrop. So counter -> timer -> Stomp Player -> sequence ->
  GUI Changer (Roll Credits) works on the PS2 engine with no runtime change. `e1` (first attempt) had a converter bug in the test aid itself (float
  passed as raw bits): nothing fired, nothing else broke.
* **Dr. Evil stand-ins**: the level loads with the four scaled Giants; with `OOTF_TEST_WAKE=8` and Rynn put on the altar (`w1`) large creatures
  appear around her after the wake message. Not played through: killing four 250 HP giants and Delon (the PC order: Delon, then the heads).
* **Link fix + all changes, level loads**: every single-player level except the Book loads and reaches control with the new conversion (runs
  `a1-*`: Ruined Village, Wartok Canyons, Grimstone Mines, Grotto, Islands, Alwarren, Rift Crystal, Islands Night, Succubus Lair, Volcanic, Rift
  World, Intro: all PASS, no crash / missing-target lines). The Book was not re-run (known hang, unrelated).
* **Plates**: 42 plates convert to 42 detectors and the levels load; the plate-to-gate chain was *not* proven in game. The detector is the same
  class and message path as the exit detectors, which are proven (`DRAKAN_PLAYTEST_POS` into the box fires them), but the two Wartok runs
  (`p1`, `p2`: Rynn placed on the plate of the Secret Entrance mover) did not show the movers (p1: Rynn died of a fall off the cliff the plate is on).
* **Quest markers / combination lock / messenger**: convert and load (Rift Crystal, Islands, Volcanic, ...). The journal screen was not opened.

## What is still open

* Plate semantics not modelled: initial down state, release message, Rynn-only (see section 4). The Islands 2x2 plate puzzle that enables three plate
  gates is flattened: all plates are live from the start (the gates open more easily than on the PC, the level stays completable).
* The Dr. Evil model is a giant, not the hydra head; the fight is four giants (250 HP each, scale 1.6) after Delon, not tested to the end.
* Class Messenger of Rift Crystal has no receiver objects (its target class has no placed instances); a relay with no links.
* Generic "Map Trigger" objects (17) have no text and are dropped; Fog / Sky changers, Quickening Effect, Fall01 / Black Box / Splash 03 remain dropped.
