# gsr - PS2 Graphics Synthesizer renderer (Direct3D 11)

Standalone C++20 static library (no external deps, Windows SDK only: d3d11, dxgi, d3dcompiler). Feed it GIF packets
and privileged register writes; it keeps a 4 MB swizzled GS local memory, renders on the GPU and presents the
DISPFB/DISPLAY circuits.

Attribution: the page/block/column swizzle tables in `src/gs_mem.h` follow the layout of PCSX2's
GSLocalMemory/GSTables (GPL-3.0-or-later) and were cross-checked against the PS2Recomp tables. Treat this directory as
GPL-3.0-or-later derived.

## Build / test

```powershell
$cm = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cm -S runtime\gs -B build\gs\cmake -G "Visual Studio 17 2022" -A x64
& $cm --build build\gs\cmake --config Release
build\gs\cmake\Release\gsr_test_swizzle.exe      # address tables (known values, bijection, round trip, cross-check vs PS2Recomp)
build\gs\cmake\Release\gsr_test_synthetic.exe    # [--warp] [--scale N] -> build\gs\synthetic_scene1.png/.bmp, seam_test_*.png, fade_test.png + pixel checks (41 checks: scene, seam, fog/blend regression, 13 texture-format decode cases, RT-as-texture alpha)
build\gs\cmake\Release\gsr_replay.exe <cap.bin>  # replay a DRKGSCAP capture, one PNG per vsync in build\gs\
build\gs\cmake\Release\texaudit.exe <cap.bin> --pool build\gs\texaudit --report build\gs\texaudit.txt   # texture audit (see below)
```
Texture ground-truth pool (needs the disc image, writes `pool.bin`): `python -I runtime\gs\tests\texaudit_pool.py build\gs\texaudit`.
Link `gsr` (CMake target, public include dir `include/`) and include `gsr/gsr.h`.

## API (`gsr::Renderer`, header `include/gsr/gsr.h`)

Mirrors PS2Recomp's `GS` class entry points so it can replace it:

| PS2Recomp `GS` | gsr |
|---|---|
| `processGIFPacket(data,size)` | `processGIFPacket` (PATH3) or `submitGifPacket(GifPath, data, size)` |
| `writeRegister(reg, val)` | `writeRegister(reg, val)` (GS general regs 0x00..0x62) |
| `GSRegisters` writes (PMODE, DISPFB1...) | `writePriv(addr_or_offset, val)` / `readPriv` (PMODE..BGCOLOR, CSR, IMR, BUSDIR, SIGLBLID) |
| vsync + `latchHostPresentationFrame` | `vsync(present=true)` / `present()` |
| `consumeLocalToHostBytes` | same |
| `ReadVram/WriteVram` | `readVram/writeVram` (flush GPU first) |

```cpp
gsr::Config cfg; cfg.hwnd = hwnd; cfg.scale = 3; auto gs = gsr::Renderer::create(cfg);
gs->onFinish = [&]{ csr |= 2; };           // GS FINISH register written
gs->submitGifPacket(gsr::GifPath::Path1, xgkickData, bytes);   // VU1 XGKICK
gs->submitGifPacket(gsr::GifPath::Path3, dmaData, bytes);      // GIF DMA (IMAGE data may continue across calls)
gs->writePriv(0x12000070, dispfb1);       // etc.
gs->vsync();                              // each vblank (draws display to window, sets CSR.VSINT, toggles FIELD)
```

Integration notes for the runtime owner:
* Single-threaded API: call from one thread (or wrap with your own mutex). Packets are processed synchronously;
  draws are batched and issued at state-change/vsync/transfer boundaries.
* GIF tag state is kept per path, so packets may be split at any quadword boundary (a PATH3 IMAGE can span calls).
  Arbitration between paths (MSKPATH3 etc.) stays in the runtime's GIF arbiter; feed packets in the order the GS would see them.
* CSR: gsr keeps its own CSR (`readPriv(0x12001000)`: SIGNAL/FINISH/VSINT/FIELD + REV/ID); use `onFinish`, `onSignal`, `onVsync` to
  drive the runtime's own interrupt/semaphore logic. Writing CSR through `writePriv` clears event bits (write-1-clear).
* Direct CPU writes to `vram()` must be followed by `vramTouched(firstBlock, count)` (invalidates textures/targets). Before reading
  `vram()` from the CPU (screenshots, GS->EE BUSDIR) call `flushToMemory()` (GPU render targets are written back, swizzled).
* Local->host (TRXDIR=1) data is queued and drained with `consumeLocalToHostBytes`.
* `captureDisplay()` returns the merged display as RGBA (display size x scale); `captureFramebuffer()` dumps any FBP.
* Capture replay: the runtime's `DRKGSCAP` capture format is parsed by `tests/replay.cpp` (record layout documented in its header; docs/reference/gs-capture.md
  did not exist yet when this was written). Captures contain no initial local-memory image, so start recording at boot.

