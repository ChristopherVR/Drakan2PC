# Issue 4 follow-up: the WebView2 smoke test on CI, and "Save to mods folder..." in the release builds

Follow-up of `issue-4-release-builds.md`. Evidence under `build/issue-4-ci` (ignored).

## The smoke test on the hosted runner

Every path of `editor-release.yml` publishes: `workflow_dispatch` with an empty tag picks the newest version release and
uploads to it, and a push to main that touches `editor/**` does the same. So no run was started from here.

The smoke step had in fact already run on a hosted runner: the push of `ddc588c1` started
[run 42125231662](https://github.com/ChristopherVR/Drakan2PC/actions/runs/42125231662) (log saved as
`run-42125231662.log`). The job is green only because of `continue-on-error`; the step itself failed:

```
ok   desktop shell
FAIL run: timed out waiting for the importer button
```

Cause: the runner has a fresh WebView2 profile and no disc. The Order of the Flame tile needs a disc, so its button reads
"Open your disc to start" and never "Open the importer". The local runs (including the earlier "no disc" runs with
`recentIsos` emptied) all reused the developer's WebView2 profile and so always saw "Open the importer". Reproduced
locally with `WEBVIEW2_USER_DATA_FOLDER` set to a new folder: the same failure, and the new timeout message lists the
visible buttons, "Open your disc to start" among them.

Fix in `editor/packages/desktop/scripts/webview-smoke.mjs`:

- When the tile offers the disc instead of the importer, the test opens the importer from the command palette ("Search
  everything", `>Import Order of the Flame`), which needs no disc. It prints which route it took.
- A timeout names the visible buttons.
- The test refuses to start while the debugging port still answers. Before this, a second run straight after the first
  attached to the first run's WebView2 browser, which was still closing, and failed "desktop shell".
- The test kills the whole process tree (`taskkill /T`) and gives up after 120 s instead of hanging the job.

Results with the portable release exe: 5 of 5 runs passed on fresh profiles via the palette (`smoke-fresh-profile.log`),
and 2 of 2 on the own profile with the disc via the start page (`smoke-own-profile.log`).

New `.github/workflows/editor-smoke.yml`: dispatch only, `contents: read`, no release lookup or upload. It builds the
editor exe (`tauri build --no-bundle`) and runs the smoke test N times (input `runs`, default 3), each on a new WebView2
profile. It needs a push before `gh workflow run editor-smoke.yml` can start it. The release workflow's smoke step stays
`continue-on-error` until that has passed on the runner.

Also found: the release workflow chose the newest release by `^v\d`. The current latest release is tagged `0.1.3`
(without "v"), so editor-only refreshes went to `v0.1.2` (run 42125231662: `TAG: v0.1.2`). The filter is now `^v?\d`.

## "Save to mods folder..." in the installed and portable builds

Settings backed up to `build/issue-4-ci/settings.backup.json` (SHA256 `b2f0bb6f...56bc1e`). For each run, `modsDir` was
set to a scratch folder; `gameDir` stayed at the existing scratch play folder under `build/scratch`. dist was not
touched. `d1-mods-drive.mjs` is `d1-drive.mjs` with the Save As step replaced by a click on "Save to mods folder...": 14
PAL templates, `LEVEL1NA`, `build/ootf/cab_disc`, 23 levels, Convert, save, wait for the "Saved to" toast.

| Build | Convert | Save | Folder |
| --- | --- | --- | --- |
| installed (`DrakanEditor-Setup.exe /S /D=build/issue-4-ci/installed`) | 101.4 s | 32.7 s | `mods-installed/ootf-levels` |
| portable (`DrakanEditor-Portable.zip`) | 100.6 s | 31.6 s | `mods-portable/ootf-levels` |

`dircmp.py` against `build/issue-4-full/ootf-levels.zip`: both folders have 10,559 files and 307,979,061 bytes,
with no files missing or extra and no file differing (`cmp-installed.txt`, `cmp-portable.txt`). Both have the same tree
hash, `bb1c259d...`. The folder is named after the pack id
`ootf-levels`. `mods.ini`: in the empty folder it was created as `[mods]` / `ootf-levels = on`. In the portable folder
it was seeded with a comment and `other-pack = off`, and both were kept, with `ootf-levels = on` appended. There were no
console errors (`mods-*-run.drive.json`).

`uninstall.exe /S`: exit 0, the install folder, the uninstall key and both shortcuts are gone. Settings restored from the
backup: the SHA256 matches again, and `.window-state.json` is unchanged.

Bugs found and fixed (`editor/packages/app/src/modio.ts`, `editor/ootfimport.ts`, test `test/modio-save.test.ts`):

- A failed "Save to mods folder..." was silent: the button had no rejection handler, so a shell error such as "No game
  folder is set" or a write error showed no toast. Both buttons of the dialog now use `toastSaveResult`, which reports the
  error.
- With a mods folder chosen but no game folder, the desktop write first asked for a game folder it did not need (the
  shell writes to `modsDir` when it is set). It now asks only when neither is known (`knownModsFolder`).

## Not checked

- The smoke test and `editor-smoke.yml` on the hosted runner (they need a push, then `gh workflow run editor-smoke.yml`).
- The two fixes in a packaged exe: the verified release exes predate them. Only the error and no-game-folder paths
  changed, and those are covered by unit tests.
- The toast path mixes separators (`.../mods-installed\ootf-levels`) when `modsDir` is stored with forward slashes. This
  is cosmetic.
