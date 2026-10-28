# Third-party source

This repository contains compatibility/runtime source and build tooling. Game
data, generated game programs, private transcripts and NVIDIA binaries must not be
part of the source repository or compiler artifacts.

| Component | Notice |
| --- | --- |
| PS2Recomp compiler and runtime (`ps2recomp/`) | In-repo fork of [ran-j/PS2Recomp](https://github.com/ran-j/PS2Recomp) at upstream commit `eb8688b`, with the Drakan changes applied directly; GPL-3.0 ([ps2recomp/LICENSE](ps2recomp/LICENSE), [copy](licenses/PS2Recomp-GPL-3.0.md)). |
| Dear ImGui | Vendored with its [MIT license](third_party/imgui/LICENSE.txt). |
| Gelasio Regular / Bold (`third_party/fonts/gelasio/`) | [SIL Open Font License 1.1](third_party/fonts/gelasio/OFL.txt) (no Reserved Font Name); instanced from the variable font at weights 400 / 700, subset to Latin-1 / Latin Extended-A and compressed into sources. The UI text face (metric-compatible with Georgia). |
| Roboto Medium and compressed font representation | [Apache-2.0](licenses/Roboto-Apache-2.0.txt); font copyright remains with the Roboto authors. |
| FMV MPEG tables/decoder adaptation | See [FMV attribution](runtime/fmv/README.md) and [LGPL-2.1](licenses/FFmpeg-LGPL-2.1.txt). |
| SPU2 mixer, ADSR, ADPCM and reverb (`runtime/spu2/`) | Ports of PCSX2's SPU2 `Mixer.cpp`, `ADSR.cpp`, `Reverb.cpp`, `ReverbResample.cpp` and `XA_decode_block` (GPL-3.0-or-later, PCSX2 Dev Team); attribution is kept in each file header. |
| SPU Gaussian interpolation table (`runtime/spu2/src/GaussTable.inc`) | Reconstructed from PCSX2's **GPL-3.0-or-later** `pcsx2/SPU2/interpolate_table.h` at commit `f0523f8acb1d3a765ee73b74cffec63093569199` (PCSX2 Dev Team; generator credited to Near/nocash/Ryphecha). All 512 values were verified unchanged. |
| ps2sdk freesd behaviour (`runtime/spu2/`) | The libsd register layer (`Spu2.cpp` init/register handling, `Sdr.h` dispatch) reproduces the libsd/sdrdrv interface behaviour as documented by [ps2dev/ps2sdk](https://github.com/ps2dev/ps2sdk) freesd/sdrdrv (AFL-2.0), with credit to the ps2sdk authors. `Libsd.h` holds the interface constants and structure layouts. No ps2sdk data tables are included: Note2Pitch/Pitch2Note are an independent implementation and the reverb presets are read from the player's own `LIBSD.IRX` at runtime. |
| GS swizzle tables (`runtime/gs/src/gs_mem.h`) | Page/block/column layout follows PCSX2's `GSLocalMemory`/`GSTables` (GPL-3.0-or-later), cross-checked against the GS manual. |
| VU1 reference simulator (`runtime/vu1ref/`) | Instruction and flag semantics follow PCSX2's VU interpreter (`VUops.cpp`, `VUflags`; GPL-3.0-or-later). |
| xBRZ texture scaler (`runtime/gs/`) | Reimplementation of Zenju's xBRZ algorithm (GPL-3.0-or-later). |
| VIF1 interpreter (`ps2recomp/ps2xRuntime/src/lib/ps2_vif1_interpreter.cpp`) | From upstream PS2Recomp, based on the Blackline Interactive implementation (credit kept in the file header). |
| NVIDIA DLSS SDK | Optional. Not included in this repository or in any package: `scripts/buildtool/fetch_dlss_sdk.py` downloads it from [NVIDIA/DLSS](https://github.com/NVIDIA/DLSS) (tag pinned in `scripts/buildtool/dlss.lock.json`) only after the player accepts the [NVIDIA RTX SDKs License](https://github.com/NVIDIA/DLSS/blob/v310.9.16/LICENSE.txt). DLSS runs only on NVIDIA GPUs and the game credits it in its PC settings. NVIDIA, RTX and DLSS are trademarks of NVIDIA Corporation. |
| wgpu-native licence texts | Apache-2.0 and MIT licence texts shipped in `third_party/wgpu-native/`; binary is downloaded locally by the build. |

Compiler dependencies (ELFIO, toml11, fmt, libdwarf, Rabbitizer, pinned in `scripts/buildtool/toolchain.lock.json`) are downloaded at build time and retain their own license files in compiler
source packages. Runtime dependencies such as raylib and FFmpeg are fetched
locally by their build configuration. The optional NVIDIA DLSS SDK is downloaded by
the player on request (see above); builds without it simply leave DLSS out.

No license is granted here for the original game or its assets. This notice
does not relicense third-party components.