### Live settings (settings overlay; any thread)
`Renderer::setAspect(forcedAspect, stretch)`, `setTextureFilter(0 game|1 point|2 bilinear)`, `setDeinterlace(0 weave|1 bob|2 blend)`, `setFlickerFilter`, `setSharpPresent`,
`setAnisotropic(1..16)`, `setMipBias(bias, autoBias)`, `setTextureUpscale(factor 0 auto|1 off|2|4|8, filter 0 auto|1 xbrz|2 smooth)`, `setTextureReplace(on)`, `setTextureDump(on)`,
`rescanTextures()`, `setInternalScale(1..8)`, `setVsync(on)`, `setFrameLatency(1..4)`, getters `internalScale()` / `vsyncEnabled()` (requested value).
* Callable from **any thread**; they only take a tiny private mutex (never the renderer lock, so a UI thread does not wait for a long GIF packet) and queue the value. All queued
  changes are applied together at the next **frame boundary** (`vsync()`, `present()`, `presentLatched()`, `captureDisplay()`) under the renderer's recursive mutex, after the
  pending draw batch was flushed, so a draw never sees half a change. `Stats::liveApplied` counts batches. Setting a value that is already in effect costs nothing.
* Present options (aspect / stretch, deinterlace, sharp, filter, aniso, mip bias, flicker) are plain field writes, effective in the same frame. The flicker filter is a display-state
  option of the core, the others live in the backend.
* **Texture upscale factor/filter, HD replace, dump, rescan** flush the texture and upscaled-texture caches (`flushTexCaches`: decoded textures are decoded again from local memory on the
  next draw, one frame of CPU work) and bump a generation number so results of worker jobs that were already running are discarded when they arrive. Replacement index, `exclude.txt` and
  the dump list are re-read from `Config::textureDir`; the worker pool is created on demand if upscaling is switched on later.
* **Internal scale**: `flushAll()` writes every dirty render target back to GS local memory, all render targets and depth buffers are dropped and re-created on demand at the new size and
  re-filled from local memory (nearest upscale of the 1x data), the "auto" upscale factor cache and `mip_bias = auto` are recomputed. The latched display image keeps being presented
  (with the scale it was rendered at, `latchedScale`) until the same `vsync()` re-latches it at the new size. Cost: depth buffers and the sub-native detail of what was drawn at the old scale are
  lost, so the frame in which the change happens can look soft / have z glitches for one frame; test: the first frame after 1 -> 2 -> 1 keeps its content (`gsr_test_live`).
* Frame latency: `IDXGISwapChain2::SetMaximumFrameLatency`. VSync: `Present(syncInterval)` from then on.
* Memory: D3D11 destroys released resources only when the context stops referencing them; the live path unbinds and `Flush()`es after dropping targets / caches (without it the private
  bytes grew ~45 MB per scale cycle). `gsr_test_live` cycles scale and upscale settings 8 times and checks the private bytes stay flat.
* Test: `gsr_test_live [capture.bin] [--frames N] [--warp]` replays `build/captures/gscap_20261016_194452.bin` (first 700 vsyncs, HUD from frame ~536) twice (synchronous / asynchronous upscale
  jobs): deterministic boundary checks, texture-setting toggles, scale cycles with a memory check, then a second thread hammering random setters (millions of calls) while the replay runs and
  captures; ends with destroying the renderer with jobs in flight. 34 checks.

## Architecture

* `gs_core.cpp` - GIF parsing (PACKED/REGLIST/IMAGE, A+D, PRE), register file for both contexts (PRMODE/PRMODECONT honoured), vertex queue &
  primitive assembly (point, line, linestrip, triangle, strip, fan, sprite; ADC/XYZ3 no-kick), CLUT buffer (CSM1/CSM2, CLD 0-5, CSA, CT32/24/16/16S),
  host->local / local->host / local->local transfers, privileged regs.
* `gs_mem.h` - 4 MB local memory, address functions for PSMCT32/24/16/16S, T8, T4, T8H, T4HL, T4HH, Z32/24/16/16S, per-block write stamps.
* `d3d11_backend.cpp` + `gs_shaders.h` - GPU side:
  * render targets keyed by (FBP, FBW, 32/16-bit class), RGBA8, resolution scale 1..8, created lazily from local memory; GPU/CPU coherence by
    write stamps per page row (CPU/DMA writes reload rows, GPU draws mark rows dirty and are read back before any local-memory read);
  * depth buffers (D32F) keyed by ZBP, GEQUAL/GREATER/ALWAYS/NEVER, ZMSK;
  * texture cache decoded from local memory (CLUT resolved, TEXA, mip chains via MIPTBP), invalidated by block stamps; a texture that lives in
    a render target at a page boundary with compatible format is sampled directly on the GPU (framebuffer-as-texture, incl. feedback with a snapshot copy);
  * uber pixel shader: STQ perspective-correct and UV, TFX MODULATE/DECAL/HIGHLIGHT/HIGHLIGHT2 + TCC, manual bilinear/point with REPEAT/CLAMP/
    REGION_CLAMP/REGION_REPEAT, LOD (L/K/LCM, MMAG/MMIN incl. trilinear), fog, alpha test (all ATST, AFAIL via second pass), DATE/DATM, PABE, FBA, COLCLAMP,
    FBMSK, 16-bit quantisation + DIMX dither;
  * blending: fixed-function dual-source mapping for the common (A-B)*C+D forms with C=As/FIX, otherwise a shader path with a destination copy
    (Ad, wrap, PABE, partial FBMSK, DATE);
  * display: PMODE EN1/EN2, MMOD/AMOD/ALP/SLBG, BGCOLOR, DX/DY/MAGH/MAGV/DW/DH sizing, DBX/DBY, bilinear present, letterbox.

