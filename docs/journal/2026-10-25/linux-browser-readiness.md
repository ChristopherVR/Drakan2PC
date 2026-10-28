# Linux and browser ISO-only readiness audit

Date: 2026-10-25 (Australia/Brisbane).

## Result and scope

The checkout cannot currently be described as supporting a native Linux game or a browser game
when the user supplies only an ISO. Windows has the implemented ISO-to-game pipeline. Linux has
a translator build, and some reusable portable libraries. Browser support remains a porting goal.

This was a source/build/CI audit with the existing Python suite and repository guard run locally.
No runtime code was changed, no game was rebuilt, and existing combat/input work was left intact.
No native Linux gameplay, Wine/Proton compatibility, or browser execution was tested. Wine/Proton
would be a separate compatibility exercise and would not validate a native Linux or web port.

## Source evidence

Paths below are repository-relative.

| Area | Evidence | Consequence |
| --- | --- | --- |
| Build selection | `CMakePresets.json`: `game` inherits Windows/VS 2022; `linux-recompiler` sets `DRAKAN_BUILD_GAME=OFF` | Linux compiler support is not Linux game support |
| Native compile blocker | `ps2recomp/ps2xRuntime/CMakeLists.txt` unconditionally lists `src/lib/drakan_config_win.cpp`; that source unconditionally includes `windows.h` and uses `GetModuleFileNameA`, process and DPI APIs | A non-Windows runtime build needs an actual replacement and conditional source selection |
| Other host assumptions | `ps2recomp/ps2xRuntime/src/lib/Kernel/EeScheduler.cpp`: `_stricmp` and MXCSR intrinsics | Audit and replace Windows CRT and x86 assumptions as part of the core compile pass |
| Renderer | `runtime/gs/CMakeLists.txt`: non-Windows early return, D3D11/DXGI/D3DCompiler linkage; `runtime/gs/include/gsr/gsr.h`: HWND and D3D11 device/context/RTV callback semantics | Reuse GIF/GS algorithms, but add a real platform-neutral GPU/presentation boundary and another renderer implementation |
| Reference rendering | `ps2recomp/ps2xRuntime/src/lib/gs/gs_cpu_backend.cpp` and the generic host/raylib path exist | They do not demonstrate production Linux rendering, feature parity or acceptable performance |
| Sound and input | Root CMake adds `runtime/spu2` and `runtime/input` only under `WIN32`; runtime CMake similarly gates their integration and modern settings host | Removing Windows flags would remove gameplay services rather than port them |
| Audio output | `runtime/spu2/CMakeLists.txt` adds `AudioOutputWasapi.cpp` only on Windows | Keep mixer/libsd semantics and implement native Linux/browser audio sinks |
| Scheduler | `Kernel/EeScheduler.cpp`: guest-context ready queues, one executor, long-running `run()`, `waitForEvent()` and timed CV waits | The old portability doc's host-thread-per-guest-thread statement was stale; bounded browser stepping is still absent |
| Workers | `ps2_runtime.cpp`: game executor thread; `drakan_overrides.cpp`: resource worker and detached sound pump; `vu/ps2_vu1_async.cpp`: optional worker and queue waits | Browser worker placement, shutdown, pacing and blocking rules need explicit implementation |
| SIMD/code generation | `include/ps2_runtime.h`, `include/ps2_runtime_macros.h`, `src/lib/vu/ps2_vu1_simd.h`: SSE types/intrinsics; `cmake/Ps2xPlatform.cmake`: ARM SSE-to-NEON support | No wasm mapping was found; verify exact PS2 float/vector behavior, not just successful compilation |
| Compiler resources | Runtime CMake gives MSVC VU sources `/constexpr:steps200000000` and `/constexpr:depth2048`, and the runner `/STACK:268435456`; generated register macros document MSVC aliasing assumptions | Clang/GCC/wasm generated-code builds need their own correctness and resource checks; desktop build costs do not prove browser feasibility |
| ISO I/O | `runtime/hle/include/hle/IsoFs.h`: `void* f_` representing `FILE*`; `src/IsoFs.cpp`: `fopen`, `fseeko`/`_fseeki64`, locking | Contrary to the prior reference text, there is no injected byte-source interface here |
| Saves/settings | `ps2_vfs.cpp`: host filesystem/stdio; settings `Store.h`: replaceable file hooks | Browser persistence, flush/restore and quota/error behavior remain unimplemented |
| FMV dependencies | Runtime CMake has Windows FFmpeg prebuilt handling and non-Windows `pkg_check_modules(FFMPEG REQUIRED ...)` | Linux dependency discovery partly exists; browser-compatible decoder build/integration remains unverified |
| Player build/deployment | `Build-LocalGame.ps1`: Windows `game` preset, VS discovery, `drakan.exe`, DLL staging and `.cmd` launchers; setup kit uses Windows tools/Inno Setup | No equivalent one-input Linux or browser delivery pipeline exists |
| CI | `.github/workflows/build-tools.yml`: Windows runtime checks; Windows/Linux compiler-only matrix; no Emscripten/browser job | CI configuration provides no evidence of a playable Linux/web build; remote run results were not inspected |

