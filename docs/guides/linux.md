# Install on Linux

Build and play the native Linux version from your own PAL disc image. The Linux bootstrap installs the public build tools it needs and calls the same game-building pipeline as the Windows version.

![Rynn in Surdana in the native Linux player](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/linux-gameplay.jpg)

## Before you start

| Requirement | What you need |
| --- | --- |
| Game disc | Your own **PAL SCES-500.06** release as a plain `.iso`. The USA release (SCUS-971.28) is an experimental Windows-only build for now: see [USA disc support](install.md#usa-disc-support-experimental) |
| Architecture | **x86-64 Linux**; this bootstrap does not support ARM |
| Distribution | Ubuntu **22.04 or newer** is the validated environment; automatic dependency installation supports Debian/Ubuntu |
| Desktop | A working graphical desktop with OpenGL and audio support |
| Network | Internet access for the source checkout, packages and pinned build tools |
| Storage and memory | Several GB of free build space, plus your ISO; generated-code compilation needs substantial memory |
| Permissions | `sudo` access if required system packages are missing |

The Linux player uses the software GS renderer. Advanced Windows graphics options such as DLSS and DXR are not available through this renderer. Performance varies by scene and CPU; see [Platforms & portability](../reference/portability.md).

## Get the source

Install Git if your system does not already have it. On Debian/Ubuntu:

```sh
sudo apt update
sudo apt install git
git clone https://github.com/ChristopherVR/Drakan2PC.git
cd Drakan2PC
```

If you already have a checkout, use it instead of cloning a second copy. Keep the ISO in a stable location; the game continues to read it during play.

## Build from your ISO

Run this from the repository root, replacing the path with your own disc image:

```sh
bash scripts/buildtool/Build-Drakan.sh "/path/to/Drakan.iso"
```

The bootstrap checks for the compiler, CMake, Ninja, Python, PowerShell and runtime dependencies. On Debian/Ubuntu it installs missing public packages through `apt-get`, including the audio, graphics and FFmpeg development libraries. It downloads pinned Python/PowerShell tools when required.

It validates the disc, prepares the generated game code, compiles the Linux runtime and stages the playable installation at `dist/DrakanPC-linux`. The default work directory is `build/final-game`. Allow substantial time for the first compilation and leave the terminal open until it finishes.

The default is two build jobs. If memory is tight, use one:

```sh
bash scripts/buildtool/Build-Drakan.sh "/path/to/Drakan.iso" --jobs 1
```

Do not run simultaneous target builds against the same work directory.

## Launch the game

```sh
./dist/DrakanPC-linux/Play-Drakan.sh
```

Launch from the deployed folder through this script rather than a bare executable in the build tree. Modern title, pause, save/load and PC Settings screens have a portable rendering path; full Windows inventory/map overlay parity remains unfinished.

The portable controls include WASD movement, mouse look, Shift to walk, Space to jump, E to use, Q for Quick Inventory, Tab for inventory and arrows for menus. F10 releases mouse capture. Use the in-game controls and key prompts for the current portable mapping; some bindings differ from Windows.

![Display settings in the native Linux player](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/display-settings.jpg)

## Rebuild and keep your saves

Run the same bootstrap command again. When the existing work directory has been prepared, it automatically requests an incremental build:

```sh
bash scripts/buildtool/Build-Drakan.sh "/path/to/Drakan.iso"
```

Native Linux saves live in the deployed `mc0` folder, with `mc1` used if present. Back up the card folders and INI files with the game closed before updating or moving your installation. See [Saves & updates](saves.md).

To use an existing optional texture pack, supply its location:

```sh
bash scripts/buildtool/Build-Drakan.sh "/path/to/Drakan.iso" --texture-dir "/path/to/textures"
```

## Build the browser version on Linux

The same bootstrap can prepare WebAssembly instead:

```sh
bash scripts/buildtool/Build-Drakan.sh "/path/to/Drakan.iso" --web
./dist/DrakanPC-web/Play-Browser.sh
```

Keep the local server running during play. Follow [Play in your browser](browser.md) for hosted-launcher use and browser save-storage rules.

## Common Linux setup problems

- **Unsupported architecture:** the bootstrap currently requires x86-64; ARM devices need a separate porting effort.
- **Missing packages on another distribution:** automatic installation is Debian/Ubuntu-specific. Install equivalent compiler, OpenGL/X11, ALSA and FFmpeg development packages; use [Build from source](../../BUILDING.md) for the supported build layout.
- **Compiler killed or out of memory:** retry with `--jobs 1`, close memory-heavy applications and check available RAM/swap. Reuse the same work directory.
- **No display:** the native player needs a graphical session. A headless shell alone cannot show the game. WSL additionally needs a working GUI environment such as WSLg.
- **No sound under WSLg:** check the host sound configuration and the ALSA PulseAudio plugin. The bootstrap includes `libasound2-plugins` for the native target.

For disc validation, moved ISO paths and reproducible bug reports, see [Troubleshooting](troubleshooting.md).