## Feature status

Done and exercised by `gsr_test_synthetic` (T8+CLUT perspective strip, alpha test, UV sprites, 4 blend forms, fog, RT-as-texture, transfers):
everything in the list above except the items below. Swizzle functions are unit tested. CT16/CT24/T8/mip decode is verified against real game captures and the TXD textures on disc (texture audit below); T4/T8H/T4HL/T4HH/CT16S/CSM2/CSA are verified by synthetic decode tests only (the game does not use them).


## Sprite seam fix (menu "dark vertical line at x=341")

Root cause (found by replaying a 4000-frame boot->menu capture, `build/captures/menu_run.bin`): the main menu background is
three 256x256 CT24 textures drawn as three textured sprites at x = 0..170.625, 170..340.625 and 342..512 (y 0..256 and 256..512).
Pixel 341 (centre 341.5) lies in neither the second (right edge 340.625) nor the third (left edge 342) sprite, so it is never written and
keeps the stale (black) frame buffer content. This is what the real GS does as well (no sprite coverage rule closes a 1.375 px gap), and
the software GS (`PS2X_GS=sw`) shows the same line (checked by grabbing the sw window at the menu) - it is a game-side quirk, not a gsr
rasterisation bug. The plain rules gsr implements (pixel centres inside [x0,x1), XYOFFSET subtracted before the 1/16 subpixel conversion,
exactly adjacent sprites neither overlap nor leave a gap, ST/UV interpolated at pixel centres) are covered by `gsr_test_synthetic`.

Because the line is visible and undesirable, gsr closes such gaps on purpose (`Config::fixSpriteSeams`, default **on**, `GSR_SEAMFIX=0` in the
environment disables it): when a textured sprite directly follows another one with the same extent along one axis, both at least 32 px big,
and the gap along the other axis is in (0, 2] px, a filler sprite covering exactly the gap is drawn with the new sprite's border texel
row/column. The new sprite itself is unchanged, so the diff against the unfixed render is only the seam column (about 1.4 px wide, i.e. 1-4 px depending on
the scale; PSNR 35-38 dB over the whole frame, every other pixel identical). Verified at scale 1, 2, 3 (and 4 in the test).
Regression test: `seamTest()` in `tests/test_synthetic.cpp` (runs with the fix on and off: horizontal gap, vertical gap, exactly adjacent pairs).
Before/after: `build/gs/diff/menu_before_s1.png`, `menu_after_s1.png` (and `_s3`), `seam_before_after_zoom.png`, `menu_before_after_diff_x8.png`.

## Texture audit (tests/texaudit.cpp, tests/texaudit_pool.py)

`texaudit <capture.bin>` replays a DRKGSCAP capture through gsr with the audit hooks (`Config::onUpload / onClut / onTexture`) and checks every
texture gsr decodes from local memory, **every mip level and every cached reuse** (a draw that reuses a cached decode is compared with the
*current* upload, so a stale cache shows up as `STALE`), against two independent references:

1. the raw host->local IMAGE stream of the upload that wrote the texture (same base pointer, PSM and size) read as a raster; indexed textures go
   through the raw CLUT upload (CT32 16x16 raster, 8-bit: entry j = raster `swap34(j)`; CT16/CT24 follow the TEXA rules, written again in the tool);
2. ground truth from the disc: `texaudit_pool.py <outdir>` exports every texture of the ENGLISH `*.TXD` files (`pool.bin`); the pixel bytes of each
   upload are hashed and looked up there, and the TXD image (palette B,G,R,A, alpha 0..0x80, ARGB1555, RGB, RGBA) is compared (RGB exact, alpha
   only reported because the game edits palette alpha: 0x7F/0xFF -> 0x80 etc.).

It also classifies how the game uploads its palettes: **the game uploads the TXD palette with entries 8..15 and 16..23 of every 32 swapped
(`raw[swap34(j)] == txd[j]`, in a 3358 frame boot->movie capture 20785 palettes byte-identical to the swapped TXD palette plus 8749 with edited alpha and none in any other order)**, which is exactly the GS CSM1 order gsr implements,
so the "CLUT CSM1 entry order from memory" risk is closed: gsr's decoded textures are bit-identical to the TXD images on disc.

