# Issue 5 follow-ups: Ruined Village opening scene, unreadable packs, title note plate

Three items left open by [issue-2-4-in-game-checks.md](issue-2-4-in-game-checks.md). Same driver as there
(`build/harness/verify-2-4/drive.py`, private ini / memory card / mods / logs per run, launched from `dist/DrakanPC`),
now with `WIN_W` / `WIN_H` for the window size. Evidence stays private under `build/harness/verify-2-4/<run>`.

## 1. Ruined Village opening scene: ends on its own

Question: after the Intro handoff the scripted scene ended between +145 and +170 s in three runs, but the run without
input (`full4i`) was still in it at +174 s. Does it wait for a key?

Two long runs with the full 23-level pack (`full-mods/ootf-levels.zip`), audio on, no input at all until the end.
RAM dumps every 10 s, decoded with `settings_check.camera_state` (`idle_measure.py <run>`, `measure.json`):

| Run | Exe | Scene end (no input) | Then | Input at the end |
| --- | --- | --- | --- | --- |
| `idle600` (600 s) | `861946f2...` | between +150 and +160 (Rynn jumps from the fixed opening point `(267379.9, 20634.8, 205510.6)` to `(226818.7, 19256.6, 176273.7)`) | a second scripted scene with the dying villager (`s200.png`), then the HUD and player control from about +250 (`s260.png`); Rynn stands still by the hut for the remaining 350 s (steps under 20 units, idle sway) | W 3 s at +602: 141 units, she faces the hut wall (`s605.png`) |
| `idle300` (313 s) | `d8bc7d47...` | between +140 and +150 (`(229111.5, 19257.6, 188353.3)`) | same villager scene, HUD and control from about +240 | S 3 s: 1,475 units; A 2 s: 235; W 3 s: 1,885 (`s311.png`, back against the post next to the villager) |

So the opening does not wait for input and does not soft-lock: it ends by itself, runs the villager scene and hands
over control, with no key pressed. The end time simply varies between runs (+140 to +150, +145 to +150, +150 to +160,
+165 to +170, later than +174): `full4i` was a late one, not a stuck one. The spread (about 30 s) was not traced to a
cause; candidates are the harness's 32 MB RAM dumps and frame pacing during the scene (the World rate swings between
8 and 108 fps in the shots). No converter or runtime change was needed.

Note for movement checks: the slower numbers after the idle runs are the cramped spot beside the villager (walls and a
post on two sides), not a lack of control; the earlier `full3` / `full4w` runs pressed W before the villager scene and
ran in the open (about 5,500 units/s).

## 2. Unreadable pack named on the title

Before: a corrupt `test-zip.zip` (`skip-zip`) got no title line, only "Skipped: invalid pack: not a zip (too small)" on
the Mods page; listed as `new` it would even have been offered on the new packs card, and enabling it loads nothing.

Change (`fix(game)` commit b663387e):

* hle `ModStatus::readError`: the archive or manifest error, kept whether the pack is enabled or not (before, a
  disabled pack's error was replaced by "disabled in mods.ini").
* settings `ModInfo::readError` / `file`; `ModDiscovery` `unreadablePacks` (found, read error, enabled or still `new`;
  a pack the player turned off is not named) and `unreadablePacksText` ("A pack in the mods folder could not be read:
  {0}." / "{0} packs in the mods folder could not be read: {1}.", German, French, Italian, Spanish in `MenuText.cpp`).
* `ModsModel::undecided / enableUndecided / keepUndecidedOff` skip unreadable packs, so the card neither lists nor
  enables them; their Mods page status reads "Disabled, cannot be read: <reason>", the page has an **Unreadable** row.
* `TitleState::modsNote` on the title's main page, next to the game choice line; log
  `[mods] pack 'test-zip.zip' cannot be read (not a zip (too small)): named on the title screen`.
* Tests: `mod_test` (readError for a disabled broken zip, a folder without manifest, none for a missing pack),
  `mods_model_test` (policy, card exclusion, decided-off packs, German text, Mods page row), `gameui_saveload_test`
  (both notes drawn).

In game (`d8bc7d47...`): `after2-newzip/title_note.png` (pack `new`: "Ein Paket im Mod-Ordner konnte nicht gelesen
werden: test-zip.zip."), `after-newzip-card/notice.png` (the card lists only the readable "Fresh Text Pack"; mods.ini
keeps `test-zip = new`), `after-skipzip/mods_page.png` (enabled broken zip: Unreadable row), `after2-*/title_note.png`
(both lines together with a wrong-disc campaign).

## 3. Title note plate off the copyright line

Before (`before-169`, `before-43`, `before-1610` `/title_note.png`, exe `861946f2...`): the plate (1140 of 1920 units,
above the footer) covered the left half of the art's "(c)2002 Sony Computer Entertainment America. All rights
reserved." line at 16:9, 4:3 and 16:10 alike. The game letterboxes its own 16:9 picture in 4:3 and 16:10 windows, so the
art and the design canvas line up at every aspect ratio and one design position fits all.

After: the plate is `design::title::notePlate(lines)` in the sky of the top left corner (x 24-456, y 32 to at most 256
for six lines), left of the logo (x >= 530) and the campaign plaque (x >= 460), above Rynn's head; up to three lines per
note. The FPS counter moves to the bottom corner while the plate shows (`setAvoidTop`). Shots `after2-169`,
`after2-43`, `after2-1610` (960x540, 960x720, 960x600): copyright line, menu, logo and hints all clear.
`gameui_saveload_test` checks the plate stays inside that corner for 1 to 6 lines.
