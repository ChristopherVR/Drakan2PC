# Issue #6: review of the save location fix (2026-11-12)

Adversarial check of `docs/journal/2026-11-10/issue6-save-location.md`, which had not been built or run. Scratch,
logs, snapshots and screenshots: `build/review/saves/`.

## Root cause: confirmed

- `git show v0.1.11:ps2recomp/ps2xRuntime/src/lib/ps2_runtime.cpp`: `configureIoPathsFromElf` sets
  `mcRoot = <ELF folder>/mc0`; only native Linux used `exeDir()/mc0`.
- `main.cpp` takes the ELF from `drakan.ini [general] elf` first; `prepare_launch.py` writes `elf = <WorkDir>/SCES_500.06`
  (`Build-LocalGame.ps1`, `--elf $bootElf`, `$bootElf = Join-Path $WorkDir ...`). `Build-Drakan.ps1` defaults WorkDir to
  `<install>/build/game`. So an installer build saves into `build\game\mc0` (and `mc1`, `mc0_<campaign>`).
- The function body hashes the same at every tag `v0.1.0` .. `v0.1.11`, and every tag passes the WorkDir ELF to
  `prepare_launch.py`. It never differed in a release; the player most likely noticed it only when backing up.
- Not explained: why the player's play folder has empty `mc0` / `mc1` folders. Nothing in `scripts/buildtool` or the
  v0.1.11 runtime creates them next to the exe.
- Old uninstallers (v0.1.11 and earlier) delete `{app}\build`, saves included. That cannot be fixed after the fact:
  the release note must say so.

## Holes found in the 2026-11-10 fix and what was done

1. **One note for every play folder (real, and hit on this machine).** The builder's note stopped the copy for all
   play folders. Several play folders can name one ELF folder (developer `iso/`, review copies). The review build into
   `build/review/play` wrote `iso/saves-copied-to-play-folder.txt`; from then on neither the next build nor the game
   would have brought `iso/mc0` into the owner's `dist/DrakanPC`, which would have looked like every save was gone.
   Fix: the note holds one line `<seconds> <play folder>` per play folder; `migrate_saves.py` and the game
   (`drakan_card_root.h` `noteNames`, UTF-8, `fs::equivalent`) only stop for a play folder that is listed. A note
   without such lines (the first format) settles nothing.
2. **Uninstaller missed campaign cards.** On a failed copy it asked only when `mc0` or `mc1` existed; a build folder
   with only `mc0_ootf` was deleted without a question. Fix: `HasCardFolder` (mc0, mc1, mc0_*, mc1_*). The question is
   now `SuppressibleMsgBox(..., IDNO)`: a silent uninstall keeps the build folder.
3. **Rescue before a delete trusted the note.** `Build-Drakan.ps1` and the uninstaller called the normal run, which
   skips a settled place unless a file is newer than the note. A save written by an old exe with a clock behind the
   note would be deleted with the build folder. Fix: `--before-delete` reads the place again whatever the note says.
4. **Re-reading a settled place brought deleted saves back.** When a place was read again (newer file), slots the
   player had deleted in the game were copied back into the card. Fix: an entry not newer than the play folder's note
   line never goes back into the card; when it is missing from or differs from the card it is copied to
   `save-backups/<time>/other-folder/` instead. With `--before-delete` this keeps everything not in the card.
5. **Linux.** Native Linux always used `exeDir()/mc0`; the 2026-11-10 code also copied from the ELF folder there and
   ran the builder step for Linux targets. Fix: adoption is `_WIN32` only; `Build-LocalGame.ps1` runs the step only for
   `-TargetPlatform windows`. The browser build is unchanged (`playDir` empty, no adoption, card `<ELF>/mc0`).
6. **Boot robustness.** The adoption ran unguarded in `configureIoPathsFromElf` and printed paths with
   `path::string()`, which throws on MSVC for characters outside the ANSI code page. Now in `try` / `catch`, printed
   as UTF-8.
7. **Smaller.** Read-only leftovers of a stopped copy are removed (`remove()` clears the flag; `onexc` on 3.12); a
   relative `elf =` line is not used as a place; the summary separates "brought over" from "kept in save-backups"; the
   note no longer claims its folder is deleted with the build (false for `iso/`); ISPP rejects lines that start with
   `#13#10`, so the uninstaller message was reflowed (the first try did not compile).

