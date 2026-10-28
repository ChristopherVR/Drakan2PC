# Developer overview

Drakan2PC statically recompiles the supported PS2 program into C++, then links the locally generated code to a replacement runtime. The repository contains the recompiler, runtime and tools; the player supplies the ISO.

## Start with the canonical build

Read [Build from source](../../BUILDING.md) for prerequisites and the single supported game pipeline. Use `scripts/buildtool/Build-LocalGame.ps1`; rebuild with `-Incremental` in the same work directory. Launch and validate from `dist/DrakanPC` on Windows.

The CMake runtime tests and Python suite do not require a game disc. Data-dependent tests skip when their private inputs are missing.

## Repository map

| Directory | Responsibility |
| --- | --- |
| `ps2recomp/ps2xRecomp` | MIPS-to-C++ recompiler |
| `ps2recomp/ps2xRuntime` | Guest execution and integration runtime |
| `runtime/` | Graphics, sound, input, settings, game UI, FMV and service libraries |
| `runtime/web/` | Browser runtime and source-only hosted launcher |
| `configs/` | Validated game recompilation configuration |
| `scripts/buildtool/` | Build, bootstrap, packaging and documentation-site entry points |
| `analysis/` | Reverse-engineering tooling and reference tables |
| `docs/` | Player guides, technical references, status trackers and dated investigations |

The root CMake project owns the shared compiler options and target selection. Runtime libraries expose `drakan::<library>` targets. Keep platform APIs out of public headers so new code remains portable.

## Run checks

```powershell
python -m pip install -r requirements-dev.txt
python scripts/run_tests.py
python scripts/repository_guard.py
cmake --preset runtime-tests
cmake --build --preset runtime-tests --parallel 8
ctest --preset runtime-tests
```

Use the corresponding Linux presets in [Build from source](../../BUILDING.md). Follow `.clang-format` for C++, Ruff for Python and LF line endings for source files.

## Keep private inputs private

Never commit ISOs, extracted executables, translated game C++, VU images, saves, captures or other game assets. Generated output belongs under ignored `build/`. The repository guard runs in CI and can run before pushes:

```powershell
git config core.hooksPath .githooks
```

Use the existing main-branch workflow; short-lived branches are for risky changes and should be merged promptly. See [AGENTS.md](../../AGENTS.md) for repository conventions.

## Find the relevant subsystem

Use the [technical reference index](reference.md) for graphics, input, saves, subtitles and other subsystems. Current reference documents and dated journal entries serve different purposes: a journal records what was known on that date, not necessarily today's behavior.

The [site maintenance guide](../reference/github-pages.md) explains how to edit these pages and preview them without building the game.
