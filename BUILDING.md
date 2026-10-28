# Building tools and private game players

## Repository layout

This is a monorepo. Everything needed to build lives in the tree:

| Path | Contents |
| --- | --- |
| `CMakeLists.txt`, `CMakePresets.json`, `cmake/` | The single CMake root, its presets and shared modules (compiler flags, fast-release mode). |
| `ps2recomp/` | The PS2Recomp fork: recompiler (`ps2xRecomp`), runtime (`ps2xRuntime`), IOP (`ps2xIOP`), unit tests (`ps2xTest`). GPL-3.0. |
| `runtime/` | Drakan PC libraries linked into the runtime: `fmv`, `hle`, `ui`, `spu2`, `input`, `settings`, `gameui` (modern Tab screen core), `gs` (D3D11 renderer), plus the `vu1ref` reference simulator. |
| `third_party/imgui/` | Vendored Dear ImGui (MIT), used by `settings` and `ui`. |
| `configs/drakan.toml` | Validated recompilation configuration for PAL SCES_500.06. |
| `scripts/buildtool/` | Compiler build, packaging and the one-command local game build. |
| `scripts/prepare_local_runtime.py` | Generates the private, git-ignored VU1 image and placeholder icon from your ELF. |

`ps2recomp/` is a plain copy of the fork's `drakan` branch (no history import).
It was forked from upstream <https://github.com/ran-j/PS2Recomp> at commit
`eb8688b2fd191129f103a39543002c005b33409c`, which `scripts/buildtool/toolchain.lock.json`
still records for provenance. Nothing is cloned from upstream and no patches are
applied any more; edit `ps2recomp/` directly. What was left out of the copy:
`ps2xAnalyzer` (config generator, 12 MB symbol database), `ps2xStudio`, Vita and
Android ports, `proposals/`, `research/`, the fork's own docs and scripts.

## What is supported

| Deliverable | Inputs | Status |
| --- | --- | --- |
| Windows x64 compiler package | `ps2recomp/`, C++20 toolchain | Tool-only recipe and CI job; no ISO or NVIDIA files |
| Linux x64 compiler package | The same source, GCC, CMake and Ninja | Compiler-only CI job; not a Linux game binary |
| Private Windows game generation/build | Your supported ISO plus this checkout | `Build-LocalGame.ps1` prepares private ELF/VU inputs and builds |
| Linux runtime libraries / tests | GCC or Clang, CMake, Ninja, ALSA development headers | `linux-runtime-tests` preset and CI job; no game data needed |
| Linux game | Your supported ISO plus this checkout | ISO bootstrap builds and deploys the native software-rendered player; gameplay/save/restart/load validated |
| Browser game | Your supported ISO plus this checkout | ISO bootstrap builds the local Wasm player; gameplay/audio/save/restart/load validated |

CI does not accept/upload ISOs, extracted ELFs, generated game C++, game
executables, asset rips, subtitle transcripts, saves or NVIDIA SDK/runtime files.
Compiler artifacts are tools, not playable game installers. If the account's
artifact storage quota is full, the workflow reports the failed upload without
failing the build; `upload_artifacts=false` skips uploading.

## Prerequisites

- Git, Python 3.11+ and CMake 3.21+ (found through PATH or vswhere).
- Windows: VS 2022 Desktop development with C++, x64 tools and a Windows SDK.
- Linux compiler only: GCC with C++20, CMake and Ninja.
- Linux runtime tests additionally need ALSA development headers (`libasound2-dev` on Ubuntu).
  Native game builds also use window/OpenGL and FFmpeg development packages. The Debian/Ubuntu
  ISO bootstrap below installs missing public packages and prepares the browser toolchain automatically.

## CMake layout

One root `CMakeLists.txt` configures everything; nothing else is a project root any more. What gets built is
selected by options (the presets in `CMakePresets.json` set them; build trees go to `build/<preset>`):

