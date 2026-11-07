# Issue 1: Order of the Flame New Game hangs on a German disc

Tester log (RabeOhneNamen, German PS2 disc, release 0.1.4): the pack mounts, `New Game of 'ootf' opens 'OotF 00 -
Intro'`, the level load is rewritten to `cdrom0:/German/Levels/OotF 00 - Intro/...`, then nothing happens until exit.

## Not the language

The suspected cause (pack files registered only under `ENGLISH\`) was already handled: the same log shows every
`files/ENGLISH/...` file mounted a second time under `GERMAN\` (`installFiles`, "portable pack namespace", since
8ae96142), the levels registered in the German `LEVELLST.TXT`, and the entry patches applied to the German tree. The
PAL disc carries all five trees (`ENGLISH FRENCH GERMAN ITALIAN SPANISH`, identical `LEVELLST.TXT`, identical mangled
level folders), so `OotF 00 - Intro` <-> `OOTF00RO` is the same in every language.

## Root cause: the pack was converted on the intro template

The tester's log patches `GERMAN\LEVELS\LEVEL0RO\LEVEL0RO.ODB` (17 entries) and `LEVEL0RO.SSD` (65) and nothing in
`LEVEL1ON`, which is what a pack converted on `Level 0 - Intro` (`LEVEL0RO`) produces; one converted on Surdana patches
`LEVEL1ON.*` (checked with a one-level conversion on each template, `build/issue1-intro/convert.ts`). The 0.1.4 importer
preselected the first disc level, the intro; Surdana became the default only in 0a0c8db5, after that release.

A converted level copies the template's dependency table. Surdana names its level databases with a folder
(`..\Level 1 - Common\Level 1 - Common`); the intro names its own by the bare name `Level 0 - Intro`. Dependency paths are
relative to the level's folder, so the converted level asks for `Levels/OotF 00 - Intro/Level 0 - Intro.db/.odb/...`,
which does not exist, and the load never finishes. This happens in every language.

With the `DRAKAN_START_LEVEL` test aid (and the editor's Play) a second fault hid behind it: the file-layer redirect
replaced the text `Level 0 - Intro` anywhere in a path, turning that request into `.../OotF - intro level.db`.

## Fix

* Runtime (`ModPack.cpp` step 4, `IsoFs::addAlias`): for each pack level, read the LVL header's dependency table; a bare
  name with no file of that stem next to the level is served from the sibling level folder of that name (`LEVEL0RO.*`
  except `.LVL`, sharing the disc extent or the rebuilt overlay, after the entry patches, in the running tree). The
  tester's existing pack loads without a new export. `mod_test` covers it on a synthetic German tree.
* Runtime (`FileService::redirectPath`): the start-level redirect rewrites only the redirected folder component and the
  file names inside it that start with the level name. `override_test` covers both cases.
* Editor (`ootfimport.ts` `ootfTemplateLevels`): the importer no longer offers the intro as a template and lists Surdana
  first in every language tree (`ootfimport.test.ts`).

## Verification

Private one-level packs (PC `Intro.lvl`) converted on `LEVEL0RO` and on `LEVEL1NA`, run from `dist/DrakanPC` with
`DRAKAN_LANG=german` and `DRAKAN_START_LEVEL`:

| Run | Result |
| --- | --- |
| intro template, alias in, old substring redirect, English | `Level 0 - Intro.db` redirected to `OotF - intro level.db`, not found; the game never reaches gameplay (screen -1, gameplay 0) |
| intro template, both fixes, German | log `serving the 8 file(s) of GERMAN\LEVELS\LEVEL0RO`, every `Level 0 - Intro.*` open served from the German tree, gameplay at 8.5 s, clean scripted exit |
| Surdana template, German | gameplay, clean exit; the same (dark) first frames as the intro-template run |

Not verified: the tester's own 13-level pack and the menu New Game path (the runs used the start-level path; the New Game
rewrite changes only the level path, which the alias does not depend on), and whether the intro template's dropped
objects leave later intro-template levels incomplete. Converting again with Surdana as the template remains the
recommended pack.

## Follow-up: the tester's German Order of the Flame disc

The tester's German PC disc image (kept private under `build/issue1-ootf-german/`) was extracted with
`analysis/scripts/ootf_cab.py` (same InstallShield 5 cabinet as the English disc) and converted with the editor's own
`runConversion` (the importer's worker path), the PS2 template read from the ENGLISH tree as the editor does by default.

### What differs from the English release

* Same 337 files, same folder layout and file names (no umlauts in any path). 26 files differ: `Dragon.rrc`, the speech
  banks (`.sdb`), a few `.ssd` sequence files, the multiplayer `.lfl` titles (`LL Luftkampf`, `LL Bodenkampf`), one byte
  pair in `rift crystal.lvl`, `Ruined Village.odb` (12 bytes longer), `ReadMe.txt`. `Drakan.exe` and `Dragon.rfl` have
  the same sizes.
* The converter detects the install as German (`stop words: german 253, ...`); level header names are the English
  fallback texts, so the pack titles are the same as from the English install (`OotF 00 - Intro` ... `OotF 12 - Rift
  World`). Object counts per level are identical to an English conversion on the same template.
* German-only conversion notes: four cinematic speech lines have "no compatible resource mapping" (Islands 0x191 and
  0x195, Succubus Lair 0x11b, Rift World 0x101ad). The German `.ssd` files still reference them, but the German sound banks
  do not contain those ids (Islands.sdb has 77 sounds against 78, without "We cannot allow" / "30-to gain the rift
  crystal"; Succubus Lair.sdb has no "ShillaThrone"; System.sdb has no "dummy09"). The original German game cannot play
  them either; the converter drops those actions and the scenes run without them. Nothing to fix.
* No converter change was needed: the issue 3 work (Windows-1252 text, ASCII titles, id-keyed paths) covers this release.

### In game (current build, `DRAKAN_LANG=german`, title menu)

Recipe: title card, Down + Enter (game card: Order of the Flame), Enter (Neues Spiel), Enter (Allein).

| Pack | New Game | After |
| --- | --- | --- |
| Intro template (`LEVEL0RO`, the 0.1.4 default; 48 files and 65 `LEVEL0RO.SSD` entries as in the tester's log) | `New Game of 'ootf' opens 'OotF 00 - Intro'`, gameplay 0.5 s after the load, intro scene plays | exit to `OotF 01 - Ruined Village` taken by the intro, the village opening scene plays; a direct start in the village walks and looks around normally |
| Surdana template (`LEVEL1NA`) | same, gameplay after the load | same exit to the village and opening scene; walking and looking around in the village normal |

### Objects dropped per level (converter report, out / in)

| Level | Surdana template | Intro template |
| --- | --- | --- |
| 00 Intro | 102 / 102 | 102 / 102 |
| 01 Ruined Village | 1858 / 1858 | 1846 / 1858 (Visibility Toggler x10, Land Morph) |
| 02 Ruined Village Book | 17 / 17 | 17 / 17 |
| 03 Wartok Canyons | 1995 / 2006 (Black Box x2, Quickening Effect, effects) | 1980 / 2006 (also Earth Quake x3, Troll Mines Land Morph) |
| 04 Grimstone Mines | 1557 / 1567 | 1555 / 1567 |
| 05 Grotto | 527 / 528 (Quickening Effect) | 524 / 528 (also Visibility Toggler x2, Quickening Morph) |
| 06 Islands | 2357 / 2360 (Fog Transition x3) | 2353 / 2360 (also Visibility Toggler x3) |
| 07 Alwarren | 1809 / 1820 (Fall01 x5, effects) | 1793 / 1820 |
| 08 Rift Crystal | 1572 / 1572 | 1556 / 1572 (Visibility Toggler x13, hider) |
| 09 Islands Night | 700 / 700 | 694 / 700 (Visibility Toggler x6) |
| 10 Succubus Lair | 1010 / 1011 (Fog + Sky Changer) | 1005 / 1011 (vis x4, open throne morph) |
| 11 Volcanic | 1672 / 1679 (Fog + Sky Changer x6, Quickening Effect) | 1661 / 1679 (also Visibility Toggler x3, Death Mage Invis Morph) |
| 12 Rift World | 149 / 149 | 137 / 149 (Altar Ceil / Floor Morph01, ...) |

The intro template has no Visibility Toggler, morph or earthquake classes, so its packs lose puzzle logic (the Alwarren
hammer and the Rift Crystal togglers, the throne and altar morphs). With the runtime fix the tester's existing pack gets
past the freeze, but it should be converted again with Surdana (the default since 0a0c8db5) to play through.
