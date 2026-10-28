# Licenses

## This project

Drakan2PC is distributed under the **GNU General Public License, version 3** ([LICENSE](LICENSE)). It is built on
[PS2Recomp](https://github.com/ran-j/PS2Recomp) (GPL-3.0) and links code adapted from PCSX2 (GPL-3.0-or-later), so
the project as a whole, and any game you build with it, is covered by the GPL-3.0. Individual files may carry a more
permissive notice; that notice applies to the file on its own.

## Included third-party code

| Component | Licence | Where |
| --- | --- | --- |
| PS2Recomp (recompiler and runtime fork) | GPL-3.0 | `ps2recomp/`, [ps2recomp/LICENSE](ps2recomp/LICENSE) |
| PCSX2-derived SPU2, GS tables and VU semantics | GPL-3.0-or-later | `runtime/spu2`, `runtime/gs`, `runtime/vu1ref` |
| xBRZ scaler (reimplementation) | GPL-3.0-or-later | `runtime/gs` |
| FFmpeg-derived MPEG decoder tables | LGPL-2.1-or-later | `runtime/fmv`, [licenses/FFmpeg-LGPL-2.1.txt](licenses/FFmpeg-LGPL-2.1.txt) |
| ps2sdk libsd/sdrdrv interface behaviour | AFL-2.0 (credited) | `runtime/spu2` |
| Dear ImGui | MIT | `third_party/imgui` |
| Gelasio font | SIL Open Font License 1.1 | `third_party/fonts/gelasio` |
| Roboto font | Apache-2.0 | [licenses/Roboto-Apache-2.0.txt](licenses/Roboto-Apache-2.0.txt) |
| wgpu-native licence texts | Apache-2.0 / MIT | `third_party/wgpu-native` |

Attribution details and source commits are in [THIRD_PARTY.md](THIRD_PARTY.md).

## Downloaded at build time (not included here)

| Component | Licence |
| --- | --- |
| ELFIO, toml11, fmt, libdwarf, Rabbitizer (recompiler) | Their own licences (MIT / BSD / LGPL), pinned in `scripts/buildtool/toolchain.lock.json` |
| raylib | zlib |
| FFmpeg prebuilt libraries | LGPL-2.1-or-later |
| NVIDIA DLSS SDK (optional, opt-in) | [NVIDIA RTX SDKs License](https://github.com/NVIDIA/DLSS/blob/v310.9.16/LICENSE.txt), accepted by the player before download |
| Microsoft Visual Studio Build Tools (installer only) | [Microsoft licence terms](https://visualstudio.microsoft.com/license-terms/) |
| PowerShell, Python (bundled by the installer) | MIT, PSF License |

## The game

No licence of any kind is granted for *Drakan: The Ancients' Gates* or its content. The game, its code and its assets
belong to their respective owners and are not part of this project. See the legal notice in [README.md](README.md).