Results (reports in `build/gs/texaudit.txt`): capture 1 (1500 frames, memory-card screen -> menu -> Level 0 loading scene with the 3D model): 5174 mip levels
in 1989 decodes, 0 mismatches, 0 stale; capture 2 (3358 frames, logos, Surreal movie, title menu, intro book movie): 201683 levels in 43157 decodes
(149667 cached reuses), 0 mismatches, 0 stale, 34284 level-0 textures compared with the TXD (25170 exact, 9114 with edited alpha, none different).
Formats in use by the game: T8 + CT32 CLUT (CSM1, with/without mip), CT32, CT24 (+mip), CT16; the game never uses T4, T8H/T4HL/T4HH, CT16S, CSM2 or CSA != 0.
Those formats, TEXA TA0/TA1/AEM, CT16 CLUTs, CSA and MIPTBP1 levels are covered by `texFormatTests()` in `gsr_test_synthetic` (IMAGE upload -> decode ->
compare with a reference): 13 cases.

The audit found no decode error. The visible in-game breakage came from the **shader**, see "Fixed" below.

Debug aids (environment, all optional): `GSR_DRAWLOG=<first>-<last>` prints every draw of those vsync frames (state registers, flags, first vertices, shader
constants; `GSR_DRAWLOGV=<n>` vertices per draw), `GSR_DRAWSTOP=<n>` executes only the first n draws of the run, `GSR_DRAWSKIP=<a>-<b>` skips draws a..b
(bisecting a bad frame with `gsr_replay`), `GSR_TEXLOG=1` also logs invalidations and render-target-as-texture samples.

## Fixed in the texture / colour pipeline (visible bugs)

* **Fog made the blender wrap (white / inverted frames)**: `f = F/255` evaluated on the GPU can be 1.0000001, so `c*f + fogcol*(1-f)` became `floor(-1e-5) = -1`
  for black source texels; with COLCLAMP = 0 (the game never writes COLCLAMP, power-on value 0) the blend `(Cs-Cd)*As + Cd` then wrapped modulo 256 to 255. The Level 0 loading
  screen (3D model + a black fade quad blended with As = 0x7F) showed an inverted, blown-out picture (47 of 556 sampled frames were >25 % pure white, now 0), alternating
  with the correct black frame every vsync (double buffer). The shader now uses the GS formula `(F*Cs + (256-F)*FOGCOL) >> 8` with integer arithmetic and clamps.
  Regression test: `fadeTest()` in `gsr_test_synthetic`. Before/after: `build/screenshots/tex_before_loading_f*.png` / `tex_after_loading_f*.png`.
* **COLCLAMP = 0 blending**: the lerp forms `(Cs-Cd)*As+Cd` / `(Cd-Cs)*As+Cs` (the only ones the game uses: ALPHA = 0x44 / 0x0a) cannot leave 0..255 for As <= 0x80, so they use the
  fixed-function blender even when COLCLAMP = 0. Before, nearly every blended draw took the shader path whose pre-draw destination copy ignores overlapping primitives of the same draw
  (three overlapping fade quads, particles, hair) and was slow. Other forms with COLCLAMP = 0 still wrap exactly like the GS.
* **Render target sampled as a texture in a different format**: a CT32 target read as CT24 now gets alpha = TA0 (AEM honoured), read as CT16 TA0/TA1 by the stored alpha bit
  (`rtAlphaTest()`); the game only samples CT32 targets as CT32 so far.

* **Render target chosen for a texture**: targets are >= 512 rows tall and overlap; the target at the exact TBP (then one drawn in those rows, then the most recently used) is sampled.
  The first match in creation order sampled stale memory (Drakan blob shadows as huge dark polygons). Test: `rtOverlapTest`. Debug: `GSR_XFERLOG`, `GSR_RTDUMP`, `gsr_replay --upload/--fb`.

## Known gaps / risks

* Verified against real Drakan captures (see the audit above); the formats the game does not use are only covered by the synthetic tests.
* The game's draws never set COLCLAMP, TEXA, DTHE, DIMX, PRMODECONT or PABE (power-on values assumed: 0, TEXA = 0 -> CT24/CT16 texture alpha 0 when TCC = 1, as on the GS).
* With COLCLAMP = 0 and As > 0x80 (textures with palette alpha up to 0xBE exist) the shader path wraps like the GS; the fixed-function path used for the lerp forms clamps the factor to 1.0.
* Mip level selection uses `lod = log2(1/Q)*2^L + K` exactly as written in TEX1; the game's K values (-13 .. -19) keep almost every draw on level 0 (nothing was changed here, no reference to compare against).
* Alpha coefficients above 1.0 (As > 0x80) are clamped in fixed-function blends; AFAIL FB_ONLY/ZB_ONLY/RGB_ONLY use two passes (ordering within one draw is approximate).
* Z buffer is GPU-only (not written back to local memory, never sampled as a texture); depth is not reloaded when memory at ZBP is overwritten by uploads.
* Render targets of different formats aliasing the same memory are reconciled only through memory readback; partially overlapping/rotated aliasing is slow but handled via stamps.
* No AA1 line antialiasing, no SCANMSK, no interlace/field-offset emulation (FFMD frame is shown as stored), TRXPOS direction bits other than left-right/top-bottom are ignored,
  MTBA auto mip addresses and TEXFLUSH are no-ops, bilinear on RT-sampled textures uses the scaled RT (no CLAMP edge exactness).
