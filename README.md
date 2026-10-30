# Drakan2PC

A PC port of **Drakan: The Ancients' Gates** (PlayStation 2, 2002), built by **static recompilation**: the game's
own PS2 program is translated to C++ and compiled into a native Windows or Linux executable, or a WebAssembly build for the
browser. A new runtime supplies graphics, audio, input, saves and the PS2 system services the game expects.

**You need your own copy of the game.** This repository holds only source code and build tools. The game is built on
your PC from a disc image of your own disc; the builder does not upload your game files.

**[Project website & user guides](https://christophervr.github.io/Drakan2PC/)** ·
[Install on Windows](docs/guides/install.md) · [Install on Linux](docs/guides/linux.md) · [Controls](docs/guides/controls.md) ·
[Troubleshooting](docs/guides/troubleshooting.md) · [Developer documentation](docs/guides/development.md)

## PC enhancements compared with PS2

The original adventure, with configurable PC improvements. Windows has the fullest feature set;
Linux and browser support differs ([platform details](docs/reference/portability.md)).

| Area | PC improvements |
| --- | --- |
| **60+ FPS support** | 30, 60 and uncapped world frame-rate modes. Actual FPS depends on hardware and scene; the original 30 FPS cadence remains available. |
| **Enhanced graphics, optional** | Higher resolutions, widescreen, borderless fullscreen, improved filtering and HD texture replacement. Optional DLSS Super Resolution / DLAA on compatible NVIDIA RTX GPUs, and ray-traced water/shadows on compatible DXR hardware. Enhancements can be disabled. |
| **Mouse and keyboard** | Direct mouse look, WASD movement/strafe, rebindable controls, sensitivity/inversion settings, keyboard prompts and mouse-driven menus. XInput controllers remain supported on Windows. |
| **Modern bow aiming, configurable** | Hold left mouse to aim with a crosshair and slight zoom; release to shoot. Choose Classic for the original bow controls. This changes the existing bow, rather than adding a crossbow weapon. |
| **Dual wielding, configurable** | Equip a second one-handed weapon in Rynn's left hand instead of a shield; attacks alternate hands. Can be enabled or disabled in PC Settings. |
| **Modern inventory, configurable** | Optional full-screen PC inventory on **Tab**, with the original inventory still selectable. |
| **Dedicated map and inventory strip** | **M** opens a full-screen map; **I** opens an equipment strip over the world. Each can revert to the normal inventory pages independently of the Tab inventory setting. |
| **PC Settings and menus** | **F1** opens display, audio, input and gameplay options. Configurable modern title, pause and save/load screens, plus original-menu choices. |
| **Upscaled video, optional** | Locally generated **4× AI-upscaled intro, outro and logo movies** on Windows. Original video remains available; a movie pack must be generated separately. |
| **Narration subtitles** | Added intro/ending subtitle support with scalable text, alongside the game's existing dialogue subtitles. Requires local language-specific SRT files; transcripts are not included. |
| **Save conveniences** | **F5 quicksave / F8 quickload**, additional manual save pages and optional modern save previews. Native saves live beside the game; browser saves use browser storage. |
| **More ways to play** | Native Windows and Linux builds, plus WebAssembly through the local or [hosted browser player](https://christophervr.github.io/Drakan2PC/browser.html). |

The modern Tab inventory, dedicated map/strip overlays and advanced graphics are most complete on Windows;
do not assume full Linux/browser parity. HD textures and movies are generated from your own disc, and narration
SRTs are supplied locally. No such packs or game assets are distributed here.

See **[the full PS2-to-PC comparison](docs/guides/enhancements.md)** for feature details, platform limits and
original-style options; [PC settings](docs/guides/settings.md) and [optional extras](docs/guides/extras.md) explain setup.

## Screenshots and demonstrations

Development captures of the PC port. Click a screenshot to enlarge it. The GIFs below are reduced-size demonstrations, not frame-rate benchmarks.

| Modern Tab inventory on Windows | Full-screen map showing Surdana and objectives |
| --- | --- |
| [![Modern Tab inventory on Windows](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/modern-inventory.jpg)](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/modern-inventory.jpg) | [![Full-screen map showing Surdana and objectives](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/fullscreen-map.jpg)](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/fullscreen-map.jpg) |

| Inventory strip over gameplay with a bow selected | PC Settings categories in the native Linux player |
| --- | --- |
| [![Inventory strip over gameplay with a bow selected](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/inventory-strip.jpg)](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/inventory-strip.jpg) | [![PC Settings categories in the native Linux player](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/pc-settings.jpg)](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/pc-settings.jpg) |

| Side-by-side texture comparison, original on left and HD replacements on right | Modern save browser with a Surdana save preview |
| --- | --- |
| [![Side-by-side texture comparison, original on left and HD replacements on right](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/hd-textures.jpg)](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/hd-textures.jpg) | [![Modern save browser with a Surdana save preview](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/save-browser.jpg)](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/save-browser.jpg) |

| Rynn in Surdana in the native Linux player | Rynn in Surdana in the browser player |
| --- | --- |
| [![Rynn in Surdana in the native Linux player](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/linux-gameplay.jpg)](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/linux-gameplay.jpg) | [![Rynn in Surdana in the browser player](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/browser-gameplay.jpg)](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/browser-gameplay.jpg) |

<details>
<summary>Watch dual wielding (sampled capture sequence)</summary>

![Rynn holding a one-handed weapon in each hand](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/dual-wield.gif)

Optional dual wielding on Windows: a second one-handed weapon takes the shield slot. Animation assembled from sampled test frames; not a frame-rate demonstration.

</details>

<details>
<summary>Watch a short HD intro excerpt</summary>

![A dragon illustration in the locally upscaled intro movie](https://github.com/ChristopherVR/Drakan2PC/releases/download/site-media-20261028/hd-movie.gif)

A short excerpt from the locally generated 4x HD intro pack. The movie pack itself is not distributed.

</details>

[More screenshots, setup instructions and optional features](docs/guides/enhancements.md).

## Supported disc

**PAL release, SCES-500.06**, as a plain `.iso` image. The builder checks the disc and stops on any other version.

**USA release, SCUS-971.28 (experimental, Windows):** builds into `dist/DrakanPC-usa` and plays with keyboard,
direct mouse look, PC settings and the game's own menus. Settings/pause and mouse sensitivity reloads have harness coverage;
widescreen, frame-rate options, modern Tab/map screens, quicksave, save pages and subtitles remain unavailable. Saves are region-specific, and a build refuses the
other release's disc. Details: [USA disc support](docs/guides/install.md#usa-disc-support-experimental) and
[disc regions](docs/reference/regions.md).

## Getting started

### Installer (Windows)

1. Download **[DrakanPC-Setup.exe](https://github.com/ChristopherVR/Drakan2PC/releases/latest/download/DrakanPC-Setup.exe)**
   from the [latest release](https://github.com/ChristopherVR/Drakan2PC/releases/latest) and run it (or build it
   yourself with `python scripts/buildtool/package_builder_kit.py --installer`).
2. Choose an install folder and your `.iso`. Optionally tick **NVIDIA DLSS** (accepts NVIDIA's SDK licence) and
   **browser version** (needs WSL with Ubuntu), and choose how many CPU cores the build may use.
3. Leave "Build the game now" ticked. On first run the builder installs Microsoft's free C++ Build Tools. The first
   build takes one to two hours; some files take about 10 minutes each with no output.
4. Start the game from the **Drakan - The Ancients' Gates** shortcut.

| | Build specs (once) | Game specs (to play) |
| --- | --- | --- |
| OS | Windows 10/11 64-bit | Windows 10/11 64-bit |
| Memory | 16 GB, 24 GB or more recommended | 8 GB |
| CPU | Any 64-bit; more cores build faster | 4 cores |
| GPU | Not used | Vulkan or Direct3D 12 drivers (Direct3D 11 renderer for older cards); NVIDIA RTX for DLSS |
| Disk | About 10 GB | About 0.5 GB, plus 1.2 GB for HD textures |

Details in [Install on Windows](docs/guides/install.md#before-you-start). The game reads the `.iso` while it runs, so
leave it where it is. "Rebuild Drakan" in the Start menu continues an
interrupted build and keeps your saves and settings.

### Linux

On x86-64 Ubuntu 22.04+ or Debian, clone this repository and run the ISO bootstrap from its root:

```sh
bash scripts/buildtool/Build-Drakan.sh "/path/to/Drakan.iso"
./dist/DrakanPC-linux/Play-Drakan.sh
```

The bootstrap installs missing public dependencies and reuses `build/final-game` for incremental rebuilds.
See **[Install on Linux](docs/guides/linux.md)** for requirements, controls, saves and troubleshooting.

### Browser

Build the browser version (installer option, or `Build-LocalGame.ps1 -TargetPlatform web`). Then either run
`Play-Browser.cmd` in `dist\DrakanPC-web`, or open the
**[browser player page](https://christophervr.github.io/Drakan2PC/browser.html)** in desktop Chrome or Edge and choose your
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

- **Bring your own game.** Source and installer packages contain no disc images, game executables,
  translated game code or playable game data. The game is built locally from a disc image you make from a disc you own.
  Documentation links to separately hosted screenshots and short demonstration clips; these are not game packs.
- **Use your own disc.** Do not download disc images you do not own, and do not share disc images, built games or
  files generated from your disc. This project does not support piracy.
- **Trademarks.** Drakan is a trademark of its respective owner. PlayStation and PS2 are trademarks of Sony
  Interactive Entertainment Inc. NVIDIA, RTX and DLSS are trademarks of NVIDIA Corporation. All other trademarks
  belong to their owners and are used only to identify the products they refer to.
- **No warranty.** The software is provided as is, without warranty of any kind, as set out in the GPL.
- **Rights holders** with concerns about this project can open an issue on this repository to contact the maintainer.
