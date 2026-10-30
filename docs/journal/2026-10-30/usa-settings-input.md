# USA PC settings and input/pause integration

## Changes

Moved settings/pause differences into disc profiles: `InputFlowLayout`, `SettingsLayout`, language count,
explicit global/function mappings, and validated `InputFlow`/`OptionsSettings` capabilities. The shared pure
pause policy covers native START, delayed opening, timeout, cancellation on scripted input, and optional resume
only for pauses initiated by the overlay. PAL's desktop and portable input paths use that policy too.

The basic input backend now hooks the native PadMgr frame boundary, replacing the former 8 ms heuristic in
scePadRead. It publishes flow, handles host/focus holds, selects menu cursor context, and suspends pad controls
while settings is open. Test autopress is suppressed while the overlay is open. The backend honors the same
input INI override as the settings host. A module-level direct-look capability survives every settings reload.

The settings schema shows right-stick emulation on basic profiles. Shared host capability filtering disables
settings that require unported hooks, including advanced command/quick-slot binding rows. The Options handler translates through the profile and repurposes only
Adjust Screen on USA; PAL-only native autosave suppression/close bypass remains gated by `Menus`.

## Evidence from owned discs (no executable content retained)

- PadMgr_Update: PAL `0x1e3970` -> USA `0x1e38b8`, same 1544-byte normalized body.
- Options handler: PAL `0x4424e0` -> USA `0x441e08`; bodies differ, but widget access remains a halfword at +0x14.
- Both discs' layout resource 0x2340 contains Adjust Screen widget 0x2345/property 116. USA INTERFCE.XDB entry
  156 is Adjust Screen. The interception is before retail dispatch and other events use the original handler.
- Active screen pointer: `0x4fb60c` -> `0x4fc48c`; scripted-input flag: `0x4fbafc` -> `0x4fc97c`;
  context pointer: `0x5565d4` -> `0x555a54`, confirmed by unconflicted data-reference mappings.
- Screen factories `0x2c4458` / `0x2c4390`, tables `0x52a020` / `0x5293a0`: each interactive
  menu ID in the profile calls the mapped constructor at the same ID. Pause remains 0x17 (constructors
  `0x4326a0` / `0x432478`); title remains 8. The full-text cursor screen 0x10 also keeps its ID.
- Camera_UpdateLook uses context +0x4b4 in both bodies (`0x44e4a8` / `0x44dc88`, +0x64 load). The previously
  recorded +0x4b4 -> +0x4b0 shifts belong to other structures, not this context field.
- The flow reader bounds every pointer access. It uses camera modes 1/6 and scripted/screen state, and deliberately
  does not use `0x4fb730`: this is the Auto Mount Dragon option, not a general gameplay-enabled flag.

## Validation and limitations

154 Python tests passed. Profile tests exercise both discs with synthetic RAM, invalid pointers, menu masks,
native pause/open/resume, duplicate requests, timeout and cutscene cancellation. Input tests cover retaining
stick mode across reload, injected START, overlay neutralization and release. Schema tests cover effective look
mode and unavailable controls. The final focused rerun passed all eight selected input/settings/profile/display tests.

The rebuilt USA player reached the native title screen from `dist/DrakanPC-usa`, with isolated INI files and
memory card. Diagnostics reported `screen 8, camera 0, scripted 0, title seen 1`; the settings host initialized.
The user stopped computer use with physical Escape before the F1 action completed. No further interactive
input was sent. The owned smoke process was then stopped. Live gameplay F1, Options entry, closing/resuming,
focus pause and applying/reloading settings are **not verified by this run**. No new PAL interactive run.

Logs: `build/usa-settings-final-build.log`, `build/pal-settings-build.log`, `build/usa-settings-ctest.log`,
`build/usa-settings-python-tests.log`, and `build/harness/usa-settings-live/`. Native captures and game assets stay ignored.

The full runtime ctest run covered 79 tests: 3 data/interactive-dependent skips, with two initial failures.
The schema test was corrected to inspect the selected choice rather than an unused label field and passed
on rerun. The unrelated synthetic renderer test was stopped after 387 seconds; a bounded rerun passed in
209 seconds. Together with focused reruns, all 76 runnable runtime tests passed. Ruff, clang-format, diff
whitespace checks and tracked/untracked repository content audits passed.

Both final native builds completed through the standard incremental builder and deployed successfully:

- PAL: SHA-256 `e52f32f243b3bf9f2a8ebfb2332892bcc1f9c298731b40e958b05f5c77ea1418`.
- USA: SHA-256 `ca2e54f31227c7034623acede67ea3143fe95e117aa56ad85bbff19829291b92`.

The final PAL sync build is in `build/pal-settings-final-build.log`. The title smoke preceded final pointer-bound
and capability-filter refinements; the final executables have build/test coverage, not a new interactive run.
