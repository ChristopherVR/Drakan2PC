# Issue 4: D1 import dropdown and export in the packaged portable editor

Follow-up of issue 1 (`docs/journal/2026-11-05/issue-1-template-dropdown.md`, `issue-1-d1-loading.md`): both fixes had
only been checked in Chromium or through the CLI pipeline.

## Package

`DrakanEditor-Portable.zip` is the release exe zipped on its own (`.github/workflows/editor-release.yml`, step "Build the
Drakan Editor"). Built the same way locally: `npx tauri build --no-bundle` in `editor/packages/desktop` (release
profile, frontend from `scripts/prepare.mjs` + `npm run build -w @drakan/editor-app`), then the exe zipped to the ignored
`build/issue-4/DrakanEditor-Portable.zip` and run from its extracted copy in `build/issue-4/portable`. The working tree
held other sessions' uncommitted editor and formats changes, so the build included them.

Settings (`%APPDATA%/dev.drakan.editor/settings.json`) had `gameDir` pointed at the scratch `build/issue-4/play` for the
run and were restored afterwards. The app reopened the PAL ISO from its recent list. The exe was started with
`WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--remote-debugging-port=9333` (WebView2 156, Edg UA, `__TAURI_INTERNALS__`
present) and driven over CDP (Playwright `connectOverCDP` plus raw `Runtime.evaluate`).

## Template Level dropdown (native)

Passes in WebView2, no change needed:

- The popup is a descendant of the modal `dialog.ootf-dlg` (`:modal` true) and paints above it: 11 of the 14 PAL level
  options lie below the dialog's bottom edge and every visible option passes an `elementFromPoint` hit test.
- CDP mouse: the click opened it and selecting "Level 6 - Valley of the Fallen" set `LEVEL6EN`. CDP keyboard: ArrowDown
  opened it, ArrowUp twice + Enter picked "Level 4B", Escape closed only the popup and focus went back to the trigger.
- Real OS input (user32 `SendInput`-style `mouse_event` / `keybd_event` into the WebView2 render widget, per-monitor DPI
  aware; the recorder showed `isTrusted` events at the intended CSS coordinates): a click on the trigger opened it and a
  click on "Level 8 - Stratos", 241 px below the dialog, selected `LEVEL8OS`. Then Down, Up x5 + Enter selected
  "Level 3B - Crystal Mine" (`LEVEL3NE`), and Escape closed the popup with the dialog still open.
- A harness pitfall, not a defect: with the window restored to 958x550, the popup scrolled so that an option's
  `getBoundingClientRect` lay above the list's visible area, and a click there landed on the dialog body. Clicks on
  shown options were correct.

New automated coverage: `npm run smoke:webview -w @drakan/desktop` (`editor/packages/desktop/scripts/webview-smoke.mjs`)
starts the built exe with remote debugging and checks, over plain CDP (`Input.dispatchMouseEvent` /
`dispatchKeyEvent`), that the dialog is modal, the popup opens inside it, options below the dialog take hits, mouse and
keyboard selection work, Escape keeps the dialog, and closing it leaves no popup. Without a disc it fills the list with
synthetic names. All 9 checks pass locally. It is not wired into CI.

## D1 export (native)

Drove the real dialog: template "Level 1A - Surdana" (`LEVEL1NA`, the CLI default) picked through the dropdown, install
folder `build/ootf/cab_disc` given to the `webkitdirectory` input (`setInputFiles` with the folder: 23 levels listed),
"intro level" and "Ruined Village" ticked, Convert. The conversion took 22-28 s with every stage ok
(OotF 00 - Intro 102/102 objects, OotF 01 - Ruined Village 1858/1858).

### Defects found and fixed