## What "only the ISO" needs to mean

For the existing Windows pipeline, the ISO is the only required user-supplied game-data input.
The setup/builder still obtains tools/dependencies and compiles private game output. Only the
verified PAL SCES_500.06 ELF revision is accepted. `prepare_iso.py` checks SYSTEM.CNF, ELF32/MIPS,
entry `0x100008` and the locked ELF hash. `prepare_local_runtime.py` derives the VU1 image.
The pipeline does not ask for a separate PS2 BIOS. Optional enhancement packs/SDKs are outside
the base-game input requirement. This audit did not repeat a clean Windows install or ISO build.

For a browser, selecting an ISO solves access to assets, but does not supply the translated game
instructions as WebAssembly. Under this repository's rule against distributing game-derived
generated output, there must also be a private generation/compilation path. Two possible designs
need an explicit choice and implementation:

1. A local builder accepts the ISO, generates and compiles the private wasm bundle, and launches
   a local server/browser automatically. This can still be one user action, but needs an installed
   or bootstrapped toolchain and sufficient build resources.
2. The browser performs generation/compilation locally from the selected ISO. That requires a
   browser-hosted toolchain and measured memory/startup/storage feasibility; none was found here.

Neither design currently exists. A hosted page must not be claimed to run this static recompilation
from an ISO simply because it has a file picker. Avoid copying a multi-gigabyte disc wholesale into
wasm memory as the default design; implement random-access reads and measure the working set.

## Implementation order and acceptance gates

1. **Compile the actual core and private generated game with Linux Clang/GCC.** Select host services
   by platform; replace unconditional Win32/CRT dependencies; validate SIMD, aliasing, float behavior,
   constant evaluation and stack consumption. Keep using the existing root CMake and extend the
   authorized build entry point rather than inventing a separate game build system.
2. **Complete native Linux services.** Add window/presentation, GPU, input, SPU2 output and settings
   overlay implementations. Preserve game hooks and mixer behavior. Provide writable save/config
   locations and dependency packaging. A cross-platform renderer boundary should also serve web.
3. **Complete the browser execution model.** Compile the core/generated output to wasm; implement
   wasm SIMD or scalar equivalents and PS2 float semantics. Either implement bounded cooperative
   ticks for all required work or run blocking guest execution in workers with the required browser
   threading/hosting setup. Keep the page responsive during loads, pauses and shutdown.
4. **Complete browser host services and private build flow.** Add graphics, input, audio startup,
   random-access ISO reads, save/settings persistence, FMV decoding, error handling and browser
   launch/serving. Choose and validate one of the private wasm-generation approaches above.
5. **Prove the one-input experience.** From a clean supported Linux environment/browser profile,
   provide only the supported ISO; generate required private artifacts automatically; reach title,
   start gameplay, load another area, play FMV/audio, exercise keyboard/mouse/controller and menus,
   save, close, reopen and reload. Test ISO relocation/reselection, invalid revision rejection,
   read-only install locations and failed persistence. Record build time, peak compiler memory,
   runtime memory, frame times and supported browser/device combinations. Add Linux core CI and
   wasm compile/browser smoke coverage without committing/uploading game-derived outputs.

Optional NVIDIA/HD features should remain optional platform capabilities. They must not become
requirements for the Linux/browser base-game path.

## Validation performed

- `python scripts/run_tests.py`: **101 tests passed** on the Windows host.
- `python scripts/repository_guard.py`: **1267 approved; 0 prohibited tracked files**.
- WSL Ubuntu reports x86_64 Ubuntu 22.04.3; `cmake`, `clang++`, `g++`, `ninja` and `emcc` were not
  available on its PATH. Windows PATH also did not expose CMake/Clang/Emscripten; the existing
  private game's CMake cache points to bundled Visual Studio CMake. No Linux/Emscripten toolchain
  was installed or build attempted during this audit.
- Tests above cover existing tooling contracts, not Linux/web game behavior. Runtime and browser
  support remain unproven in addition to the concrete source/build blockers listed above.
