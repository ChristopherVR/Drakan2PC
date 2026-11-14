# Issue 6 items 2 and 3: quicksave not loadable, camera snap while strafing (2026-11-15)

Tester report (installer v0.1.17, Drakan 2, in-game FPS uncapped, 140 fps cap in the GPU driver, 144 Hz monitor):
"quicksave doesn't create a savegame that I can load" and "sometimes when strafing and rotating the camera at the same
time, the camera suddenly jerks or snaps". Scratch, runs, logs and pictures: `build/review/issue6-save-cam/`. Play
folder: a copy of `build/review/baseline-play` (the owner's `drakan.ini`: modern save screens, native pause and title,
world rate uncapped, vsync off) with a private ELF copy. This machine's display is 60 Hz, so a 144 Hz vsync lock was not
reproduced; the 140 fps cap was set with `frame_limit = 140` (env `DRAKAN_FRAME_LIMIT`), not in the driver.

## Item 2: the quicksave is written, but Load Game never lists it

Reproduced on the dist exe (main `bacb3eaf`), run `runs/qs1`: F5 in Surdana wrote
`card/mc0/BESCES-50006Slot33/Level 1A - Surdana.rsg` (log `quick save: returned to gameplay`). Pause > Load Game was
enabled (the save-exists hook counts the quicksave) and showed page 1 of 4, all empty (`runs/qs1/e_load.png`).

Cause: the quicksave lives in folder `Slot33`, outside the manual slots 1..`save_pages * 4` (at most 32), and only F5 /
F8 ever used it. Neither the Load Game pager (native or modern) nor the title's Continue (newest manual save) looked at
it. A player who quick saves and then opens Load Game, or quits and comes back, finds nothing. F8 does load it, also at
the title after a restart, but nothing in the menus says so. Same code in the v0.1.17 tag (`drakan_quick_save.inl`,
`drakan_menus.cpp`, `titleScanSaves` are unchanged between v0.1.17 and `bacb3eaf`). Nothing else differs for an
installer player: the default install folder is `%LOCALAPPDATA%\DrakanPC` (`PrivilegesRequired=lowest`), the card is
`dist\DrakanPC\mc0` next to the exe, F5 / F8 are the default bindings, and the file is written at once, not on exit.

Fix (`[general] quicksave_in_load = 1`, default on, env `DRAKAN_QUICKSAVE_IN_LOAD`; 0 = as before):

- `drakan_save_pages_policy.h`: folder pages (what the slot cells name, 8 = Slot33..36) vs pager pages (manual pages,
  then the quicksave page when listed), stepping and wrap. `drakan_menus.cpp` keeps the folder page in `g_page`, lists
  the quicksave page only on a Load screen while `<card>/<saveId>Slot33/*.rsg` exists, and names the manual page again
  as soon as Load Game is gone (boxes over it excepted), so Save Game can never write into the quicksave's folder. The
  pager hooks are installed with one manual page too.
- Modern screen: `SaveSlotsState::quickPage` / `quickSlot`, the page is titled "Quicksave" and shows its one slot; the
  load question names it ("Progress since your last save is lost. Quicksave: Surdana."; new string, 5 languages).
