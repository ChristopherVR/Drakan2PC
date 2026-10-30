# Install on Windows

The installer builds Drakan locally from your own disc image. It supplies the tools and source code; the playable game is created on your computer.

## Before you start

You need your own **PAL SCES-500.06** disc as a plain `.iso`. The **USA SCUS-971.28** release is also accepted, with fewer features: see [USA disc support](#usa-disc-support-experimental). Other regions and BIN/CUE images are not supported. The builder validates the disc and stops if it does not match. Keep the ISO in a stable location: the game continues to read it during play.

There are two sets of requirements. **Building** the game is a one-time job that needs far more memory than playing it. **Playing** the finished game needs an ordinary gaming PC.

### Build specs (one time, about one to two hours)

| Requirement | What you need |
| --- | --- |
| Operating system | Windows 10 or 11, 64-bit |
| Memory | At least 16 GB; **24 GB or more recommended**. A few generated files need about 17 GB each while they compile; with less memory Windows uses its page file and the build is slow |
| CPU | Any 64-bit CPU. More cores build faster; the installer's **Build speed** page sets how many the build may use |
| Disk space | About 10 GB free: Microsoft's C++ Build Tools (about 3 GB), the build folder and the play folder |
| Network | Internet access for Microsoft's C++ Build Tools (first run only) and, if chosen, the NVIDIA DLSS SDK |
| Time | **One to two hours** for the first build, depending on CPU and memory; rebuilds after an update take minutes |

### Game specs (to play)

| Requirement | What you need |
| --- | --- |
| Operating system | Windows 10 or 11, 64-bit |
| Memory | **8 GB** recommended. The game itself uses about 1 GB at the title screen and about 3 GB in a level with HD textures and ray tracing on |
| CPU | A 64-bit CPU with **4 or more cores** recommended; the game keeps two to three cores busy |
| Graphics | A GPU with **Vulkan or Direct3D 12** drivers for the default WebGPU renderer. A Direct3D 11 GPU can use the **Direct3D 11** renderer instead (PC Settings > Display > Renderer). Optional effects: DLSS needs an NVIDIA RTX GPU; hardware ray tracing needs a DirectX Raytracing GPU and the Direct3D 11 renderer; ray tracing on the WebGPU renderer runs on any GPU but costs much more frame rate |
| Disk space | About 0.5 GB for the play folder, plus about 1.2 GB if you generate HD textures; the ISO must stay where it is |

Measured on an RTX 5080 PC (29–30 October 2026); slower hardware will run the optional effects at a lower frame rate.

## Install and build

1. Open the [GitHub releases page](https://github.com/ChristopherVR/Drakan2PC/releases/latest) and download **DrakanPC-Setup.exe**. If a release is unavailable, use the [source build instructions](../../BUILDING.md).
2. Run the installer. Choose an installation folder and your supported ISO.
3. Choose optional components if wanted. **NVIDIA DLSS** requires compatible hardware and acceptance of NVIDIA's SDK licence. **Browser version** requires WSL with Ubuntu. **Keep the PC responsive** compiles at low priority, so other programs stay smooth while the build runs.
4. On **Build speed**, enter how many CPU cores the build may use, or leave it empty for the automatic choice (half the cores, at most 4). Memory still limits how many compilers run at once. These settings only affect the build, not the game, and **Rebuild Drakan** keeps them.
5. Leave **Build the game now** selected. The builder installs Microsoft's C++ Build Tools if they are missing; Windows may ask for permission.
6. Leave the build window open until it reports completion. Some large files can take about ten minutes without new output.
7. Launch **Drakan - The Ancients' Gates** from the desktop or Start menu shortcut.

The playable Windows folder is `dist/DrakanPC` inside the installation. Launch its `drakan.exe` or the installed shortcut.

## Your first game

Press **F1** for PC Settings. Start with the current defaults, then choose your resolution, frame rate, mouse sensitivity and preferred interface. Use **F11** or **Alt+Enter** for borderless fullscreen, **Esc** to pause and **F10** to release the mouse.

Read [PC enhancements](enhancements.md) for the PS2 comparison, then [Controls & combat](controls.md) for movement, inventory, bow aiming and saves.

![PC Settings categories in the native Linux player](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/pc-settings.jpg)

## Interrupted build or update

Use **Rebuild Drakan** in the Start menu. It resumes an interrupted build and preserves saves, INI settings and existing texture packs. It uses the CPU cores and priority chosen in the installer; to change them, run `Build-Drakan.cmd -Cores 8 -Priority Low` from the installation folder (`-Cores 0` returns to the automatic choice, `-Priority Normal` to normal priority). Avoid starting a second build against the same work folder while the first is running.

Before changing installations, back up your saves and settings as described in [Saves & updates](saves.md). For failures, see [Troubleshooting](troubleshooting.md).

## USA disc support (experimental)

The builder also accepts the **USA release, SCUS-971.28**. It builds a separate, experimental version; the PAL release
remains the supported one. The full feature table is in [disc regions](../reference/regions.md).

- **Works (run on Windows, 30 October 2026):** building from the USA ISO; the memory-card screen, title screen and New
  Game into the first level; keyboard menu navigation, Esc for the pause menu, WASD movement and mouse look (the mouse
  directly changes the camera angles, with persistent pitch and live sensitivity); the default keys from `drakan_input.ini`.
- **Harness verified:** F1 PC Settings and Options > PC Settings, with shared native-pause
  integration and input isolation. Controls requiring unported features are disabled.
- **Not available on the USA build:** the native in-game key binding screen, widescreen and field of view, the frame-rate options and PC frame pacing, modern bow aim, the modern Tab inventory and M map screens, quick slots, quicksave and quickload, the 16
  save pages and the title Quit row, subtitles, the automatic skip of the memory-card warning (press the confirm key),
  DLSS and the scene enhancements, Linux and browser builds.
- **Not tested yet:** gamepads, saving and loading, sound quality, later levels and movies, HD textures.
- **Where it goes:** `Build-LocalGame.ps1` puts a USA build in `dist/DrakanPC-usa`, next to the PAL `dist/DrakanPC`.
  The installer and `Build-Drakan.cmd` build whichever disc you choose into `dist\DrakanPC`; choosing the other
  release later rebuilds from scratch.
- **Wrong disc or build:** an incremental rebuild with the other release's ISO stops with an error (build it into a
  fresh work folder); a `drakan.exe` started with the other release's disc image shows "wrong disc for this build" and
  exits. Saves are region-specific: the USA game writes `mc0/BASCUS-97128…`, the PAL game `mc0/BESCES-50006…`, so a
  save from one release does not appear in the other.

## Build from source or on Linux

Linux users should start with [Install on Linux](linux.md). Developers should use the single supported pipeline in [Build from source](../../BUILDING.md). The [platform reference](../reference/portability.md) describes differences from the Windows renderer.
