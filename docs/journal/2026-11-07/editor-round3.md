# Editor round 3: Play from the desktop app, converted-level scopes, small-window layouts

Driven in the real desktop app (Tauri build, WebView2 with remote debugging, a script over the Chrome DevTools Protocol)
against the player's PAL disc and Order of the Flame install. The game ran from a COPY of the play folder
(`robocopy dist\DrakanPC build\editorplay /MIR`, the desktop settings' `gameDir` pointed at it; the original
`settings.json` is backed up as `build/settings.backup.json`). Nothing in `dist/DrakanPC` or the game exe was touched.

## Play, end to end

| Case | Result |
| --- | --- |
| Disc level (Surdana), toolbar Play | Game started on a fresh throw-away card (`%TEMP%/drakan-editor-playtest/card0`), level loaded, `[playtest] +120 boundaries: player at ...` in the game log; closing the window ends the process and `game_running` turns false |
| Play twice | Second press reports "The game is already running. Close it first." |
| Converted level (OotF - Ruined Village), before the fix | The game started but never reached the level: the converted pack was not in the mods folder (the one there came from an older run with other level titles), and the game stalled after the level load |
| Converted level after the fix | Play writes the converted pack as `mods/ootf-levels.zip` (stored zip, one transfer, skipped when a zip of the same size is there), enables it, passes `DRAKAN_LANG` for the pack's language tree, and the game places the player in the level |

Findings that were fixed on the way:

* Play on a converted level did not install anything. New shell commands `mods_zip_size` and `mods_write_zip` (raw body
  zip, atomic rename, `mods.ini` updated) and a worker call `convertedPackZip` that zips the pack the page holds.
* The play folder was set to German while the editor opened the English tree, so the converted pack (English tree only)
  was invisible to the game. Play now sends `DRAKAN_LANG` (allow-listed in the shell) for converted levels.
* The importer preselected the first template, "Level 0 - Intro". With it Ruined Village converted 1846 of 1858 objects
  and the game stalled on it; with Surdana it converts 1858 of 1858 (0 classes left out) and runs. Surdana is now the
  default template.
* Browser: every Play entry that threw ("Play needs the desktop app.") now opens a dialog explaining what Play does and
  linking to the releases page; the browser export path is named in it.

Caveat: Play replaces a pack called `ootf-levels` in the mods folder. A player who installed their own full OotF pack
under that id gets it replaced by the levels the editor converted.

## Smaller items

* Levels table: converted levels are typed "Converted" (disc levels stay "Disc").
* Place > Level exits: RFL class 0x48 is a bare marker; "Particle Spawner_null" and "null marker" moved to Effects and
  Other.
* Load card says "converted from Order of the Flame" for converted levels.
* Logic dock: container queries slim the side column below 820 px of dock width and fold the how-it-works column
  below 640 px (the empty state already explains it).
* Story: cut scenes and conversations get the same scope menu as quests. The worker records the sequences
  (`new/<STEM>.SSD/0311_`) and classes (`new/<STEM>.ODB/0102_`) a converted pack adds; the converted pack of the test
  install adds 8 cut scenes to LEVEL1ON (list 60 -> 8) and no conversation handlers (the scope stays hidden then).
* Narrow windows: shrinking below 900 px folds the Outliner/Details column (it only did so at startup before); the
  status bar drops the camera position under 1000 px; the Place header shows only the Landscape icon when narrow; the
  Save editor app bar stays on one line at 820 px (brand and folder labels hidden).

## Not done

* Strings and the Text and export tab still show the disc's text for converted levels (the converter writes the OotF
  text into `SYSTEM.XDB` and `LEVEL1ON.XDB`; no scope for those yet).
* Terrain draw calls (about 2450 on Islands) are unchanged: a texture array or atlas needs a custom material that keeps
  the lighting and fog; raising `SHARED_CHUNK` trades culling for draws and was not measured.