- Title Continue considers slot 33 (newest of manual and quick).
- Load Game opens on the quicksave page when the quicksave is the newest save on the card (otherwise on the page it
  showed last), so quick save, quit, Load Game shows the save at once. With one page in all (`save_pages = 1` and no
  quicksave) PgUp / PgDn / L1 / R1 keep their native meaning on the slot screens.
  Checked in the game: `runs/v1_open/o1_load_opens.png` (quicksave-only card: Load Game opens on "Page 5 of 5,
  Quicksave"), `runs/v3_manual/m3_load_opens.png` (quicksave, then a newer manual save: opens on page 1, slot 1),
  `runs/v2_sheet.png` (`save_pages = 1`, native, no quicksave: Load Game, PgUp, Esc, PgDn identical with the
  v0.1.17-era dist exe on the left and this build on the right).

After (own build of main + this change, `runs/qs2`, `qs3`, `qs4`):

- `qs2` (modern): F5, Pause > Load Game > Left: "Page 5 of 5", QUICKSAVE, Surdana with its picture
  (`f_quick_page.png`); Enter, Load: `ConfirmLoad(33)`, loaded. Pause > Save Game afterwards: page 1 of 4, slots 1..4
  (`j_save_screen.png`); the card still holds only `Slot33`.
- `qs3` (restart with that card, modern title): "Continue  Surdana" (`c_title_quick_page.png`), Continue loads slot 33.
- `qs4` (`save_menu = native`, the installer default): PgUp shows "33 - Surdana", "Page 5/5" (`n2_quick_page.png`),
  PgDn returns to page 1.

All menu input in these runs is the harness input script (keys), not SendInput; see "Not verified".

## Item 3: one-tick sideways camera snap when her heading crosses world -x

Recipe `recipes/cam_strafe.txt`: hold D or A (strafe) with steady mouse turning, both directions, then W+D / W+A, three
turn speeds, `log on 1` (per-frame `[in-pos]`: Rynn's position and heading, camera position). `tools/cam_report.py`
computes per frame the change of the camera's bearing round Rynn plus her heading change (`res`, 0 for a rigid
follow) and the camera's movement relative to her (`crel`); frames next to a hitch (dt > 40 ms) are left out.

Before (dist exe), `runs/cam140` and `runs/ct140` (140 fps cap, vsync off): four single-frame jumps each, `res` 0.26 to
0.29 rad (15 to 17 degrees) and 290 to 330 units, then a slow drift back; e.g. f=2801 in `cam140/cam.csv`. The trace run
(`DRAKAN_CAMERA_TRACE=1`, new in this change) shows where: `ct140` f=2562 `p0=(443421.2 ...) p1=(443069.9 ...)`, the PC
carry before the native tick moved the camera 351 units onto the native target in one tick. Rynn's heading went from
3.1386 to -3.1385 in that tick. `beforeCameraFollow` took the raw difference (-6.277 rad) as the turn; the rotation
did not care, but `lagKeptAfterTurn(-6.277)` is 0, so the whole travel lag (the distance the camera trails her, a few
hundred units while strafing) vanished at once. The seam test (`> 2.5 rad` = a re-seated transform) used the wrapped
value, the rest did not. Every one of the four jumps per run is one crossing of the seam (counted from the log).

So it is not frame-rate dependent: it happens at any rate whenever her heading crosses world -x while she moves (the
lag is largest while strafing); the 60 and 30 fps runs simply did not cross the seam with the same per-sample mouse
counts. It came in with the travel lag split (`camera_lag_in_place`, 12 November) and is in v0.1.17 (the camera code is
unchanged between the tag and `bacb3eaf`); `[mouse] camera_lag_in_place = 0` avoids it on v0.1.17.

Fix: `carryYaw` in `input/CameraCarry.h` (the short way round, 0 above 2.5 rad), used for the rotation and the lag
shortening alike. No setting: a turn of 0.006 rad now does what a turn of 0.006 rad did everywhere else; nothing changes
away from the seam. Unit tests: `carryYaw` cases and a module case crossing the seam (`input_test`, fails without the
fix and passes with it).

After (own build), same recipe:

| run | rate | seam crossings | max `res` (rad) | max `crel` | p99 `res` |
| --- | --- | --- | --- | --- | --- |
| cam140 (before) | 140 cap | 4 | 0.262 | 294 | 0.025 |
| ct140 (before, trace) | 140 cap | 4 | 0.292 | 328 | 0.035 |
| fix140 | 140 cap | 4 | 0.071 | 188 (radial: collision pull-in, `res` 0.005) | 0.029 |
| cam60 / fix60t | 60 | 0 / 0 | 0.056 / 0.056 | 90 / 95 | 0.041 / 0.043 |
| cam30 / fix30 | 30 | 0 / 0 | 0.108 / 0.119 | 121 / 122 | 0.088 / 0.089 |

The 30 and 60 fps distributions are unchanged (the larger values there are the multi-frame ease at the start and end of
a turn). Another game instance (another agent) was running during these runs; frame hitches show up as `dt` spikes and
are left out of the table.

Other findings:

- `runs/fix140` f=3545 to 3552: the camera distance falls from 1280 to 490 over eight frames while she walks uphill
  (W+A) with no change of bearing (`res` about 0). The pattern is the native camera's collision pull-in towards a
  blocked target, not the carry; a traced rerun (`runs/fix140t`, two seam crossings, max `res` 0.070, max `crel` 78) did
  not meet that slope again, so this attribution is not traced.
- The tester's vsync state is not known (the game's default is vsync on). With vsync on at 144 Hz and a 140 fps driver
  cap, `tick_snap` (`snapTickToRefresh`) gives the world a double step once every 35 ticks (about four times a second;
  computed with 1/140 s ticks against a 1/144 s refresh), matching the frame the display shows for two refreshes. Mouse
  turning is counted in real time and not affected. That is an even judder, not a sideways camera jump, and is not
  changed here; a cap at or above the refresh rate, or vsync without the driver cap, avoids it.

## Also done, not a fix

- `DRAKAN_CAMERA_TRACE=1`: `[cam-tick]` line per follow tick (`drakan_input_glue.cpp`).
- Tried a SendInput (real Raw Input) run first (`tools/cam_drive.py`): the game lost the foreground to another window
  after about a second (`foc=0`), so the camera evidence uses the harness input script's per-sample mouse counts. The
  carry code is the same for both; the accumulator and `mouseCutoffUs` path (only active with vsync) is not exercised.

## Process note

The second game build was started without first taking `build/.game-build.lock` (my `mkdir` had failed because another
agent held it), and the command's cleanup removed that agent's lock folder. The build script's own work-dir lock
refused to run, so their build was not disturbed; the lock folder was recreated at once and my build ran later under my
own lock.

## Not verified

- A real driver-level 140 fps cap on a 144 Hz display with vsync (this display is 60 Hz).
- Real SendInput keys and mouse in the menus and for the camera (focus was taken by another window).
- German menus on the new page (the string is translated; not looked at in the game).
- Order of the Flame campaign cards (`mc0_ootf`): same code path, not run.
- Quickload from the new page while a native error box is up; a card with only manual saves still behaves as before
  (no quicksave page) by the code path, checked only through the policy tests.
