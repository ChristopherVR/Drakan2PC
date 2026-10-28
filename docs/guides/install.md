# Install on Windows

The installer builds Drakan locally from your own disc image. It supplies the tools and source code; the playable game is created on your computer.

## Before you start

| Requirement | What you need |
| --- | --- |
| Game disc | Your own **PAL SCES-500.06** release, as a plain `.iso` |
| Operating system | Windows 10 or 11, 64-bit |
| Memory | At least 16 GB; **24 GB or more recommended** because some compilation steps use about 17 GB |
| Disk space | About 10 GB free for the basic setup; allow more for optional generated packs |
| Graphics | A DirectX 11-capable GPU; optional effects have additional requirements |
| Network | Internet access for the initial tool and dependency downloads |
| Time | Allow **one to two hours** for the first build; your hardware affects the time |

Other regions and BIN/CUE images are not supported. The builder validates the disc and stops if it does not match. Keep the ISO in a stable location: the game continues to read it during play.

## Install and build

1. Open the [GitHub releases page](https://github.com/ChristopherVR/Drakan2PC/releases/latest) and download **DrakanPC-Setup.exe**. If a release is unavailable, use the [source build instructions](../../BUILDING.md).
2. Run the installer. Choose an installation folder and your supported ISO.
3. Choose optional components if wanted. **NVIDIA DLSS** requires compatible hardware and acceptance of NVIDIA's SDK licence. **Browser version** requires WSL with Ubuntu.
4. Leave **Build the game now** selected. The builder installs Microsoft's C++ Build Tools if they are missing; Windows may ask for permission.
5. Leave the build window open until it reports completion. Some large files can take about ten minutes without new output.
6. Launch **Drakan - The Ancients' Gates** from the desktop or Start menu shortcut.

The playable Windows folder is `dist/DrakanPC` inside the installation. Launch its `drakan.exe` or the installed shortcut.

## Your first game

Press **F1** for PC Settings. Start with the current defaults, then choose your resolution, frame rate, mouse sensitivity and preferred interface. Use **F11** or **Alt+Enter** for borderless fullscreen, **Esc** to pause and **F10** to release the mouse.

Read [PC enhancements](enhancements.md) for the PS2 comparison, then [Controls & combat](controls.md) for movement, inventory, bow aiming and saves.

![PC Settings categories in the native Linux player](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/pc-settings.jpg)

## Interrupted build or update

Use **Rebuild Drakan** in the Start menu. It resumes an interrupted build and preserves saves, INI settings and existing texture packs. Avoid starting a second build against the same work folder while the first is running.

Before changing installations, back up your saves and settings as described in [Saves & updates](saves.md). For failures, see [Troubleshooting](troubleshooting.md).

## Build from source or on Linux

Linux users should start with [Install on Linux](linux.md). Developers should use the single supported pipeline in [Build from source](../../BUILDING.md). The [platform reference](../reference/portability.md) describes differences from the Windows renderer.