Checked and fine: copies go to `<name>.migrating` / `.adopting` then rename; file times are kept; nothing in the old
place is changed except the note; a slot folder is one unit (files of two saves are never mixed); loose files on a card
(`mc0_ootf/OotF 01 - Ruined Village.rsg`) are units too; `DRAKAN_MEMCARD_DIR` skips adoption; card 2 and campaign cards
are siblings of `<play>/mc0`, so they follow; harness launches of map and settings pictures set their own card. Clock
or mtime trouble can only change which version is in the card; the other one is always in `save-backups`.

Not handled: a play folder path outside the ANSI code page was already broken before this change (`exeDir()` uses
`GetModuleFileNameA`, so `drakan.ini` is not found either). Until the builder has run once (its step failed), the
game's own adoption copies a deleted save back on each start; that is the intended fallback.

## Results

- `python scripts/run_tests.py`: 525 tests, OK (2 skipped); before the changes 514, OK. `test_migrate_saves.py`: 31.
- ctest `card_root_policy` (own tree `build/review/saves/rt`, `cmake --preset runtime-tests -B ...`): passed, with new
  cases for two play folders on one ELF folder, the old note format, and a non-ASCII play folder.
- Full build by the owner (`build/review/fullbuild.log`, old migration code): `EXIT=0`; it copied 17 entries from
  `iso/` (mc0 7, mc0_ootf 9, mc0_testvalley 1) into `build/review/play`, which then matched `iso/mc*` file for file.
- Incremental build with these fixes (`build/review/saves/build1.log`, play folder `build/review/saves/play` with its
  `elf` pointed at a private empty ELF folder so nothing in `iso/` is touched): `EXIT=0`.
- Uninstaller, run for real: ISCC (`%LOCALAPPDATA%\Programs\Inno Setup 6`) compiled a copy of `DrakanPC-Setup.iss` with
  another AppId and no shortcuts against a stub stage with the pinned embeddable Python
  (`build/review/saves/inno/uninstall_test.ps1`, output `result.txt`). Python present: mc0 and mc0_ootf were copied to
  the play folder from `GetPreviousData('PlayFolder')`, then `build` was removed. Python missing and only `mc0_ootf`:
  silent uninstall stopped, build folder and saves kept. No build folder: uninstalled. No registry entry left.
- Sandbox (`build/review/saves/sandbox`: `play` = copy of the play folder without `textures`, `drakan.ini elf =
  sandbox\build\game\SCES_500.06`, `build/game/mc0`, `mc0_ootf`, `mc1` = copies of `iso/`). Runs use
  `run_game.py` (play_run.py's environment without `DRAKAN_MEMCARD_DIR`):
  - run1: 16 `[mc] copied the save` lines, card `sandbox\play\mc0`; all 36 + 41 copied files equal in content and
    mtime; the Load screen listed the slots from `sandbox\play\mc0`.
  - run2, run3, run4, run5: 0 copies.
  - run4: Load screen (`runs/run4/load_slot2.png`), slot 5 loaded, F5 quick save wrote `sandbox\play\mc0\BESCES-50006Slot33`.
  - `sandbox/build/game` unchanged (sha256, size, mtime) after run1..run4; after the builder step only the note was new.
  - `migrate_saves.py --play sandbox/play --work sandbox/build/game`: nothing to copy (all the same), note written;
    a second run prints nothing.
  - `Slot13` deleted in the play folder, run5: not copied back.
  - Table rows with copies of real slots and the real command line (`table_check.py`, `table_check_output.txt`): all
    rows, the second run, `--before-delete` after a deletion, and the rescue-only place behave as described.
- `iso/mc0`, `iso/mc0_ootf`, `iso/mc0_testvalley`, `iso/mc1`: identical (sha256 and mtime listing) before the owner's
  build, after it and at the end. `iso/` did get `saves-copied-to-play-folder.txt`: first from the owner's review build,
  later rewritten by other agents' review builds that ran the new script (their play folders `build/review/issue6-2`,
  `issue6-4`). It lists only those review folders, so `dist/DrakanPC` still gets the saves. It can be deleted.