* Menu comparison d3d11 vs sw: the sw GS has no screenshot hook (only the raylib window), so no pixel-exact diff against sw was made; instead the
  d3d11 menu at scale 3 (box-filtered) was compared with scale 1 (PSNR 33.4 dB, differences are the bilinear texture filtering at higher
  resolution, see `build/gs/diff/menu_s1_vs_s3_diff_x4.png`). No other discrepancy was visible in the menu frame (text, panel, dragon, loading bar).
* The Level 0 loading scene (3D model, textures, fade) is in the capture `build/captures/gscap_20261016_194452.bin` and renders correctly (`build/screenshots/tex_after_loading_scene_before_fade.png`);
  the free-roaming 3D world is not in any capture yet (the game currently issues no world draws after the intro skip, only the HUD), so world geometry/texture filtering was not compared.
* Pixel centre convention follows PCSX2 (no half-pixel shift); thin-triangle fill rules differ slightly from the GS.
* Alpha written to 16-bit targets is 0/0x80; CT24 targets keep alpha at 0x80.

## Runtime integration (branch `drakan-gs` of ps2recomp, worktree `ps2recomp-gs`)

The branch (based on `drakan` @ 0dfbc85) lets the runtime render with gsr instead of the software GS. Select with the environment
variable `PS2X_GS=d3d11` (alias `gsr`); unset or `sw` keeps the old software GS + raylib texture path unchanged, for comparison.

What changes (all GS-facing, no guest-visible API change):
* `ps2xRuntime/src/lib/gs/gs_gsr_bridge.cpp`, `include/runtime/gs/gs_gsr_bridge.h` (new): `GsrBridge` owns the `gsr::Renderer` and the window.
  Window: a child `HWND` (class `PS2XGsrChild`, mouse-transparent, no focus) is created inside the raylib window's client area and
  receives the flip-model swapchain. raylib keeps owning the real top-level window, so keyboard/pad input, audio, resizing, the 60 fps
  loop and the close button are unchanged. The child follows the client size each host frame (`hostTick()` -> `Renderer::resizeWindow`),
  the image is letterboxed (aspect `PS2X_ASPECT`, default 4:3, e.g. `PS2X_ASPECT=16:9`; `PS2X_GS_STRETCH=1` fills the window),
  Alt+Enter toggles raylib fullscreen (the child follows).
* `GS` (`gs_frontend.{h,cpp}`): new `enableD3D11(hwnd)`, `shutdownD3D11()`, `d3d11Active()`, `vsyncHost(tick)`, `hostTick()`,
  `processGIFPacketPath(path, ...)`. When the bridge is active `processGIFPacket`, `writeRegister`, `uploadImageNative`,
  `consumeLocalToHostBytes`, `ReadVram/WriteVram` forward to gsr (the software backend sits idle); `processNativePackedGIFPacket` returns false and
  `PS2Memory::tryProcessNativeGif{Image,Packed}Chain` bail out, so every packet goes through the GIF arbiter (and the capture) with its path.
  `clearFramebufferContext/clearActiveFramebuffer` (HLE sceGsSwapDBuff shortcut) are no-ops because the clear packet is drawn as a normal sprite.
* `GifArbiter::setProcessPathPacketFn`: arbiter output now carries the path (1/2/3) so gsr keeps per-path GIF tag state.
* EE-visible GS behaviour, as with the software GS: `GSRegisters` (in `PS2Memory`) stays the register file the EE reads/writes. GS FINISH ->
  `CSR |= 2`, SIGNAL -> `CSR |= 1` + SIGLBLID (copied back/forth around each submit, LABEL included), FIELD/vsync tick/INTC VBLANK still come from
  `EeScheduler` (`processEvent`, which now also calls `GS::vsyncHost`). No GS-side interrupt was generated by the software GS either (CSR polling only).
  `vsyncHost` pushes PMODE/SMODE*/SYNC*/DISPFB1-2/DISPLAY1-2/EXT*/BGCOLOR/IMR/BUSDIR into gsr and runs `gsr::vsync(false)`; the display is presented
  from the host loop. GS->EE (BUSDIR / TRXDIR=1) reads go through `consumeLocalToHostBytes`.
* `CMakeLists.txt` (root): `runtime/gs` is added by the root project (tests only with `DRAKAN_BUILD_RUNTIME_TESTS`); `ps2xRuntime/CMakeLists.txt` adds `gs_gsr_bridge.cpp` to `ps2_runtime` and links `drakan::gs` (alias of `gsr`).
* `ps2_runtime.cpp`: backend selection after `InitWindow`, host loop branch (`hostTick`, Alt+Enter), `shutdownD3D11` before `CloseWindow`.
* Debug env: `PS2X_GS_SCALE=1..8` (internal resolution), `PS2X_GS_STATS=<n frames>`, `PS2X_GS_SHOT=<png>` + `PS2X_GS_SHOT_AT=<frame>` (default 1200)
  write the merged display of that vsync frame, `PS2X_GS_LOG=1` logs unsupported-feature notes.

