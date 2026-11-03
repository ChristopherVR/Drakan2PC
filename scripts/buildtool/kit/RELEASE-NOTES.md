Windows installer for the Drakan: The Ancients' Gates PC port.

This refresh includes the updated level/mod editor and standalone save editor, landscape sculpting and painting,
campaign packs with separate saves, and custom multiplayer modes. Arena play tests now use the map's offered custom
mode, and match results retain the server's end reason when the last opponent leaves. The builder includes the
corresponding runtime source; rebuild your game to use the runtime changes.

**You need your own disc image of the PAL release (SCES-500.06).** The download contains source code, build scripts
and build tools only: run `DrakanPC-Setup.exe`, pick your `.iso`, and the game is built on your PC (one to two hours
the first time). No game files are included or downloaded.

- `DrakanPC-Setup.exe`: guided installer (bundles PowerShell, Python and the prebuilt recompiler).
- `DrakanPC-Builder.zip`: the same builder as a zip, for people who already have PowerShell 7, Python 3.11+ and Git.
- `DrakanEditor-Setup.exe` / `DrakanEditor-Portable.zip`: the level and mod editor. `DrakanSaveEditor-Setup.exe` /
  `DrakanSaveEditor-Portable.zip`: the save editor. They read your own disc image and saves; no game data is included.
- Browser version: tick it in the installer (needs WSL with Ubuntu), then play it with `Play-Browser.cmd` or on the
  [browser player page](https://christophervr.github.io/Drakan2PC/).

See the [README](https://github.com/ChristopherVR/Drakan2PC#readme) for requirements, controls and the legal notice.
