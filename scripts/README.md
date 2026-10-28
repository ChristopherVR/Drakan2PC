# scripts/

Everything here derives the repository root from its own location, so it can be run from any directory.
Generated output and game data live under `build/` and `iso/` (both gitignored); nothing here may contain
either.

| Path | What |
| --- | --- |
| `buildtool/` | Build entry points. `Build-LocalGame.ps1 -IsoPath <iso>` builds the whole game; `Build-Drakan.sh <iso> [--web]` bootstraps Linux/browser builds; `build_tool.py` builds only the recompiler. |
| `tests/` | Python tests and C++ policy tests (built by the root `CMakeLists.txt`). `python scripts/run_tests.py` runs all Python tests. |
| `launchers/` | Windows `Play*.cmd`, native Linux `Play-Drakan.sh`, and loopback browser `Play-Browser.sh` launchers. |
| `harness/` | Game-run harnesses (scripted input, save fixtures, RAM/audio/input reports) and the build helper. |
| `harness/recipes/` | Input-script `.txt` recipes consumed by the harness runners (`DRAKAN_INPUT_SCRIPT` format, `docs/reference/input.md`). |
| `harness/stability/` | Boot-matrix stability runner, verdicts, scenario generator, instruction-level difftest driver (`docs/reference/stability.md`). |
| `perf/` | Perf run scripts and log/profile summarisers (`docs/reference/perf.md`). |
| `assets/` | Asset helpers, e.g. `make_wide_backdrop.py` (wide menu backdrops from a 4:3 image, `docs/reference/widescreen.md`). |
| `archive/<date>/` | One-off investigation checks tied to a dated journal entry, grouped by the date they were first committed. Kept for reproducibility; not maintained. |
| top level | `repository_guard.py` (tracked-file audit, CI), `run_tests.py`, `prepare_local_runtime.py` (local ELF to runtime sources), `vu1_aot_image.py`. |

## Main entry points

- Documentation site: `python scripts/buildtool/build_pages.py` (install `docs/site/requirements.txt` first).
  Generates and validates `build/github-pages`; see `docs/reference/github-pages.md` for preview and publication.
- Build: `pwsh scripts/buildtool/Build-LocalGame.ps1 -IsoPath <iso>` (Windows), or `bash scripts/buildtool/Build-Drakan.sh <iso> [--web]` (Linux/browser). Windows browser builds use `-TargetPlatform web` through WSL.
  `pwsh scripts/harness/build_game.ps1 [-BuildDir build/recomp_combat] [-Gen] [-Jobs N] [-CompilerJobs N]`
  is the incremental rebuild of a private `build/recomp_*` dev tree (replaces the former
  `combat_build.ps1`, `combat_build.sh` and `perf_build.ps1`; `-Jobs 0 -CompilerJobs 0` is the old unbounded perf build).
- Scripted gameplay without a save: `python harness/play_run.py --name <run> [--level ravenshold] [--recipe <txt>]
  [--shots 0,300] [--env KEY=VALUE]` (New Game on an empty throwaway card, any level, renderer screenshots, output in
  `build/harness/<run>/`; `docs/reference/input.md` 13).
- Real-time video of a scripted run: `python harness/record_window.py --name <run> --recipe <txt> --seconds N --out <mp4>
  [--save <card>/mc0] [--env KEY=VALUE] [--ffmpeg <exe>]` (waits for the recipe's `shot rec_start`, then records the game
  window with FFmpeg gdigrab; used for the site clips, `docs/reference/github-pages.md`).
- Scripted gameplay from save fixtures: `harness/combat_run.sh <recipe> <name>` (bash), `harness/combat_diagnostic_run.ps1`,
  `harness/combat_encounter_run.ps1` (fresh save copies + audit), `harness/combat_quick_inventory_run.ps1`,
  `harness/combat_parry_run.ps1`, `harness/snd_test_run.sh` (sound diagnostics), `harness/combat_audio_verify.ps1` (bounded audio verification),
  `harness/dual_wield_check.py --save <card>/mc0` (dual wielding: equip, hand lines, mirrored strikes, blades, unequip;
  PASS / FAIL report, `docs/reference/combat-controls.md` "Dual wielding").
  `harness/dual_wield_transitions.py --save <magic5-card>/mc0` checks bow transitions, dropping either hand's weapon,
  and saving/loading dual-wield equipment (including an in-session reload); each run uses an isolated card.
  `harness/dual_wield_movement.py` and `harness/dual_wield_toggle.py` take a card already saved while dual wielding
  and check strafing attacks and live disable/re-enable without UI automation or modifying the player's INI.
  Reports: `harness/combat_ram_report.py`, `harness/combat_audio_report.py`, `harness/combat_trace_summary.py`, `harness/combat_parry_report.py`, `perf/frame_rate_report.py`.
- Stability: `harness/stability/stab_matrix2.sh`, `stab_experiments.sh`, `stab_audit.py`, `stab_difftest.ps1`, `stab_check_runtime.ps1`.
- Perf: `perf/perf_run.ps1` (timed run), `perf/perf_stab.ps1` (freeze check), `perf/perf_summary.py`,
  `perf/runtime_perf_summary.py`, `perf/pacing_summary.py`, `perf/hostprof*.py` (resolve `hostprof.csv` against a linker `.map`).
  `perf/software_frame_report.py <receipt.json>` checks a measured Linux/browser gameplay window
  for frame throughput, interval deadlines, unique texture uploads and game-clock speed. It exits
  nonzero for failed or incomplete evidence; a pass applies only to the recorded scene.

## Notes

- The perf summarisers overlap but are not merged: `perf_summary.py` averages `[perf N]` lines (`PS2X_PERF=1`) per scene;
  `runtime_perf_summary.py` selects the last windows with worldUpdates >= 20 and weights by elapsed time, summarising backend profiles separately;
  `pacing_summary.py` summarises pacing logs privately; `frame_rate_report.py` compares two harness runs. All four parse
  overlapping `worldUpdates`/vblank counters.
- `stab_matrix.sh` (the first matrix runner) is archived; `stab_matrix2.sh` supersedes it (per-boot verdicts, slot isolation).
- Machine-local layout assumed by the harnesses: `iso/SCES_500.06`, saves under `iso/mc0`, dev trees in `build/recomp_*`.
