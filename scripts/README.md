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
- Load times: `harness/play_run.py --timestamps --env DRAKAN_IO_STATS=1 --env DRAKAN_LIFECYCLE_LOG=1` then
  `perf/load_phases.py <run> [...]` (start-up parts, every loading screen, level load / save / restore, exit; medians
  over several runs and `--label` groups for alternating A/B runs). Hitches: `--env DRAKAN_FRAME_AUDIT=2` with
  `harness/recipes/perf_walk.txt` (any level) or `perf_walk_surdana.txt`, then `perf/hitch_survey.py <run>` (frames
  over 2x and 4x the median with the cause the log shows). `perf/evict_file_cache.py <files or folders>` drops files
  from the Windows file cache for a cold run. Commands and caveats: `docs/journal/2026-11-12/perf-loading-hitches.md`.
- Perf: `perf/perf_run.ps1` (timed run), `perf/perf_stab.ps1` (freeze check), `perf/perf_summary.py`,
  `perf/runtime_perf_summary.py`, `perf/pacing_summary.py`, `perf/hostprof*.py` (resolve `hostprof.csv` against a linker `.map`).
  `perf/software_frame_report.py <receipt.json>` checks a measured Linux/browser gameplay window
  for frame throughput, interval deadlines, unique texture uploads and game-clock speed. It exits
  nonzero for failed or incomplete evidence; a pass applies only to the recorded scene.
  `perf/cpu_scene_bench.py --exe <exe> --scene <name> --variant A:KEY=VALUE --variant B --repeats 3` runs gameplay
  scenes interleaved (A B A B ...) through `play_run.py` and prints, per variant, world fps, the frame interval
  distribution and CPU milliseconds per world frame (EE thread, VU1 worker, all others); `--summarise <folder>` prints
  the tables of an earlier run again (`docs/reference/perf.md`, "CPU side of a gameplay frame").
  `perf/gs_replay_ab.py --replay <gsr_replay.exe> --capture <gscap.bin> --a KEY=VALUE [--a-replay <older exe>]`
  replays a recorded GS stream (`PS2X_GS_CAPTURE`) through the renderer alone, alternating two environments or two
  builds, and prints renderer thread CPU per frame, counters and whether every hashed frame is the same picture
  (`docs/reference/perf.md`, "Render target refreshes").

## Notes

- The perf summarisers overlap but are not merged: `perf_summary.py` averages `[perf N]` lines (`PS2X_PERF=1`) per scene;
  `runtime_perf_summary.py` selects the last windows with worldUpdates >= 20 and weights by elapsed time, summarising backend profiles separately;
  `pacing_summary.py` summarises pacing logs privately; `frame_rate_report.py` compares two harness runs. All four parse
  overlapping `worldUpdates`/vblank counters.
- `stab_matrix.sh` (the first matrix runner) is archived; `stab_matrix2.sh` supersedes it (per-boot verdicts, slot isolation).
- Machine-local layout assumed by the harnesses: `iso/SCES_500.06`, saves under `iso/mc0`, dev trees in `build/recomp_*`. The game's card is `<folder of the exe>/mc0` (not the ELF's folder any more); a run without `DRAKAN_MEMCARD_DIR` copies the save folders that card lacks from `<elf dir>/mc0`.

### Disc settings and camera checks

`shadow_review.py` reviews issue #1 using the reporter's logged configuration and fresh New Game cards. It launches
only `dist/DrakanPC/drakan.exe`; it creates new output folders and isolates the main, input and native-options INIs.
Empty per-run mods and developer-overrides folders prevent local packs from changing the test geometry.
The checked-in `shadow_review_profile.json` separates log/body facts from unknown settings and screenshot timing.
The default case retains water/reflections **on** and ray shadows **off**, as the reporter's first three ray passes
actually logged. The screenshot toggle state and exact location remain unknown.

```sh
python scripts/harness/shadow_review.py --dry-run --case observed-baseline --case both --backend both
python scripts/harness/shadow_review.py --name issue1-baseline --case observed-baseline --backend d3d11
python scripts/harness/shadow_review.py --name issue1-direct --case direct --backend webgpu
python scripts/harness/shadow_review.py --dry-run --all-cases --backend both --scene surdana --scene shadowmire
```

Select cases individually to keep runs bounded: `observed-baseline` / `native-off` isolate native projection with
ray shadows off, `both` / `ray-only` add modern shadows, and `ao` / `direct` / `receiver` separate modern shading.
All retain modern water and reflections. The fresh-card camera sweep does not require a save or reproduce an
unknown reporter location. `--language english` follows the reporter's observed English resources; default German
follows their requested setting. `--windowed` records a departure from their borderless presentation.

`manifest.json` records known facts, assumptions, requested environment, executable hash and local host.
Each case writes logs, camera shots, a request and `report.json`, with executable hashes before and after each run
and actual PNG dimensions (borderless presentation uses the local monitor). Texture upscaling is explicitly 1x/off.
DXR additionally attempts paired scene, `.models.csv` source metadata and temporal dumps at `--dump-epoch`
(default 1000). Early epoch 200 preceded gameplay in the German profile smoke. Model identity must match the
post-load resident-model level, camera X/Z and depth dimensions must agree, and the selected epoch must appear
after target load; raw depth has no independent epoch tag. WebGPU lacks equivalent scene/model/depth dumps and
reflection-enable receipts; both backends log applied diagnostic masks after successful submission. Reports
retain gaps instead of inferring activation from environment alone. A case's `evidence_complete`
means its **backend-supported** execution receipts are complete; inspect `unverified_configuration` as well.
Shader diagnostic cases without a mask receipt remain incomplete. Clean exits, configuration receipts and images
never establish a visual fix: `visual_review` remains pending. Nonzero exit means incomplete execution/evidence.
Existing run folders are refused. Timeouts use process termination only after the scripted quit/watchdog did not
finish, and are always classified as failed execution. The full matrix is opt-in.
The 30-second post-control recipe leaves time for epoch 1000 after the camera sweep. Selected German DX11 baseline
and receiver, German WebGPU direct, and English DX11 observed-baseline smokes were validated on 2026-11-04;
execution/configuration evidence passed, visual defect attribution remains pending (see the dated issue-1 journal).

