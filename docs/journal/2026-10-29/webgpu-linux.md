# WebGPU renderer: Linux on the shared path (phase 4)

Plan: [webgpu-renderer.md](../../proposals/webgpu-renderer.md). Previous step: [webgpu-overlay.md](webgpu-overlay.md).

Native Linux now builds and runs the code Windows runs: the GS bridge (`gs_gsr_bridge.cpp`) driving the WebGPU
renderer, the input module (`runtime/input` through `drakan_input_glue.cpp`'s full hook set), the settings overlay with
the modern title / pause / save / Tab screens, the `drakan_dlss` glue (DLSS itself stays D3D11-only) and HD movies. The raylib window, the
portable camera and the raylib UI remain only for builds without a GS renderer (the browser).

## What is platform-specific now

| Piece | Windows | Linux |
| --- | --- | --- |
| Window (`drakan_window.h`) | `drakan_window.cpp` (Win32) | `drakan_window_glfw.cpp`: GLFW without a client API, X11 surface |
| Input host (`HostInput`) | Raw Input / XInput (`runtime/input/src/HostInput.cpp`) | `drakan_input_glfw.cpp`, installed as a `HostInputBackend` |
| Overlay window platform | `OverlayWin32.cpp` (imgui_impl_win32, subclass) | `OverlayGlfw.cpp` (imgui_impl_glfw, chained callbacks) |
| Overlay renderer | `OverlayD3D11.cpp` or `OverlayWebGpu.cpp` | `OverlayWebGpu.cpp` |

- `drakan_window.h` grew what the bridge, pause gate, widescreen and settings host used Win32 for: surface handle, client
  size, minimised / focused, borderless fullscreen, monitor size and refresh, client resize and `keyDown(vk)` (the
  physical key state by virtual-key code, which the modern screens and the Alt+Enter / F11 / F2 hotkeys read).
- `SettingsOverlay` is split into a platform-free core (`Overlay.cpp`, `OverlayImpl.h`) and an `OverlayPlatform` per
  window system. `OverlayInit::hwnd` is a `void*` (HWND or GLFWwindow*).
- GLFW is the copy raylib builds (target `glfw`, X11); only its headers are used, so its symbols are linked once.
  `third_party/imgui/backends/imgui_impl_glfw.*` are ImGui v1.91.9's files.
- GLFW queries are main-thread only. The window caches size, focus and key state from its callbacks; the input host
  samples the gamepad and cursor in `mainThreadTick()` after each `glfwPollEvents`. The overlay renders on the present
  thread, which on both platforms is the host thread that pumps the window.
- Restart from the settings menu re-executes `/proc/self/exe` with the same arguments; the new process waits for the old
  one (`DRAKAN_WAIT_PID`, as on Windows).
- `Build-LocalGame.ps1` no longer writes `renderer = sw` into Linux play folders. Existing folders keep the line until it
  is removed (any value other than `sw` / `software` / `cpu` selects the GPU renderer).

## Validation (WSLg, llvmpipe Vulkan: a CPU rasteriser, so slow)

- `play_run.py` on the Linux build with `PS2X_GS=gpu`, 640x480, internal scale 1: gameplay in Surdana with HD
  textures; F1 (sent to the X11 window with `xdotool`) pauses through the modern pause menu and opens the PC Settings
  overlay in the game's art, and a second F1 closes it; Tab opens the modern Tab screen (Inventory with its tutorial
  box) with `DRAKAN_INVENTORY_SCREEN=modern`; the FPS counter draws. About 7 fps in gameplay on llvmpipe.
- Windows after the refactor (D3D11 default): the same tour (F1 overlay over the modern pause menu, Tab screen,
  borderless fullscreen, refresh-rate query) behaves as before.
- Runtime tests: 75 / 75 on Windows. On Linux every test passes except `portable_gpu` (the older WGSL compute raster,
  unchanged by this phase): it takes the Mesa D3D12-backed GL adapter in WSL and reads back zeros.

## Not done

- Wayland-native windows: raylib's GLFW is built for X11 (XWayland works).
- A hardware-GPU Linux run (WSL exposes Vulkan only through llvmpipe here).
- Removing raylib from the Linux build: it still provides audio initialisation and the software-GS fallback window.