* The full polish pass was screenshots of the start page, Level Editor, Story, Project Settings, Menus and the Save
  editor at 1920x1080, 1280x720 and 820x700 (not every dialog); no cramped or unstyled spot beyond the ones above.

## Follow-up: temporary Play pack, scoped strings, terrain draws, narrow toolbar

**Play never touches the player's pack.** The converted levels go into `mods/editor-play-ootf.zip` with the manifest
retagged (`id`, `name`). Two converted OotF packs cannot be on together (both replace the widened `RESOURES.MOD` /
`WORLDCON.MOD` and hand out class ids from the same ranges), so for the session every `ootf*` entry of `mods.ini` is
switched off and the Play pack on; the desktop shell (`playpack.rs`) keeps the original `mods.ini` text in
`%APPDATA%/dev.drakan.editor/play-pack-backup.json`, restores it and deletes the zip when the game and its extra
instances have closed, and again at the next editor start if the editor was closed first. A second session keeps the
first backup. Checked in the desktop app with the player's own `ootf-levels` pack present and enabled: the game log
listed only `drakan-character-motion` and `editor-play-ootf`, no `conflict` lines, the player was placed in the level,
and after the window closed `mods.ini` and the mods folder were as before (`play-pack-backup.json` gone). Tests:
`play-pack.test.ts` (ini edit, manifest retag, id), `playpack::tests` (restore, backup kept).

**Strings** get the same Order of the Flame tick as the other Story lists (ids from `new/<STEM>.XDB/0400_<id>_`).

**Terrain draw calls.** Chunk size of untouched layers is now 512 cells (`SHARED_CHUNK`). A texture array or atlas was not
built: the numbers below show terrain is down to well under 100 draws, the rest is object models.
Software-rendered browser (Playwright Chromium, SwiftShader), Islands and Volcanic converted with the Surdana template,
three runs each, draws read from the status bar after the load finished:

| Chunk | Islands draws / fps | Volcanic draws / fps |
| --- | --- | --- |
| 128 (before) | 3141 / 32 | 2168 / 50 |
| 256 | 2108 / 41 | 1280 / 60 |
| 512 or 1024 | 1351 / 51 to 59 | 1280 / 60 |

Triangles are identical in every run (1082k and 991k). Open time (object list, then terrain and models) stayed within
the run-to-run noise (about 3 to 6 s); the JS heap reading moved between 250 and 610 MB from run to run on the same
chunk size, so no heap difference can be claimed. Screenshots of 128 and 512 differ in 164 and 140 pixels out of
574k (animated parts), i.e. the picture is the same. The cost is coarser frustum culling; on a GPU that is not visible.

**Narrow toolbar.** At 820 px the Level Editor toolbar was 1200 px wide: Arrange, Export and Play were pushed off the
screen. Below 1100 px the menu buttons show their icon only and the separators go; below 900 px duplicate, delete, the
saved flag, theme and help leave the bar too (their shortcuts and menu entries remain). Play and Export are on screen at
820 px (checked in the desktop app). The page no longer scrolls sideways when a folded column leaves a few pixels
past the edge.

**Polish pass, what was checked.** An automated sweep (overlap, off-screen and clipped-text checks) ran over 29 states
(start page, level picker, importer, undo history, Add dialogs, Unreal export, shortcuts, help drawer, the four Story
areas, Disc Files, Landscape, Logic, Menus, Vendors, World Settings, Package and Play, Project Settings and the three
bottom docks) at 820x700 and 1280x720. 1280x720 was clean apart from the 4 px sideways scroll fixed above; 820x700 showed
the toolbar problem above. The final 820 sweep did not complete (a file chooser opened mid-run), and 1920x1080 was only
looked at on the start page, so those are not claimed.

Still open: the left Place panel keeps 320 px at 820 px (the viewport is about 400 px wide there), the task checklist
pill overlaps the bottom right of a short dock, and the Strings scope was not exercised against a pack that adds text
(the test pack adds none to SYSTEM or LEVEL1ON).
