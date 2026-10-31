# OotF classes, logic and Level Properties on PS2 (2026-10-31)

Scope: re-targeting PC (Order of the Flame) classes onto PS2 engine classes in the PC -> PS2 level converter.
Code: `editor/packages/formats/src/ootfclass.ts` (+ `test/ootfclass.test.ts`, synthetic; not yet exported from `index.ts`).
Census table: `ootf-classes-census.csv` (one row per engine class id). Throwaway scripts lived in `build/ootf-classes/`
(gitignored). Tags: **[data]** measured on the user's PC install and the extracted PAL ODBs, **[inf]** inference, **[U]** untested in game.

## 1. Census (23 PC levels, 16312 objects, 1983 distinct classes, 80 engine class ids)

* **Same engine class id on PS2: 66 of 80 ids, 15889 of 16312 objects [data].** Missing: multiplayer spawns and power-ups
  (18, 86, 120, 126-128), Goblin 88, Succubus 100, Queen 101, Quickening 80, magic crystals 91, combination lock 123, class
  messenger 137. (PS2 has ids the PC lacks, e.g. 223-225 combo lock / message relay / elevator handler.)
* **Every PS2 engine class has exactly one field list [data]** (171 ids, all 6520 classes), so a class of engine id N can be
  cloned from any PS2 class of id N. PC field lists of the same id are mostly name-identical subsets: Detector 12/12, Timer 7/7,
  Count Down 6/6, Stomp Player 4/4, Level Transition 4/4, Fader 5/5, Visibility Toggler 3/3, Waypoint 17/17, Static Light 5/5,
  Looping Sound 5/5, Door 16/16. Divergent: Music Trigger 1/7, Ambient Transition 0/3, Sky/Fog Changer 0/2, Map Object 3/9, Flame (20) 0/8.
* Old name-only matcher against LEVEL1NA's tables: 5270 objects (32%). `planClassMapping` (same name **and** engine id, else clone
  by engine id for logic/light/sound/waypoint/spawn): 4803 mapped + 2493 synthesized = **7296 (45%)**, 9016 unsupported. The name
  matcher also maps 467 objects whose same-named PS2 class has a *different* engine id (collisions; the planner refuses them).
  LEVEL4LD as template gives the same picture (baseline 5247).

| kind | PC classes | objects | name-mapped | synthesized | unsupported |
|---|---|---|---|---|---|
| scenery (17, 15, 16, 99, 130) | 895 | 5964 | 284 | 0 | 5680 |
| logic (trigger, timer, counter, stomp, transition, fader, vis toggler, current ...) | 275 | 2155 | 1186 | 882 | 87 |
| waypoint | 47 | 1505 | 501 | 1004 | 0 |
| sound (looping, music, EAX, ambient transition) | 104 | 1482 | 1259 | 202 | 21 |
| mechanism (door, switch, lift, plate, lock) | 356 | 1330 | 9 | 0 | 1321 |
| light | 13 | 1278 | 1180 | 98 | 0 |
| creature | 45 | 807 | 191 | 0 | 616 |
| fx | 43 | 742 | 3 | 0 | 739 |
| spawn (generators) | 58 | 307 | 0 | 307 | 0 |
| mp (launch pads, MP power-ups) | 26 | 259 | 0 | 0 | 259 |
| item / weapon | 92 | 395 | 131 | 0 | 264 |
| system / player | 29 | 88 | 59 | 0 | 29 |

Unsupported reasons (objects): needs a model 8445, no engine class 465, low field coverage 106. What stays lost is
**scenery, mechanisms, creatures: they need converted models** (borrowing a PS2 template model is possible per kind with
`borrowModelKinds`, off by default).

## 2. Synthesized class records [layout data, game behaviour U]

`planClassMapping(pcClasses, ps2Odbs, opts) -> {mapped, synthesized, unsupported, resolve(db, id)}`.

1. Same name and same engine id as a PS2 class of the template's databases: `mapped` (existing class).
2. Else, for kinds logic/light/sound/waypoint/spawn (or kinds in `borrowModelKinds`): clone the PS2 class of that engine id whose
   defaults are closest, overwrite defaults by **field name and base type** (int, float, enum, colour, string, layer and arrays of
   those). Resource references (class, model, sound, animation, sequence, texture) keep the template's value; class refs can go
   through `mapClassRef`. Needs >= 50% field coverage. Identical results share one record.
3. Else `unsupported` with a reason.

