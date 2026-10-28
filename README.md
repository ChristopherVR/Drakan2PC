# Drakan2PC

An experimental native PC port of **Drakan: The Ancients' Gates**, using static
recompilation of its PS2 executable and a PC graphics, audio and input runtime.
The translated game code runs as native machine code; the runtime implements the
PS2 services the game still needs.

You supply your own disc image. This repository contains source, build tools,
configuration and research metadata. It contains **no ISO, extracted executable,
game assets, generated game code, save files or NVIDIA runtime binaries**.

## Build from your ISO

See [BUILDING.md](BUILDING.md) for prerequisites and the local build commands.
The build tool validates the supported disc executable, extracts it locally,
generates C++ and private VU data, and builds the Windows executable. The ISO,
generated files and resulting game executable stay on your computer.

Currently supported disc: **PAL SCES-500.06**. Other releases require their own
validated configuration; renaming a different ISO does not make it compatible.

GitHub Actions builds the recompilation **tool**, without a game image. It does
not build or distribute a ready-to-play copy of the game.

| Target | Current status |
| --- | --- |
| Windows x64 game | Native build; D3D11, Windows audio and mouse/keyboard runtime |
| Windows / Linux recompilation tool | CI builds the tool used to translate a supplied executable |
| Linux game | Requires graphics, audio, input and window adapters; not implemented |
| Browser game | Built from your ISO (WSL); play with `Play-Browser.cmd` or the browser player page (GitHub Pages) |

## Playing a local build

The local output contains `drakan.exe` and configuration files. Point its ISO
setting at your disc image. F1 opens PC settings during gameplay; Alt+Enter or
F11 toggles borderless fullscreen. Mouse and keyboard bindings are configurable.
Keep your existing configuration and save directories when replacing an exe.

Experimental uncapped rendering is available under Display → World frame rate
(restart required). Disable VSync and the Presentation limiter to remove those
presentation limits. Auto internal resolution follows fullscreen/window size;
fixed scales remain available for performance. See [uncapped FPS and fullscreen
scaling](docs/journal/2026-10-18/uncapped-fps.md) for measured results and limitations.

Rendering, widescreen HUD and combat support are still being improved. See
[native inventory refinements](docs/journal/2026-10-18/native-inventory.md),
[latest integration receipt](docs/journal/2026-10-18/followup.md),
[combat evidence and limits](docs/journal/2026-10-17/combat-encounters.md) and
[widescreen notes](docs/reference/widescreen.md).

Optional NVIDIA DLSS needs an NVIDIA RTX GPU. The SDK is downloaded from NVIDIA only when you opt in and accept
its licence (installer box, or `-AcceptNvidiaDlssLicense`); see
[third_party/nvidia/dlss](third_party/nvidia/dlss/README.md) and [DLSS status and limitations](docs/reference/dlss.md).

## Source layout

- `runtime/`: PC renderer, audio, input, settings and replacement services.
- `ps2recomp/`: the PS2Recomp fork (recompiler, runtime, IOP, tests), GPL-3.0, built directly from this tree.
- `configs/`: validated recompilation configuration.
- `scripts/buildtool/`: compiler build, packaging and `Build-LocalGame.ps1 -IsoPath <iso>`.
- `analysis/`, `docs/`: research metadata and implementation notes (`docs/proposals/`: unmerged proposals).
- `licenses/`, [THIRD_PARTY.md](THIRD_PARTY.md): dependency notices and licenses.

## Keeping game data local

Build output, `iso/`, `dist/`, dependency checkouts, disc images, extracted code,
memory cards, transcripts and captures are ignored. Run:

```powershell
python scripts/repository_guard.py
git config core.hooksPath .githooks
```

The pre-push hook scans the objects a push introduces, including files deleted in those commits. An ignore rule
alone does not remove a file from history.

Third-party source keeps the licenses listed in [THIRD_PARTY.md](THIRD_PARTY.md). Drakan is a trademark of its
respective owners; this project is not affiliated with or endorsed by them, and no game files are included.
