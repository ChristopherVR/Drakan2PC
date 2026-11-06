# Issue 2 follow-ups: missing packs on the title, title notes in every menu language

Two items left open by [issue-2-4-in-game-checks.md](issue-2-4-in-game-checks.md) and
[issue-5-village-scene-and-title-notes.md](issue-5-village-scene-and-title-notes.md).

## 1. A pack enabled in mods.ini but missing from the folder

Before: only the Mods page said "Skipped: not found in the mods folder"; the title showed nothing, and without a
manifest the game choice note could not explain a missing campaign either.

Policy (`docs/reference/modding.md`, "mods.ini"):

| mods.ini value | Pack missing from the folder |
| --- | --- |
| `on` | line kept; title note "A mod pack enabled in mods.ini is missing: {0}." / "{0} mod packs enabled in mods.ini are missing: {1}.", Mods page **Missing** row, log `[mods] pack '<name>' is enabled in mods.ini but missing from the mods folder` |
| `off` | line kept (a decision), not named; the Mods page entry stays |
| `new` | never decided, so nothing to remember: `ModManager::load` drops the line from mods.ini (temp file + rename, other lines and comments kept) and logs `mods: new pack '<name>' is no longer in the mods folder: removed from mods.ini` |

Code: settings `missingPacks` / `missingPacksText` (`ModDiscovery.h`), `ModsPage` Missing row, gameui
`TitleState::missingModsNote` (a third note on the plate), host `drakan_settings_host.cpp`, hle `ModManager::load`.
Translations in `MenuText.cpp` follow the existing terms (Mod-Paket, pack de mods, pacchetto mod, paquete de mods).

The plate now shares its six lines between up to three notes: an equal share each (at most three), then leftover lines
to the notes that need more, in order; a note cut short ends in "...".

## 2. Title notes and the new packs card in English, French, Italian, Spanish (and German)

Driver `build/harness/issue-2-langs/drive.py` (the verify-2-4 driver plus `[general] language`), run from
`dist/DrakanPC` with a private ini, memory card, logs and mods folder. One mods folder for every run:
`test-disc = on` (wrong disc profile: game choice note), `missing-pack = on` (not on disk), `old-gone = new` (not on
disk: pruned), `test-zip = new` (corrupt zip: unreadable note), `zz-new-notes = new` and `test-newcamp = new` (the new
packs card, with the campaign line). Recipe: card shot, Open Mods menu, Mods page shot, Esc, title shot.
`DRAKAN_MODS_NOTICE=1`, 960x540 windowed. Evidence private under `build/harness/issue-2-langs/<run>`.

First round (exe `3d0fe792...`, runs `en`, `fr`, `it`, `es`): every string present and correctly encoded (accents in
French, Italian, Spanish render), card excluded the unreadable zip, all three notes on the plate, `old-gone` removed
from mods.ini, mods.ini otherwise unchanged. Two defects:

1. **French wrapping.** The French space before ':' was a break point, so lines started with ": sa campagne ..." and
   ": missing-pack." (`fr/title_note.png`). `gameui::wrapText` now keeps a lone ':' ';' '!' '?' with the word before
   it (`gameui_test`).
2. **Hints over the copyright line.** On the card the two hints (Select + Later) are right-aligned on the bottom row,
   which the art's second copyright line shares. In French, Italian and Spanish they covered "Software, Inc."
   (`fr/notice.png`, `it/notice.png`, `es/notice.png`); in English "Later" touched "Inc.". A hint row that would start
   left of `design::title::kCopyrightLine2Right` (x 1600) now sits one row up (`kRaisedHintY` 956), right of the
   shorter first line (ends near x 1424). `gameui_saveload_test` checks the card's hint glyphs in all five languages.

Second round (exe `872f7e87...`, runs `en2`, `fr2`, `it2`, `es2`, `de2`; `pair.png` = card above, title below,
`zoom_note.png` = plate enlarged): no line starts with a colon, the hints sit right of "All rights reserved." and
clear of both copyright lines, nothing overlaps or is cut off except by design: in German the three notes need seven
lines, so the game choice note keeps two and ends "...nicht..." (the plate's six-line limit).

## Tests

`mod_test` (stale `new` line dropped, `on` / `off` kept, file untouched on the next start), `mods_model_test`
(missing policy, singular / plural, German and French text, Mods page row, turning it off), `gameui_saveload_test`
(three notes, line budget with three long notes, card hint row per language), `gameui_test` (French punctuation
wrap). Full runtime ctest: 115 of 115 pass (data tests skip).

## Not covered

- The Mods page Missing row was not scrolled into view in game (unit test only).
- The title footer line (bottom left, game name when the game card is offered) still shares the copyright row; it was
  not part of these scenarios.
- Wording by native speakers of French, Italian and Spanish.