Record layout: the clone keeps the template head byte for byte (group, model ref and 74-byte model block, rfl, icon, field count)
and replaces name and data area (`cloneClassRecord`). **Round trip: all 6520 PAL class records re-serialise identically.**
`buildPlainClassRecord` (from nothing) matches 1734 of 1752 model-less records; the other 18 (`Bunker Piece ...`) have non-zero
bytes in the "10 zero bytes" block, so that rule in level-format.md section 12 is "usually zero" (clones are unaffected). The
model block varies at body offsets 6, 10-12, 16-47, 60-63, 76-79 and looks like the model's bounds/transform floats (detector
cube -0.5..0.5); a clone that swaps the model must patch it **[U]**. Clones go into the **template's own ODB**, so model and
class references (relative to that ODB's `.DB`) stay valid.

Shipping (`buildClassEntries`, same shape as editor-core `StoryEntryChange`, so `buildStoryPackFiles` ships it):
`new/<FILE>.ODB/0102_<id>_<group>.bin` per class, plus the `0103` entry of that ODB replaced by the stored one with the new field
lists appended **in ascending id order**. Why: `ModPack.cpp` adds `new/` entries sorted by (file, type, id, sub) and appends them
at the end of the container index; 0x0103 lists pair with records by index order. Ids are free ids inside the ODB's existing id
range, highest first (modding.md: an id above the file's maximum did not spawn). So **adding classes to an existing ODB through
the pack is possible; no whole new ODB file is needed.** A new ODB file would also need a `.DB`, a dependency entry, and the
id-range rule is unknown for it, so it was not designed. LEVEL1NA plan: 205 new records in 7 ODBs; the largest resulting 0x0103
is 40 KB (limit 0x40800). Unverified in game: 205 appended records at once (only single quest classes were tested) and growth
of the guest's 0x0103 buffer (modding.md: "guest buffer sizes unverified"). Plan all levels of a pack together so ids are stable.

For the converter owner: `ConvertOptions.classOverrides` is keyed by lower-case class name, which is ambiguous (four different
`timer` classes with different defaults); use `plan.resolve(db, id)` instead. Converted levels also lack three singleton system
objects every PS2 level carries (Rynn 9, Camera 27, Level Properties 54; PC places Rynn and Camera but never Level Properties).

## 3. Messages and enums

* Class/object enum values are the **same ordinals** on both platforms [data]: of 264 enum fields common to both (same engine id
  and name) 211 have the same most common default and 239 have the PC default among the PS2 values. The 28 message fields agree:
  creature `Message To Send` 8 (I'm Dead) on both, Stomp Player `Message To Play Next` 4, Detector/Timer `Trigger Message` 1 (On),
  switches 1/0. Differences are tuning, not renumbering: Timer/Counter `Reset Message` default 5 PC vs 6 PS2 (a "never used"
  slot), Plate `Send Message When`, Detector `Detect Only Once?` 1 vs 0. Links therefore keep their message fields unchanged
  (OpenDrakan's 0-9 list is a prefix of the PS2 0-32 table, level-format.md 15.1).
* 4097 On, 4098 Off, 4353 Lock, 4354 Unlock, 4257 Play Sequence, 4259 Blow Up, 4103 Triggered are **sequence (STOMP) message action
  numbers** (SSD type 7), a separate numbering from the enum. PS2 sequences use the same family (4097 x256, 4259, 4098, 4257 on
  disc). The class mapping does not touch them. PC SSD contents were not compared here: [inf].
* Not equivalent: other enums the PS2 engine extended or retuned (`Level Type`, `Seek Type`, `Follow Mode`, `Open Direction`; 53 enum
  fields differ in default, see the DIFF lines of the analysis). Units: lu = 2048 world units, wu = world units, in both
  engines for fields with the same name.
* Class, sound, model, animation, sequence, texture fields hold `(db << 16) | id` of PC databases and mean nothing on PS2.

## 4. Level Properties

PC never places a Level Properties object: the engine reads the **defaults of the first engine-54 class of the level's own ODB**
(OpenDrakan `findFirstClassOfType`). Verified: 0 placed objects of engine id 54 in 23 levels, exactly one such class per level
ODB. PS2 places one object per level (Surdana: id 13284) that overrides a few of the 134 defaults (Surdana: only `Starting
Fader`). PS2-only: 64 `Vertex Color` slots, day/night cycle refs, 16 water colours, cache sizes, water level, underwater fog,
flier limits, dragon kill altitude, treasure tables. Mapping in code: `LEVEL_PROPERTY_RULES`, and `levelPropertiesParams(pcClass,
ps2Class) -> {specs: ParamSpec[], left}` for `setObjectParams`.

| PC field | PS2 field | action |
|---|---|---|
| Min / Max Fog Depth (lu) | same | copy (PC 12/30 vs PS2 8-90; consider max with the template) |
| Default Field of View (deg) | same | copy (80) |
| Gravity (wu/s^2) | same | copy (6000; 4096 in 4 PC levels) |
| Flight Ceiling (wu) | same | copy (PC 45000-300000) |
| Slow / Med / Fast Current Speed (lu/s) | same | copy |
| Saveable? | same | copy (0 in intro, book, all MP maps) |
| Recharge Rate Multiplier | same | copy (0.25 in Master of the Dragon) |
| Kill Altitude (wu) | same | copy (3 MP maps set 98000-210000, just under the 200000 layer base; 0 = off) |
| Initial Music ID (int 0, 1, 6-10) | Initial Music File (sound ref) | hook: needs a PC music id -> PS2 music map |
| Level Type (0/2 SP, 1 ground, 3 Master of the Dragon) | Level Type (2 on every PS2 level) | keep template |
| Default Material Properties | same | keep template (class ref) |
| Fog Color, Fog Start Ratio | none (fog colour lives in the layers; PS2 has only an underwater ratio) | drop |
| Alpha / Beta / Gamma / Delta Color | none | drop (role unknown) |

PC has no level water-height field, so water must come from elsewhere [U]. The PS2 `Level Type` meaning is unknown.

## 5. Verified vs guessed

Verified on data: engine id equality, one field list per id, clone round trip (6520/6520), synthesized records re-parse
(205/205), enum ordinal agreement, plan numbers, Level Properties not placed on PC. Guessed / untested in game: that the engine
accepts many appended records and a larger 0x0103; that a clone with PC defaults behaves (same by field names and defaults,
not by behaviour); fog, ceiling and kill-altitude values suiting PS2 rendering; model-block patching; music mapping; whether a
missing Level Properties object breaks a converted level.
