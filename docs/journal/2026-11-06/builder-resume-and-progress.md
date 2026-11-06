# Player builder: resume review, compile progress, play folder

Report: the compile step printed `[   1] context.c` (padded counter, no total), there was no way to choose where the
game goes, the installer had few options, long files gave no sign of progress, and "Rebuild Drakan" should say where
the last build stopped, check those files and continue from there. Also asked: can the build be faster on lower-end
PCs?

## Console

- The padding came from `"{0,4}"`; the missing total from `Build-LocalGame.ps1` counting sources only on a full build,
  so every `-Incremental` run (Rebuild Drakan, and the automatic retry) printed bare numbers. The `context.c` ...
  lines are GLFW, i.e. a rebuild of a folder whose raylib had not finished compiling.
- `Build-Progress.ps1` (dot-sourced by both builders) now reads the compile plan from the vcxproj closure of
  `ps2EntryRunner` with each source's object file (IntDir, or the item's `ObjectFileName` for same-named sources;
  all 368 objects of `build/final-game` resolved). The counter's N is the number of sources whose object is missing,
  empty or older than the source: everything left after an interrupted build, the edited files after a fix. Counter
  format `[12/299]`, `[12]` when N is unknown or passed (header edits rebuild more than the estimate).
- Slow files: a second progress bar for the longest-running file, a `still compiling X, 6 min so far (usually about
  10 min)` line every 3 minutes, and `finished X after 11 min` for files over a minute. Finished means the object is
  non-empty and written after the file started: measured on a VU1 slice, cl creates a 0-byte `.obj` about 8 s after
  it starts and fills it in at the end, so existence alone would have been wrong. Files without a known object count
  as finished once more than Jobs x CompilerProcesses are running. Times over a minute are kept in
  `<WorkDir>/compile-times.json` and replace the built-in 10-minute VU1 estimate on later builds.
- Checked with `Invoke-Logged` extracted from the script and a fake MSBuild (three files, one slow): unpadded
  `[1/3]` lines, one "still compiling" and one "finished after" line, the time saved.

## Resume

`Build-LocalGame.ps1` writes `<WorkDir>/build-state.json` (step started, `running` / `failed` + error / `done`).
`Build-Drakan.ps1` prints it with a per-step check of the files on disk and continues from the first incomplete step
(Enter) or starts over (`S`, `-Fresh`); a folder whose ISO files are incomplete starts over by itself, because
`prepare_iso.py --refresh-config` cannot repair it. Continuing was already mostly how `-Incremental` behaves (steps 1,
3 and 4 are cheap re-runs, MSBuild keeps finished objects); one gap is closed: `recompile.stamp` is deleted before the
recompiler runs, so a translation interrupted halfway is redone instead of being trusted on the next run. A region
change now also goes through the start-over path instead of deleting the folder before `-CheckOnly` returns.

## Installer

- `DisableDirPage=no`: Inno hides the folder page on a reinstall by default.
- New **Play folder** page (default `{app}\dist\DrakanPC`, `/PLAYDIR=`, remembered with `SetPreviousData`). The shortcuts
  point at it; `Build-Drakan.ps1 -DistDirectory` remembers it in `build/build-options.json` for Rebuild Drakan
  (`default` resets). Refused: a drive root, the builder folder, anything under its `build\`.
- New option **Start the game when the build has finished** (`-LaunchWhenDone`).
- `ISCC` compiled the script against a stub stage directory.

## Faster on lower-end PCs: measured, not changed

Single-file compiles of the VU1 AOT code with the builder's flags (9800X3D, 16 threads, 62 GB):

| Pairs compiled (from pair 381) | Time | Peak working set |
| --- | --- | --- |
| 1 | 0.17 min | 1.5 GB |
| 190 | 4.2 min | 6.6 GB |
| 381 (all of `ps2_vu1_aot_1.cpp`) | 5.2 min | 7.2 GB |

Earlier: slice 0 11.7 min / 12.4 GB, slice 3 16.6 GB ([issue-1-kit-build-parallelism.md](issue-1-kit-build-parallelism.md)).
The constant-evaluated tables cost about 10 s, so the slice count is not the lever: splitting into more slices
would repeat that work and keep the heavy blocks just as heavy. The cost sits in a few large `blockFn<B>` bodies
(`PS2_VU_FORCEINLINE` per pair, packet trace and native loops in slice 0). Making those cheaper to compile (fewer
forced inlines in the largest blocks, or a lower optimisation level for them) is a change to the VU1 hot path and
needs a frame-rate A/B before it ships; it is the one change that would lower the 17 GB peak that makes 16 GB PCs page.
Nothing else in the build is close: the 83 generated files peak at 1.3 GB per compiler process.

What this change does for slower PCs is not lose work: an interrupted or out-of-memory build continues with every
finished object kept and says how far it is.

## Tests

`scripts/tests/test_build_progress.py`: counter and duration formats, estimates, compile plan through a project
reference with a per-item `ObjectFileName`, pending objects (missing, empty, stale), the tracker (an empty
placeholder does not finish a file; slot fallback), the review of an interrupted and a half-translated folder, and
`build-state.json`. Not run: a full player build from the setup.