| Option | Builds | Default |
| --- | --- | --- |
| `DRAKAN_BUILD_RECOMPILER` | `ps2_recomp` (and `ps2_difftest_gen`) from `ps2recomp/ps2xRecomp` | ON |
| `DRAKAN_BUILD_RUNTIME_TESTS` | `runtime/*` libraries, their tests and the `scripts/tests` policy tests (no game data) | ON |
| `DRAKAN_BUILD_GAME` | `drakan.exe` (`ps2EntryRunner`); needs `-DPS2X_GENERATED_DIR=<recompiler output>` and the local files from `scripts/prepare_local_runtime.py` | OFF |
| `DRAKAN_BUILD_PS2X_TESTS` | `ps2recomp/ps2xTest` unit tests (downloads raylib and FFmpeg) | OFF |

Each runtime library is an `add_subdirectory()`-able component exposing `drakan::<lib>` (`hle`, `fmv`, `ui`,
`spu2`, `input`, `gs`, `settings`, `gameui`, `vu1ref`; `drakan::imgui` for `third_party/imgui`). `ps2xRuntime` links them by
target. `runtime/<lib>` (and `scripts/tests`) still configure standalone with
`cmake -S runtime/<lib> -B build/<lib>`. Project-wide flags (MSVC `/W4 /permissive- /utf-8 /Zc:__cplusplus`,
clang/gcc `-Wall -Wextra`) live in `cmake/CompilerOptions.cmake` as the `drakan::options` target, applied to our own
targets only (not to FetchContent dependencies or generated game code); warnings are never errors, except in a few
tests that already had `/WX`. `-DDRAKAN_COMPILER_WARNINGS=OFF` (player builds) compiles those targets without warnings. Executables land in `build/<preset>/bin/<config>`.

| Preset | Use |
| --- | --- |
| `windows-msvc` | VS 2022 x64: recompiler + runtime libraries and tests |
| `windows-clang-cl` | the same with the `ClangCL` toolset (needs the VS "C++ Clang tools" component) |
| `runtime-tests` | libraries, tests and policies only; no network fetches, no game data (what CI runs) |
| `recompiler` / `linux-recompiler` | `ps2_recomp` only (what `build_tool.py` configures) |
| `ps2x-tests` | `windows-msvc` plus `ps2xTest` |
| `game` | `drakan.exe`; used by `Build-LocalGame.ps1` and `scripts/harness/build_game.ps1` |
| `linux-runtime-tests` | portable libraries (including SPU2/ALSA), settings UI and policy/host-service tests; no game data |
| `linux-game` | native runtime/generated game build with the software GS; selected by the ISO builder |

```powershell
cmake --preset runtime-tests
cmake --build --preset runtime-tests --parallel 8
ctest --preset runtime-tests          # skips: hle_test, fmv_vdb, live, ray_water (need disc/movies/captures/DXR)
```

On Linux, the library/test path is:

```sh
cmake --preset linux-runtime-tests
cmake --build --preset linux-runtime-tests --parallel 2
ctest --preset linux-runtime-tests
```

The SPU2 output tests use synthetic samples, silent wall-clock rendering and ALSA's `null` PCM;
they need neither a disc nor speakers. Windows continues to use WASAPI. The existing
`Build-LocalGame.ps1` ISO-to-game/deployment pipeline selects Windows, Linux or browser output.
Use the ISO bootstrap below for private playable builds; CMake presets alone do not extract or
generate game data. See [portability](docs/reference/portability.md) for validation and limits.

## Build the compiler only

```powershell
python scripts/buildtool/build_tool.py --work-dir build/buildtool --jobs 2
python scripts/buildtool/package_tool.py --work-dir build/buildtool --output-directory build/tool-package
```

The builder configures the root project with the `recompiler` preset (only
`DRAKAN_BUILD_RECOMPILER`), builds target `ps2_recomp` and checks its no-argument usage/exit behaviour. The third-party
dependencies (ELFIO, toml11, fmt, libdwarf, Rabbitizer) are fetched at the full
commits pinned in `toolchain.lock.json` into `WORKDIR/deps` and supplied through
FETCHCONTENT_SOURCE_DIR overrides; no moving branch is fetched. Compressed DWARF
support is disabled and Windows uses the static CRT. Packages contain the
compiler, the GPL text, the recipe and a corresponding-source archive with
`rebuild_sources.py` (extract, then run it with CMake to rebuild offline).

