# Issue 1 item 4: German Order of the Flame freezes at New Game (log analysis and repeat)

Reporter log: `Ootf-Start-Crash-Drakan.log` (downloaded to `build/`, not committed). Checked against this tree with an own
copy of the play folder (`build/de13/play`, language German, `DRAKAN_GAME=d1`, scratch under `build/de13/`).

## What the log says

* The pack is `ootf-levels`, 13 levels, 48 files, 1218 entries. It patches `GERMAN\LEVELS\LEVEL0RO\LEVEL0RO.ODB` (17 entries)
  and `LEVEL0RO.SSD` (65) and nothing in `LEVEL1ON`: a pack converted on the Intro template (LEVEL0RO), which the 0.1.4
  importer preselected. "3 to 12 left out objects per level" is the converter's unmapped-class count of exactly that
  template (Ruined Village 3 classes / 12 objects up to Rift World 12 classes; German install detected, same counts as English).
* The log has no `serving the 8 file(s) of ...LEVEL0RO` line: that build predates the runtime alias of 773a5f7c, so the
  converted Intro asked for `Levels/OotF 00 - Intro/Level 0 - Intro.*`, which does not exist, and the load never finished.
  After `New Game of 'ootf' opens 'OotF 00 - Intro'` and the level-path rewrite there is nothing but frame flips until the
  player quits 22 s later (the HLE log is capped at 400 lines, so the missing opens are not visible; lifecycle logging was off).
* Not language: resource root `\GERMAN`, the pack mounts under both trees, the rewrite uses the German folder correctly.

This is the same cause the earlier note `docs/journal/2026-11-07/issue-1-ootf-intro-template.md` found from the first log; the second log
is the same fault from a build older than its fix.

## Repeat at HEAD

| Pack | Result |
| --- | --- |
| German install (private cab_disc), converted the importer's way (`runConversion`, template LEVEL0RO, 13 levels, German detected, same unmapped counts as the log) | New Game: `afterLevelLoad OotF 00 - Intro ok=1`, intro plays, exit to `OotF 01 - Ruined Village` loads (`ok=1`) |
| English-converted dist pack on LEVEL1NA, German language | same chain, Intro black with HUD for the first seconds, then the owl and landscape |
| CLI (`ootf-convert.ts`) pack on LEVEL0RO from `build/odbx/data` | `afterLevelLoad ... ok=0 result=-4`, black screen with HUD forever |

The third row was a scratch-data fault, not the importer path: `build/odbx/data` holds no `LEVEL0TS.DB/.TXD` (the harness only
extracts the template's own splat files, LEVEL1TS), so `OotF 00 - Intro_splats.db` was "not found" and the engine returns -4
and never starts the level. The CLI only warned about that. It is now an error.

## Changes

* `formats` `introTemplateWarning` (`ootfsource.ts`): the pipeline warns when the PS2 template names its own database without a
  folder (the Intro). Warning, not error: packs that way load at HEAD through the runtime alias, so refusing would break them.
* `editor/scripts/ootf-convert.ts`: missing template splat files exit with an error (`result -4` freeze otherwise).
* Test: `ootfsource.test.ts` "the Intro as a conversion template" (synthetic dependency paths, backslash and slash, umlaut folder).

## Still true

The reporter should export again with the current editor (Surdana is the default, the Intro is not offered) or just update:
the old pack also loads with the current game. A game-side change was not needed. When a level load fails (`result -4`) the
engine stays on a black frame; the cause is always a missing level file, so the guards above are on the converter side.
Issue 3 audit and fixtures: `docs/journal/2026-11-06/issue-3-localized-ootf.md` (already covers umlaut text, Windows-1252,
localized names, id-keyed paths; 18 synthetic tests).
