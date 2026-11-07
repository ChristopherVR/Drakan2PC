# Issue 3: Order of the Flame class coverage (no unknown or unmapped classes left)

GitHub issue #3 (Rabe) reported "3 to 12 left out objects per level" after converting the German Order of the Flame
release. This pass compares the PC (Drakan 1) class tables of the English and the German install with the PS2 (Drakan 2)
class tables of the PAL disc, maps what can be mapped, stands PS2 classes in where the PC class has no counterpart, and
lists the rest explicitly as kept out on purpose, with the reason, instead of "no PS2 class ... dropped".

## Inputs (private, under `build/`, nothing committed)

| Input | Path | How |
| --- | --- | --- |
| English PC install | `build/ootf/cab_disc` (337 files) | `analysis/scripts/ootf_cab.py` on the user's `Drakan.zip` (`DRAKAN.mdx`) |
| German PC install | `build/ootf-de/cab_disc` (337 files) | `ootf_cab.py` on `Drakan.German.iso` (from the user's `Drakan.German.7z`) |
| PS2 templates | `build/map16/odbx/data` | `scripts/harness/ootf_level_run.py` `extract()` for LEVEL1NA and LEVEL0RO from the PAL ISO |

The earlier scratch folders (`build/ootf`, `build/cab_disc`, `build/issue1-ootf-german`) had been removed by a cleanup;
both installs were extracted again from the user's own archives in `Downloads/Compressed`.

## English against German

Both installs have the same 23 levels, the same class names in every level, and the same counts in every audit column
(converted with LEVEL1NA before this change: 225 classes name-mapped, 2248 cloned, the same unmapped rows). The German install differs
in text only (Dragon.rrc, detected as German), which issue 3's first pass already covered
(`docs/journal/2026-11-06/issue-3-localized-ootf.md`). No class needs a German-specific mapping; the mapping is keyed on
engine ids and the English class names that both releases share.

## What was left out before (LEVEL1NA template, both installs)

Placed objects of classes the plan could not map ("no PS2 class for ... dropped" in the notes, "Left out" in the importer):

| Level | Class (engine id) | Objects | Why it had no PS2 class |
| --- | --- | ---: | --- |
| Wartok Canyons | Fog Color Changer (143) | 7 | PS2 engine 143 classes have no fields |
| Wartok Canyons | Black Box, Splash 03 (17) | 2 + 1 | the install has no such model (3853, 3860 not in `Wartok Canyons.mod`) |
| Wartok, Grotto, Alwarren, Volcanic | Quickening Effect (80) | 1 each | no PS2 engine 80; Power FX (71) shares none of the 16 fields |
| Grimstone Mines | Sky Trigger (143) | 10 | as Fog Color Changer |
| Islands | Fog Transition (143) | 3 | as Fog Color Changer |
| Alwarren | Sky + Fog Changer (143) | 4 | as Fog Color Changer |
| Alwarren | Fall01 (17) | 5 | model 3918 is not in `Alwarren.mod` |
| Alwarren | Brick17 (44) | 1 | a break-away floor whose class also names a model: the plan looked for a model-bearing floor template (none exists) |
| Succubus Lair, Volcanic | Fog + Sky Changer (143) | 1 + 6 | as Fog Color Changer |
| Sanctuary (arena) | Ambient Sound (131) | 1 | fields Slot / Front Sound / Back Sound against the PS2 slot names |
| all 10 arenas | Rynn / Dragon / Red Dragon Launch Pad, Soul Crystal, Dragon Invulnerability (18, 127, 128, 86, 126) | 5 to 18 | no PS2 engine class; already exported as arena spawns and markers |

The importer only showed the first three notes of a level (`buildOotfPack`), so Grimstone's ten Sky Triggers never
appeared in the notes although the level counted "1 class" left out.

The reporter's "3 to 12" were the counts of the Intro template (LEVEL0RO, `issue-1-item4-german-ootf-start.md`): that
template reaches neither Land Morph (53) nor Visibility Toggler (121), so Ruined Village lost 3 classes and Rift World 12.
The importer no longer offers that template (Surdana is preselected); with LEVEL1NA both engine classes map.

## Mapping now

| PC class | Outcome | Detail |
| --- | --- | --- |
| Quickening Effect (80) | stand-in | `CLONE_ADJUST` 80 reset: a clone of the PS2 Particle Spawner with its own defaults stands at the same place (4 objects) |
| Brick17 (44) | converted | break-away floors always take the model-less floor template; the pieces of the "Model List" are converted as for the other floors |
| Ambient Sound (131) | name-mapped | `PC_CLASS_ALIASES` to the PS2 `AmbientTransition`; `pcParamAlias` puts Front / Back Sound into "Front / Back Sound Slot <Slot>" |
| Fog and sky changers (143) | kept out on purpose | the converted level's sky and fog come from the template's day/night cycle, so there is no PC fog to switch to |
| Black Box, Splash 03, Fall01 (17) | kept out on purpose | the install has no model for them; the original game draws nothing there either |
| multiplayer pads, crystals (18, 86, 126, 127, 128) | kept out on purpose | arena spawns and markers in `maps/<level>.ini` |

`ootfcoverage.ts` holds the catalogue (`deliberateOmission`, `REFERENCE_ENGINES`, `pcParamAlias`). `convertPcLevel` takes
`omitClass` and counts those objects in `report.omittedClasses`, not in `unmappedClasses`, with the note
`kept out on purpose: "<class>" (<n> objects): <reason>`. The class audit marks those rows `omitted`. The pack lists every
kept-out and every unmapped note of a level (`packLevelNotes`), and the importer's "Left out" column reads `none` or
`none (10 kept out on purpose)`.

### References to PC-only effects

Classes that no level places but that converted classes reference (gibs, projectile trails, lens flares, burn effects ...,
`REFERENCE_ENGINES`) produced 433 notes `no safe target for PC class 0x...; omitted`. They are now one note
per family (`PC-only gib / blood chunk references left empty: 248 in 38 converted classes ...`, 20 lines); the audit kind of
such classes is `effect-reference` instead of `unknown` (25 ids, the name-mapped troll shield 139 included). Four
references stay itemised with the target's name, worded `PC class "<name>" 0x... has no PS2 counterpart; the reference is
left empty`: "Flame Sword Light" (the Extra Multiplayer Effects of Flame Sword and Flaming Sword), "Snow Wall 1" (Snow Wall
Explosion's spawn; its model was not converted) and Goblin grenade's "grenade explosion" (a Radius Damage with a model, see
`ootf-referenced-engine-coverage.md`).

## Result (both installs, LEVEL1NA, full 23-level conversion)

`ootf-convert.ts --campaign 1 --audit .` on each install (logs and packs in `build/map16/final/`):

| | before (English = German) | now (English = German) |
| --- | --- | --- |
| levels with unmapped classes | 17 of 23 (campaign 7 of 13) | 0 of 23 |
| "no PS2 class ... dropped" notes | 30 (the importer showed at most 3 notes per level) | 0 |
| objects converted, campaign levels (of 15369) | 15325 | 15330 (+4 Quickening stand-ins, +1 Brick17) |
| objects converted, arenas (of 943) | 834 | 835 (+1 Ambient Sound) |
| objects kept out on purpose, with a note | (reported as dropped) | 147: campaign 39 (fog and sky changers 31, missing PC models 8), arenas 108 (pads and crystals) |
| "no safe target ... omitted" notes | 433 | 4 itemised + 20 family lines |
| audit rows of kind `unknown` | 276 | 1 (the name-mapped troll shield, engine 139, catalogued after this run) |
| class plan | 225 mapped, 2248 cloned, 285 unsupported | 226 mapped, 2250 cloned, 282 unsupported (all referenced-only, none placed) |

The German install gives exactly the same numbers (its pack has 14 fewer files: text and sound differ).

In game (own copy `build/map16/play/drakan.exe`, game language German, `DRAKAN_GAME=d1`, `DRAKAN_START_LEVEL`, runs in
`build/map16/runs/`): the English pack's Alwarren (1645 entities), Wartok Canyons (1896), Grotto (489), Volcanic (1347)
and the Sanctuary arena (45) load with `ok=1`, take control and walk; the German pack's Alwarren the same. The stand-ins were
loaded, not looked at up close.

## Tests (synthetic, no game bytes)

* `formats/test/ootfcoverage.test.ts`: the deliberate omissions by engine id (localized class name), arena vs campaign,
  missing model only when the install lacks it, no alarming words; `pcParamAlias` slots and clamping; the plan name-maps
  Ambient Sound, stands a Particle Spawner in for the Quickening Effect, clones a floor with a class model from the
  model-less template with its converted piece.
* `formats/test/pc.test.ts`: `omitClass` counts apart from `unmappedClasses` with its note; `paramAlias` carries a renamed
  parameter.
* `formats/test/ootfclassrefs.test.ts`: family summary and itemised wording.
* `editor-core/test/ootf-notes.test.ts`, `app/test/ootfimport.test.ts` (`leftOutText`).

## Still open

* A faithful sky and fog switch: a PC changer box could become a one-way detector pair driving two PS2 day/night cycle
  switchers (engine 202, "Morph-To Cycle") whose cycles (engine 201) carry the PC fog colours and the linked Sky objects
  ("Associated Skies"), as Level 4's Green Fog Switcher does. That needs the level's own day/night cycle converted first.
* The Quickening stand-in shows the PS2 spawner's particles, not the PC sparks and lightning.