1. **Download mod (.zip) bypassed the desktop save dialog.** A plain `<a download>` in WebView2 saved straight to
   `%USERPROFILE%\Downloads\ootf-levels.zip` with no dialog and no message (it worked only because the WebView2 default
   is to allow it). The rest of the desktop app uses `platform.downloadFile` (native Save As through `file_save_as`).
   The dialog now uses it too and toasts the path. Verified natively: the "Export" Save As window opened, the typed
   path was written, and the status bar showed "Saved to ...". The browser build still downloads.
2. **The editor pack differed from the CLI pack.** Same levels and template: editor 6,340 files / 115,138,868 bytes,
   CLI (`editor/scripts/ootf-convert.ts` on `build/odbx/data`) 4,636 files / 105,829,925 bytes. A Node probe building the
   editor's exact `ootfDiscContext` from the ISO matched the packaged export byte for byte, so WebView2 itself added
   nothing. The causes were all in the dialog's context and options:
   - `sounds` is opt-in in `convertOotfLevels` (`opt.sounds === true`). The CLI enables it, the dialog did not, so
     the editor pack had none of the 306 `new/resoures.sdb` sounds and its cinematics dropped their sound actions
     ("no compatible resource mapping"). Fixed with `OOTF_DIALOG_OPTIONS = { sounds: true }`.
   - No `xdbFiles`: FX classes got text ids although the disc has no `FX.XDB`, which produced `new/fx.xdb` entries.
     The context now lists the XDBs of the template's dependencies.
   - `hostFiles` covered every dependency with a MOD/TXD. The editor therefore hosted a converted model in a
     replaced `Common/System/System.mod`/`.txd`. It is now limited to the CLI's LEVEL1ON, WORLDCON, FX and MOUNTAON.
   - `mods`/`adbs` covered every dependency, RESOURES (the host) included. As a result Rynn was retargeted: 75 PS2
     clips of `SYSTEM.ADB` were replaced, with 1,854 `new/system.adb` entries and 151 overrides. They now hold only
     the creature container NPCS, as in the CLI.

   After the fix, the Node probe and a fresh CLI run on the same tree: 4,636 files each, identical except `mod.toml`. The
   dialog's `mod.toml` adds `description` and `campaign_start = "OotF 00 - Intro"`, which is intended. Rebuilt the
   portable exe and repeated the native export. "Save to mods folder..." (desktop `mods_write_pack`, enabled in
   `mods.ini`) and the Save As zip both match that probe byte for byte (105,830,026 bytes).
   `test/ootfdisc.test.ts` now asserts the host, creature and XDB sets and the sounds option.

## In game

No `drakan.exe` was running. Ran the deployed `dist/DrakanPC/drakan.exe` (SHA256
`F4A3FF9088A3038AE5EA38A5DB30028625D087BDDC61D681E7FB31C21370E61C`, not rebuilt) with private ini/card/log copies under
`build/issue-4/run`, `game = ask`, and `DRAKAN_MODS` pointing at the pack that the packaged editor saved to
`build/issue-4/play/mods`. dist was not touched. The recipe was the one from issue 1: wait for title screen 8, pick the
Order of the Flame card, New Game, a walk after the intro. The ini language was German. Results:

- Clean scripted quit (exit 0). Logs: `[game] switched to ootf`, `New Game of 'ootf' opens 'OotF 00 - Intro'`, Intro and
  Ruined Village loaded, no null-target jump. The mod mount reports
  `RESOURES.SDB rebuilt with 306 entry patch(es)` (the sounds that were missing before).
- Screenshots show the game card with "Drakan: Order of the Flame" and, after the automatic Intro handoff, Rynn and a
  textured forest in Ruined Village (`after_intro.png`). Movement was not measured this time (no RAM decode), and
  sound playback was not listened to (audio off).

## Not checked

- An installer (NSIS) build and the zip produced by CI itself. Only the local exe that the same steps produce was
  tested.
- The full 23-level campaign through the dialog, and a non-English D1 install.
- Whether the broader context the editor used before (Rynn retarget, SYSTEM hosting) would work in game. It is now
  excluded to match the verified CLI.
