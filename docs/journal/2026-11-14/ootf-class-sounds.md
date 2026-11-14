# Order of the Flame: class-default sounds of converted classes (14 November)

Follow-up of [the sound survey](ootf-sound-subtitles.md) section 2, item 1. Scratch, packs, runs, logs and frames:
`build/review/ootf-class-sounds/` (nothing from either game is quoted here beyond class, field and clip names).

## 1. What was wrong

A PC class names its own sounds in fields of type 5 (`db << 16 | id`, or a list of them): death and hit voices, I see you,
attack and taunt lists, door start / loop / stop / bang, weapon equip / pickup / drop / swoosh, ambient "Sounds" lists,
quest map chime, rope swing. The converter's sounds stage converted only the sounds that **object parameters** and
**scenes** name, and `mergeDefaults` (`ootfclass.ts`) carries only plain values. So a clone kept its Drakan 2 template's
sound fields: either a Drakan 2 sound of an unrelated class (the Goblin, cloned from "Stomp Sand Dragon", played no
goblin voice; a projectile impact of the goblins played a Drakan 2 host sound), or a ref that names nothing in the
clone's database.

The torches were a second case: a PC "Torch Flame" has no sound field. It names a sound effect class ("Sound Effect
Object", engine class 6 `Torch_1`, a `Sounds` list with `NewTorch_1_11k`, played every 6 +- 1.5 s). The PS2 flame
(RESOURES "Wall Torch", the template of all 487 converted flames) has a "Looping Sound" field instead, which the clone
kept as the template's value. In the game every torch asked for sound 0.

## 2. The change (formats `ootfclasssounds.ts`, the sounds stage of `ootfpipeline.ts`)

* **Collect:** for every synthesized class (each PC class folded into it), its PC sound fields are matched **by name** to
  the template's sound fields (`matchClassSounds`). Their clips join the sounds the stage converts. Only fields the PS2
  class has are written; template sound fields without a PC counterpart keep the template's value, as the other PC
  defaults do.
* **Write:** after the conversion, each matched field gets the converted ids in the clone's database frame (`hostDbOf`, as
  object parameters): a scalar in place, a list appended to the data area (`writeClassSounds`, `setClassArray`).
* **A PC field without a sound clears the template's** (the PC class plays nothing there): 188 fields, mostly door start
  and close sounds, Rynn's "It's locked" list on doors, potions and weapons without a drop sound.
* **Sound effect classes:** a PC class ref "Sound Effect Object" whose class (engine 6) has `Sounds` fills the template's
  "Looping Sound" (`PC_SOUND_OBJECT_FIELDS`, `matchSoundObjects`): the torches, sconces, hanging and standing flames.