`python scripts/harness/settings_check.py --region usa --mode title` checks title F1, Apply persistence and
Options > PC Settings. `--mode gameplay --resume 0|1` checks native pause, overlay input isolation and pause
ownership. `--mode camera --resume 1` measures direct yaw/pitch, idle persistence and sensitivity reload.
Use `--region pal` for the same regression coverage. Outputs live under `build/harness/<region>-settings-*`.
The runner isolates both INIs and the memory card, uses `play_run` plus scripted input/RAM dumps, and drives the
real overlay hotkey/Apply paths through the opt-in `DRAKAN_OVERLAY_SHOT_DIR` inbox. No desktop input is sent.

### Disc combat and flight checks

`disc_combat_check.py` uses the deployed executable, isolated INIs/cards and `play_run`. It refuses a card with
another release's folder prefix, hashes the source card before/after, and writes RAM samples and `report.json`
under a new `build/harness/<name>`. A completed script alone is not a combat/flight pass: select `--check` to
assert the corresponding behavior. `--check observe` only captures evidence.

```sh
python scripts/harness/disc_combat_check.py --region usa --save <native-USA-mc0> --recipe scripts/harness/recipes/combat_encounter_melee.txt --check melee --name usa-melee
python scripts/harness/disc_combat_check.py --region usa --save <native-USA-mc0> --recipe scripts/harness/recipes/disc_movement.txt --check movement --name usa-movement
python scripts/harness/disc_combat_check.py --region usa --level "Level 4A - Ravenshold" --recipe scripts/harness/recipes/disc_flight.txt --check flight --name usa-flight --timeout 300
```

The melee recipe needs a native save near enemies. `combat_save_fixture.py SOURCE NEW_DIR X Y Z --region=usa`
copies the source card and relocates Rynn/nearby cameras using profile serialization classes; it never converts
PAL saves into USA saves. Its older, SHA-pinned forced-dragon fixture options do not establish flight readiness.
The flight check uses the level's native mounted start, measures movement/yaw/pitch and verifies attack requests
release; it does not claim breath damage or landing. All release-specific observation offsets are profile data.
Use `--region pal` for the same checks with a native PAL card. Combat timing and damage need not be identical.

`directional_attack_check.py --save <native-PAL-mc0> --name directional --timeout 300` compares neutral attack,
arrow+attack and WASD+attack through the actual root/spine skeleton animation channels. Use `--direction up`
for a bounded first trial. The source card is copied; a melee weapon must be available through Quick Inventory
(the recipe taps Q). The check requires a distinct directional arrow clip, the same advancing attack clip through
WASD, native attack release and subsequent movement/release. Reports include the executable SHA-256, channel
clips/times, screenshots and preserved source-card hashes; completed input scripts alone cannot pass.
`--analyze <existing-run-folder>` re-evaluates captured RAM without launching another game.

`settings_check.py --region usa --mode gameplay --resume 1 --level "Level 4A - Ravenshold" --name usa-flight-settings --timeout 300`
extends the existing pause/settings ownership check to mounted flight. `--name` preserves earlier named evidence;
`--timeout` permits a complete native introduction before control becomes available. Neither harness uses desktop input.

Forward-jump regression: run `harness/play_run.py --name forward-jump --recipe scripts/harness/recipes/forward_jump.txt
--env DRAKAN_GAME=d2`, then `python scripts/harness/jump_check.py build/harness/forward-jump`. Repeat the runner with
`--env DRAKAN_WORLD_RATE=30`, `60` and `uncapped`; checks require actual run/walk travel and a stationary jump.

## savetool

`savetool/savetool.py` inspects and edits native `.rsg` saves (SRSC containers, stdlib only). Run it on a copy;
it never modifies an input unless `--in-place` is given, and it keeps the previous file as `NAME.bak` (or `NAME.bak.1`, ...).

```sh
python scripts/savetool/savetool.py info  <save.rsg>                       # entries table
python scripts/savetool/savetool.py get   <save.rsg> hp                   # also gold, pos, skill.N, skill.N.max
python scripts/savetool/savetool.py set   <save.rsg> gold 9999 -o <out.rsg>
python scripts/savetool/savetool.py set   <save.rsg> pos 1,2,3 --in-place
python scripts/savetool/savetool.py dump  <save.rsg> <dir>                # inflated entries + manifest.json
python scripts/savetool/savetool.py pack  <dir> -o <out.rsg>              # byte-identical when unedited
python scripts/savetool/savetool.py fields                                # field table with confidence notes
```

`hp` and `gold` are pinned to the record entry ids where they were observed (`--force` overrides, with the risk
stated in `savetool fields`). Field evidence and open questions: `docs/journal/2026-10-31/save-editor.md`.
Tests: `scripts/tests/test_savetool.py` (synthetic saves only).