Expected merge-conflict points on `drakan`: `ps2_runtime.cpp` (initialize() after `SetTargetFPS`, run() loop head and tail, arbiter hookup in
`syncCoreSubsystems`), `EeScheduler.cpp` (one line in `VBlankStart`), `ps2_memory.cpp` (two one-line guards), `gs_frontend.{h,cpp}`, `ps2_gif_arbiter.{h,cpp}`,
and `CMakeLists.txt` next to the HLE `add_subdirectory`. Build dir used: `decomp/build/recomp_gs/` (own copy of `gen/`, so the exe lands in `recomp_gs/bin`).

Capture check (task 1): replaying the four existing captures gives an all-black display, which is correct: every GIF packet in them is the
per-frame full-screen sprite clear with RGBAQ = 0 (255,492 vertices, all colour 0, no TME, no IMAGE/BITBLT traffic), so there is nothing but black to show.
No format mismatch with docs/reference/gs-capture.md was found (PRIV_REGS order, register offsets, field/frame records all match). Display: 512x512 (DW=2560/MAGH 5, DH=512), alternating DISPFB 0x1000/0x1080.

### Native window / present layer (branch `drakan-win`)

The raylib / GLFW OpenGL window is gone from the D3D11 path (raylib remains linked for audio output and for `renderer = sw`, the software reference
GS, which still uses its own window). The earlier layout (a mouse-transparent D3D11 child HWND inside a raylib GL top-level window) is replaced by:
* `ps2xRuntime/src/lib/drakan_window.{h,cpp}`: a native Win32 top-level window (class `DrakanWindow`, `WS_OVERLAPPEDWINDOW`, icon resource 1, black
  class brush, centred, DPI aware via `drakan_cfg::enableDpiAwareness`). It owns the message pump (`pump()`, once per host frame), the close request
  (`WM_CLOSE` = X button / Alt+F4 / taskbar, `requestClose()`), the cursor shape, the modal move/size timer (keeps presenting while dragging) and
  `timeBeginPeriod(1)`. `GsrBridge` gets this HWND and `gsr::Config::hwnd` is the top-level window itself: **the flip-model swapchain
  (`DXGI_SWAP_EFFECT_FLIP_DISCARD`, 3 buffers, waitable frame-latency object) is on the top-level HWND**, which is what GeForce / NVIDIA App, AMD, Steam,
  RTSS and capture hooks expect (verified: Alt+Z opens the NVIDIA overlay over the game, `nvspcap64.dll` is loaded).
* Fullscreen = borderless window on the monitor (style swap + `SetWindowPos`; DWM promotes it to independent flip, i.e. the same latency as exclusive).
  `DXGI_MWA_NO_ALT_ENTER` stays set; Alt+Enter / F11 are polled in `GsrBridge::hostTick`. There is no exclusive `SetFullscreenState` mode
  (window_mode = exclusive is treated as borderless).
* Tearing: when `DXGI_FEATURE_PRESENT_ALLOW_TEARING` is supported the swapchain is created with `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` (also kept in
  `ResizeBuffers`) and `Present(0, DXGI_PRESENT_ALLOW_TEARING)` is used while vsync is off (VRR / G-SYNC friendly, with `frame_limit`).
* FPS counter (`drakan_fpsmeter.{h,cpp}`): `onPresent()` per present, statistics twice a second; guest fps = `ps2perf::g_flips` (DISPFB changes) per second,
  presented fps / frame time / worst frame / 1% low from a ring of present intervals, CPU load from `GetProcessTimes` / `GetThreadTimes` of the process, the EE
  thread and the renderer worker. Drawn by the settings overlay's stats layer (`SettingsOverlay::setStatsLayer`, `drawStatsPanel`), F2 cycles the mode,
  drakan.ini `[video] show_fps = 0|1|2`, `fps_position`, F1 > Display.
* Clean shutdown (window close, Alt+F4, F1 > Quit to Desktop, pause menu Quit confirmed): host loop ends -> SPU2 muted (10 ms ramp) and stopped -> EE thread
  stopped (3 s guard) -> renderer / overlay destroyed -> window destroyed -> `drakan.log` flushed (`drakan_log::flush`, marker through the log pipe) ->
  exit code 0. Memory card files are flushed after every write already.
* d3d11 changes outside the present path: only `createSwapchain` / `resize` / the final `Present` call and a `swapFlags()` helper (tearing).
* Not ported to Win32 (inert with the native window): the stock `scePad` stubs (`Kernel/Stubs/Pad.cpp`, `ps2_pad.cpp`, raylib keyboard / GLFW gamepad) used when
  `DRAKAN_INPUT=0`; they only work with `renderer = sw`. The default input path (Raw Input + XInput in `runtime/input`) does not use raylib.

## Texture quality: blur investigation, texture upscaling, HD texture packs

### Where the blur comes from (measured on `build/captures/gscap_20261016_194452.bin`, scales 1/3/5)

