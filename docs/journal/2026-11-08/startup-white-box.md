# Start-up white box

Symptom: launching `dist/DrakanPC/drakan.exe` showed a black screen with a large white rectangle in the middle for
about five seconds, before the first "Loading options data" frame.

Cause: the window is created windowed (1280x960 client, centred, hidden). Inside `enableD3D11` the start-up window
mode turns it borderless fullscreen with `SWP_SHOWWINDOW`, so it becomes visible while the renderer start-up still runs
(shader cache, HD FMV probes) without pumping messages. `WM_ERASEBKGND` returns 1 without painting and the swapchain
has not presented yet, so DWM composed a surface nobody had drawn: white over the old windowed rect, black elsewhere.
The in-game `GSR_WINSHOT` captures never showed it because it is not in the swapchain; only a desktop capture does.

Fix: `drakan_window.cpp` fills the client area black with GDI right after the fullscreen `SetWindowPos` and in
`show()`, both before the first present. Desktop captures every 150 ms after launch now show a solid black screen
until the first rendered frame.

## Other renderers

- WebGPU (`renderer = webgpu` / `PS2X_GS=webgpu`) uses the same native window and fullscreen switch, so the same fix
  covers it; a desktop capture of its start-up is black until the first frame.
- The fallback window (raylib, used when D3D11 / WebGPU cannot start, and for `renderer = sw`) had its own box: until
  the first guest frame the software presenter uploaded a magenta placeholder (`ps2_runtime.cpp`). It is black now;
  `PrintWindow` captures of that window from its first visible frame show black, then the loading text.
