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