| Source | Finding | Contribution |
|---|---|---|
| (a) mip LOD | gsr uses the **GS formula** (LOD = -log2(Q)*2^L + K, LCM/K/L/MXL of TEX1), not GPU derivatives. The GS computes it for 1x pixels, so at internal scale s every mip level is log2(s) levels too blurry. `GSR_LODSTAT=1` (printed at exit) over 149k mip-mapped triangles: mean GS LOD 0.85, mean log2(major axis texels/pixel) 1.34, minor 0.01 (at scale 1). At scale 3 the whole distribution shifts by -1.58. | Largest source for minified, mip-mapped surfaces (models, world) at scale >= 2. Fix: `mip_bias = auto` (default) = -log2(scale). |
| (b) filter mode | TEX1 MMAG/MMIN are honoured per draw (`filter=game`); the game mostly uses bilinear/trilinear. `filter=bilinear` only changes draws that were point sampled (UI/fonts), `point` makes things blockier. | Not a blur source by itself; magnified texels are blurry only because the source is small. |
| (c) anisotropy | GS LOD is isotropic: 8% of mip-mapped area has footprint ratio < 1.25, 37% < 2, 32% < 4, 19% < 8, 4% >= 8 (mean major/minor = 2.5x). The GS LOD lies between the minor and major axis (major - GS LOD: 32% of the area is blurrier than the major axis needs by >0.5 level, 38% sharper by >0.5). `anisotropic = N` replaces the GS LOD by the footprint LOD log2(major/taps) with up to N taps along the major axis (minified, mip-mapped, Q-driven draws only; FST/LCM draws keep the GS LOD). | Moderate on oblique ground/walls. Off by default (costs up to 16x samples). No ground-plane scene is in the captures (the world renders black in the reachable state), so this was verified on the HUD models and by compiling/running only; judge visually. |
| (d) intermediate resampling | None. Render targets are created at the full internal resolution, framebuffer-as-texture samples the scaled RT directly (bilinear in the RT's own resolution), and the final blit is area-average / sharp-bilinear (`sharp_present`). The only low-res copy is the 1x display latch for readbacks. | none |
| (e) source resolution | The textures dumped from the capture are 256x256 (22), 128x128 (20), 64x64 (4), 32x32 (2). Magnified on a 1080p/4K screen each texel covers 3-10 pixels. | Dominant for everything that is magnified (UI, HUD, models close to the camera): only upscaling / HD packs help. |

Side-by-side crops: `build/screenshots/texup_s3_off_hud.png` vs `texup_s3_auto_hud.png` (and `_s5_`), `texup_crop_book_text_s5.png`, `texup_crop_model_s5.png`, `texup_algos_native_xbrz_smooth.png` (native / xBRZ / smooth on a terrain texture), `texup_mip_aniso_s3.png`.
The menu capture requested could not be recorded: a fresh `PS2X_GS_CAPTURE` run stopped at the memory-card warning (needs the X press), so the HUD/quest-log scene of the existing capture was used.

### Texture upscaling (`gs_texup.{h,cpp}` core + `d3d11_backend.cpp`)

* The decoded texture (after CLUT/TEXA, so the key includes the CLUT contents) is hashed (64 bit, level 0 + size). A worker pool (1-4 threads) upscales it; until the result is ready the draw uses the native texture, then the cache entry switches to the upscaled one (no hitch). Results are shared by content hash across all TBPs and survive re-decodes. `GSR_TEXUP_SYNC=1` (set by `gsr_replay`) waits for the jobs so screenshots are final.
* Factor: `auto` (default) = scale 1 -> off, 2-3 -> 4x, 4+ -> 8x, UI sprites always 8x; `2|4|8` forced (capped to 4096 px per side and by the texture budget, LRU eviction above `textureBudgetMB`, default 1536).
* Algorithms: **xBRZ** (2x/4x, 8x = 4x then 2x; reimplementation of Zenju's algorithm, GPL-3.0-or-later, alpha-aware colour distance) and **smooth** (Catmull-Rom bicubic on alpha-premultiplied colour + 0.45 unsharp mask). `auto` picks xBRZ when the texture has <= 24 colours or >= 50 % identical neighbours, else smooth. Sprites drawn at ~1:1 texel/pixel (HUD, fonts, menu art) are forced to xBRZ. Alpha stays in GS units 0..0x80, partially transparent texels never pull in the colour of fully transparent ones (alpha-tested foliage keeps its edges, no dark fringe), REPEAT textures wrap in the kernels instead of clamping.
* Mips are regenerated from the upscaled base (alpha-weighted box filter). UV normalisation is unchanged: the shader samples in logical texels * N and applies REPEAT/CLAMP/REGION_CLAMP/REGION_REPEAT in logical texels, so texel-exact sprites hit the same cells.
* Exclusion: put hashes (one per line, `#` comments) in `<texture_dir>/exclude.txt`.
* Cost (this capture, 48 unique textures, RTX 5080 host CPU): scale 3 / 2x: 480 ms total (avg 10 ms, xBRZ 1-4 ms, smooth 7-23 ms per 128/256 px texture), 33 MB; scale 5 / 4x: 1.7 s total (xBRZ 2-7 ms, smooth 28-89 ms), 131 MB. All on worker threads. A disk cache was not needed (re-upscale is cheaper than reading 4-16 MB per texture); the in-memory cache is hash keyed.

### HD texture pack / dump (PCSX2 style)

1. `texture_dump = 1` (or `GSR_TEXDUMP=1`): every unique decoded texture is written once to `textures/dump/<hash>.png` (RGBA, alpha 0x80 -> 255; names are the 16-hex-digit content hash incl. CLUT). Run the game / replay through the levels you want.
2. Run an offline upscaler over the dump (e.g. `realesrgan-ncnn-vulkan -i textures/dump -o textures -n realesrgan-x4plus -s 4 -f png`; keep the file names).
3. Start the game with `texture_replace = 1` (default): `textures/<hash>.png` or `.dds` (uncompressed 24/32 bit, BC1/BC2/BC3) replaces the texture. Any integer multiple of the original size works (2x/4x/8x; other sizes are resampled to the nearest power-of-two factor). Mips are generated. Non-interlaced PNG only (8/16 bit, all colour types).

### drakan.ini [video] keys (gsr::Config fields / environment overrides; the lead must wire the ini, the environment variables work today)

| ini key | Config field | env | values |
|---|---|---|---|
| `texture_upscale` | `textureUpscale` (0 auto, 1 off, 2/4/8) | `GSR_TEXUP` | auto (default), 1, 2, 4, 8 |
| `texture_upscale_filter` | `textureUpscaleFilter` (0 auto, 1 xbrz, 2 smooth) | `GSR_TEXUP_FILTER` | auto (default), xbrz, smooth |
| `anisotropic` | `anisotropic` | `GSR_ANISO` | 16 (default), 0/1 = off, 2, 4, 8 |
| `mip_bias` | `mipBiasAuto` / `mipBias` (a number sets `mipBiasAuto=false`) | `GSR_MIPBIAS` | auto (default) = -log2(scale), or a float (0 = original GS LOD) |
| `texture_dump` | `textureDump` | `GSR_TEXDUMP` | 0 (default) / 1 |
| `texture_replace` | `textureReplace` | `GSR_TEXREPLACE` | 1 (default) / 0 |
| `texture_dir` | `textureDir` | `GSR_TEXDIR` | folder, default `textures` (relative to the working directory) |
| `texture_budget_mb` | `textureBudgetMB` | `GSR_TEXBUDGET` | 0 = auto (default, 40 % of VRAM, 1.5-8 GB) or MB |
| - | `textureUpscaleSync` | `GSR_TEXUP_SYNC` | 1 = wait for jobs (tests/replay) |

Debug: `GSR_TEXUP_LOG=1` (per job timing), `GSR_LODSTAT=1` (LOD statistics at exit). `gsr_replay` takes `--texup --filter --aniso --mipbias --texdir --dump`.
Tools/tests: `gsr_test_texup` (16 core checks; `--file in.png --factor 4 --algo xbrz|smooth --out o.png` upscales one image and prints the time), `gsr_test_swizzle` and `gsr_test_synthetic` still pass (41 checks at scale 1/3/4).


## HUD clusters, hud_scale, widescreen anchoring (round 4)

* `Renderer::setHudScaleX(k)` (aspect squeeze, (4/3)/aspect) and the new `Renderer::setHudSize(s)` (`[video] hud_scale`, 0.5 .. 1.5, 1 = original) act on UI draws: sprites (per sprite, 6 vertices of a batch)
  and triangle quads drawn with ZTE=1 / ZTST=ALWAYS / ZMSK=1, colour write on, not sampling a render target, inside a display sized frame, narrower than 85 % of it.
* Sprites are grouped into **clusters** (boxes that overlap or touch within 3 px, built from the UI sprites of the previous frame that had any; the game draws its UI only every 2nd - 4th presented frame, so the
  clusters survive empty frames for 60 frames). Every sprite of a cluster uses the same map `x' = ax + (x - ax) * k * size`, `y' = ay + (y - ay) * size` with the anchor at the nearest screen edge / corner of the cluster
  (middle 40..60 % of an axis = centre). An orb's fill, ring and highlights therefore move as one piece and edge anchored elements stay on screen. Clusters covering more than 7 % of the frame (menus, popups, pages)
  are anchored at the centre and keep their size (aspect squeeze only; the menu mouse maps through that). Full-width bands under 40 % of the frame height (subtitle bar) are resized vertically only. Screen-grab tiles
  (128 x 128 aligned) are left alone. Regression test `hudClusterTest` in `gsr_test_synthetic` (47 checks).
* Debug: `GSR_DRAWLOGTBP=<tbp>` (log draws sampling a texture base), `GSR_RTLIST=1` (render target list per logged draw), `GSR_RTDUMPF=<a>-<b>` (every render target at the end of those frames),
  `GSR_RTDUMPD=<a>-<b>` (target after every draw of those frames), `GSR_XFERLOG` also logs local->host requests (`[l2h ...]`), draw log flags garbage vertices.

HD pack (Real-ESRGAN, `alias.txt` RGB-hash fallback, game alpha for aliased textures): see `docs/reference/hd-textures.md` section 8.