* **Loop flag:** a clip that only a looping field plays gets the descriptor loop flag (0xa), because the PS2 engine plays a
  clip without it once. A clip that any object, scene or other class field also plays keeps its own flags. Sound effect
  classes that no level object places (the torches' `Torch_1`, `Torch_2`, `SconceTwin_A` ...) do not count, since nothing
  spawns them. 15 clips (the three NewTorch clips among them).
* **Switch:** `classSounds: false` (CLI `--class-sounds 0`) keeps every clone's template sounds as before. The diagnostic
  log `DRAKAN_DBG_CLASSSOUNDS=1` lists every written field, every unmatched PC field, every template sound field kept, and
  the name-mapped classes that keep their own.
* Tests: `test/ootfclasssounds.test.ts` (scalar, list, clearing, sound effect class, record round trip).

Convert log of the full campaign pack: `class sounds: 989 field(s) of 470 converted classes (188 cleared: the PC class plays
none there), 4 field(s) differ between folded PC classes (first kept)`. The four: Invulnerability Potion (Chrome), Potion of
Speed, Ring of Invisibility (Activate Sound) and Cable E (Swing Sounds): PC classes with equal plain defaults fold into one
record; the first one's sounds are kept. PC fields with no PS2 field of that name: 16 "Health Potion (Red)" clones'
"Activation Sound", 2 spears' "Hit Ground Sound", and the "Primitive Giant" clone's Navaros sounds (Bite, Light Damage
Reaction, Vulnerable, Death, Ambient).

### Decisions per family

| Family | Decision | Why |
| --- | --- | --- |
| Synthesized creatures with OotF models (Goblins, Goblin Grenadier, Tick, Succubus, Crimson and Ebon Knights, Death Mage, dragons, Navy Hueges) | PC voices | Their templates are unrelated Drakan 2 creatures (Goblin from "Stomp Sand Dragon", Succubus from "Gryphon"). |
| Enemies shown with a Drakan 2 model and its clips (Wartoks, orcs, Spider, Scavenger: `modelSwap.enemy(..) === 'drakan2'`) | Drakan 2 voices kept | The Drakan 2 clips voice the creature through their animation events (survey item 2); PC death or hit lines on top would give one creature two voices. All of them are name-mapped classes in this pack anyway. |
| Name-mapped classes (Rynn, Arokh, Primitive Giant, Boulder, Health / Invisibility potions, D2-named ambient objects such as Waterfall_Med1, Gulls_1, Deep Cave) | Drakan 2 record kept | A mapped class is Drakan 2's own record. Packs are installed for the whole session, so changing it would change Drakan 2 (`reference/modding.md`, "Keep a campaign self-contained"). Arokh's remarks (Separated, Meet At Cave, Killed Enemy, Call Response, Get Out of My Way, Crash) and Rynn's Dragon Call, Can't Reach, HitRxn lists therefore stay Drakan 2's. Converting them needs a clone of the player and the player's dragon: an owner's decision with risk to every scene that names them. |
| Weapons (also those shown with Drakan 2 models) | PC equip, drop, swoosh; Drakan 2 pickup kept | The class data stays the PC weapon's (`reference/modding.md`); the sounds are part of it. |
| Torches and flames | PC crackle, looped | See above. The flames agent's Drakan 2 fire generator adds no sound of its own in these runs. |
| Doors, gates, movers, traps, ropes, map markers, power-ups | PC sounds, cleared where the PC has none | OotF data is the reference. |

## 3. Evidence (logs only; I cannot listen)

Play folder `build/review/ootf-class-sounds/play` (copy of `baseline-play`, the owner's `drakan.ini`, dist exe of 15:30,
private ELF), New Game of the Order of the Flame campaign (Ruined Village) or a direct start (Islands), god mode,
`DRAKAN_SND_DBG=1`, audio dump on (`runs/<run>.wav`). "Before" is the pack of the same code with `--class-sounds 0`
(`packs/off4`; the Ruined Village "before" used `off1`, built before the scene follow-up of section 4, which changes only Wartok Canyons), "after" `packs/on5` / `on6`. Analysis:
`tools/plays.py <run> <names.json>` (every `[play]` by id and caller, named from the pack's descriptors).

| What | Run (played / placed) | Before | After |
| --- | --- | --- | --- |
| Torches, sconce and hanging flames | `rv-off`, `rv-on6`: Ruined Village start, walking to the start house door, standing 15 s, then placed at the Long Sword and the orc waypoints | Flame starts ask for id 0 (no clip) | Hanging Flame 785 plays `NewTorch_2_11k`, SconceFlame 769 `NewTorch_3_11k`, Torch Flame 2617 `NewTorch_1_11k`, descriptors with the loop flag |
| Torch loop held | `rv-torch6`: placed beside the Hanging Flame 785, 40 s standing, `DRAKAN_SND_LOG=2` | | One play (game voice 22); the 500 ms voice snapshots show core 0 voice 22 active for the whole stand (snapshots 30 to 65 s), its address cycling inside an 18 KB block, the size of the clip's ADPCM: it loops |
| Equip (Atimar, Tab / Enter) | `rv-off`, `rv-on6`, played | Drakan 2 host sound `0x3001f` | `Equip Sword` (PC) |
| Creature voices | `is-off`, `is-on`: Islands direct start (on Arokh), placed by the Goblins at 481000, 659000, fire breath at the nearest | none from the Goblins; projectile impacts by Rynn and Arokh (`ra 39b8dc`) play a Drakan 2 host sound (`0x301fb`, 418 plays); a flier's projectile launches a Drakan 2 host sound | `GobISeeYou_01` / `_02` / `_03` at the Goblins' positions (23 plays); the impacts play `Proj_Impact_2` (PC; the impact of the converted Sonic Boom and breath projectiles), the launches `CrimD_Sonic Attack` (PC, the converted Sonic Boom). Which creature fired is not identified |
| Arokh | same Islands runs | SYSTEM.SDB 726 to 731 (`0x202d6`..`0x202db`, Drakan 2 "Arohk Hit 1" to "6") | unchanged, by the decision above |
| Door (start house, auto-open) | `rv-off`, `rv-on6`, played | the door plays its Open Looped (field 14) and Open Stop (15) slots, both empty (id 0) | the same: the PC door has no loop or stop sound. Its PC Open Start (`0x400f6`) and Bang Hinges are in the record, but the auto-open path did not reach the start sound and the door did not bounce in either run, so neither was heard in the log |
| Ambient | Ruined Village runs | sound objects play from object parameters as before (Gust, Cricket, BirdWren, FireSmall, Torch_4) | the same, plus the flames; class "Sounds" of converted sound objects are now the PC ones (e.g. Birds (Songbirds), Bats (Bats Area)) |
| Weapon pickup | `rv-off`, `rv-off4`, `rv-on6`, `rv-exp1`, `rv-on7`: Long Sword 1433 picked up with E | the Long Sword's Picked Up call (`ra 347254`, template sound `0x300c4`) in 2 of 2 runs | with the converted PC clip (`SwordUp_5`) in the field: **no call at all** in 3 of 3 runs (a regression). The same pack with only that field set back to `0xc4` (`rv-exp1`): the call is back. So the pickup path (`0x3471d4`..`0x347254`) refuses something about a converted clip; what, is not established. "Picked Up Sound" now keeps the template's value (`TEMPLATE_KEPT_SOUND_FIELDS`); `rv-on7`: the call is back with the Drakan 2 sound |

Frames: `evidence/rv_door_open_before.png`, `evidence/rv_hanging_flame_after.png` (Rynn beside the lamppost flame
during the loop check), `evidence/is_on_arokh_before.png`, `evidence/is_goblins_after.png`.

Pack (final code, `packs/on7` against `packs/off4`): 23 levels, 12,211 files against 11,268; zip 161.97 MB to 167.06 MB
(+3.1 %); RESOURES.SDB descriptors 628 to 942 (+314 PC clips), sample data 30.4 MB to 36.2 MB; no `warning: sounds`
(no clip dropped by the size cap). The coverage script of the survey on the after pack: every family still "missing" is a
name-mapped class (Trolls, orcs, Spider, Skeleton Dragon, War Giant, Rynn, Arokh, smallfire, waterfalls). Between the before and after packs only `new/`
entries (added records and sounds; ids of other converted sounds shift), the pack's own level files and `mod.toml`
differ; no `overrides/` entry changes, so a Drakan 2 game with the pack installed reads the same disc records. Subtitles:
297 lines shown, 1 withheld in both. Drakan 2 was not played: the change is converter-only and touches no shared record.

## 4. The cheap follow-ups of the survey

* **The four waypoints' "Sound to Play"** (Ruined Village 2592, 2621, 3308, 3309): they survive conversion and name the
  converted `Orc_01Ambient Mumble1` (read from the converted LVL, `tools/wpprobe.ts`). Not heard: the orcs did not walk.
* **The 9 missing scene clips:** two scenes no object parameter names. Islands "partial completion" (2 clips) is named by
  nothing in the PC level (unused there). Wartok Canyons "newwarrior" (7 clips) is the **class default** "Sequence To
  Play" of the Warrior Detector 526, which the converter never read: the scene was dropped. A detector without a sequence
  parameter of its own now uses its class's (`classSequenceRefs`), converted and relayed to a Stomp Player like an
  object's own: Wartok Canyons 4 to 5 scenes converted, 3 to 4 detector scenes given a Stomp Player; no other level
  changes, and no new warning for the level. The scene was **not played**: the detector starts switched off (Initial
  State 0) and fires once something in the level's progression turns it on.
* **Music in the Islands direct start:** the PC Islands Level Properties has "Initial Music ID" 0; its music starts from
  music triggers (the nearest, 6020, lies about 70,000 units from the direct start). So no music at a direct start matches the PC data; the converted start
  loop (6147) is off at start as on the PC. In both Islands runs no music request was logged, as before.

## 5. Not verified

* Listening: nothing was heard. The loop check is from voice snapshots, not from the ear.
* A creature's death or hit voice in the game: the Goblin was not hurt (fire breath from Arokh missed) and no PC-voiced
  creature was killed on foot. Death and Damage Reaction lists are in the records (convert log), not shown in play.
* A door's start and bang sounds in the game (see the table), the map chime (no quest update was played), rope creaks,
  power-ups, the Wartok Canyons scene and every level but Ruined Village and Islands.
* Why the pickup path refuses converted clips (see the table); whether other one-shot item fields (Dropped Sound,
  power-up Activation) are refused in the same way. Equip (Activate) and every voice and loop seen do play.
* The rule gave 15 clips the loop flag (convert log); against the before pack these 19 names are newly looped (the rest
  are new clips the PC already loops): `NewTorch_1/2/3_11k`, `Ballista Loop`, `CRD_FireBall Loop 11`, `CRD_PoisonLoop`,
  `CrusherLoop_2eq`, `Deep Wind-11`, `DiggerLoop_2`, `FlameLoop_1`, `GasLoop_3`, `IceBalloop_1`, `PorticullisLoop_2`,
  `SuccLoop_1`, `TrollGateLoop_2`, `Winch_2`, `WindHiGusty_2`, `Wisp_1`, `spinner_3comp`. Only the torches' loop was seen held; that a looped torch stops when
  Rynn walks away comes from the flames agent's reading of the flame code (stop fades the sound), not from a run.
* The 188 cleared fields (doors' start and close sounds, Rynn's "It's locked" on doors ...) were not heard as silent in
  play; they follow the PC data and have only the global switch.
* Volume: PC clips keep their PC volume and drop-off; whether torches or voices are too loud or quiet next to Drakan 2
  sounds was not judged.
* SPU2 sample memory with more clips per level was not measured (the survey's peak was 17 of 48 voices).