## Setup .exe / builder kit for someone with their own ISO

```powershell
python scripts/buildtool/package_builder_kit.py --installer  # -> build/builder-kit/DrakanPC-Setup-<commit>.exe (+ .json)
python scripts/buildtool/package_builder_kit.py              # -> build/builder-kit/DrakanPC-Builder-<commit>.zip
```

Both package the committed tree at `HEAD` (`--revision` for another; uncommitted changes are left out) minus
developer-only material (`docs/journal`, `scripts/archive`, CI, agent notes). Every file passes
`repository_guard.reason`, so neither holds an ISO, ELF, generated C++ or VU image. The root gets `Build-Drakan.cmd`
and `README.txt` from `scripts/buildtool/kit/`, plus `KIT-VERSION.txt` with the commit.

The setup (for non-technical players) additionally bundles everything that would otherwise be a prerequisite, pinned
by SHA-256/commit in `scripts/buildtool/kit/installer.lock.json`: portable PowerShell 7 (`tools/pwsh`), the Python
embeddable package (`tools/python`), `ps2_recomp.exe` built from the same snapshot (`scripts/buildtool/`, so
`Build-LocalGame.ps1` skips the Git-based compiler build), and raylib plus the FFmpeg prebuilt zip (`deps/`, picked up
by `Build-LocalGame.ps1 -DependencyDirectory`, default `<repo>/deps`). Packaging needs Git, network access and Inno
Setup 6 (`winget install JRSoftware.InnoSetup`; script `scripts/buildtool/kit/DrakanPC-Setup.iss`); downloads are
cached in `build/builder-kit/work/cache`. The setup installs per user (default `%LOCALAPPDATA%\DrakanPC`, no admin),
asks for the ISO, creates Start menu/desktop shortcuts to `dist\DrakanPC\drakan.exe` and a "Rebuild Drakan" entry,
and on its last page starts `Build-Drakan.cmd -IsoPath <iso>`.

