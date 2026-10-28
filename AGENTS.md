# Drakan: The Ancients' Gates — PC port

Static recompilation of the PS2 game (PAL, SCES-500.06) to a native Windows executable.
The player supplies their own ISO. Native Windows is the first target, but code must
stay portable to clang / WebAssembly later (see `docs/reference/portability.md`).

## Layout

| Path | What |
| --- | --- |
| `ps2recomp/` | Our copy of PS2Recomp (forked from ran-j/PS2Recomp @ eb8688b). Owned code: edit it directly. `ps2xRecomp` = MIPS→C++ recompiler, `ps2xRuntime` = guest runtime the generated code links against. |
| `CMakeLists.txt`, `CMakePresets.json`, `cmake/` | The single CMake root (options `DRAKAN_BUILD_RECOMPILER` / `_RUNTIME_TESTS` / `_GAME` / `_PS2X_TESTS`), presets and shared flags (`cmake/CompilerOptions.cmake`). See `BUILDING.md`. |
| `runtime/<lib>/` | Drakan-specific runtime libraries (gs, hle, fmv, spu2, input, settings, ui, vu1ref), each an `add_subdirectory()` component exposing `drakan::<lib>`. Each has `include/`, `src/`, `tests/`. `ps2recomp/ps2xRuntime` links them by target. |
| `third_party/` | Vendored third-party code (`imgui`, target `drakan::imgui`); excluded from formatters. |
| `configs/drakan.toml` | Recompiler configuration for the game ELF. |
| `scripts/buildtool/` | Build entry points. `Build-LocalGame.ps1 -IsoPath <iso>` does the whole local build. |
| `scripts/tests/` | Python tests and CMake policy tests. |
| `scripts/` (rest) | `harness/` (game-run harnesses, recipes, stability), `perf/` (profilers/summarisers), `archive/<date>/` (one-off investigation scripts); see `scripts/README.md`. |
| `analysis/` | Reverse-engineering tooling (`analysis/scripts`), Ghidra scripts, small reference tables. |
| `docs/` | `reference/` (kept current), `status/` (living trackers), `journal/YYYY-MM-DD/` (append-only dated notes), `proposals/`. Index: `docs/README.md`. |

Never commit original game assets, executable content, verbatim disassembly or translated code (ISO, ELF, VU microcode such as
`ps2_vu1_image.inc`, generated recompiled C++, saves, captures). `scripts/repository_guard.py`
enforces this in CI and in the pre-push hook (`git config core.hooksPath .githooks`).
Generated output goes under `build/` (gitignored).

## Working rules

- One repo, one main branch. Use a short-lived branch only for risky multi-commit changes and merge it
  back promptly; don't leave parallel worktrees/branches lying around.
- Write findings into `docs/`: update the relevant `reference/` doc; put dated investigation notes in
  `docs/journal/<date>/<topic>.md`. Don't create new top-level doc files.
- Throwaway probes and outputs go in `build/` or a scratch dir, not the repo root or `scripts/`.
- Commit messages: imperative subject that says what changed ("Fix VIF1 readback for CHCR.DIR=0"),
  not "dev"/"wip".

## Build and deploy

- The one build path is `scripts/buildtool/Build-LocalGame.ps1` (see `BUILDING.md`). Use `-Incremental` on the existing
  `WorkDir` (currently `build/final-game`) after a fix; do not invent per-task build trees or `build/deploy_*.ps1` scripts.
- That script also produces the play folder `dist/DrakanPC` (cleaned and refilled each build; saves, `textures/`,
  `subtitles/`, `nr/`, `ngx_data/` and `*.ini` are kept). Launch and test from `dist/DrakanPC`, never from a bare
  `WORKDIR/bin` (default settings, no HD textures). Do not copy files into `dist/` by hand.
- The only currently permitted tracked binary is the SHA-pinned original `drakan.ico` in
  `scripts/repository_guard.py`; NVIDIA SDK/runtime binaries must stay under ignored `local/` or `tools/`.
  HD textures stay uncommitted and are regenerated with `analysis/scripts/hd_pack.py`.

## Conventions

- **C/C++** — `.clang-format` (LLVM base, 4 spaces, attached braces, 120 cols). Types `PascalCase`,
  functions/methods `camelCase`, constants `kName`, globals `g_name`. `#pragma once`.
  New code: keep platform APIs (Win32, D3D, WASAPI, XInput) out of public headers.
- **Python** — 3.11+, ruff (config in `pyproject.toml`).
- **Line endings** — LF everywhere except `.bat`/`.cmd` (`.gitattributes`, `.editorconfig`).
- Formatting-only commits are listed in `.git-blame-ignore-revs`
  (`git config blame.ignoreRevsFile .git-blame-ignore-revs`).

## Commands

```sh
python -m pip install -r requirements-dev.txt         # ruff
python -m ruff check && git ls-files -z '*.py' | xargs -0 python -m ruff format --check
git ls-files -z -- '*.c' '*.cpp' '*.h' ':!:third_party/**' ':!:analysis/experiments/**' ':!:docs/proposals/**' \
  | xargs -0 clang-format --dry-run --Werror
python scripts/run_tests.py                           # all Python tests
python scripts/repository_guard.py                    # tracked-file audit
python scripts/buildtool/build_tool.py --work-dir build/tool   # recompiler only (root CMake, `recompiler` preset)
pwsh scripts/buildtool/Build-LocalGame.ps1 -IsoPath "<path to iso>" -WorkDir build/local-game  # full game (~75 min)
pwsh scripts/buildtool/Build-LocalGame.ps1 -IsoPath "<path to iso>" -WorkDir build/local-game -Incremental -Jobs 8  # rebuild after a fix (seconds-minutes)
```

Runtime library + policy tests (one ctest run; data-dependent tests skip):
`cmake --preset runtime-tests && cmake --build --preset runtime-tests --parallel 8 && ctest --preset runtime-tests`
(CMake ships with VS 2022; build trees go to `build/<preset>`). A single library still configures alone:
`cmake -S runtime/<lib> -B build/<lib> -A x64`.
