# Issue #6 follow-up: saves were written to the build folder, not the play folder

Player report (installer build, v0.1.10 updating to v0.1.11): the saves are in `build\game`, the `mc0` / `mc1`
folders in `dist\DrakanPC` are empty, the game is started from `dist\DrakanPC` and loads the saves normally.

## Root cause

On Windows the memory card root was not tied to the play folder. `PS2Runtime::configureIoPathsFromElf`
(`ps2recomp/ps2xRuntime/src/lib/ps2_runtime.cpp`) set `mcRoot = <folder of the boot ELF>/mc0`. Only native Linux was
moved to `exeDir()/mc0` (an `#if defined(__linux__)` block), with the comment that the ELF lives in the disposable
build cache. The same was true on Windows and was not fixed there.

The ELF's folder comes from `main.cpp` `getExecutablePath`: `drakan.ini [general] elf` first. The builder writes
that file (`scripts/buildtool/prepare_launch.py`, called at the end of `Build-LocalGame.ps1` with
`--elf $bootElf`, and `$bootElf = <WorkDir>/SCES_500.06`). So in every installer build:

- `dist\DrakanPC\drakan.ini` holds `elf = <install>\build\game\SCES_500.06`;
- the cards are `<install>\build\game\mc0`, `mc1` and the campaign cards `mc0_<campaign>`.

Neither the working directory nor the way the game is started matters: the installer's shortcuts start
`<play folder>\drakan.exe` with the play folder as working directory (`DrakanPC-Setup.iss` `[Icons]`), `Play.cmd`
does `cd /d "%~dp0"`, and a double click behaves the same. The files in the play folder are plain copies
(`Copy-Item` from `WORKDIR/bin`), not links and not a stub.

Everything else the player can change was already next to the executable (`drakan_cfg::exeDir()`): `drakan.ini`,
`drakan_input.ini`, `drakan_native_options.ini`, `multiplayer.ini`, `ui.ini`, `mp_maps.ini`, `drakan.log`, crash
dumps, `mods/`, `previews/`, `subtitles/`, `movies/`, `dev/`. Only the cards followed the ELF. (The debug panel's
`ps2_debug_dumps` uses the working directory, and the ELF read from the disc image when no ELF is found goes to
`%LOCALAPPDATA%\DrakanPC`; neither is player data.)

### Which commit

There is no regressing commit in this repository's history. `git log -S` for the card root code, for
`prepare_launch.py` and for the `elf =` line all end at the first public commit, `3eb29c5d` (2026-10-28), and every
release tag from `v0.1.1` to `v0.1.11` has the identical `configureIoPathsFromElf` and the identical
`elf = <WorkDir>/...` line. An installer build has therefore always saved into `build\game\mc0`. The empty `mc0` and
`mc1` in the play folder are consistent with that; the docs and the kit README did say "Saves go to
dist\DrakanPC\mc0", which was the intent and was wrong for Windows. What the player saw as a change is most likely
the first time they looked. The developer tree shows the same thing: `dist/DrakanPC/mc0` is empty and the saves are
in `iso/mc0` (its `drakan.ini` names `iso/SCES_500.06`).

The real danger was not the place itself but what deletes it:

- `Build-Drakan.ps1` removed the whole WorkDir on "start over" (`-Fresh`, the `S` answer, a disc of the other
  region, incomplete ISO files) while printing "the play folder, saves and settings are kept";
- the uninstaller deletes `{app}\build` ("the play folder (saves and settings) is left in place").

## Fix

1. Resolution rule (`ps2recomp/ps2xRuntime/src/lib/drakan_card_root.h`, standard library only):
   `DRAKAN_MEMCARD_DIR` if set, else `<folder of the executable>/mc0`, else (a platform where `exeDir()` is only
   the launch directory: the browser, others) `<ELF folder>/mc0` as before. Card 2 and the campaign cards stay
   siblings of card 1 (`mc1`, `mc0_<campaign>`), so they follow. Windows and native Linux now share the rule.
2. The game, on start (no `DRAKAN_MEMCARD_DIR`): `adoptMissingSaves(<ELF folder>, <play folder>)` copies every
   top-level entry of every card folder next to the ELF that the play folder's card does not have. It never
   replaces an entry, never changes the old place, copies under a temporary name first and keeps file times. It
   stops once the builder's note is in the old place, so a save deleted in the game does not come back.
3. The builder (`scripts/buildtool/migrate_saves.py`), which settles what both places have. Old places: the folder
   of the ELF in the play folder's `drakan.ini` (what the old game really used); the WorkDir when there is no such
   line; the WorkDir as a rescue-only place when the ini names another folder.

| State | What happens |
| --- | --- |
| Only the old place has the save slot | It is copied into the play folder. |
| Only the play folder has it | Nothing. |
| Both, identical | Nothing. |
| Both, old place newer | The play folder's slot is moved to `save-backups/<time>/play-folder/<card>/<slot>` and the old place's slot is copied in. |
| Both, play folder newer (or a rescue-only place) | The play folder's slot stays; the other one is copied to `save-backups/<time>/other-folder/<card>/<slot>`. |

   A save slot folder is one unit (the newest file decides), so files of two saves are never mixed. Nothing in the
   old place is changed or removed. `save-backups/<time>/README.txt` lists what was set aside. A finished place gets
   `saves-copied-to-play-folder.txt` and is only read again when a save in it is newer than that note.
4. Where it runs:
   - `Build-LocalGame.ps1`, right after the play folder is refilled (a failure is a warning; nothing was deleted and
     the game adopts the missing saves itself);
   - `Build-Drakan.ps1`, before each of its two `Remove-Item $WorkDir` (a failure stops the script, the folder is
     not deleted);
   - `DrakanPC-Setup.iss` `InitializeUninstall`, before `{app}\build` is removed (on a failure the player is asked,
     default No).
5. The play folder clean keeps `save-backups` next to the cards.

Copy, not move: the old place can be the developer's `iso/` folder or a folder another play folder also points at,
and a move that is interrupted or wrong cannot be undone. The copies in the build folder go away with it.

## Side effects for the harnesses

A run that gave the game a private ELF copy to get a private card now has to say so:
`combat_diagnostic_run.ps1` (used by `combat_encounter_run.ps1` and `combat_quick_inventory_run.ps1`),
`combat_run.sh` and `snd_test_run.sh` set `DRAKAN_MEMCARD_DIR` to the card next to their ELF. The Python harnesses
already set it. `stab_matrix2.sh` copies the exe into each slot folder, so its cards stay per slot.
`perf_run.ps1` and `listener_repro.sh` start a development exe with `iso/SCES_500.06` and now use the card next to
that exe.

## Tests

- `scripts/tests/test_migrate_saves.py` (20 tests): each row of the table, a slot as one unit, the rescue-only
  place, the second run, unfinished copies, the old place list from `drakan.ini`, the command line, and that both
  builders and the uninstaller call the migration before anything is deleted.
- `ps2recomp/ps2xRuntime/tests/card_root_policy_tests.cpp` (ctest `card_root_policy`): the rule, the card folder
  names, adoption for only-old / only-play / both / same folder / builder's note.
- `python scripts/run_tests.py`: 514 tests, OK. `ps2_runtime.cpp` passes an MSVC `/Zs` check with the game
  build's flags; the installer script compiles with ISCC against a stub stage. The game itself was not built or
  run for this note, and the uninstaller step was compiled but not run.
