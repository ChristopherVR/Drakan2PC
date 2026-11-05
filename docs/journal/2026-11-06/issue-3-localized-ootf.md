# Issue 3: converting localized (German) Order of the Flame installs

GitHub issue #3 follows up issue 1: the reporter owns only the German PC release, while every conversion so far used the
English install (`build/ootf/cab_disc`). No German install is available here, so this pass audits every place the D1
readers and converter assume English, fixes what can be fixed from source and synthetic data, and makes the converter
stop with an actionable message where a localized install could otherwise produce a broken pack.

## Facts from the English install (names and counts only)

* `Dragon.rrc` holds 426 strings. Its only bytes above 0x7f are four Latin-1 letters (e9, f9, dc, a8); none is in the
  0x80..0x9f range where Windows-1252 differs from Latin-1.
* Every level header except the intro names its title by Dragon.rrc id: `<0x9000>Ruined Village` to `<0x900b>Rift World`
  for the campaign, `<0x900c>` to `<0x9015>` for the ten arenas. The intro header is the bare text `intro level`.
* No class name, field name or parameter name in the 23 levels contains a non-ASCII byte (3,687 class names,
  1,313 field names).
* Every level dependency database (`.odb`) that placed objects use exists in the install.

## English assumptions found

| Where | Assumption | Effect on a localized install | Now |
| --- | --- | --- | --- |
| `ootftext.ts` `cleanGameText`, `decodeRrcString`, `wordsToString` | Text is read one byte per code unit, then checked as Unicode | German `„ “` (0x84 / 0x93), `–`, `’` read as C1 control characters and were written as `?` | Fixed: `rawToCp1252` maps 0x80..0x9f to their Windows-1252 characters before the check; the XDB entry keeps the original bytes |
| Runtime `validLevelName` vs. converter titles | Level titles are ASCII | A title such as `OotF 04 - Grimstone-Höhlen` is skipped by the runtime with only a log line: the pack loads but has no level and exits lead nowhere | Fixed: campaign and caller titles are transliterated (`asciiLevelTitle`: `ä ö ü ß` to `ae oe ue ss`, accents dropped, path characters removed); `validateModMeta` now rejects any name the runtime would refuse |
| `planCampaign` titles vs. pipeline `.slice(0, 60)` | Names are short | A long localized name was cut in the level title but not in the exit destination strings, so exits named a level that does not exist | Fixed: the plan cuts the display name so the whole title fits `CAMPAIGN_TITLE_MAX` (60) and every use agrees |
| `planCampaign` Book detection `/book/i` on the header name | English level name | A German `Buch` level lost its automatic exit timer and the Book cut-scene never ended | Fixed: also keyed on name id 0x9001 |
| `planCampaign` first level `/^ruined village$/i` | English level name | Fell back to the second level by id, which is the same level in retail data | Hardened: keyed on name id 0x9000 first |
| `planCampaign` start `/intro/i` on the path | English folder name | Fell back to the id-less level, which sorts first anyway | Hardened: the language-neutral key of the intro is tried first |
| `isOotfArenaPath`, arena row audit, air-arena dragon, CLI, import dialog, `ootfArenaFromPc` | Arenas live under `Multiplayer/` (and `Multiplayer/Air/`) | Localized folders, or an install chosen one folder too high (`Drakan/Multiplayer/...`), put arenas into the campaign plan as chapters | Fixed: `ootfLevelKey` maps the header name id to the retail English path; `isOotfArenaLevel` / `isOotfAirArenaLevel` and the new `OotfLevelOut.arena` flag replace the folder regexes |
| `ootfmusic.ts` palettes | English folder names in the level path | Every level would get the fallback music palette | Fixed: the palette is chosen from `ootfLevelKey` |
| `rynnStart` (campaign arrival markers) | Player class named `Rynn` | Arrival markers at the origin | Hardened: falls back to engine class 9 when no class is called Rynn |
| `ootfArenaFromPc` spawns | Pads named `Rynn Launch Pad` / `launch pad` | Arena with no spawns, so no map definition | Hardened: falls back to engine class 18 |
| `loadRrcStrings` | One `Dragon.rrc`, first match in list order | With several (for example a language subfolder) the pick depended on listing order | Fixed: the one nearest the root wins, the others are warned about |
| `loadPcLevel` missing dependencies | Complete install | Objects of a missing database were dropped with only a level warning, giving a quietly incomplete pack | Fixed: conversion stops with the missing database names (only for databases placed objects use; others warn) |
| Exit field `Transition To...`, text fields `Message String` / `DisplayName`, music / timer / detector field names | Field names are the engine schema (defined by `Dragon.rfl`, not by the level designers) | Not expected to change; if a release localized them no exit would work | Guarded: conversion stops when a campaign level has level transitions (engine class 55) and none carries `Transition To...`, and when a campaign level has no player start |

