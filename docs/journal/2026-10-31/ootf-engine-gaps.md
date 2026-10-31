# OotF gameplay-system gaps vs the PS2 engine (2026-10-31)

Scope: which OotF mechanics the recompiled PS2 engine cannot run as-is, ranked by how much of the single-player campaign
(13 SP levels: Mountain World 6, Islands World 5, Rift World 1, Volcanic World 1; the other 10 of the "23 levels" are
multiplayer maps) each blocks, and whether the fix is data, engine code or infeasible. Input: `ootf-classes.md`,
`ootf-classes-census.csv`, `ootf-data-study.md`, `level-change.md`, `world-model.md`, plus a per-level census of the 14
missing engine ids and a field-name overlap of each against every PS2 engine id (throwaway scripts in `build/ootf-classes/`:
`perlevel.ts`, `overlap.ts`, `f2.ts`, gitignored). Tags: **[data]** measured, **[inf]** inference, **[U]** untested in game.
No engine code was written in this pass (section 4).

## 1. The 14 engine class ids missing on PS2: where they occur and what to map them to

"Overlap" = shared field names / PC field count with the best PS2 engine id [data].

| Id | PC engine class | SP objects (levels) | MP objects | Best PS2 substitute (engine id) | Overlap | Verdict |
|---|---|---|---|---|---|---|
| 88 | Goblin, Goblin Grenadier | 120 (Alwarren 24, Islands 65, Islands Night 31) | 0 | **49 Orc/Troll/Slave** (melee humanoid; Approach/Defend/Running Attack radii); 41 Scavenger and 40 Spider tie at 0.41-0.42 | 104/120 | data: clone a PS2 id-49 class, overwrite by field name, keep PS2 model/anims. Behaviour [U] |
| 100 | Succubus | 18 (Islands 5, Islands Night 2, Succubus Lair 11) | 0 | **30 Dragon (creature)** (Blade Dragon "Crow": Flight/Hover/Strafe/Glide, Launch Weapon, Rider) | 70/81 | data: clone id-30 class; borrow a flying creature model |
| 101 | The Queen (boss) | 2 (Succubus Lair, Volcano) | 0 | **30 Dragon (creature)** as boss | 69/75 | data; boss scripting is level logic (detectors, counters, transitions), which maps 1:1 |
| 91 | Magic Crystal (Fire / Ice / Lightning) | 15 in 9 of 13 levels | 0 | **171 (Mana power-up item)** or **33 Key / Quest Item**; PC class is an inventory item (DisplayName, Quantity, Icon Index, Player Slot, Powerup Object, Effect, Hold Use Button To Charge) | 11/16 | data for placement and pickup; the *effect* (granting Rynn a spell) may need engine code [U]: see section 4 |
| 80 | Quickening Effect (visual) | 4 (Alwarren, Grotto, Wartok, Volcano) | 0 | **71 Power FX**, else 134 Sparkle / 20 Flame; pure particle parameters | 1/16 | data: replace by a template particle object, no gameplay lost |
| 123 | Combination Lock | 1 (Rift Crystal) | 0 | **223 Combo Lock** (PS2-only id: Message List, success/failure messages) | 0/3 by name | data: Toggle A / Toggle B / Fire Message are the same switch puzzle; map to one-entry message lists. Behaviour [U] |
| 137 | Class Messenger | 1 (Rift Crystal) | 0 | **224 Message Relay** (Incoming/Outgoing Message) | 0/2 | data: `Receiver Class` has no counterpart; resolve to concrete receiver objects at convert time and link directly |
| 130 | Pirate Ship (spline vehicle) | 3 (Islands Night) | 0 | **35 Mover / Elevator Platform** or 16 Rigid Body on a Waypoint chain | 0/6 | data, needs a ship model; cosmetic in 1 level |
| 18 | Rynn Launch Pad | 0 | 77 (8 maps) | no PS2 class. Arena spawn points are host data (`drakan_arena`, netcode.md "Spawn points") | n/a | data: export pads as `mp_maps.ini` spawn points |
| 127 | DragonLaunch Pad | 0 | 22 | same, with a dragon flag | n/a | data (needs a dragon-spawn flag in the arena config) |
| 128 | Red Dragon Launch Pad | 0 | 3 | same (Master of the Dragon only) | n/a | data + engine (MotD mode) |
| 86 | Fire Dragon Soul Crystal | 0 | 3 | no PS2 object; MotD is a mode rule (first to touch bonds to the unclaimed dragon) | n/a | engine code in the MP layer (netcode.md: Dragon's Duel / Master of the Dragon not implemented) |
| 120 | Multiplayer Power-up (weapons, armor, life crystals, QuickSpeed, DragonStrike) | 0 | 151 | **138 Health Power-up / 57 Dragon Power-up / 10 Health Potion** (8/10 fields match 138) | 8/10 | data: arena pickup tables |
| 126 | MP Dragon Invulnerability | 0 | 3 | **138 / 57** power-up with a timer | 9/14 | data |

These 14 ids are 1107 of 16312 objects (7 percent); **only 160 are in the SP campaign**. They are not the main blocker.

## 2. OotF mechanics

| Mechanic | PS2 equivalent | Verdict |
|---|---|---|
| Level types | PC `Level Type`: 0 air, 1 ground, 3 Master of the Dragon, 2 in 4 SP levels (meaning unknown). PS2 Level Properties has `Level Type` = 2 on every level. PS2 engine 54 field list is a superset except Fog Color/Start, Initial Music ID, Alpha..Delta Color | data: keep the template's Level Type; copy the rest per `LEVEL_PROPERTY_RULES`. Type 3 is an MP rule only |
| Dragon flying levels | PS2 Level Properties has `Flying AI Flight Ceiling`, `Flight Floor`, `Dragon Kill Altitude`; engine 28 (player Dragon) field list is a superset of the PC one (0 PC-only fields) | data: copy ceiling / kill altitude; flight code is native |
| Rynn (engine 9) | PS2 field list is not a superset: **48 PC-only animation fields** (Fire/Ice/Lightning/Death SA, Special Attack Thrust/Left/Right/Spin/Swing180/Jump, Archer Medium/Low, Block, Sheathe, Roll L/R Attack, Stairs, 180 Turn, Slow Walk) against about 150 PS2-only (hang, ledge hoist, cartwheels, one/two-hand/hammer sets) | data: keep the PS2 Rynn class unchanged, never clone the PC one. PC special attacks have no PS2 animation sets; PS2 combos replace them |
| Rynn Quick Inventory | Native on PS2 (R1 `0x403a`, bar list at `ctl+0x214`, keys 1..5 bound, `drakan_quick_inventory.h`) | none needed |
| Items, armor (45), melee (14), bow (79), potions (10, 89) | Same engine ids; item field lists overlap 11/14 to 29/31 | data (need models/icons: ootfmodel/ootftex). Armor tiers and weapon stats differ (tuning) [inf] |
| Spells (Fire / Ice / Lightning crystals, Rynn SA anims) | PS2 spells come from dragon power-ups (57: Weapon, Alt. Weapon, HUD Bitmap Index) and mana (171). No Rynn crystal-spell item | engine-or-data: see section 4; fallback is to give the crystals as quest items (progression works if level logic only counts them) |
| Keys / doors / switches / mechanisms | Door 19 (16/16 fields), Key 33, Key Lock 34, Switch 13 (11/11), Mover 35, Pressure Plate 29, Break-away 44, Energy Door 74, Rope 73, Ladder 15, Combo Lock 223, Elevator 225 | data; blocked only by **models** (1321 of 1330 mechanism objects "needs-model") |
| Ladders | id 15 exists; 14 PC objects | data (needs model) |
| Water | PS2 water = Level Properties `Use Water Level` / `Water Level` + 16 water colours + layer water cells. PC has no water-height field [U] | data: derive Water Level from PC water layers; open risk for swimming levels |
| Rift crystals / Rift Crystal level | Detectors, counters, timers (same ids) + 123 + 137 | data (123 -> 223, 137 -> direct links) |
| Level transition semantics | Both use engine 55 with a 4-field list. PS2 arrival rule: a transition object of the *destination* whose destination text equals the old level name, otherwise the player keeps her position and falls out (level-change.md section 1) | data: the converter must emit arrival transitions with the right names |
| Persistent state between levels | PS2 writes `<slot>/<level>.rlt` + `.rsg` inside the native transition; inventory persists that way | none in engine; works for converted levels because the logic is the engine's own [U] |
| Save format | PC `.sav` vs PS2 card `.rsg`; a converted campaign saves in the PS2 format | infeasible to share, not needed |
| Master of the Dragon, launch pads, Soul Crystal | MP-only, Level Type 3 | engine code in the MP layer, outside the SP path |
| Succubus / Queen bosses | PS2 bosses are plain creatures plus level logic [inf] | data (mapping in section 1) |

## 3. Ranking by SP campaign blocked

| Rank | Gap | SP objects blocked | SP levels | Fix |
|---|---|---|---|---|
| 1 | **No converted models/skeletons/animations for scenery, mechanisms, creatures, fx** ("needs-model": 8445 of 9016 unsupported objects) | about 8400 | all 13 | data (ootfmodel, ootfanim, ootftex exist, not wired into the level path). Two orders of magnitude above everything else |
| 2 | PC -> PS2 terrain texture mapping (single fallback texture) | all terrain | all 13 | data |
| 3 | Missing singleton Level Properties object, Rynn start, arrival transitions | level boots / returns | all 13 | data |
| 4 | Goblin 88 | 120 (main enemy of Islands World) | 3 | data: id-49 clone [U] |
| 5 | Magic Crystal 91 progression items | 15 | 9 | data for pickup, spell grant maybe engine |
| 6 | Succubus 100, Queen 101 | 20 | 3 | data: id-30 clone [U] |
| 7 | Combination Lock 123, Class Messenger 137 | 2, but gate Rift Crystal | 1 | data |
| 8 | Quickening Effect 80, Pirate Ship 130 | 7 | 5 | data (cosmetic) |
| 9 | Music id -> Initial Music File; PC-only fog/colour fields | 0 objects | all | data |
| 10 | MP: launch pads, power-ups, Soul Crystal, Master of the Dragon | 0 | 10 MP maps | data (spawns, pickups) + engine (MotD) |

## 4. Engine code: what was and was not needed

* **No engine change is justified yet.** Every SP blocker is either a missing converted asset or a class clone the converter can
  emit. The PS2 engine already carries Quick Inventory, flight, level transitions with persistent state, combo lock, message
  relay and elevator handler. Hooks written before the clones are tested in game would be guesswork, and the runtime only meets
  these classes once the converted levels have models (rank 1).
* First place engine code may be needed: Magic Crystal grants a Rynn spell. Test: ship a Fire Crystal clone of a 171/33 item
  with `Effect` mapped and see whether pickup does anything. If not, add a hook in `drakan_overrides.cpp` that on pickup of
  such an item posts the same power-up message the PS2 fireball item (class 57 `Weapon` field) uses. Not written: there is no
  verified test bed and the game was not launched in this pass.
* MP rule modes (Master of the Dragon, Dragon's Duel) belong to the netcode layer (`drakan_arena`, `MatchServer`).

## 5. Mapping for the `ootfclass.ts` consumer

Add an *engine id substitution* step to `planClassMapping`, applied when the PC id has no PS2 class; the clone keeps the PS2 id,
overwrites defaults by field name and base type and keeps PS2 resources:

```
substituteEngineId = { 88: 49, 100: 30, 101: 30, 91: 171, 80: 71, 123: 223, 137: 224, 130: 35, 120: 138, 126: 138 }
// 18, 127, 128, 86 are not classes on PS2: convert to arena spawn data (configs/mp_maps.ini), emit no object.
// 137: drop Receiver Class and wire the receivers by link.
```

The 50 percent coverage rule passes for 88 (87 percent), 100 (86), 101 (92) and 91 (69); 80, 123, 137 and 130 fail it and need an
explicit field-rename map and `minCoverage: 0` (123: `Toggle 'A' Message` -> `Message List[0]`, `Toggle 'B' Message` ->
`Message List[1]`, `Fire Message` -> `Message to Send on Success`). Put 88/100/101 under `borrowModelKinds` (creature) so a
PS2 creature model is borrowed.

## 6. Open questions

* Whether the engine accepts creature clones with PC defaults (hearing, flee health, damage) without balance problems [U].
* Water height for converted levels [U].
* Meaning of `Level Type` 2 (unchanged from ootf-data-study.md).
* Only the multiplayer sections of the OotF README were consulted; `Dragon.rrc` (3.7 MB resource archive) was not opened.
