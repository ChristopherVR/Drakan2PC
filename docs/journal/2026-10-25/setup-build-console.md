# Quieter player build console with progress

A tester on a PC without Visual Studio sent console output from `Build-Drakan.cmd` that was screens of
`warning C4127: conditional expression is constant` (each with its structured-output "compiling source file" /
"consider using 'if constexpr'" lines), `C4702` and `C4324`, all from `/W4` on our own runtime sources. It gave no
sense of how far along the one-to-two-hour build was.

Changes (player path only; developer builds are unchanged):

- `DRAKAN_COMPILER_WARNINGS` (CMake, default ON) in `cmake/CompilerOptions.cmake`; OFF gives `drakan::options` `/W0`
  (`-w` elsewhere). Checked: `runtime/input` configured with OFF writes `<WarningLevel>TurnOffAllWarnings`, ON
  `Level4`.
- `Build-LocalGame.ps1 -NoWarnings` passes it. `Build-Drakan.ps1` sets it only while `build/game/runtime-build` has no
  `CMakeCache.txt`: switching it on a configured tree changes every runtime command line and would recompile all of
  it, including the four ~10-minute VU1 slices.
- `Build-LocalGame.ps1 -Progress` (always set by `Build-Drakan.ps1`): numbered steps (`[1/6] Reading the game from
  the ISO` ... `[6/6] Setting up the play folder`), a `Write-Progress` bar with the elapsed time updated every second
  (so the long VU1 slices do not look frozen) and the window title showing the percentage, one `[n/N] file.cpp` line
  per compiled file (MSBuild minimal verbosity prints each source as cl starts it; the VU1 slices get an "about 10
  minutes" note). N is the `ClCompile` count of `ps2EntryRunner.vcxproj` and its `ProjectReference` closure (299 in
  `build/final-game`; counting every vcxproj would include ~150 raylib example projects). The tools' output goes to
  `build/game-output.log`; errors (`error XNNNN:`, `CMake Error`) are shown live, and a failure prints the last 40
  lines and the stderr tail. Developer details (NVIDIA options, SHA-256, launch command, missing subtitles) go to the
  log too.

Checked: the runner against a fake MSBuild that prints the tester's warning lines then fails (only `[n/N]` lines, the
error and the tail shown), and a real `cmake --build` of `runtime/input` with `/MP4 /m:2` (16 of 17 counted files
printed in order, no warnings in the log). Not run: a full player build from the setup.
