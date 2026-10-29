# Linux / browser parity review against Windows

Code-level audit of what the portable (Linux, browser) builds are missing relative to Windows. Source of truth:
`ps2recomp/ps2xRuntime/CMakeLists.txt` (Windows-only sources), `drakan_portable_ui.cpp` (`Host::capabilities`),
`drakan_input_glue.cpp` (Windows `hookPadMgrUpdate`) vs `drakan_portable_camera.cpp` (portable `pad`).
No runtime testing was done for this note.

## Shared and at parity

Game code, VU1, IOP/audio core, FMV decode, world rate (`video.world_rate`, default uncapped), HD texture packs
(software GS path), modern title / pause / save-load / PC Settings, combat controls, direct mouse camera and follow,
quick-slot keys and Quick Inventory buttons, dual wield, FOV, frame limit, vsync, flicker filter, sharp present,
native options persistence, controller input. Rumble is ignored on every platform (`hookPadSetActDirect` returns 1).

## Missing on Linux and browser

| Area | Windows | Portable | Where |
| --- | --- | --- | --- |
| GPU renderer | D3D11 GSR | CPU software GS; WGSL raster opt-in only (~19.6 fps Surdana) | `PS2X_HAVE_GSR` |
| Internal resolution scale | yes | disabled (`caps.internalResolution = false`) | `applyVideo` rejects scale |
| Widescreen / aspect, HUD scale, HUD aspect correction | yes | `video.aspect`, `video.hud_*` unavailable; 4:3 menu view | `drakan_widescreen.cpp` GSR calls |
| Texture filter, anisotropic, mip bias, texture upscale | yes | caps off | `drakan_settings_host.cpp:1500` |
| Modern water, ray-traced reflections / shadows | DXR | unavailable | `ray_water.cpp` |
| DLSS / DLAA | NGX | unavailable | `drakan_dlss.cpp` |
| Deinterlace option, smooth menu highlight | yes | unavailable | portable caps list |
| FPS overlay | `drakan_fpsmeter.cpp` | unavailable | |
| HD movie pack | `drakan_fmv_hd.cpp` | not built | CMake `if(WIN32)` |
| Narration subtitles | yes | stubs (`drakan_subtitles.cpp` is `PS2X_HAVE_DRAKAN_SETTINGS`) | |
| Modern Tab inventory, map / inventory mouse, inventory art hooks | yes | `general.inventory_screen` unavailable; hooks not installed | `drakanInstallInput` |
| Quicksave / quickload (F5 / F8) | `quickSaveTick` | not called from portable `pad` | `drakan_quick_save.inl` |
| Modern bow aim | `updateBowAim`, `hookBowLaunch` | not installed | |
| GSR save previews | yes | GSR-only | `drakan_save_previews.cpp` |
| Audio device selection | WASAPI | unavailable | `caps.audioDeviceSelect` |
| Window size (browser only) | n/a | canvas-owned | |

Low-cost items (no renderer work): quicksave/quickload, bow aim, narration subtitles (needs a portable text draw in the
raylib overlay), FPS overlay, ALSA device selection. The rest depend on a portable GPU renderer. (Correction: Windows
has no player-facing screenshot hotkey, only scripted test captures, so that is not a gap.)

## Quick wins: shared implementation

Done the same day without platform copies:

- `settings/Hud.h` (`drakan_settings_ui`, ImGui only) owns the HUD layer set and its draw helpers (stats panel, menu
  cursor, crosshair, subtitle band), moved out of the D3D11 `SettingsOverlay`. The D3D11 overlay and the portable
  raylib UI both draw `dsettings::hudLayers()` with the output size and the game image rectangle.
- `drakan_settings_common.h`: `CommonSettingsHost` (audio, subtitles, general/menu options, default input INI,
  renderer-independent video settings) is the base of both settings hosts; `installSharedHudLayers()` registers the FPS
  counter and the bow crosshair.
- Narration subtitles are built on every target (`subtitles/` path separator fixed); the browser pre-run copies the
  deployed `subtitles/*.srt` into the virtual filesystem.
- `drakan_fpsmeter.cpp` uses `steady_clock` and per-platform CPU time (thread CPU on Linux, none in the browser) and
  is configured from the shared install path. F2 cycles it on all targets.
- Portable input calls the shared `quickSaveTick` (F5 / F8) and installs the same `hookBowLaunch` / `updateBowAim`.
- ALSA resolves friendly device names from the settings page, so Linux offers audio device selection.

Still separate: the Windows `hookPadMgrUpdate` and portable `pad` PadMgr hooks feed the same input module from
different host input sources and keep two quick-slot drivers. Merging them into one hook is the next input step.


## Browser "ISO only"

The hosted launcher (`runtime/web/pages/loader.js`) needs a locally built `DrakanPC-web` folder plus the ISO, because
`drakan.wasm` contains translated game code and must not be distributed. ISO-only play therefore needs the game code to
be produced in the player's browser:

- Distribute a runtime-only Wasm (no game code) and generate the game module from the ISO's ELF at first launch, cached
  in OPFS / IndexedDB. Compiling the generated C++ in the browser is not realistic (the native build takes ~75 min), so
  this means a second recompiler backend that emits Wasm directly, linked to the runtime through a shared memory and
  function table (`replaceFunction` hooks by address still apply). The VU1 AOT image is also build-time derived and must
  move to the same load-time path.
- An interpreter / JIT is the alternative and would not reach full speed.

Renderer parity for the browser means a WebGPU GS renderer with framebuffer residency, which the WGSL raster work starts
but does not yet provide. WebGPU has no hardware ray tracing (compute-shader tracing would be needed) and no DLSS
(an identified FSR / TAA fallback). Linux can use Vulkan, where ray queries and DLSS are available.
