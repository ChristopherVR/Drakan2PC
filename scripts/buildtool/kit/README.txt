DRAKAN: THE ANCIENTS' GATES - PC BUILDER
=========================================

This turns your own copy of the PS2 game into a native Windows game. It contains
only source code, build scripts and build tools; no game data. You supply the
disc image, and everything built from it stays on your PC. Please do not share
the built game.


WHAT YOU NEED
-------------
* Your own disc image as a plain .iso file: the PAL release, SCES-500.06, or
  the USA release, SCUS-971.28. The USA build is experimental: keyboard,
  mouse look and the game's own menus work, but not the PC settings (F1),
  widescreen, frame-rate options, the modern Tab/map screens, quicksave,
  the extra save pages or subtitles. USA and PAL saves are separate. BIN/CUE
  and other versions are not supported; the builder checks the disc and
  stops if it is a different version.
* Windows 10 or 11, 64-bit, and an internet connection for the first build.
* About 10 GB of free disk space, at least 16 GB of memory (24 GB or more
  recommended: a few files need about 17 GB while compiling), and a DirectX 11
  graphics card to play.

Nothing else: on the first run the builder downloads and installs Microsoft's
free "Visual Studio 2022 Build Tools" (the C++ compiler, about 3 GB). Windows
asks for permission once.

(If you have the zip kit instead of DrakanPC-Setup.exe, it also needs
PowerShell 7, Python 3.11+ and Git; the builder tells you what is missing.)


BUILD
-----
DrakanPC-Setup.exe: choose a folder and your .iso, and leave "Build the game
now" ticked on the last page. A build window opens.

The first build takes one to two hours; most of it is compiling the game code.
Some files take about 10 minutes each with no new output, so the window can
look stuck for a while; leave it running. When the window says "Done", start
the game with the "Drakan - The Ancients' Gates" shortcut (Start menu or
desktop). The game files are in the install folder under dist\DrakanPC.

The game reads the disc image while it runs (its path is in
dist\DrakanPC\drakan.ini), so keep the .iso where it was.

"Rebuild Drakan" in the Start menu (Build-Drakan.cmd in the install folder)
runs the build again. It continues where a stopped build left off and keeps
your saves, settings (*.ini) and textures.


PLAYING
-------
  F1               PC settings (video, controls, mouse, frame rate)
  F10              release / recapture the mouse
  F11, Alt+Enter   borderless fullscreen
  Esc              pause menu
Keyboard, mouse and XInput gamepads work; bindings are editable in F1 or in
drakan_input.ini. Saves go to dist\DrakanPC\mc0.

Play-Original30.cmd in dist\DrakanPC runs the world at the original 30 fps.


OPTIONAL EXTRAS
---------------
* HD textures: generated from your disc with an AI upscaler; see
  docs\reference\hd-textures.md ("Regenerate"). Needs numpy, Pillow and
  realesrgan-ncnn-vulkan.
* NVIDIA DLSS (NVIDIA RTX GPUs): tick the DLSS box in the setup, or answer "y"
  when the build asks. The SDK is then downloaded from NVIDIA under the NVIDIA
  RTX SDKs License, which you accept by doing so.
* Browser version: tick "Also build the browser version" in the setup (needs
  WSL with Ubuntu: wsl --install -d Ubuntu). Play it with Play-Browser.cmd in
  dist\DrakanPC-web, or on the project's browser player page by choosing that
  folder and your .iso (nothing is uploaded).
* Ray-traced water and shadows are built automatically (the Build Tools include
  the DirectX Shader Compiler).


IF SOMETHING GOES WRONG
-----------------------
* "Unsupported disc ...", "ELF hash differs ..." or "Not a 2048-byte-sector
  ISO9660 image": the image is not a supported PAL or USA disc, or it is not
  a plain ISO. Nothing was written; run "Rebuild Drakan" and pick the right
  file.
* The build stops while compiling: it already retries once with less parallel
  work. Close other programs and run "Rebuild Drakan".
* "The Build Tools ask for a restart": restart Windows, then "Rebuild Drakan".
* Everything the build printed is in build-log.txt in the install folder.
* Full details of the build are in BUILDING.md.


LICENSES
--------
The recompiler and runtime are based on PS2Recomp (GPL-3.0); see licenses\ and
THIRD_PARTY.md. The full source is included in this folder. Bundled tools:
PowerShell (MIT), Python (PSF license), raylib (zlib), FFmpeg (LGPL, prebuilt
by github.com/System233/ffmpeg-msvc-prebuilt). Drakan is a trademark of its
respective owners. This project is not affiliated with or endorsed by them.
