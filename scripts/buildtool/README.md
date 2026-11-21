# Build and installer layout

| File or folder | Responsibility |
| --- | --- |
| [Build-Drakan.ps1](Build-Drakan.ps1) | Guided Windows builder used by Setup and the ZIP's `Build-Drakan.cmd`. It validates the disc, selects optional packs, and calls the shared build. |
| [Build-Drakan.sh](Build-Drakan.sh) | Linux and browser entry point. It starts [bootstrap_linux.py](bootstrap_linux.py), which prepares the toolchain and calls the shared build. |
| [Build-LocalGame.ps1](Build-LocalGame.ps1) | Shared build and deployment for Windows, Linux and WebAssembly. |
| [powershell/](powershell/) | Shared PowerShell helpers for CPU affinity, child processes, build policy, progress and textures. [build-messages.json](build-messages.json) contains console translations used by the builders. |
| [package_builder_kit.py](package_builder_kit.py) | Creates the builder ZIP and Windows installer. |
| [kit/](kit/) | Inno Setup wizard, launcher, localized help, and assets used by the builder. |

## Translate Setup

Each language lives in [kit/locales/](kit/locales/) under its two-letter code. Use [English](kit/locales/en/) as a reference.

- Edit `setup.isl` for wizard pages, buttons and error messages. Keep message keys, `%1` placeholders and `%n` line breaks. Preserve the extensions and wildcard separators in file filters.
- Edit `SETUP-INFO.txt` for the introduction shown by Setup and `README.txt` for the help installed beside the builder. Save both as UTF-8. The ZIP also includes the translated guides under `readme/`.
- The [Setup language list](kit/DrakanPC-Setup.iss) maps each locale to Inno's standard messages and to the corresponding guide. Keep [the packager's README list](package_builder_kit.py) in sync when adding a language.

## Game subtitles

The installer language does not determine the language of the game recordings. For Order of the Flame, choose **Generate subtitles from my recordings** to use your own German, French, Italian or Spanish copy. A transcript you already have can be supplied as `ootf-subtitles.json` alongside an installed copy or as `<image filename>.subtitles.json` beside a disc image. The bundled scene captions match English recordings.

For The Ancients' Gates intro and ending, place your matching SRTs in `local/subtitles/` as described in the [extras guide](../../docs/guides/extras.md). Gameplay dialogue subtitles are read from the game's disc.