Left as they are, because the fallback does not break a pack: the Map Marker label cleanup (`ootfpuzzle.ts`
`GENERIC_MARKER`, English words), the arena `Weapon Pad` pickups (no pickups without them), the intro end at the
`Stomp` detector (falls back to the Rynn start), and the PS2 side, which always reads English template class names
from the PS2 disc.

## Language detection and explicit handling

`detectOotfLanguage` scores the Dragon.rrc strings with short stop-word lists for English, German, French, Italian and
Spanish (umlauts and sharp s count for German) and returns `unknown` without a clear winner. The pipeline runs
`checkOotfSource` after loading the levels: it logs `source language: <lang> (<evidence>)`, records the profile in
`OotfPipelineResult.source`, adds its warnings and throws `OotfSourceError` with every blocking problem, before any stage
writes output. The editor dialog shows the multi-line message; the CLI prints the detected language.

The language changes nothing else on purpose:

* Converted text goes into XDB slot 0. The runtime shows slot 0 for every language whose slot is empty, so a German
  source shows its German text in every game language, as an English source shows English.
* Level titles keep coming from the level header text (`pcLevelName`), not from Dragon.rrc, so they are the same in
  every release whose levels carry the same header text, and saves and mod order are not affected by the language.
* Pack files stay under `files/ENGLISH/`; the runtime mounts English-namespace pack files in the active language
  (issue 1 journal).

## Verification

* English regression: a fresh full conversion of `build/ootf/cab_disc` through the editor disc context (all 23 levels,
  arenas, campaign manifest; scratch probe and outputs under `build/issue3/`) was made before the change and repeated
  after it. All 17,334 pack files are byte-identical (SHA-256 per file), the level list and every stage result match, and
  the warning sets are equal. The new log line reads `source language: english (stop words: english 189, german 1, ...)`.
* Synthetic tests (no game bytes): `formats/test/ootfsource.test.ts` (18 tests: language detection, Windows-1252
  quotes and umlauts through `cleanGameText` and the XDB encoder, transliteration and the runtime name rule, id-keyed
  arena and music keys with German folders and a too-high install root, a German campaign plan with umlaut and
  over-long names, Book and first level by id, a renamed player class, and each blocking source check), a pipeline test
  that a missing dependency stops conversion before any level is converted, an editor-core manifest test and a German
  arena fixture with renamed pads. `npm run typecheck` and the whole editor suite (97 files, 988 tests) pass.

## Still needs a real German install

* Whether the German level headers keep the English fallback text (`<0x9000>Ruined Village`) or carry German text; the
  latter changes pack titles, which is handled but unverified.
* Whether the German release ships one root `Dragon.rrc`, keeps the same Dragon.rrc ids and level folder layout, and
  keeps the engine field names (the converter now stops if it does not).
* German speech and sequence timing: the Intro (44 s) and Book (28 s) handoff timers are the English sequence lengths;
  a German dub of a different length would cut or delay the handoff.
* Literal German texts in class defaults (Detector `Message String`), their line lengths in the PS2 message box, and the
  converted speech.
* An end-to-end German conversion, the native campaign start and a playthrough.
