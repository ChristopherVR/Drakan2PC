# DrakanPC-Setup.exe for players with their own ISO

`python scripts/buildtool/package_builder_kit.py --installer` builds `build/builder-kit/DrakanPC-Setup-<commit>.exe`
(142 MiB at ee60fc5). See BUILDING.md, "Setup .exe / builder kit".

Verified on this PC (VS 2022 already installed, so the Build Tools download path was not exercised):

- Silent install (`/VERYSILENT /DIR=... /TASKS="" /ISO=...`) to `build/setup-test`; installed tree has `tools/pwsh`,
  `tools/python`, `deps/raylib`, the FFmpeg zip and `scripts/buildtool/ps2_recomp.exe`.
- With PATH reduced to `System32;Windows;Wbem` (no Git, no Python): `Build-Drakan.cmd -CheckOnly` used the bundled
  PowerShell and Python and reported the sources as bundled.
- Bundled pwsh + Python: `Build-LocalGame.ps1 -SourceOnly` prepared the ISO, recompiled with the prebuilt
  `ps2_recomp.exe` and generated the VU image.
- `cmake --preset game` configured offline from `deps/raylib`; `--target ffmpeg_external` verified the pinned SHA-256
  and extracted the bundled zip.
- Silent uninstall removed the install folder completely, including the disc-generated `ps2_vu1_image.inc` and
  `build/`.

Not verified: the full game compile from the setup, and the VS Build Tools auto-install on a PC without Visual Studio.

Problems found on the way:

- A silent install hung: the ISO page's `NextButtonClick` check showed a (suppressed) message box and returned False
  forever. Silent installs now skip the check when no `/ISO=` is given (the build is `skipifsilent`).
- Inno Setup 6.7 names the page type `TInputFileWizardPage` (not `TInputFilePage`).
- An earlier full build from the zip kit (`build/kit-test`, 2 projects x 4 compiler processes, 61 GB RAM) stopped with
  `MSB6006: "CL.exe" exited with code 1` on `ps2_runtime.vcxproj` (VU1 AOT files) and no compiler diagnostic. Cause not
  determined; the same sources build in `build/final-game`. `Build-Drakan.ps1` now retries a failed build once with
  `-Jobs 1` and half the compiler processes.

## VU1 AOT slices exhaust memory on player PCs (fixed in 4bba3fe)

A tester (31 GB, Build Tools freshly installed by the setup) sent a log that ends while compiling
`ps2_vu1_aot_3.cpp`, the same place the `build/kit-test` build died. Compiled alone (same cl command line as
`build/final-game`), `ps2_vu1_aot_3.cpp` takes 591 s and peaks at 16.6 GB. The four slices ran side by side in one
`/MP` batch (~70 GB), so the PC swapped until the build looked frozen, or cl died with exit code 1 and no diagnostic.

Fix: `PS2X_VU1_AOT_SERIAL` gives each slice its own `PS2X_VU1_AOT_SLICE=N` define, which puts it in its own MSBuild CL
batch; MSBuild runs the batches one after another. A four-file experiment (60M-step constexpr per file, `/MP`, `/m:1`)
showed all four .obj written within 1.3 s without the defines (116 s build) and about 2 min apart with them (436 s).
Player builds (`Build-Drakan.ps1` -> `Build-LocalGame.ps1 -SerialVuSlices`) always set it; developer builds keep the
parallel default.

Check: `ps2_runtime` built with the option ON, `/m:1`, `_CL_=/MP4`: 25 min, exit 0, peak combined cl working set
17.4 GB (sampled every 2 s), slices compiled in order 0-3.

Follow-up: every slice re-evaluates `Vu1Blocks::build` over the whole image in constant evaluation; computing that
table once would likely cut both the memory and the 4 x 10 min.