`Build-Drakan.cmd` runs `scripts/buildtool/Build-Drakan.ps1` with the bundled PowerShell (else `pwsh` on PATH). It
checks Python (bundled, else 3.11+ on PATH), Git (only when the kit lacks the bundled sources) and disk space, and when
no VS 2022 instance has the C++ x64 tools and CMake it downloads `vs_BuildTools.exe` from Microsoft, checks its
Authenticode signature and installs the VCTools workload with `--passive` (one UAC prompt); that instance is then
forced through `CMAKE_GENERATOR_INSTANCE`. It passes `-SerialVuSlices` (CMake `PS2X_VU1_AOT_SERIAL`: each of the four
`ps2_vu1_aot_N.cpp` slices takes ~17 GB and ~10 min in cl, measured 16.6 GB / 591 s, so four in one `/MP` batch
exhausted 31-61 GB machines; a distinct define per slice makes MSBuild compile them one after another) and sizes
`-CompilerProcesses` (~6 GB each on the generated code) and `-Jobs` (2 only with room for a slice beside them) from the
installed memory, asks for the ISO when not given (file dialog; remembered in
`build/iso-path.txt`) and calls `Build-LocalGame.ps1` with `WorkDir` `build/game` (`-Incremental` once prepared). A
failed build is retried once incrementally with `-Jobs 1` and half the compiler processes. It also passes `-Progress`
(numbered steps, a progress bar with the elapsed time, one `[n/N]` line per compiled file; the tools' own output goes to
`build/game-output.log` and only errors and, on failure, the output's tail reach the console) and, while
`build/game` is not configured yet, `-NoWarnings` (CMake `DRAKAN_COMPILER_WARNINGS=OFF`, `/W0` instead of `/W4`; not
applied to a configured tree because the flag change would recompile everything). The console is logged to
`build-log.txt`. The result is `dist/DrakanPC` in the install folder, with the `scripts/launchers` `Play*.cmd` files.
`Build-Drakan.cmd -CheckOnly` stops after the checks and installs nothing. The built game is derived from the disc and
is not for sharing; only the setup/kit is.

## Point the tool at your ISO

Only PAL **SCES_500.06**, ELF entry `0x100008`, is verified. The extractor checks
SYSTEM.CNF, ISO directory bounds, the ELF headers and the exact ELF SHA-256. It
reads a normal 2048-byte-sector ISO9660 image (not BIN/CUE) and extracts only the
boot ELF.

```powershell
./scripts/buildtool/Build-LocalGame.ps1 -IsoPath 'D:/MyDiscs/Drakan.iso'
```

Output defaults to `build/local-game` (pass `-WorkDir` to change it; it must not
exist yet). The script:

1. extracts and verifies the ELF and writes `config.toml` from `configs/drakan.toml`;
2. builds `ps2_recomp` from the root project (unless `-Compiler` is given) and
   regenerates the game C++ into `WORKDIR/generated`;
3. runs `scripts/prepare_local_runtime.py`, which reconstructs the VU1 image
   (`ps2recomp/ps2xRuntime/src/lib/vu/aot/ps2_vu1_image.inc`) from your ELF. That path is in
   `.gitignore` and rejected by `scripts/repository_guard.py`; an existing file is never overwritten.
   The application icon (`ps2recomp/ps2xRuntime/src/res/drakan.ico`) is tracked; the script only
   writes a placeholder if it is missing;
4. configures the root project with the `game` preset (in-repo `runtime/*` libraries) and builds
   `ps2EntryRunner` into `WORKDIR/bin/drakan.exe`, then stages `drakan.ini`
   (absolute ISO/ELF paths) and `drakan_input.ini`. Existing configs are preserved.

Optional parameters: `-Jobs`, `-Compiler`, `-ToolWorkDir`, `-ConfigTemplate`,
`-RuntimeSource` (must be `ps2recomp/`, the default), `-GenerateOnly` (stop after game C++),
`-PrepareOnly` (stop after ELF preparation), `-SourceOnly` (stop before native
compilation). The game is never launched and nothing is uploaded.

### Fast rebuilds after a fix

A full build takes over an hour, almost all of it compiling the generated game C++. After the
first build, rebuild the same `WorkDir` with `-Incremental`:

```powershell
./scripts/buildtool/Build-LocalGame.ps1 -IsoPath 'D:/MyDiscs/Drakan.iso' -WorkDir build/local-game -Incremental -Jobs 8
```

This skips ISO extraction and reruns the recompiler only when its inputs changed
(`ps2recomp/ps2xRecomp/**`, `configs/drakan.toml`, `analysis/functions.csv`,
`analysis/jumptables.csv`, `scripts/buildtool/toolchain.lock.json`; the hash is kept in
`WORKDIR/recompile.stamp`). The recompiler rewrites only translation units whose text changed, so
a runtime fix (`runtime/*`, `ps2recomp/ps2xRuntime`) recompiles a few files and relinks
`drakan.exe`. A change that alters the generated C++ is slower again, but only the affected
batches recompile. `-Regenerate` forces the recompile step. Build directories configured before the
single CMake root (2026-10-22) are refused; do one full build into a fresh `WorkDir` first.

### The `dist/DrakanPC` play folder

`Build-LocalGame.ps1` is the only supported way to produce `dist/DrakanPC`. Never launch or test a bare
`WORKDIR/bin` as "the game": it has default settings and none of the HD textures. Every successful
build cleans `dist/DrakanPC` (or `-DistDirectory <path>`) and refills it from `WORKDIR/bin`, so stale
executables, DLLs, logs and notes do not pile up. When DLSS is built, `nvngx_dlss.dll` from your downloaded DLSS SDK (or `local/nvidia/dlss/`) is copied into the play folder; it is never part of the source tree. The script also stages the `scripts/launchers/Play*.cmd` launchers that are not there yet and prints the deployed `drakan.exe` SHA-256. Kept across the clean (never overwritten from `bin/`):
`mc0`/`mc1` (saves), `textures`, `subtitles`, `nr`, `ngx_data`, `*.ini`, `Play*.cmd` and `PLAY.txt`.
Pass `-NoDist` to leave `dist/` untouched.

`textures/` is not committed; regenerate it from your ISO with `analysis/scripts/hd_pack.py extract`
then `upscale` (see `docs/reference/hd-textures.md`, "Regenerate"). The file names (texture hashes)
and the decode are deterministic; the Real-ESRGAN pixels depend on the GPU and driver, so a regenerated
pack matches the old one by name but not necessarily bit for bit.

Linux and browser deployment reuse the same optional HD pack: the builder checks
`local/textures`, then the existing `dist/DrakanPC/textures`. An explicit
`-TextureDirectory <folder>` overrides that source (`--texture-dir` for
`Build-Drakan.sh`). Only hash-named PNG/DDS images and alias/exclusion metadata
are staged for native play; existing player files are preserved. The browser
launcher can stream the existing shared folder directly without copying the pack. It sends
an index and streams requested images on a texture worker instead of preloading
the pack. No external browser save or graphics toolbar is required.

The software replacement path is under real-disc validation; it is not yet
feature/performance parity with the Windows GPU renderer. Ray tracing and
vendor-specific enhancements still need compatible Linux/browser backends.

### Optional local NVIDIA build

The ordinary build needs no NVIDIA SDK. Two things are optional and independent:

* **Hardware ray tracing** (DXR water reflections, ray-traced shadows) is built whenever `dxc` (the DirectX Shader
  Compiler from the Windows 10/11 SDK) is found; CMake prints `gsr: DXR ray tracing ON/OFF`. `-DGSR_DXR=OFF` disables it.
  Without `dxc` the settings menu shows these toggles as "not available in this build".
* **DLSS Super Resolution.** `Build-LocalGame.ps1` turns it on by itself when the SDK is at `tools/dlss/dlss_sdk`
  (gitignored; `include/nvsdk_ngx.h`, `lib/Windows_x86_64/x64/nvsdk_ngx_d.lib`). Use `-DlssSdkDirectory 'D:/SDKs/DLSS'`
  for another location or `-NoNvidia` to build without it. The script prints the choice (`NVIDIA options: DLSS=...`)
  so an `-Incremental` run cannot silently drop it.
  The SDK is never vendored. `-AcceptNvidiaDlssLicense` accepts the NVIDIA RTX SDKs License and downloads the
  pinned SDK files from NVIDIA (`scripts/buildtool/fetch_dlss_sdk.py`, see `third_party/nvidia/dlss/README.md`); the
  guided `Build-Drakan.ps1` asks on PCs with an NVIDIA GPU, and the installer has an opt-in box. DLSS starts
  disabled in a fresh clone without the SDK.

### Tests

```powershell
python scripts/repository_guard.py
python scripts/test_repository_guard.py
python -m unittest discover -s scripts/tests -p "test_*.py"
cmake --preset runtime-tests && cmake --build --preset runtime-tests && ctest --preset runtime-tests
```

The runtime library tests and the C++ policy tests of `scripts/tests` are one `ctest` run. The upstream-style
suite in `ps2recomp/ps2xTest` is built with `DRAKAN_BUILD_PS2X_TESTS` (preset `ps2x-tests`).

## Limits

- Runtime dependencies are not fully locked: raylib/ImGui use upstream tags and
  FFmpeg uses a prebuilt release zip (SHA-256 pinned in `ps2xRuntime/CMakeLists.txt`).
- CI validates compiler builds and unit/policy tests, not the private generated
  game build or live behaviour.
- Linux and browser use the software renderer and original game menus. Advanced Windows graphics
  and settings overlays are platform-specific; live validation and remaining limits are recorded in
  `docs/reference/portability.md`.

## Linux and browser ISO builds

The Linux bootstrap accepts the same verified PAL ISO as Windows. It installs missing public
Debian/Ubuntu build dependencies, downloads checksum-pinned Python/PowerShell when needed, and calls
`Build-LocalGame.ps1`. No ELF, VU image, BIOS or separately extracted game assets are required.
The current native target is **x86-64 Linux**; Ubuntu 22.04 or newer is the local validation environment.
An initial build requires network access, several GB of disk space and substantial compilation time.
The bootstrap reuses `build/final-game` automatically on subsequent runs.

```sh
bash scripts/buildtool/Build-Drakan.sh "/path/to/Drakan.iso"
./dist/DrakanPC-linux/Play-Drakan.sh

bash scripts/buildtool/Build-Drakan.sh "/path/to/Drakan.iso" --web
./dist/DrakanPC-web/Play-Browser.sh
```

Windows can build the browser player through an installed WSL Ubuntu distribution:

```powershell
pwsh scripts/buildtool/Build-LocalGame.ps1 -IsoPath "D:/MyDiscs/Drakan.iso" -TargetPlatform web -WorkDir build/final-game
./dist/DrakanPC-web/Play-Browser.cmd
```

`runtime-build-linux` / `bin-linux` and `runtime-build-web` / `bin-web` are siblings of the existing
Windows output inside WorkDir. Build one target at a time because extraction/generation inputs are shared. Deployment goes through the canonical builder and keeps the Windows
play folder separate. Native Linux cards are stored in the deployed player's `mc0` / `mc1` folders.
`DRAKAN_MEMCARD_DIR` can redirect cards for tests.

The browser launcher runs a Python standard-library server on `http://127.0.0.1:8715/`. It supplies
COOP/COEP isolation headers and bounded reads from the selected ISO; the browser never uploads the
ISO or loads the full disc into memory. Keep the server running during play. Use this launcher instead
of opening `index.html` directly. WSL builds also stage a Windows launcher and the corresponding
Windows ISO path. `serve_web.py --iso <path>` selects a moved ISO; `--port` is optional, but changing
the port changes the browser origin and therefore its saved-game storage.

**Browser player page.** The project site (GitHub Pages, published from `runtime/web/pages` by
`.github/workflows/pages.yml`) is an alternative to the launcher: choose your `dist/DrakanPC-web` folder, your ISO and
optionally an HD texture folder, then Play. The page holds no game code; its service worker (`sw.js`) serves `play/`
straight from the chosen files with the isolation headers and bounded disc reads, so nothing is uploaded. It needs a
desktop Chromium browser (File System Access API). Saves made there belong to the site's origin, separate from the
launcher's `127.0.0.1:8715`.

Browser cards and `drakan.ini` use IndexedDB. In-game saves persist automatically when their files
close; periodic recovery and shutdown flushes share the same serialized queue. Keep the same browser
profile and origin when returning to your saves. Clearing site
data removes them. Browser execution requires native Wasm exceptions, Wasm SIMD, shared memory, workers, WebGL, AudioWorklet
and a desktop keyboard/controller. A minimal **Play** gesture starts browser audio, then disappears.
During play the browser shows only the game, with its own title, pause, load and save menus.

Linux and browser use the software GS renderer and the game's original menus. Their basic controls
are WASD movement, Shift walk, mouse look (F10; the browser also captures a click in gameplay), left click/Z attack,
right click/C magic, Space jump, F confirm/crouch, E use, Q quick inventory, R next target, middle click target lock,
Enter/Escape pause or back, Tab inventory and arrow keys for menus. Controllers retain the original DualShock layout.
Windows keeps its native input, menus, D3D11 renderer and optional NVIDIA features.

For implementation status and validation limits, see `docs/reference/portability.md`.

## Licenses and publishing gates

PS2Recomp is GPL-3.0 (`ps2recomp/LICENSE`, `licenses/PS2Recomp-GPL-3.0.md`);
the in-repo fork and distributed compiler need the applicable notices and
corresponding source. NVIDIA SDKs/runtime DLLs are optional proprietary
components and excluded. FFmpeg, SDL/raylib and ImGui need their own notices
before any runtime package is shared. See [THIRD_PARTY.md](THIRD_PARTY.md).
No right to redistribute game-derived outputs is implied by building them.
`python scripts/repository_guard.py` (also the pre-push hook) rejects tracked game
data, binaries over 2 MiB and the private VU/icon files.
