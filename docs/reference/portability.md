# Linux and browser portability

The single ISO pipeline is `scripts/buildtool/Build-LocalGame.ps1`. Linux uses the
`Build-Drakan.sh` bootstrap; Windows can request `-TargetPlatform web` through WSL.
See [BUILDING.md](../../BUILDING.md#linux-and-browser-iso-builds) for commands.

## Current implementations

| Service | Windows x64 | Linux x64 | Browser / wasm32 |
| --- | --- | --- | --- |
| Build | MSVC, `game` preset | Clang, `linux-game` preset | Pinned Emscripten, `web-game` preset |
| Window / presentation | Win32 window (`drakan_window.cpp`) | GLFW window without a client API (`drakan_window_glfw.cpp`, raylib's GLFW, X11) | The page's canvas (`drakan_window_web.cpp`), owned by the renderer's GPU pthread as an OffscreenCanvas |
| GS rendering | Shared WebGPU renderer (default; Vulkan, D3D12 fallback; portable temporal AA, compute ray tracing) or D3D11 with the optional NVIDIA features and DXR (PC Settings > Display > Renderer) | Shared WebGPU renderer (wgpu-native: Vulkan, GL fallback); software GS with `renderer = sw` | Shared WebGPU renderer (the browser's WebGPU, through the threaded `rhi` proxy); WebGPU required |
| Audio | WASAPI | ALSA; PulseAudio plugin fallback when provided by WSLg | Shared PCM ring and AudioWorklet |
| Controls | Input module (`runtime/input`): Raw Input / XInput host | The same input module: GLFW host (`drakan_input_glfw.cpp`: keys, raw mouse motion, GLFW gamepads) | The same input module: DOM host (`drakan_input_web.cpp`: keys, pointer lock, Gamepad API) |
| ISO | Random reads from disk | Random reads from disk | Bounded range requests to the local launcher |
| Cards | Existing native card paths / launch overrides | `mc0` beside the deployed executable | `/persist/mc0` in IndexedDB |
| Configuration location | Executable directory | `/proc/self/exe`, with launch-directory fallback | Virtual filesystem; `/persist/drakan.ini` |
| Modern title / pause / save / PC Settings | `SettingsOverlay` (Win32 platform; D3D11 or WebGPU renderer) | The same `SettingsOverlay` (GLFW platform, WebGPU renderer) | The same `SettingsOverlay` (event-fed platform, WebGPU renderer) |

Since phase 4 of [webgpu-renderer.md](../proposals/webgpu-renderer.md) (2026-10-29) native Linux runs the Windows
code path: the GS bridge, the WebGPU renderer, the input module and the settings overlay, with only the window / input
host replaced ([webgpu-linux.md](../journal/2026-10-29/webgpu-linux.md)). The notes below about the software GS and the
raylib UI on Linux describe the previous path. Since phase 5 the browser runs it too
([webgpu-browser.md](../journal/2026-10-29/webgpu-browser.md)).

Linux has reached gameplay, accepted keyboard movement and pause input, written a native save
with thumbnail into the deployed card folder, restarted, and loaded that save back into gameplay.
The full browser game has rendered the opening movie and 3D scenes, accepted movement and pause,
emitted a nonzero audio signal, and created a Surdana save with thumbnail. Stop reports a completed
IndexedDB flush. A page restart restored the card and loaded the game-created save into gameplay.
The final browser build also accepted a 20 ms confirm/pause tap and retained the camera view after
mouse movement stopped. Remaining limits are recorded in the dated journal.

The accepted input remains a 2048-byte-sector ISO9660 PAL **SCES_500.06**, with the exact ELF SHA-256
in `scripts/buildtool/toolchain.lock.json`. The builder extracts the ELF, derives the VU image and
runs the translator automatically. The ISO is the only game data the user supplies; no BIOS or
separate resource extraction is required. HD textures, HD movies and vendor SDKs remain optional.
Game-derived outputs stay under ignored build/deployment folders and must never be committed.

## Required enhancement parity

The user explicitly requires HD textures and graphics enhancements on Linux and
browser ports wherever the platform and hardware can support them. Base-game
software rendering is a fallback and is not completion of that requirement.
HD texture support must preserve the existing content hashes and replacement
packs; native/browser differences must not require separate art packs. The
ISO-only base-game path remains available without optional external assets.
Ray tracing requires an appropriate renderer and device capability check.
Vendor-specific features need compatible implementations or clearly identified
fallbacks; unsupported features must not appear enabled in the game settings.
Configuration belongs in the game's settings, not a browser toolbar.

The shared `drakan::texture` target builds texture hashing, PNG/DDS decoding,
upscaling, mip generation and a bounded asynchronous replacement cache independently
of D3D11. The software GS now samples replacements on proven immutable texture
paths, including original palette/TEXA decoding, RGB-alpha aliases, nearest/linear
filtering and wrapping. Feedback/stale-cache cases retain the original path;
pack lookup does not mutate guest texture-cache observations. Worker jobs retain
original pixels while replacements load, with a 128 MiB decoded-image budget, 64 MiB validated source cache and
16 MiB queued-source budget. Integer power-of-two replacements are accepted;
other sizes fall back to the original texture in this initial software path.

The canonical builder reuses the same optional pack from `local/textures`, the
existing Windows play folder or explicit `-TextureDirectory`. Native deployment
stages it incrementally; the local browser launcher can stream the existing pack
directly without duplicating it. Assets stay ignored.
The browser indexes names/alias/exclusion metadata at startup and loads requested
images from its local launcher on a texture worker; it does not preload the pack.
Private environment controls are `DRAKAN_HD_TEXTURES=0`, `DRAKAN_TEXTURE_DIR` and
`DRAKAN_HD_TEXTURE_LOG=1`. No browser controls or toolbar were added.

Cross-platform pixel tests exercise actual HD subtexels, source-slot recycling,
retained-palette changes, aliases and exclusions. Real-disc browser sessions have
loaded replacement textures in menus and Surdana. One native Linux six-worker
Surdana window passed the pacing gate; recent browser HD runs failed it under
concurrent host load. This is not full-game visual or performance validation. In-game settings parity, portable GPU rendering,
ray tracing and other vendor-specific effects remain unfinished. The Windows
DXR path has not been ported; enhancement parity is not yet claimed.

## Portable GPU work in progress

A private `DRAKAN_GPU_RASTER=1` path (browser `?gpu=1`) runs ordered tile
rasterization using the same WGSL compute program on Linux wgpu-native and browser
WebGPU. The pinned native runtime is downloaded automatically during the canonical
build. Windows retains its existing D3D11 renderer. The browser retains Windows-style keyboard/mouse prompt generation and bindings.
A separate browser GPU worker
keeps device promises and readback waits off the UI thread. The UI owns worker
creation so its blocked Wasm parent does not prevent startup. A process-lifetime
shared request queue then delivers jobs and completed pixels without per-job UI
callbacks or an intermediate full-image copy. Ownership states arbitrate input
copying, asynchronous device work, output copying and cancellation. The GPU worker
reuses owned input buffers and refreshes shared memory views after growth. This
private GPU mode requires `Atomics.waitAsync`; otherwise it falls back to CPU.
Decoded textures retain immutable cache leases.
Immutable texture texels use a bounded 128 MiB GPU atlas with sparse uploads;
weak ownership checks avoid pinning the separate CPU HD cache. Recycling, atlas
eviction and device-loss replay have procedural complete-VRAM coverage.
The live Surdana GPU prototype remains below target performance (the latest
recorded window is about 19.58 fps, with game time tracking wall time); it is not
enabled by default. The default CPU path passed one 32.57 fps Surdana window,
which does not establish full-game or GPU performance parity.

RGBA32/RGB24 destinations, 32/24-bit depth and immutable decoded/HD textures are
covered by the initial path. Unsupported/feedback/packed operations fall back to
the existing CPU renderer. Private `DRAKAN_GPU_SCALE=3|5` (browser
`?gpu=1&gpuScale=3`) adds real fractional scene samples, retained presentation
surfaces and high-resolution dual-CRT composition. Native centre samples alone
update guest VRAM. CPU writes invalidate presentation detail; unsupported paths
fall back to native samples. GPU batching also works with one CPU raster worker.
GS submission scopes nearest rounding and restores the EE's incoming control.
Each dispatch still uploads and reads back its pixel rectangle; GPU framebuffer
residency is unfinished. A 3x Surdana browser window measured about 7.51 fps under
concurrent host load and fails the pacing gate. Neither high-resolution nor the
native-resolution GPU mode is enabled by default.
This is experimental and does not establish 1:1 performance or portable RT parity.
The procedural pixel suite and live validation are recorded in
[portable-gpu-renderer.md](../journal/2026-10-26/portable-gpu-renderer.md).
High-resolution checks and unsuccessful performance experiments are recorded in
[portable-high-resolution-rendering.md](../journal/2026-10-27/portable-high-resolution-rendering.md).

## Build boundaries

`runtime-build-linux` / `bin-linux` and `runtime-build-web` / `bin-web` are platform siblings inside
the existing WorkDir. Deployment produces `dist/DrakanPC-linux` and `dist/DrakanPC-web`, leaving the
Windows deployment separate. The canonical builder performs deployment and preserves native cards
and user INIs. Launch from the deployed folder, including for tests.

The Debian/Ubuntu bootstrap obtains missing public build packages, checksum-pinned Python and
PowerShell, and the pinned Emscripten SDK / FFmpeg sources for a browser build. Public tool versions
and archive hashes are in `scripts/buildtool/web-toolchain.lock.json`. Initial builds require network
access and substantial compilation; subsequent builds reuse the same private sources and tool caches.
A browser build on Windows currently requires an installed WSL Linux distribution.

Generated units are an object library on non-Windows targets. This makes all registration and native
function objects available before LTO performs internalization; the Windows static-library path stays
intact. Non-MSVC builds disable strict aliasing for the generated register overlays and use explicit
VU constant-evaluation / expression-depth budgets. Runtime-only tests do not probe whole-game IPO.

## Browser execution

<!-- media:diagram-browser-runtime -->

The browser UI thread handles DOM and input events (fed to the input module and the settings overlay through rings),
filesystem proxy requests and the audio worklet connection. `PROXY_TO_PTHREAD` runs the application in a worker. The
guest scheduler, resource service, sound pump and audio mixer use pthread workers. Blocking ISO reads and guest
condition-variable waits therefore do not block the browser UI. There is no unisolated or single-thread fallback:
shared memory and COOP/COEP isolation are required.

The GS renderer's WebGPU device lives on its own GPU pthread, which owns the canvas as an OffscreenCanvas; the renderer
records its calls into ordered batches for that thread (the `rhi` proxy, `rhi_proxy.cpp`) and readbacks complete from
its event loop. Frames are drawn into a back buffer copied to the canvas at present. See
[webgpu-browser.md](../journal/2026-10-29/webgpu-browser.md).

Use native Wasm exceptions (`-fwasm-exceptions`). Legacy JS exception trampolines cannot transport
`v128` arguments/results used by the memory bus. `wasm_vector_exception_tests.cpp` checks both valid
vector calls and an exception across them. Browser requirements include WebGPU, native Wasm exceptions,
SIMD, shared memory, workers and AudioWorklet.

The launch server binds only loopback, validates Host/Origin, supplies isolation headers, and serves
at most 1 MiB per ISO range request. It never uploads or preloads the full ISO. The browser source
keeps a bounded 16 MiB block cache per worker. The generated executable and extracted boot ELF are
private local build products; only the boot ELF is preloaded into the virtual filesystem.

Cards and the runtime INI are mounted at `/persist` using IDBFS. Periodic, manual and exit flushes
share a serialized queue. Keep the same browser profile and origin when returning to saves; changing
the launch port or clearing browser site data changes/removes that storage.

## Numerical and input boundaries

Wasm has fixed nearest floating-point rounding. VU arithmetic uses explicit toward-zero helpers,
including residual handling for tiny opposing addends, overflow saturation and denormal flushing.
Native x86 retains scoped MXCSR rounding. The production SIMD / rounding headers run in both native
and Wasm tests; the rounding digest also checks against hardware x86 results.

The window thread samples portable input and publishes complete pad states under a mutex. Guest
reads retain short button presses and mouse movement instead of calling GLFW from the guest executor.
Portable PadMgr_Update hooks latch one sample per engine input frame, so repeated padman reads
cannot erase an edge or relative movement. Other controller ports return a neutral sample.
The browser publishes DOM events into shared atomics independently of raylib frame polling. Held
buttons are suppressed across GUI transitions until released so a confirm cannot answer two dialogs.
The portable camera adapter now reuses the Windows input module's direct mouse
camera, idle-rate clearing, pitch persistence and follow handling. On-foot mouse
counts no longer also deflect the emulated right stick. Flight retains native
stick steering; physical controller activity transfers ownership until keyboard
or mouse activity resumes. A radial controller deadzone rejects idle drift.
Linux captures the cursor on gameplay entry and releases it for menus/focus loss;
F10 release and click recapture are retained, with the recapture click suppressed.
Configured stick-look mode retains right-stick projection. Focus loss clears held
input and pending motion. The DOM bridge retains fractional
pointer counts and requests unadjusted movement when supported. Keyboard / mouse
controls merge with a connected pad. Live validation of the new adapter is recorded
in the camera review journal; unit coverage is not a full camera-mode playthrough.
Linux and browser now share Windows' modern title, pause, save/load and PC Settings
models, game art and fonts through a portable renderer. Native transactions still
own loading, saving and game flow. Modern inventory/map overlays remain unfinished.
Both settings hosts derive from `CommonSettingsHost` (`drakan_settings_common.h`), and both
presentation backends draw the same HUD layer set (`settings/Hud.h`): the FPS counter (F2 / PC
Settings), narration subtitles, the modern bow-aim crosshair and the native-menu cursor. Linux and
browser therefore share Windows' quicksave / quickload (F5 / F8), modern bow aim, subtitles and FPS
counter; Linux also offers ALSA output-device selection. The browser copies the deployed
`subtitles/*.srt` into its virtual filesystem before start-up.
The portable PC Settings host uses the same configuration paths as the runtime,
including the browser's persistent mount. It applies audio, bindings, direct-camera
settings, FOV and sharp presentation; unsupported renderer controls are disabled.
Browser persistence serializes flushes and retries bounded transient ENOENT races
from atomic settings-file replacement. Persistent storage failures remain visible.
The browser displays only the game after its initial audio-unlock gesture. It does not add a
control legend, toolbar, help panel or separate save interface. Menu state is published from the
guest executor to manage pointer capture without reading live guest RAM from browser callbacks.
Mouse capture releases for in-game menus and can resume on a user-confirmed return to gameplay.
File-close persistence feeds the same IndexedDB queue as periodic recovery and exit, and a failed
initial restore cannot flush an empty filesystem over existing saves.

## Software rendering

Linux and browser use the CPU GS backend behind a bounded, ordered rendering queue. The dedicated
renderer overlaps command production with compatible draw batches, distributed as rectangles across
up to fifteen persistent workers plus the rendering thread, bounded by hardware concurrency. Every
rectangle retains primitive order. Uploads, transfers, presentation, FINISH and public VRAM access
retain completion fences. Independent CSM1 palette reloads retain immutable per-draw snapshots;
overlapping palette reads and CSM2 reloads synchronize. Texture-cache invalidations remain ordered
within a batch. Texture feedback, wrapped buffers, packed destination writes, overlapping color/depth
and retained stale texture pages use the serial path.

GS submissions scope nearest rounding independently of EE arithmetic; native workers
inherit that scoped mode and Wasm uses fixed nearest rounding.
SIMD coverage/interpolation, conservative row bounds, bilinear rounding, early depth rejection and
common fragment specializations preserve the scalar renderer's pixel decisions. Immutable texture
scopes avoid intermediate cache-page copies while retaining the final sampled page. Palette, PSMT8
upload and framebuffer readback paths reduce repeated decoding/address calculations. Identical CRT
sources require one conversion, since their blend cannot alter RGB and output alpha is normalized.
Software presentation follows completed original graphics-worker flips, with a stale-clock fallback
for paths such as movies. Windows retains its existing default GPU backend selection.
Linux/browser capture the software image at that flip before subsequent guest draws;
the image and its revision stay coupled until the window copies them. GL uploads
run after releasing the image lock.

Portable presentation uses every row of the completed framebuffer, regardless of
PAL field parity. This avoids the old bob path's alternating one-line displacement
and loss of half the vertical detail. Guest SMODE2 and VRAM are unchanged;
`DRAKAN_SOFTWARE_FIELDS=1` restores the legacy host field mode for comparison.
The host also matches Windows' default anti-flicker bypass: two enabled reads of
the same framebuffer with matching horizontal origin/width, nearby vertical
origins and constant-alpha PMODE composition use the first circuit directly.
The live PC Anti-flicker setting can restore the original blend. Unrelated CRT
composition keeps its original behavior. This prevents a native-line vertical
blur and avoids an unnecessary full-frame blend; guest registers remain intact.
Progressive scanout alone preserves source detail; the separate private GPU
scale described above adds fractional geometry samples.

The portable camera reuses the shared input module for direct mouse look. Focus
loss clears physical input and pointer capture while preserving idle direct-view
ownership, avoiding native stick centering without mouse movement. Controller and
scripted camera ownership still hand control back to the game. Linux retains mouse
press edges before raylib's final button-level sampling, so complete fast recapture
taps are not lost. Browser fractional pointer motion is accumulated before the
integer bridge. GLFW clock reads/setters use a shared native steady-clock origin
and atomic offset without a synchronous browser-thread proxy; window/GL operations
still use the browser thread.

Production-backend tests compare all 4 MiB of VRAM against serial rendering for texture formats,
filters, fog, depth/alpha tests, blending, framebuffer masks, feedback, stale pages, transfer/read
barriers and concurrent clients. Native checks cover all four MXCSR rounding modes; Wasm checks
its supported nearest mode. `PS2X_CPU_RASTER_THREADS=1` selects the serial diagnostic path.
`PS2X_CPU_ASYNC=0` disables the ordered queue; `PS2X_CPU_RASTER_COLUMNS=1..4` and
`PS2X_CPU_RASTER_BATCH_AREA=65536..16777216` support private scheduling comparisons. Batch area
is the sum of primitive bounding-box pixel areas; the default is 1,048,576 pixels. Larger limits
showed no gain in the tested view. Detailed raster logging materially affects browser throughput.

## Validation and limits

For stricter measurements, set `DRAKAN_FRAME_METRICS=1` before startup and leave
`DRAKAN_FRAME_AUDIT=0`. Browser harnesses use `?metrics=1`; this adds no player UI.
These observations do not modify the guest timestep.
Native runs emit one `[frame-metrics]` line per second. Browser builds export
`Module._drakan_web_game_seconds()` and `Module._drakan_web_frame_metric(index)`:
indices 0..7 count completed-frame intervals bounded by 16.666667, 25, 30,
33.333334, 40, 50, 100 ms and infinity; 8 counts game updates, 9 invalid timing
observations, 10 unique successful software-frame texture uploads, 11 failed
frame copies, and 12 indicates enabled collection. Counters reset at game startup;
`Module._drakan_web_upload_metric(index)` exposes the eight corresponding upload
interval buckets (native lines append `uploadBins`).
Take two snapshots in steady gameplay, outside menus, loading and movies.
`scripts/perf/software_frame_report.py` documents the JSON receipt schema and
requires at least 20 seconds, 30 completed frames/uploads per second, game time
within 1% of wall time, at most 5% late render/upload intervals and no invalid/copy errors.
It rejects incomplete evidence and limits a pass to the recorded scene. Texture
uploads measure submission to the graphics API, not physical display scanout.
The Linux/browser host presentation poll limit is 120 Hz to avoid delaying completed
images behind the old 60 Hz limiter. This does not set the guest update rate;
`DRAKAN_SOFTWARE_PRESENT_FPS=30..240` is a private comparison override. Windows
presentation retains its existing behavior.
`PS2X_CPU_RASTER_THREADS=1..16` supports worker-count experiments, bounded by
available CPUs. Browsers reporting at least sixteen logical CPUs default to
sixteen raster threads. Linux reserves capacity for EE, resource/audio and host
presentation work: four raster threads on an eight-CPU host for native textures,
six when an HD pack is active, scaling to eight on larger hosts. Windows retains its previous default. The browser preallocates
24 pthread workers for the renderer and other runtime services.

Recorded clean engineering windows on this Ryzen 7 9800X3D host are below. Each
window is about 30 seconds; the Linux VM was changed to eight CPUs with explicit
user approval. The original-speed target is at least 30 completed/uploaded game
frames per second, with game time tracking wall time.

| Target / scene | Completed fps | Unique uploads/s | Late uploads | Game/wall time |
| --- | ---: | ---: | ---: | ---: |
| Linux, Surdana after walking, balanced default | 42.25 | 42.15 | 0.08% | 0.99997 |
| Linux, Shadowmire diagnostic entry, balanced default | 52.46 | 52.46 | 1.27% | 0.99971 |
| Browser, Surdana grass | 36.15 | 36.15 | 0.28% | 0.99964 |
| Browser, walking then stationary | 39.64 | 39.64 | 0% | 0.99907 |
| Browser, reverse camera view | 60.01 | 59.98 | 0% | 0.99938 |

These receipts predate the final game-only browser shell and its automatic
file-close persistence checks; exact binaries and windows are recorded in the
performance journal. The final browser workflow is verified for the original
menus, gameplay mouse capture, fullscreen and in-game save/reload. Its latest
instrumented runs were competing with Call of Duty (roughly six CPU cores of
activity). The progressive-scanout/local-clock browser build measured 21.32
completed/uploaded fps with HD textures over 39.25 seconds, game/wall 1.00020,
and failed the pacing gate. That sample is not presented as a clean benchmark;
an uncontended rerun is pending.

The receipts do not establish a minimum frame rate throughout the entire game
or across other hardware/browsers. Full-game performance/fidelity still requires
broader playthrough and reference coverage. Shadowmire used the diagnostic level
redirect, not a normal story transition. The original PAL movie path retains
25 fps: intro images advanced and audio ran near 48 kHz without ring overflows
in the checked movie window. Saving through the game, reloading the browser and
loading the new slot succeeded; existing slots retained their file hashes.

- Linux runtime/policy suite: 35 tests passed and two disc-dependent tests skipped, including coverage
  for ISO byte sources, ALSA/silent audio, input publication, host paths, VU math, completed-frame
  selection, ordered rendering and complete-VRAM renderer comparisons.
- Windows: the complete ISO build has linked/deployed and booted through GSR; nine focused host,
  VU, frame-clock, renderer, cadence and pose checks pass. This does not assert that every unrelated
  test in the working tree passes.
- Wasm: the focused checks pass with the pinned SDK: SIMD, VU rounding, vector exceptions, retained
  input/read-only frame counter and shared GLFW clock, worker ISO transport, frame-clock selection, raster math,
  complete-VRAM renderer parity and ordered CPU rendering. The shared input/camera suite also passes
  3,415 checks in Wasm. The math/VRAM comparisons include
  common opaque/transparent shader states and exact display composition. All 118 Python/server/persistence
  checks pass. The subsequent working-copy repository guard passes, including the
  pinned `third_party/nvidia/dlss/nvngx_dlss.dll` hash.
- No full-game completion/playthrough, controller hardware matrix, ARM Linux, Safari/Firefox matrix,
  or GPU renderer parity is established by those checks. Linux/browser default to software rendering
  (the experimental GPU path is described above) and
  do not provide D3D11, DLSS, DXR or HD movie-pack parity. The shared modern
  title, pause, save/load and PC Settings screens now have a portable raylib
  backend. Unsupported renderer settings are disabled; modern inventory/map
  overlays and full feature parity remain unfinished.
- The browser launcher is a local desktop player. Arbitrary third-party hosting, an unisolated page,
  and mobile/touch-only play are not implemented.

The original audit remains in
[linux-browser-readiness.md](../journal/2026-10-25/linux-browser-readiness.md); implementation and live
validation notes are recorded in the subsequent dated journal entries.

Performance iterations and private-run methodology are recorded in
[linux-web-performance.md](../journal/2026-10-26/linux-web-performance.md).
