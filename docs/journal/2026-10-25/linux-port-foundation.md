# Linux runtime foundation

Date: 2026-10-25 (Australia/Brisbane).

## Scope and status

First implementation pass following the [readiness audit](linux-browser-readiness.md). The
Windows player build/deployment entry point remains unchanged. This pass adds Linux runtime-library
coverage and fixes concrete native compiler blockers. It does **not** deliver an ISO-only Linux
installer or a browser game. Existing input/menu/combat work in the shared checkout was preserved.

## Changes

- Root CMake now offers `linux-runtime-tests` and an explicitly experimental `linux-game` preset.
  The CI workflow builds/tests the portable Linux libraries without disc data.
- Runtime CMake selects `drakan_config_win.cpp` on Windows and `drakan_config_posix.cpp` otherwise.
  Linux resolves the executable through `/proc/self/exe`, independent of the launch directory.
  The fallback for hosts without `/proc` is the launch directory; DPI awareness remains Win32-only.
- Common scheduler/widescreen token comparisons now use ASCII folding, rather than `_stricmp`.
  VU helpers use the existing compiler-aware inline macro instead of bare `__forceinline`.
- Runtime consumers inherit native thread linkage and, on non-MSVC x86, SSE4.1 and disabled strict
  aliasing for the generated scalar/vector register aliases. Clang/GCC VU sources receive explicit
  constexpr budgets; Clang also receives a 2048 expression-nesting limit for the AOT registration folds.
- Fixed dependent-type `typename` requirements in GS memory templates, a VU enum collision with
  POSIX's `R_OK` macro and a 64-to-32-bit diagnostic header narrowing conversion. The same compile exposed out-of-bounds VU diagnostic ring reads:
  the ring now stores all fourteen registers the report prints, instead of ten.
- SPU2/libsd is linked on Linux. `AudioOutput.cpp` contains the shared gain/mute/pause/watchdog,
  levels, WAV dump, device-selection state and thread lifecycle previously embedded in WASAPI.
  The remaining Windows implementation is unchanged. `AudioOutputAlsa.cpp` owns Linux device I/O,
  uses nonblocking writes, handles underruns, retries missing devices and advances the mixer on a
  wall clock even with no audio hardware. Unusable ALSA plugin hints are excluded from enumeration.
  The device-rate field is atomic so concurrent device queries/reopens do not race.
- Shutdown stops the Drakan audio output on Linux as well as Windows. Non-Windows execution no
  longer passes a pthread native handle to the Windows-only host profiler interface.
- Software-only builds do not reference unlinked GSR setters in widescreen glue.
- Tests cover executable-location/configuration preservation, synthetic output behavior, ALSA's null
  device, and production VU SIMD conversion/classification/lane writes across rounding modes.
  The disc-dependent SPU2 test now uses CMake's Python interpreter and host-appropriate shell quoting.
  WAV outputs are isolated per binary tree, avoiding collisions between simultaneous Windows/Linux tests.
- IPO capability checking is lazy and shared across fast-release targets; library/test-only
  configurations avoid a compiler/linker probe they do not use.

## Validation

- Installed native build tools and ALSA/raylib/FFmpeg development dependencies in the existing WSL
  Ubuntu 22.04 environment, enabling actual GCC/Clang checks.
- Linux GCC: root `linux-runtime-tests` configure/build/CTest completed with **30 tests, no failures,
  two skips** (`hle_test`, `fmv_vdb`). Local disc sound data allowed the existing SPU2 suite to run;
  the synthetic output/null-device tests and SIMD test require no game data.
- Linux Clang 14: **the complete `ps2_runtime` static library compiled successfully**, including all
  four private VU AOT slices, FFmpeg-backed MPEG code, scheduler/syscalls and ALSA integration. A final
  incremental build after the last changes also succeeded. This used the existing private inputs in
  `build/final-game`, with a native sibling `runtime-build-linux` inside that same work directory.
- Two existing private generated translation units (`ps2_recomp_0000.cpp`, `ps2_recomp_0001.cpp`)
  passed Clang syntax checking. The complete generated library, game executable link and gameplay
  remain unvalidated; these checks do not establish optimized generated-code correctness.
- Linux output was also compiled directly in a temporary probe and passed no-device/null-device tests.
- Windows MSVC: runtime libraries/test executables rebuilt. Focused `audio_output`, `host_services`,
  `scheduler_policy`, `widescreen_policy` and `vu_simd_portability` tests all passed.
- The broad Windows run (excluding the long synthetic renderer sweep, as in CI) reported failures in
  `spu2_test` device-switch progress, `input_test` configuration reload and `native_display_commit`.
  Isolated retries reproduced failures. The audio failure also reproduced when linking the test with
  the **original, unmodified** `HEAD` WASAPI implementation, compiled in a private baseline probe.
  The other failing tests exercise concurrently edited input/display work; this pass does not claim
  the whole Windows suite is green.
- A source comparison confirmed the extracted common output methods and remaining WASAPI source
  exactly preserve the original implementation before the move. No claim of Linux physical-device
  playback or full Windows/Linux gameplay testing is made.
- `python scripts/run_tests.py`: **101 passed**. `python scripts/repository_guard.py`: no prohibited
  tracked files. `git diff --check`: clean. No generated game data or binaries were added to tracking.

## Remaining player/browser work

The runtime library now compiles; the full private generated-game build must finish and link before
the experimental game preset can be promoted. Validate generated-code behavior, stack use and performance with the
actual game. Then implement the Linux GPU/window, modern input and settings integrations and extend
the existing ISO build/deployment entry point with dependency bootstrap and a native play folder.

Browser work still includes a wasm SIMD/float path, worker/cooperative execution, browser GPU/audio/input,
random-access user ISO ingestion, persistent saves/settings and private ISO-to-wasm generation. The
POSIX fallback and portable libraries are reusable groundwork, not implementations of browser services.
