# Issue 4 follow-up: release builds and the full D1 campaign through the packaged editor

Follow-up of `issue-4-portable-editor.md`, which tested only a local `tauri build --no-bundle` exe and two levels.

## CI replay

`build/issue-4-release/ci-build.ps1` (ignored) replays the `build` job of `.github/workflows/editor-release.yml` up to the
upload, on a clean export of HEAD `e435da85` plus the pinned `editor/packages/formats` commit `4fa08e5b` (`git archive`,
so no untracked or uncommitted files from other sessions): `npm ci`, `npm test` (97 files, 990 tests), `npm run build`,
the version stamp (`0.1.344`), `npx tauri build`, the save editor build, `EDITOR-SHA256SUMS.txt`. Output in
`build/issue-4-release/out`, log `ci-build.log`.

Differences from CI: Windows 11 instead of the `windows-2022` image, Node 24 instead of 22, local stable Rust 1.99, no
release lookup or upload. The stamped `tauri.conf.json` parses equal to the committed one apart from `version`, so the
`ConvertFrom-Json`/`ConvertTo-Json` round trip loses nothing.

Findings:

- The installer's `drakan-editor-desktop.exe` and the portable zip's differ in 3 bytes: Tauri's bundle-type marker,
  NSIS in the installed copy and "unknown" in the portable one. Expected.
- The portable step picked "the newest `*.exe` in `target/release`". On a fresh runner that is the editor, but with a
  reused target dir it can be the save editor. The step now zips `drakan-editor-desktop.exe` by name.
- Two exports of the same levels are not byte-identical zips: entries carry the export time. Their contents are.

## Installer

`DrakanEditor-Setup.exe /S /D=build/issue-4-release/installed`: exit 0, exe + `uninstall.exe`, HKCU uninstall entry
"Drakan Editor 0.1.344", Start menu and desktop shortcuts. `uninstall.exe /S`: exit 0, folder, uninstall key and both
shortcuts gone. The WebView2 data in `%LOCALAPPDATA%/dev.drakan.editor` (378 files) and `settings.json` were left alone.

## Smoke and import dialog, installed and portable

`npm run smoke:webview -w @drakan/desktop -- --exe <exe>`: all 9 checks pass on the installed exe and on the exe extracted
from `DrakanEditor-Portable.zip` (`smoke-installed.log`, `smoke-portable.log`, PAL ISO open from the recent list). With
`recentIsos` emptied, as on a CI runner (settings backed up and restored, same SHA256), 5 of 5 runs passed
(`smoke-nodisc.log`). A relative `--exe` was resolved against the workspace instead of the caller's folder; the script now
uses `INIT_CWD`.

`build/issue-4-release/d1-drive.mjs` drives the real import dialog over CDP: 14 PAL templates, `LEVEL1NA` picked, the
install folder `build/ootf/cab_disc` given to the `webkitdirectory` input (`DOM.setFileInputFiles` with the folder),
"23 levels found", all ticked, Convert, then "Download mod (.zip)" and the native "Export" Save As window filled through
`save-as.ps1` (the dialog's controls show up only as UIA panes, so it sends `WM_SETTEXT` / `BM_CLICK`). The toast
"Saved to ..." appeared and no console errors were logged.

## Full campaign

| Build | Convert | Pack |
| --- | --- | --- |
| installed | 85.5 s | `build/issue-4-full/ootf-levels-installed.zip` |
| portable | 70.2 s | `build/issue-4-full/ootf-levels.zip` (SHA256 `6044fa9a...`) |
| CLI | 115 s total | `build/issue-4-full/cli/ootf-levels-cli.zip` |

The CLI run: `ootf-convert.ts --ootf build/ootf/cab_disc --ps2 build/odbx/data --template LEVEL1NA --campaign 1`
(`cli/conversion.log`). All three packs have 10,559 files and 307,979,061 / 307,978,960 bytes. File by file
(`build/issue-4-release/zipcmp.py`) the two editor packs are identical, and each differs from the CLI pack only in
`mod.toml`, where the editor adds `description` and `campaign_start = "OotF 00 - Intro"`. All 23 levels converted
with every stage ok (13 single-player levels, 10 multiplayer arenas; per-level object counts in
`ootf-levels.zip.drive.json`). `build/issue-4-full/READY.txt` names the portable pack for the in-game check, which was
not run here.

## CI

`editor-release.yml` now runs the smoke test after building the editor exe. It has not run on a hosted runner yet, so
the step is `continue-on-error` with a 5 minute limit and does not block the release; make it blocking once it has passed
there.

## Not checked

- "Save to mods folder..." in these builds (the earlier note checked it on the `--no-bundle` exe).
- The pack in game; a non-English D1 install.
