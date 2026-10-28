# Drakan2PC

A PC port of **Drakan: The Ancients' Gates** (PlayStation 2, 2002), built by **static recompilation**: the game's
own PS2 program is translated to C++ and compiled into a native Windows executable, or a WebAssembly build for the
browser. A new runtime supplies graphics, audio, input, saves and the PS2 system services the game expects.

**You need your own copy of the game.** This repository holds only source code and build tools. The game is built on
your PC from a disc image of your own disc, and everything made from it stays on your PC.

## Features

- Native Windows build: Direct3D 11, any resolution, borderless fullscreen, widescreen HUD and
  higher frame rates (the original 30 fps is still available).
- Mouse and keyboard controls with rebindable keys, plus XInput controllers. F1 opens the PC settings
  (video, controls, mouse, frame rate).
- Optional HD texture pack, generated from your disc with an AI upscaler.
- Optional ray-traced water and shadows (needs a GPU with DirectX Raytracing).
- Optional NVIDIA DLSS Super Resolution / DLAA on NVIDIA RTX GPUs (downloaded from NVIDIA only if you opt in).
- Browser version: WebAssembly, played locally or through the [browser player page](https://christophervr.github.io/Drakan2PC/).
- Saves on virtual memory cards next to the game (or in browser storage).

## Supported disc

**PAL release, SCES-500.06**, as a plain `.iso` image. The builder checks the disc and stops on any other version;
other regions would need their own validated configuration.

## Getting started

### Installer (Windows)

1. Run `DrakanPC-Setup.exe` (from this repository's Releases, or build it yourself with
   `python scripts/buildtool/package_builder_kit.py --installer`).
2. Choose an install folder and your `.iso`. Optionally tick **NVIDIA DLSS** (accepts NVIDIA's SDK licence) and
   **browser version** (needs WSL with Ubuntu).
3. Leave "Build the game now" ticked. On first run the builder installs Microsoft's free C++ Build Tools. The first
   build takes one to two hours; some files take about 10 minutes each with no output.
4. Start the game from the **Drakan - The Ancients' Gates** shortcut.

Needs Windows 10/11 64-bit, about 10 GB of free disk space and 16 GB of memory (24 GB or more recommended). The
game reads the `.iso` while it runs, so leave it where it is. "Rebuild Drakan" in the Start menu continues an
interrupted build and keeps your saves and settings.

### Browser

Build the browser version (installer option, or `Build-LocalGame.ps1 -TargetPlatform web`). Then either run
`Play-Browser.cmd` in `dist\DrakanPC-web`, or open the
**[browser player page](https://christophervr.github.io/Drakan2PC/)** in desktop Chrome or Edge and choose your
`DrakanPC-web` folder and `.iso`. The page contains no game code: it runs your own build from your own disk, and
nothing is uploaded.

### From source

```powershell
pwsh scripts/buildtool/Build-LocalGame.ps1 -IsoPath "D:\Games\Drakan.iso" -WorkDir build/game
```

The playable folder is `dist/DrakanPC`. Rebuild after a change with `-Incremental`. See [BUILDING.md](BUILDING.md)
for prerequisites, Linux and browser builds, presets and every option.

## Controls

| Key | Action |
| --- | --- |
| F1 | PC settings |
| F10 | Release / recapture the mouse |
| F11, Alt+Enter | Borderless fullscreen |
| Esc | Pause menu |

All bindings can be changed in F1 or in `drakan_input.ini`. Gamepads use the original DualShock layout. Details:
[input](docs/reference/input.md), [combat controls](docs/reference/combat-controls.md).

## Optional extras

- **HD textures**: [docs/reference/hd-textures.md](docs/reference/hd-textures.md).
- **NVIDIA DLSS**: [third_party/nvidia/dlss](third_party/nvidia/dlss/README.md), [docs/reference/dlss.md](docs/reference/dlss.md).
- **Widescreen**: [docs/reference/widescreen.md](docs/reference/widescreen.md).

## Status

| Target | Status |
| --- | --- |
| Windows x64 | Playable; D3D11 renderer, native settings and input |
| Linux x64 | Playable software-rendered build from your ISO |
| Browser (WebAssembly) | Playable from your ISO; built with WSL/Linux |
| Recompiler tool | Built and tested by CI on Windows and Linux (no game data) |

This is an experimental project. Expect rough edges in rendering, the widescreen HUD and some combat situations.

## Repository layout

| Path | Contents |
| --- | --- |
| `ps2recomp/` | The PS2Recomp fork: recompiler, runtime, IOP and tests |
| `runtime/` | PC renderer, audio, input, settings, UI and replacement system services; `runtime/web` holds the browser player |
| `configs/` | Recompilation configuration for SCES-500.06 |
| `scripts/` | Build tools, installer, tests and test harnesses |
| `analysis/`, `docs/` | Reverse-engineering tooling and technical notes ([index](docs/README.md)) |
| `third_party/`, `licenses/` | Vendored third-party code and licence texts |

## Development

```powershell
python scripts/run_tests.py                 # Python tests
python scripts/repository_guard.py          # tracked-file audit: no game data may be committed
git config core.hooksPath .githooks         # pre-push audit
```

Never commit disc images, extracted executables, generated code, game assets, saves or captures; the guard and the
pre-push hook reject them. Conventions are in [AGENTS.md](AGENTS.md).

## Licenses

The project is licensed under the [GNU General Public License v3.0](LICENSE). Third-party components keep their own
licences; see [LICENSES.md](LICENSES.md) and [THIRD_PARTY.md](THIRD_PARTY.md).

## Legal notice

Drakan2PC is an unofficial fan project. It is not affiliated with, endorsed by or sponsored by Surreal Software,
Sony Interactive Entertainment, Warner Bros. Games or any other holder of rights in *Drakan: The Ancients' Gates*.

- **No game content is included.** This repository and its packages contain no disc images, game executables,
  game code (original or translated), assets, audio, video or text from the game. The game is built locally from a
  disc image you make from a disc you own.
- **Use your own disc.** Do not download disc images you do not own, and do not share disc images, built games or
  files generated from your disc. This project does not support piracy.
- **Trademarks.** Drakan is a trademark of its respective owner. PlayStation and PS2 are trademarks of Sony
  Interactive Entertainment Inc. NVIDIA, RTX and DLSS are trademarks of NVIDIA Corporation. All other trademarks
  belong to their owners and are used only to identify the products they refer to.
- **No warranty.** The software is provided as is, without warranty of any kind, as set out in the GPL.
- **Rights holders** with concerns about this project can open an issue on this repository to contact the maintainer.
