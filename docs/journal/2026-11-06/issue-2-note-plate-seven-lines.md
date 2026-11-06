# Issue 2 follow-up: a seven-line title note plate

[issue-2-missing-packs-and-title-languages.md](issue-2-missing-packs-and-title-languages.md) left one cut: with all
three title notes (game choice explanation, unreadable pack, missing pack) German needs seven lines and the plate had
six, so the game choice note ended "...nicht...".

## Room on the art

Measured on the 960x540 shots of that pass (`de2/title_note.png`, design units = 2 x pixels): the plate sits in the sky
at x 24-456, y 32-256 (six 30-unit lines). To its right the logo starts at x 530 and the campaign plaque at x 460, so
it cannot get wider; below it Rynn's hair starts at about y 220 near the plate's right edge, so it should not get
taller either. The menu column and the copyright lines are far away.

## Change

`design::title`: `kNoteLinesMax` 7 (was 6), line height 26 (was 30; Small text is 19), plate top y 24 (was 32). Seven
lines end at y 250, inside the six-line plate's old bottom (256); one to six lines take less room than before. The share
rule is unchanged (an equal share each, at most three lines per note, leftovers in order), so with three notes the
first now keeps three lines. The German strings stay as they are: shortening them would only move the limit, and the
plate now fits the longest translation of the three notes.

Tests (`gameui_saveload_test`): the plate stays in the top left corner and above y 256 for 1 to 7 lines; three long
notes keep seven lines with three "..." cuts; new check that the three real notes (`menuFormat` of the shipped strings)
are drawn without a cut in English, French, Spanish, Italian and German.

## In game

Exe of this pass, `build/harness/fixes3/langs_drive.py` (the issue 2 driver: private ini with `[general] language`,
card, logs, mods folder `langs_pack` with `test-disc = on` (game choice note), `test-zip = new` (unreadable),
`missing-pack = on` (missing)), 960x540, `title_note.png` per language under `build/harness/fixes3/langs/<language>`:

| Language | Lines | Cut |
| --- | --- | --- |
| English | 6 | none |
| French | 6 | none |
| Italian | 6 | none |
| Spanish | 6 | none |
| German | 7 | none (was "...nicht..." with six lines) |

`plates.png` stacks the five plates, `de_zoom.png` is the German plate at 3x. Logo, menu, hints and both copyright lines
are clear in every language. The plate's lower right corner ornament reaches the top of Rynn's hair by a few pixels,
as the six-line plate did (its bottom is now 6 units higher than before).

## Not covered

* A note longer than seven lines (several unreadable or missing packs with long names) is still cut with "...".
* Wording by native speakers.
